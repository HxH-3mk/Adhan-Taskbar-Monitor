# Adhan Taskbar Monitor

A lightweight, native Win32 C++ taskbar widget for Windows 10 & Windows 11 that displays a live countdown to the next Adhan and Iqamah directly on the Windows taskbar.

> [العربية](#العربية)

## Features

- **Native Taskbar Integration**: Renders directly on the Windows taskbar (`Shell_TrayWnd`) with crisp DPI-aware text (`PerMonitorV2`), hover highlight, Windows 11 Widgets button avoidance, Windows 10 `ReBarWindow32` support, and automatic recovery on Explorer restart.
- **Live Adhan & Iqamah Countdown**:
  - Counts down to the next Adhan with Western/English numerals.
  - **5 minutes before Adhan**: Pulses a translucent red rounded box behind the countdown.
  - **Between Adhan & Iqamah**: Switches to the **Iqamah** countdown with a steady translucent red box.
- **Global Islamic Calculation Support**:
  - **8 Calculation Methods**: Umm al-Qura (Makkah), Muslim World League (MWL), Egyptian General Authority, University of Islamic Sciences (Karachi), ISNA, UOIF (France), Diyanet (Turkey), and Shia Ithna-Ashari (Jafari).
  - **Asr Juristic Methods**: Standard (Shafi'i, Maliki, Hanbali) and Hanafi.
  - **High-Latitude Fallback**: Automatic 1/7th-of-night fallback for high-latitude regions.
- **Smart Location Search**:
  - Instant offline search across 55+ built-in Islamic & global cities.
  - Optional online search (via free OpenStreetMap Nominatim, no API key required) to find any city or town worldwide, plus manual coordinate entry.
- **Manual Adhan Times & Per-Prayer Offsets**:
  - Switch to fixed manual `HH:MM` Adhan times (with a one-click copy from today's astronomical times), or apply per-prayer `± minute` offsets.
- **Customizable Iqamah Durations**: Configure Iqamah minutes individually for Fajr, Dhuhr, Asr, Maghrib, and Isha.
- **12-Hour / 24-Hour Format**: Defaults to 12-hour (`AM/PM` / `ص/م`) with a quick right-click toggle to 24-hour.
- **Bilingual UI (Arabic & English)**: Arabic by default, switchable to English at any time from the right-click menu.
- **Dark Mode & System Theme**: Follows Windows Light/Dark theme (`AppsUseLightTheme` / `SystemUsesLightTheme`), defaulting to Dark Mode.
- **Zero Dependencies**: Single standalone static `.exe` (~250 KB), no .NET or external runtime required.

## Usage

1. Download `AdhanMonitor.exe` from [Releases](../../releases) and run it.
2. Hover over the taskbar counter to view today's full prayer schedule.
3. **Right-click** the counter to configure:
   - Edit Location & Search
   - Calculation Method & Madhhab
   - Manual Adhan Times
   - Edit Iqamah Duration
   - Language (`العربية` / `English`)
   - 12-hour / 24-hour format
   - Background behind taskbar text (for transparent taskbar mods)
   - Start with Windows

## Building from Source

Requires MSVC Build Tools (`cl.exe` and `rc.exe` / Windows 10+ SDK).

```bat
build.bat
```

This produces a standalone `AdhanMonitor.exe`.

## Support

If you find this project useful, you can support its development:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-hxh3mk-FFDD00?style=flat&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/hxh3mk)

---

<a id="العربية"></a>
## العربية

أداة خفيفة ومباشرة لشريط مهام ويندوز 10 وويندوز 11 (مكتوبة بلغة C++ Win32 أصلية بدون أي اعتماديات خارجية) تعرض عداداً تنازلياً حياً للوقت المتبقي على الأذان والإقامة مباشرة على شريط المهام.

### المميزات

- **مدمجة بسلاسة في شريط المهام:** تدعم تعدد الشاشات ودقة العرض العالية (`PerMonitorV2`)، وتفادي زر Widgets في ويندوز 11، وتعديل عرض شريط المهام في ويندوز 10، وإعادة الظهور تلقائياً عند إعادة تشغيل Explorer.
- **عداد الأذان والإقامة وتنبيه بصري:**
  - يعرض المتبقي على الأذان بالأرقام الإنجليزية الواضحة.
  - **قبل الأذان بـ 5 دقائق:** يومض (يكبس) مربع أحمر شفاف دائري الحواف خلف العداد.
  - **بين الأذان والإقامة:** يتحول لعداد **الإقامة** مع مربع أحمر شفاف ثابت.
- **دعم شامل لجميع أنحاء العالم الإسلامي والمذاهب:**
  - **8 طرق حساب معتمدة:** أم القرى (مكة المكرمة)، رابطة العالم الإسلامي (MWL)، الهيئة المصرية العامة للمساحة، كراتشي، ISNA، UOIF (فرنسا)، Diyanet (تركيا)، والجعفرية.
  - **مذهب العصر:** الجمهور (الشافعي، المالكي، الحنبلي) أو الحنفي.
  - معالجة تلقائية للمناطق ذات خطوط العرض العالية (أوروبا وكندا وغيرها).
- **بحث ذكي عن الموقع:**
  - بحث فوري بدون إنترنت لأكثر من 55 مدينة إسلامية وعالمية.
  - زر **بحث بالإنترنت** (عبر خدمة OpenStreetMap Nominatim المجانية بدون الحاجة لأي API Key) للبحث عن أي مدينة أو قرية في العالم، مع إمكانية إدخال الإحداثيات يدوياً.
- **إدخال يدوي لأوقات الأذان وتعديلات الدقائق:**
  - إمكانية ضبط أوقات أذان يدوية ثابتة (`HH:MM`) لكل صلاة مع زر لنسخ أوقات اليوم الفلكية، أو إضافة تعديل (`± دقيقة`) لكل صلاة على الحساب الفلكي.
- **تعديل مدة الإقامة:** ضبط دقائق الإقامة لكل صلاة على حدة (الفجر، الظهر، العصر، المغرب، العشاء).
- **تنسيق 12 / 24 ساعة:** الافتراضي 12 ساعة (`ص/م`) مع خيار التبديل السريع إلى 24 ساعة.
- **دعم اللغتين العربية والإنجليزية:** العربية افتراضياً مع إمكانية التبديل للإنجليزية من قائمة زر الفأرة الأيمن.
- **الوضع الداكن:** يتبع ثيم النظام مع اعتماد الوضع الداكن افتراضياً في القوائم والنوافذ.

### طريقة الاستخدام

1. حمّل ملف `AdhanMonitor.exe` من صفحة [الإصدارات (Releases)](../../releases) وشغّله مباشرة.
2. مرّر الماوس فوق العداد لعرض جميع مواقيت اليوم.
3. اضغط **بزر الفأرة الأيمن** على العداد للوصول إلى جميع الإعدادات (الموقع، نظام التوقيت والمذهب، الضبط اليدوي، مدة الإقامة، اللغة، تنسيق 12/24 ساعة، التشغيل مع ويندوز).

### الدعم

إذا أعجبك البرنامج وأفادك، يمكنك دعمه عبر:

[![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-hxh3mk-FFDD00?style=flat&logo=buy-me-a-coffee&logoColor=black)](https://buymeacoffee.com/hxh3mk)
