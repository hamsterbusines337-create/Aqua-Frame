#include "window.h"
#include "../chromium/client.h"
#include "../chromium/pages.h"
#include "../reset/cleanup.h"
#include <cmath>
#include "../navigation/address.h"
#include "include/cef_frame.h"
#include "include/cef_app.h"
#include "include/cef_version.h"
#include <algorithm>
#include <charconv>
#include <chrono>
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
constexpr UINT navigation_timing_ready = WM_APP + 5;
constexpr UINT confirm_site_permission = WM_APP + 6;
constexpr UINT download_action = WM_APP + 7;
constexpr UINT_PTR history_timer = 1;
constexpr UINT_PTR warm_restore_timer = 2;
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

DWORD MonitorRefreshRate(HWND window) {
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST),
                         reinterpret_cast<MONITORINFO*>(&monitor))) return 0;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode) ||
        !(mode.dmFields & DM_DISPLAYFREQUENCY) || mode.dmDisplayFrequency <= 1) return 0;
    return mode.dmDisplayFrequency;
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

void PlaceChild(HWND parent, HWND child, int x, int y, int width, int height) {
    RECT current{};
    if (GetWindowRect(child, &current)) {
        MapWindowPoints(nullptr, parent, reinterpret_cast<POINT*>(&current), 2);
        if (current.left == x && current.top == y && current.right - current.left == width &&
            current.bottom - current.top == height) return;
    }
    MoveWindow(child, x, y, width, height, FALSE);
    InvalidateRect(child, nullptr, FALSE);
}
}

bool Window::Create(HINSTANCE instance, int show, const std::filesystem::path& profile, Preferences preferences) {
    profile_ = profile;
    preferences_ = std::move(preferences);
    scripts_.Load(profile_);
    UpdateAquaScriptList(scripts_.Items());
    UpdateAquaPageSettings(preferences_);
    UpdateAquaHistory(preferences_.history);
    UpdateAquaBookmarks(preferences_.bookmarks);
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
        if (preferences_.warm_restored_tab && tabs_.size() >= 2) {
            restore_foreground_ = selected_;
            restore_warm_tab_ = tabs_[tabs_.size() - 2].id;
        }
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
                              (GetKeyState(VK_SHIFT) < 0 ? EVENTFLAG_SHIFT_DOWN : 0) |
                              ((l & (1L << 30)) ? EVENTFLAG_IS_REPEAT : 0);
        if (self.Shortcut(static_cast<int>(w), modifiers)) return 0;
    }
    if (msg == WM_CHAR && (w == VK_RETURN || w == 12 || w == 20 || w == 23)) return 0;
    return DefSubclassProc(hwnd, msg, w, l);
}

LRESULT Window::Message(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_KEYDOWN:
        if ((w == VK_F11 || w == VK_F12 || w == VK_ESCAPE) &&
            Shortcut(static_cast<int>(w), (l & (1L << 30)) ? EVENTFLAG_IS_REPEAT : 0)) return 0;
        break;
    case WM_TIMER:
        if (w == history_timer) WriteHistoryInBackground();
        if (w == warm_restore_timer) WarmRestoredTab();
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
    case WM_CONTEXTMENU: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (point.x == -1 && point.y == -1) break;
        const auto screen = point;
        ScreenToClient(hwnd_, &point);
        if (const auto id = TabAt(point)) {
            TabMenu(id, screen);
            return 0;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        const auto id = TabAt(point);
        const auto it = std::ranges::find(tabs_, id, &Tab::id);
        if (it != tabs_.end() && point.x < TabBounds(it - tabs_.begin()).right - Scale(26)) {
            dragged_tab_ = id;
            drag_start_ = point;
            tab_reordered_ = false;
            SetCapture(hwnd_);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (!dragged_tab_ || !(w & MK_LBUTTON) || std::abs(point.x - drag_start_.x) < Scale(8)) break;
        const auto source = std::ranges::find(tabs_, dragged_tab_, &Tab::id);
        const auto target = std::ranges::find(tabs_, TabAt(point), &Tab::id);
        if (source != tabs_.end() && target != tabs_.end() && source != target &&
            source->pinned == target->pinned && source->group == target->group) {
            std::iter_swap(source, target);
            tab_reordered_ = true;
            RECT strip{};
            GetClientRect(hwnd_, &strip);
            strip.bottom = Scale(36);
            InvalidateRect(hwnd_, &strip, FALSE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        dragged_tab_ = 0;
        tab_reordered_ = false;
        return 0;
    case WM_LBUTTONUP: {
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (dragged_tab_) {
            const auto dragged = dragged_tab_;
            const bool reordered = tab_reordered_;
            ReleaseCapture();
            if (reordered) { Select(dragged, true); return 0; }
        }
        if (point.y < Scale(36)) {
            for (size_t i = 0; i < tabs_.size(); ++i) {
                const auto r = TabBounds(i);
                if (!PtInRect(&r, point)) continue;
                if (point.x >= r.right - Scale(26)) CloseTab(tabs_[i].id, false);
                else Select(tabs_[i].id, true);
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
    case navigation_timing_ready:
        ShowNavigationTiming(static_cast<uint64_t>(w));
        return 0;
    case confirm_site_permission:
        ConfirmSitePermission();
        return 0;
    case download_action:
        RunDownloadAction(static_cast<uint64_t>(w));
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
            if (timing_run_) {
                timing_run_->Cancel();
                timing_run_ = nullptr;
            }
            KillTimer(hwnd_, warm_restore_timer);
            KillTimer(hwnd_, history_timer);
            FinishHistoryWrite();
            if (!reset_requested_) SaveSession();
            exiting_ = true;
            downloads_.clear();
            extensions_->Close();

            std::vector<uint64_t> ids;
            for (const auto& tab : tabs_) ids.push_back(tab.id);
            for (auto id : ids) CloseTab(id, true);
        }
        if (tabs_.empty() && !extensions_->HasBrowsers()) DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, history_timer);
        KillTimer(hwnd_, warm_restore_timer);
        if (font_) DeleteObject(font_);
        if (address_brush_) DeleteObject(address_brush_);
        CefQuitMessageLoop();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}

int Window::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

RECT Window::TabBounds(size_t index) const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return TabBounds(index, client.right);
}

RECT Window::TabBounds(size_t index, long viewport_width) const {
    const long room = std::max(1L, viewport_width - Scale(55));
    const long width = std::clamp(room / static_cast<long>(std::max(size_t{1}, tabs_.size())),
                                  static_cast<long>(Scale(44)), static_cast<long>(Scale(190)));
    const long start = Scale(8) + static_cast<long>(index) * width;
    return {start, Scale(4), start + width - Scale(2), Scale(36)};
}

uint64_t Window::TabAt(POINT point) const {
    if (fullscreen_ || point.y < 0 || point.y >= Scale(36)) return 0;
    RECT client{};
    GetClientRect(hwnd_, &client);
    for (size_t i = 0; i < tabs_.size(); ++i) {
        const auto bounds = TabBounds(i, client.right);
        if (bounds.left > point.x) break;
        if (PtInRect(&bounds, point)) return tabs_[i].id;
    }
    return 0;
}

Window::Tab* Window::Find(uint64_t id) {
    const auto it = std::ranges::find(tabs_, id, &Tab::id);
    return it == tabs_.end() ? nullptr : &*it;
}

CefRefPtr<CefBrowser> Window::Current() {
    if (auto* tab = Find(selected_); tab && !tab->closing && !tab->unloading) return tab->browser;
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
    if (((GetWindowLongPtrW(edit_, GWL_STYLE) & WS_VISIBLE) != 0) == fullscreen_)
        ShowWindow(edit_, fullscreen_ ? SW_HIDE : SW_SHOWNA);
    PlaceChild(hwnd_, edit_, address_left, Scale(47), std::max(0L, r.right - address_left - Scale(118)), Scale(22));
    const int top = ContentTop();
    if (auto browser = Current()) {
        if (const auto child = browser->GetHost()->GetWindowHandle())
            PlaceChild(hwnd_, child, 0, top, r.right, std::max(0L, r.bottom - top));
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
    if (ps.rcPaint.top < Scale(36)) {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            const auto tab = TabBounds(i, r.right);
            if (tab.left >= ps.rcPaint.right) break;
            if (tab.right <= ps.rcPaint.left) continue;
            const bool current = tabs_[i].id == selected_;
            Rounded(dc, tab, Scale(8), current ? colors.active : colors.inactive);
            if (tabs_[i].group) {
                const auto stripe = tabs_[i].group == 1 ? colors.accent : RGB(166, 113, 240);
                Fill(dc, {tab.left + Scale(6), tab.bottom - Scale(2), tab.right - Scale(6), tab.bottom}, stripe);
            }
            RECT title = tab;
            title.left += Scale(10);
            title.right -= Scale(26);
            auto title_text = tabs_[i].private_tab ? L"◈  " + tabs_[i].title :
                IsSettings(tabs_[i].url) ? L"⚙  " + tabs_[i].title : tabs_[i].title;
            if (tabs_[i].pinned) title_text.insert(0, L"◆ ");
            else if (tabs_[i].group) title_text.insert(0, tabs_[i].group == 1 ? L"Работа · " : L"Учёба · ");
            Label(dc, title, title_text, current ? colors.text : colors.muted, DT_END_ELLIPSIS);
            Label(dc, {tab.right - Scale(25), tab.top, tab.right, tab.bottom}, L"×", colors.text, DT_CENTER);
        }
        const long plus = tabs_.empty() ? Scale(8) : TabBounds(tabs_.size() - 1, r.right).right;
        Label(dc, {plus, 0, plus + Scale(34), Scale(36)}, L"+", colors.accent, DT_CENTER);
    }
    if (ps.rcPaint.bottom > Scale(36) && ps.rcPaint.top < Scale(76)) {
        Fill(dc, {0, Scale(36), r.right, Scale(76)}, colors.toolbar);
        Rounded(dc, {Scale(101), Scale(42), r.right - Scale(109), Scale(71)}, Scale(8), colors.border);
        Rounded(dc, {Scale(102), Scale(43), r.right - Scale(110), Scale(70)}, Scale(8), colors.background);
        if (selected && IsSettings(selected->url)) {
            Label(dc, {Scale(113), Scale(43), Scale(130), Scale(70)}, L"A", colors.accent, DT_CENTER);
            Label(dc, {Scale(135), Scale(43), Scale(222), Scale(70)}, L"Aqua Frame", colors.text, DT_END_ELLIPSIS);
            Fill(dc, {Scale(224), Scale(49), Scale(225), Scale(64)}, colors.border);
        }
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
    }
    if (ps.rcPaint.bottom > Scale(76) && ContentTop() > Scale(76)) {
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
    if (tab.id != selected_) info.style &= ~WS_VISIBLE;
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
    const bool focus_content = std::exchange(tab->focus_on_show, false);
    if (id == selected_) Layout();
    ShowWindow(browser->GetHost()->GetWindowHandle(), id == selected_ ? SW_SHOWNA : SW_HIDE);
    if (focus_content && id == selected_ && GetForegroundWindow() == hwnd_)
        browser->GetHost()->SetFocus(true);
}

bool Window::QueueBrowserClose(uint64_t id) {
    // CEF's default notification targets the shared top-level window. Close
    // only this tab's child HWND, after the DoClose callback has returned.
    return PostMessageW(hwnd_, close_browser_window, static_cast<WPARAM>(id), 0) != FALSE;
}

void Window::BrowserClosed(uint64_t id) {
    https_upgrades_.erase(id);
    if (id == timing_tab_ && timing_run_) {
        timing_run_->Cancel();
        timing_run_ = nullptr;
    }
    if (id == fullscreen_document_) {
        fullscreen_document_ = 0;
        if (!exiting_ && fullscreen_) ToggleFullscreen();
    }
    if (auto* tab = Find(id); tab && tab->unloading && !tab->closing && !exiting_) {
        tab->browser = nullptr;
        tab->deferred = true;
        tab->unloading = false;
        if (id == selected_) Select(id, tab->focus_on_show);
        return;
    }
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
    if (tab->unloading) {
        tab->closing = true;
        return;
    }
    if (tab->deferred) {
        BrowserClosed(id);
        return;
    }
    tab->closing = force || !tab->browser;
    if (tab->browser) tab->browser->GetHost()->CloseBrowser(force);
}

bool Window::CanUnloadTab(const Tab& tab) const {
    return preferences_.allow_tab_unload && !exiting_ && tab.id != selected_ && !tab.closing && !tab.unloading &&
        !tab.pinned && !tab.private_tab && tab.browser && !tab.browser->IsLoading() &&
        !tab.browser->GetHost()->IsFullscreen() && IsLibraryUrl(tab.url) &&
        std::ranges::none_of(downloads_, [](const Download& download) { return download.entry.active; });
}

void Window::UnloadTab(uint64_t id) {
    const auto* tab = Find(id);
    if (!tab || !CanUnloadTab(*tab)) return;
    const auto answer = MessageBoxW(hwnd_,
        L"Выгрузить фоновую вкладку? Страница откроется заново при выборе. "
        L"Несохранённые данные и работа сайта в фоне будут потеряны.",
        L"Освобождение памяти Aqua Frame", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION);
    auto* current = Find(id);
    if (answer != IDYES || !current || !CanUnloadTab(*current)) return;
    current->unloading = true;
    current->browser->GetHost()->CloseBrowser(true);
}

void Window::WarmRestoredTab() {
    KillTimer(hwnd_, warm_restore_timer);
    const auto id = std::exchange(restore_warm_tab_, 0);
    if (exiting_ || !preferences_.warm_restored_tab || selected_ != restore_foreground_ ||
        GetForegroundWindow() != hwnd_ || IsIconic(hwnd_)) return;
    const auto* active = Find(selected_);
    auto* tab = Find(id);
    if (active && active->browser && !active->browser->IsLoading() &&
        tab && tab->deferred && !tab->closing) StartTab(*tab);
}

void Window::Select(uint64_t id, bool focus_content) {
    if (id != restore_foreground_ && restore_warm_tab_) {
        KillTimer(hwnd_, warm_restore_timer);
        restore_warm_tab_ = 0;
    }
    if (auto old = Current()) {
        if (selected_ != id) {
            if (old->GetHost()->IsFullscreen()) old->GetHost()->ExitFullscreen(true);
            ShowWindow(old->GetHost()->GetWindowHandle(), SW_HIDE);
        }
    }
    selected_ = id;
    if (auto* tab = Find(id)) {
        tab->focus_on_show = focus_content && (!tab->browser || tab->unloading);
        if (tab->deferred && !StartTab(*tab)) {
            BrowserClosed(id);
            return;
        }
        const auto origin = WebOrigin(CefString(tab->url).ToString());
        if (!tab->private_tab && !origin.empty()) last_web_origin_ = origin;
        SetWindowTextW(edit_, tab->url == L"aqua://newtab/" ? L"" : tab->url.c_str());
    }
    Layout();
    if (auto browser = Current()) {
        ShowWindow(browser->GetHost()->GetWindowHandle(), SW_SHOWNA);
        if (focus_content && GetForegroundWindow() == hwnd_) browser->GetHost()->SetFocus(true);
    }
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
        auto next = title.ToWString().substr(0, 160);
        if (tab->title == next) return;
        tab->title = std::move(next);
        const auto r = TabBounds(static_cast<size_t>(tab - tabs_.data()));
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
    if (address == L"aqua://library/history" || address == L"aqua://library/bookmarks" ||
        address == L"aqua://library/downloads") {
        if (const auto* tab = Find(selected_); tab && !tab->private_tab)
            browser->GetMainFrame()->LoadURL(address);
        return;
    }
    if (address == L"aqua://settings" || address == L"aqua://settings/" ||
        address.starts_with(L"aqua://settings/#")) {
        browser->GetHost()->SetFocus(true);
        browser->GetMainFrame()->LoadURL(address == L"aqua://settings" ? L"aqua://settings/" : address);
        return;
    }
    if (address == L"chrome://gpu" || address == L"chrome://gpu/" ||
        address == L"chrome://version" || address == L"chrome://version/") {
        if (const auto* tab = Find(selected_); tab && !tab->private_tab) {
            browser->GetHost()->SetFocus(true);
            browser->GetMainFrame()->LoadURL(address);
        }
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
        if (!(modifiers & EVENTFLAG_IS_REPEAT)) {
            const bool owned_by_page = fullscreen_document_ != 0;
            if (auto browser = Current(); browser && browser->GetHost()->IsFullscreen())
                browser->GetHost()->ExitFullscreen(true);
            if (!owned_by_page) ToggleFullscreen();
        }
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
    if (key == VK_TAB && !tabs_.empty()) {
        const auto active = std::ranges::find(tabs_, selected_, &Tab::id) - tabs_.begin();
        const auto index = (active + tabs_.size() + (shift ? -1 : 1)) % tabs_.size();
        Select(tabs_[index].id, true);
        return true;
    }
    if (key == 'H') { ShowLibrary(L"history"); return true; }
    if (key == 'J') { ShowLibrary(L"downloads"); return true; }
    if (key == 'M' && shift) { MeasureNavigation(); return true; }
    if (key == 'D' && !shift) { ToggleBookmark(); return true; }
    if (key == 'B' && shift) { bookmark_bar_ = !bookmark_bar_; Layout(); return true; }
    if (key == 'N' && shift) { AddTab(L"aqua://newtab/", true); return true; }
    if (key == 'L') {
        if (auto* tab = Find(selected_)) tab->focus_on_show = false;
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

void Window::MeasureNavigation() {
    auto browser = Current();
    if (!browser) return;
    const auto* tab = Find(selected_);
    auto url = browser->GetMainFrame()->GetURL().ToString();
    if (!tab || !IsLibraryUrl(CefString(url).ToWString())) {
        MessageBoxW(hwnd_, L"Откройте сайт и дождитесь загрузки страницы.", L"Aqua Performance", MB_OK);
        return;
    }
    if (timing_run_) timing_run_->Cancel();
    timing_result_.reset();
    timing_tab_ = selected_;
    const auto request = ++timing_request_;
    performance_before_ = SampleBrowserProcesses();
    timing_run_ = new ScriptRun(browser->GetHost(), std::move(url),
        [this, request](std::optional<NavigationTiming> result) {
            timing_result_ = std::move(result);
            PostMessageW(hwnd_, navigation_timing_ready, request, 0);
        });
    timing_run_->Start();
}

void Window::ShowNavigationTiming(uint64_t request) {
    if (exiting_ || request != timing_request_ || !timing_run_) return;
    timing_run_->Cancel();
    timing_run_ = nullptr;
    if (timing_tab_ != selected_) return;
    auto timing = std::exchange(timing_result_, std::nullopt);
    if (!timing) {
        MessageBoxW(hwnd_, L"Данные загрузки страницы пока недоступны.", L"Aqua Performance", MB_OK);
        return;
    }
    wchar_t data[700]{};
    swprintf_s(data, L"DNS: %.1f мс\nСоединение: %.1f мс\nTLS: %.1f мс\n"
        L"Первый байт ответа (TTFB): %.1f мс\nЗагрузка документа: %.1f мс\n\n"
        L"При использовании кэша или готового соединения некоторые интервалы равны нулю.\n\n",
        timing->dns, timing->connection, timing->tls, timing->first_byte, timing->document);
    std::wstring report = data;
    if (const auto hz = MonitorRefreshRate(hwnd_)) {
        swprintf_s(data, L"Частота монитора по данным Windows: %lu Гц\n", hz);
        report += data;
    }
    if (timing->page_fps > 0) {
        swprintf_s(data, L"Кадры страницы (rAF): %.1f FPS · %.2f мс на кадр\n",
                   timing->page_fps, timing->page_frame_ms);
        report += data;
        swprintf_s(data, L"Длительность замера: %.1f с · Интервалов: %d\n"
            L"P95: %.2f мс · P99: %.2f мс\nИнтервалы дольше бюджета 120 FPS (+0,5 мс): %.1f%%\n",
            timing->sample_ms / 1000.0, timing->intervals, timing->p95_frame_ms, timing->p99_frame_ms,
            timing->over_120_percent);
        report += data;
        if (timing->long_animation_frames >= 0) {
            swprintf_s(data, L"Долгие кадры по данным Chromium (>50 мс): %d\n",
                       timing->long_animation_frames);
            report += data;
        }
        if (timing->low_fps > 0) {
            swprintf_s(data, L"1%% low страницы: %.1f FPS\n", timing->low_fps);
            report += data;
        }
    }
    const auto after = SampleBrowserProcesses();
    const auto cpu = BrowserCpuPercent(performance_before_, after);
    uint64_t working_set = 0;
    for (const auto& process : after.processes) working_set += process.working_set;
    if (cpu >= 0) {
        swprintf_s(data, L"CPU браузера: %.1f%%\n", cpu);
        report += data;
    }
    swprintf_s(data, L"Рабочие наборы процессов: %.1f МиБ\nПроцессов: %zu · Вкладок: %zu\n"
                     L"Chromium %d.%d.%d.%d · CEF %d.%d.%d\n",
        static_cast<double>(working_set) / (1024 * 1024), after.processes.size(), tabs_.size(),
        CHROME_VERSION_MAJOR, CHROME_VERSION_MINOR, CHROME_VERSION_BUILD, CHROME_VERSION_PATCH,
        CEF_VERSION_MAJOR, CEF_VERSION_MINOR, CEF_VERSION_PATCH);
    report += data;
    MessageBoxW(hwnd_, report.c_str(), L"Aqua Performance", MB_OK | MB_ICONINFORMATION);
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

void Window::DocumentFullscreen(uint64_t id, bool enabled) {
    if (exiting_) return;
    if (enabled && id == selected_ && !fullscreen_) {
        fullscreen_document_ = id;
        ToggleFullscreen();
    } else if (!enabled && id == fullscreen_document_) {
        fullscreen_document_ = 0;
        if (fullscreen_) ToggleFullscreen();
    }
}

void Window::ShowLibrary(const wchar_t* section) {
    if (const auto* tab = Find(selected_); tab && tab->private_tab) return;
    AddTab(std::wstring(L"aqua://library/") + section);
}

void Window::RefreshDownloads() {
    std::vector<DownloadEntry> states;
    for (const auto& item : downloads_) states.push_back(item.entry);
    UpdateAquaDownloads(states);
}

void Window::ScheduleHistorySave() {
    if (!history_dirty_) SetTimer(hwnd_, history_timer, 3500, nullptr);
    history_dirty_ = true;
}

void Window::WriteHistoryInBackground() {
    if (history_write_.valid() &&
        history_write_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    KillTimer(hwnd_, history_timer);
    FinishHistoryWrite();
    if (!history_dirty_ || exiting_) return;
    try {
        history_write_ = std::async(std::launch::async, [profile = profile_, snapshot = preferences_] {
            return WritePreferences(profile, snapshot);
        });
        history_dirty_ = false;
    } catch (...) {
        // Preserve the pending data for the next explicit save or shutdown.
    }
}

void Window::FinishHistoryWrite() {
    if (!history_write_.valid()) return;
    try {
        if (!history_write_.get()) history_dirty_ = true;
    } catch (...) {
        history_dirty_ = true;
    }
}

bool Window::SavePreferences(const Preferences& snapshot) {
    // Explicit saves supersede any older history snapshot.
    FinishHistoryWrite();
    if (!WritePreferences(profile_, snapshot)) return false;
    KillTimer(hwnd_, history_timer);
    history_dirty_ = false;
    return true;
}

void Window::RecordVisit(uint64_t id, const CefString& address, int) {
    if (id == restore_foreground_ && id == selected_ && restore_warm_tab_ && preferences_.warm_restored_tab)
        SetTimer(hwnd_, warm_restore_timer, 2500, nullptr);
    const auto* tab = Find(id);
    const auto url = address.ToWString();
    if (!tab || tab->private_tab || exiting_ || !IsLibraryUrl(url)) return;
    auto& history = preferences_.history;
    if (!history.empty() && history.front().url == url && history.front().title == tab->title) return;
    std::erase_if(history, [&](const PageEntry& item) { return item.url == url; });
    history.insert(history.begin(), {url, tab->title});
    if (history.size() > library_limit) history.pop_back();
    UpdateAquaHistory(history);
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
    if (!SavePreferences(next)) {
        MessageBoxW(hwnd_, L"Не удалось сохранить закладки.", L"Aqua Frame", MB_OK | MB_ICONERROR);
        return;
    }
    preferences_ = std::move(next);
    UpdateAquaBookmarks(preferences_.bookmarks);
    Layout();
}

void Window::TrackDownload(uint32_t id, const CefString& name, std::wstring state, bool paused,
                           CefRefPtr<CefDownloadItemCallback> callback) {
    if (exiting_) return;
    const auto item = std::ranges::find_if(downloads_, [id](const auto& row) { return row.entry.id == id; });
    auto filename = name.ToWString().substr(0, 260);
    if (filename.empty()) filename = L"Файл";
    auto label = filename + L" — " + state;
    if (item != downloads_.end()) {
        const bool active = callback != nullptr;
        item->callback = std::move(callback);
        if (item->entry.label == label && item->entry.paused == paused && item->entry.active == active) return;
        item->entry = {id, std::move(label), active, paused};
    } else {
        downloads_.insert(downloads_.begin(), {{id, std::move(label), callback != nullptr, paused}, std::move(callback)});
        if (downloads_.size() > 64) downloads_.pop_back();
    }
    RefreshDownloads();
}

void Window::QueueDownloadAction(uint64_t tab, std::string_view url) {
    constexpr std::string_view prefix = "aqua://library/action/";
    const auto route = url.substr(prefix.size());
    const auto delimiter = route.find('/');
    if (delimiter == std::string_view::npos || download_action_) return;
    const auto id_text = route.substr(delimiter + 1);
    uint32_t id = 0;
    const auto parsed = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != id_text.data() + id_text.size()) return;
    const auto operation = route.substr(0, delimiter);
    if (operation != "pause" && operation != "resume" && operation != "cancel") return;
    download_action_.emplace(id, operation);
    if (!PostMessageW(hwnd_, download_action, tab, 0)) download_action_.reset();
}

void Window::RunDownloadAction(uint64_t id) {
    const auto action = std::exchange(download_action_, std::nullopt);
    const auto* tab = Find(id);
    if (!action || exiting_ || id != selected_ || !tab || tab->closing || tab->private_tab ||
        tab->url != L"aqua://library/downloads") return;
    const auto item = std::ranges::find_if(downloads_, [&](const auto& row) { return row.entry.id == action->first; });
    if (item == downloads_.end() || !item->callback) return;
    const auto callback = item->callback;
    if (action->second == "pause" && !item->entry.paused) callback->Pause();
    else if (action->second == "resume" && item->entry.paused) callback->Resume();
    else if (action->second == "cancel") callback->Cancel();
}

void Window::UpgradeNavigation(uint64_t tab, const std::string& url) {
    if (exiting_) return;
    https_upgrades_.insert_or_assign(tab, CefString("https://" + url.substr(7)).ToWString());
    PostMessageW(hwnd_, upgrade_navigation, tab, 0);
}

void Window::RequestSitePermission(uint64_t id, const CefString& address, std::wstring names,
                                   std::function<void(bool)> done) {
    auto origin = WebOrigin(address.ToString());
    auto* tab = Find(id);
    if (exiting_ || permission_ || permission_open_ || id != selected_ || !tab || tab->closing ||
        !origin.starts_with("https://") || origin != WebOrigin(CefString(tab->url).ToString())) {
        done(false);
        return;
    }
    permission_.emplace(SitePermission{id, std::move(origin), std::move(names), std::move(done)});
    if (!PostMessageW(hwnd_, confirm_site_permission, 0, 0)) {
        auto request = std::exchange(permission_, std::nullopt);
        request->done(false);
    }
}

void Window::ConfirmSitePermission() {
    auto request = std::exchange(permission_, std::nullopt);
    if (!request) return;
    const auto still_current = [&] {
        const auto* tab = Find(request->tab);
        return !exiting_ && tab && !tab->closing && tab->id == selected_ &&
            request->origin == WebOrigin(CefString(tab->url).ToString());
    };
    if (!still_current()) {
        request->done(false);
        return;
    }
    const auto message = L"Разрешить доступ сайту " + CefString(request->origin).ToWString() +
        L"?\n\n" + request->names;
    permission_open_ = true;
    const auto answer = MessageBoxW(hwnd_, message.c_str(), L"Разрешение сайта Aqua Frame",
                                    MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION);
    permission_open_ = false;
    request->done(answer == IDYES && still_current());
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
    if (!SavePreferences(preferences_))
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
    if (action == SettingsAction::open_gpu_report) {
        AddTab(L"chrome://gpu/");
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
    case SettingsAction::warm_restore_on: next.warm_restored_tab = true; break;
    case SettingsAction::warm_restore_off: next.warm_restored_tab = false; break;
    case SettingsAction::tab_unload_on: next.allow_tab_unload = true; break;
    case SettingsAction::tab_unload_off: next.allow_tab_unload = false; break;
    case SettingsAction::high_gpu_on: next.prefer_high_performance_gpu = true; break;
    case SettingsAction::high_gpu_off: next.prefer_high_performance_gpu = false; break;
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
    if (!SavePreferences(next)) {
        MessageBoxW(hwnd_, L"Не удалось сохранить настройки.", L"Aqua Frame", MB_OK | MB_ICONERROR);
        return;
    }
    const bool theme_changed = next.theme != preferences_.theme;
    const bool zoom_changed = next.zoom != preferences_.zoom;
    const bool home_changed = theme_changed || next.animations != preferences_.animations ||
        next.search != preferences_.search || action == SettingsAction::clear_history;
    preferences_ = std::move(next);
    if (action == SettingsAction::clear_history) UpdateAquaHistory(preferences_.history);
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
        if (zoom_changed) ApplyZoom(item.browser, item.url);
        if (item.url.starts_with(L"aqua://settings/") || (home_changed && item.url == L"aqua://newtab/"))
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
    AppendMenuW(menu, MF_STRING, 13, L"Время соединения и ответа сайта\tCtrl+Shift+M");
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
    if (action == 13) MeasureNavigation();
    if (action == 4) MessageBoxW(hwnd_, L"Aqua Frame 1.0.0\nAqua Deep Ocean\nBeyond the ordinary.",
                                 L"Aqua Frame", MB_OK | MB_ICONINFORMATION);
}

void Window::TabMenu(uint64_t id, POINT position) {
    const auto* tab = Find(id);
    if (!tab || tab->closing) return;
    const auto menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Дублировать вкладку");
    AppendMenuW(menu, MF_STRING, 2, tab->pinned ? L"Открепить вкладку" : L"Закрепить вкладку");
    AppendMenuW(menu, MF_STRING, 3, L"Добавить в группу «Работа»");
    AppendMenuW(menu, MF_STRING, 4, L"Добавить в группу «Учёба»");
    AppendMenuW(menu, MF_STRING | (tab->group ? 0 : MF_GRAYED), 5, L"Убрать из группы");
    if (preferences_.allow_tab_unload)
        AppendMenuW(menu, MF_STRING | (CanUnloadTab(*tab) ? 0 : MF_GRAYED), 7, L"Выгрузить вкладку из памяти");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 6, L"Закрыть вкладку");
    const auto action = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, position.x, position.y,
                                       0, hwnd_, nullptr);
    DestroyMenu(menu);
    auto* target = Find(id);
    if (!target || target->closing || !action) return;
    if (action == 1) { AddTab(target->url, target->private_tab); return; }
    if (action == 6) { CloseTab(id, false); return; }
    if (action == 7) { UnloadTab(id); return; }
    if (action == 2) {
        target->pinned = !target->pinned;
        if (target->pinned) target->group = 0;
    } else {
        target->pinned = false;
        target->group = action == 5 ? 0 : action - 2;
    }
    std::stable_sort(tabs_.begin(), tabs_.end(), [](const Tab& a, const Tab& b) {
        const auto key = [](const Tab& tab) { return tab.pinned ? 0U : tab.group ? tab.group : 3U; };
        return key(a) < key(b);
    });
    Layout();
}
}
