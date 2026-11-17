#include "settings_ui.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <vector>

namespace {

constexpr int IDC_SEARCH_EDIT = 100;
constexpr int IDC_SEARCH_ONLINE = 101;
constexpr int IDC_CITY_LIST = 102;
constexpr int IDC_LOC_NAME = 103;
constexpr int IDC_LOC_LAT = 104;
constexpr int IDC_LOC_LON = 105;

constexpr int IDC_CALC_METHOD = 110;
constexpr int IDC_ASR_METHOD = 111;

constexpr int IDC_MANUAL_CHECK = 120;
constexpr int IDC_COPY_ASTRO = 121;
constexpr int IDC_MANUAL_TIME_BASE = 130;   // 130..134
constexpr int IDC_OFFSET_MIN_BASE = 140;    // 140..144

constexpr int IDC_IQ_BASE = 200;            // 200..204

int DpiScale(int px, UINT dpi) {
    return MulDiv(px, static_cast<int>(dpi ? dpi : 96), 96);
}

HFONT CreateUiFont(UINT dpi) {
    return CreateFontW(-DpiScale(14, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void SetDlgFont(HWND wnd, HFONT font) {
    SendMessageW(wnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

std::wstring GetEditText(HWND edit) {
    int len = GetWindowTextLengthW(edit);
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(edit, s.data(), len + 1);
    s.resize(len);
    return s;
}

bool ParseDouble(const std::wstring& s, double& out) {
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    double v = std::wcstod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    while (*end == L' ' || *end == L'\t') end++;
    if (*end != L'\0') return false;
    out = v;
    return true;
}

bool ParseInt(const std::wstring& s, int& out) {
    if (s.empty()) return false;
    wchar_t* end = nullptr;
    long v = std::wcstol(s.c_str(), &end, 10);
    if (end == s.c_str()) return false;
    while (*end == L' ' || *end == L'\t') end++;
    if (*end != L'\0') return false;
    out = static_cast<int>(v);
    return true;
}

bool ParseHHMM(const std::wstring& s, int& outMinutes) {
    int hh = -1, mm = -1;
    if (swscanf_s(s.c_str(), L"%d:%d", &hh, &mm) != 2) return false;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    outMinutes = hh * 60 + mm;
    return true;
}

std::wstring FormatHHMM(int minutes) {
    minutes = std::clamp(minutes, 0, 1439);
    wchar_t buf[16]{};
    swprintf_s(buf, L"%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

std::wstring NormalizeForSearch(const std::wstring& in) {
    std::wstring out;
    out.reserve(in.size());
    for (wchar_t c : in) {
        if (c >= 0x064B && c <= 0x065F) continue;  // Arabic tashkeel
        if (c == 0x0670 || c == 0x0640) continue;   // superscript alef / tatweel
        if (c == L'أ' || c == L'إ' || c == L'آ') c = L'ا';
        else if (c == L'ة') c = L'ه';
        else if (c == L'ى') c = L'ي';
        else c = towlower(c);
        out.push_back(c);
    }
    return out;
}

int EditDistance(const std::wstring& a, const std::wstring& b) {
    size_t n = a.size(), m = b.size();
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; j++) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= n; i++) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= m; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

int MinWordEditDistance(const std::wstring& q, const std::wstring& full) {
    int best = EditDistance(q, full);
    size_t i = 0;
    while (i < full.size()) {
        while (i < full.size() && (full[i] == L' ' || full[i] == L'-' || full[i] == L'/' || full[i] == L',')) i++;
        if (i >= full.size()) break;
        size_t j = i;
        while (j < full.size() && full[j] != L' ' && full[j] != L'-' && full[j] != L'/' && full[j] != L',') j++;
        std::wstring word = full.substr(i, j - i);
        best = std::min(best, EditDistance(q, word));
        i = j;
    }
    return best;
}

struct ThemeColors {
    bool light = false;
    HBRUSH bgBrush = nullptr;
    HBRUSH editBrush = nullptr;
};

bool IsAppLightTheme() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS) {
        return v != 0;
    }
    return false;  // Dark mode by default
}

void InitDialogTheme(HWND dlg, ThemeColors& tc) {
    tc.light = IsAppLightTheme();
    BOOL dark = tc.light ? FALSE : TRUE;
    DwmSetWindowAttribute(dlg, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    tc.bgBrush = CreateSolidBrush(tc.light ? RGB(240, 240, 240) : RGB(32, 32, 32));
    tc.editBrush = CreateSolidBrush(tc.light ? RGB(255, 255, 255) : RGB(45, 45, 45));
}

void FreeDialogTheme(ThemeColors& tc) {
    if (tc.bgBrush) { DeleteObject(tc.bgBrush); tc.bgBrush = nullptr; }
    if (tc.editBrush) { DeleteObject(tc.editBrush); tc.editBrush = nullptr; }
}

void ThemeCtrl(HWND ctrl, bool light) {
    SetWindowTheme(ctrl, light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
}

bool HandleThemeMsg(HWND h, UINT msg, WPARAM wp, const ThemeColors& tc, LRESULT& out) {
    if (msg == WM_ERASEBKGND) {
        RECT rc{};
        GetClientRect(h, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, tc.bgBrush);
        out = 1;
        return true;
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, tc.light ? RGB(20, 20, 20) : RGB(240, 240, 240));
        out = reinterpret_cast<LRESULT>(tc.bgBrush);
        return true;
    }
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkColor(hdc, tc.light ? RGB(255, 255, 255) : RGB(45, 45, 45));
        SetTextColor(hdc, tc.light ? RGB(20, 20, 20) : RGB(240, 240, 240));
        out = reinterpret_cast<LRESULT>(tc.editBrush);
        return true;
    }
    return false;
}

void CenterWindowOnScreen(HWND wnd) {
    RECT r{};
    GetWindowRect(wnd, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    POINT pt{};
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - w) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - h) / 2;
    SetWindowPos(wnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

// ---- Location Dialog with Smart Search --------------------------------------------------------

struct LocDlgState {
    LocationConfig* loc = nullptr;
    AppLang lang = AppLang::Arabic;
    HFONT font = nullptr;
    ThemeColors theme;
    HWND editSearch = nullptr;
    HWND listCities = nullptr;
    HWND editName = nullptr;
    HWND editLat = nullptr;
    HWND editLon = nullptr;
    std::vector<LocationConfig> shownCities;
    bool accepted = false;
    bool done = false;
};

void PopulateLocalFilter(LocDlgState* st, const std::wstring& filter) {
    SendMessageW(st->listCities, LB_RESETCONTENT, 0, 0);
    st->shownCities.clear();
    std::wstring q = NormalizeForSearch(filter);
    bool en = (st->lang == AppLang::English);

    struct ScoredCity {
        int score;
        std::wstring disp;
        double lat, lon;
    };
    std::vector<ScoredCity> matches;

    for (const auto& p : prayer::Presets()) {
        const std::wstring& disp = en ? std::wstring(p.nameEn) : std::wstring(p.nameAr);
        if (q.empty()) {
            matches.push_back({ 0, disp, p.latitude, p.longitude });
            continue;
        }
        std::wstring arNorm = NormalizeForSearch(p.nameAr);
        std::wstring enNorm = NormalizeForSearch(p.nameEn);
        if (arNorm.find(q) != std::wstring::npos || enNorm.find(q) != std::wstring::npos) {
            matches.push_back({ 0, disp, p.latitude, p.longitude });
            continue;
        }
        int d = std::min(MinWordEditDistance(q, arNorm), MinWordEditDistance(q, enNorm));
        int maxAllowed = std::max(1, static_cast<int>(q.size()) / 3 + 1);
        if (d <= maxAllowed) {
            matches.push_back({ d, disp, p.latitude, p.longitude });
        }
    }

    if (!q.empty()) {
        std::stable_sort(matches.begin(), matches.end(), [](const ScoredCity& a, const ScoredCity& b) {
            return a.score < b.score;
        });
    }
    for (const auto& m : matches) {
        st->shownCities.push_back({ m.disp, m.lat, m.lon });
        SendMessageW(st->listCities, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(m.disp.c_str()));
    }
}

void ApplySelectedCity(LocDlgState* st, int idx) {
    if (idx < 0 || idx >= static_cast<int>(st->shownCities.size())) return;
    const auto& c = st->shownCities[idx];
    SetWindowTextW(st->editName, c.name.c_str());
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.4f", c.latitude);
    SetWindowTextW(st->editLat, buf);
    swprintf_s(buf, L"%.4f", c.longitude);
    SetWindowTextW(st->editLon, buf);
}

LRESULT CALLBACK LocDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<LocDlgState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (st) {
        LRESULT res = 0;
        if (HandleThemeMsg(h, msg, wp, st->theme, res)) return res;
    }
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_COMMAND: {
            if (!st) break;
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == IDC_SEARCH_EDIT && code == EN_CHANGE) {
                PopulateLocalFilter(st, GetEditText(st->editSearch));
                return 0;
            }
            if (id == IDC_SEARCH_ONLINE) {
                bool en = (st->lang == AppLang::English);
                std::wstring q = GetEditText(st->editSearch);
                if (q.empty()) {
                    MessageBoxW(h, en ? L"Please enter a city or town name first." : L"اكتب اسم المدينة أو القرية في خانة البحث أولاً.",
                                en ? L"Notice" : L"تنبيه",
                                MB_OK | MB_ICONINFORMATION | (en ? 0 : (MB_RTLREADING | MB_RIGHT)));
                    return 0;
                }
                SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                auto onlineRes = prayer::SearchCitiesOnline(q, st->lang);
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                PopulateLocalFilter(st, q);
                std::vector<LocationConfig> merged = st->shownCities;
                for (auto& oc : onlineRes) {
                    bool dup = false;
                    for (const auto& ex : merged) {
                        if (std::fabs(ex.latitude - oc.latitude) < 0.05 && std::fabs(ex.longitude - oc.longitude) < 0.05) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) merged.push_back(std::move(oc));
                }
                if (merged.empty()) {
                    MessageBoxW(h, en ? L"No results found or connection failed." : L"لم يتم العثور على نتائج عبر الإنترنت أو تعذر الاتصال.",
                                en ? L"Search Results" : L"نتائج البحث",
                                MB_OK | MB_ICONINFORMATION | (en ? 0 : (MB_RTLREADING | MB_RIGHT)));
                } else {
                    SendMessageW(st->listCities, LB_RESETCONTENT, 0, 0);
                    st->shownCities = std::move(merged);
                    for (const auto& c : st->shownCities) {
                        SendMessageW(st->listCities, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.name.c_str()));
                    }
                    SendMessageW(st->listCities, LB_SETCURSEL, 0, 0);
                    ApplySelectedCity(st, 0);
                }
                return 0;
            }
            if (id == IDC_CITY_LIST && (code == LBN_SELCHANGE || code == LBN_DBLCLK)) {
                int idx = static_cast<int>(SendMessageW(st->listCities, LB_GETCURSEL, 0, 0));
                ApplySelectedCity(st, idx);
                return 0;
            }
            if (id == IDOK) {
                bool en = (st->lang == AppLang::English);
                std::wstring name = GetEditText(st->editName);
                double lat = 0, lon = 0;
                if (name.empty() || !ParseDouble(GetEditText(st->editLat), lat) || !ParseDouble(GetEditText(st->editLon), lon) ||
                    lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
                    MessageBoxW(h,
                                en ? L"Please enter a valid location name and coordinates (Lat -90..90, Lon -180..180)."
                                   : L"يرجى إدخال اسم الموقع وإحداثيات صحيحة (خط العرض بين -90 و 90، وخط الطول بين -180 و 180).",
                                en ? L"Notice" : L"تنبيه",
                                MB_OK | MB_ICONWARNING | (en ? 0 : (MB_RTLREADING | MB_RIGHT)));
                    return 0;
                }
                st->loc->name = name;
                st->loc->latitude = lat;
                st->loc->longitude = lon;
                st->accepted = true;
                st->done = true;
                return 0;
            }
            if (id == IDCANCEL) {
                st->done = true;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st) st->done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- Method & Madhhab Dialog ------------------------------------------------------------------

struct MethodDlgState {
    MethodConfig* mc = nullptr;
    HFONT font = nullptr;
    ThemeColors theme;
    HWND comboCalc = nullptr;
    HWND comboAsr = nullptr;
    bool accepted = false;
    bool done = false;
};

LRESULT CALLBACK MethodDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<MethodDlgState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (st) {
        LRESULT res = 0;
        if (HandleThemeMsg(h, msg, wp, st->theme, res)) return res;
    }
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_COMMAND: {
            if (!st) break;
            int id = LOWORD(wp);
            if (id == IDOK) {
                int cm = static_cast<int>(SendMessageW(st->comboCalc, CB_GETCURSEL, 0, 0));
                int am = static_cast<int>(SendMessageW(st->comboAsr, CB_GETCURSEL, 0, 0));
                if (cm >= 0 && cm < static_cast<int>(CalcMethod::Count))
                    st->mc->calcMethod = static_cast<CalcMethod>(cm);
                if (am >= 0 && am <= 1)
                    st->mc->asrMethod = static_cast<AsrMethod>(am);
                st->accepted = true;
                st->done = true;
                return 0;
            }
            if (id == IDCANCEL) {
                st->done = true;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st) st->done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- Manual Adhan & Offsets Dialog ------------------------------------------------------------

struct ManualDlgState {
    const LocationConfig* loc = nullptr;
    const MethodConfig* mc = nullptr;
    ManualAdhanConfig* ma = nullptr;
    AppLang lang = AppLang::Arabic;
    HFONT font = nullptr;
    ThemeColors theme;
    HWND chkManual = nullptr;
    HWND btnCopy = nullptr;
    HWND editTime[5]{};
    HWND editOff[5]{};
    bool accepted = false;
    bool done = false;
};

void UpdateManualEnabled(ManualDlgState* st) {
    bool manual = (SendMessageW(st->chkManual, BM_GETCHECK, 0, 0) == BST_CHECKED);
    EnableWindow(st->btnCopy, manual ? TRUE : FALSE);
    for (int i = 0; i < 5; i++) {
        EnableWindow(st->editTime[i], manual ? TRUE : FALSE);
        EnableWindow(st->editOff[i], manual ? FALSE : TRUE);
    }
}

void FillFromAstro(ManualDlgState* st) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    TIME_ZONE_INFORMATION tzi{};
    DWORD mode = GetTimeZoneInformation(&tzi);
    LONG bias = tzi.Bias + (mode == TIME_ZONE_ID_DAYLIGHT ? tzi.DaylightBias : (mode == TIME_ZONE_ID_STANDARD ? tzi.StandardBias : 0));
    double tz = -static_cast<double>(bias) / 60.0;
    ManualAdhanConfig zero{};
    DayPrayerTimes d = prayer::CalculateDay(now.wYear, now.wMonth, now.wDay,
                                            st->loc->latitude, st->loc->longitude, tz, *st->mc, zero);
    for (int i = 0; i < 5; i++) {
        int min = static_cast<int>(std::llround(d.adhanHours[i] * 60.0)) % 1440;
        if (min < 0) min += 1440;
        SetWindowTextW(st->editTime[i], FormatHHMM(min).c_str());
    }
}

LRESULT CALLBACK ManualDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ManualDlgState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (st) {
        LRESULT res = 0;
        if (HandleThemeMsg(h, msg, wp, st->theme, res)) return res;
    }
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_COMMAND: {
            if (!st) break;
            int id = LOWORD(wp);
            if (id == IDC_MANUAL_CHECK) {
                UpdateManualEnabled(st);
                return 0;
            }
            if (id == IDC_COPY_ASTRO) {
                FillFromAstro(st);
                return 0;
            }
            if (id == IDOK) {
                bool en = (st->lang == AppLang::English);
                ManualAdhanConfig next = *st->ma;
                next.useManual = (SendMessageW(st->chkManual, BM_GETCHECK, 0, 0) == BST_CHECKED);
                for (int i = 0; i < 5; i++) {
                    int m = 0;
                    if (!ParseHHMM(GetEditText(st->editTime[i]), m)) {
                        MessageBoxW(h,
                                    en ? L"Please enter manual time in HH:MM format (e.g. 05:23)."
                                       : L"يرجى إدخال الوقت اليدوي بصيغة صحيحة (HH:MM) مثل 05:23.",
                                    en ? L"Notice" : L"تنبيه",
                                    MB_OK | MB_ICONWARNING | (en ? 0 : (MB_RTLREADING | MB_RIGHT)));
                        return 0;
                    }
                    next.manualMinutes[i] = m;

                    int off = 0;
                    if (!ParseInt(GetEditText(st->editOff[i]), off) || off < -60 || off > 60) {
                        MessageBoxW(h,
                                    en ? L"Please enter minute offset between -60 and 60."
                                       : L"يرجى إدخال التعديل بالدقائق بين -60 و 60.",
                                    en ? L"Notice" : L"تنبيه",
                                    MB_OK | MB_ICONWARNING | (en ? 0 : (MB_RTLREADING | MB_RIGHT)));
                        return 0;
                    }
                    next.offsetMinutes[i] = off;
                }
                *st->ma = next;
                st->accepted = true;
                st->done = true;
                return 0;
            }
            if (id == IDCANCEL) {
                st->done = true;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st) st->done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- Iqamah Dialog ----------------------------------------------------------------------------

struct IqDlgState {
    IqamahConfig* iq = nullptr;
    AppLang lang = AppLang::Arabic;
    HFONT font = nullptr;
    ThemeColors theme;
    HWND edits[5]{};
    bool accepted = false;
    bool done = false;
};

LRESULT CALLBACK IqDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<IqDlgState*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (st) {
        LRESULT res = 0;
        if (HandleThemeMsg(h, msg, wp, st->theme, res)) return res;
    }
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_COMMAND: {
            if (!st) break;
            int id = LOWORD(wp);
            if (id == IDOK) {
                bool en = (st->lang == AppLang::English);
                IqamahConfig next;
                for (int i = 0; i < 5; i++) {
                    BOOL ok = FALSE;
                    UINT val = GetDlgItemInt(h, IDC_IQ_BASE + i, &ok, FALSE);
                    if (!ok || val > 120) {
                        MessageBoxW(h,
                                    en ? L"Please enter a valid duration in minutes (0 to 120)."
                                       : L"يرجى إدخال مدة صحيحة بالدقائق (من 0 إلى 120).",
                                    en ? L"Notice" : L"تنبيه",
                                    MB_OK | MB_ICONWARNING | (en ? 0 : (MB_RTLREADING | MB_RIGHT)));
                        return 0;
                    }
                    next.minutes[i] = static_cast<int>(val);
                }
                *st->iq = next;
                st->accepted = true;
                st->done = true;
                return 0;
            }
            if (id == IDCANCEL) {
                st->done = true;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st) st->done = true;
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace settings_ui {

bool ShowLocationDialog(HINSTANCE inst, HWND parent, LocationConfig& loc, AppLang lang) {
    bool en = (lang == AppLang::English);
    static bool reg = false;
    const wchar_t* cls = L"AdhanLocationDlg";
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = LocDlgProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        reg = RegisterClassExW(&wc) != 0;
    }

    UINT dpi = GetDpiForSystem();
    auto s = [&](int v) { return DpiScale(v, dpi); };

    LocDlgState st;
    st.loc = &loc;
    st.lang = lang;
    st.font = CreateUiFont(dpi);

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | (en ? 0 : WS_EX_LAYOUTRTL), cls, en ? L"Edit Location & Search" : L"تعديل الموقع والبحث",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, s(390), s(385), parent, nullptr, inst, &st);
    if (!dlg) { DeleteObject(st.font); return false; }
    InitDialogTheme(dlg, st.theme);

    auto mkLabel = [&](const wchar_t* txt, int x, int y, int w, int h) {
        HWND c = CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE, x, y, w, h, dlg, nullptr, inst, nullptr);
        SetDlgFont(c, st.font);
        return c;
    };
    auto mkEdit = [&](int id, const wchar_t* txt, int x, int y, int w, int h) {
        HWND c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 x, y, w, h, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SetDlgFont(c, st.font);
        return c;
    };

    mkLabel(en ? L"Search city:" : L"بحث عن مدينة:", s(14), s(16), s(90), s(20));
    st.editSearch = mkEdit(IDC_SEARCH_EDIT, L"", s(106), s(12), s(168), s(24));
    HWND btnOnline = CreateWindowExW(0, L"BUTTON", en ? L"Search Online" : L"بحث بالإنترنت", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     s(280), s(12), s(90), s(24), dlg, reinterpret_cast<HMENU>(IDC_SEARCH_ONLINE), inst, nullptr);
    SetDlgFont(btnOnline, st.font);
    ThemeCtrl(btnOnline, st.theme.light);

    st.listCities = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
                                    s(14), s(44), s(356), s(140), dlg, reinterpret_cast<HMENU>(IDC_CITY_LIST), inst, nullptr);
    SetDlgFont(st.listCities, st.font);
    ThemeCtrl(st.listCities, st.theme.light);
    PopulateLocalFilter(&st, L"");

    mkLabel(en ? L"Location name:" : L"اسم الموقع:", s(14), s(196), s(90), s(20));
    std::wstring initLocName = (en && loc.name == L"مكة المكرمة") ? L"Makkah"
                             : (en && loc.name == L"ينبع الصناعية") ? L"Yanbu Industrial City"
                             : loc.name;
    st.editName = mkEdit(IDC_LOC_NAME, initLocName.c_str(), s(106), s(192), s(264), s(24));

    wchar_t latBuf[64]{}, lonBuf[64]{};
    swprintf_s(latBuf, L"%.4f", loc.latitude);
    swprintf_s(lonBuf, L"%.4f", loc.longitude);

    mkLabel(en ? L"Latitude:" : L"خط العرض:", s(14), s(232), s(90), s(20));
    st.editLat = mkEdit(IDC_LOC_LAT, latBuf, s(106), s(228), s(264), s(24));

    mkLabel(en ? L"Longitude:" : L"خط الطول:", s(14), s(268), s(90), s(20));
    st.editLon = mkEdit(IDC_LOC_LON, lonBuf, s(106), s(264), s(264), s(24));

    HWND okBtn = CreateWindowExW(0, L"BUTTON", en ? L"Save" : L"حفظ", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 s(188), s(308), s(88), s(28), dlg, reinterpret_cast<HMENU>(IDOK), inst, nullptr);
    HWND cancelBtn = CreateWindowExW(0, L"BUTTON", en ? L"Cancel" : L"إلغاء", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     s(282), s(308), s(88), s(28), dlg, reinterpret_cast<HMENU>(IDCANCEL), inst, nullptr);
    SetDlgFont(okBtn, st.font);
    SetDlgFont(cancelBtn, st.font);
    ThemeCtrl(okBtn, st.theme.light);
    ThemeCtrl(cancelBtn, st.theme.light);

    CenterWindowOnScreen(dlg);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(dlg);
    FreeDialogTheme(st.theme);
    DeleteObject(st.font);
    return st.accepted;
}

bool ShowMethodDialog(HINSTANCE inst, HWND parent, MethodConfig& mc, AppLang lang) {
    bool en = (lang == AppLang::English);
    static bool reg = false;
    const wchar_t* cls = L"AdhanMethodDlg";
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = MethodDlgProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        reg = RegisterClassExW(&wc) != 0;
    }

    UINT dpi = GetDpiForSystem();
    auto s = [&](int v) { return DpiScale(v, dpi); };

    MethodDlgState st;
    st.mc = &mc;
    st.font = CreateUiFont(dpi);

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | (en ? 0 : WS_EX_LAYOUTRTL), cls,
        en ? L"Calculation Method & Madhhab" : L"نظام التوقيت والمذهب الفقهي",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, s(400), s(185), parent, nullptr, inst, &st);
    if (!dlg) { DeleteObject(st.font); return false; }
    InitDialogTheme(dlg, st.theme);

    HWND lbl1 = CreateWindowExW(0, L"STATIC", en ? L"Method:" : L"طريقة الحساب:", WS_CHILD | WS_VISIBLE,
                                s(16), s(20), s(95), s(20), dlg, nullptr, inst, nullptr);
    SetDlgFont(lbl1, st.font);

    st.comboCalc = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                                   s(115), s(16), s(260), s(220), dlg, reinterpret_cast<HMENU>(IDC_CALC_METHOD), inst, nullptr);
    SetDlgFont(st.comboCalc, st.font);
    ThemeCtrl(st.comboCalc, st.theme.light);
    for (int i = 0; i < static_cast<int>(CalcMethod::Count); i++) {
        SendMessageW(st.comboCalc, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(prayer::CalcMethodName(static_cast<CalcMethod>(i), lang)));
    }
    SendMessageW(st.comboCalc, CB_SETCURSEL, static_cast<int>(mc.calcMethod), 0);

    HWND lbl2 = CreateWindowExW(0, L"STATIC", en ? L"Asr Juristic:" : L"مذهب العصر:", WS_CHILD | WS_VISIBLE,
                                s(16), s(62), s(95), s(20), dlg, nullptr, inst, nullptr);
    SetDlgFont(lbl2, st.font);

    st.comboAsr = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                  s(115), s(58), s(260), s(120), dlg, reinterpret_cast<HMENU>(IDC_ASR_METHOD), inst, nullptr);
    SetDlgFont(st.comboAsr, st.font);
    ThemeCtrl(st.comboAsr, st.theme.light);
    SendMessageW(st.comboAsr, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(prayer::AsrMethodName(AsrMethod::Standard, lang)));
    SendMessageW(st.comboAsr, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(prayer::AsrMethodName(AsrMethod::Hanafi, lang)));
    SendMessageW(st.comboAsr, CB_SETCURSEL, static_cast<int>(mc.asrMethod), 0);

    HWND okBtn = CreateWindowExW(0, L"BUTTON", en ? L"Save" : L"حفظ", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 s(195), s(106), s(86), s(28), dlg, reinterpret_cast<HMENU>(IDOK), inst, nullptr);
    HWND cancelBtn = CreateWindowExW(0, L"BUTTON", en ? L"Cancel" : L"إلغاء", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     s(289), s(106), s(86), s(28), dlg, reinterpret_cast<HMENU>(IDCANCEL), inst, nullptr);
    SetDlgFont(okBtn, st.font);
    SetDlgFont(cancelBtn, st.font);
    ThemeCtrl(okBtn, st.theme.light);
    ThemeCtrl(cancelBtn, st.theme.light);

    CenterWindowOnScreen(dlg);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(dlg);
    FreeDialogTheme(st.theme);
    DeleteObject(st.font);
    return st.accepted;
}

bool ShowManualAdhanDialog(HINSTANCE inst, HWND parent, const LocationConfig& loc,
                           const MethodConfig& mc, ManualAdhanConfig& ma, AppLang lang) {
    bool en = (lang == AppLang::English);
    static bool reg = false;
    const wchar_t* cls = L"AdhanManualDlg";
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = ManualDlgProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        reg = RegisterClassExW(&wc) != 0;
    }

    UINT dpi = GetDpiForSystem();
    auto s = [&](int v) { return DpiScale(v, dpi); };

    ManualDlgState st;
    st.loc = &loc;
    st.mc = &mc;
    st.ma = &ma;
    st.lang = lang;
    st.font = CreateUiFont(dpi);

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | (en ? 0 : WS_EX_LAYOUTRTL), cls, en ? L"Manual Adhan Times" : L"ضبط أوقات الأذان يدوياً",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, s(350), s(330), parent, nullptr, inst, &st);
    if (!dlg) { DeleteObject(st.font); return false; }
    InitDialogTheme(dlg, st.theme);

    st.chkManual = CreateWindowExW(0, L"BUTTON",
                                   en ? L"Use fixed manual Adhan times (HH:MM)" : L"استخدام أوقات أذان يدوية ثابتة (HH:MM)",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                   s(16), s(14), s(310), s(22), dlg, reinterpret_cast<HMENU>(IDC_MANUAL_CHECK), inst, nullptr);
    SetDlgFont(st.chkManual, st.font);
    ThemeCtrl(st.chkManual, st.theme.light);
    SendMessageW(st.chkManual, BM_SETCHECK, ma.useManual ? BST_CHECKED : BST_UNCHECKED, 0);

    st.btnCopy = CreateWindowExW(0, L"BUTTON",
                                 en ? L"Copy from today's calculated times" : L"نسخ من الحساب الفلكي لليوم",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                 s(16), s(42), s(210), s(24), dlg, reinterpret_cast<HMENU>(IDC_COPY_ASTRO), inst, nullptr);
    SetDlgFont(st.btnCopy, st.font);
    ThemeCtrl(st.btnCopy, st.theme.light);

    HWND hTime = CreateWindowExW(0, L"STATIC", en ? L"Manual (HH:MM)" : L"الوقت اليدوي", WS_CHILD | WS_VISIBLE,
                                 s(100), s(76), s(100), s(18), dlg, nullptr, inst, nullptr);
    HWND hOff = CreateWindowExW(0, L"STATIC", en ? L"Offset (± min)" : L"تعديل (± دقيقة)", WS_CHILD | WS_VISIBLE,
                                s(212), s(76), s(110), s(18), dlg, nullptr, inst, nullptr);
    SetDlgFont(hTime, st.font);
    SetDlgFont(hOff, st.font);

    for (int i = 0; i < 5; i++) {
        int y = s(100 + i * 32);
        std::wstring label = std::wstring(prayer::PrayerName(static_cast<PrayerId>(i), lang)) + L":";
        HWND lbl = CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE,
                                   s(16), y + s(3), s(76), s(20), dlg, nullptr, inst, nullptr);
        SetDlgFont(lbl, st.font);

        st.editTime[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", FormatHHMM(ma.manualMinutes[i]).c_str(),
                                         WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                         s(100), y, s(86), s(24), dlg,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MANUAL_TIME_BASE + i)), inst, nullptr);
        SetDlgFont(st.editTime[i], st.font);

        wchar_t offBuf[16]{};
        swprintf_s(offBuf, L"%d", ma.offsetMinutes[i]);
        st.editOff[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", offBuf,
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        s(212), y, s(70), s(24), dlg,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OFFSET_MIN_BASE + i)), inst, nullptr);
        SetDlgFont(st.editOff[i], st.font);
    }

    UpdateManualEnabled(&st);

    HWND okBtn = CreateWindowExW(0, L"BUTTON", en ? L"Save" : L"حفظ", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 s(146), s(266), s(86), s(28), dlg, reinterpret_cast<HMENU>(IDOK), inst, nullptr);
    HWND cancelBtn = CreateWindowExW(0, L"BUTTON", en ? L"Cancel" : L"إلغاء", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     s(240), s(266), s(86), s(28), dlg, reinterpret_cast<HMENU>(IDCANCEL), inst, nullptr);
    SetDlgFont(okBtn, st.font);
    SetDlgFont(cancelBtn, st.font);
    ThemeCtrl(okBtn, st.theme.light);
    ThemeCtrl(cancelBtn, st.theme.light);

    CenterWindowOnScreen(dlg);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(dlg);
    FreeDialogTheme(st.theme);
    DeleteObject(st.font);
    return st.accepted;
}

bool ShowIqamahDialog(HINSTANCE inst, HWND parent, IqamahConfig& iq, AppLang lang) {
    bool en = (lang == AppLang::English);
    static bool reg = false;
    const wchar_t* cls = L"AdhanIqamahDlg";
    if (!reg) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = IqDlgProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = cls;
        reg = RegisterClassExW(&wc) != 0;
    }

    UINT dpi = GetDpiForSystem();
    auto s = [&](int v) { return DpiScale(v, dpi); };

    IqDlgState st;
    st.iq = &iq;
    st.lang = lang;
    st.font = CreateUiFont(dpi);

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | (en ? 0 : WS_EX_LAYOUTRTL), cls,
        en ? L"Edit Iqamah Duration (minutes)" : L"تعديل مدة الإقامة (بالدقائق)",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, s(280), s(280), parent, nullptr, inst, &st);
    if (!dlg) { DeleteObject(st.font); return false; }
    InitDialogTheme(dlg, st.theme);

    for (int i = 0; i < 5; i++) {
        int y = s(18 + i * 36);
        std::wstring label = std::wstring(prayer::PrayerName(static_cast<PrayerId>(i), lang)) + L":";
        HWND lbl = CreateWindowExW(0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE,
                                   s(18), y + s(3), s(90), s(20), dlg, nullptr, inst, nullptr);
        SetDlgFont(lbl, st.font);

        wchar_t val[16]{};
        swprintf_s(val, L"%d", iq.minutes[i]);
        st.edits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER,
                                      s(115), y, s(75), s(24), dlg,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_IQ_BASE + i)), inst, nullptr);
        SetDlgFont(st.edits[i], st.font);

        HWND unit = CreateWindowExW(0, L"STATIC", en ? L"min" : L"دقيقة", WS_CHILD | WS_VISIBLE,
                                    s(198), y + s(3), s(50), s(20), dlg, nullptr, inst, nullptr);
        SetDlgFont(unit, st.font);
    }

    HWND okBtn = CreateWindowExW(0, L"BUTTON", en ? L"Save" : L"حفظ", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 s(72), s(204), s(86), s(28), dlg, reinterpret_cast<HMENU>(IDOK), inst, nullptr);
    HWND cancelBtn = CreateWindowExW(0, L"BUTTON", en ? L"Cancel" : L"إلغاء", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                     s(166), s(204), s(86), s(28), dlg, reinterpret_cast<HMENU>(IDCANCEL), inst, nullptr);
    SetDlgFont(okBtn, st.font);
    SetDlgFont(cancelBtn, st.font);
    ThemeCtrl(okBtn, st.theme.light);
    ThemeCtrl(cancelBtn, st.theme.light);

    CenterWindowOnScreen(dlg);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DestroyWindow(dlg);
    FreeDialogTheme(st.theme);
    DeleteObject(st.font);
    return st.accepted;
}

}  // namespace settings_ui
