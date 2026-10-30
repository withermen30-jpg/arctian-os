/**
 * plat_x11.c — X11 arka ucu (Linux/WSLg, derleyici basligi varsa).
 */
#ifdef EMU_PLAT_X11

#include "emu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

static Display *s_dpy;
static Window   s_win;
static GC       s_gc;
static XImage  *s_img;
static Atom     s_wm_delete;
static int      s_quit;
static int      s_img_w, s_img_h;

static void img_ensure(void) {
    if (s_img && s_img_w == emu_fb_w() && s_img_h == emu_fb_h()) return;
    if (s_img) { XDestroyImage(s_img); s_img = 0; }
    s_img = XCreateImage(s_dpy, DefaultVisual(s_dpy, DefaultScreen(s_dpy)),
                         DefaultDepth(s_dpy, DefaultScreen(s_dpy)), ZPixmap, 0,
                         (char *)emu_fb(), (unsigned)emu_fb_w(),
                         (unsigned)emu_fb_h(), 32, emu_fb_w() * 4);
    s_img_w = emu_fb_w();
    s_img_h = emu_fb_h();
}

static void mods_from(unsigned state) {
    emu_mods((state & ControlMask) ? 1 : 0, (state & ShiftMask) ? 1 : 0);
}

int plat_init(int w, int h, const char *title) {
    s_dpy = XOpenDisplay(0);
    if (!s_dpy) {
        fprintf(stderr, "[emu] X11 acilamadi (DISPLAY yok; --headless dene)\n");
        return -1;
    }
    int scr = DefaultScreen(s_dpy);
    s_win = XCreateSimpleWindow(s_dpy, RootWindow(s_dpy, scr), 0, 0,
                                (unsigned)w, (unsigned)h, 0,
                                BlackPixel(s_dpy, scr),
                                BlackPixel(s_dpy, scr));
    XSelectInput(s_dpy, s_win,
                 ExposureMask | PointerMotionMask | ButtonPressMask |
                 ButtonReleaseMask | KeyPressMask | StructureNotifyMask);
    s_wm_delete = XInternAtom(s_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(s_dpy, s_win, &s_wm_delete, 1);
    XStoreName(s_dpy, s_win, title ? title : "Arctian Emulator");
    s_gc = XCreateGC(s_dpy, s_win, 0, 0);
    XMapWindow(s_dpy, s_win);
    return 0;
}

void plat_shutdown(void) {
    if (s_img) {
        /* s_img->data emu_fb()'yi gosterir; XDestroyImage serbest birakmasin */
        s_img->data = 0;
        XDestroyImage(s_img);
        s_img = 0;
    }
    if (s_dpy) XCloseDisplay(s_dpy);
    s_dpy = 0;
}

const char *plat_name(void) { return "x11"; }

void plat_set_title(const char *t) {
    if (s_dpy && t) XStoreName(s_dpy, s_win, t);
}

void plat_set_size(int w, int h) {
    if (!s_dpy) return;
    XResizeWindow(s_dpy, s_win, (unsigned)w, (unsigned)h);
    XFlush(s_dpy);
}

void plat_present_rect(int x, int y, int w, int h) {
    if (!s_dpy || !emu_fb()) return;
    img_ensure();
    if (!s_img) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_img_w) w = s_img_w - x;
    if (y + h > s_img_h) h = s_img_h - y;
    if (w <= 0 || h <= 0) return;
    XPutImage(s_dpy, s_win, s_gc, s_img, x, y, x, y,
              (unsigned)w, (unsigned)h);
    XFlush(s_dpy);
}

int plat_pump(void) {
    while (XPending(s_dpy)) {
        XEvent e;
        XNextEvent(s_dpy, &e);
        switch (e.type) {
        case Expose:
            if (e.xexpose.count == 0)
                plat_present_rect(0, 0, emu_fb_w(), emu_fb_h());
            break;
        case MotionNotify:
            mods_from(e.xmotion.state);
            emu_set_mouse(e.xmotion.x, e.xmotion.y,
                          (e.xmotion.state & Button1Mask) ? 1 : 0);
            break;
        case ButtonPress:
            if (e.xbutton.button == Button1) {
                mods_from(e.xbutton.state);
                emu_set_mouse(e.xbutton.x, e.xbutton.y, 1);
            } else if (e.xbutton.button == 4) {
                emu_scroll(3);
            } else if (e.xbutton.button == 5) {
                emu_scroll(-3);
            }
            break;
        case ButtonRelease:
            if (e.xbutton.button == Button1) {
                mods_from(e.xbutton.state);
                emu_set_mouse(e.xbutton.x, e.xbutton.y, 0);
            }
            break;
        case KeyPress: {
            char buf[16];
            KeySym ks;
            int n = XLookupString(&e.xkey, buf, sizeof buf, &ks, 0);
            mods_from(e.xkey.state);
            for (int i = 0; i < n; i++) {
                unsigned char c = (unsigned char)buf[i];
                if (ks == XK_Return) { emu_push_key('\n'); break; }
                if (c >= 32 || c == '\b' || c == '\t') emu_push_key(c);
            }
            break;
        }
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == s_wm_delete) {
                s_quit = 1;
                emu_request_quit();
            }
            break;
        case ConfigureNotify:
            break;
        default:
            break;
        }
    }
    return s_quit;
}

void plat_wait_ms(int ms) {
    if (emu_det_active()) { emu_det_advance((uint32_t)ms); return; }
    if (!s_dpy) { usleep((useconds_t)ms * 1000u); return; }
    fd_set fds;
    FD_ZERO(&fds);
    int fd = ConnectionNumber(s_dpy);
    FD_SET(fd, &fds);
    struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
    select(fd + 1, &fds, 0, 0, &tv);
}

uint32_t plat_ticks_ms(void) {
    if (emu_det_active()) return emu_det_now();
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (uint32_t)((uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u);
}

#endif /* EMU_PLAT_X11 */
