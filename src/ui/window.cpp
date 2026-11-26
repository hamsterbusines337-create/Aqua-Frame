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
#include <utility>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

namespace aqua {
namespace {
constexpr UINT close_browser_window = WM_APP + 1;
constexpr UINT apply_settings = WM_APP + 2;
constexpr UINT process_script_action = WM_APP + 3;
constexpr UINT upgrade_navigation = WM_APP + 4;
constexpr UINT_PTR history_timer = 1;
constexpr size_t library_limit = 128;
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
    scripts_.Load(profile_);
    UpdateAquaScriptList(scripts_.Items());
    UpdateAquaPageSettings(preferences_);
    RefreshLibrary();
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
    extensions_ = new ExtensionManager(hwnd_, [this](const std::wstring& url) { AddTab(url); },
        [this] { OpenScriptManager(); });
    DragAcceptFiles(hwnd_, TRUE);
    ShowWindow(hwnd_, show);
    const auto marker = profile_ / first_run_marker;
    if (GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES) {
        AddTab(L"aqua://welcome/");
        if (!tabs_.empty()) DeleteFileW(marker.c_str());
    } else if (preferences_.restore_session) {
        const auto count = preferences_.session.size();
        const auto first = preferences_.limit_restored_tabs && count > 8 ? count - 8 : 0;
        for (auto i = first; i < count; ++i) AddTab(preferences_.session[i], false, i + 1 < count);
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
    case WM_TIMER:
        if (w == history_timer) {
            KillTimer(hwnd_, history_timer);
            if (history_dirty_ && WritePreferences(profile_, preferences_)) history_dirty_ = false;
        }
        return 0;
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
            else if (point.x >= r.right - Scale(106)) ToggleBookmark();
            else if (auto b = Current()) {
                if (point.x < Scale(38)) b->GoBack();
                else if (point.x < Scale(70)) b->GoForward();
                else if (point.x < Scale(102)) b->Reload();
            }
        } else if (point.y < ContentTop() && point.x >= Scale(8)) {
            const auto index = static_cast<size_t>((point.x - Scale(8)) / Scale(132));
            if (index < preferences_.bookmarks.size() && index < 8) {
                if (auto browser = Current()) browser->GetMainFrame()->LoadURL(preferences_.bookmarks[index].url);
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
    case WM_DROPFILES: {
        const auto drop = reinterpret_cast<HDROP>(w);
        std::filesystem::path candidate;
        if (DragQueryFileW(drop, 0xffffffff, nullptr, 0) == 1) {
            const auto length = DragQueryFileW(drop, 0, nullptr, 0);
            if (length && length < 32768) {
                std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
                if (DragQueryFileW(drop, 0, buffer.data(), length + 1) == length) {
                    buffer.resize(length);
                    candidate = buffer;
                }
            }
        }
        DragFinish(drop);
        if (!exiting_ && !candidate.empty() &&
            CompareStringOrdinal(candidate.extension().c_str(), -1, L".js", -1, TRUE) == CSTR_EQUAL)
            ImportScript(candidate);
        return 0;
    }
    case process_script_action:
        RunScriptAction();
        return 0;
    case upgrade_navigation: {
        auto request = https_upgrades_.extract(static_cast<uint64_t>(w));
        if (request && !exiting_) {
            if (const auto* tab = Find(request.key()); tab && tab->browser && !tab->closing)
                tab->browser->GetMainFrame()->LoadURL(request.mapped());
        }
        return 0;
    }
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
        KillTimer(hwnd_, history_timer);
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
    if (dpi_ != font_dpi_) {
        const auto new_font = CreateFontW(-Scale(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        if (new_font) {
            SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(new_font), FALSE);
            if (font_) DeleteObject(font_);
            font_ = new_font;
            font_dpi_ = dpi_;
        }
    }
    const auto* tab = Find(selected_);
    const int address_left = Scale(tab && IsSettings(tab->url) ? 236 : 112);
    ShowWindow(edit_, fullscreen_ ? SW_HIDE : SW_SHOWNA);
    MoveWindow(edit_, address_left, Scale(47), std::max(0L, r.right - address_left - Scale(118)), Scale(22), TRUE);
    const int top = ContentTop();
    if (auto browser = Current()) {
        if (const auto child = browser->GetHost()->GetWindowHandle())
            MoveWindow(child, 0, top, r.right, std::max(0L, r.bottom - top), TRUE);
    }
    RECT chrome{0, 0, r.right, Scale(108)};
    InvalidateRect(hwnd_, &chrome, FALSE);
}

void Window::Paint() {
    const auto& colors = Colors(preferences_.theme);
    const auto* selected = Find(selected_);
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT r{};
    GetClientRect(hwnd_, &r);
    Fill(dc, ps.rcPaint, colors.background);
    if (fullscreen_) {
        EndPaint(hwnd_, &ps);
        return;
    }
    SetBkMode(dc, TRANSPARENT);
    const auto old_font = SelectObject(dc, font_);
    const auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Fill(dc, {0, Scale(36), r.right, Scale(76)}, colors.toolbar);
    Rounded(dc, {Scale(101), Scale(42), r.right - Scale(109), Scale(71)}, Scale(8), colors.border);
    Rounded(dc, {Scale(102), Scale(43), r.right - Scale(110), Scale(70)}, Scale(8), colors.background);
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
        const auto title_text = tabs_[i].private_tab ? L"◈  " + tabs_[i].title :
            IsSettings(tabs_[i].url) ? L"⚙  " + tabs_[i].title : tabs_[i].title;
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
    const bool bookmarked = selected && !selected->private_tab &&
        std::ranges::find(preferences_.bookmarks, selected->url, &PageEntry::url) != preferences_.bookmarks.end();
    Label(dc, {r.right - Scale(106), Scale(36), r.right - Scale(78), Scale(76)},
          bookmarked ? L"★" : L"☆", bookmarked ? colors.accent : colors.text, DT_CENTER);
    if (ContentTop() > Scale(76)) {
        Fill(dc, {0, Scale(76), r.right, Scale(108)}, colors.background);
        for (size_t i = 0; i < preferences_.bookmarks.size() && i < 8; ++i) {
            const auto x = Scale(8 + static_cast<int>(i) * 132);
            if (x >= r.right) break;
            const auto& entry = preferences_.bookmarks[i];
            Label(dc, {x, Scale(76), std::min(r.right, long(x + Scale(124))), Scale(108)},
                  entry.title.empty() ? entry.url : entry.title, colors.text, DT_END_ELLIPSIS);
        }
        if (preferences_.bookmarks.empty())
            Label(dc, {Scale(8), Scale(76), r.right, Scale(108)}, L"Ctrl+D — добавить закладку", colors.muted, DT_END_ELLIPSIS);
    }
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_font);
    EndPaint(hwnd_, &ps);
}

void Window::AddTab(const std::wstring& url, bool private_tab, bool deferred) {
    if (exiting_ || tabs_.size() >= 64) return;
    if (private_tab && !private_context_) {
        CefRequestContextSettings isolated;
        auto context = CefRequestContext::CreateContext(isolated, nullptr);
        if (!context || !RegisterAquaPages(context)) return;
        private_context_ = std::move(context);
    }
    const auto id = next_id_++;
    const auto title = deferred && url != L"aqua://newtab/" ? url :
        std::wstring(private_tab ? L"Инкогнито" : L"Новая вкладка");
    tabs_.push_back({id, title, url, nullptr, false, private_tab, true});
    if (!deferred) Select(id);
}

bool Window::StartTab(Tab& tab) {
    RECT r{};
    GetClientRect(hwnd_, &r);
    CefWindowInfo info;
    const auto top = ContentTop();
    info.SetAsChild(hwnd_, CefRect(0, top, r.right, std::max(0L, r.bottom - top)));
    info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    CefBrowserSettings settings;
    const auto bg = Colors(preferences_.theme).background;
    settings.background_color = CefColorSetARGB(255, GetRValue(bg), GetGValue(bg), GetBValue(bg));
    if (!CefBrowserHost::CreateBrowser(info, new Client(*this, tab.id, tab.private_tab), tab.url, settings,
                                       nullptr, tab.private_tab ? private_context_ : nullptr)) return false;
    tab.deferred = false;
    return true;
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
    https_upgrades_.erase(id);
    if (const auto* tab = Find(id); tab && !exiting_ && !tab->private_tab) {
        if (closed_.size() == 32) closed_.erase(closed_.begin());
        closed_.push_back(tab->url);
    }
    std::erase_if(tabs_, [id](const Tab& tab) { return tab.id == id; });
    if (private_context_ && std::ranges::none_of(tabs_, &Tab::private_tab)) private_context_ = nullptr;
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
    if (tab->deferred) {
        BrowserClosed(id);
        return;
    }
    tab->closing = force || !tab->browser;
    if (tab->browser) tab->browser->GetHost()->CloseBrowser(force);
}

void Window::Select(uint64_t id) {
    if (auto old = Current()) {
        if (selected_ != id) ShowWindow(old->GetHost()->GetWindowHandle(), SW_HIDE);
    }
    selected_ = id;
    if (auto* tab = Find(id)) {
        if (tab->deferred && !StartTab(*tab)) {
            BrowserClosed(id);
            return;
        }
        const auto origin = WebOrigin(CefString(tab->url).ToString());
        if (!tab->private_tab && !origin.empty()) last_web_origin_ = origin;
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
    if (id == selected_) {
        const auto origin = WebOrigin(address.ToString());
        if (!tab->private_tab && !origin.empty()) last_web_origin_ = origin;
        if (was_settings != IsSettings(tab->url)) Layout();
    }
    if (tab->browser) ApplyZoom(tab->browser, tab->url);
    if (id == selected_ && GetFocus() != edit_)
        SetWindowTextW(edit_, tab->url == L"aqua://newtab/" ? L"" : tab->url.c_str());
    if (id == selected_) {
        RECT r{};
        GetClientRect(hwnd_, &r);
        r.bottom = Scale(76);
        InvalidateRect(hwnd_, &r, FALSE);
    }
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
    if (key == VK_F11 || (key == VK_ESCAPE && fullscreen_)) {
        ToggleFullscreen();
        return true;
    }
    if (key == VK_F12) {
        if (auto browser = Current()) {
            CefWindowInfo info;
            info.SetAsPopup(hwnd_, L"Инструменты разработчика Aqua Frame");
            CefBrowserSettings settings;
            browser->GetHost()->ShowDevTools(info, nullptr, settings, CefPoint());
        }
        return true;
    }
    if (!(modifiers & EVENTFLAG_CONTROL_DOWN)) return false;
    const bool shift = (modifiers & EVENTFLAG_SHIFT_DOWN) != 0;
    if (key == 'H') { ShowLibrary(L"history"); return true; }
    if (key == 'J') { ShowLibrary(L"downloads"); return true; }
    if (key == 'D' && !shift) { ToggleBookmark(); return true; }
    if (key == 'B' && shift) { bookmark_bar_ = !bookmark_bar_; Layout(); return true; }
    if (key == 'N' && shift) { AddTab(L"aqua://newtab/", true); return true; }
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
            const auto* tab = Find(selected_);
            AddTab(L"aqua://newtab/", tab && tab->private_tab);
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

int Window::ContentTop() {
    if (fullscreen_) return 0;
    const auto* tab = Find(selected_);
    return Scale(bookmark_bar_ && tab && !tab->private_tab ? 108 : 76);
}

void Window::ToggleFullscreen() {
    if (!fullscreen_) {
        MONITORINFO monitor{sizeof(MONITORINFO)};
        if (!GetWindowPlacement(hwnd_, &windowed_) ||
            !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        fullscreen_ = true;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | WS_VISIBLE);
        const auto& box = monitor.rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, box.left, box.top, box.right - box.left,
                     box.bottom - box.top, SWP_FRAMECHANGED);
    } else {
        fullscreen_ = false;
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE);
        SetWindowPlacement(hwnd_, &windowed_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    Layout();
    if (auto browser = Current()) browser->GetHost()->SetFocus(true);
}

void Window::ShowLibrary(const wchar_t* section) {
    if (const auto* tab = Find(selected_); tab && tab->private_tab) return;
    RefreshLibrary();
    AddTab(std::wstring(L"aqua://library/") + section);
}

void Window::RefreshLibrary() {
    std::vector<std::wstring> states;
    for (const auto& item : downloads_) states.push_back(item.second);
    UpdateAquaLibrary(preferences_.history, preferences_.bookmarks, states);
}

void Window::ScheduleHistorySave() {
    if (!history_dirty_) SetTimer(hwnd_, history_timer, 3500, nullptr);
    history_dirty_ = true;
}

void Window::RecordVisit(uint64_t id, const CefString& address, int) {
    const auto* tab = Find(id);
    const auto url = address.ToWString();
    if (!tab || tab->private_tab || exiting_ || !IsLibraryUrl(url)) return;
    auto& history = preferences_.history;
    std::erase_if(history, [&](const PageEntry& item) { return item.url == url; });
    history.insert(history.begin(), {url, tab->title});
    if (history.size() > library_limit) history.pop_back();
    RefreshLibrary();
    ScheduleHistorySave();
}

void Window::ToggleBookmark() {
    const auto* tab = Find(selected_);
    if (!tab || tab->private_tab || !IsLibraryUrl(tab->url)) return;
    auto next = preferences_;
    if (!std::erase_if(next.bookmarks, [&](const PageEntry& item) { return item.url == tab->url; })) {
        if (next.bookmarks.size() >= library_limit) {
            MessageBoxW(hwnd_, L"Достигнут лимит закладок.", L"Aqua Frame", MB_OK | MB_ICONINFORMATION);
            return;
        }
        next.bookmarks.insert(next.bookmarks.begin(), {tab->url, tab->title});
    }
    if (!WritePreferences(profile_, next)) {
        MessageBoxW(hwnd_, L"Не удалось сохранить закладки.", L"Aqua Frame", MB_OK | MB_ICONERROR);
        return;
    }
    preferences_ = std::move(next);
    RefreshLibrary();
    Layout();
}

void Window::TrackDownload(uint32_t id, const CefString& name, std::wstring state) {
    if (exiting_) return;
    const auto item = std::ranges::find_if(downloads_, [id](const auto& row) { return row.first == id; });
    auto filename = name.ToWString().substr(0, 260);
    if (filename.empty()) filename = L"Файл";
    auto label = filename + L" — " + state;
    if (item != downloads_.end()) {
        if (item->second == label) return;
        item->second = std::move(label);
    } else {
        downloads_.insert(downloads_.begin(), {id, std::move(label)});
        if (downloads_.size() > 64) downloads_.pop_back();
    }
    RefreshLibrary();
}

void Window::UpgradeNavigation(uint64_t tab, const std::string& url) {
    if (exiting_) return;
    https_upgrades_.insert_or_assign(tab, CefString("https://" + url.substr(7)).ToWString());
    PostMessageW(hwnd_, upgrade_navigation, tab, 0);
}

CefRefPtr<CefClient> Window::DefaultChromeClient() const {
    return extensions_;
}

void Window::OpenExtensions() {
    if (!exiting_ && !extensions_->Open()) {
        MessageBoxW(hwnd_, L"Не удалось открыть менеджер расширений.", L"Aqua Frame", MB_OK | MB_ICONERROR);
    }
}

void Window::OpenScriptManager() {
    if (exiting_) return;
    if (const auto* tab = Find(selected_); tab && tab->private_tab) return;
    for (const auto& tab : tabs_) {
        if (!tab.closing && tab.url == L"aqua://settings/#scripts") {
            Select(tab.id);
            SetForegroundWindow(hwnd_);
            return;
        }
    }
    AddTab(L"aqua://settings/#scripts");
    SetForegroundWindow(hwnd_);
}

bool Window::QueueScriptAction(uint64_t tab, std::string_view target) {
    constexpr std::string_view prefix = "aqua://settings/action/script-";
    if (!target.starts_with(prefix)) return false;
    target.remove_prefix(prefix.size());
    const auto slash = target.find('/');
    const auto action = target.substr(0, slash);
    const auto id = slash == std::string_view::npos ? std::string_view{} : target.substr(slash + 1);
    const bool valid = (action == "import" && slash == std::string_view::npos) ||
        ((action == "enable" || action == "disable" || action == "remove") && id.size() == 36 &&
         id.find_first_not_of("0123456789ABCDEFabcdef-") == std::string_view::npos);
    if (valid && !script_action_) {
        script_action_.emplace(PendingScriptAction{tab, std::string(action), std::string(id)});
        if (!PostMessageW(hwnd_, process_script_action, 0, 0)) script_action_.reset();
    }
    return true;
}

void Window::RunScriptAction() {
    if (!script_action_) return;
    auto action = std::move(*script_action_);
    script_action_.reset();
    const auto* tab = Find(action.tab);
    if (exiting_ || !tab || tab->closing || selected_ != action.tab ||
        tab->url != L"aqua://settings/#scripts") return;
    if (action.action == "import") {
        PickScriptFile();
        return;
    }

    auto next = scripts_.Items();
    const auto item = std::ranges::find(next, action.script, &Script::id);
    if (item == next.end()) return;
    if (action.action == "remove") next.erase(item);
    else item->enabled = action.action == "enable";
    if (scripts_.Save(std::move(next))) RefreshScriptSettings();
    else MessageBoxW(hwnd_, L"Не удалось сохранить список скриптов.", L"Aqua Scripts", MB_OK | MB_ICONERROR);
}

void Window::PickScriptFile() {
    if (last_web_origin_.empty()) {
        MessageBoxW(hwnd_, L"Сначала откройте сайт, которому хотите разрешить скрипт.",
                    L"Aqua Scripts", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring path(32768, L'\0');
    OPENFILENAMEW chooser{};
    chooser.lStructSize = sizeof(chooser);
    chooser.hwndOwner = hwnd_;
    chooser.lpstrTitle = L"Импорт JavaScript в Aqua Frame";
    chooser.lpstrFilter = L"JavaScript (*.js; *.user.js)\0*.js\0\0";
    chooser.lpstrFile = path.data();
    chooser.nMaxFile = static_cast<DWORD>(path.size());
    chooser.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
    if (GetOpenFileNameW(&chooser)) ImportScript(path.c_str());
}

void Window::ImportScript(const std::filesystem::path& path) {
    if (const auto* tab = Find(selected_); tab && tab->private_tab) return;
    const auto origin = last_web_origin_;
    if (origin.empty()) {
        MessageBoxW(hwnd_, L"Сначала откройте сайт, которому хотите разрешить скрипт.",
                    L"Aqua Scripts", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Script item;
    if (!scripts_.Add(path, origin, item)) {
        MessageBoxW(hwnd_, L"Нужен файл .js или .user.js в UTF-8, до 512 КБ. "
            L"Поддерживается до 16 скриптов, без GM API, @require и запуска document-start.",
            L"Aqua Scripts", MB_OK | MB_ICONWARNING);
        return;
    }
    const auto message = L"Разрешить запуск скрипта «" + CefString(item.name).ToWString() +
        L"» на сайте:\n\n" + CefString(origin).ToWString() +
        L"\n\nСкрипт сможет читать и изменять содержимое этого сайта, в том числе когда вы вошли в аккаунт. "
        L"Разрешайте запуск только доверенных файлов.";
    if (MessageBoxW(hwnd_, message.c_str(), L"Разрешение Aqua Scripts",
        MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return;
    auto next = scripts_.Items();
    next.push_back(std::move(item));
    if (!scripts_.Save(std::move(next))) {
        MessageBoxW(hwnd_, L"Не удалось сохранить скрипт.", L"Aqua Scripts", MB_OK | MB_ICONERROR);
        return;
    }
    RefreshScriptSettings();
    OpenScriptManager();
}

void Window::RefreshScriptSettings() {
    UpdateAquaScriptList(scripts_.Items());
    for (const auto& item : tabs_) {
        if (item.browser && !item.closing && item.url == L"aqua://settings/#scripts")
            item.browser->ReloadIgnoreCache();
    }
}

std::vector<Script> Window::ScriptsFor(std::string_view url) const {
    std::vector<Script> result;
    if (exiting_) return result;
    for (const auto& script : scripts_.Items()) {
        if (RunsOn(script, url)) result.push_back(script);
    }
    return result;
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
    if (!preferences_.restore_session && !history_dirty_) return;
    preferences_.session.clear();
    for (const auto& tab : tabs_) {
        if (preferences_.restore_session && !tab.closing && !tab.private_tab &&
            !tab.url.empty() && IsSessionAddress(tab.url))
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
    case SettingsAction::animations_on: next.animations = true; break;
    case SettingsAction::animations_off: next.animations = false; break;
    case SettingsAction::https_on: next.https_only = true; break;
    case SettingsAction::https_off: next.https_only = false; break;
    case SettingsAction::clear_history:
        if (MessageBoxW(hwnd_, L"Удалить историю посещений Aqua Frame?", L"Очистить историю",
                        MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return;
        next.history.clear();
        break;
    case SettingsAction::reset_preferences: {
        auto defaults = Preferences{};
        defaults.history = std::move(next.history);
        defaults.bookmarks = std::move(next.bookmarks);
        next = std::move(defaults);
        break;
    }
    default: return;
    }
    if (!WritePreferences(profile_, next)) {
        MessageBoxW(hwnd_, L"Не удалось сохранить настройки.", L"Aqua Frame", MB_OK | MB_ICONERROR);
        return;
    }
    const bool theme_changed = next.theme != preferences_.theme;
    preferences_ = std::move(next);
    RefreshLibrary();
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
    AppendMenuW(menu, MF_STRING, 8, L"Новая вкладка инкогнито\tCtrl+Shift+N");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 9, L"История\tCtrl+H");
    AppendMenuW(menu, MF_STRING, 10, L"Закладки");
    AppendMenuW(menu, MF_STRING, 11, L"Загрузки\tCtrl+J");
    AppendMenuW(menu, MF_STRING, 12, L"Полноэкранный режим\tF11");
    AppendMenuW(menu, MF_STRING, 6, L"Настройки");
    AppendMenuW(menu, MF_STRING, 7, L"Aqua Scripts — импорт JS");
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
    if (action == 7) OpenScriptManager();
    if (action == 8) AddTab(L"aqua://newtab/", true);
    if (action == 9) ShowLibrary(L"history");
    if (action == 10) ShowLibrary(L"bookmarks");
    if (action == 11) ShowLibrary(L"downloads");
    if (action == 12) ToggleFullscreen();
    if (action == 4) MessageBoxW(hwnd_, L"Aqua Frame 1.0.0\nAqua Deep Ocean\nBeyond the ordinary.",
                                 L"Aqua Frame", MB_OK | MB_ICONINFORMATION);
}
}
