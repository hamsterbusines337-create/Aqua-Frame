#include "window.h"
#include "../chromium/client.h"
#include "../navigation/address.h"
#include "include/cef_frame.h"
#include "include/cef_app.h"
#include <algorithm>
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace aqua {
namespace {
constexpr COLORREF background = RGB(11, 22, 40);
constexpr COLORREF toolbar = RGB(20, 36, 58);
constexpr COLORREF active = RGB(32, 59, 89);
constexpr COLORREF inactive = RGB(26, 46, 69);
constexpr COLORREF text = RGB(230, 241, 255);
constexpr COLORREF muted = RGB(140, 160, 179);
constexpr COLORREF accent = RGB(0, 184, 230);

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

bool Window::Create(HINSTANCE instance, int show) {
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
    ShowWindow(hwnd_, show);
    AddTab();
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
        address_brush_ = CreateSolidBrush(background);
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
        SetTextColor(reinterpret_cast<HDC>(w), text);
        SetBkColor(reinterpret_cast<HDC>(w), background);
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
    case WM_CLOSE:
        if (tabs_.empty()) {
            DestroyWindow(hwnd_);
        } else {
            exiting_ = true;
            std::vector<uint64_t> ids;
            for (const auto& tab : tabs_) ids.push_back(tab.id);
            for (auto id : ids) CloseTab(id, true);
        }
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
    MoveWindow(edit_, Scale(112), Scale(48), std::max(0L, r.right - Scale(171)), Scale(23), TRUE);
    if (auto browser = Current()) {
        if (const auto child = browser->GetHost()->GetWindowHandle())
            MoveWindow(child, 0, Scale(76), r.right, std::max(0L, r.bottom - Scale(76)), TRUE);
    }
    RECT top{0, 0, r.right, Scale(76)};
    InvalidateRect(hwnd_, &top, FALSE);
}

void Window::Paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd_, &ps);
    RECT r{};
    GetClientRect(hwnd_, &r);
    Fill(dc, ps.rcPaint, background);
    SetBkMode(dc, TRANSPARENT);
    const auto old_font = SelectObject(dc, font_);
    const auto old_brush = SelectObject(dc, GetStockObject(DC_BRUSH));
    const auto old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Fill(dc, {0, Scale(36), r.right, Scale(76)}, toolbar);
    Rounded(dc, {Scale(101), Scale(42), r.right - Scale(48), Scale(71)}, Scale(8), background);
    for (size_t i = 0; i < tabs_.size(); ++i) {
        RECT tab = TabBounds(i);
        const bool current = tabs_[i].id == selected_;
        Rounded(dc, tab, Scale(8), current ? active : inactive);
        RECT title = tab;
        title.left += Scale(10);
        title.right -= Scale(26);
        Label(dc, title, tabs_[i].title, current ? text : muted, DT_END_ELLIPSIS);
        Label(dc, {tab.right - Scale(25), tab.top, tab.right, tab.bottom}, L"×", text, DT_CENTER);
    }
    const long plus = tabs_.empty() ? Scale(8) : TabBounds(tabs_.size() - 1).right;
    Label(dc, {plus, 0, plus + Scale(34), Scale(36)}, L"+", accent, DT_CENTER);
    Label(dc, {Scale(6), Scale(36), Scale(38), Scale(76)}, L"←", text, DT_CENTER);
    Label(dc, {Scale(38), Scale(36), Scale(70), Scale(76)}, L"→", text, DT_CENTER);
    Label(dc, {Scale(70), Scale(36), Scale(102), Scale(76)}, L"↻", text, DT_CENTER);
    Label(dc, {r.right - Scale(44), Scale(36), r.right, Scale(76)}, L"☰", text, DT_CENTER);
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
    settings.background_color = CefColorSetARGB(255, 11, 22, 40);
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
    if (tab->closing) {
        browser->GetHost()->CloseBrowser(true);
        return;
    }
    ShowWindow(browser->GetHost()->GetWindowHandle(), id == selected_ ? SW_SHOWNA : SW_HIDE);
    Layout();
}

void Window::BrowserClosed(uint64_t id) {
    if (const auto* tab = Find(id); tab && !exiting_) {
        if (closed_.size() == 32) closed_.erase(closed_.begin());
        closed_.push_back(tab->url);
    }
    std::erase_if(tabs_, [id](const Tab& tab) { return tab.id == id; });
    if (tabs_.empty()) {
        DestroyWindow(hwnd_);
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
    tab->url = address.ToWString();
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
    auto browser = Current();
    if (!browser) return;
    const int length = GetWindowTextLengthW(edit_);
    std::wstring address(static_cast<size_t>(length) + 1, L'\0');
    address.resize(GetWindowTextW(edit_, address.data(), length + 1));
    const auto url = ResolveAddress(address);
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
    if (key == 'R') {
        if (auto b = Current()) b->Reload();
        return true;
    }
    return false;
}

void Window::Menu() {
    const auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Новая вкладка\tCtrl+T");
    AppendMenuW(menu, MF_STRING, 2, L"Восстановить вкладку\tCtrl+Shift+T");
    AppendMenuW(menu, MF_STRING, 3, L"Закрыть вкладку\tCtrl+W");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"О браузере Aqua Frame");
    POINT pos{};
    GetCursorPos(&pos);
    const auto action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_NONOTIFY,
                                       pos.x, pos.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (action == 1) AddTab();
    if (action == 2) Shortcut('T', EVENTFLAG_CONTROL_DOWN | EVENTFLAG_SHIFT_DOWN);
    if (action == 3) CloseTab(selected_, false);
    if (action == 4) MessageBoxW(hwnd_, L"Aqua Frame 1.0.0\nAqua Deep Ocean\nBeyond the ordinary.",
                                 L"Aqua Frame", MB_OK | MB_ICONINFORMATION);
}
}
