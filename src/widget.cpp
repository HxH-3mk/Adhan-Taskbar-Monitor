#include "widget.h"

#include <commctrl.h>
#include <uiautomation.h>
#include <algorithm>
#include <memory>
#include <vector>

namespace {

struct BarInstance {
    HWND taskbar = nullptr;
    HWND wnd = nullptr;
    HWND tip = nullptr;
    HWND rebar = nullptr;
    bool isPrimary = false;
    bool hover = false;
    int lastW = -1;
    int lastDpi = 0;
    int lastLeft = -1;
    SIZE size{};
    RECT lastTray{};
    RECT lastBar{};
    RECT lastWidgets{};
    RECT lastBattery{};
};

HINSTANCE g_inst = nullptr;
HWND g_notify = nullptr;
UINT g_clickMsg = 0;
bool g_light = false;
bool g_background = false;
bool g_updateDot = false;
wchar_t g_iconGlyph = 0xE823;
std::wstring g_label;
std::wstring g_timeText;
bool g_accent = false;
int g_redBoxAlpha = 0;
std::wstring g_tipText;
std::vector<std::unique_ptr<BarInstance>> g_bars;

const wchar_t* kClass = L"AdhanTaskbarWidget";

int DpiFor(HWND taskbar) {
    UINT d = taskbar ? GetDpiForWindow(taskbar) : 0;
    return d ? static_cast<int>(d) : 96;
}
int Px(HWND taskbar, int v) { return MulDiv(v, DpiFor(taskbar), 96); }

// Background UI Automation lookup for Windows 11 Widgets button on primary taskbar.
SRWLOCK g_wbLock = SRWLOCK_INIT;
RECT g_wbRect{};
HWND g_wbRectBar = nullptr;
HWND g_wbTaskbar = nullptr;
HANDLE g_wbWake = nullptr;

DWORD WINAPI WidgetsButtonThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    IUIAutomationElement* btn = nullptr;
    HWND btnBar = nullptr;
    for (;;) {
        WaitForSingleObject(g_wbWake, btnBar ? 5000 : INFINITE);
        AcquireSRWLockShared(&g_wbLock);
        HWND bar = g_wbTaskbar;
        ReleaseSRWLockShared(&g_wbLock);
        if (btn && bar != btnBar) { btn->Release(); btn = nullptr; }
        btnBar = bar;
        RECT r{};
        if (bar && IsWindow(bar)) {
            if (!uia)
                CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia);
            if (uia && !btn) {
                IUIAutomationElement* root = nullptr;
                IUIAutomationCondition* cond = nullptr;
                VARIANT id;
                VariantInit(&id);
                id.vt = VT_BSTR;
                id.bstrVal = SysAllocString(L"WidgetsButton");
                if (SUCCEEDED(uia->ElementFromHandle(bar, &root)) && root &&
                    SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &cond)) && cond)
                    root->FindFirst(TreeScope_Descendants, cond, &btn);
                VariantClear(&id);
                if (cond) cond->Release();
                if (root) root->Release();
            }
            if (btn && FAILED(btn->get_CurrentBoundingRectangle(&r))) {
                btn->Release();
                btn = nullptr;
                r = {};
            }
        }
        AcquireSRWLockExclusive(&g_wbLock);
        if (bar && g_wbTaskbar == bar) {
            g_wbRect = r;
            g_wbRectBar = bar;
        }
        ReleaseSRWLockExclusive(&g_wbLock);
    }
}

void WatchWidgetsButton(HWND taskbar) {
    AcquireSRWLockExclusive(&g_wbLock);
    g_wbTaskbar = taskbar;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (!g_wbWake) {
        g_wbWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = g_wbWake ? CreateThread(nullptr, 0, WidgetsButtonThread, nullptr, 0, nullptr) : nullptr;
        if (t) CloseHandle(t);
    }
    if (g_wbWake) SetEvent(g_wbWake);
}

RECT WidgetsButtonRect(HWND taskbar) {
    AcquireSRWLockShared(&g_wbLock);
    RECT r = (g_wbRectBar && g_wbRectBar == taskbar) ? g_wbRect : RECT{};
    ReleaseSRWLockShared(&g_wbLock);
    return r;
}

void ApplyTipTheme(HWND tip) {
    if (!tip) return;
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setTheme = ux ? (SetWindowThemeFn)GetProcAddress(ux, "SetWindowTheme") : nullptr;
    if (setTheme) {
        setTheme(tip, g_light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        SendMessageW(tip, WM_THEMECHANGED, 0, 0);
    }
}

HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback, int fallbackPx = 0) {
    for (const wchar_t* f : { face, fallback }) {
        int h_px = (f == fallback && fallbackPx > 0) ? fallbackPx : px;
        HFONT h = CreateFontW(-h_px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, f);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, h);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        DeleteDC(dc);
        if (!_wcsicmp(got, f) || f == fallback) return h;
        DeleteObject(h);
    }
    return nullptr;
}

struct Mask {
    int w = 0, h = 0;
    std::vector<BYTE> a;
};

Mask RenderMask(const std::wstring& s, HFONT font, bool darkOnLight) {
    Mask m;
    if (s.empty()) return m;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    int W = sz.cx + 8, H = sz.cy + 8;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, darkOnLight ? 0xFF : 0x00, static_cast<size_t>(W) * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, darkOnLight ? RGB(0, 0, 0) : RGB(255, 255, 255));
    TextOutW(dc, 4, 4, s.c_str(), static_cast<int>(s.size()));
    GdiFlush();
    auto* p = static_cast<DWORD*>(bits);
    auto cover = [&](DWORD v) -> BYTE {
        int c = static_cast<int>((v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF)) / 3;
        return static_cast<BYTE>(darkOnLight ? 255 - c : c);
    };
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (cover(p[y * W + x])) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 >= 0) {
        m.w = x1 - x0 + 1;
        m.h = y1 - y0 + 1;
        m.a.resize(static_cast<size_t>(m.w) * m.h);
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++) m.a[y * m.w + x] = cover(p[(y0 + y) * W + x0 + x]);
    }
    SelectObject(dc, ob);
    SelectObject(dc, of);
    DeleteObject(bmp);
    DeleteDC(dc);
    return m;
}

struct Rgb { int r, g, b; };

struct Canvas {
    int w, h;
    std::vector<DWORD> px;
    Canvas(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_, 0) {}
    void Blend(int x, int y, Rgb c, int a) {
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        DWORD d = px[y * w + x];
        int inv = 255 - a;
        int da = d >> 24, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int oa = a + da * inv / 255;
        int orr = c.r * a / 255 + dr * inv / 255;
        int og = c.g * a / 255 + dg * inv / 255;
        int ob = c.b * a / 255 + db * inv / 255;
        px[y * w + x] = (static_cast<DWORD>(oa) << 24) | (static_cast<DWORD>(orr) << 16) |
                        (static_cast<DWORD>(og) << 8) | static_cast<DWORD>(ob);
    }
    void Draw(const Mask& m, int x, int y, Rgb c, int alpha) {
        for (int j = 0; j < m.h; j++)
            for (int i = 0; i < m.w; i++) Blend(x + i, y + j, c, m.a[j * m.w + i] * alpha / 255);
    }
};

struct Piece { Mask m; Rgb color; int alpha; int gapAfter; };

std::vector<Piece> Layout(HWND taskbar, int& width) {
    auto px = [&](int v) { return Px(taskbar, v); };
    Rgb fg = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    Rgb accent = g_light ? Rgb{ 0, 120, 212 } : Rgb{ 96, 205, 255 };
    HFONT icons = MakeFont(px(16), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT text = MakeFont(px(13), FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI", px(12));

    std::vector<Piece> out;
    Rgb tc = g_accent ? accent : fg;
    bool hasText = !g_label.empty() || !g_timeText.empty();
    out.push_back({ RenderMask(std::wstring(1, g_iconGlyph), icons, g_light), tc, 255, hasText ? px(6) : 0 });
    if (!g_label.empty()) {
        out.push_back({ RenderMask(g_label, text, g_light), tc, 255, g_timeText.empty() ? 0 : px(5) });
    }
    if (!g_timeText.empty()) {
        out.push_back({ RenderMask(g_timeText, text, g_light), tc, 255, 0 });
    }
    DeleteObject(icons);
    DeleteObject(text);

    width = px(8) * 2;
    for (auto& p : out) width += p.m.w + p.gapAfter;
    return out;
}

void PaintBar(BarInstance& b) {
    if (!b.wnd || !b.taskbar) return;
    RECT bar{};
    GetClientRect(b.taskbar, &bar);
    int H = bar.bottom - bar.top;
    int W = 0;
    auto pieces = Layout(b.taskbar, W);
    if (W <= 0 || H <= 0) return;
    Canvas cv(W, H);
    auto px = [&](int v) { return Px(b.taskbar, v); };

    for (auto& p : cv.px) p = 0x01000000;
    auto roundRect = [&](int inset, int r, Rgb c, int a) {
        for (int y = inset; y < H - inset; y++)
            for (int x = 0; x < W; x++) {
                int dx = x < r ? r - x : (x >= W - r ? x - (W - r - 1) : 0);
                int dy = y < inset + r ? inset + r - y : (y >= H - inset - r ? y - (H - inset - r - 1) : 0);
                if (dx * dx + dy * dy <= r * r) cv.Blend(x, y, c, a);
            }
    };
    if (g_background) roundRect(px(4), px(6), g_light ? Rgb{ 243, 243, 243 } : Rgb{ 32, 32, 32 }, 240);
    if (g_redBoxAlpha > 0) roundRect(px(4), px(6), Rgb{ 220, 38, 38 }, g_redBoxAlpha);
    if (b.hover) roundRect(px(4), px(4), g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, g_light ? 20 : 26);

    int x = px(8);
    for (auto& p : pieces) {
        cv.Draw(p.m, x, (H - p.m.h) / 2, p.color, p.alpha);
        x += p.m.w + p.gapAfter;
    }

    if (g_updateDot) {
        int r = std::max(2, px(3));
        int cx = W - px(5);
        int cy = px(7);
        Rgb dotCol{ 34, 197, 94 };
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dx * dx + dy * dy <= r * r) cv.Blend(cx + dx, cy + dy, dotCol, 240);
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    memcpy(bits, cv.px.data(), static_cast<size_t>(W) * H * 4);
    HGDIOBJ ob = SelectObject(mem, bmp);
    b.size = { W, H };
    SIZE sz{ W, H };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(b.wnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    b.lastW = -1;
}

bool RepositionBar(BarInstance& b) {
    if (!b.wnd || !IsWindow(b.wnd) || !b.taskbar || !IsWindow(b.taskbar)) return false;
    RECT bar{};
    GetWindowRect(b.taskbar, &bar);
    if (bar.bottom - bar.top > bar.right - bar.left) return false;

    int dpi = DpiFor(b.taskbar);
    if (dpi != b.lastDpi) {
        b.lastDpi = dpi;
        PaintBar(b);
    }

    auto px = [&](int v) { return Px(b.taskbar, v); };
    bool rtl = (GetWindowLongW(b.taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) != 0;

    RECT tr{};
    int edge = rtl ? bar.left : bar.right;

    if (b.isPrimary) {
        HWND tray = FindWindowExW(b.taskbar, nullptr, L"TrayNotifyWnd", nullptr);
        if (!tray) return true;
        GetWindowRect(tray, &tr);
        edge = rtl ? tr.right : tr.left;
    } else {
        // Secondary taskbar: check for clock/tray child windows, or reserve space for Win11 XAML clock.
        bool foundClock = false;
        for (const wchar_t* cls : { L"TrayNotifyWnd", L"ClockButton", L"TrayClockWClass" }) {
            HWND clk = FindWindowExW(b.taskbar, nullptr, cls, nullptr);
            if (clk && IsWindowVisible(clk)) {
                GetWindowRect(clk, &tr);
                if (tr.right > tr.left) {
                    edge = rtl ? tr.right : tr.left;
                    foundClock = true;
                    break;
                }
            }
        }
        if (!foundClock) {
            bool isWin11 = FindWindowExW(b.taskbar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr) != nullptr;
            int reserve = isWin11 ? px(80) : px(8);
            edge = rtl ? (bar.left + reserve) : (bar.right - reserve);
        }
    }

    RECT wr{};
    HWND wbSibling = FindWindowExW(b.taskbar, nullptr, L"WirelessBatteryWidget", nullptr);
    if (wbSibling && IsWindowVisible(wbSibling)) {
        GetWindowRect(wbSibling, &wr);
        if (wr.right > wr.left && wr.top < bar.bottom && wr.bottom > bar.top) {
            if (rtl) { if (wr.right > edge) edge = wr.right; }
            else     { if (wr.left < edge) edge = wr.left; }
        } else {
            wr = {};
        }
    }

    if (b.isPrimary && !rtl) {
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(b.taskbar, nullptr, cls, nullptr);
            if (!feeds || !IsWindowVisible(feeds)) continue;
            RECT fr{};
            GetWindowRect(feeds, &fr);
            if (fr.left < edge && fr.right > fr.left) edge = fr.left;
        }
    }

    RECT wb = b.isPrimary ? WidgetsButtonRect(b.taskbar) : RECT{};
    if (wb.right > wb.left && wb.top < bar.bottom && wb.bottom > bar.top &&
        (rtl ? wb.left < edge + b.size.cx + px(2) && wb.right > edge : wb.right > edge - b.size.cx - px(2) && wb.left < edge)) {
        edge = rtl ? wb.right : wb.left;
    }

    POINT p{ edge, bar.top };
    ScreenToClient(b.taskbar, &p);
    int left = p.x - b.size.cx - px(4);

    if (b.isPrimary) {
        DWORD customOffset = 0, sz = sizeof(customOffset);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\AdhanTaskbarMonitor", L"TaskbarOffset",
                         RRF_RT_REG_DWORD, nullptr, &customOffset, &sz) == ERROR_SUCCESS) {
            left -= px(static_cast<int>(customOffset));
        }
    }

    if (b.rebar && IsWindow(b.rebar)) {
        RECT rr{};
        GetWindowRect(b.rebar, &rr);
        POINT tl{ rtl ? rr.right : rr.left, rr.top };
        ScreenToClient(b.taskbar, &tl);
        if (tl.x + (rr.right - rr.left) > left && left > tl.x)
            MoveWindow(b.rebar, tl.x, tl.y, left - tl.x, rr.bottom - rr.top, TRUE);
    }

    bool covered = GetWindow(b.wnd, GW_HWNDPREV) != nullptr;
    if (!covered && EqualRect(&tr, &b.lastTray) && EqualRect(&bar, &b.lastBar) && EqualRect(&wb, &b.lastWidgets) &&
        EqualRect(&wr, &b.lastBattery) && b.lastW == b.size.cx && b.lastLeft == left)
        return true;

    b.lastTray = tr;
    b.lastWidgets = wb;
    b.lastBattery = wr;
    b.lastBar = bar;
    b.lastW = b.size.cx;
    b.lastLeft = left;
    SetWindowPos(b.wnd, HWND_TOP, left, 0, b.size.cx, b.size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

LRESULT CALLBACK WidgetProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* b = reinterpret_cast<BarInstance*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_MOUSEMOVE:
            if (b && !b->hover) {
                b->hover = true;
                TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
                TrackMouseEvent(&t);
                PaintBar(*b);
                RepositionBar(*b);
            }
            return 0;
        case WM_MOUSELEAVE:
            if (b) {
                b->hover = false;
                PaintBar(*b);
                RepositionBar(*b);
            }
            return 0;
        case WM_LBUTTONUP:
            if (b && b->tip) SendMessageW(b->tip, TTM_POP, 0, 0);
            PostMessageW(g_notify, g_clickMsg, 0, reinterpret_cast<LPARAM>(h));
            return 0;
        case WM_RBUTTONUP:
            if (b && b->tip) SendMessageW(b->tip, TTM_POP, 0, 0);
            PostMessageW(g_notify, g_clickMsg, 1, reinterpret_cast<LPARAM>(h));
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void DestroyBar(BarInstance& b) {
    if (b.rebar && IsWindow(b.rebar) && b.taskbar && IsWindow(b.taskbar)) {
        RECT r{};
        GetClientRect(b.taskbar, &r);
        PostMessageW(b.taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    }
    b.rebar = nullptr;
    if (b.tip) { DestroyWindow(b.tip); b.tip = nullptr; }
    if (b.wnd) { DestroyWindow(b.wnd); b.wnd = nullptr; }
}

std::unique_ptr<BarInstance> CreateBar(HWND taskbar, bool isPrimary) {
    if (!taskbar || !IsWindow(taskbar) || !IsWindowVisible(taskbar)) return nullptr;
    RECT tb{};
    GetWindowRect(taskbar, &tb);
    if (tb.bottom - tb.top > tb.right - tb.left) return nullptr;
    if (isPrimary && !FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr)) return nullptr;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WidgetProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }

    auto b = std::make_unique<BarInstance>();
    b->taskbar = taskbar;
    b->isPrimary = isPrimary;
    b->rebar = (isPrimary && !FindWindowExW(taskbar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr))
                   ? FindWindowExW(taskbar, nullptr, L"ReBarWindow32", nullptr)
                   : nullptr;

    b->wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS,
                             0, 0, 1, 1, taskbar, nullptr, g_inst, nullptr);
    if (!b->wnd) return nullptr;
    SetWindowLongPtrW(b->wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(b.get()));

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    b->tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                             0, 0, 0, 0, g_notify, nullptr, g_inst, nullptr);
    if (b->tip) {
        ApplyTipTheme(b->tip);
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = g_notify;
        ti.uId = reinterpret_cast<UINT_PTR>(b->wnd);
        ti.lpszText = const_cast<wchar_t*>(g_tipText.empty() ? L"مواقيت الصلاة" : g_tipText.c_str());
        SendMessageW(b->tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        SendMessageW(b->tip, TTM_SETMAXTIPWIDTH, 0, 600);
    }

    if (isPrimary) WatchWidgetsButton(taskbar);
    b->lastDpi = DpiFor(taskbar);
    PaintBar(*b);
    b->lastW = -1;
    RepositionBar(*b);
    ShowWindow(b->wnd, SW_SHOWNOACTIVATE);
    return b;
}

struct EnumTaskbarsCtx {
    std::vector<std::pair<HWND, bool>> list;
};

BOOL CALLBACK EnumSecondaryProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumTaskbarsCtx*>(lp);
    wchar_t cls[64]{};
    if (GetClassNameW(hwnd, cls, 64) && wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0 && IsWindowVisible(hwnd)) {
        ctx->list.push_back({ hwnd, false });
    }
    return TRUE;
}

std::vector<std::pair<HWND, bool>> FindAllTaskbars() {
    EnumTaskbarsCtx ctx;
    HWND primary = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (primary && IsWindowVisible(primary)) {
        ctx.list.push_back({ primary, true });
    }
    EnumWindows(EnumSecondaryProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.list;
}

}  // namespace

namespace widget {

bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme) {
    Destroy();
    g_inst = inst;
    g_notify = notifyWnd;
    g_clickMsg = clickMsg;
    g_light = lightTheme;
    return Reposition();
}

void Destroy() {
    if (g_wbWake) WatchWidgetsButton(nullptr);
    for (auto& b : g_bars) DestroyBar(*b);
    g_bars.clear();
}

bool Alive() {
    if (g_bars.empty()) return false;
    for (const auto& b : g_bars) {
        if (b->wnd && IsWindow(b->wnd) && b->taskbar && IsWindow(b->taskbar)) return true;
    }
    return false;
}

RECT ScreenRect(HWND widgetWnd) {
    RECT r{};
    if (widgetWnd && IsWindow(widgetWnd)) {
        GetWindowRect(widgetWnd, &r);
        return r;
    }
    if (!g_bars.empty() && g_bars[0]->wnd && IsWindow(g_bars[0]->wnd)) {
        GetWindowRect(g_bars[0]->wnd, &r);
    }
    return r;
}

void SetDisplay(wchar_t iconGlyph, const std::wstring& label, const std::wstring& timeText,
                bool accent, int redBoxAlpha, const std::wstring& tooltip) {
    bool visualChanged = (iconGlyph != g_iconGlyph || label != g_label || timeText != g_timeText ||
                          accent != g_accent || redBoxAlpha != g_redBoxAlpha);
    g_iconGlyph = iconGlyph;
    g_label = label;
    g_timeText = timeText;
    g_accent = accent;
    g_redBoxAlpha = redBoxAlpha;

    if (tooltip != g_tipText) {
        g_tipText = tooltip;
        for (auto& b : g_bars) {
            if (!b->tip) continue;
            TTTOOLINFOW ti{ sizeof(ti) };
            ti.hwnd = g_notify;
            ti.uId = reinterpret_cast<UINT_PTR>(b->wnd);
            ti.lpszText = const_cast<wchar_t*>(g_tipText.c_str());
            SendMessageW(b->tip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&ti));
        }
    }
    if (visualChanged) {
        for (auto& b : g_bars) {
            PaintBar(*b);
            RepositionBar(*b);
        }
    }
}

void SetLightTheme(bool light) {
    g_light = light;
    for (auto& b : g_bars) {
        ApplyTipTheme(b->tip);
        PaintBar(*b);
    }
}

void SetUpdateAvailable(bool on) {
    if (g_updateDot != on) {
        g_updateDot = on;
        for (auto& b : g_bars) PaintBar(*b);
    }
}

void SetBackground(bool on) {
    g_background = on;
    for (auto& b : g_bars) PaintBar(*b);
}

bool Reposition() {
    auto found = FindAllTaskbars();

    // Remove bars whose taskbar disappeared or became invalid.
    for (auto it = g_bars.begin(); it != g_bars.end();) {
        bool keep = false;
        for (const auto& f : found) {
            if (f.first == (*it)->taskbar && f.second == (*it)->isPrimary) {
                keep = true;
                break;
            }
        }
        if (!keep || !(*it)->wnd || !IsWindow((*it)->wnd)) {
            DestroyBar(**it);
            it = g_bars.erase(it);
        } else {
            ++it;
        }
    }

    // Add bars for newly connected monitors/taskbars.
    for (const auto& f : found) {
        bool exists = false;
        for (const auto& b : g_bars) {
            if (b->taskbar == f.first) { exists = true; break; }
        }
        if (!exists) {
            auto nb = CreateBar(f.first, f.second);
            if (nb) g_bars.push_back(std::move(nb));
        }
    }

    // Reposition all active bars.
    for (auto it = g_bars.begin(); it != g_bars.end();) {
        if (!RepositionBar(**it)) {
            DestroyBar(**it);
            it = g_bars.erase(it);
        } else {
            ++it;
        }
    }

    return !g_bars.empty();
}

}  // namespace widget
