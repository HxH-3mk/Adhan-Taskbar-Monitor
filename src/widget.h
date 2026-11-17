#pragma once
// A small transparent strip on the taskbar, just left of the notification area, showing
// "[icon] الفجر 01:23:45". Implemented as a layered child window of the taskbar (Shell_TrayWnd).
#include <windows.h>
#include <string>

namespace widget {
// Creates the widget on the primary taskbar. Clicks are reported by posting `clickMsg` to `notifyWnd`
// (wParam == 1 for right-click, 0 for left-click).
bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme);
void Destroy();
bool Alive();
void SetDisplay(wchar_t iconGlyph, const std::wstring& label, const std::wstring& timeText, bool accent, int redBoxAlpha, const std::wstring& tooltip);
void SetLightTheme(bool light);
// Draws a rounded plate in the taskbar's colours behind the text (kept across Create/Destroy).
void SetBackground(bool on);
// Re-checks the taskbar layout and moves the widget if needed; call about once a second.
// Returns false when the taskbar it was attached to is gone.
bool Reposition();
}  // namespace widget
