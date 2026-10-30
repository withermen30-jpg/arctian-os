#!/usr/bin/env python3
"""
Ornek masaustu arka planlari (PNG) uretir -> store/server/wallpapers/.

DuckStore arka planlari kucuk olmalidir (OS ag tamponu ~176 KB), bu yuzden
gradyan/basit tasarimlar uretilir; masaustu bunlari ekrana olceklendirir.

Kullanim:
    python3 tools/gen_wallpapers.py
"""
import json
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "store", "server", "wallpapers")
W, H = 800, 450


def png_bytes(w, h, pix):
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: none
        for x in range(w):
            r, g, b = pix(x, y)
            raw += bytes((r, g, b))

    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit RGB
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def lerp(a, b, t):
    return int(a + (b - a) * t)


def vgrad(top, bot):
    def f(x, y):
        t = y / (H - 1)
        return (lerp(top[0], bot[0], t), lerp(top[1], bot[1], t), lerp(top[2], bot[2], t))
    return f


def diag(c1, c2):
    def f(x, y):
        t = (x / (W - 1) + y / (H - 1)) / 2.0
        return (lerp(c1[0], c2[0], t), lerp(c1[1], c2[1], t), lerp(c1[2], c2[2], t))
    return f


WALLS = {
    "gunbatimi": {"title": "Gün Batımı", "author": "Arctian",
                  "pix": vgrad((0x2B, 0x1B, 0x4A), (0xF5, 0x8A, 0x3B))},
    "okyanus":   {"title": "Okyanus", "author": "Arctian",
                  "pix": vgrad((0x06, 0x18, 0x2E), (0x2A, 0x9B, 0xD4))},
    "gece":      {"title": "Gece", "author": "Arctian",
                  "pix": vgrad((0x0A, 0x0A, 0x12), (0x2A, 0x2D, 0x3A))},
    "orman":     {"title": "Orman", "author": "Arctian",
                  "pix": vgrad((0x0B, 0x1F, 0x14), (0x4D, 0xBD, 0x8A))},
    "mor":       {"title": "Mor Sis", "author": "Arctian",
                  "pix": diag((0x3B, 0x82, 0xF6), (0x8B, 0x5C, 0xF6))},
    "kivilcim":  {"title": "Kıvılcım", "author": "Arctian",
                  "pix": diag((0x14, 0x16, 0x1C), (0xFE, 0xBC, 0x2E))},
}


def main():
    import argparse
    import random
    ap = argparse.ArgumentParser(description="Ornek arka plan uretici")
    ap.add_argument("--big", action="store_true",
                    help="Akisli indirmeyi test icin buyuk bir arka plan da uret (detay.png)")
    args = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    meta = {}
    for name, w in WALLS.items():
        data = png_bytes(W, H, w["pix"])
        path = os.path.join(OUT, name + ".png")
        with open(path, "wb") as f:
            f.write(data)
        meta[name] = {"title": w["title"], "author": w["author"]}
        print("%-12s %6d bayt" % (name + ".png", len(data)))

    if args.big:
        rnd = random.Random(1234)
        bw, bh = 1280, 720

        def noise(x, y):
            t = y / (bh - 1)
            n = rnd.randint(0, 60)
            return (min(255, lerp(0x10, 0x50, t) + n),
                    min(255, lerp(0x18, 0x60, t) + n),
                    min(255, lerp(0x30, 0x90, t) + n))

        data = png_bytes(bw, bh, noise)
        with open(os.path.join(OUT, "detay.png"), "wb") as f:
            f.write(data)
        meta["detay"] = {"title": "Detay Test", "author": "Arctian"}
        print("%-12s %6d bayt (buyuk test)" % ("detay.png", len(data)))

    with open(os.path.join(OUT, "wallpapers.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    print("-> %s" % OUT)


if __name__ == "__main__":
    main()
