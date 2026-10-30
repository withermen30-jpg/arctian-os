/**
 * plat_win32.c — Windows GDI arka ucu.
 *
 * Cerceve yok (uygulama kendi pencere icerigini ciziyor); DIB blit ile
 * framebuffer dogrudan pencereye kopyalanir. Fare/klavye/tekerlek
 * olaylari emu_* arayuzuyle LVGL'e aktarilir.
 */
#ifdef EMU_PLAT_WIN32

#include "emu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static HWND    s_hwnd;
static HWND    s_wnd_main;
static int     s_quit;
static char    s_wclass[] = "ArctianEmu";

static int client_w(void) { RECT r; GetClientRect(s_hwnd, &r); return r.right - r.left; }
static int client_h(void) { RECT r; GetClientRect(s_hwnd, &r); return r.bottom - r.top; }

static void blit(HDC dc) {
    int w = emu_fb_w(), h = emu_fb_h();
    if (!emu_fb() || w <= 0 || h <= 0) return;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;               /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc, 0, 0, client_w(), client_h(),
                  0, 0, w, h, emu_fb(), &bi, DIB_RGB_COLORS, SRCCOPY);
}

static void mods_from_keys(void) {
    emu_mods((GetKeyState(VK_CONTROL) & 0x8000) ? 1 : 0,
             (GetKeyState(VK_SHIFT) & 0x8000) ? 1 : 0);
}

static LRESULT CALLBACK wndproc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        blit(dc);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE:
        mods_from_keys();
        emu_set_mouse(LOWORD(lp), HIWORD(lp),
                      (wp & MK_LBUTTON) ? 1 : 0);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hw);
        mods_from_keys();
        emu_set_mouse(LOWORD(lp), HIWORD(lp), 1);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        mods_from_keys();
        emu_set_mouse(LOWORD(lp), HIWORD(lp), 0);
        return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        POINT p = {LOWORD(lp), HIWORD(lp)};
        ScreenToClient(hw, &p);
        emu_set_mouse(p.x, p.y, (wp & MK_LBUTTON) ? 1 : 0);
        emu_scroll(d * 3);
        return 0;
    }
    case WM_CHAR: {
        wchar_t c = (wchar_t)wp;
        if (c == 13) c = '\n';
        if (c >= 32 || c == '\n' || c == '\b' || c == '\t')
            emu_push_key((int)c);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hw, 0, FALSE);
        return 0;
    case WM_CLOSE:
        s_quit = 1;
        emu_request_quit();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

int plat_init(int w, int h, const char *title) {
    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(0);
    wc.hCursor = LoadCursorA(0, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = 0;
    wc.lpszClassName = s_wclass;
    if (!RegisterClassA(&wc)) {
        fprintf(stderr, "[emu] win32 sinif kaydi basarisiz\n");
        return -1;
    }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc, style, FALSE);
    s_hwnd = CreateWindowExA(0, s_wclass, title ? title : "Arctian Emulator",
                             style, CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top,
                             0, 0, wc.hInstance, 0);
    if (!s_hwnd) {
        fprintf(stderr, "[emu] pencere olusturulamadi (err=%lu)\n", GetLastError());
        return -1;
    }
    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);
    (void)s_wnd_main;
    return 0;
}

void plat_shutdown(void) {
    if (s_hwnd) DestroyWindow(s_hwnd);
    s_hwnd = 0;
}

const char *plat_name(void) { return "win32"; }

void plat_set_title(const char *t) {
    if (s_hwnd && t) SetWindowTextA(s_hwnd, t);
}

void plat_set_size(int w, int h) {
    if (!s_hwnd) return;
    DWORD style = (DWORD)GetWindowLongA(s_hwnd, GWL_STYLE);
    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc, style, FALSE);
    MoveWindow(s_hwnd, 0, 0, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    (void)style;
}

void plat_present_rect(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    if (s_hwnd) InvalidateRect(s_hwnd, 0, FALSE);
}

int plat_pump(void) {
    MSG m;
    while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return s_quit;
}

void plat_wait_ms(int ms) { if (emu_det_active()) { emu_det_advance((uint32_t)ms); return; } Sleep((DWORD)(ms > 0 ? ms : 1)); }

uint32_t plat_ticks_ms(void) { if (emu_det_active()) return emu_det_now(); return (uint32_t)GetTickCount64(); }

#endif /* EMU_PLAT_WIN32 */
