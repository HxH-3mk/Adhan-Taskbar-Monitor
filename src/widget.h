#pragma once
// Transparent taskbar strip(s) on all connected monitors (Shell_TrayWnd + Shell_SecondaryTrayWnd).
#include <windows.h>
#include <string>

namespace widget {
// Creates widgets on all active horizontal taskbars. Clicks post `clickMsg` to `notifyWnd`
// (wParam == 1 for right-click, 0 for left-click; lParam == clicked widget HWND).
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme);
void Destroy();
bool Alive();
void SetDisplay(wchar_t iconGlyph, const std::wstring& label, const std::wstring& timeText,
                bool accent, int redBoxAlpha, const std::wstring& tooltip);
void SetLightTheme(bool light);
void SetUpdateAvailable(bool on);
void SetBackground(bool on);
// Syncs widgets with connected monitors and repositions them; call about once a second.
bool Reposition();
RECT ScreenRect(HWND widgetWnd = nullptr);
}  // namespace widget
