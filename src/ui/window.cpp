#include "window.h"
#include "../chromium/client.h"
#include "../chromium/pages.h"
#include "../reset/cleanup.h"
#include <cmath>
#include "../navigation/address.h"
#include "include/cef_frame.h"
#include "include/cef_app.h"
#include <algorithm>
#include <iterator>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

namespace aqua {
namespace {
constexpr UINT close_browser_window = WM_APP + 1;
constexpr UINT apply_settings = WM_APP + 2;
struct Palette {
    COLORREF background, toolbar, active, inactive, text, muted, accent, border;
};

constexpr Palette ocean{RGB(11, 22, 40), RGB(20, 36, 58), RGB(32, 59, 89),
    RGB(26, 46, 69), RGB(230, 241, 255), RGB(140, 160, 179), RGB(0, 184, 230), RGB(43, 61, 82)};
constexpr Palette contrast{RGB(7, 16, 28), RGB(18, 36, 58), RGB(19, 76, 103),
    RGB(25, 48, 73), RGB(255, 255, 255), RGB(210, 227, 244), RGB(85, 216, 255), RGB(102, 135, 167)};

const Palette& Colors(Theme theme) {
    return theme == Theme::contrast ? contrast : ocean;
}

bool IsSettings(std::wstring_view address) {
    return address == L"aqua://settings" || address.starts_with(L"aqua://settings/");
}

void Fill(HDC dc, RECT area, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Rounded(HDC dc, RECT area, int radius, COLORREF color) {
    SetDCBrushColor(dc, color);
    RoundRect(dc, area.left, area.top, area.right, area.bottom, radius, radius);
}

void Label(HDC dc, RECT area, const std::wstring& value, COLORREF color, UINT flags) {
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &area,
        flags | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}
}

bool Window::Create(HINSTANCE instance, int show, const std::filesystem::path& profile) {
    profile_ = profile;
    preferences_ = ReadPreferences(profile_);
    UpdateAquaPageSettings(preferences_);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = Procedure;
    wc.lpszClassName = L"AquaFrame.Main";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return false;

    hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Aqua Frame",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        1180, 820, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    if (!edit_) {
        DestroyWindow(hwnd_);
        return false;
    }
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));
    extensions_ = new ExtensionManager(hwnd_, [this](const std::wstring& url) { AddTab(url); });
    ShowWindow(hwnd_, show);
    const auto marker = profile_ / first_run_marker;
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        AddTab(L"aqua://welcome/");
        if (!tabs_.empty()) DeleteFileW(marker.c_str());
    } else if (preferences_.restore_session) {
        const auto count = preferences_.session.size();
        const auto first = preferences_.limit_restored_tabs && count > 8 ? count - 8 : 0;
        for (auto i = first; i < count; ++i) AddTab(preferences_.session[i]);
    }
    if (tabs_.empty()) AddTab(preferences_.startup_page == StartupPage::settings
        ? L"aqua://settings/" : L"aqua://newtab/");
    return !tabs_.empty();
}

LRESULT CALLBACK Window::Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->Message(message, w, l) : DefWindowProcW(hwnd, message, w, l);
}

LRESULT CALLBACK Window::AddressProcedure(HWND hwnd, UINT msg, WPARAM w, LPARAM l,
                                           UINT_PTR, DWORD_PTR data) {
    auto& self = *reinterpret_cast<Window*>(data);
    if (msg == WM_KEYDOWN) {
        if (w == VK_RETURN) {
            self.Navigate();
            return 0;
        }
        const int modifiers = (GetKeyState(VK_CONTROL) < 0 ? EVENTFLAG_CONTROL_DOWN : 0) |
                              (GetKeyState(VK_SHIFT) < 0 ? EVENTFLAG_SHIFT_DOWN : 0);
        if (self.Shortcut(static_cast<int>(w), modifiers)) return 0;
    }
    if (msg == WM_CHAR && (w == VK_RETURN || w == 12 || w == 20 || w == 23)) return 0;
    return DefSubclassProc(hwnd, msg, w, l);
}

LRESULT Window::Message(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        dpi_ = GetDpiForWindow(hwnd_);
        address_brush_ = CreateSolidBrush(Colors(preferences_.theme).background);
        edit_ = CreateWindowExW(0, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
            hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (edit_) {
            SetWindowSubclass(edit_, AddressProcedure, 1, reinterpret_cast<DWORD_PTR>(this));
            SendMessageW(edit_, EM_SETLIMITTEXT, 8192, 0);
            SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
                reinterpret_cast<LPARAM>(L"Введите поисковый запрос или URL"));
            Layout();
        }
        return 0;
    case WM_SIZE:
        if (w != SIZE_MINIMIZED) Layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(l);
        info->ptMinTrackSize = {Scale(450), Scale(280)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(w);
        const auto& r = *reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        Layout();
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor(reinterpret_cast<HDC>(w), Colors(preferences_.theme).text);
        SetBkColor(reinterpret_cast<HDC>(w), Colors(preferences_.theme).background);
        return reinterpret_cast<LRESULT>(address_brush_);
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: Paint(); return 0;
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (point.y < Scale(36)) {
            for (size_t i = 0; i < tabs_.size(); ++i) {
                const auto r = TabBounds(i);
                if (!PtInRect(&r, point)) continue;
                if (point.x >= r.right - Scale(26)) CloseTab(tabs_[i].id, false);
                else Select(tabs_[i].id);
                return 0;
            }
            const auto last = TabBounds(tabs_.empty() ? 0 : tabs_.size() - 1);
            if (tabs_.empty() || (point.x >= last.right && point.x < last.right + Scale(34))) AddTab();
        } else if (point.y < Scale(76)) {
            RECT r{};
            GetClientRect(hwnd_, &r);
            if (point.x >= r.right - Scale(44)) Menu();
            else if (point.x >= r.right - Scale(78)) OpenExtensions();
            else if (auto b = Current()) {
                if (point.x < Scale(38)) b->GoBack();
                else if (point.x < Scale(70)) b->GoForward();
                else if (point.x < Scale(102)) b->Reload();
            }
        }
        return 0;
    }
    case WM_SETFOCUS:
        if (edit_) SetFocus(edit_);
        return 0;
    case close_browser_window:
        if (auto* tab = Find(static_cast<uint64_t>(w)); tab && tab->browser) {
            const auto child = tab->browser->GetHost()->GetWindowHandle();
            if (child && IsChild(hwnd_, child)) DestroyWindow(child);
        }
        return 0;
    case apply_settings:
        ApplySettingsAction(static_cast<uint64_t>(w), static_cast<SettingsAction>(l));
        return 0;
    case WM_CLOSE:
        if (!exiting_) {
            if (!reset_requested_) SaveSession();
            exiting_ = true;
            extensions_->Close();

            std::vector<uint64_t> ids;
            for (const auto& tab : tabs_) ids.push_back(tab.id);
            for (auto id : ids) CloseTab(id, true);
        }
        if (tabs_.empty() && !extensions_->HasBrowsers()) DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        if (font_) DeleteObject(font_);
        if (address_brush_) DeleteObject(address_brush_);
        CefQuitMessageLoop();
        return 0;
    default:
        return DefWindowProcW(hwnd_, msg, w, l);
    }
}

int Window::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

RECT Window::TabBounds(size_t index) const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const long room = std::max(1L, client.right - Scale(55));
    const long width = std::clamp(room / static_cast<long>(std::max(size_t{1}, tabs_.size())),
                                  static_cast<long>(Scale(44)), static_cast<long>(Scale(190)));
    const long start = Scale(8) + static_cast<long>(index) * width;
    return {start, Scale(4), start + width - Scale(2), Scale(36)};
}

Window::Tab* Window::Find(uint64_t id) {
    const auto it = std::ranges::find(tabs_, id, &Tab::id);
    return it == tabs_.end() ? nullptr : &*it;
}

CefRefPtr<CefBrowser> Window::Current() {
    if (auto* tab = Find(selected_); tab && !tab->closing) return tab->browser;
    return nullptr;
}

void Window::Layout() {
    if (!edit_) return;
    RECT r{};
    GetClientRect(hwnd_, &r);
    const auto new_font = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(new_font), FALSE);
    if (font_) DeleteObject(font_);
    font_ = new_font;
    const auto* tab = Find(selected_);
    const int address_left = Scale(tab && IsSettings(tab->url) ? 236 : 112);
    MoveWindow(edit_, address_left, Scale(47), std::max(0L, r.right - address_left - Scale(92)), Scale(22), TRUE);
    if (auto browser = Current()) {
        if (const auto child = browser->GetHost()->GetWindowHandle())
            MoveWindow(child, 0, Scale(76), r.right, std::max(0L, r.bottom - Scale(76)), TRUE);
    }
    RECT top{0, 0, r.right, Scale(76)};
    InvalidateRect(hwnd_, &top, FALSE);
}

void Window::Paint() {
    const auto& colors = Colors(preferences_.theme);
    const auto* selected = Find(selected_);
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT r{};
    GetClientRect(hwnd_, &r);
    Fill(dc, ps.rcPaint, colors.background);
    SetBkMode(dc, TRANSPARENT);
    const auto old_font = SelectObject(dc, font_);
    const auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Fill(dc, {0, Scale(36), r.right, Scale(76)}, colors.toolbar);
    Rounded(dc, {Scale(101), Scale(42), r.right - Scale(81), Scale(71)}, Scale(8), colors.border);
    Rounded(dc, {Scale(102), Scale(43), r.right - Scale(82), Scale(70)}, Scale(8), colors.background);
    if (selected && IsSettings(selected->url)) {
        Label(dc, {Scale(113), Scale(43), Scale(130), Scale(70)}, L"A", colors.accent, DT_CENTER);
        Label(dc, {Scale(135), Scale(43), Scale(222), Scale(70)}, L"Aqua Frame", colors.text, DT_END_ELLIPSIS);
        Fill(dc, {Scale(224), Scale(49), Scale(225), Scale(64)}, colors.border);
    }
    for (size_t i = 0; i < tabs_.size(); ++i) {
        RECT tab = TabBounds(i);
        const bool current = tabs_[i].id == selected_;
        Rounded(dc, tab, Scale(8), current ? colors.active : colors.inactive);
        RECT title = tab;
        title.left += Scale(10);
        title.right -= Scale(26);
        const auto title_text = IsSettings(tabs_[i].url) ? L"⚙  " + tabs_[i].title : tabs_[i].title;
        Label(dc, title, title_text, current ? colors.text : colors.muted, DT_END_ELLIPSIS);
        Label(dc, {tab.right - Scale(25), tab.top, tab.right, tab.bottom}, L"×", colors.text, DT_CENTER);
    }
    const long plus = tabs_.empty() ? Scale(8) : TabBounds(tabs_.size() - 1).right;
    Label(dc, {plus, 0, plus + Scale(34), Scale(36)}, L"+", colors.accent, DT_CENTER);
    Label(dc, {Scale(6), Scale(36), Scale(38), Scale(76)}, L"←", colors.text, DT_CENTER);
    Label(dc, {Scale(38), Scale(36), Scale(70), Scale(76)}, L"→", colors.text, DT_CENTER);
    Label(dc, {Scale(70), Scale(36), Scale(102), Scale(76)}, L"↻", colors.text, DT_CENTER);
    // Outline the extension control with the same 16-DIP stroke icon as the
    // settings sidebar, using the native toolbar paint path.
    constexpr POINT puzzle[]{{1, 4}, {6, 4}, {6, 2}, {7, 1}, {9, 1}, {10, 2},
        {10, 4}, {15, 4}, {15, 8}, {17, 8}, {18, 9}, {18, 11}, {17, 12},
        {15, 12}, {15, 16}, {10, 16}, {10, 14}, {9, 13}, {7, 13}, {6, 14},
        {6, 16}, {1, 16}, {1, 12}, {3, 12}, {4, 11}, {4, 9}, {3, 8}, {1, 8}, {1, 4}};
    POINT icon[std::size(puzzle)]{};
    for (size_t i = 0; i < std::size(puzzle); ++i) {
        icon[i] = {r.right - Scale(70) + Scale(puzzle[i].x), Scale(47) + Scale(puzzle[i].y)};
    }
    const auto icon_pen = CreatePen(PS_SOLID, std::max(1, Scale(1)), colors.text);
    if (icon_pen) {
        SelectObject(dc, icon_pen);
        Polyline(dc, icon, static_cast<int>(std::size(icon)));
        SelectObject(dc, GetStockObject(NULL_PEN));
        DeleteObject(icon_pen);
    }
    Label(dc, {r.right - Scale(44), Scale(36), r.right, Scale(76)}, L"☰", colors.text, DT_CENTER);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_font);
    EndPaint(hwnd_, &ps);
}

void Window::AddTab(const std::wstring& url) {
    RECT r{};
    GetClientRect(hwnd_, &r);
    if (exiting_ || tabs_.size() >= 64) return;
    const auto previous = selected_;
    const auto id = next_id_++;
    tabs_.push_back({id, L"Новая вкладка", url, nullptr});
    Select(id);
    CefWindowInfo info;
    info.SetAsChild(hwnd_, CefRect(0, Scale(76), r.right, std::max(0L, r.bottom - Scale(76))));
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings settings;
    const auto bg = Colors(preferences_.theme).background;
    settings.background_color = CefColorSetARGB(255, GetRValue(bg), GetGValue(bg), GetBValue(bg));
    if (!CefBrowserHost::CreateBrowser(info, new Client(*this, id), url, settings, nullptr, nullptr)) {
        tabs_.pop_back();
        Select(previous);
    }
}

void Window::BrowserCreated(uint64_t id, CefRefPtr<CefBrowser> browser) {
    auto* tab = Find(id);
    if (!tab) {
        browser->GetHost()->CloseBrowser(true);
        return;
    }
    tab->browser = browser;
    ApplyZoom(browser, tab->url);
    if (tab->closing) {
        browser->GetHost()->CloseBrowser(true);
        return;
    }
    ShowWindow(browser->GetHost()->GetWindowHandle(), id == selected_ ? SW_SHOWNA : SW_HIDE);
    Layout();
}

bool Window::QueueBrowserClose(uint64_t id) {
    // CEF's default notification targets the shared top-level window. Close
    // only this tab's child HWND, after the DoClose callback has returned.
    return PostMessageW(hwnd_, close_browser_window, static_cast<WPARAM>(id), 0) != FALSE;
}

void Window::BrowserClosed(uint64_t id) {
    if (const auto* tab = Find(id); tab && !exiting_) {
        if (closed_.size() == 32) closed_.erase(closed_.begin());
        closed_.push_back(tab->url);
    }
    std::erase_if(tabs_, [id](const Tab& tab) { return tab.id == id; });
    if (tabs_.empty()) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
    if (selected_ == id && !exiting_) Select(tabs_.back().id);
    Layout();
}

void Window::CloseTab(uint64_t id, bool force) {
    auto* tab = Find(id);
    if (!tab) return;
    tab->closing = force || !tab->browser;
    if (tab->browser) tab->browser->GetHost()->CloseBrowser(force);
}

void Window::Select(uint64_t id) {
    if (auto old = Current()) {
        if (selected_ != id) ShowWindow(old->GetHost()->GetWindowHandle(), SW_HIDE);
    }
    selected_ = id;
    if (auto* tab = Find(id)) {
        SetWindowTextW(edit_, tab->url == L"aqua://newtab/" ? L"" : tab->url.c_str());
        if (tab->browser) ShowWindow(tab->browser->GetHost()->GetWindowHandle(), SW_SHOWNA);
    }
    Layout();
}

void Window::AddressChanged(uint64_t id, const CefString& address) {
    auto* tab = Find(id);
    if (!tab) return;
    const auto was_settings = IsSettings(tab->url);
    tab->url = address.ToWString();
    if (id == selected_ && was_settings != IsSettings(tab->url)) Layout();
    if (tab->browser) ApplyZoom(tab->browser, tab->url);
    if (id == selected_ && GetFocus() != edit_)
        SetWindowTextW(edit_, tab->url == L"aqua://newtab/" ? L"" : tab->url.c_str());
}

void Window::TitleChanged(uint64_t id, const CefString& title) {
    if (auto* tab = Find(id)) {
        tab->title = title.ToWString().substr(0, 160);
        RECT r{};
        GetClientRect(hwnd_, &r);
        r.bottom = Scale(36);
        InvalidateRect(hwnd_, &r, FALSE);
    }
}

void Window::Navigate() {
    const int length = GetWindowTextLengthW(edit_);
    std::wstring address(static_cast<size_t>(length) + 1, L'\0');
    address.resize(GetWindowTextW(edit_, address.data(), length + 1));
    if (address == L"aqua://extensions" || address == L"aqua://extensions/") {
        OpenExtensions();
        return;
    }
    auto browser = Current();
    if (!browser) return;
    if (address == L"aqua://settings" || address == L"aqua://settings/" ||
        address.starts_with(L"aqua://settings/#")) {
        browser->GetHost()->SetFocus(true);
        browser->GetMainFrame()->LoadURL(address == L"aqua://settings" ? L"aqua://settings/" : address);
        return;
    }
    const auto url = ResolveAddress(address, preferences_.search);
    if (url.empty()) return;
    SetWindowTextW(edit_, url.c_str());
    browser->GetHost()->SetFocus(true);
    browser->GetMainFrame()->LoadURL(url);
}

bool Window::Shortcut(int key, int modifiers) {
    if (!(modifiers & EVENTFLAG_CONTROL_DOWN)) return false;
    if (key == 'L') {
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        return true;
    }
    if (key == 'T') {
        if (modifiers & EVENTFLAG_SHIFT_DOWN) {
            if (!closed_.empty()) {
                auto url = std::move(closed_.back());
                closed_.pop_back();
                AddTab(url);
            }
        } else {
            AddTab();
        }
        return true;
    }
    if (key == 'W') {
        CloseTab(selected_, false);
        return true;
    }
    if (key == 'E' && (modifiers & EVENTFLAG_SHIFT_DOWN)) {
        OpenExtensions();
        return true;
    }
    if (key == 'R') {
        if (auto b = Current()) b->Reload();
        return true;
    }
    return false;
}

CefRefPtr<CefClient> Window::DefaultChromeClient() const {
    return extensions_;
}

void Window::OpenExtensions() {
    if (!exiting_ && !extensions_->Open()) {
        MessageBoxW(hwnd_, L"Не удалось открыть менеджер расширений.", L"Aqua Frame", MB_OK | MB_ICONERROR);
    }
}

void Window::QueueSettingsAction(uint64_t tab, SettingsAction action) {
    PostMessageW(hwnd_, apply_settings, static_cast<WPARAM>(tab), static_cast<LPARAM>(action));
}

void Window::ApplyZoom(const CefRefPtr<CefBrowser>& browser, std::wstring_view url) {
    const bool local = url.starts_with(L"aqua://");
    const auto zoom = local ? 0 : std::log(preferences_.zoom / 100.0) / std::log(1.2);
    browser->GetHost()->SetZoomLevel(zoom);
}

void Window::SaveSession() {
    if (!preferences_.restore_session) return;
    preferences_.session.clear();
    for (const auto& tab : tabs_) {
        if (!tab.closing && !tab.url.empty() && IsSessionAddress(tab.url))
            preferences_.session.push_back(tab.url);
    }
    if (!WritePreferences(profile_, preferences_))
        MessageBoxW(hwnd_, L"Не удалось сохранить открытые вкладки.", L"Aqua Frame", MB_OK | MB_ICONWARNING);
}

void Window::ApplySettingsAction(uint64_t id, SettingsAction action) {
    const auto* tab = Find(id);
    if (exiting_ || !tab || id != selected_ || !tab->browser || tab->closing ||
        !(tab->url == L"aqua://settings/" || tab->url.starts_with(L"aqua://settings/#"))) return;

    if (action == SettingsAction::open_extensions) {
        OpenExtensions();
        return;
    }
    if (action == SettingsAction::open_downloads) {
        PWSTR folder = nullptr;
        bool opened = false;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, nullptr, &folder))) {
            opened = reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_, L"open", folder,
                nullptr, nullptr, SW_SHOWNORMAL)) > 32;
            CoTaskMemFree(folder);
        }
        if (!opened) MessageBoxW(hwnd_, L"Не удалось открыть папку загрузок.", L"Aqua Frame", MB_OK | MB_ICONERROR);
        return;
    }
    if (action == SettingsAction::factory_reset) {
        const auto confirm = MessageBoxW(hwnd_,
            L"Удалить весь локальный профиль Aqua Frame?\n\n"
            L"История, авторизации, данные сайтов, расширения, пароли и настройки будут удалены. "
            L"Файлы в папке загрузок сохранятся. Это действие необратимо.\n\n"
            L"Браузер автоматически перезапустится.",
            L"Полный заводской сброс Aqua Frame", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING);
        if (confirm == IDYES) {
            reset_requested_ = true;
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
        return;
    }

    auto next = preferences_;
    switch (action) {
    case SettingsAction::restore_on: next.restore_session = true; break;
    case SettingsAction::restore_off: next.restore_session = false; next.session.clear(); break;
    case SettingsAction::google: next.search = SearchEngine::google; break;
    case SettingsAction::bing: next.search = SearchEngine::bing; break;
    case SettingsAction::duckduckgo: next.search = SearchEngine::duckduckgo; break;
    case SettingsAction::zoom_90: next.zoom = 90; break;
    case SettingsAction::zoom_100: next.zoom = 100; break;
    case SettingsAction::zoom_110: next.zoom = 110; break;
    case SettingsAction::zoom_125: next.zoom = 125; break;
    case SettingsAction::startup_new_tab: next.startup_page = StartupPage::new_tab; break;
    case SettingsAction::startup_settings: next.startup_page = StartupPage::settings; break;
    case SettingsAction::theme_ocean: next.theme = Theme::ocean; break;
    case SettingsAction::theme_contrast: next.theme = Theme::contrast; break;
    case SettingsAction::limit_restore_on: next.limit_restored_tabs = true; break;
    case SettingsAction::limit_restore_off: next.limit_restored_tabs = false; break;
    case SettingsAction::reset_preferences: next = Preferences{}; break;
    default: return;
    }
    if (!WritePreferences(profile_, next)) {
        MessageBoxW(hwnd_, L"Не удалось сохранить настройки.", L"Aqua Frame", MB_OK | MB_ICONERROR);
        return;
    }
    const bool theme_changed = next.theme != preferences_.theme;
    preferences_ = std::move(next);
    if (theme_changed) {
        const auto brush = CreateSolidBrush(Colors(preferences_.theme).background);
        if (brush) {
            DeleteObject(address_brush_);
            address_brush_ = brush;
        }
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    UpdateAquaPageSettings(preferences_);
    for (const auto& item : tabs_) {
        if (!item.browser || item.closing) continue;
        ApplyZoom(item.browser, item.url);
        if (item.url.starts_with(L"aqua://settings/") || item.url == L"aqua://newtab/")
            item.browser->ReloadIgnoreCache();
    }
}

void Window::Menu() {
    const auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Новая вкладка\tCtrl+T");
    AppendMenuW(menu, MF_STRING, 2, L"Восстановить вкладку\tCtrl+Shift+T");
    AppendMenuW(menu, MF_STRING, 3, L"Закрыть вкладку\tCtrl+W");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 6, L"Настройки");
    AppendMenuW(menu, MF_STRING, 5, L"Расширения — перетащить файл\tCtrl+Shift+E");
    AppendMenuW(menu, MF_STRING, 4, L"О браузере Aqua Frame");
    POINT pos{};
    GetCursorPos(&pos);
    const auto action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_NONOTIFY,
                                       pos.x, pos.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (action == 1) AddTab();
    if (action == 2) Shortcut('T', EVENTFLAG_CONTROL_DOWN | EVENTFLAG_SHIFT_DOWN);
    if (action == 3) CloseTab(selected_, false);
    if (action == 5) OpenExtensions();
    if (action == 6) AddTab(L"aqua://settings/");
    if (action == 4) MessageBoxW(hwnd_, L"Aqua Frame 1.0.0\nAqua Deep Ocean\nBeyond the ordinary.",
                                 L"Aqua Frame", MB_OK | MB_ICONINFORMATION);
}
}
