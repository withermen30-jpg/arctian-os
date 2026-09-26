#include "svg.h"
#include <stddef.h>

extern double sqrt(double);
extern double sin(double);
extern double cos(double);

#define SVG_PI 3.14159265358979
#define SS     2

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int slen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static int seq(const char *a, const char *b) {
    int i = 0; for (; a[i] && b[i]; i++) if (a[i] != b[i]) return 0;
    return a[i] == b[i];
}
static int sncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 1;
    return 0;
}
static int sprefix(const char *s, const char *pre) {
    for (int i = 0; pre[i]; i++) if (s[i] != pre[i]) return 0;
    return 1;
}

static float my_atan2(float y, float x) {
    if (x == 0 && y == 0) return 0;
    float ax = x < 0 ? -x : x, ay = y < 0 ? -y : y, a;
    if (ax >= ay) {
        float z = (ax == 0) ? 0 : ay / ax;
        float z2 = z * z;
        float at = z * (0.9998660f - z2 * (0.3302995f - z2 * (0.1801410f - z2 * 0.0851330f)));
        a = at;
        if (x < 0) a = (float)SVG_PI - a;
    } else {
        float z = (ay == 0) ? 0 : ax / ay;
        float z2 = z * z;
        float at = z * (0.9998660f - z2 * (0.3302995f - z2 * (0.1801410f - z2 * 0.0851330f)));
        a = (float)SVG_PI / 2.0f - at;
    }
    if (y < 0) a = -a;
    return a;
}

static const char *find_attr(const char *tag, const char *name, int *outlen) {
    int nl = slen(name);
    const char *p = tag;
    while (*p) {
        const char *p0 = p;
        while (*p && is_ws(*p)) p++;
        const char *start = p;
        while (*p && *p != '=' && !is_ws(*p) && *p != '>' && *p != '/') p++;
        int tl = (int)(p - start);
        const char *q = p;
        while (*q && is_ws(*q)) q++;
        if (*q == '=') {
            q++;
            while (*q && is_ws(*q)) q++;
            char quote = 0;
            if (*q == '"' || *q == '\'') { quote = *q; q++; }
            const char *vs = q;
            if (quote) { while (*q && *q != quote) q++; }
            else { while (*q && !is_ws(*q) && *q != '>' && *q != '/') q++; }
            int vl = (int)(q - vs);
            if (tl == nl && sncmp(start, name, nl) == 0) { *outlen = vl; return vs; }
            if (quote && *q == quote) q++;
            p = q;
        } else p = q;
        if (p == p0) p++;
    }
    return 0;
}
static int attr_copy(const char *tag, const char *name, char *out, int outsz) {
    out[0] = 0;
    int len = 0;
    const char *v = find_attr(tag, name, &len);
    if (!v || len <= 0) return 0;
    if (len > outsz - 1) len = outsz - 1;
    for (int i = 0; i < len; i++) out[i] = v[i];
    out[len] = 0;
    return 1;
}
static float pnum(const char **pp) {
    const char *p = *pp;
    while (*p && (is_ws(*p) || *p == ',')) p++;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; } else if (*p == '+') p++;
    float v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    if (*p == '.') {
        p++; float f = 0.1f;
        while (*p >= '0' && *p <= '9') { v += (*p - '0') * f; f *= 0.1f; p++; }
    }
    if (*p == 'e' || *p == 'E') {
        p++; int es = 1;
        if (*p == '-') { es = -1; p++; } else if (*p == '+') p++;
        int e = 0; while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); p++; }
        float m = 1; for (int i = 0; i < e; i++) m *= 10.0f;
        v *= (es > 0) ? m : (1.0f / m);
    }
    *pp = p;
    return sign * v;
}
static float attr_num(const char *tag, const char *name, float def) {
    char v[64];
    if (!attr_copy(tag, name, v, sizeof(v))) return def;
    const char *p = v;
    return pnum(&p);
}
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static void parse_color(const char *s, uint32_t *rgb, float *a) {
    *a = 1.0f; *rgb = 0;
    if (!s || !*s || seq(s, "none")) { *a = 0; return; }
    if (s[0] == '#') {
        int r, g, b;
        if (slen(s + 1) >= 6) {
            r = hexval(s[1]) * 16 + hexval(s[2]);
            g = hexval(s[3]) * 16 + hexval(s[4]);
            b = hexval(s[5]) * 16 + hexval(s[6]);
        } else {
            r = hexval(s[1]) * 17; g = hexval(s[2]) * 17; b = hexval(s[3]) * 17;
        }
        if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
        *rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        return;
    }
    if (seq(s, "white")) *rgb = 0xFFFFFF;
    else if (seq(s, "black")) *rgb = 0x000000;
    else if (seq(s, "red")) *rgb = 0xFF0000;
    else if (seq(s, "gray") || seq(s, "grey")) *rgb = 0x808080;
    else *a = 0;
}

#define SVG_MAXSUB 12
#define SVG_MAXPTS 320

typedef struct { float x[SVG_MAXPTS], y[SVG_MAXPTS]; int n, closed; } sp_t;
typedef struct {
    sp_t sp[SVG_MAXSUB]; int nsp;
    uint32_t fill; float fill_a; int fill_on;
    uint32_t stroke; float stroke_a; float sw; int stroke_on;
} shape_t;

static sp_t *new_sub(shape_t *s) {
    sp_t *sp;
    if (s->nsp >= SVG_MAXSUB) return &s->sp[SVG_MAXSUB - 1];
    sp = &s->sp[s->nsp++];
    sp->n = 0; sp->closed = 0;
    return sp;
}
static void add_pt(sp_t *sp, float x, float y) {
    if (sp->n < SVG_MAXPTS) { sp->x[sp->n] = x; sp->y[sp->n] = y; sp->n++; }
}
static void add_arc_pts(sp_t *sp, float cx, float cy, float r, float a0, float a1, int steps) {
    for (int i = 0; i <= steps; i++) {
        float deg = a0 + (a1 - a0) * (float)i / (float)steps;
        float rad = deg * (float)SVG_PI / 180.0f;
        add_pt(sp, cx + r * (float)cos(rad), cy + r * (float)sin(rad));
    }
}
static void cubic(sp_t *sp, float x0, float y0, float x1, float y1,
                  float x2, float y2, float x3, float y3) {
    for (int i = 1; i <= 24; i++) {
        float t = (float)i / 24.0f, u = 1.0f - t;
        add_pt(sp, u*u*u*x0 + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3,
                   u*u*u*y0 + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3);
    }
}
static void quad(sp_t *sp, float x0, float y0, float x1, float y1, float x2, float y2) {
    for (int i = 1; i <= 16; i++) {
        float t = (float)i / 16.0f, u = 1.0f - t;
        add_pt(sp, u*u*x0 + 2*u*t*x1 + t*t*x2, u*u*y0 + 2*u*t*y1 + t*t*y2);
    }
}
static void arc_to(sp_t *sp, float x0, float y0, float rx, float ry, float phi,
                   int fa, int fs, float x1, float y1) {
    if (rx == 0 || ry == 0) { add_pt(sp, x1, y1); return; }
    rx = rx < 0 ? -rx : rx; ry = ry < 0 ? -ry : ry;
    float pr = phi * (float)SVG_PI / 180.0f;
    float cosp = (float)cos(pr), sinp = (float)sin(pr);
    float dx = (x0 - x1) / 2.0f, dy = (y0 - y1) / 2.0f;
    float x1p = cosp * dx + sinp * dy;
    float y1p = -sinp * dx + cosp * dy;
    float lambda = (x1p*x1p)/(rx*rx) + (y1p*y1p)/(ry*ry);
    if (lambda > 1.0f) { float s = (float)sqrt((double)lambda); rx *= s; ry *= s; }
    float num = rx*rx*ry*ry - rx*rx*y1p*y1p - ry*ry*x1p*x1p;
    float den = rx*rx*y1p*y1p + ry*ry*x1p*x1p;
    float co = 0;
    if (den != 0) { float t = num / den; if (t < 0) t = 0; co = (fa != fs ? 1.0f : -1.0f) * (float)sqrt((double)t); }
    float cxp = co * rx * y1p / ry;
    float cyp = -co * ry * x1p / rx;
    float cx = cosp*cxp - sinp*cyp + (x0 + x1)/2.0f;
    float cy = sinp*cxp + cosp*cyp + (y0 + y1)/2.0f;
    float v1x = (x1p - cxp)/rx, v1y = (y1p - cyp)/ry;
    float v2x = (-x1p - cxp)/rx, v2y = (-y1p - cyp)/ry;
    float a1 = my_atan2(v1y, v1x);
    float a2 = my_atan2(v2y, v2x);
    float dth = a2 - a1;
    if (!fs && dth > 0) dth -= 2.0f*(float)SVG_PI;
    else if (fs && dth < 0) dth += 2.0f*(float)SVG_PI;
    for (int i = 1; i <= 24; i++) {
        float ang = a1 + dth * (float)i / 24.0f;
        add_pt(sp, cx + (cosp*rx)*(float)cos(ang) - (sinp*ry)*(float)sin(ang),
                   cy + (sinp*rx)*(float)cos(ang) + (cosp*ry)*(float)sin(ang));
    }
}

static uint32_t *g_out;
static int g_W;

static float dist_seg(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx-ax, dy = by-ay, l2 = dx*dx + dy*dy, t = 0;
    if (l2 > 0) { t = ((px-ax)*dx + (py-ay)*dy)/l2; if (t < 0) t = 0; if (t > 1) t = 1; }
    float qx = ax + t*dx, qy = ay + t*dy, ddx = px-qx, ddy = py-qy;
    return (float)sqrt((double)(ddx*ddx + ddy*ddy));
}
static int evenodd(float X, float Y, shape_t *sh) {
    int cnt = 0;
    for (int s = 0; s < sh->nsp; s++) {
        sp_t *sp = &sh->sp[s];
        int n = sp->n; if (n < 2) continue;
        for (int i = 0, j = n-1; i < n; j = i++) {
            float xi = sp->x[i], yi = sp->y[i], xj = sp->x[j], yj = sp->y[j];
            if (((yi > Y) != (yj > Y)) && (X < (xj-xi)*(Y-yi)/(yj-yi)+xi)) cnt++;
        }
    }
    return cnt & 1;
}
static int stroke_hit(float X, float Y, shape_t *sh, float half) {
    for (int s = 0; s < sh->nsp; s++) {
        sp_t *sp = &sh->sp[s];
        int n = sp->n; if (n < 2) continue;
        int last = sp->closed ? n : n - 1;
        for (int i = 0; i < last; i++) {
            int a = i, b = (i+1) % n;
            if (dist_seg(X, Y, sp->x[a], sp->y[a], sp->x[b], sp->y[b]) <= half) return 1;
        }
    }
    return 0;
}
static void blend(int px, int py, uint32_t rgb, float a) {
    if (a <= 0.001f) return;
    if (a > 1) a = 1;
    uint32_t *slot = &g_out[py * g_W + px];
    uint32_t dst = *slot;
    float da = (float)((dst >> 24) & 0xff) / 255.0f;
    float sr = (float)((rgb >> 16) & 0xff), sg = (float)((rgb >> 8) & 0xff), sb = (float)(rgb & 0xff);
    float dr = (float)((dst >> 16) & 0xff), dg = (float)((dst >> 8) & 0xff), db = (float)(dst & 0xff);
    float oa = a + da * (1 - a);
    if (oa <= 0) { *slot = 0; return; }
    int r = (int)((sr*a + dr*da*(1-a)) / oa + 0.5f);
    int g = (int)((sg*a + dg*da*(1-a)) / oa + 0.5f);
    int b = (int)((sb*a + db*da*(1-a)) / oa + 0.5f);
    int A = (int)(oa * 255.0f + 0.5f);
    *slot = ((uint32_t)A << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static void render_shape(shape_t *sh, int W, int H) {
    if (!sh->fill_on && !sh->stroke_on) return;
    float half = sh->sw * 0.5f;
    for (int py = 0; py < H; py++) {
        for (int px = 0; px < W; px++) {
            float fc = 0, sc = 0;
            for (int sy = 0; sy < SS; sy++) {
                for (int sx = 0; sx < SS; sx++) {
                    float X = (float)px + ((float)sx + 0.5f) / (float)SS;
                    float Y = (float)py + ((float)sy + 0.5f) / (float)SS;
                    if (sh->fill_on && evenodd(X, Y, sh)) fc += 1.0f;
                    if (sh->stroke_on && half > 0 && stroke_hit(X, Y, sh, half)) sc += 1.0f;
                }
            }
            float inv = 1.0f / (float)(SS * SS);
            fc *= inv; sc *= inv;
            if (sh->fill_on && fc > 0) blend(px, py, sh->fill, sh->fill_a * fc);
            if (sh->stroke_on && sc > 0) blend(px, py, sh->stroke, sh->stroke_a * sc);
        }
    }
}
static void xform(shape_t *sh, float gtx, float gty, float gsx, float gsy,
                  float vsx, float vsy) {
    for (int s = 0; s < sh->nsp; s++) {
        sp_t *sp = &sh->sp[s];
        for (int i = 0; i < sp->n; i++) {
            float ux = sp->x[i], uy = sp->y[i];
            sp->x[i] = (gtx + ux * gsx) * vsx;
            sp->y[i] = (gty + uy * gsy) * vsy;
        }
    }
    sh->sw = sh->sw * gsx * vsx;
}

static void parse_transform(const char *tag, float *gtx, float *gty, float *gsx, float *gsy) {
    int len = 0;
    const char *v = find_attr(tag, "transform", &len);
    if (!v) return;
    char buf[256]; int n = len < 255 ? len : 255;
    for (int i = 0; i < n; i++) buf[i] = v[i];
    buf[n] = 0;
    const char *p = buf;
    while (*p) {
        if (sprefix(p, "translate(")) { p += 10; *gtx += pnum(&p) * (*gsx); *gty += pnum(&p) * (*gsy); }
        else if (sprefix(p, "scale(")) { p += 6; float s = pnum(&p); *gsx *= s; *gsy *= s; }
        else p++;
    }
}

int svg_render(const char *svg, int W, int H, uint32_t *out) {
    for (int i = 0; i < W * H; i++) out[i] = 0;
    g_out = out; g_W = W;

    float vbW = (float)W, vbH = (float)H;
    {
        int len = 0;
        const char *v = find_attr(svg, "viewBox", &len);
        if (v && len > 0) {
            char buf[128]; int n = len < 127 ? len : 127;
            for (int i = 0; i < n; i++) buf[i] = v[i];
            buf[n] = 0;
            const char *p = buf;
            (void)pnum(&p); (void)pnum(&p);
            vbW = pnum(&p); vbH = pnum(&p);
            if (vbW <= 0) vbW = (float)W;
            if (vbH <= 0) vbH = (float)H;
        }
    }
    float vsx = (float)W / vbW, vsy = (float)H / vbH;

    float gtx = 0, gty = 0, gsx = 1, gsy = 1;
    float stk[8][4]; int stk_n = 0;

    uint32_t fill = 0; float fill_a = 1; int fill_on = 0;
    uint32_t stroke = 0; float stroke_a = 1; float sw = 1; int stroke_on = 0;
    uint32_t sf[8], so[8]; float sfa[8], ssa[8], ssw[8]; int sfon[8], son[8];

    const char *p = svg;
    while (*p) {
        while (*p && *p != '<') p++;
        if (!*p) break;
        p++;
        if (*p == '!' || *p == '?') { while (*p && *p != '>') p++; if (*p) p++; continue; }

        int closing = 0;
        if (*p == '/') { closing = 1; p++; }
        const char *name_start = p;
        while (*p && !is_ws(*p) && *p != '>' && *p != '/') p++;
        int name_len = (int)(p - name_start);
        const char *gt = p;
        while (*gt && *gt != '>') gt++;
        if (!*gt) break;
        char name[32]; int nn = name_len < 31 ? name_len : 31;
        for (int i = 0; i < nn; i++) name[i] = name_start[i];
        name[nn] = 0;
        char tag[2048];
        { int bl = 0; for (const char *q = name_start; q < gt && bl < 2047; q++) tag[bl++] = q[0]; tag[bl] = 0; }

        if (closing) {
            if (seq(name, "g") && stk_n > 0) {
                stk_n--;
                gtx = stk[stk_n][0]; gty = stk[stk_n][1]; gsx = stk[stk_n][2]; gsy = stk[stk_n][3];
                fill = sf[stk_n]; fill_a = sfa[stk_n]; fill_on = sfon[stk_n];
                stroke = so[stk_n]; stroke_a = ssa[stk_n]; sw = ssw[stk_n]; stroke_on = son[stk_n];
            }
            p = gt + 1; continue;
        }
        if (seq(name, "defs")) {
            while (*gt && !sprefix(gt, "</defs")) gt++;
            while (*gt && *gt != '>') gt++;
            if (*gt) gt++;
            p = gt; continue;
        }
        if (seq(name, "svg") || seq(name, "g")) {
            if (seq(name, "g") && stk_n < 8) {
                stk[stk_n][0] = gtx; stk[stk_n][1] = gty; stk[stk_n][2] = gsx; stk[stk_n][3] = gsy;
                sf[stk_n] = fill; sfa[stk_n] = fill_a; sfon[stk_n] = fill_on;
                so[stk_n] = stroke; ssa[stk_n] = stroke_a; ssw[stk_n] = sw; son[stk_n] = stroke_on;
                stk_n++;
                parse_transform(tag, &gtx, &gty, &gsx, &gsy);
                char v[64]; float a;
                if (attr_copy(tag, "fill", v, sizeof(v))) { parse_color(v, &fill, &a); fill_a = a; fill_on = a > 0; }
                if (attr_copy(tag, "stroke", v, sizeof(v))) { parse_color(v, &stroke, &a); stroke_a = a; stroke_on = a > 0; }
                if (attr_copy(tag, "stroke-width", v, sizeof(v))) { const char *pp = v; sw = pnum(&pp); }
            }
            p = gt + 1; continue;
        }

        int is_shape = seq(name,"rect")||seq(name,"circle")||seq(name,"ellipse")||
                       seq(name,"line")||seq(name,"polygon")||seq(name,"path");
        if (is_shape) {
            shape_t sh;
            for (int i = 0; i < SVG_MAXSUB; i++) { sh.sp[i].n = 0; sh.sp[i].closed = 0; }
            sh.nsp = 0;
            sh.fill = fill; sh.fill_a = fill_a; sh.fill_on = fill_on;
            sh.stroke = stroke; sh.stroke_a = stroke_a; sh.sw = sw; sh.stroke_on = stroke_on;

            char v[1024]; float a;
            if (attr_copy(tag, "fill", v, sizeof(v))) { parse_color(v, &sh.fill, &a); sh.fill_a = a; sh.fill_on = a > 0; }
            if (attr_copy(tag, "fill-opacity", v, sizeof(v))) { const char *pp = v; sh.fill_a *= pnum(&pp); }
            if (attr_copy(tag, "stroke", v, sizeof(v))) { parse_color(v, &sh.stroke, &a); sh.stroke_a = a; sh.stroke_on = a > 0; }
            if (attr_copy(tag, "stroke-opacity", v, sizeof(v))) { const char *pp = v; sh.stroke_a *= pnum(&pp); }
            if (attr_copy(tag, "stroke-width", v, sizeof(v))) { const char *pp = v; sh.sw = pnum(&pp); }
            if (sh.fill_on && sh.fill_a <= 0) sh.fill_on = 0;
            if (sh.stroke_on && sh.stroke_a <= 0) sh.stroke_on = 0;

            if (seq(name, "rect")) {
                float x = attr_num(tag,"x",0), y = attr_num(tag,"y",0);
                float w = attr_num(tag,"width",0), h = attr_num(tag,"height",0);
                float rx = attr_num(tag,"rx",0);
                if (rx <= 0) rx = attr_num(tag,"ry",0);
                if (rx > w/2) rx = w/2;
                if (rx > h/2) rx = h/2;
                sp_t *sp = new_sub(&sh); sp->closed = 1;
                if (rx > 0) {
                    add_arc_pts(sp, x+w-rx, y+rx,     rx, -90, 0,   6);
                    add_arc_pts(sp, x+w-rx, y+h-rx,   rx, 0,   90,  6);
                    add_arc_pts(sp, x+rx,   y+h-rx,   rx, 90,  180, 6);
                    add_arc_pts(sp, x+rx,   y+rx,     rx, 180, 270, 6);
                } else {
                    add_pt(sp, x, y); add_pt(sp, x+w, y); add_pt(sp, x+w, y+h); add_pt(sp, x, y+h);
                }
            } else if (seq(name, "circle")) {
                float cx = attr_num(tag,"cx",0), cy = attr_num(tag,"cy",0), r = attr_num(tag,"r",0);
                sp_t *sp = new_sub(&sh); sp->closed = 1;
                add_arc_pts(sp, cx, cy, r, 0, 360, 40);
            } else if (seq(name, "ellipse")) {
                float cx = attr_num(tag,"cx",0), cy = attr_num(tag,"cy",0);
                float rx = attr_num(tag,"rx",0), ry = attr_num(tag,"ry",0);
                sp_t *sp = new_sub(&sh); sp->closed = 1;
                for (int k = 0; k < 40; k++) {
                    float ang = (float)k/40.0f*2.0f*(float)SVG_PI;
                    add_pt(sp, cx + rx*(float)cos(ang), cy + ry*(float)sin(ang));
                }
            } else if (seq(name, "line")) {
                sp_t *sp = new_sub(&sh); sp->closed = 0;
                add_pt(sp, attr_num(tag,"x1",0), attr_num(tag,"y1",0));
                add_pt(sp, attr_num(tag,"x2",0), attr_num(tag,"y2",0));
            } else if (seq(name, "polygon")) {
                if (attr_copy(tag, "points", v, sizeof(v))) {
                    const char *pp = v; sp_t *sp = new_sub(&sh); sp->closed = 1;
                    while (*pp) {
                        while (*pp && (is_ws(*pp) || *pp == ',')) pp++;
                        if (!*pp) break;
                        float x = pnum(&pp), y = pnum(&pp); add_pt(sp, x, y);
                    }
                }
            } else if (seq(name, "path")) {
                if (attr_copy(tag, "d", v, sizeof(v))) {
                    const char *pp = v; sp_t *sp = 0;
                    float cx = 0, cy = 0, sxp = 0, syp = 0; char cmd = 0;
                    while (*pp) {
                        const char *before = pp;
                        while (*pp && is_ws(*pp)) pp++;
                        if (!*pp) break;
                        if ((*pp >= 'A' && *pp <= 'Z') || (*pp >= 'a' && *pp <= 'z')) cmd = *pp++;
                        int rel = (cmd >= 'a'); char C = rel ? (char)(cmd - 32) : cmd;
                        if (C == 'M') {
                            float x = pnum(&pp), y = pnum(&pp); if (rel) { x+=cx; y+=cy; }
                            cx=x; cy=y; sxp=x; syp=y; sp = new_sub(&sh); add_pt(sp, cx, cy);
                            cmd = rel ? 'l' : 'L';
                        } else if (C == 'L') {
                            float x = pnum(&pp), y = pnum(&pp); if (rel) { x+=cx; y+=cy; }
                            cx=x; cy=y; if(!sp) sp=new_sub(&sh); add_pt(sp, cx, cy);
                        } else if (C == 'H') {
                            float x = pnum(&pp); if (rel) x+=cx; cx=x; if(!sp) sp=new_sub(&sh); add_pt(sp, cx, cy);
                        } else if (C == 'V') {
                            float y = pnum(&pp); if (rel) y+=cy; cy=y; if(!sp) sp=new_sub(&sh); add_pt(sp, cx, cy);
                        } else if (C == 'Z') {
                            if (sp) { add_pt(sp, sxp, syp); sp->closed = 1; }
                            cx=sxp; cy=syp;
                        } else if (C == 'C') {
                            float x1=pnum(&pp),y1=pnum(&pp),x2=pnum(&pp),y2=pnum(&pp),x=pnum(&pp),y=pnum(&pp);
                            if(rel){x1+=cx;y1+=cy;x2+=cx;y2+=cy;x+=cx;y+=cy;}
                            if(!sp) sp=new_sub(&sh); cubic(sp,cx,cy,x1,y1,x2,y2,x,y); cx=x; cy=y;
                        } else if (C == 'Q') {
                            float x1=pnum(&pp),y1=pnum(&pp),x=pnum(&pp),y=pnum(&pp);
                            if(rel){x1+=cx;y1+=cy;x+=cx;y+=cy;}
                            if(!sp) sp=new_sub(&sh); quad(sp,cx,cy,x1,y1,x,y); cx=x; cy=y;
                        } else if (C == 'A') {
                            float rx=pnum(&pp),ry=pnum(&pp),rot=pnum(&pp);
                            int fa=(int)pnum(&pp), fs=(int)pnum(&pp);
                            float x=pnum(&pp),y=pnum(&pp); if(rel){x+=cx;y+=cy;}
                            if(!sp) sp=new_sub(&sh); arc_to(sp,cx,cy,rx,ry,rot,fa,fs,x,y); cx=x; cy=y;
                        } else { pnum(&pp); }
                        if (pp == before) pp++;
                    }
                }
            }

            xform(&sh, gtx, gty, gsx, gsy, vsx, vsy);
            render_shape(&sh, W, H);
        }
        p = gt + 1;
    }
    return 0;
}
