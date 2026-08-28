// AnalogClock.cpp
// Author: katahiromz
// License: MIT
//////////////////////////////////////////////////////////////////////////////
// Simple analog clock with C++11 / Win32 / GDI+
// - Hour / minute / second hands
// - Anti-aliased
// - Minute hand steps (no continuous sweep)
// - Second hand steps (no continuous sweep)
// - High-DPI aware (Per-Monitor V2)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <cmath>
#include <ctime>
#include <string>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")

using namespace Gdiplus;

HINSTANCE g_hInst = nullptr;

#ifndef _DPI_AWARENESS_CONTEXTS_
    DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#endif

// -------------------------------------------------------------------------
// High-DPI helpers (Windows 10+)
// -------------------------------------------------------------------------
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);

static PFN_GetDpiForWindow pGetDpiForWindow = nullptr;

static void EnableHighDpi()
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) return;

    auto pSetCtx = reinterpret_cast<PFN_SetProcessDpiAwarenessContext>(
        GetProcAddress(hUser, "SetProcessDpiAwarenessContext"));
    if (pSetCtx) {
        pSetCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else {
        // Fallback for older Windows
        typedef BOOL (WINAPI *PFN_SetProcessDPIAware)();
        auto pAware = reinterpret_cast<PFN_SetProcessDPIAware>(
            GetProcAddress(hUser, "SetProcessDPIAware"));
        if (pAware) pAware();
    }

    pGetDpiForWindow = reinterpret_cast<PFN_GetDpiForWindow>(
        GetProcAddress(hUser, "GetDpiForWindow"));
}

static float GetWindowScale(HWND hwnd)
{
    if (pGetDpiForWindow) {
        UINT dpi = pGetDpiForWindow(hwnd);
        return dpi / 96.0f;
    }
    return 1.0f;
}

// -------------------------------------------------------------------------
// Window position / size
// -------------------------------------------------------------------------

POINT g_pt = { CW_USEDEFAULT, CW_USEDEFAULT };
SIZE g_siz = { CW_USEDEFAULT, CW_USEDEFAULT };
BOOL g_bMaximized = FALSE;
BOOL g_bNoTitleBar = FALSE;
BOOL g_bPlaySound = FALSE;

BOOL LoadSettings(VOID)
{
    g_pt.x = g_pt.y = CW_USEDEFAULT;
    g_siz.cx = g_siz.cy = CW_USEDEFAULT;
    g_bMaximized = FALSE;
    g_bNoTitleBar = FALSE;
    g_bPlaySound = FALSE;

    HKEY hKey;
    LSTATUS error;
    DWORD dwValue, cbValue;

    error = RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Katayama Hirofumi MZ\\HKClock",
                          0, KEY_READ, &hKey);
    if (error)
        return FALSE;

    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"WindowX", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_pt.x = dwValue;
    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"WindowY", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_pt.y = dwValue;
    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"WindowCX", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_siz.cx = dwValue;
    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"WindowCY", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_siz.cy = dwValue;
    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"Maximized", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_bMaximized = !!dwValue;
    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"NoTitleBar", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_bNoTitleBar = !!dwValue;
    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"PlaySound", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_bPlaySound = !!dwValue;

    RegCloseKey(hKey);
    return TRUE;
}

BOOL SaveSettings(VOID)
{
    HKEY hKey;
    LSTATUS error;
    error = RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\Katayama Hirofumi MZ\\HKClock",
                            0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    if (error)
        return FALSE;

    RegSetValueExW(hKey, L"WindowX", 0, REG_DWORD, (PBYTE)&g_pt.x, sizeof(g_pt.x));
    RegSetValueExW(hKey, L"WindowY", 0, REG_DWORD, (PBYTE)&g_pt.y, sizeof(g_pt.y));
    RegSetValueExW(hKey, L"WindowCX", 0, REG_DWORD, (PBYTE)&g_siz.cx, sizeof(g_siz.cx));
    RegSetValueExW(hKey, L"WindowCY", 0, REG_DWORD, (PBYTE)&g_siz.cy, sizeof(g_siz.cy));
    RegSetValueExW(hKey, L"Maximized", 0, REG_DWORD, (PBYTE)&g_bMaximized, sizeof(g_bMaximized));
    RegSetValueExW(hKey, L"NoTitleBar", 0, REG_DWORD, (PBYTE)&g_bNoTitleBar, sizeof(g_bNoTitleBar));
    RegSetValueExW(hKey, L"PlaySound", 0, REG_DWORD, (PBYTE)&g_bPlaySound, sizeof(g_bPlaySound));

    RegCloseKey(hKey);
    return TRUE;
}

// -------------------------------------------------------------------------
// Math helpers
// -------------------------------------------------------------------------
static constexpr float PI = 3.14159265358979323846f;

static float Deg2Rad(float deg) { return deg * PI / 180.0f; }

// Angle from 12-o'clock position, clockwise (standard clock)
static void HandEndpoint(float cx, float cy, float length, float angleDeg,
                         float& outX, float& outY)
{
    float rad = Deg2Rad(angleDeg - 90.0f); // 0 deg = 12 o'clock
    outX = cx + length * std::cos(rad);
    outY = cy + length * std::sin(rad);
}

// -------------------------------------------------------------------------
// Drawing
// -------------------------------------------------------------------------
static void DrawClock(Graphics& g, int clientW, int clientH, float scale)
{
    // Diameter = shorter side
    float diameter = static_cast<float>(std::min(clientW, clientH));
    float radius   = diameter * 0.5f;
    float cx       = clientW * 0.5f;
    float cy       = clientH * 0.5f;

    // Slight inset so the rim doesn't touch the window edge
    float inset    = diameter * 0.02f;
    radius        -= inset;
    diameter       = radius * 2.0f;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    // Background - soft blue vertical stripes (inspired by the reference)
    {
        Color bgTop1(255, 30, 50, 120);
        Color bgMid1(255, 80, 190, 230);
        Color bgBot1(255, 30, 50, 120);
        LinearGradientBrush bgBrush(
            PointF(0, 0), PointF(0, static_cast<float>(clientH)),
            bgTop1, bgBot1);

        Color colors[] = { bgTop1, bgMid1, bgBot1 };
        REAL  positions[] = { 0.0f, 0.5f, 1.0f };
        bgBrush.SetInterpolationColors(colors, positions, 3);
        g.FillRectangle(&bgBrush, 0, 0, clientW, clientH);

        // Subtle vertical stripes
        float stripeStep = 0.06 * radius;
        Color bgTop2(255, 94, 200, 255);
        Color bgBot2(255, 94, 100, 255);
        float lineWidth = 0.02 * radius;
        for (float x = 0; x < clientW; x += stripeStep) {
            LinearGradientBrush bgBrush2(
                PointF(x, 0), PointF(x, static_cast<float>(clientH)),
                bgTop2, bgBot2);
            g.FillRectangle(&bgBrush2, x, 0.0f, lineWidth, (float)clientH);
        }
    }

    float ringW, ringH;
    ringH = std::min((float)clientW * 0.77f, (float)clientH) * 0.85;
    ringW = ringH / 0.77f;

    float faceR = (float)std::min(ringW, ringH) / 2 * 0.9f;

    // Outer light-blue ring (rim)
    {
        Color cTop(255, 106, 125, 135);
        Color cMid(255, 190, 200, 200);
        Color cBot(255, 106, 125, 135);

        SolidBrush shadowBrush(Color(80, 0, 0, 0));
        float shadowWidth = radius * 0.03;
        g.FillEllipse(&shadowBrush,
            cx - ringW/2 - shadowWidth,
            cy - ringH/2 - shadowWidth,
            ringW + 2*shadowWidth, ringH + 2*shadowWidth);

        LinearGradientBrush rimBrush(
            PointF(cx, cy - ringH/2),
            PointF(cx, cy + ringH/2),
            cTop, cBot);

        Color colors[] = { cTop, cMid, cBot };
        REAL  positions[] = { 0.0f, 0.5f, 1.0f };
        rimBrush.SetInterpolationColors(colors, positions, 3);

        g.FillEllipse(&rimBrush, cx - ringW/2, cy - ringH/2, ringW, ringH);

        // Inner black face
        SolidBrush faceBrush(Color(255, 13, 13, 20));
        g.FillEllipse(&faceBrush, cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);
    }

    // Hour markers (dots) and numbers 12, 3, 6, 9
    {
        float fontSize = faceR * 0.20f;

        FontFamily fontFamily1(L"Arial Black");
        FontFamily fontFamily2(L"Segoe UI");
        FontFamily fontFamily3(L"Noto Sans");
        Font font(
            ((fontFamily1.GetLastStatus() == Ok) ? &fontFamily1 :
             (fontFamily2.GetLastStatus() == Ok) ? &fontFamily2 :
                                                   &fontFamily3),
            fontSize, FontStyleBold, UnitPixel);
        SolidBrush textBrush(Color(255, 255, 255, 255));
        SolidBrush dotBrush(Color(255, 255, 255, 255));

        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);

        const wchar_t* labels[] = { L"12", L"3", L"6", L"9" };
        float labelAngles[]     = { 0.0f, 90.0f, 180.0f, 270.0f };
        float labelDist         = faceR * 0.80f;

        for (int i = 0; i < 4; ++i) {
            float lx, ly;
            HandEndpoint(cx, cy, labelDist, labelAngles[i], lx, ly);
            RectF rect(lx - fontSize, ly - fontSize, fontSize * 2.0f, fontSize * 2.0f);
            g.DrawString(labels[i], -1, &font, rect, &sf, &textBrush);
        }

        // Markers: 5-second (hour) marks = dots, second marks = radial line ticks
        Pen tickPen(Color(255, 255, 255, 255), faceR * 0.012f);
        tickPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);

        for (int i = 0; i < 60; ++i) {
            float angle = i * 6.0f;

            if (i % 5 == 0) {
                // 5-second / hour marks: keep as dots
                float dist = faceR * 0.95f;
                float dx, dy;
                HandEndpoint(cx, cy, dist, angle, dx, dy);
                float r = faceR * 0.04f;
                g.FillEllipse(&dotBrush, dx - r, dy - r, r * 2.0f, r * 2.0f);
            } else {
                // Second marks: short radial line segments
                float outer = faceR * 0.98f;
                float inner = faceR * 0.91f;
                float ox, oy, ix, iy;
                HandEndpoint(cx, cy, outer, angle, ox, oy);
                HandEndpoint(cx, cy, inner, angle, ix, iy);
                g.DrawLine(&tickPen, ox, oy, ix, iy);
            }
        }
    }

    // Current local time
    SYSTEMTIME st;
    GetLocalTime(&st);

#if 0
    st.wHour = 6;
    st.wMinute = 51;
    st.wSecond = 55;
#endif

    // Angles (0 = 12 o'clock, clockwise)
    float sec  = static_cast<float>(st.wSecond);
    float min  = static_cast<float>(st.wMinute);
    float hour = static_cast<float>(st.wHour % 12) + min / 60.0f;

    float secAngle  = sec  * 6.0f;
    float minAngle  = min  * 6.0f;
    float hourAngle = hour * 30.0f;

    // ----- Hands -----
    // Helper: unit vector at clock angle (0° = 12 o'clock, clockwise)
    auto Dir = [](float angleDeg, float& dx, float& dy) {
        float rad = Deg2Rad(angleDeg - 90.0f);
        dx = std::cos(rad);
        dy = std::sin(rad);
    };

    // Minute hand - longer, slightly narrower
    {
        float dx, dy;
        Dir(minAngle, dx, dy);
        float txm, tym, txp, typ;
        Dir(minAngle - 1, txm, tym);
        Dir(minAngle + 1, txp, typ);
        float txmm, tymm, txpp, typp;
        Dir(minAngle - 6, txmm, tymm);
        Dir(minAngle + 6, txpp, typp);
        float txmmm, tymmm, txppp, typpp;
        Dir(minAngle - 12, txmmm, tymmm);
        Dir(minAngle + 12, txppp, typpp);
        float txmmmm, tymmmm, txpppp, typppp;
        Dir(minAngle - 5, txmmmm, tymmmm);
        Dir(minAngle + 5, txpppp, typppp);

        Color secColor(255, 255, 255, 255);
        SolidBrush brush(secColor);

        float shaftEnd = faceR * 0.9f;
        float middle = faceR * 0.25f;
        float tail = -faceR * 0.08f;

        // Main shaft
        GraphicsPath path;
        PointF outer[] = {
            PointF(cx + shaftEnd * dx, cy + shaftEnd * dy),
            PointF(cx + middle * txmmm, cy + middle * tymmm),
            PointF(cx + 0.8 * middle * txmm, cy + 0.8 * middle * tymm),
            PointF(cx + tail * txppp, cy + tail * typpp),
            PointF(cx + tail * txmmm, cy + tail * tymmm),
            PointF(cx + 0.8 * middle * txpp, cy + 0.8 * middle * typp),
            PointF(cx + middle * txppp, cy + middle * typpp),
        };
        path.AddPolygon(outer, _countof(outer));
        PointF inner[] = {
            PointF(cx + 0.6 * shaftEnd * dx, cy + 0.6 * shaftEnd * dy),
            PointF(cx + middle * txmmmm, cy + middle * tymmmm),
            PointF(cx + 0.95 * middle * dx, cy + 0.95 * middle * dy),
            PointF(cx + middle * txpppp, cy + middle * typppp),
        };
        path.AddPolygon(inner, _countof(inner));
        path.SetFillMode(FillModeAlternate);

        g.FillPath(&brush, &path);

        // Bosses
        float r1 = faceR * 0.08f;
        float r2 = faceR * 0.06f;
        SolidBrush boss1(Color(255, 255, 255, 255));
        SolidBrush boss2(Color(255, 0, 0, 0));
        g.FillEllipse(&boss1, cx - r1, cy - r1, r1 * 2.0f, r1 * 2.0f);
        g.FillEllipse(&boss2, cx - r2, cy - r2, r2 * 2.0f, r2 * 2.0f);
    }

    // Hour hand - shorter, wider, classic tapered
    {
        float dx, dy;
        Dir(hourAngle, dx, dy);
        float txm, tym, txp, typ;
        Dir(hourAngle - 1, txm, tym);
        Dir(hourAngle + 1, txp, typ);
        float txmm, tymm, txpp, typp;
        Dir(hourAngle - 5, txmm, tymm);
        Dir(hourAngle + 5, txpp, typp);
        float txmmm, tymmm, txppp, typpp;
        Dir(hourAngle - 11, txmmm, tymmm);
        Dir(hourAngle + 11, txppp, typpp);
        float txmmmm, tymmmm, txpppp, typppp;
        Dir(hourAngle - 5, txmmmm, tymmmm);
        Dir(hourAngle + 5, txpppp, typppp);

        Color secColor(255, 255, 255, 255);
        SolidBrush brush(secColor);

        float shaftEnd = faceR * 0.6f;
        float middle = faceR * 0.3f;
        float tail = -faceR * 0.05f;

        // Main shaft
        GraphicsPath path;
        PointF outer[] = {
            PointF(cx + shaftEnd * dx, cy + shaftEnd * dy),
            PointF(cx + middle * txmmm, cy + middle * tymmm),
            PointF(cx + 0.8 * middle * txmm, cy + 0.8 * middle * tymm),
            PointF(cx + tail * txppp, cy + tail * typpp),
            PointF(cx + tail * txmmm, cy + tail * tymmm),
            PointF(cx + 0.8 * middle * txpp, cy + 0.8 * middle * typp),
            PointF(cx + middle * txppp, cy + middle * typpp),
        };
        path.AddPolygon(outer, _countof(outer));
        PointF inner[] = {
            PointF(cx + 0.8 * shaftEnd * dx, cy + 0.8 * shaftEnd * dy),
            PointF(cx + middle * txmmmm, cy + middle * tymmmm),
            PointF(cx + 0.95 * middle * dx, cy + 0.95 * middle * dy),
            PointF(cx + middle * txpppp, cy + middle * typppp),
        };
        path.AddPolygon(inner, _countof(inner));
        path.SetFillMode(FillModeAlternate);

        g.FillPath(&brush, &path);

        // Boss
        float r1 = faceR * 0.05f;
        SolidBrush boss1(Color(255, 255, 255, 255));
        g.FillEllipse(&boss1, cx - r1, cy - r1, r1 * 2.0f, r1 * 2.0f);
    }

    // Second hand
    {
        float txm, tym, txp, typ;
        Dir(secAngle - 0.3, txm, tym);
        Dir(secAngle + 0.3, txp, typ);
        float txmm, tymm, txpp, typp;
        Dir(secAngle - 2.5, txmm, tymm);
        Dir(secAngle + 2.5, txpp, typp);

        Color secColor(255, 255, 255, 255);
        SolidBrush brush(secColor);

        // Main shaft
        float shaftEnd = faceR * 0.9f;
        float tail = -faceR * 0.3f;
        PointF tipPoly[] = {
            PointF(cx + shaftEnd * txm, cy + shaftEnd * tym),
            PointF(cx + shaftEnd * txp, cy + shaftEnd * typ),
            PointF(cx + tail * txmm, cy + tail * tymm),
            PointF(cx + tail * txpp, cy + tail * typp),
        };
        g.FillPolygon(&brush, tipPoly, _countof(tipPoly));

        // Boss
        float r1 = faceR * 0.048f;
        SolidBrush boss1(Color(255, 255, 255, 255));
        g.FillEllipse(&boss1, cx - r1, cy - r1, r1 * 2.0f, r1 * 2.0f);
    }

    // Draw "HK"
    {
        float fontSize = faceR * 0.20f;

        FontFamily fontFamily1(L"Arial Black");
        FontFamily fontFamily2(L"Segoe UI");
        FontFamily fontFamily3(L"Noto Sans");
        Font font(
            ((fontFamily1.GetLastStatus() == Ok) ? &fontFamily1 :
             (fontFamily2.GetLastStatus() == Ok) ? &fontFamily2 :
                                                   &fontFamily3),
            fontSize, FontStyleBoldItalic, UnitPixel);
        SolidBrush textBrush(Color(255, 255, 255, 255));

        StringFormat sf;
        sf.SetAlignment(StringAlignmentFar);
        sf.SetLineAlignment(StringAlignmentFar);

        RectF rect(0, 0, clientW - fontSize, clientH - fontSize);
        g.DrawString(L"H K", -1, &font, rect, &sf, &textBrush);
    }
}

// -------------------------------------------------------------------------
// Second-tick sound
// -------------------------------------------------------------------------
static int g_lastPlayedSecond = -1; // last wSecond we already played a sound for

static void CheckAndPlayTick()
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    // Only fire once per second, right when the second actually changes.
    if (static_cast<int>(st.wSecond) == g_lastPlayedSecond)
        return;
    g_lastPlayedSecond = static_cast<int>(st.wSecond);

    if (!g_bPlaySound)
        return;

    const wchar_t* file = MAKEINTRESOURCEW(1);
    if (st.wSecond == 0) {
        file = MAKEINTRESOURCEW(3);
    } else if (st.wSecond >= 57) {
        file = MAKEINTRESOURCEW(2);
    }

    PlaySoundW(file, g_hInst, SND_RESOURCE | SND_ASYNC | SND_NOSTOP);
}

// -------------------------------------------------------------------------
// Window procedure
// -------------------------------------------------------------------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        if (g_bNoTitleBar)
        {
            DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
            if (g_bNoTitleBar)
                style &= ~WS_CAPTION;
            SetWindowLongW(hwnd, GWL_STYLE, style);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_DRAWFRAME);
        }
        SetTimer(hwnd, 1, 200, nullptr); // ~5 Hz is enough for stepped second hand
        SetTimer(hwnd, 2, 15, nullptr);  // fine-grained polling so ticks fire right on the second
        return 0;

    case WM_TIMER:
        if (wParam == 2) {
            CheckAndPlayTick();
        } else {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        if (HDC hdc = BeginPaint(hwnd, &ps))
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            // Double-buffer with GDI+
            Bitmap buffer(w, h, PixelFormat32bppARGB);
            Graphics g(&buffer);
            float scale = GetWindowScale(hwnd);
            DrawClock(g, w, h, scale);

            Graphics screen(hdc);
            screen.DrawImage(&buffer, 0, 0);

            EndPaint(hwnd, &ps);
        }
        return 0;
    }

    case WM_GETMINMAXINFO:
        {
            auto *info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = 240;
            info->ptMinTrackSize.y = 200;
        }
        break;

    case WM_ERASEBKGND:
        return TRUE; // we paint everything

    case WM_MOVE:
        {
            if (IsZoomed(hwnd) || IsIconic(hwnd))
                return 0;
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_pt.x = rc.left;
            g_pt.y = rc.top;
        }
        return 0;

    case WM_SIZE:
        {
            g_bMaximized = IsZoomed(hwnd);
            if (g_bMaximized || IsIconic(hwnd))
                return 0;
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_siz.cx = rc.right - rc.left;
            g_siz.cy = rc.bottom - rc.top;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;

    case WM_NCHITTEST:
        {
            LRESULT ret = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (ret == HTCLIENT)
                return HTCAPTION;
            return ret;
        }

    case WM_NCRBUTTONDOWN:
        {
            BOOL bZoomed = IsZoomed(hwnd);
            BOOL bIconic = IsIconic(hwnd);
            DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
            HMENU hMenu = CreatePopupMenu();
            BOOL bTitleBar = (style & WS_CAPTION);
            AppendMenuW(hMenu, MF_STRING, 105, L"HK時計 Version 1.0.1");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 102, (bZoomed | bIconic) ? L"元のサイズに戻す(&R)" : L"最大化(&X)");
            if (!bZoomed && !bIconic)
                AppendMenuW(hMenu, MF_STRING, 103, L"最小化(&N)");
            AppendMenuW(hMenu, MF_STRING, 101, bTitleBar ? L"タイトルバーを隠す(&H)" : L"タイトルバーを表示する(&S)");
            AppendMenuW(hMenu, MF_STRING, 104, L"音声を再生");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, 100, L"閉じる(&C)\tAlt+F4");

            MENUITEMINFOW info = { sizeof(info), MIIM_STATE };
            info.fState = MFS_GRAYED;
            SetMenuItemInfoW(hMenu, 105, FALSE, &info);

            if (g_bPlaySound)
                CheckMenuItem(hMenu, 104, MF_CHECKED);

            POINT pt;
            GetCursorPos(&pt);

            SetForegroundWindow(hwnd);
            INT nCmd = (INT)TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_RETURNCMD,
                                           pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);

            switch (nCmd)
            {
            case 100:
                DestroyWindow(hwnd);
                break;
            case 101:
                {
                    bTitleBar = !bTitleBar;
                    if (bTitleBar)
                        style |= WS_CAPTION;
                    else
                        style &= ~WS_CAPTION;
                    SetWindowLongW(hwnd, GWL_STYLE, style);
                    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_DRAWFRAME);
                    if (bZoomed)
                        ShowWindow(hwnd, SW_MAXIMIZE);
                    g_bNoTitleBar = !bTitleBar;
                }
                break;
            case 102:
                if (bZoomed || bIconic)
                    ShowWindow(hwnd, SW_RESTORE);
                else
                    ShowWindow(hwnd, SW_MAXIMIZE);
                break;
            case 103:
                ShowWindow(hwnd, SW_MINIMIZE);
                break;
            case 104:
                g_bPlaySound = !g_bPlaySound;
                if (!g_bPlaySound)
                    PlaySoundW(nullptr, nullptr, SND_PURGE);
                break;
            case 105:
                break;
            }
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// -------------------------------------------------------------------------
// Entry
// -------------------------------------------------------------------------
INT WINAPI
WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    INT nCmdShow)
{
    g_hInst = hInstance;
    EnableHighDpi();
    InitCommonControls();

    GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    if (GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr) != Ok) {
        MessageBoxW(nullptr, L"GDI+ startup failed", L"Error", MB_ICONERROR);
        return 1;
    }

    LoadSettings();

    const wchar_t CLASS_NAME[] = L"katahiromz's AnalogClockWnd";

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1));

    if (!RegisterClassExW(&wc)) {
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    // Start with a reasonable size (will be DPI-scaled by the system)
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int width   = std::min(screenW, screenH) * 2 / 5;
    int height  = width * 3 / 4;

    if (g_pt.x == CW_USEDEFAULT)
        g_pt.x = (screenW - width) / 2;
    if (g_pt.y == CW_USEDEFAULT)
        g_pt.y = (screenH - height) / 2;
    if (g_siz.cx == CW_USEDEFAULT)
        g_siz.cx = width;
    if (g_siz.cy == CW_USEDEFAULT)
        g_siz.cy = height;

    DWORD style = WS_OVERLAPPEDWINDOW;
    BOOL bMaximized = g_bMaximized;
    if (bMaximized)
        style |= WS_MAXIMIZE;
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST,
        CLASS_NAME, L"HK時計", style, g_pt.x, g_pt.y, g_siz.cx, g_siz.cy,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        GdiplusShutdown(gdiplusToken);
        return 1;
    }

    // 完全透明の状態で表示し、初回描画が終わってから不透明化することで
    // 起動時に白い画面が一瞬見えるのを防ぐ
    SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
    ShowWindow(hwnd, bMaximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
    UpdateWindow(hwnd);
    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    SaveSettings();

    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
