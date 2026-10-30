/**
 * Arctian minimal PNG cozucu (freestanding)
 *  - zlib/DEFLATE inflate (stored + fixed + dynamic Huffman)
 *  - PNG filtreleri (None/Sub/Up/Average/Paeth)
 *  - 8-bit, non-interlaced: gray / RGB / gray+alpha / RGBA
 *  - Cikti: 0x00RRGGBB (XRGB8888)
 */
#include "png.h"

/* ---------- yerel kopyalama ---------- */
static void p_copy(void *d, const void *s, size_t n) {
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}
static void p_fill(void *d, int c, size_t n) {
    unsigned char *dd = (unsigned char *)d;
    for (size_t i = 0; i < n; i++) dd[i] = (unsigned char)c;
}
static int p_cmp8(const unsigned char *a, const unsigned char *b) {
    for (int i = 0; i < 8; i++) if (a[i] != b[i]) return 1;
    return 0;
}

/* ================= INFLATE ================= */
#define MAXBITS 15

typedef struct {
    const unsigned char *in;
    uint32_t inlen, incnt;
    uint32_t bitbuf;
    int bitcnt;
} bits_t;

typedef struct {
    unsigned char *out;
    uint32_t cap, pos;
} outbuf_t;

typedef struct {
    short count[MAXBITS + 1];
    short symbol[288];
} huff_t;

static int get_bits(bits_t *s, int need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->incnt >= s->inlen) return -1;
        val |= (uint32_t)s->in[s->incnt++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (int)(val & ((1u << need) - 1u));
}

static void bits_align(bits_t *s) { s->bitbuf = 0; s->bitcnt = 0; }

static int huff_construct(huff_t *h, const short *length, int n) {
    int len, left;
    short offs[MAXBITS + 1];
    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (int sym = 0; sym < n; sym++) h->count[length[sym]]++;
    if (h->count[0] == n) return 0;
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (int sym = 0; sym < n; sym++)
        if (length[sym] != 0) h->symbol[offs[length[sym]]++] = (short)sym;
    /* left >= 0: complete veya eksik (izlib gibi tolere edilir); left < 0: asiri abone */
    return left < 0 ? -1 : 0;
}

static int huff_decode(bits_t *s, const huff_t *h) {
    int len = 1, code = 0, first = 0, index = 0, count;
    for (;;) {
        int b = get_bits(s, 1);
        if (b < 0) return -1;
        code |= b;
        count = h->count[len];
        if (code - first < count) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        len++;
        if (len > MAXBITS) return -1;
    }
}

static int out_put(outbuf_t *o, int c) {
    if (o->pos >= o->cap) return -1;
    o->out[o->pos++] = (unsigned char)c;
    return 0;
}

static int out_copy(outbuf_t *o, uint32_t dist, uint32_t len) {
    if (dist == 0 || dist > o->pos) return -1;
    for (uint32_t i = 0; i < len; i++) {
        if (o->pos >= o->cap) return -1;
        o->out[o->pos] = o->out[o->pos - dist];
        o->pos++;
    }
    return 0;
}

static const short len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const short len_ext[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const short dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,
    3073,4097,6145,8193,12289,16385,24577
};
static const short dist_ext[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static int inflate_codes(bits_t *s, outbuf_t *o, const huff_t *lencode, const huff_t *distcode) {
    for (;;) {
        int sym = huff_decode(s, lencode);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (out_put(o, sym) != 0) return -1;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            int e = len_ext[sym];
            int len = len_base[sym];
            if (e) { int b = get_bits(s, e); if (b < 0) return -1; len += b; }

            int dsym = huff_decode(s, distcode);
            if (dsym < 0 || dsym >= 30) return -1;
            int de = dist_ext[dsym];
            int dist = dist_base[dsym];
            if (de) { int b = get_bits(s, de); if (b < 0) return -1; dist += b; }

            if (out_copy(o, (uint32_t)dist, (uint32_t)len) != 0) return -1;
        }
    }
}

static int inflate_stored(bits_t *s, outbuf_t *o) {
    bits_align(s);
    if (s->incnt + 4 > s->inlen) return -1;
    uint32_t len = s->in[s->incnt++];
    len |= (uint32_t)s->in[s->incnt++] << 8;
    uint32_t nlen = s->in[s->incnt++];
    nlen |= (uint32_t)s->in[s->incnt++] << 8;
    if ((len ^ 0xFFFFu) != nlen) return -1;
    if (s->incnt + len > s->inlen) return -1;
    for (uint32_t i = 0; i < len; i++)
        if (out_put(o, s->in[s->incnt++]) != 0) return -1;
    return 0;
}

static int inflate_fixed(bits_t *s, outbuf_t *o) {
    static huff_t lencode, distcode;
    static int init = 0;
    if (!init) {
        short lengths[288];
        int i;
        for (i = 0; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        huff_construct(&lencode, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        huff_construct(&distcode, lengths, 30);
        init = 1;
    }
    return inflate_codes(s, o, &lencode, &distcode);
}

static const short clc_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

static int inflate_dynamic(bits_t *s, outbuf_t *o) {
    static huff_t lencode, distcode;
    short lengths[288 + 32];
    int i;

    int hlit = get_bits(s, 5); if (hlit < 0) return -1; hlit += 257;
    int hdist = get_bits(s, 5); if (hdist < 0) return -1; hdist += 1;
    int hclen = get_bits(s, 4); if (hclen < 0) return -1; hclen += 4;

    for (i = 0; i < 288 + 32; i++) lengths[i] = 0;
    for (i = 0; i < hclen; i++) {
        int b = get_bits(s, 3);
        if (b < 0) return -1;
        lengths[clc_order[i]] = (short)b;
    }
    if (huff_construct(&lencode, lengths, 19) != 0) return -1;

    int index = 0;
    while (index < hlit + hdist) {
        int sym = huff_decode(s, &lencode);
        if (sym < 0) return -1;
        if (sym < 16) {
            lengths[index++] = (short)sym;
        } else if (sym == 16) {
            if (index == 0) return -1;
            int len = lengths[index - 1];
            int b = get_bits(s, 2); if (b < 0) return -1;
            int rep = b + 3;
            while (rep-- && index < hlit + hdist) lengths[index++] = (short)len;
        } else if (sym == 17) {
            int b = get_bits(s, 3); if (b < 0) return -1;
            int rep = b + 3;
            while (rep-- && index < hlit + hdist) lengths[index++] = 0;
        } else {
            int b = get_bits(s, 7); if (b < 0) return -1;
            int rep = b + 11;
            while (rep-- && index < hlit + hdist) lengths[index++] = 0;
        }
    }

    if (huff_construct(&lencode, lengths, hlit) != 0) return -1;
    if (huff_construct(&distcode, lengths + hlit, hdist) != 0) return -1;
    return inflate_codes(s, o, &lencode, &distcode);
}

static int zlib_inflate(const unsigned char *in, uint32_t inlen, outbuf_t *o) {
    if (inlen < 2) return -1;
    uint32_t cmf = in[0], flg = in[1];
    if ((cmf & 0x0f) != 8) return -1;
    if (((cmf << 8) | flg) % 31 != 0) return -1;
    if (flg & 0x20) return -1;

    bits_t s;
    s.in = in; s.inlen = inlen; s.incnt = 2; s.bitbuf = 0; s.bitcnt = 0;

    int last;
    do {
        last = get_bits(&s, 1);
        if (last < 0) return -1;
        int type = get_bits(&s, 2);
        if (type < 0) return -1;

        int err;
        if (type == 0) err = inflate_stored(&s, o);
        else if (type == 1) err = inflate_fixed(&s, o);
        else if (type == 2) err = inflate_dynamic(&s, o);
        else return -1;
        if (err != 0) return err;
    } while (!last);

    return 0;
}

/* ================= PNG ================= */

static uint32_t rd32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static unsigned png_sample(const unsigned char *row, int bitdepth, uint32_t ch) {
    if (bitdepth == 8) return row[ch];
    if (bitdepth == 16) return ((unsigned)row[ch * 2u] << 8) | row[ch * 2u + 1u];
    int per = 8 / bitdepth;
    int shift = 8 - bitdepth * ((int)(ch % (uint32_t)per) + 1);
    return ((unsigned)row[ch / (uint32_t)per] >> shift) & ((1u << bitdepth) - 1u);
}

static int png_decode_impl(const uint8_t *data, uint32_t len,
                           png_alloc_fn alloc, png_free_fn freep,
                           int keep_alpha,
                           uint32_t **out, int *w, int *h) {
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (len < 8 || p_cmp8(data, sig) != 0) return -1;

    uint32_t width = 0, height = 0;
    int bitdepth = 0, colortype = -1, interlace = 0;
    uint32_t idat_total = 0;
    const unsigned char *plte = 0;
    uint32_t plte_len = 0;
    const unsigned char *trns = 0;
    uint32_t trns_len = 0;

    uint32_t pos = 8;
    while (pos + 8 <= len) {
        uint32_t clen = rd32(data + pos);
        const unsigned char *ctype = data + pos + 4;
        const unsigned char *cdata = data + pos + 8;
        if (pos + 12 + clen > len) break;
        if (ctype[0] == 'I' && ctype[1] == 'H' && ctype[2] == 'D' && ctype[3] == 'R') {
            width = rd32(cdata);
            height = rd32(cdata + 4);
            bitdepth = cdata[8];
            colortype = cdata[9];
            interlace = cdata[12];
        } else if (ctype[0] == 'I' && ctype[1] == 'D' && ctype[2] == 'A' && ctype[3] == 'T') {
            idat_total += clen;
        } else if (ctype[0] == 'P' && ctype[1] == 'L' && ctype[2] == 'T' && ctype[3] == 'E') {
            plte = cdata; plte_len = clen;
        } else if (ctype[0] == 't' && ctype[1] == 'R' && ctype[2] == 'N' && ctype[3] == 'S') {
            trns = cdata; trns_len = clen;
        } else if (ctype[0] == 'I' && ctype[1] == 'E' && ctype[2] == 'N' && ctype[3] == 'D') {
            break;
        }
        pos += 12 + clen;
    }

    if (width == 0 || height == 0 || idat_total == 0) return -1;
    if (width > 8192 || height > 8192) return -1;
    if (bitdepth != 1 && bitdepth != 2 && bitdepth != 4 &&
        bitdepth != 8 && bitdepth != 16) return -1;

    int channels;
    switch (colortype) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 3: channels = 1; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return -1;
    }
    if (colortype != 0 && colortype != 3 && bitdepth < 8) return -1;
    if (colortype == 3 && bitdepth == 16) return -1;
    if (colortype == 3 && plte_len < 3) return -1;

    unsigned char *idat = (unsigned char *)alloc(idat_total);
    if (!idat) return -1;
    {
        uint32_t off = 0;
        pos = 8;
        while (pos + 8 <= len) {
            uint32_t clen = rd32(data + pos);
            const unsigned char *ctype = data + pos + 4;
            const unsigned char *cdata = data + pos + 8;
            if (pos + 12 + clen > len) break;
            if (ctype[0] == 'I' && ctype[1] == 'D' && ctype[2] == 'A' && ctype[3] == 'T') {
                p_copy(idat + off, cdata, clen);
                off += clen;
            } else if (ctype[0] == 'I' && ctype[1] == 'E' && ctype[2] == 'N' && ctype[3] == 'D') {
                break;
            }
            pos += 12 + clen;
        }
    }

    static const int xorig[7] = { 0, 4, 0, 2, 0, 1, 0 };
    static const int yorig[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const int xstep[7] = { 8, 8, 4, 4, 2, 2, 1 };
    static const int ystep[7] = { 8, 8, 8, 4, 4, 2, 2 };
    int npass = interlace ? 7 : 1;

    uint32_t raw_size = 0;
    for (int pass = 0; pass < npass; pass++) {
        int xo = interlace ? xorig[pass] : 0;
        int yo = interlace ? yorig[pass] : 0;
        int xs = interlace ? xstep[pass] : 1;
        int ys = interlace ? ystep[pass] : 1;
        int pw = (int)((width - (uint32_t)xo + (uint32_t)xs - 1u) / (uint32_t)xs);
        int ph = (int)((height - (uint32_t)yo + (uint32_t)ys - 1u) / (uint32_t)ys);
        if (pw <= 0 || ph <= 0) continue;
        uint32_t rowbytes = ((uint32_t)pw * (uint32_t)channels * (uint32_t)bitdepth + 7u) / 8u;
        raw_size += (rowbytes + 1u) * (uint32_t)ph;
    }
    if (raw_size == 0) { freep(idat); return -1; }

    unsigned char *raw = (unsigned char *)alloc(raw_size);
    if (!raw) { freep(idat); return -1; }

    outbuf_t ob;
    ob.out = raw; ob.cap = raw_size; ob.pos = 0;
    if (zlib_inflate(idat, idat_total, &ob) != 0 || ob.pos != raw_size) {
        freep(idat); freep(raw); return -1;
    }
    freep(idat);

    uint32_t *dst = (uint32_t *)alloc((size_t)width * height * 4u);
    if (!dst) { freep(raw); return -1; }

    unsigned char pal[256][3];
    unsigned char pal_a[256];
    int gray_trns = -1, has_gray_trns = 0, has_rgb_trns = 0;
    int rgb_trns[3] = { 0, 0, 0 };
    if (colortype == 3) {
        int np = (int)(plte_len / 3);
        if (np > 256) np = 256;
        for (int i = 0; i < 256; i++) { pal[i][0] = pal[i][1] = pal[i][2] = 0; pal_a[i] = 255; }
        for (int i = 0; i < np; i++) {
            pal[i][0] = plte[i * 3 + 0];
            pal[i][1] = plte[i * 3 + 1];
            pal[i][2] = plte[i * 3 + 2];
        }
        int na = (int)trns_len; if (na > np) na = np;
        for (int i = 0; i < na; i++) pal_a[i] = trns[i];
    } else if (colortype == 0 && trns && trns_len >= 2) {
        gray_trns = (trns[0] << 8) | trns[1];
        has_gray_trns = 1;
    } else if (colortype == 2 && trns && trns_len >= 6) {
        rgb_trns[0] = (trns[0] << 8) | trns[1];
        rgb_trns[1] = (trns[2] << 8) | trns[3];
        rgb_trns[2] = (trns[4] << 8) | trns[5];
        has_rgb_trns = 1;
    }

    uint32_t rp = 0;
    for (int pass = 0; pass < npass; pass++) {
        int xo = interlace ? xorig[pass] : 0;
        int yo = interlace ? yorig[pass] : 0;
        int xs = interlace ? xstep[pass] : 1;
        int ys = interlace ? ystep[pass] : 1;
        int pw = (int)((width - (uint32_t)xo + (uint32_t)xs - 1u) / (uint32_t)xs);
        int ph = (int)((height - (uint32_t)yo + (uint32_t)ys - 1u) / (uint32_t)ys);
        if (pw <= 0 || ph <= 0) continue;
        uint32_t rowbytes = ((uint32_t)pw * (uint32_t)channels * (uint32_t)bitdepth + 7u) / 8u;
        int bpp = (channels * bitdepth + 7) / 8;
        if (bpp < 1) bpp = 1;

        unsigned char *prev = (unsigned char *)alloc(rowbytes);
        unsigned char *cur = (unsigned char *)alloc(rowbytes);
        if (!prev || !cur) {
            if (prev) freep(prev);
            if (cur) freep(cur);
            freep(raw); freep(dst); return -1;
        }
        p_fill(prev, 0, rowbytes);

        for (int py = 0; py < ph; py++) {
            const unsigned char *src = raw + rp;
            int filter = *src++;
            rp += rowbytes + 1u;
            uint32_t x;
            for (x = 0; x < rowbytes; x++) {
                int a = (x >= (uint32_t)bpp) ? cur[x - (uint32_t)bpp] : 0;
                int b = prev[x];
                int c = (x >= (uint32_t)bpp) ? prev[x - (uint32_t)bpp] : 0;
                int v = src[x];
                switch (filter) {
                    case 0: break;
                    case 1: v += a; break;
                    case 2: v += b; break;
                    case 3: v += (a + b) >> 1; break;
                    case 4: v += paeth(a, b, c); break;
                    default: break;
                }
                cur[x] = (unsigned char)(v & 0xFF);
            }

            uint32_t yy = (uint32_t)(yo + py * ys);
            if (yy < height) {
                uint32_t *row = dst + (size_t)yy * width;
                for (int px = 0; px < pw; px++) {
                    uint32_t xx = (uint32_t)(xo + px * xs);
                    if (xx >= width) continue;
                    unsigned r8 = 0, g8 = 0, b8 = 0, a8 = 255;
                    if (colortype == 3) {
                        unsigned idx = png_sample(cur, bitdepth, (uint32_t)px);
                        if (idx > 255u) idx = 255u;
                        r8 = pal[idx][0]; g8 = pal[idx][1]; b8 = pal[idx][2];
                        if (keep_alpha) a8 = pal_a[idx];
                    } else if (colortype == 0) {
                        unsigned v = png_sample(cur, bitdepth, (uint32_t)px);
                        if (has_gray_trns && keep_alpha && (int)v == gray_trns) a8 = 0;
                        if (bitdepth < 8) { int mx = (1 << bitdepth) - 1; v = v * 255u / (unsigned)mx; }
                        else if (bitdepth == 16) v >>= 8;
                        r8 = g8 = b8 = v;
                    } else if (colortype == 4) {
                        unsigned v = png_sample(cur, bitdepth, (uint32_t)px * 2u);
                        unsigned al = png_sample(cur, bitdepth, (uint32_t)px * 2u + 1u);
                        if (bitdepth == 16) { v >>= 8; al >>= 8; }
                        r8 = g8 = b8 = v;
                        if (keep_alpha) a8 = al;
                    } else if (colortype == 2) {
                        unsigned rr = png_sample(cur, bitdepth, (uint32_t)px * 3u);
                        unsigned gg = png_sample(cur, bitdepth, (uint32_t)px * 3u + 1u);
                        unsigned bb = png_sample(cur, bitdepth, (uint32_t)px * 3u + 2u);
                        if (has_rgb_trns && keep_alpha &&
                            (int)rr == (rgb_trns[0] >> 8) &&
                            (int)gg == (rgb_trns[1] >> 8) &&
                            (int)bb == (rgb_trns[2] >> 8)) a8 = 0;
                        if (bitdepth == 16) { rr >>= 8; gg >>= 8; bb >>= 8; }
                        r8 = rr; g8 = gg; b8 = bb;
                    } else {
                        unsigned rr = png_sample(cur, bitdepth, (uint32_t)px * 4u);
                        unsigned gg = png_sample(cur, bitdepth, (uint32_t)px * 4u + 1u);
                        unsigned bb = png_sample(cur, bitdepth, (uint32_t)px * 4u + 2u);
                        unsigned al = png_sample(cur, bitdepth, (uint32_t)px * 4u + 3u);
                        if (bitdepth == 16) { rr >>= 8; gg >>= 8; bb >>= 8; al >>= 8; }
                        r8 = rr; g8 = gg; b8 = bb;
                        if (keep_alpha) a8 = al;
                    }
                    row[xx] = ((uint32_t)a8 << 24) | ((uint32_t)r8 << 16) |
                              ((uint32_t)g8 << 8) | (uint32_t)b8;
                }
            }

            unsigned char *tmp = prev; prev = cur; cur = tmp;
        }
        freep(prev);
        freep(cur);
    }

    freep(raw);

    *out = dst;
    *w = (int)width;
    *h = (int)height;
    return 0;
}

int png_decode(const uint8_t *data, uint32_t len,
               png_alloc_fn alloc, png_free_fn freep,
               uint32_t **out, int *w, int *h) {
    return png_decode_impl(data, len, alloc, freep, 0, out, w, h);
}

int png_decode_argb(const uint8_t *data, uint32_t len,
                    png_alloc_fn alloc, png_free_fn freep,
                    uint32_t **out, int *w, int *h) {
    return png_decode_impl(data, len, alloc, freep, 1, out, w, h);
}
