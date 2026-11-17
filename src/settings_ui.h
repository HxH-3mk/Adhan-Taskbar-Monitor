#pragma once
#include <windows.h>
#include "prayer.h"

namespace settings_ui {

bool ShowLocationDialog(HINSTANCE inst, HWND parent, LocationConfig& loc, AppLang lang);
bool ShowMethodDialog(HINSTANCE inst, HWND parent, MethodConfig& mc, AppLang lang);
bool ShowManualAdhanDialog(HINSTANCE inst, HWND parent, const LocationConfig& loc,
                           const MethodConfig& mc, ManualAdhanConfig& ma, AppLang lang);
bool ShowIqamahDialog(HINSTANCE inst, HWND parent, IqamahConfig& iq, AppLang lang);

}  // namespace settings_ui
