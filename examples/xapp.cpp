/*
X11+Xft expr calc |
 q/Esc quit | Enter eval | c copy | f seps | u use | Home/End/←→ | Shift+move select | Del clear

g++ -O3 -flto -march=native -mtune=native -ffast-math -fno-exceptions -fno-rtti \
  -DNDEBUG xapp.cpp $(pkg-config --cflags --libs xft) -lX11 -o mothcalc -s

clang++ -O3 -flto -march=native -mtune=native -ffast-math -fno-exceptions -fno-rtti \
  -DNDEBUG xapp.cpp $(pkg-config --cflags --libs xft) -lX11 -o mothcalc -s
  
strip mothcalc

*/  
#include "../MothMath.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <algorithm>
#include <time.h>

/* ── colors ───────────────────────────────────────────────────── */
static constexpr unsigned long COL_BG     = 0x000000;
static constexpr unsigned long COL_INPUT  = 0xFFFFFF;
static constexpr unsigned long COL_OUTPUT = 0xCCCCCC;
static constexpr unsigned long COL_HINT   = 0x444444;
static constexpr unsigned long COL_SEL    = 0xA1A1A1;

struct Flash {
    unsigned long copy  = 0x00FF88;
    unsigned long error = 0xFF4444;
} flash;

/* ── state ────────────────────────────────────────────────────── */
static Display *dpy;
static Window win;
static Visual *vis;
static Colormap cmap;
static int scr;
static GC gc;
static XftDraw *draw_xft;
static XftFont *font;
static XftFont *font_hint;
static XftColor xc_in, xc_out, xc_hint, xc_flash;
static Atom XA_CLIPBOARD, XA_UTF8, XA_TARGETS, XA_TEXT;
static std::string input, result, clipbuf;
static size_t cursor;
static ssize_t sel_anchor = -1;   // -1 = no selection
static bool seps = true;
static int W = 560, H = 160;
static double font_px;
static unsigned long flash_col;
static timespec flash_end;

static void xft_color(XftColor *c, unsigned long rgb) {
    XRenderColor rc;
    rc.red   = ((rgb >> 16) & 0xFF) * 257;
    rc.green = ((rgb >>  8) & 0xFF) * 257;
    rc.blue  = ((rgb      ) & 0xFF) * 257;
    rc.alpha = 0xFFFF;
    XftColorAllocValue(dpy, vis, cmap, &rc, c);
}

static int text_w(const char *s, int n = -1) {
    if (!font) return 0;
    XGlyphInfo ext;
    XftTextExtentsUtf8(dpy, font, (FcChar8 *)s, n < 0 ? (int)strlen(s) : n, &ext);
    return ext.xOff;
}

static bool has_sel() {
    return sel_anchor >= 0 && (size_t)sel_anchor != cursor;
}

static void sel_range(size_t &a, size_t &b) {
    a = (size_t)std::min((ssize_t)cursor, sel_anchor);
    b = (size_t)std::max((ssize_t)cursor, sel_anchor);
}

static void clear_sel() { sel_anchor = -1; }

static void delete_sel() {
    if (!has_sel()) return;
    size_t a, b;
    sel_range(a, b);
    input.erase(a, b - a);
    cursor = a;
    clear_sel();
}

static void load_font() {
    // scale from height; also clamp by width so short windows still fit
    double target = H / 5.5;
    double by_w = W / 18.0;          // rough: ~18px per char budget
    if (by_w < target) target = by_w;
    if (target < 8.0)  target = 8.0;
    if (target > 72.0) target = 72.0;

    // reload if changed by >5% either direction
    if (font && font_px > 0) {
        double r = target / font_px;
        if (r > 0.95 && r < 1.05) return;
    }

    if (font) { XftFontClose(dpy, font); font = nullptr; }
    font_px = target;

    char name[64];
    snprintf(name, sizeof name, "monospace:pixelsize=%.1f:antialias=true", font_px);
    font = XftFontOpenName(dpy, scr, name);
    if (!font) font = XftFontOpenName(dpy, scr, "monospace");

    if (!font_hint) {
        font_hint = XftFontOpenName(dpy, scr, "monospace:pixelsize=12:antialias=true");
        if (!font_hint) font_hint = XftFontOpenName(dpy, scr, "monospace");
    }
}

static void draw() {
    XSetForeground(dpy, gc, COL_BG);
    XFillRectangle(dpy, win, gc, 0, 0, W, H);
    if (!font || !draw_xft) return;

    XftColor *cin  = flash_col ? &xc_flash : &xc_in;
    XftColor *cout = flash_col ? &xc_flash : &xc_out;
    XftColor *chin = flash_col ? &xc_flash : &xc_hint;

    int ascent  = font->ascent;
    int descent = font->descent;
    int lh = ascent + descent;
    int y1 = H / 2 - lh / 2 - 2;
    int y2 = H / 2 + lh / 2 + 6;

    int x1 = (W - text_w(input.c_str())) / 2;
    if (x1 < 4) x1 = 4;

    // selection highlight
    if (has_sel()) {
        size_t a, b;
        sel_range(a, b);
        int sx = x1 + text_w(input.c_str(), (int)a);
        int sw = text_w(input.c_str() + a, (int)(b - a));
        XSetForeground(dpy, gc, COL_SEL);
        XFillRectangle(dpy, win, gc, sx, y1, sw, lh);
    }

    // input text
    XftDrawStringUtf8(draw_xft, cin, font, x1, y1 + ascent,
                      (FcChar8 *)input.c_str(), (int)input.size());

    // caret (hide when selection active spanning away — still show at cursor)
    int cx = x1 + text_w(input.c_str(), (int)cursor);
    XSetForeground(dpy, gc, flash_col ? flash_col : COL_INPUT);
    XDrawLine(dpy, win, gc, cx, y1, cx, y1 + lh);

    // output
    if (!result.empty()) {
        int x2 = (W - text_w(result.c_str())) / 2;
        if (x2 < 4) x2 = 4;
        XftDrawStringUtf8(draw_xft, cout, font, x2, y2 + ascent,
                          (FcChar8 *)result.c_str(), (int)result.size());
    }

    // hint
    const char *hint = seps ? "f seps | c copy | u use | q quit"
                            : "f plain | c copy | u use | q quit";
    if (font_hint) {
        XGlyphInfo hext;
        XftTextExtentsUtf8(dpy, font_hint, (FcChar8 *)hint, (int)strlen(hint), &hext);
        int hx = (W - hext.xOff) / 2;
        if (hx < 4) hx = 4;
        int hy = H - font_hint->descent - 6;
        if (hy < font_hint->ascent + 4) hy = font_hint->ascent + 4;
        XftDrawStringUtf8(draw_xft, chin, font_hint, hx, hy,
                          (FcChar8 *)hint, (int)strlen(hint));
    }
    XFlush(dpy);
}

static void set_flash_color(unsigned long rgb) {
    XftColorFree(dpy, vis, cmap, &xc_flash);
    xft_color(&xc_flash, rgb);
    flash_col = rgb;
}

static void flash_for(unsigned long c, int ms = 160) {
    set_flash_color(c);
    clock_gettime(CLOCK_MONOTONIC, &flash_end);
    flash_end.tv_nsec += (long)ms * 1000000L;
    if (flash_end.tv_nsec >= 1000000000L) {
        flash_end.tv_sec++;
        flash_end.tv_nsec -= 1000000000L;
    }
    draw();
}

static void tick_flash() {
    if (!flash_col) return;
    timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > flash_end.tv_sec ||
        (now.tv_sec == flash_end.tv_sec && now.tv_nsec >= flash_end.tv_nsec)) {
        flash_col = 0;
        draw();
    }
}

static void eval() {
    double v;
    std::string err;
    if (expr::eval(input, v, err)) {
        result = expr::format_number(v, seps);
        flash_col = 0;
    } else {
        result = err.empty() ? "Error" : err;
        flash_for(flash.error);
        return;
    }
    draw();
}

static void copy() {
    if (result.empty()) return;
    clipbuf = result;
    XSetSelectionOwner(dpy, XA_CLIPBOARD, win, CurrentTime);
    flash_for(flash.copy);
}

static void use_result() {
    if (result.empty()) return;
    for (char c : result)
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return;
    input.clear();
    for (char c : result) if (c != ',') input += c;
    cursor = input.size();
    clear_sel();
    result.clear();
    draw();
}

static void handle_selection(XSelectionRequestEvent *req) {
    XSelectionEvent r = {};
    r.type = SelectionNotify;
    r.display = req->display;
    r.requestor = req->requestor;
    r.selection = req->selection;
    r.target = req->target;
    r.time = req->time;
    r.property = None;

    if (req->target == XA_TARGETS) {
        Atom t[] = {XA_UTF8, XA_TEXT, XA_STRING};
        XChangeProperty(dpy, req->requestor, req->property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)t, 3);
        r.property = req->property;
    } else if (req->target == XA_UTF8 || req->target == XA_TEXT || req->target == XA_STRING) {
        XChangeProperty(dpy, req->requestor, req->property, req->target, 8,
                        PropModeReplace, (unsigned char *)clipbuf.data(), (int)clipbuf.size());
        r.property = req->property;
    }
    XSendEvent(dpy, req->requestor, False, 0, (XEvent *)&r);
}

/* move cursor; shift = extend/create selection */
static void move_cursor(size_t neu, bool shift) {
    if (shift) {
        if (sel_anchor < 0) sel_anchor = (ssize_t)cursor;
    } else {
        clear_sel();
    }
    cursor = neu;
    draw();
}

int main() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) return 1;
    scr = DefaultScreen(dpy);
    vis = DefaultVisual(dpy, scr);
    cmap = DefaultColormap(dpy, scr);

    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 80, 80, W, H, 0,
                              BlackPixel(dpy, scr), BlackPixel(dpy, scr));
    XStoreName(dpy, win, "mothcalc");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);

    XGCValues gcv = {};
    gcv.foreground = COL_INPUT;
    gcv.background = COL_BG;
    gc = XCreateGC(dpy, win, GCForeground | GCBackground, &gcv);

    draw_xft = XftDrawCreate(dpy, win, vis, cmap);
    xft_color(&xc_in,   COL_INPUT);
    xft_color(&xc_out,  COL_OUTPUT);
    xft_color(&xc_hint, COL_HINT);
    xft_color(&xc_flash, flash.copy);
    load_font();

    XA_CLIPBOARD = XInternAtom(dpy, "CLIPBOARD", False);
    XA_UTF8      = XInternAtom(dpy, "UTF8_STRING", False);
    XA_TARGETS   = XInternAtom(dpy, "TARGETS", False);
    XA_TEXT      = XInternAtom(dpy, "TEXT", False);

    XEvent ev;
    for (;;) {
        if (!XPending(dpy)) {
            tick_flash();
            timespec ts = {0, 6 * 1000 * 1000};
            nanosleep(&ts, nullptr);
            continue;
        }
        XNextEvent(dpy, &ev);
        switch (ev.type) {
        case Expose:
            if (!ev.xexpose.count) draw();
            break;
        case ConfigureNotify:
            if (ev.xconfigure.width != W || ev.xconfigure.height != H) {
                W = ev.xconfigure.width;
                H = ev.xconfigure.height;
                load_font();
                draw();
            }
            break;
        case KeyPress: {
            KeySym ks;
            char ch[8] = {};
            int n = XLookupString(&ev.xkey, ch, sizeof ch, &ks, nullptr);
            bool shift = ev.xkey.state & ShiftMask;

            if (ks == XK_Escape || ks == XK_q || ks == XK_Q) goto done;
            if (ks == XK_Return || ks == XK_KP_Enter) { eval(); break; }
            if (ks == XK_c || ks == XK_C) { copy(); break; }
            if (ks == XK_u || ks == XK_U) { use_result(); break; }
            if (ks == XK_f || ks == XK_F) {
                seps = !seps;
                if (!result.empty()) {
                    bool ok = true;
                    for (char c : result)
                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { ok = false; break; }
                    if (ok) eval();
                    else draw();
                } else draw();
                break;
            }
            if (ks == XK_Home) {
                move_cursor(0, shift);
                break;
            }
            if (ks == XK_End) {
                move_cursor(input.size(), shift);
                break;
            }
            if (ks == XK_Left) {
                size_t ncur = cursor ? cursor - 1 : 0;
                move_cursor(ncur, shift);
                break;
            }
            if (ks == XK_Right) {
                size_t ncur = cursor < input.size() ? cursor + 1 : cursor;
                move_cursor(ncur, shift);
                break;
            }
            if (ks == XK_BackSpace) {
                if (has_sel()) {
                    delete_sel();
                } else if (cursor) {
                    input.erase(--cursor, 1);
                }
                draw();
                break;
            }
            if (ks == XK_Delete) {
                // nuke: clear input + output + selection
                input.clear();
                result.clear();
                cursor = 0;
                clear_sel();
                draw();
                break;
            }
            if (n == 1 && ch[0] >= 32 && ch[0] < 127) {
                if (has_sel()) delete_sel();
                input.insert(cursor++, 1, ch[0]);
                clear_sel();
                draw();
            }
            break;
        }
        case SelectionRequest:
            handle_selection(&ev.xselectionrequest);
            break;
        case SelectionClear:
            clipbuf.clear();
            break;
        }
    }
done:
    if (font) XftFontClose(dpy, font);
    if (font_hint) XftFontClose(dpy, font_hint);
    XftColorFree(dpy, vis, cmap, &xc_in);
    XftColorFree(dpy, vis, cmap, &xc_out);
    XftColorFree(dpy, vis, cmap, &xc_hint);
    XftColorFree(dpy, vis, cmap, &xc_flash);
    if (draw_xft) XftDrawDestroy(draw_xft);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
