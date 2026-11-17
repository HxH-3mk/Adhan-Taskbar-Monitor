#include "prayer.h"

#include <winhttp.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

const wchar_t* kSettingsKey = L"Software\\AdhanTaskbarMonitor";

double FixAngle(double a) {
    a = std::fmod(a, 360.0);
    return a < 0.0 ? a + 360.0 : a;
}

double FixHour(double h) {
    h = std::fmod(h, 24.0);
    return h < 0.0 ? h + 24.0 : h;
}

double SinDeg(double d) { return std::sin(d * kDegToRad); }
double CosDeg(double d) { return std::cos(d * kDegToRad); }
double TanDeg(double d) { return std::tan(d * kDegToRad); }
double AsinDeg(double x) { return std::asin(std::clamp(x, -1.0, 1.0)) * kRadToDeg; }
double AcosDeg(double x) { return std::acos(std::clamp(x, -1.0, 1.0)) * kRadToDeg; }
double AtanDeg(double x) { return std::atan(x) * kRadToDeg; }
double Atan2Deg(double y, double x) { return std::atan2(y, x) * kRadToDeg; }

double JulianDay(int year, int month, int day) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int a = year / 100;
    int b = 2 - a + a / 4;
    return std::floor(365.25 * (year + 4716)) + std::floor(30.6001 * (month + 1)) + day + b - 1524.5;
}

struct SunPos {
    double decl;
    double eqTime;
};

SunPos ComputeSunPosition(double jd) {
    double d = jd - 2451545.0;
    double g = FixAngle(357.529 + 0.98560028 * d);
    double q = FixAngle(280.459 + 0.98564736 * d);
    double l = FixAngle(q + 1.915 * SinDeg(g) + 0.020 * SinDeg(2.0 * g));
    double e = 23.439 - 0.00000036 * d;
    double ra = FixHour(Atan2Deg(CosDeg(e) * SinDeg(l), CosDeg(l)) / 15.0);
    double decl = AsinDeg(SinDeg(e) * SinDeg(l));
    double eqTime = q / 15.0 - ra;
    if (eqTime > 12.0) eqTime -= 24.0;
    if (eqTime < -12.0) eqTime += 24.0;
    return { decl, eqTime };
}

bool TrySunAngleTime(double jdBase, double lat, double angleDeg, bool afterNoon, double& outOffset) {
    SunPos sp = ComputeSunPosition(jdBase);
    double cosH = (-SinDeg(angleDeg) - SinDeg(lat) * SinDeg(sp.decl)) / (CosDeg(lat) * CosDeg(sp.decl));
    if (cosH < -1.0 || cosH > 1.0) return false;
    double h = AcosDeg(cosH) / 15.0;
    outOffset = afterNoon ? h : -h;
    return true;
}

double AsrOffset(double jdBase, double lat, AsrMethod asrMethod) {
    SunPos sp = ComputeSunPosition(jdBase);
    double factor = (asrMethod == AsrMethod::Hanafi) ? 2.0 : 1.0;
    double alt = AtanDeg(1.0 / (factor + TanDeg(std::fabs(lat - sp.decl))));
    double cosH = (SinDeg(alt) - SinDeg(lat) * SinDeg(sp.decl)) / (CosDeg(lat) * CosDeg(sp.decl));
    cosH = std::clamp(cosH, -1.0, 1.0);
    return AcosDeg(cosH) / 15.0;
}

struct MethodParams {
    double fajrAngle;
    bool ishaByMinutes;
    double ishaValue;     // angle in degrees, or minutes after Maghrib if ishaByMinutes
    double maghribAngle;  // 0.833 for sunset, 4.0 for Jafari
};

MethodParams GetMethodParams(CalcMethod m) {
    switch (m) {
        case CalcMethod::UmmAlQura:         return { 18.5, true,  90.0, 0.833 };
        case CalcMethod::MuslimWorldLeague: return { 18.0, false, 17.0, 0.833 };
        case CalcMethod::Egyptian:          return { 19.5, false, 17.5, 0.833 };
        case CalcMethod::Karachi:           return { 18.0, false, 18.0, 0.833 };
        case CalcMethod::ISNA:              return { 15.0, false, 15.0, 0.833 };
        case CalcMethod::UOIF:              return { 12.0, false, 12.0, 0.833 };
        case CalcMethod::Diyanet:           return { 18.0, false, 17.0, 0.833 };
        case CalcMethod::Jafari:            return { 16.0, false, 14.0, 4.0 };
        default:                            return { 18.5, true,  90.0, 0.833 };
    }
}

double LocalTimezoneHours() {
    TIME_ZONE_INFORMATION tzi{};
    DWORD mode = GetTimeZoneInformation(&tzi);
    LONG totalBiasMin = tzi.Bias;
    if (mode == TIME_ZONE_ID_DAYLIGHT) totalBiasMin += tzi.DaylightBias;
    else if (mode == TIME_ZONE_ID_STANDARD) totalBiasMin += tzi.StandardBias;
    return -static_cast<double>(totalBiasMin) / 60.0;
}

ULONGLONG LocalMidnightTicks(int year, int month, int day) {
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    FILETIME ft{};
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER ui{};
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return ui.QuadPart;
}

ULONGLONG LocalNowTicks(SYSTEMTIME& outLocal) {
    GetLocalTime(&outLocal);
    FILETIME ft{};
    SystemTimeToFileTime(&outLocal, &ft);
    ULARGE_INTEGER ui{};
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return ui.QuadPart;
}

void AddDays(int& year, int& month, int& day, int deltaDays) {
    ULONGLONG ticks = LocalMidnightTicks(year, month, day);
    LONGLONG shifted = static_cast<LONGLONG>(ticks) + static_cast<LONGLONG>(deltaDays) * 86400LL * 10000000LL;
    ULARGE_INTEGER ui{};
    ui.QuadPart = static_cast<ULONGLONG>(shifted);
    FILETIME ft{ ui.LowPart, ui.HighPart };
    SYSTEMTIME st{};
    FileTimeToSystemTime(&ft, &st);
    year = st.wYear;
    month = st.wMonth;
    day = st.wDay;
}

std::wstring FormatDuration(int totalSeconds) {
    if (totalSeconds < 0) totalSeconds = 0;
    int h = totalSeconds / 3600;
    int m = (totalSeconds % 3600) / 60;
    int s = totalSeconds % 60;
    wchar_t buf[32]{};
    if (h > 0) swprintf_s(buf, L"%02d:%02d:%02d", h, m, s);
    else swprintf_s(buf, L"%02d:%02d", m, s);
    return buf;
}

std::wstring FormatClock(double hours, bool use12h, AppLang lang) {
    double h = FixHour(hours);
    int totalMin = static_cast<int>(std::llround(h * 60.0)) % (24 * 60);
    int hh = totalMin / 60;
    int mm = totalMin % 60;
    wchar_t buf[24]{};
    if (!use12h) {
        swprintf_s(buf, L"%02d:%02d", hh, mm);
    } else {
        bool pm = (hh >= 12);
        int h12 = hh % 12;
        if (h12 == 0) h12 = 12;
        const wchar_t* suf = (lang == AppLang::English) ? (pm ? L"PM" : L"AM") : (pm ? L"م" : L"ص");
        swprintf_s(buf, L"%02d:%02d %s", h12, mm, suf);
    }
    return buf;
}

std::wstring ReadRegString(const wchar_t* name, const std::wstring& defVal) {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || size < sizeof(wchar_t))
        return defVal;
    std::wstring out(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_SZ, nullptr, out.data(), &size) != ERROR_SUCCESS)
        return defVal;
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out.empty() ? defVal : out;
}

void WriteRegString(const wchar_t* name, const std::wstring& val) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_SZ, val.c_str(),
                    static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
}

double ReadRegDouble(const wchar_t* name, double defVal) {
    std::wstring s = ReadRegString(name, L"");
    if (s.empty()) return defVal;
    wchar_t* end = nullptr;
    double v = std::wcstod(s.c_str(), &end);
    return (end && end != s.c_str()) ? v : defVal;
}

void WriteRegDouble(const wchar_t* name, double val) {
    wchar_t buf[64]{};
    swprintf_s(buf, L"%.6f", val);
    WriteRegString(name, buf);
}

int ReadRegInt(const wchar_t* name, int defVal) {
    DWORD v = static_cast<DWORD>(defVal), sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return static_cast<int>(v);
    return defVal;
}

void WriteRegInt(const wchar_t* name, int val) {
    DWORD v = static_cast<DWORD>(val);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &v, sizeof(v));
}

const wchar_t* kIqamahRegNames[5] = {
    L"IqamahFajr", L"IqamahDhuhr", L"IqamahAsr", L"IqamahMaghrib", L"IqamahIsha"
};
const wchar_t* kManualTimeRegNames[5] = {
    L"ManualFajr", L"ManualDhuhr", L"ManualAsr", L"ManualMaghrib", L"ManualIsha"
};
const wchar_t* kOffsetRegNames[5] = {
    L"OffsetFajr", L"OffsetDhuhr", L"OffsetAsr", L"OffsetMaghrib", L"OffsetIsha"
};

std::string UrlEncodeUtf8(const std::wstring& ws) {
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), static_cast<int>(ws.size()), utf8.data(), n, nullptr, nullptr);
    std::string out;
    out.reserve(utf8.size() * 3);
    for (unsigned char c : utf8) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            char hex[4]{};
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), ws.data(), n);
    return ws;
}

bool ExtractJsonStringField(const std::string& obj, const char* key, std::string& out) {
    std::string pat = std::string("\"") + key + "\":\"";
    size_t pos = obj.find(pat);
    if (pos == std::string::npos) return false;
    pos += pat.size();
    std::string val;
    while (pos < obj.size()) {
        char c = obj[pos++];
        if (c == '\\' && pos < obj.size()) {
            char esc = obj[pos++];
            if (esc == 'u' && pos + 4 <= obj.size()) {
                unsigned int cp = 0;
                if (sscanf_s(obj.substr(pos, 4).c_str(), "%4x", &cp) == 1) {
                    if (cp < 0x80) val.push_back(static_cast<char>(cp));
                    else if (cp < 0x800) {
                        val.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        val.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        val.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        val.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        val.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                }
                pos += 4;
            } else {
                val.push_back(esc);
            }
        } else if (c == '"') {
            out = std::move(val);
            return true;
        } else {
            val.push_back(c);
        }
    }
    return false;
}

}  // namespace

namespace prayer {

const wchar_t* PrayerName(PrayerId id, AppLang lang) {
    bool en = (lang == AppLang::English);
    switch (id) {
        case PrayerId::Fajr:    return en ? L"Fajr"    : L"الفجر";
        case PrayerId::Dhuhr:   return en ? L"Dhuhr"   : L"الظهر";
        case PrayerId::Asr:     return en ? L"Asr"     : L"العصر";
        case PrayerId::Maghrib: return en ? L"Maghrib" : L"المغرب";
        case PrayerId::Isha:    return en ? L"Isha"    : L"العشاء";
        default:                return L"";
    }
}

const wchar_t* CalcMethodName(CalcMethod m, AppLang lang) {
    bool en = (lang == AppLang::English);
    switch (m) {
        case CalcMethod::UmmAlQura:         return en ? L"Umm al-Qura - Makkah (Saudi Arabia)" : L"أم القرى - مكة المكرمة (السعودية)";
        case CalcMethod::MuslimWorldLeague: return en ? L"Muslim World League (MWL)"           : L"رابطة العالم الإسلامي (MWL)";
        case CalcMethod::Egyptian:          return en ? L"Egyptian General Authority"          : L"الهيئة المصرية العامة للمساحة";
        case CalcMethod::Karachi:           return en ? L"Univ. of Islamic Sciences, Karachi"  : L"جامعة العلوم الإسلامية - كراتشي";
        case CalcMethod::ISNA:              return en ? L"Islamic Society of North America (ISNA)" : L"الجمعية الإسلامية لأمريكا الشمالية (ISNA)";
        case CalcMethod::UOIF:              return en ? L"Union of Islamic Org. of France (UOIF)" : L"اتحاد المنظمات الإسلامية في فرنسا (UOIF)";
        case CalcMethod::Diyanet:           return en ? L"Diyanet (Turkey)"                    : L"رئاسة الشؤون الدينية التركية (Diyanet)";
        case CalcMethod::Jafari:            return en ? L"Shia Ithna-Ashari (Jafari)"          : L"الجعفرية (الشيعية الاثنا عشرية)";
        default:                            return L"";
    }
}

const wchar_t* AsrMethodName(AsrMethod m, AppLang lang) {
    bool en = (lang == AppLang::English);
    switch (m) {
        case AsrMethod::Standard: return en ? L"Standard (Shafi'i, Maliki, Hanbali)" : L"الجمهور (الشافعي، المالكي، الحنبلي)";
        case AsrMethod::Hanafi:   return en ? L"Hanafi"                              : L"المذهب الحنفي";
        default:                  return L"";
    }
}

const std::vector<CityPreset>& Presets() {
    static const std::vector<CityPreset> kPresets = {
        { L"ينبع الصناعية - السعودية", 23.9896, 38.2215 },
        { L"ينبع البحر - السعودية",    24.0895, 38.0618 },
        { L"مكة المكرمة - السعودية",   21.4225, 39.8262 },
        { L"المدينة المنورة - السعودية", 24.4672, 39.6112 },
        { L"جدة - السعودية",           21.4858, 39.1925 },
        { L"الرياض - السعودية",        24.7136, 46.6753 },
        { L"الدمام - السعودية",        26.4207, 50.0888 },
        { L"الخبر - السعودية",         26.2172, 50.1971 },
        { L"الطائف - السعودية",        21.2703, 40.4158 },
        { L"أبها - السعودية",          18.2164, 42.5053 },
        { L"تبوك - السعودية",          28.3835, 36.5662 },
        { L"القصيم / بريدة - السعودية", 26.3260, 43.9750 },
        { L"الأحساء - السعودية",       25.3800, 49.5888 },
        { L"دبي - الإمارات",           25.2048, 55.2708 },
        { L"أبوظبي - الإمارات",        24.4539, 54.3773 },
        { L"الشارقة - الإمارات",       25.3463, 55.4209 },
        { L"الدوحة - قطر",             25.2854, 51.5310 },
        { L"الكويت - الكويت",          29.3759, 47.9774 },
        { L"المنامة - البحرين",        26.2285, 50.5860 },
        { L"مسقط - عمان",              23.5880, 58.3829 },
        { L"صنعاء - اليمن",            15.3694, 44.1910 },
        { L"عدن - اليمن",              12.7855, 45.0187 },
        { L"عمان - الأردن",            31.9454, 35.9284 },
        { L"القدس - فلسطين",           31.7683, 35.2137 },
        { L"غزة - فلسطين",             31.5017, 34.4668 },
        { L"دمشق - سوريا",             33.5138, 36.2765 },
        { L"حلب - سوريا",              36.2021, 37.1343 },
        { L"بيروت - لبنان",            33.8938, 35.5018 },
        { L"بغداد - العراق",           33.3152, 44.3661 },
        { L"البصرة - العراق",          30.5085, 47.7804 },
        { L"أربيل - العراق",           36.1911, 44.0092 },
        { L"القاهرة - مصر",            30.0444, 31.2357 },
        { L"الإسكندرية - مصر",         31.2001, 29.9187 },
        { L"أسوان - مصر",              24.0889, 32.8998 },
        { L"الخرطوم - السودان",        15.5007, 32.5599 },
        { L"طرابلس - ليبيا",           32.8872, 13.1913 },
        { L"تونس - تونس",              36.8065, 10.1815 },
        { L"الجزائر - الجزائر",        36.7538,  3.0588 },
        { L"الرباط - المغرب",          34.0209, -6.8416 },
        { L"الدار البيضاء - المغرب",   33.5731, -7.5898 },
        { L"نواكشوط - موريتانيا",      18.0735, -15.9582 },
        { L"إسطنبول - تركيا",          41.0082, 28.9784 },
        { L"أنقرة - تركيا",            39.9334, 32.8597 },
        { L"كراتشي - باكستان",         24.8607, 67.0011 },
        { L"لاهور - باكستان",          31.5204, 74.3587 },
        { L"إسلام آباد - باكستان",     33.6844, 73.0479 },
        { L"دكا - بنغلاديش",           23.8103, 90.4125 },
        { L"نيودلهي - الهند",          28.6139, 77.2090 },
        { L"كوالالمبور - ماليزيا",      3.1390, 101.6869 },
        { L"جاكرتا - إندونيسيا",       -6.2088, 106.8456 },
        { L"طشقند - أوزبكستان",        41.2995, 69.2401 },
        { L"لندن - بريطانيا",          51.5074, -0.1278 },
        { L"باريس - فرنسا",            48.8566,  2.3522 },
        { L"برلين - ألمانيا",          52.5200, 13.4050 },
        { L"أمستردام - هولندا",        52.3676,  4.9041 },
        { L"مدريد - إسبانيا",          40.4168, -3.7038 },
        { L"نيويورك - أمريكا",         40.7128, -74.0060 },
        { L"تورونتو - كندا",           43.6532, -79.3832 },
        { L"سيدني - أستراليا",        -33.8688, 151.2093 },
    };
    return kPresets;
}

AppLang LoadLanguage() {
    int v = ReadRegInt(L"Language", 0);
    return (v == static_cast<int>(AppLang::English)) ? AppLang::English : AppLang::Arabic;
}

void SaveLanguage(AppLang lang) {
    WriteRegInt(L"Language", static_cast<int>(lang));
}

bool LoadUse12Hour() {
    return ReadRegInt(L"Use12Hour", 1) != 0;
}

void SaveUse12Hour(bool use12h) {
    WriteRegInt(L"Use12Hour", use12h ? 1 : 0);
}

LocationConfig LoadLocation() {
    LocationConfig loc;
    loc.name = ReadRegString(L"LocationName", loc.name);
    loc.latitude = ReadRegDouble(L"Latitude", loc.latitude);
    loc.longitude = ReadRegDouble(L"Longitude", loc.longitude);
    return loc;
}

void SaveLocation(const LocationConfig& loc) {
    WriteRegString(L"LocationName", loc.name);
    WriteRegDouble(L"Latitude", loc.latitude);
    WriteRegDouble(L"Longitude", loc.longitude);
}

MethodConfig LoadMethod() {
    MethodConfig mc;
    int cm = ReadRegInt(L"CalcMethod", static_cast<int>(mc.calcMethod));
    int am = ReadRegInt(L"AsrMethod", static_cast<int>(mc.asrMethod));
    mc.calcMethod = static_cast<CalcMethod>(std::clamp(cm, 0, static_cast<int>(CalcMethod::Count) - 1));
    mc.asrMethod = (am == static_cast<int>(AsrMethod::Hanafi)) ? AsrMethod::Hanafi : AsrMethod::Standard;
    return mc;
}

void SaveMethod(const MethodConfig& mc) {
    WriteRegInt(L"CalcMethod", static_cast<int>(mc.calcMethod));
    WriteRegInt(L"AsrMethod", static_cast<int>(mc.asrMethod));
}

ManualAdhanConfig LoadManualAdhan() {
    ManualAdhanConfig ma;
    ma.useManual = ReadRegInt(L"ManualAdhanEnabled", 0) != 0;
    for (int i = 0; i < 5; i++) {
        ma.manualMinutes[i] = std::clamp(ReadRegInt(kManualTimeRegNames[i], ma.manualMinutes[i]), 0, 1439);
        ma.offsetMinutes[i] = std::clamp(ReadRegInt(kOffsetRegNames[i], 0), -60, 60);
    }
    return ma;
}

void SaveManualAdhan(const ManualAdhanConfig& ma) {
    WriteRegInt(L"ManualAdhanEnabled", ma.useManual ? 1 : 0);
    for (int i = 0; i < 5; i++) {
        WriteRegInt(kManualTimeRegNames[i], std::clamp(ma.manualMinutes[i], 0, 1439));
        WriteRegInt(kOffsetRegNames[i], std::clamp(ma.offsetMinutes[i], -60, 60));
    }
}

IqamahConfig LoadIqamah() {
    IqamahConfig iq;
    for (int i = 0; i < 5; i++) {
        iq.minutes[i] = std::clamp(ReadRegInt(kIqamahRegNames[i], iq.minutes[i]), 0, 120);
    }
    return iq;
}

void SaveIqamah(const IqamahConfig& iq) {
    for (int i = 0; i < 5; i++) {
        WriteRegInt(kIqamahRegNames[i], std::clamp(iq.minutes[i], 0, 120));
    }
}

DayPrayerTimes CalculateDay(int year, int month, int day, double lat, double lon, double tzHours,
                            const MethodConfig& mc, const ManualAdhanConfig& ma) {
    DayPrayerTimes res;
    res.year = year;
    res.month = month;
    res.day = day;

    double jd0 = JulianDay(year, month, day) - lon / 360.0;
    MethodParams mp = GetMethodParams(mc.calcMethod);

    auto solveNoon = [&](double approxHour) {
        double jd = jd0 + approxHour / 24.0;
        SunPos sp = ComputeSunPosition(jd);
        return 12.0 + tzHours - lon / 15.0 - sp.eqTime;
    };
    double dhuhr = solveNoon(12.0);
    dhuhr = solveNoon(dhuhr);

    auto solveAngle = [&](double approxHour, double angleDeg, bool afterNoon, double fallbackVal) {
        for (int pass = 0; pass < 2; pass++) {
            double jd = jd0 + approxHour / 24.0;
            SunPos sp = ComputeSunPosition(jd);
            double noon = 12.0 + tzHours - lon / 15.0 - sp.eqTime;
            double off = 0.0;
            if (!TrySunAngleTime(jd, lat, angleDeg, afterNoon, off)) return fallbackVal;
            approxHour = noon + off;
        }
        return approxHour;
    };

    double sunrise = solveAngle(6.0, 0.833, false, dhuhr - 6.0);
    double sunset = solveAngle(18.0, 0.833, true, dhuhr + 6.0);
    double nightLen = (24.0 - sunset) + sunrise;
    if (nightLen < 0.0) nightLen += 24.0;

    double fajr = solveAngle(5.0, mp.fajrAngle, false, sunrise - nightLen / 7.0);
    double maghrib = (mp.maghribAngle <= 0.84) ? sunset : solveAngle(sunset + 0.25, mp.maghribAngle, true, sunset + 0.25);

    auto solveAsr = [&](double approxHour) {
        double jd = jd0 + approxHour / 24.0;
        SunPos sp = ComputeSunPosition(jd);
        double noon = 12.0 + tzHours - lon / 15.0 - sp.eqTime;
        return noon + AsrOffset(jd, lat, mc.asrMethod);
    };
    double asr = solveAsr(15.0);
    asr = solveAsr(asr);

    double isha = mp.ishaByMinutes
        ? (maghrib + mp.ishaValue / 60.0)
        : solveAngle(sunset + 1.5, mp.ishaValue, true, sunset + nightLen / 7.0);

    res.sunriseHours = sunrise;
    res.adhanHours[static_cast<int>(PrayerId::Fajr)] = fajr;
    res.adhanHours[static_cast<int>(PrayerId::Dhuhr)] = dhuhr;
    res.adhanHours[static_cast<int>(PrayerId::Asr)] = asr;
    res.adhanHours[static_cast<int>(PrayerId::Maghrib)] = maghrib;
    res.adhanHours[static_cast<int>(PrayerId::Isha)] = isha;

    if (ma.useManual) {
        for (int i = 0; i < 5; i++) {
            res.adhanHours[i] = static_cast<double>(ma.manualMinutes[i]) / 60.0;
        }
    } else {
        for (int i = 0; i < 5; i++) {
            res.adhanHours[i] += static_cast<double>(ma.offsetMinutes[i]) / 60.0;
        }
    }
    return res;
}

CountdownState ComputeCountdown(const LocationConfig& loc, const MethodConfig& mc,
                                const ManualAdhanConfig& ma, const IqamahConfig& iq, AppLang lang, bool use12h) {
    bool en = (lang == AppLang::English);
    SYSTEMTIME nowSt{};
    ULONGLONG nowTicks = LocalNowTicks(nowSt);
    double tz = LocalTimezoneHours();

    DayPrayerTimes today = CalculateDay(nowSt.wYear, nowSt.wMonth, nowSt.wDay, loc.latitude, loc.longitude, tz, mc, ma);
    ULONGLONG todayMidnight = LocalMidnightTicks(today.year, today.month, today.day);

    auto toTicks = [](ULONGLONG midnight, double hours) -> ULONGLONG {
        LONGLONG sec = std::llround(hours * 3600.0);
        return static_cast<ULONGLONG>(static_cast<LONGLONG>(midnight) + sec * 10000000LL);
    };

    CountdownState st;
    bool found = false;

    for (int i = 0; i < 5; i++) {
        ULONGLONG adhanTicks = toTicks(todayMidnight, today.adhanHours[i]);
        ULONGLONG iqamahTicks = adhanTicks + static_cast<ULONGLONG>(iq.minutes[i]) * 60ULL * 10000000ULL;

        if (nowTicks < adhanTicks) {
            st.isIqamah = false;
            st.prayer = static_cast<PrayerId>(i);
            st.prayerName = PrayerName(st.prayer, lang);
            st.remainingSeconds = static_cast<int>((adhanTicks - nowTicks + 9999999ULL) / 10000000ULL);
            found = true;
            break;
        }
        if (nowTicks < iqamahTicks) {
            st.isIqamah = true;
            st.prayer = static_cast<PrayerId>(i);
            st.prayerName = PrayerName(st.prayer, lang);
            st.remainingSeconds = static_cast<int>((iqamahTicks - nowTicks + 9999999ULL) / 10000000ULL);
            found = true;
            break;
        }
    }

    if (!found) {
        int ny = nowSt.wYear, nm = nowSt.wMonth, nd = nowSt.wDay;
        AddDays(ny, nm, nd, 1);
        DayPrayerTimes tomorrow = CalculateDay(ny, nm, nd, loc.latitude, loc.longitude, tz, mc, ma);
        ULONGLONG tomorrowMidnight = LocalMidnightTicks(ny, nm, nd);
        ULONGLONG nextFajr = toTicks(tomorrowMidnight, tomorrow.adhanHours[0]);

        st.isIqamah = false;
        st.prayer = PrayerId::Fajr;
        st.prayerName = PrayerName(PrayerId::Fajr, lang);
        st.remainingSeconds = nextFajr > nowTicks
            ? static_cast<int>((nextFajr - nowTicks + 9999999ULL) / 10000000ULL)
            : 0;
    }

    std::wstring dur = FormatDuration(st.remainingSeconds);
    st.labelText = st.isIqamah ? (en ? L"Iqamah" : L"الإقامة") : st.prayerName;
    st.timeText = dur;

    std::wstring locDisp = (en && loc.name == L"ينبع الصناعية") ? L"Yanbu Industrial City" : loc.name;
    std::wstring tip = locDisp;
    if (ma.useManual) tip += en ? L" (Manual)" : L" (يدوي)";
    tip += L"\n";
    if (en) {
        tip += st.isIqamah
            ? (L"Time until " + st.prayerName + L" Iqamah: " + dur + L"\n\n")
            : (L"Time until " + st.prayerName + L" Adhan: " + dur + L"\n\n");
        tip += L"Today's Times:\n";
        tip += L"Fajr: " + FormatClock(today.adhanHours[0], use12h, lang) + L"\n";
        tip += L"Sunrise: " + FormatClock(today.sunriseHours, use12h, lang) + L"\n";
        tip += L"Dhuhr: " + FormatClock(today.adhanHours[1], use12h, lang) + L"\n";
        tip += L"Asr: " + FormatClock(today.adhanHours[2], use12h, lang) + L"\n";
        tip += L"Maghrib: " + FormatClock(today.adhanHours[3], use12h, lang) + L"\n";
        tip += L"Isha: " + FormatClock(today.adhanHours[4], use12h, lang);
    } else {
        tip += st.isIqamah
            ? (L"المتبقي على إقامة " + st.prayerName + L": \u200E" + dur + L"\n\n")
            : (L"المتبقي على أذان " + st.prayerName + L": \u200E" + dur + L"\n\n");
        tip += L"مواقيت اليوم:\n";
        tip += L"الفجر: \u200E" + FormatClock(today.adhanHours[0], use12h, lang) + L"\n";
        tip += L"الشروق: \u200E" + FormatClock(today.sunriseHours, use12h, lang) + L"\n";
        tip += L"الظهر: \u200E" + FormatClock(today.adhanHours[1], use12h, lang) + L"\n";
        tip += L"العصر: \u200E" + FormatClock(today.adhanHours[2], use12h, lang) + L"\n";
        tip += L"المغرب: \u200E" + FormatClock(today.adhanHours[3], use12h, lang) + L"\n";
        tip += L"العشاء: \u200E" + FormatClock(today.adhanHours[4], use12h, lang);
    }
    st.tooltipText = std::move(tip);

    return st;
}

std::vector<LocationConfig> SearchCitiesOnline(const std::wstring& query, AppLang lang) {
    std::vector<LocationConfig> results;
    if (query.empty()) return results;

    HINTERNET hSession = WinHttpOpen(L"AdhanTaskbarMonitor/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return results;

    WinHttpSetTimeouts(hSession, 4000, 4000, 4000, 6000);
    HINTERNET hConnect = WinHttpConnect(hSession, L"nominatim.openstreetmap.org", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        std::string encoded = UrlEncodeUtf8(query);
        std::wstring acceptLang = (lang == AppLang::English) ? L"en,ar" : L"ar,en";
        std::wstring path = L"/search?q=" + Utf8ToWide(encoded) + L"&format=json&limit=10&accept-language=" + acceptLang;
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (hReq) {
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                std::string body;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (WinHttpReadData(hReq, chunk.data(), avail, &read) && read > 0) {
                        chunk.resize(read);
                        body += chunk;
                        if (body.size() > 256 * 1024) break;
                    } else {
                        break;
                    }
                }
                size_t pos = 0;
                while (results.size() < 10) {
                    size_t objStart = body.find('{', pos);
                    if (objStart == std::string::npos) break;
                    size_t objEnd = body.find("},", objStart);
                    if (objEnd == std::string::npos) objEnd = body.find("}]", objStart);
                    if (objEnd == std::string::npos) break;

                    std::string obj = body.substr(objStart, objEnd - objStart + 1);
                    std::string disp, latS, lonS;
                    if (ExtractJsonStringField(obj, "display_name", disp) &&
                        ExtractJsonStringField(obj, "lat", latS) &&
                        ExtractJsonStringField(obj, "lon", lonS)) {
                        LocationConfig lc;
                        lc.name = Utf8ToWide(disp);
                        if (lc.name.size() > 64) {
                            size_t comma = lc.name.find(L',', 20);
                            if (comma != std::string::npos) lc.name.resize(comma);
                        }
                        lc.latitude = std::strtod(latS.c_str(), nullptr);
                        lc.longitude = std::strtod(lonS.c_str(), nullptr);
                        results.push_back(std::move(lc));
                    }
                    pos = objEnd + 1;
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return results;
}

}  // namespace prayer
