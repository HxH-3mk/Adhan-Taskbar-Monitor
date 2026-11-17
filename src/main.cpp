#include <windows.h>
#include <dwmapi.h>
#include <string>

#include "prayer.h"
#include "settings_ui.h"
#include "widget.h"

namespace {

constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT_PTR TIMER_TICK = 1;

enum MenuId : UINT {
    ID_EDIT_LOCATION = 100,
    ID_EDIT_METHOD,
    ID_EDIT_MANUAL,
    ID_EDIT_IQAMAH,
    ID_LANG_AR,
    ID_LANG_EN,
    ID_TOGGLE_12H,
    ID_WIDGET_BACKGROUND,
    ID_AUTOSTART,
    ID_EXIT
};

const wchar_t* kAppName = L"Adhan Taskbar Monitor";
const wchar_t* kWndClass = L"AdhanTaskbarWnd";
const wchar_t* kPopupClass = L"AdhanSchedulePopup";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"AdhanTaskbarMonitor";
const wchar_t* kSettingsKey = L"Software\\AdhanTaskbarMonitor";

constexpr wchar_t kIconClock = 0xE823;
constexpr wchar_t kIconIqamah = 0xE916;

HINSTANCE g_inst = nullptr;
HWND g_hwnd = nullptr;
HWND g_popup = nullptr;
UINT g_taskbarCreatedMsg = 0;
bool g_lightTaskbar = false;
bool g_lightApps = false;
bool g_widgetBackground = false;
bool g_dialogOpen = false;

LocationConfig g_loc;
MethodConfig g_mc;
ManualAdhanConfig g_ma;
IqamahConfig g_iq;
AppLang g_lang = AppLang::Arabic;
bool g_use12Hour = true;

bool ReadPersonalizeBool(const wchar_t* valueName, bool defaultVal) {
    DWORD v = defaultVal ? 1 : 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     valueName, RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS) {
        return v != 0;
    }
    return defaultVal;
}

bool ReadLightTaskbar() {
    return ReadPersonalizeBool(L"SystemUsesLightTheme", false);
}

void ApplyMenuTheme(bool light) {
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setMode = ux ? (SetPreferredAppModeFn)GetProcAddress(ux, MAKEINTRESOURCEA(135)) : nullptr;
    static auto flush = ux ? (FlushMenuThemesFn)GetProcAddress(ux, MAKEINTRESOURCEA(136)) : nullptr;
    if (setMode) setMode(light ? 3 : 2);
    if (flush) flush();
}

std::wstring ExePath() {
    wchar_t p[MAX_PATH]{};
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

bool LoadBoolSetting(const wchar_t* name, bool defaultVal) {
    DWORD v = defaultVal ? 1 : 0, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v != 0;
}

void SaveBoolSetting(const wchar_t* name, bool on) {
    DWORD v = on ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &v, sizeof(v));
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2]{};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS)
        return false;
    return _wcsicmp(buf, (L"\"" + ExePath() + L"\"").c_str()) == 0;
}

void SetAutostart(bool on) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                       static_cast<DWORD>((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunValue);
    }
    RegCloseKey(k);
}

void UpdateDisplay() {
    if (!widget::Alive()) {
        widget::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
    }
    if (widget::Alive()) {
        CountdownState st = prayer::ComputeCountdown(g_loc, g_mc, g_ma, g_iq, g_lang, g_use12Hour);
        int redAlpha = 0;
        if (st.isIqamah) {
            redAlpha = g_lightTaskbar ? 115 : 140;
        } else if (st.remainingSeconds <= 5 * 60) {
            redAlpha = (st.remainingSeconds % 2 == 0) ? (g_lightTaskbar ? 115 : 140) : 0;
        }
        widget::SetDisplay(st.isIqamah ? kIconIqamah : kIconClock, st.labelText, st.timeText, false, redAlpha, st.tooltipText);
        if (!widget::Reposition()) {
            widget::Destroy();
        }
    }
}

// ---- Schedule Popup (on Left-Click) -----------------------------------------------------------

struct SchedulePopupState {
    TodaySchedule sched;
    bool light = false;
    bool en = false;
    HFONT fontTitle = nullptr;
    HFONT fontSub = nullptr;
    HFONT fontRow = nullptr;
    HFONT fontBold = nullptr;
};

LRESULT CALLBACK SchedulePopupProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<SchedulePopupState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) DestroyWindow(h);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
            break;
        case WM_PAINT: {
            if (!st) break;
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(h, &ps);
            RECT rc{};
            GetClientRect(h, &rc);

            COLORREF bg = st->light ? RGB(246, 246, 246) : RGB(32, 32, 32);
            COLORREF fg = st->light ? RGB(20, 20, 20) : RGB(242, 242, 242);
            COLORREF muted = st->light ? RGB(100, 100, 100) : RGB(170, 170, 170);
            COLORREF accent = st->light ? RGB(0, 103, 192) : RGB(96, 205, 255);
            COLORREF hiBg = st->light ? RGB(228, 238, 249) : RGB(46, 56, 68);
            COLORREF sepCol = st->light ? RGB(218, 218, 218) : RGB(56, 56, 56);

            HBRUSH bgBrush = CreateSolidBrush(bg);
            FillRect(hdc, &rc, bgBrush);
            DeleteObject(bgBrush);

            UINT dpi = GetDpiForSystem();
            auto s = [&](int v) { return MulDiv(v, static_cast<int>(dpi ? dpi : 96), 96); };

            SetBkMode(hdc, TRANSPARENT);
            int pad = s(14);

            HGDIOBJ oldF = SelectObject(hdc, st->fontTitle);
            SetTextColor(hdc, fg);
            RECT rTitle{ pad, s(12), rc.right - pad, s(34) };
            DrawTextW(hdc, st->sched.locationDisplay.c_str(), -1, &rTitle,
                      DT_SINGLELINE | DT_END_ELLIPSIS | (st->en ? DT_LEFT : DT_RIGHT | DT_RTLREADING));

            SelectObject(hdc, st->fontSub);
            SetTextColor(hdc, accent);
            RECT rSub{ pad, s(34), rc.right - pad, s(54) };
            DrawTextW(hdc, st->sched.statusLine.c_str(), -1, &rSub,
                      DT_SINGLELINE | DT_END_ELLIPSIS | (st->en ? DT_LEFT : DT_RIGHT | DT_RTLREADING));

            HPEN pen = CreatePen(PS_SOLID, 1, sepCol);
            HGDIOBJ oldP = SelectObject(hdc, pen);
            MoveToEx(hdc, pad, s(60), nullptr);
            LineTo(hdc, rc.right - pad, s(60));
            SelectObject(hdc, oldP);
            DeleteObject(pen);

            int y = s(68);
            int rowH = s(28);
            for (const auto& row : st->sched.rows) {
                RECT rRow{ pad - s(6), y, rc.right - pad + s(6), y + rowH };
                if (row.isNext) {
                    HBRUSH hb = CreateSolidBrush(hiBg);
                    FillRect(hdc, &rRow, hb);
                    DeleteObject(hb);
                }
                SelectObject(hdc, row.isNext ? st->fontBold : st->fontRow);
                SetTextColor(hdc, row.isNext ? accent : fg);

                RECT rName{ pad, y, rc.right - pad, y + rowH };
                RECT rTime{ pad, y, rc.right - pad, y + rowH };
                if (st->en) {
                    DrawTextW(hdc, row.name.c_str(), -1, &rName, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                    SetTextColor(hdc, row.isNext ? accent : muted);
                    DrawTextW(hdc, row.time.c_str(), -1, &rTime, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
                } else {
                    DrawTextW(hdc, row.name.c_str(), -1, &rName, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_RTLREADING);
                    SetTextColor(hdc, row.isNext ? accent : muted);
                    DrawTextW(hdc, row.time.c_str(), -1, &rTime, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
                }
                y += rowH;
            }

            SelectObject(hdc, oldF);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_DESTROY:
            if (st) {
                if (st->fontTitle) DeleteObject(st->fontTitle);
                if (st->fontSub) DeleteObject(st->fontSub);
                if (st->fontRow) DeleteObject(st->fontRow);
                if (st->fontBold) DeleteObject(st->fontBold);
                delete st;
                SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            }
            g_popup = nullptr;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void ShowSchedulePopup() {
    if (g_popup && IsWindow(g_popup)) {
        DestroyWindow(g_popup);
        g_popup = nullptr;
        return;
    }

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = SchedulePopupProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kPopupClass;
        reg = RegisterClassExW(&wc) != 0;
    }

    UINT dpi = GetDpiForSystem();
    auto s = [&](int v) { return MulDiv(v, static_cast<int>(dpi ? dpi : 96), 96); };

    auto* st = new SchedulePopupState();
    st->sched = prayer::GetTodaySchedule(g_loc, g_mc, g_ma, g_iq, g_lang, g_use12Hour);
    st->light = g_lightApps;
    st->en = (g_lang == AppLang::English);

    auto mkF = [&](int px, int weight) {
        return CreateFontW(-s(px), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    st->fontTitle = mkF(15, FW_SEMIBOLD);
    st->fontSub = mkF(13, FW_NORMAL);
    st->fontRow = mkF(14, FW_NORMAL);
    st->fontBold = mkF(14, FW_SEMIBOLD);

    int W = s(245);
    int H = s(68 + static_cast<int>(st->sched.rows.size()) * 28 + 12);

    RECT wr = widget::ScreenRect();
    POINT anchor{ (wr.left + wr.right) / 2, wr.top };
    HMONITOR mon = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);

    int x = anchor.x - W / 2;
    int y = wr.top - H - s(8);
    if (y < mi.rcWork.top) y = wr.bottom + s(8);
    if (x < mi.rcWork.left + s(6)) x = mi.rcWork.left + s(6);
    if (x + W > mi.rcWork.right - s(6)) x = mi.rcWork.right - W - s(6);

    g_popup = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kPopupClass, L"",
        WS_POPUP | WS_BORDER,
        x, y, W, H, g_hwnd, nullptr, g_inst, st);
    if (!g_popup) {
        DeleteObject(st->fontTitle);
        DeleteObject(st->fontSub);
        DeleteObject(st->fontRow);
        DeleteObject(st->fontBold);
        delete st;
        return;
    }

    BOOL dark = st->light ? FALSE : TRUE;
    DwmSetWindowAttribute(g_popup, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    DWORD cornerPref = 2 /*DWMWCP_ROUND*/;
    DwmSetWindowAttribute(g_popup, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &cornerPref, sizeof(cornerPref));

    ShowWindow(g_popup, SW_SHOW);
    SetForegroundWindow(g_popup);
}

void ShowMenu() {
    if (g_popup && IsWindow(g_popup)) DestroyWindow(g_popup);
    if (g_dialogOpen) return;
    bool en = (g_lang == AppLang::English);
    HMENU m = CreatePopupMenu();
    if (!m) return;

    AppendMenuW(m, MF_STRING, ID_EDIT_LOCATION, en ? L"Edit Location & Search..." : L"تعديل الموقع والبحث...");
    AppendMenuW(m, MF_STRING, ID_EDIT_METHOD, en ? L"Calculation Method & Madhhab..." : L"نظام التوقيت والمذهب...");
    AppendMenuW(m, MF_STRING, ID_EDIT_MANUAL, en ? L"Manual Adhan Times..." : L"ضبط أوقات الأذان يدوياً...");
    AppendMenuW(m, MF_STRING, ID_EDIT_IQAMAH, en ? L"Edit Iqamah Duration..." : L"تعديل مدة الإقامة...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    HMENU langMenu = CreatePopupMenu();
    AppendMenuW(langMenu, MF_STRING | (!en ? MF_CHECKED : 0), ID_LANG_AR, L"العربية");
    AppendMenuW(langMenu, MF_STRING | (en ? MF_CHECKED : 0), ID_LANG_EN, L"English");
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(langMenu), en ? L"Language / اللغة" : L"اللغة / Language");

    AppendMenuW(m, MF_STRING | (g_use12Hour ? MF_CHECKED : 0), ID_TOGGLE_12H,
                en ? L"12-hour format (AM/PM)" : L"تنسيق 12 ساعة (ص/م)");
    AppendMenuW(m, MF_STRING | (g_widgetBackground ? MF_CHECKED : 0), ID_WIDGET_BACKGROUND,
                en ? L"Background behind taskbar text" : L"خلفية خلف النص (للشريط الشفاف)");
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART,
                en ? L"Start with Windows" : L"التشغيل مع ويندوز");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, en ? L"Exit" : L"خروج");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);

    switch (cmd) {
        case ID_EDIT_LOCATION:
            g_dialogOpen = true;
            if (settings_ui::ShowLocationDialog(g_inst, g_hwnd, g_loc, g_lang)) {
                prayer::SaveLocation(g_loc);
                UpdateDisplay();
            }
            g_dialogOpen = false;
            break;
        case ID_EDIT_METHOD:
            g_dialogOpen = true;
            if (settings_ui::ShowMethodDialog(g_inst, g_hwnd, g_mc, g_lang)) {
                prayer::SaveMethod(g_mc);
                UpdateDisplay();
            }
            g_dialogOpen = false;
            break;
        case ID_EDIT_MANUAL:
            g_dialogOpen = true;
            if (settings_ui::ShowManualAdhanDialog(g_inst, g_hwnd, g_loc, g_mc, g_ma, g_lang)) {
                prayer::SaveManualAdhan(g_ma);
                UpdateDisplay();
            }
            g_dialogOpen = false;
            break;
        case ID_EDIT_IQAMAH:
            g_dialogOpen = true;
            if (settings_ui::ShowIqamahDialog(g_inst, g_hwnd, g_iq, g_lang)) {
                prayer::SaveIqamah(g_iq);
                UpdateDisplay();
            }
            g_dialogOpen = false;
            break;
        case ID_LANG_AR:
            g_lang = AppLang::Arabic;
            prayer::SaveLanguage(g_lang);
            UpdateDisplay();
            break;
        case ID_LANG_EN:
            g_lang = AppLang::English;
            prayer::SaveLanguage(g_lang);
            UpdateDisplay();
            break;
        case ID_TOGGLE_12H:
            g_use12Hour = !g_use12Hour;
            prayer::SaveUse12Hour(g_use12Hour);
            UpdateDisplay();
            break;
        case ID_WIDGET_BACKGROUND:
            g_widgetBackground = !g_widgetBackground;
            SaveBoolSetting(L"WidgetBackground", g_widgetBackground);
            widget::SetBackground(g_widgetBackground);
            break;
        case ID_AUTOSTART:
            SetAutostart(!AutostartEnabled());
            break;
        case ID_EXIT:
            DestroyWindow(g_hwnd);
            break;
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreatedMsg && msg != 0) {
        widget::Destroy();
        UpdateDisplay();
        return 0;
    }
    switch (msg) {
        case WM_WIDGET_CLICK:
            if (wp == 1) ShowMenu();
            else if (wp == 0) ShowSchedulePopup();
            return 0;
        case WM_TIMER:
            if (wp == TIMER_TICK) UpdateDisplay();
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC) UpdateDisplay();
            return TRUE;
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW(reinterpret_cast<LPCWSTR>(lp), L"ImmersiveColorSet")) {
                g_lightApps = ReadPersonalizeBool(L"AppsUseLightTheme", false);
                bool light = ReadLightTaskbar();
                if (light != g_lightTaskbar) {
                    g_lightTaskbar = light;
                    ApplyMenuTheme(light);
                    widget::SetLightTheme(light);
                }
            }
            return 0;
        case WM_ENDSESSION:
            if (wp) DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, TIMER_TICK);
            widget::Destroy();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--quit")) {
        if (HWND w = FindWindowW(kWndClass, nullptr)) {
            PostMessageW(w, WM_CLOSE, 0, 0);
            for (int i = 0; i < 50 && FindWindowW(kWndClass, nullptr); i++) Sleep(100);
        }
        return 0;
    }

    g_inst = inst;
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\AdhanTaskbarMonitor");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    if (!LoadBoolSetting(L"AutostartInitDone", false)) {
        SetAutostart(true);
        SaveBoolSetting(L"AutostartInitDone", true);
    }

    g_lightTaskbar = ReadLightTaskbar();
    g_lightApps = ReadPersonalizeBool(L"AppsUseLightTheme", false);
    ApplyMenuTheme(g_lightTaskbar);
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClass, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 1;
    }

    g_lang = prayer::LoadLanguage();
    g_use12Hour = prayer::LoadUse12Hour();
    g_loc = prayer::LoadLocation();
    g_mc = prayer::LoadMethod();
    g_ma = prayer::LoadManualAdhan();
    g_iq = prayer::LoadIqamah();
    g_widgetBackground = LoadBoolSetting(L"WidgetBackground", false);
    widget::SetBackground(g_widgetBackground);

    UpdateDisplay();
    SetTimer(g_hwnd, TIMER_TICK, 1000, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
