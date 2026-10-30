#!/bin/sh
# build.sh — Arctian .wlf uygulama derleyicisi (Linux / macOS / WSL)
#
# Gereksinimler: gcc (veya clang), ld, objcopy, nm, readelf, python3
#
# Kullanim:
#   sh build.sh                 # bu klasordeki main.c
#   sh build.sh /proje/yolu     # verilen klasor
#   sh build.sh --check         # araclari denetle, derleme yapma
#
# Cikti: <ad>.wlf
#
# NOT: Bu betik POSIX sh ile uyumludur (bash gerekmez).
set -e

SDK=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# ---------------------------------------------------------------- araclar
CC=${CC:-}
if [ -z "$CC" ]; then
    for c in gcc cc clang; do
        if command -v "$c" >/dev/null 2>&1; then CC="$c"; break; fi
    done
fi
LD=${LD:-ld}
OBJCOPY=${OBJCOPY:-objcopy}
NM=${NM:-nm}
READELF=${READELF:-readelf}
PY=${PYTHON:-python3}

missing=""
[ -n "$CC" ] || missing="$missing gcc"
for t in "$LD" "$OBJCOPY" "$NM" "$READELF" "$PY"; do
    command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
done

if [ -n "$missing" ]; then
    echo "[hata] eksik araclar:$missing" >&2
    echo "       Linux (Debian/Ubuntu): sudo apt install build-essential binutils python3" >&2
    echo "       macOS               : xcode-select --install" >&2
    exit 1
fi

if [ "${1:-}" = "--check" ]; then
    echo "libWolf SDK hazir"
    echo "  SDK  : $SDK"
    echo "  CC   : $($CC --version 2>/dev/null | head -1)"
    echo "  ld   : $($LD --version 2>/dev/null | head -1)"
    echo "  py   : $($PY --version 2>&1)"
    exit 0
fi

PROJ=${1:-$(pwd)}
cd "$PROJ" || { echo "[hata] klasor yok: $PROJ" >&2; exit 1; }

if [ ! -f main.c ]; then
    echo "[hata] main.c yok: $PROJ" >&2
    echo "       Ornek: cd \"$SDK/examples/merhaba\" && sh build.sh" >&2
    exit 1
fi
if [ ! -f wolf.json ]; then
    echo "[hata] wolf.json yok: $PROJ" >&2
    exit 1
fi

CFLAGS="-std=gnu11 -m64 -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
 -O2 -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables \
 -mno-red-zone -fno-pie -Wall -Wextra"
INC="-I$SDK/include"

echo "[1/4] derleniyor (C, freestanding)..."
$CC $CFLAGS $INC -c -o app.o main.c
$CC $CFLAGS $INC -c -o libwolf.o "$SDK/src/libwolf.c"

echo "[2/4] linkleniyor..."
$LD -m elf_x86_64 -nostdlib -T "$SDK/link.ld" -o app.elf app.o libwolf.o

echo "[3/4] binary cikariliyor..."
$OBJCOPY -O binary app.elf app.bin

echo "[4/4] .wlf paketleniyor..."
NAME=$($PY -c "import json;print(json.load(open('wolf.json'))['name'])")

# Giris islevi: wolf_main ya da main. entry_off = adres - 0x03000000
ENTRY_SYM=$($NM app.elf | grep -E ' T (wolf_main|main)$' | head -1 | cut -d' ' -f1)
if [ -z "$ENTRY_SYM" ]; then
    echo "[hata] wolf_main/main sembolu bulunamadi" >&2
    exit 1
fi
ENTRY_OFF=$($PY -c "print(int('$ENTRY_SYM',16) - 0x03000000)")

BSS=$($PY - "$READELF" <<'PY'
import subprocess, sys
out = subprocess.run([sys.argv[1], "-SW", "app.elf"],
                     capture_output=True, text=True).stdout
bss = 0
for line in out.splitlines():
    if " .bss " in line:
        try:
            bss = int(line.split("]")[1].split()[4], 16)
        except (IndexError, ValueError):
            pass
print(bss)
PY
)

$PY "$SDK/wlfpack.py" --bin app.bin --json wolf.json \
    --entry "$ENTRY_OFF" --bss "$BSS" -o "$NAME.wlf"

echo
echo "Hazir: $PROJ/$NAME.wlf"
echo "Arctian'da:"
echo "  wf install $NAME.wlf"
echo "  wf run $NAME"
