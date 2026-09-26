#include "png.h"

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

int png_decode(const uint8_t *data, uint32_t len,
               png_alloc_fn alloc, png_free_fn freep,
               uint32_t **out, int *w, int *h) {
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (len < 8 || p_cmp8(data, sig) != 0) return -1;

    uint32_t width = 0, height = 0;
    int bitdepth = 0, colortype = -1, interlace = 0;
    uint32_t idat_total = 0;

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
        } else if (ctype[0] == 'I' && ctype[1] == 'E' && ctype[2] == 'N' && ctype[3] == 'D') {
            break;
        }
        pos += 12 + clen;
    }

    if (width == 0 || height == 0 || bitdepth != 8 || interlace != 0) return -1;
    int channels;
    switch (colortype) {
        case 0: channels = 1; break;
        case 2: channels = 3; break;
        case 4: channels = 2; break;
        case 6: channels = 4; break;
        default: return -1;
    }
    if (idat_total == 0) return -1;

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

    uint32_t raw_stride = width * (uint32_t)channels;
    uint32_t raw_size = height * (raw_stride + 1u);
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

    unsigned char *prev = (unsigned char *)alloc(raw_stride);
    unsigned char *cur = (unsigned char *)alloc(raw_stride);
    if (!prev || !cur) {
        freep(raw); if (prev) freep(prev); if (cur) freep(cur); freep(dst); return -1;
    }
    p_fill(prev, 0, raw_stride);

    for (uint32_t y = 0; y < height; y++) {
        const unsigned char *src = raw + y * (raw_stride + 1u);
        int filter = *src++;
        for (uint32_t x = 0; x < raw_stride; x++) {
            int a = (x >= (uint32_t)channels) ? cur[x - channels] : 0;
            int b = prev[x];
            int c = (x >= (uint32_t)channels) ? prev[x - channels] : 0;
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

        uint32_t *row = dst + (uint32_t)y * width;
        for (uint32_t x = 0; x < width; x++) {
            unsigned char r, g, bl;
            const unsigned char *p = cur + x * (uint32_t)channels;
            if (channels >= 3) { r = p[0]; g = p[1]; bl = p[2]; }
            else { r = g = bl = p[0]; }
            row[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | bl;
        }

        unsigned char *tmp = prev; prev = cur; cur = tmp;
    }

    freep(raw);
    freep(prev);
    freep(cur);

    *out = dst;
    *w = (int)width;
    *h = (int)height;
    return 0;
}
