#!/usr/bin/env python3
"""
wlfpack — .wlf paketleyici (masaustu SDK araci)

Bir .wlf iki bicimden biridir:

  native   : x86-64 freestanding makine kodu (libWolf'a linklenmis).
             Windows/Linux'ta gcc + ld + linker script ile uretilir.
  bytecode : Wolf VM programi (OS icindeki `wf build` uretir).

Bu arac iki isi yapar:
  1) Derlenmis `app.bin` + `wolf.json` -> `.wlf`
  2) `--bytecode` verilirse hazir Wolf bytecode'u sarar

Kullanim:
  python wlfpack.py --bin build/app.bin --entry 0 --json wolf.json -o app.wlf
  python wlfpack.py --bytecode prog.wlfb --json wolf.json -o app.wlf
  python wlfpack.py --inspect app.wlf

Baslik yerlesimi `include/arctian/wlf.h` (wlf_header_t) ile BIREBIR aynidir:
  <IHHHHiiIIIIIIIII> + name[32] + title[64] + ver[16] + author[40] + category[24]
"""
import argparse
import hashlib
import json
import os
import struct
import sys

WLF_MAGIC = 0x31464C57
WLF_VERSION = 1
WLF_KIND_NATIVE = 0
WLF_KIND_SCRIPT = 1

# 0.7: uygulama rolu. Header'daki reserved0 alaninin yerini aldi.
WLF_ROLE_UI, WLF_ROLE_CLI, WLF_ROLE_RUNTIME = 0, 1, 2
ROLE_NAMES = {"ui": WLF_ROLE_UI, "cli": WLF_ROLE_CLI, "runtime": WLF_ROLE_RUNTIME}
ROLE_BY_ID = {v: k for k, v in ROLE_NAMES.items()}

# Meta blogu: "WLFM" + u16 len + key=value satirlari (header'dan hemen sonra).
WLF_META_MAGIC = 0x4D464C57
WLF_META_HDR = 6

NAME_LEN, TITLE_LEN, VER_LEN, AUTHOR_LEN, CAT_LEN = 32, 64, 16, 40, 24

HDR_FMT = "<IHHHHiiIIIIIIII%ds%ds%ds%ds%ds" % (
    NAME_LEN, TITLE_LEN, VER_LEN, AUTHOR_LEN, CAT_LEN)
HDR_SIZE = struct.calcsize(HDR_FMT)   # 228
HEADER_BASE = struct.calcsize("<IHHHHiiIIIIIIII")   # 52

FLAG_FS, FLAG_NET, FLAG_EXEC = 0x0001, 0x0002, 0x0004
FLAG_RESIZE = 0x0100


def checksum(data):
    s = 0x12345678
    for b in data:
        s = ((s << 1) ^ (s >> 31) ^ b) & 0xFFFFFFFF
    return s


def parse_perms(p):
    if p is None:
        return 0
    if isinstance(p, (list, tuple)):
        items = [str(x).strip().lower() for x in p]
    else:
        items = [x.strip().lower() for x in str(p).split(",")]
    f = 0
    for it in items:
        if it in ("fs", "files", "dosya"):
            f |= FLAG_FS
        elif it in ("net", "network", "ag"):
            f |= FLAG_NET
        elif it in ("exec", "run", "calistir"):
            f |= FLAG_EXEC
        elif it in ("all", "tam", "*"):
            f |= FLAG_FS | FLAG_NET | FLAG_EXEC
    return f


def pack_meta(meta):
    """Meta blogunu uretir: rol, islenen uzantilar, giris adi, terminal."""
    role = str(meta.get("role", "ui")).strip().lower()
    if role.isdigit():
        role_id = int(role)
        role = ROLE_BY_ID.get(role_id, "ui")
    if role not in ROLE_NAMES:
        raise SystemExit("role ui|cli|runtime olmali")
    lines = ["role=%s" % role]

    hp = meta.get("handles")
    if hp:
        if isinstance(hp, (list, tuple)):
            hp = ",".join(str(x).strip().lstrip(".") for x in hp if str(x).strip())
        else:
            hp = ",".join(t.strip().lstrip(".") for t in str(hp).split(",") if t.strip())
        if hp:
            lines.append("handles=%s" % hp)

    if meta.get("entry"):
        lines.append("entry=%s" % str(meta["entry"]).strip())

    if meta.get("terminal") or role in ("cli", "runtime"):
        lines.append("terminal=1")

    body = ("\n".join(lines) + "\n").encode("utf-8")
    return struct.pack("<IH", WLF_META_MAGIC, len(body)) + body


def read_meta(data):
    """Meta blogunu cozer; yoksa bos sozluk + 0 uzunluk doner."""
    if len(data) < HDR_SIZE + WLF_META_HDR:
        return {}, 0
    magic, mlen = struct.unpack_from("<IH", data, HDR_SIZE)
    if magic != WLF_META_MAGIC:
        return {}, 0
    body = data[HDR_SIZE + WLF_META_HDR:HDR_SIZE + WLF_META_HDR + mlen]
    out = {}
    for line in body.decode("utf-8", "replace").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out, WLF_META_HDR + mlen


def build(meta, payload, kind, entry_off=0, load_addr=0, bss_size=0):
    name = str(meta.get("name") or "uygulama")
    if not name or len(name) > NAME_LEN - 1:
        raise SystemExit("name 1..%d karakter olmali" % (NAME_LEN - 1))
    win_w = int(meta.get("w", 520) or 520)
    win_h = int(meta.get("h", 400) or 400)
    flags = parse_perms(meta.get("permissions"))
    if meta.get("resizable"):
        flags |= FLAG_RESIZE

    role_name = str(meta.get("role", "ui")).strip().lower()
    try:
        role = int(role_name)
    except ValueError:
        role = ROLE_NAMES.get(role_name, WLF_ROLE_UI)

    icon = b""
    icon_name = meta.get("icon")
    if icon_name and os.path.isfile(icon_name):
        icon = open(icon_name, "rb").read()
    elif os.path.isfile("icon.png"):
        icon = open("icon.png", "rb").read()
    elif os.path.isfile("icon.svg"):
        icon = open("icon.svg", "rb").read()

    mblk = pack_meta(meta)
    payload_off = HDR_SIZE + len(mblk) + len(icon)
    cks = checksum(icon + payload)

    hdr = struct.pack(
        HDR_FMT,
        WLF_MAGIC, WLF_VERSION, kind, flags, role,
        win_w, win_h,
        load_addr, len(payload) if kind == WLF_KIND_NATIVE else 0,
        bss_size, entry_off,
        payload_off, len(payload), len(icon), cks,
        name.encode()[:NAME_LEN - 1],
        str(meta.get("title", name)).encode()[:TITLE_LEN - 1],
        str(meta.get("version", "1.0")).encode()[:VER_LEN - 1],
        str(meta.get("publisher", meta.get("author", ""))).encode()[:AUTHOR_LEN - 1],
        str(meta.get("category", "Genel")).encode()[:CAT_LEN - 1],
    )
    assert len(hdr) == HDR_SIZE, (len(hdr), HDR_SIZE)
    return hdr + mblk + icon + payload


def sign_pkg(body, key_path):
    """PKCS#1 v1.5 / SHA-256 ile RSA-2048 imza blogu uretir ("ASG1" + 256 bayt).

    Anahtar dosyasi: n=<hex>, e=<hex>, d=<hex> satirlari.
    Yalnizca Arctian ekibi icin; normal gelistirici akisinda paket imzasiz
    kalir ve imza sunucuda eklenir.
    """
    kv = {}
    with open(key_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if "=" in line:
                k, v = line.split("=", 1)
                kv[k.strip()] = v.strip()
    n = int(kv["n"], 16)
    d = int(kv["d"], 16)
    di = bytes.fromhex("3031300d060960864801650304020105000420")
    em = (b"\x00\x01" + b"\xff" * (256 - 3 - len(di) - 32) + b"\x00" + di
          + hashlib.sha256(body).digest())
    if len(em) != 256:
        raise SystemExit("imza bloğu uzunluğu hatalı")
    sig = pow(int.from_bytes(em, "big"), d, n).to_bytes(256, "big")
    return b"ASG1" + sig


def inspect(path):
    data = open(path, "rb").read()
    if len(data) < HDR_SIZE:
        raise SystemExit("dosya cok kucuk")
    f = struct.unpack(HDR_FMT, data[:HDR_SIZE])
    (magic, ver, kind, flags, role, w, h, load, isz, bss, entry,
     poff, plen, ilen, cks, name, title, sver, author, cat) = f
    if magic != WLF_MAGIC:
        raise SystemExit("gecersiz .wlf (magic=0x%08X)" % magic)
    m, mlen = read_meta(data)
    icon = data[HDR_SIZE + mlen:HDR_SIZE + mlen + ilen]
    payload = data[poff:poff + plen]
    ok = checksum(icon + payload) == cks

    def s(raw):
        return raw.split(b"\x00", 1)[0].decode("utf-8", "replace")

    print("Ad       : %s" % s(name))
    print("Baslik   : %s" % s(title))
    print("Surum    : %s" % s(sver))
    print("Yazar    : %s" % s(author))
    print("Kategori : %s" % s(cat))
    print("Tur      : %s" % ("native" if kind == WLF_KIND_NATIVE else "bytecode"))
    print("Rol      : %s" % m.get("role", ROLE_BY_ID.get(role, "ui")))
    if m.get("handles"):
        print("Isler    : %s" % m["handles"])
    if m.get("entry"):
        print("Giris    : %s" % m["entry"])
    print("Pencere  : %dx%d" % (w, h))
    print("Yukleme  : 0x%08X  image=%d bss=%d entry=0x%X" % (load, isz, bss, entry))
    print("Izinler  : " + ("tam" if flags == 0 else " ".join(
        n for n, b in (("fs", FLAG_FS), ("net", FLAG_NET), ("exec", FLAG_EXEC))
        if flags & b)))
    print("Payload  : %d bayt (offset %d), ikon %d bayt" % (plen, poff, ilen))
    print("Checksum : %s" % ("OK" if ok else "HATALI"))


# ----------------------------------------------------------------------
# Proje sablonu: wf new / wolf new ayni kaynaktan uretir
# (market MCP get_project_template ile sozlesmesi aynidir)
# ----------------------------------------------------------------------
TMPL_C = """/* %(title)s - Arctian .wlf uygulamasi
 *
 * libWolf (wolf.h) uzerinden isletim sistemiyle konusur. C ile yazilir.
 * Tum API listesi: ../include/wolf.h (tek dogru kaynak)
 * Derleme : wolf build            (Windows)  /  sh build.sh  (Linux/WSL)
 * Test    : wolf run %(name)s.wlf           /  wolf shot %(name)s.wlf
 */
#include "wolf.h"

static wolf_id g_status;

static void on_btn(wolf_id w, void *user) {
    (void)w; (void)user;
    static int n;
    char buf[64], num[16];
    n++;
    wolf_strcpy(buf, sizeof buf, "Tiklama: ");
    wolf_itoa(n, num, sizeof num);
    wolf_strcat(buf, sizeof buf, num);
    wolf_text(g_status, buf);
}

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return -1;

    wolf_win_title("%(title)s");
    wolf_win_size(%(w)d, %(h)d);

    wolf_id root = wolf_root();
    wolf_label(root, 24, 24, "%(title)s");
    g_status = wolf_label(root, 24, 56, "Hazir");
    wolf_id b = wolf_button(root, 24, 96, 180, 44, "Tikla");
    wolf_on_click(b, on_btn);
    return 0;
}
"""

TMPL_JSON = """{
  "name": "%(name)s",
  "title": "%(title)s",
  "version": "1.0",
  "publisher": "%(publisher)s",
  "category": "Genel",
  "w": %(w)d, "h": %(h)d,
  "permissions": []
}
"""

TMPL_README = """%(title)s
--------
Uygulama kimligi : %(name)s  (paket: %(name)s.wlf)
Boyut            : %(wdx)d x %(wdh)d

Akis:
  1) main.c      -> kodu yaz (API: ../include/wolf.h)
  2) wolf.json   -> ad, boyut, izinler
  3) wolf build  -> %(name)s.wlf
  4) wolf run    -> emulatorde calistir;  wolf shot -> PNG

ABI v2 (SDK >= 1.3.0): wolf_style / wolf_transition / wolf_state_set /
wolf_anim_in ile CSS benzeri stil, durum gecisi ve animasyon.
Canli ornek: ../axiom (iki tema, gecisler, giris animasyonlari).
"""


def slug(name):
    out = "".join(c if (c.isalnum() or c in "-_") else "-" for c in
                  (name or "").lower()).strip("-")
    out = "".join(c for c in out if c.isascii())
    return out[:32] or "uygulama"


def new_project(name, title=None, w=520, h=400, publisher="Gelistirici"):
    nm = slug(name)
    title = (title or name or nm)[:64]
    if os.path.isdir(nm):
        raise SystemExit("[wf] '%s' zaten var" % nm)
    os.makedirs(nm)
    vals = {"name": nm, "title": title, "w": w, "h": h, "publisher": publisher,
            "wdx": w, "wdh": h}
    for fn, tmpl in (("main.c", TMPL_C), ("wolf.json", TMPL_JSON),
                     ("README.txt", TMPL_README)):
        with open(os.path.join(nm, fn), "w", encoding="utf-8",
                  newline="\n") as f:
            f.write(tmpl % vals)
    print("[wf] sablon olusturuldu: %s/  (main.c, wolf.json, README.txt)" % nm)
    print("[wf] kodla:   %s\\main.c   (API listesi: ..\\include\\wolf.h)" % nm
          if os.name == "nt" else
          "[wf] kodla:   %s/main.c   (API listesi: ../include/wolf.h)" % nm)
    print("[wf] derle:   cd %s && wolf build" % nm)
    print("[wf] calistir: wolf run %s.wlf" % nm)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--new", metavar="AD", help="proje sablonu olustur")
    ap.add_argument("--title", help="--new icin gorunen ad (varsayilan: AD)")
    ap.add_argument("--w", type=int, default=520, help="--new pencere genisligi")
    ap.add_argument("--h", type=int, default=400, help="--new pencere yuksekligi")
    ap.add_argument("--bin", help="native kod (objcopy -O binary ciktisi)")
    ap.add_argument("--bytecode", help="Wolf bytecode (.wlfb)")
    ap.add_argument("--json", default="wolf.json")
    ap.add_argument("--entry", type=int, default=0, help="entry_off")
    ap.add_argument("--load", type=lambda v: int(v, 0), default=0x03000000)
    ap.add_argument("--bss", type=int, default=0)
    ap.add_argument("-o", "--out")
    ap.add_argument("--inspect")
    ap.add_argument("--key", help="yerel imza anahtari (yalniz Arctian ekibi icin)")
    a = ap.parse_args()

    if a.new:
        new_project(a.new, title=a.title,
                    w=max(240, min(1600, a.w)), h=max(180, min(1200, a.h)))
        return

    if a.inspect:
        inspect(a.inspect)
        return

    meta = {}
    if os.path.isfile(a.json):
        meta = json.load(open(a.json, encoding="utf-8"))

    if a.bytecode:
        payload = open(a.bytecode, "rb").read()
        blob = build(meta, payload, WLF_KIND_SCRIPT)
    elif a.bin:
        payload = open(a.bin, "rb").read()
        blob = build(meta, payload, WLF_KIND_NATIVE,
                     entry_off=a.entry, load_addr=a.load, bss_size=a.bss)
    else:
        raise SystemExit("--bin veya --bytecode gerekli")

    out = a.out or (str(meta.get("name", "uygulama")) + ".wlf")

    # ------------------------------------------------------------------
    # Imza
    #
    # .wlf dosyalari OS'te yalnizca MAGAZA anahtariyla imzali halde
    # kurulabilir. Bu ozel anahtar gelistiriciye verilmez: paket sunucuya
    # gonderilir, orada dogrulanir ve imzalanir (onay akisindan sonra).
    #
    # Bu yuzden yerel derleme ciktiSI IMZASIZDIR ve:
    #   - Arctian Emulator'de calisir (gelistirme onizlemesi)
    #   - Arctian Market'e yuklenip onaya gonderilebilir
    #   - OS'te dogrudan `wf install` ile KURULAMAZ (imza yok)
    #
    # Arctian ekibi yerel test icin --key <dosya> vererek imzalayabilir.
    # ------------------------------------------------------------------
    signed = blob
    key_path = a.key or os.environ.get("LIBWOLF_SIGN_KEY")
    if key_path:
        signed = blob + sign_pkg(blob, key_path)

    open(out, "wb").write(signed)
    print("WLF: %s (%d bayt, %s, %s)" % (
        out, len(signed), "bytecode" if a.bytecode else "native",
        "imzali" if len(signed) != len(blob) else "imzasiz - onayda sunucu imzalar"))


if __name__ == "__main__":
    main()
