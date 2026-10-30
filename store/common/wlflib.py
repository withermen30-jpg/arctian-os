#!/usr/bin/env python3
"""
wlflib — .wlf paket dogrulama, imzalama ve magaza yardimcilari (sunucu tarafı).

Yalnizca Python 3 standart kutuphanesi kullanir.

Bu modul OS'taki `wlf_validate()` (desktop/wlf.c) ve `sig_*` (desktop/sig.c)
mantigini SUNUCU tarafinda birebir aynalar. Amaclari:

  1) Gelen ucuncu taraf .wlf dosyalarini OS'in kabul edecegi sekilde
     dogrulamak (bicim + butunluk). Boylece bozuk / Bindirma (traversal) /
     taşma (overflow) acili .wlf sunucudan hic gecmez.
  2) Dogrulanmis paketi MAGAZA anahtariyla (tools/keys/dev_rsa2048.txt)
     yeniden imzalayarak yayina almak. Boylece guven kokunun sahibi
     magaza olur; OS yalnizca kendi gostermesindeki acik anahtarla dogrular.

Onemli sinir (dürüst not): native .wlf rastgele x86-64 makine kodudur; sunucu
yapisal dogrulama + imza zinciri yapabilir, ancak kodun "kotu niyetli olmadigini"
anlamli (semantik) olarak onaylayamaz. Bu modul format/butunluk/limit guvenligini
verir; calisma zamani yetki sinirlari OS'in CAP_* modelinde kalir.
"""
import hashlib
import os
import struct
import sys

# ------------------------------------------------------------------ sabitler
# include/arctian/wlf.h ile BIREBIR ayni degerler.
WLF_MAGIC = 0x31464C57          # "WLF1" (little-endian)
WLF_VERSION = 1
WLF_KIND_NATIVE = 0
WLF_KIND_SCRIPT = 1
WLF_ROLE_UI, WLF_ROLE_CLI, WLF_ROLE_RUNTIME = 0, 1, 2
WLF_META_MAGIC = 0x4D464C57     # "WLFM"
WLF_META_HDR = 6                # 4 bayt sihir + 2 bayt uzunluk

WLF_NAME_LEN = 32
WLF_TITLE_LEN = 64
WLF_VER_LEN = 16
WLF_AUTHOR_LEN = 40
WLF_CATEGORY_LEN = 24
WLF_MAX_PACKAGE = 256 * 1024    # OS tamponu (s_wlf_buf)

# Bayraklar (izinler + pencere davranisi)
WLF_FLAG_FS = 0x0001
WLF_FLAG_NET = 0x0002
WLF_FLAG_EXEC = 0x0004
WLF_FLAG_RESIZE = 0x0100
WLF_FLAG_KNOWN = WLF_FLAG_FS | WLF_FLAG_NET | WLF_FLAG_EXEC | WLF_FLAG_RESIZE

# include/arctian/memory_map.h
WLF_BASE = 0x03000000
WLF_MAX = 8 * 1024 * 1024

# include/arctian/sig.h
SIG_MAGIC = b"ASG1"
SIG_MAGIC_LEN = 4
SIG_RSA_LEN = 256
SIG_TRAILER_LEN = SIG_MAGIC_LEN + SIG_RSA_LEN

# wlf.h'deki wlf_checksum (DPC ile ayni algoritma).
def wlf_checksum(data: bytes) -> int:
    s = 0x12345678
    for b in data:
        s = ((s << 1) ^ (s >> 31) ^ b) & 0xFFFFFFFF
    return s


# Baslik duzeni: include/arctian/wlf.h wlf_header_t (packed, 228 bayt).
HDR_FMT = ("<IHHHHiiIIIIIIII%ds%ds%ds%ds%ds" % (
    WLF_NAME_LEN, WLF_TITLE_LEN, WLF_VER_LEN, WLF_AUTHOR_LEN, WLF_CATEGORY_LEN))
HDR_SIZE = struct.calcsize(HDR_FMT)          # 228

# PKCS#1 v1.5 DigestInfo on eki (SHA-256). desktop/sig.c: DI[19].
_DI = bytes.fromhex("3031300d060960864801650304020105000420")


def _cstr(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("utf-8", "replace")


class WlfError(ValueError):
    """Dogrulama hatasi; .wlf reddedildi."""


# ------------------------------------------------------------------ anahtar
_KEY_CACHE = None


def default_key_path() -> str:
    """Magaza imzalama anahtari (tools/keys/dev_rsa2048.txt)."""
    env = os.environ.get("DUCKSTORE_SIGN_KEY")
    if env:
        return env
    here = os.path.dirname(os.path.abspath(__file__))
    # store/common -> store -> (proje kok) tools/keys
    root = os.path.dirname(os.path.dirname(here))
    candidates = [
        os.path.join(root, "tools", "keys", "dev_rsa2048.txt"),
        os.path.join(root, "keys", "dev_rsa2048.txt"),
        "/opt/duckstore/keys/dev_rsa2048.txt",
        "/opt/duckstore/tools/keys/dev_rsa2048.txt",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return candidates[0]


def load_key(path=None):
    """(n, e, d) tuple dondurur. n OS'taki SIG_MOD ile ayni moduldur."""
    global _KEY_CACHE
    path = path or default_key_path()
    if _KEY_CACHE is not None and _KEY_CACHE[3] == path:
        return _KEY_CACHE[:3]
    kv = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if "=" in line:
                k, v = line.split("=", 1)
                kv[k.strip()] = v.strip()
    n = int(kv["n"], 16)
    e = int(kv.get("e", "10001"), 16)
    d = int(kv["d"], 16)
    _KEY_CACHE = (n, e, d, path)
    return n, e, d


# ------------------------------------------------------------------ imza
def _emsa_pkcs1_v15_sha256(body: bytes) -> bytes:
    """256 baytlik encode edilmis mesaj (EM). desktop/sig.c emsa ile ayni."""
    h = hashlib.sha256(body).digest()
    # EM = 00 01 FF..FF 00 DigestInfo ; toplam 256 bayt
    # (00 01 on eki 2 bayt, ayirici 00 1 bayt)
    ps_len = SIG_RSA_LEN - 2 - 1 - len(_DI) - 32   # = 202
    return b"\x00\x01" + (b"\xff" * ps_len) + b"\x00" + _DI + h


def sign_body(body: bytes, key=None) -> bytes:
    """Govdeyi imzalayip ASG1 trailer'ini dondurur (260 bayt)."""
    n, e, d = key or load_key()
    em = _emsa_pkcs1_v15_sha256(body)
    s = pow(int.from_bytes(em, "big"), d, n)
    return SIG_MAGIC + s.to_bytes(SIG_RSA_LEN, "big")


def verify_pkg(data: bytes, key=None) -> bool:
    """Verilen .wlf (trailer dahil) imzali ve gecerli mi?"""
    if not data or len(data) <= SIG_TRAILER_LEN:
        return False
    if data[-SIG_TRAILER_LEN:-SIG_RSA_LEN] != SIG_MAGIC:
        return False
    n, e, d = key or load_key()
    body = data[:-SIG_TRAILER_LEN]
    sig = data[-SIG_RSA_LEN:]
    s = int.from_bytes(sig, "big")
    if s >= n:
        return False
    em = pow(s, e, n).to_bytes(SIG_RSA_LEN, "big")
    return em == _emsa_pkcs1_v15_sha256(body)


def strip_trailer(data: bytes) -> bytes:
    """ASG1 trailer'i varsa kaldirir (yeniden imzalama icin)."""
    if len(data) > SIG_TRAILER_LEN and \
            data[-SIG_TRAILER_LEN:-SIG_RSA_LEN] == SIG_MAGIC:
        return data[:-SIG_TRAILER_LEN]
    return data


# ------------------------------------------------------------------ meta
def read_meta(body: bytes):
    """WLFM meta blogunu cozer. (sozluk, toplam_meta_bayt) dondurur.

    Blok yoksa ({}, 0). Bozuksa WlfError firlatir (OS da reddeder).
    """
    if len(body) < HDR_SIZE + WLF_META_HDR:
        return {}, 0
    magic, mlen = struct.unpack_from("<IH", body, HDR_SIZE)
    if magic != WLF_META_MAGIC:
        return {}, 0
    if HDR_SIZE + WLF_META_HDR + mlen > len(body):
        raise WlfError("meta blogu paketi aşıyor")
    raw = body[HDR_SIZE + WLF_META_HDR:HDR_SIZE + WLF_META_HDR + mlen]
    out = {}
    for line in raw.decode("utf-8", "replace").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out, WLF_META_HDR + mlen


def _name_ok(s: str, max_len: int) -> bool:
    n = len(s)
    if n < 1 or n >= max_len:
        return False
    for c in s:
        ok = (("a" <= c <= "z") or ("A" <= c <= "Z") or ("0" <= c <= "9")
              or c == "-" or c == "_")
        if not ok:
            return False
    return True


# ------------------------------------------------------------------ dogrulama
def validate(data: bytes) -> dict:
    """Ham .wlf baytlarini OS gozunden dogrular, ayristirilmis bilgi dondurur.

    OS'taki wlf_validate() ile ayni kontrolleri yapar (imza HARIC — imzayi
    sunucu yeniden uretir). Hata durumunda WlfError firlatir.
    """
    if not data:
        raise WlfError("bos dosya")
    if len(data) > WLF_MAX_PACKAGE:
        raise WlfError("paket çok büyük (en fazla %d KB)"
                       % (WLF_MAX_PACKAGE // 1024))

    body = strip_trailer(data)
    if len(body) < HDR_SIZE:
        raise WlfError("paket çok küçük (baslık yok)")

    (magic, version, kind, flags, role, win_w, win_h, load_addr,
     image_size, bss_size, entry_off, payload_off, payload_len,
     icon_len, checksum, name_b, title_b, ver_b, author_b,
     cat_b) = struct.unpack_from(HDR_FMT, body, 0)

    if magic != WLF_MAGIC:
        raise WlfError("geçersiz sihir (bu bir .wlf değil)")
    if version != WLF_VERSION:
        raise WlfError("desteklenmeyen sürüm: %d" % version)
    if kind not in (WLF_KIND_NATIVE, WLF_KIND_SCRIPT):
        raise WlfError("geçersiz tür: %d" % kind)
    if role > WLF_ROLE_RUNTIME:
        raise WlfError("geçersiz rol: %d" % role)
    # Bilinmeyen bayrak bitleri: OS bunlari yok sayar; sunucu sadelestirir.
    flags &= WLF_FLAG_KNOWN

    name = _cstr(name_b)
    if not _name_ok(name, WLF_NAME_LEN):
        raise WlfError("geçersiz uygulama adı (yalnız a-z, 0-9, -, _)")

    if payload_len == 0:
        raise WlfError("payload boş")
    if payload_off < HDR_SIZE:
        raise WlfError("payload_ofseti başlıktan küçük")
    if payload_off + payload_len > len(body):
        raise WlfError("payload paketi aşıyor")

    # Meta blogu butunlugu (bozuksa OS da reddeder).
    meta, meta_total = read_meta(body)

    icon_off = HDR_SIZE + meta_total
    if icon_off + icon_len > len(body):
        raise WlfError("ikon paketi aşıyor")
    if payload_off < icon_off + icon_len:
        raise WlfError("ikon ile payload çakışıyor")

    if kind == WLF_KIND_NATIVE:
        if load_addr < WLF_BASE:
            raise WlfError("yükleme adresi geçersiz")
        if load_addr + image_size + bss_size > WLF_BASE + WLF_MAX:
            raise WlfError("yerleşim bellek aralığını aşıyor")
        if entry_off >= image_size:
            raise WlfError("giriş noktası kod dışında")

    # Butunluk: ikon + payload uzerinde checksum.
    got = wlf_checksum(body[icon_off:icon_off + icon_len] +
                       body[payload_off:payload_off + payload_len])
    if got != checksum:
        raise WlfError("checksum uyuşmazlığı (paket bozuk)")

    return {
        "body": body,                 # imzasiz govde (yeniden imzalanacak)
        "id": name,
        "title": _cstr(title_b) or name,
        "version": _cstr(ver_b) or "1.0",
        "author": _cstr(author_b),
        "category": _cstr(cat_b) or "Genel",
        "kind": kind,
        "role": role,
        "flags": flags,
        "win_w": win_w,
        "win_h": win_h,
        "icon": body[icon_off:icon_off + icon_len],
        "icon_svg": _as_svg_text(body[icon_off:icon_off + icon_len]),
        "permissions": permissions_text(flags),
        "payload_len": payload_len,
        "meta": meta,
    }


def sign(info: dict, key=None) -> bytes:
    """validate() ciktisini imzali .wlf dosyasi olarak dondurur."""
    key = key or load_key()
    return info["body"] + sign_body(info["body"], key)


def permissions_text(flags: int) -> str:
    """Bayraklari insana okunur izin metnine cevirir (feed 'permissions')."""
    if flags == 0:
        return ""
    out = []
    if flags & WLF_FLAG_FS:
        out.append("fs")
    if flags & WLF_FLAG_NET:
        out.append("net")
    if flags & WLF_FLAG_EXEC:
        out.append("exec")
    return ",".join(out)


def icon_content_type(icon: bytes) -> str:
    s = icon.lstrip()[:5]
    if s.startswith(b"<svg") or s.startswith(b"<?xml"):
        return "image/svg+xml"
    if icon[:8] == b"\x89PNG\r\n\x1a\n":
        return "image/png"
    if icon[:3] == b"\xff\xd8\xff":
        return "image/jpeg"
    if icon[:4] == b"RIFF" and icon[8:12] == b"WEBP":
        return "image/webp"
    return "application/octet-stream"


def _as_svg_text(icon: bytes) -> str:
    """Ikoni feed'e gomulecek SVG metni olarak dondurur; SVG degilse ''."""
    if not icon:
        return ""
    if icon_content_type(icon) != "image/svg+xml":
        return ""
    txt = icon.decode("utf-8", "replace")
    low = txt.lower()
    # Guvenlik: feed'e gomulen SVG zararli script/icermemeli.
    if "<script" in low or "javascript:" in low or "<foreignobject" in low:
        return ""
    return txt


# ------------------------------------------------------------------ feed
def enc(v) -> str:
    from urllib.parse import quote
    return quote(str(v), safe="")


def app_to_feed(a: dict) -> str:
    """Tek uygulamayi DSF satirina cevirir.

    OS'taki duckstore/main.c bu alanlari okur: id, title, category, version,
    publisher, size, desc, package, icon, permissions.

    Icon: OS, `icon=` alanina GOMULU ham SVG bekler (svg_render ile çizer,
    DS_ICON_MAX=1200 bayt). Bu yuzden yalnizca SVG ikonlar feed'e gomülür ve
    1200 bayta kisitlanir. PNG/JPEG/WEBP ikonlar feed'e girmez (OS varsayilan
    ikona duser); gercek gorsel web sitesinde /icons/<id> uzerinden sunulur.
    """
    lines = [
        "@app",
        "id=" + enc(a["id"]),
        "title=" + enc(a.get("title") or a["id"]),
        "category=" + enc(a.get("category") or "Genel"),
        "version=" + enc(a.get("version") or "1.0"),
        "publisher=" + enc(a.get("publisher") or a.get("author") or "Arctian"),
        "desc=" + enc(a.get("description") or ""),
        "size=" + str(int(a.get("size", 0))),
        "package=/packages/" + a["file"],
    ]
    if a.get("permissions"):
        lines.append("permissions=" + enc(a["permissions"]))
    svg = a.get("icon_svg")
    if svg:
        lines.append("icon=" + enc(svg[:1200]))
    return "\n".join(lines) + "\n"


def feed_text(apps) -> str:
    return "#duckstore-feed 1\n" + "".join(app_to_feed(a) for a in apps)


# ------------------------------------------------------------------ magaza
def scan_packages(packages_dir: str):
    """packages/ altindaki .wlf dosyalarini tarar, dogrulanmis liste dondurur.

    Bozuk/gercersiz dosyalar listeye girmez (ama diskten silinmez).
    """
    out = []
    if not os.path.isdir(packages_dir):
        return out
    for fn in sorted(os.listdir(packages_dir)):
        if not fn.lower().endswith(".wlf"):
            continue
        path = os.path.join(packages_dir, fn)
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError:
            continue
        try:
            info = validate(data)
        except WlfError:
            continue
        info["file"] = fn
        info["size"] = len(data)
        out.append(info)
    return out


def publish(packages_dir: str, data: bytes, key=None) -> dict:
    """Ham .wlf baytlarini dogrular, yeniden imzalar ve packages/ altina yazar.

    Ayni kimlik (id) zaten yayindaysa uzerine yazar (surum guncelleme).
    Doğrulanmış bilgi + yazilan dosya adi dondurur.
    """
    info = validate(data)
    signed = sign(info, key)
    os.makedirs(packages_dir, exist_ok=True)
    fn = info["id"] + ".wlf"
    tmp = os.path.join(packages_dir, fn + ".part")
    with open(tmp, "wb") as f:
        f.write(signed)
    os.replace(tmp, os.path.join(packages_dir, fn))
    info["file"] = fn
    info["size"] = len(signed)
    return info


if __name__ == "__main__":
    # Basit denetim: python3 wlflib.py <dosya.wlf>
    if len(sys.argv) < 2:
        print("kullanim: wlflib.py <dosya.wlf>")
        raise SystemExit(2)
    with open(sys.argv[1], "rb") as f:
        blob = f.read()
    info = validate(blob)
    print("OK  id=%s title=%s ver=%s tur=%s ikon=%d bayt payload=%d" % (
        info["id"], info["title"], info["version"],
        "native" if info["kind"] == 0 else "bytecode",
        len(info["icon"]), info["payload_len"]))
    print("imza gecerli: %s" % verify_pkg(blob))
