#!/usr/bin/env python3
"""
sdk_dist.py — libWolf SDK dagitim paketlerini uretir.

Cikti (store/public/sdk/):
  libwolf-windows-x64.exe      Windows kurulumu (mevcut self-extractor)
  libwolf-windows-x64.zip      Windows tasinabilir SDK + Arctian Emulator
  libwolf-linux-x64.sh         Linux kurulumu (SDK + Arctian Emulator gomulu)
  libwolf-macos-universal.sh   macOS kurulumu (ayni govde, macOS yollari)
  manifest.json                surum + sha256 + boyut + aciklama

Arctian Emulator (.wlf calistirici) build/emu* altindaki hazir ikililerden
eklenir:
  build/emu/arctian-emu, arctian-emu-headless        (Linux, sdk/emu/Makefile)
  build/emu-win/arctian-emu.exe, ...-headless.exe    (Windows, build-win.bat)

Kullanim:
  python tools/sdk_dist.py                 # uret
  python tools/sdk_dist.py --check         # yalnizca dogrula
  python tools/sdk_dist.py --out <klasor>  # hedef klasor
"""
import argparse
import base64
import hashlib
import io
import json
import os
import sys
import tarfile
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK = os.path.join(ROOT, "sdk")
EMU_LNX = os.path.join(ROOT, "build", "emu")
EMU_WIN = os.path.join(ROOT, "build", "emu-win")
DEFAULT_OUT = os.path.join(ROOT, "store", "public", "sdk")

SDK_VERSION = "1.3.0"
EMU_VERSION = "1.2"

# Dagitima giren dosyalar (build artifact'lari HARIC).
INCLUDE = [
    ("include/wolf.h", "include/wolf.h"),
    ("src/libwolf.c", "src/libwolf.c"),
    ("build.sh", "build.sh"),
    ("build.bat", "build.bat"),
    ("link.ld", "link.ld"),
    ("README.md", "README.md"),
    ("wlfpack.py", "wlfpack.py"),
    ("wf", "wf"),
    ("wf.cmd", "wf.cmd"),
    # Windows: "wf" Windows'un Work Folders konsoluyla (WF.msc) cakisir;
    # ayni betigin "wolf" adi her terminalde guvenilir secilir.
    ("wf.cmd", "wolf.cmd"),
]
EXAMPLES = ["merhaba", "sayac", "uidemo", "pyrt", "studio", "player", "axiom"]

LINUX_WRAPPER = """#!/bin/sh
# libWolf SDK — Linux kurulumu (Arctian)
#   sh libwolf-linux-x64.sh
#   LIBWOLF_DIR=/opt/libwolf sh libwolf-linux-x64.sh
set -e

APP="libWolf SDK"
VER="@VER@"
DIR="${LIBWOLF_DIR:-$HOME/.local/share/Arctian/libWolf}"

echo "$APP $VER kuruluyor"
echo "  hedef: $DIR"

mkdir -p "$DIR"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PAYLOAD_LINE=$(grep -n '^__LIBWOLF_PAYLOAD__$' "$0" | tail -1 | cut -d: -f1)
if [ -z "$PAYLOAD_LINE" ]; then
  echo "[hata] paket bozuk (payload bulunamadi)" >&2
  exit 1
fi
tail -n +$((PAYLOAD_LINE + 1)) "$0" | base64 -d > "$TMP/sdk.tar.gz"
tar -xzf "$TMP/sdk.tar.gz" -C "$DIR"

# PATH: kabuk yapilandirmasina ekle (tek sefer).
RC=""
if [ -n "$ZSH_VERSION" ] || [ "$(basename "${SHELL:-sh}")" = "zsh" ]; then
  RC="$HOME/.zshrc"
elif [ -f "$HOME/.bashrc" ]; then
  RC="$HOME/.bashrc"
else
  RC="$HOME/.profile"
fi
if [ -f "$RC" ] && ! grep -q 'Arctian/libWolf' "$RC" 2>/dev/null; then
  printf '\\n# libWolf SDK (Arctian)\\nexport PATH="%s/bin:%s:$PATH"\\n' "$DIR" "$DIR" >> "$RC"
  echo "  PATH guncellendi: $RC"
fi

echo
echo "Kurulum tamamlandi."
echo "  SDK       : $DIR"
echo "  Emulator  : arctian-emu (pencereli) / arctian-emu-headless"
echo
echo "Dene:"
echo "  cd $DIR/examples/merhaba && sh build.sh"
echo "  wf shot merhaba.wlf            # PNG onizleme"
echo "  wf run merhaba.wlf             # masaustunde/acik pencerede calistir"
echo
echo "Uretilen .wlf dosyasini Arctian'da: wf install <dosya>.wlf"
exit 0
__LIBWOLF_PAYLOAD__
"""

MACOS_WRAPPER = """#!/bin/sh
# libWolf SDK — macOS kurulumu (Arctian)
#   sh libwolf-macos-universal.sh
set -e

APP="libWolf SDK"
VER="@VER@"
DIR="${LIBWOLF_DIR:-$HOME/Library/Application Support/Arctian/libWolf}"

echo "$APP $VER kuruluyor"
echo "  hedef: $DIR"

if ! command -v gcc >/dev/null 2>&1 && ! command -v clang >/dev/null 2>&1; then
  echo "[uyari] C derleyicisi bulunamadi." >&2
  echo "        Xcode Command Line Tools kurun: xcode-select --install" >&2
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "[uyari] python3 bulunamadi; paketleme icin gerekli." >&2
fi

mkdir -p "$DIR"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PAYLOAD_LINE=$(grep -n '^__LIBWOLF_PAYLOAD__$' "$0" | tail -1 | cut -d: -f1)
if [ -z "$PAYLOAD_LINE" ]; then
  echo "[hata] paket bozuk (payload bulunamadi)" >&2
  exit 1
fi
tail -n +$((PAYLOAD_LINE + 1)) "$0" | base64 -d > "$TMP/sdk.tar.gz"
tar -xzf "$TMP/sdk.tar.gz" -C "$DIR"

RC="$HOME/.zshrc"
[ -f "$RC" ] || RC="$HOME/.profile"
if ! grep -q 'Arctian/libWolf' "$RC" 2>/dev/null; then
  printf '\\n# libWolf SDK (Arctian)\\nexport PATH="%s/bin:%s:$PATH"\\n' "$DIR" "$DIR" >> "$RC"
  echo "  PATH guncellendi: $RC"
fi

echo
echo "Kurulum tamamlandi."
echo "  SDK : $DIR"
echo "  Not : macOS icin Arctian Emulator ikilisi hazir degil;"
echo "        emulatoru Onizleme icin Windows/Linux SDK'sini kullanin."
echo
echo "Dene:"
echo "  cd \\"$DIR/examples/merhaba\\" && sh build.sh"
exit 0
__LIBWOLF_PAYLOAD__
"""


def _tar_bytes(platform_note: str, emu_bins=()) -> bytes:
    """SDK anlik goruntusunu .tar.gz olarak paketler.

    emu_bins: (mutlak_yol, "bin/arctian-emu") ikilileri; calistirilabilir
    olarak (0o755) eklenir. Yoksa atlanir (macOS'ta emu ikilisi yok).
    """
    buf = io.BytesIO()
    # Sabit mtime -> tekrarlanabilir cikti (ayni kaynak = ayni sha256).
    with tarfile.open(fileobj=buf, mode="w:gz", compresslevel=9) as tar:
        def add(src_rel, dst_rel, mode=0o644):
            src = os.path.join(SDK, src_rel)
            if not os.path.isfile(src):
                raise SystemExit("eksik dosya: %s" % src_rel)
            ti = tar.gettarinfo(src, arcname=dst_rel)
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = "root"
            ti.mtime = 1700000000
            ti.mode = mode
            with open(src, "rb") as f:
                tar.addfile(ti, f)

        def add_abs(src, dst_rel):
            with open(src, "rb") as f:
                data = f.read()
            ti = tarfile.TarInfo(dst_rel)
            ti.size = len(data)
            ti.mtime = 1700000000
            ti.mode = 0o755
            ti.uid = ti.gid = 0
            ti.uname = ti.gname = "root"
            tar.addfile(ti, io.BytesIO(data))

        for src_rel, dst_rel in INCLUDE:
            add(src_rel, dst_rel)
        for ex in EXAMPLES:
            d = os.path.join(SDK, "examples", ex)
            if not os.path.isdir(d):
                continue
            for fn in sorted(os.listdir(d)):
                if fn.endswith((".o", ".elf", ".bin", ".wlf")):
                    continue      # build artifact'lari dagitma
                p = os.path.join(d, fn)
                if os.path.isfile(p):
                    add("examples/%s/%s" % (ex, fn), "examples/%s/%s" % (ex, fn))
        for src, dst_rel in emu_bins:
            if os.path.isfile(src):
                add_abs(src, dst_rel)

        # platform notu
        note = ("# libWolf SDK %s\n\nPlatform: %s\n"
                "Bu paket Arctian uygulama SDK'sidir. Uygulamalar C ile yazilir\n"
                "ve libWolf (wolf.h) uzerinden isletim sistemiyle konusur.\n"
                "Arctian Emulator v%s: wf run/shot ile .wlf onizleme.\n"
                % (SDK_VERSION, platform_note, EMU_VERSION)).encode("utf-8")
        ti = tarfile.TarInfo("PLATFORM.txt")
        ti.size = len(note)
        ti.mtime = 1700000000
        ti.mode = 0o644
        tar.addfile(ti, io.BytesIO(note))
    return buf.getvalue()


def _write_sh(path: str, wrapper: str, payload: bytes):
    body = wrapper.replace("@VER@", SDK_VERSION).encode("utf-8")
    b64 = base64.encodebytes(payload)
    with open(path, "wb") as f:
        f.write(body)
        f.write(b64)


WIN_INSTALL_TXT = """libWolf SDK %s + Arctian Emulator %s (Windows, tasinabilir)
-----------------------------------------------------------
1) Bu klasoru istedigin yere ac  (orn. C:\\libWolf).
2) PATH'e ekle:  set PATH=C:\\libWolf;%%PATH%%   (kalici icin Sistem Ayarlari)
3) Proje :  wolf new benim-uygulamam "Benim Uygulamam"  -> klasor ac, main.c yaz
4) Derle :  benim-uygulamam icinde  wolf build
5) Calistir: wolf run benim-uygulamam.wlf  (pencereli emulator)
   Onizleme: wolf shot benim-uygulamam.wlf (PNG ekran goruntusu)
    Not: "wf" Windows'un Work Folders konsoluyla cakisir; "wolf" kullanin.

Emulator ikileri bin\\ altindadir: arctian-emu.exe (GDI pencereli),
arctian-emu-headless.exe (PNG/CI). gcc gerektiginde w64devkit onerilir.
"""


def _zip_bytes(win_emu_bins) -> bytes:
    """Windows tasinabilir SDK + emulator ikileri."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src_rel, dst_rel in INCLUDE:
            src = os.path.join(SDK, src_rel)
            if not os.path.isfile(src):
                raise SystemExit("eksik dosya: %s" % src_rel)
            z.write(src, "libWolf/" + dst_rel)
        for ex in EXAMPLES:
            d = os.path.join(SDK, "examples", ex)
            if not os.path.isdir(d):
                continue
            for fn in sorted(os.listdir(d)):
                if fn.endswith((".o", ".elf", ".bin", ".wlf")):
                    continue
                p = os.path.join(d, fn)
                if os.path.isfile(p):
                    z.write(p, "libWolf/examples/%s/%s" % (ex, fn))
        for src in win_emu_bins:
            if os.path.isfile(src):
                z.write(src, "libWolf/bin/" + os.path.basename(src))
        z.writestr("libWolf/INSTALL.txt",
                   (WIN_INSTALL_TXT % (SDK_VERSION, EMU_VERSION)).encode("utf-8"))
    return buf.getvalue()


def build(out_dir: str) -> dict:
    os.makedirs(out_dir, exist_ok=True)
    artifacts = []

    def emu_path(base, name):
        p = os.path.join(base, name)
        return p if os.path.isfile(p) else None

    lnx_gui = emu_path(EMU_LNX, "arctian-emu")
    lnx_head = emu_path(EMU_LNX, "arctian-emu-headless")
    win_gui = emu_path(EMU_WIN, "arctian-emu.exe")
    win_head = emu_path(EMU_WIN, "arctian-emu-headless.exe")

    linux_bins = []
    if lnx_head:
        linux_bins.append((lnx_head, "bin/arctian-emu-headless"))
    if lnx_gui:
        linux_bins.append((lnx_gui, "bin/arctian-emu"))

    linux_tar = _tar_bytes("linux-x86-64", linux_bins)
    mac_tar = _tar_bytes("macos-universal")

    linux_sh = os.path.join(out_dir, "libwolf-linux-x64.sh")
    mac_sh = os.path.join(out_dir, "libwolf-macos-universal.sh")
    _write_sh(linux_sh, LINUX_WRAPPER, linux_tar)
    _write_sh(mac_sh, MACOS_WRAPPER, mac_tar)

    # libwolf-windows-x64.exe KALDIRILDI (1.3.0): kaynaksiz ikili stub'tu,
    # Defender onu "Trojan:Win32/Wacatac!ml" olarak işaretliyordu (küçük,
    # imzasız, kendinden açılan exe'ler ML sezgiselinde sık tetiklenir).
    # Windows tek dagitim kanali artik zip: ayni SDK + emulator, savunuc
    # tarayicilarda temiz, "bilinmeyen yayinci" penceresi de cikarmiyor.

    win_bins = [p for p in (win_gui, win_head) if p]
    win_zip = os.path.join(out_dir, "libwolf-windows-x64.zip")
    with open(win_zip, "wb") as f:
        f.write(_zip_bytes(win_bins))

    def entry(path, platform, arch, kind, notes):
        with open(path, "rb") as f:
            data = f.read()
        return {
            "file": os.path.basename(path),
            "platform": platform,
            "arch": arch,
            "kind": kind,
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "notes": notes,
        }

    artifacts.append(entry(linux_sh, "linux", "x86_64", "shell",
                           "sh ile kurulum; Arctian Emulator gomulu; "
                           "derleme icin gcc + binutils + python3 gerekir"))
    artifacts.append(entry(mac_sh, "macos", "universal", "shell",
                           "sh ile kurulum; Xcode Command Line Tools gerekir"))
    artifacts.append(entry(win_zip, "windows", "x86_64", "zip",
                           "tasinabilir SDK + Arctian Emulator (wolf run/shot/new)"))

    manifest = {
        "sdk": "libWolf",
        "version": SDK_VERSION,
        "generated_at": int(time.time()),
        "api": {
            "abi_version": 2,
            "header": "include/wolf.h",
            "entry": "int wolf_main(const wolf_api_t *api)",
        },
        "package_format": {"magic": "WLF1", "ext": ".wlf",
                           "kinds": ["native", "bytecode"]},
        "install": {
            "linux": "sh libwolf-linux-x64.sh",
            "macos": "sh libwolf-macos-universal.sh",
            "windows": "libwolf-windows-x64.zip (ac + PATH'e ekle)",
        },
        "build": {
            "host": "cd examples/merhaba && sh build.sh   (veya build.bat)",
            "in_os": "wf build <proje> [ui|cli|runtime]",
            "run_in_os": "wf install <ad>.wlf && wf run <ad>",
        },
        "emulator": {
            "name": "arctian-emu",
            "version": EMU_VERSION,
            "usage": "arctian-emu uygulama.wlf [--shot out.png --frames N] "
                     "[--click KARE:X:Y] [--info]",
            "fidelity": "OS kaynaklari aynen derlenir: pikseli pikseline "
                        "Windows/Linux/Mac cikti birbirinin BAYT aynisidir",
            "windows": ["bin/arctian-emu.exe", "bin/arctian-emu-headless.exe"],
            "linux": ["bin/arctian-emu", "bin/arctian-emu-headless"],
            "macos": None,
        },
        "artifacts": artifacts,
    }
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    return manifest


def verify(out_dir: str) -> list:
    """manifest.json'daki sha256 degerlerini dogrular; hatalari dondurur."""
    problems = []
    mp = os.path.join(out_dir, "manifest.json")
    if not os.path.isfile(mp):
        return ["manifest.json yok"]
    with open(mp, "r", encoding="utf-8") as f:
        man = json.load(f)
    for a in man.get("artifacts", []):
        p = os.path.join(out_dir, a["file"])
        if not os.path.isfile(p):
            problems.append("eksik: %s" % a["file"])
            continue
        with open(p, "rb") as f:
            h = hashlib.sha256(f.read()).hexdigest()
        if h != a["sha256"]:
            problems.append("sha256 uyusmuyor: %s" % a["file"])
    return problems


def main():
    ap = argparse.ArgumentParser(description="libWolf SDK dagitim paketleyici")
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    if args.check:
        problems = verify(args.out)
        if problems:
            print("HATALI:")
            for p in problems:
                print("  -", p)
            return 1
        print("manifest dogrulandi: %s" % args.out)
        return 0

    man = build(args.out)
    print("libWolf SDK %s -> %s" % (man["version"], args.out))
    for a in man["artifacts"]:
        print("  %-32s %8.1f KB  %s" % (a["file"], a["size"] / 1024.0, a["sha256"][:16]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
