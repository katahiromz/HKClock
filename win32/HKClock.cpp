// AnalogClock.cpp
// Author: katahiromz
// License: MIT
//////////////////////////////////////////////////////////////////////////////
// Simple analog clock with C++11 / Win32 / GDI+
// - Hour / minute / second hands
// - Anti-aliased
// - High-DPI aware (Per-Monitor V2)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <cmath>
#include <ctime>
#include <string>
#include <atomic>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ws2_32.lib")

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
BOOL g_bTopMost = TRUE;

BOOL LoadSettings(VOID)
{
    g_pt.x = g_pt.y = CW_USEDEFAULT;
    g_siz.cx = g_siz.cy = CW_USEDEFAULT;
    g_bMaximized = FALSE;
    g_bNoTitleBar = FALSE;
    g_bPlaySound = FALSE;
    g_bTopMost = TRUE;

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
    cbValue = sizeof(dwValue);
    error = RegQueryValueExW(hKey, L"TopMost", nullptr, nullptr, (PBYTE)&dwValue, &cbValue);
    if (!error)
        g_bTopMost = !!dwValue;

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
    RegSetValueExW(hKey, L"TopMost", 0, REG_DWORD, (PBYTE)&g_bTopMost, sizeof(g_bTopMost));

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
// Internet time sync (SNTP, RFC 4330)
// -------------------------------------------------------------------------
// Difference in seconds between the NTP epoch (1900-01-01) and the
// FILETIME epoch (1601-01-01).
static const ULONGLONG NTP_TO_FILETIME_EPOCH_SECONDS = 9435484800ULL;

// Candidate NTP servers, tried in order until one answers.
static const wchar_t* g_ntpServers[] = {
    L"ntp.nict.jp",       // Japan (NICT)
    L"time.windows.com",  // Microsoft
    L"pool.ntp.org",      // Global pool
};

// How far the true (network) time is ahead of this PC's system clock,
// expressed in 100-ns units (FILETIME ticks). Applied on top of
// GetSystemTimeAsFileTime() to obtain a synced time.
static std::atomic<LONGLONG> g_llTimeOffset100ns{ 0 };
static std::atomic<bool>     g_bTimeSynced{ false };

static HANDLE g_hSyncThread = nullptr;
static HANDLE g_hSyncStopEvent = nullptr;

// Convert a big-endian 32-bit NTP seconds/fraction pair (as found at
// 'p') into a FILETIME-domain 100ns tick count (as ULONGLONG).
static ULONGLONG NtpTimestampToFileTimeTicks(const BYTE* p)
{
    ULONG secs, frac;
    memcpy(&secs, p, 4);
    memcpy(&frac, p + 4, 4);
    secs = ntohl(secs);
    frac = ntohl(frac);

    ULONGLONG ticks = (static_cast<ULONGLONG>(secs) + NTP_TO_FILETIME_EPOCH_SECONDS) * 10000000ULL;
    ticks += (static_cast<ULONGLONG>(frac) * 10000000ULL) >> 32;
    return ticks;
}

static ULONGLONG FileTimeToTicks(const FILETIME& ft)
{
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

// Queries a single NTP server. On success, fills 'offset100ns' with
// (server time - local time) in 100ns units and returns true.
static bool QuerySntpServer(const wchar_t* host, LONGLONG& offset100ns)
{
    ADDRINFOW hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    ADDRINFOW* result = nullptr;
    if (GetAddrInfoW(host, L"123", &hints, &result) != 0 || !result)
        return false;

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        FreeAddrInfoW(result);
        return false;
    }

    DWORD timeoutMs = 2500;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));

    BYTE packet[48] = {};
    packet[0] = 0x1B; // LI=0, VN=3, Mode=3 (client)

    FILETIME ftT1;
    GetSystemTimeAsFileTime(&ftT1);
    ULONGLONG t1 = FileTimeToTicks(ftT1);

    bool ok = false;
    if (sendto(sock, (const char*)packet, sizeof(packet), 0,
               result->ai_addr, (int)result->ai_addrlen) == sizeof(packet))
    {
        BYTE reply[48];
        sockaddr_storage from = {};
        int fromLen = sizeof(from);
        int n = recvfrom(sock, (char*)reply, sizeof(reply), 0, (sockaddr*)&from, &fromLen);

        FILETIME ftT4;
        GetSystemTimeAsFileTime(&ftT4);
        ULONGLONG t4 = FileTimeToTicks(ftT4);

        if (n == sizeof(reply)) {
            ULONGLONG t2 = NtpTimestampToFileTimeTicks(reply + 32); // server receive time
            ULONGLONG t3 = NtpTimestampToFileTimeTicks(reply + 40); // server transmit time

            // Standard SNTP clock offset formula:
            //   offset = ((T2 - T1) + (T3 - T4)) / 2
            LONGLONG off = (static_cast<LONGLONG>(t2 - t1) + static_cast<LONGLONG>(t3 - t4)) / 2;
            offset100ns = off;
            ok = true;
        }
    }

    closesocket(sock);
    FreeAddrInfoW(result);
    return ok;
}

// Tries each known server in turn; stores the resulting offset globally.
static bool SyncTimeOnce()
{
    for (const wchar_t* host : g_ntpServers) {
        LONGLONG offset;
        if (QuerySntpServer(host, offset)) {
            g_llTimeOffset100ns.store(offset);
            g_bTimeSynced.store(true);
            return true;
        }
    }
    return false;
}

static DWORD WINAPI SntpSyncThreadProc(LPVOID)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return 0;

    for (;;) {
        bool ok = SyncTimeOnce();

        // Re-sync soon after a failure, otherwise every 30 minutes.
        DWORD waitMs = ok ? (30 * 60 * 1000) : (60 * 1000);
        if (WaitForSingleObject(g_hSyncStopEvent, waitMs) == WAIT_OBJECT_0)
            break;
    }

    WSACleanup();
    return 0;
}

static DWORD WINAPI OneShotSyncThreadProc(LPVOID)
{
    SyncTimeOnce();
    return 0;
}

static void StartTimeSync()
{
    g_hSyncStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_hSyncThread = CreateThread(nullptr, 0, SntpSyncThreadProc, nullptr, 0, nullptr);
}

static void StopTimeSync()
{
    if (g_hSyncStopEvent)
        SetEvent(g_hSyncStopEvent);
    if (g_hSyncThread) {
        WaitForSingleObject(g_hSyncThread, 3000);
        CloseHandle(g_hSyncThread);
        g_hSyncThread = nullptr;
    }
    if (g_hSyncStopEvent) {
        CloseHandle(g_hSyncStopEvent);
        g_hSyncStopEvent = nullptr;
    }
}

// Local time, corrected by the last known internet-time offset.
// Falls back to the plain system clock until the first successful sync.
static void GetSyncedLocalTime(SYSTEMTIME& st)
{
    FILETIME ftUtc;
    GetSystemTimeAsFileTime(&ftUtc);

    ULARGE_INTEGER uli;
    uli.LowPart = ftUtc.dwLowDateTime;
    uli.HighPart = ftUtc.dwHighDateTime;
    uli.QuadPart += static_cast<ULONGLONG>(g_llTimeOffset100ns.load());

    FILETIME ftAdjustedUtc;
    ftAdjustedUtc.dwLowDateTime = uli.LowPart;
    ftAdjustedUtc.dwHighDateTime = uli.HighPart;

    FILETIME ftLocal;
    FileTimeToLocalFileTime(&ftAdjustedUtc, &ftLocal);
    FileTimeToSystemTime(&ftLocal, &st);
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

    // Current local time (corrected by internet time sync, if available)
    SYSTEMTIME st;
    GetSyncedLocalTime(st);

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
    GetSyncedLocalTime(st);

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

void OnNCRButtonDown(HWND hwnd)
{
    BOOL bZoomed = IsZoomed(hwnd);
    BOOL bIconic = IsIconic(hwnd);
    DWORD style = GetWindowLongW(hwnd, GWL_STYLE);
    DWORD exstyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    HMENU hMenu = CreatePopupMenu();
    BOOL bTitleBar = !!(style & WS_CAPTION);
    BOOL bTopMost = !!(exstyle & WS_EX_TOPMOST);
    AppendMenuW(hMenu, MF_STRING, 105, L"HK時計 Version 1.0.1");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 102, (bZoomed | bIconic) ? L"元のサイズに戻す(&R)" : L"最大化(&X)");
    if (!bZoomed && !bIconic)
        AppendMenuW(hMenu, MF_STRING, 103, L"最小化(&N)");
    AppendMenuW(hMenu, MF_STRING, 101, L"タイトルバーを隠す(&H)");
    AppendMenuW(hMenu, MF_STRING, 107, L"最前面に表示する");
    AppendMenuW(hMenu, MF_STRING, 104, L"音声を再生");
    AppendMenuW(hMenu, MF_STRING, 106,
        g_bTimeSynced.load() ? L"インターネット時刻に同期済み(&T)" : L"インターネット時刻に同期(&T)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, 100, L"閉じる(&C)\tAlt+F4");

    MENUITEMINFOW info = { sizeof(info), MIIM_STATE };
    info.fState = MFS_GRAYED;
    SetMenuItemInfoW(hMenu, 105, FALSE, &info);
    if (g_bTimeSynced.load())
        SetMenuItemInfoW(hMenu, 106, FALSE, &info);
    if (!bTitleBar)
        CheckMenuItem(hMenu, 101, MF_CHECKED);
    if (bTopMost)
        CheckMenuItem(hMenu, 107, MF_CHECKED);
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
    case 106:
        // Kick off an immediate one-shot resync on a worker thread
        // so the UI thread never blocks on network I/O.
        CloseHandle(CreateThread(nullptr, 0, OneShotSyncThreadProc, nullptr, 0, nullptr));
        break;
    case 107:
        {
            bTopMost = !bTopMost;
            if (bTopMost)
                exstyle |= WS_EX_TOPMOST;
            else
                exstyle &= ~WS_EX_TOPMOST;
            SetWindowLongW(hwnd, GWL_EXSTYLE, exstyle);
            SetWindowPos(hwnd, bTopMost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE);
            g_bTopMost = bTopMost;
        }
        break;
    }
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
        break;

    case WM_TIMER:
        if (wParam == 2) {
            CheckAndPlayTick();
        } else {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_POWERBROADCAST:
        // The system just woke up from sleep/hibernation. The NTP offset
        // we last computed may now be stale (the background sync thread
        // was asleep too, and the system clock can drift or jump during
        // suspend), so kick off an immediate one-shot resync and force a
        // repaint right away instead of waiting for the next scheduled
        // sync (up to 30 minutes later) or the next 200ms timer tick.
        if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            CloseHandle(CreateThread(nullptr, 0, OneShotSyncThreadProc, nullptr, 0, nullptr));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return TRUE;

    case WM_PAINT:
        {
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
        }
        break;

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
                break;
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_pt.x = rc.left;
            g_pt.y = rc.top;
        }
        break;

    case WM_SIZE:
        {
            g_bMaximized = IsZoomed(hwnd);
            if (g_bMaximized || IsIconic(hwnd))
                break;
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_siz.cx = rc.right - rc.left;
            g_siz.cy = rc.bottom - rc.top;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        break;

    case WM_NCHITTEST:
        {
            LRESULT ret = DefWindowProcW(hwnd, msg, wParam, lParam);
            if (ret == HTCLIENT)
                return HTCAPTION;
            return ret;
        }

    case WM_NCRBUTTONDOWN:
        OnNCRButtonDown(hwnd);
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return 0;
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
    StartTimeSync();

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
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | (g_bTopMost ? WS_EX_TOPMOST : 0),
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
    StopTimeSync();

    GdiplusShutdown(gdiplusToken);
    return static_cast<int>(msg.wParam);
}
