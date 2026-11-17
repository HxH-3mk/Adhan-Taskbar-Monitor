#pragma once
#include <windows.h>
#include <string>
#include <vector>

enum class PrayerId : int {
    Fajr = 0,
    Dhuhr = 1,
    Asr = 2,
    Maghrib = 3,
    Isha = 4,
    Count = 5
};

enum class CalcMethod : int {
    UmmAlQura = 0,
    MuslimWorldLeague = 1,
    Egyptian = 2,
    Karachi = 3,
    ISNA = 4,
    UOIF = 5,
    Diyanet = 6,
    Jafari = 7,
    Count = 8
};

enum class AsrMethod : int {
    Standard = 0,  // Shafi'i, Maliki, Hanbali (shadow = 1)
    Hanafi = 1     // Hanafi (shadow = 2)
};

enum class AppLang : int {
    Arabic = 0,
    English = 1
};

struct LocationConfig {
    std::wstring name = L"ينبع الصناعية";
    double latitude = 23.9896;
    double longitude = 38.2215;
};

struct MethodConfig {
    CalcMethod calcMethod = CalcMethod::UmmAlQura;
    AsrMethod asrMethod = AsrMethod::Standard;
};

struct ManualAdhanConfig {
    bool useManual = false;
    int manualMinutes[5] = { 323, 732, 919, 1060, 1150 };  // minutes from midnight (HH*60 + MM)
    int offsetMinutes[5] = { 0, 0, 0, 0, 0 };              // +/- minutes applied to astronomical times
};

struct IqamahConfig {
    int minutes[5] = { 25, 20, 20, 10, 20 };  // Fajr, Dhuhr, Asr, Maghrib, Isha
};

struct CityPreset {
    const wchar_t* name;
    double latitude;
    double longitude;
};

struct DayPrayerTimes {
    int year = 0;
    int month = 0;
    int day = 0;
    double adhanHours[5] = {};
    double sunriseHours = 0.0;
};

struct CountdownState {
    bool isIqamah = false;
    PrayerId prayer = PrayerId::Fajr;
    std::wstring prayerName;
    int remainingSeconds = 0;
    std::wstring labelText;
    std::wstring timeText;
    std::wstring tooltipText;
};

struct ScheduleRow {
    std::wstring name;
    std::wstring time;
    bool isNext = false;
};

struct TodaySchedule {
    std::wstring locationDisplay;
    std::wstring statusLine;
    std::vector<ScheduleRow> rows;
};

namespace prayer {

const wchar_t* PrayerName(PrayerId id, AppLang lang = AppLang::Arabic);
const wchar_t* CalcMethodName(CalcMethod m, AppLang lang = AppLang::Arabic);
const wchar_t* AsrMethodName(AsrMethod m, AppLang lang = AppLang::Arabic);
const std::vector<CityPreset>& Presets();

AppLang LoadLanguage();
void SaveLanguage(AppLang lang);

bool LoadUse12Hour();
void SaveUse12Hour(bool use12h);

LocationConfig LoadLocation();
void SaveLocation(const LocationConfig& loc);

MethodConfig LoadMethod();
void SaveMethod(const MethodConfig& mc);

ManualAdhanConfig LoadManualAdhan();
void SaveManualAdhan(const ManualAdhanConfig& ma);

IqamahConfig LoadIqamah();
void SaveIqamah(const IqamahConfig& iq);

DayPrayerTimes CalculateDay(int year, int month, int day, double lat, double lon, double tzHours,
                            const MethodConfig& mc, const ManualAdhanConfig& ma);
CountdownState ComputeCountdown(const LocationConfig& loc, const MethodConfig& mc,
                                const ManualAdhanConfig& ma, const IqamahConfig& iq,
                                AppLang lang = AppLang::Arabic, bool use12h = true);

TodaySchedule GetTodaySchedule(const LocationConfig& loc, const MethodConfig& mc,
                               const ManualAdhanConfig& ma, const IqamahConfig& iq,
                               AppLang lang = AppLang::Arabic, bool use12h = true);

std::vector<LocationConfig> SearchCitiesOnline(const std::wstring& query, AppLang lang = AppLang::Arabic);

}  // namespace prayer
