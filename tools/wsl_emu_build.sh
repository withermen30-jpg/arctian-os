#!/usr/bin/env bash
# WSL tarafinda emulator derlemesi + OS derleme kontrolu.
# Kural: /mnt/c altinda ASLA make calistirilmaz; kaynak ext4'e kopyalanir.
#
# Kullanim (WSL icinden ya da Windows'tan: wsl -d Debian -- bash tools/wsl_emu_build.sh):
#   tools/wsl_emu_build.sh            # emulator (headless+x11) + wlf_host.o kontrolu
#   tools/wsl_emu_build.sh os         # + tam masaustu hedefi (make desktop)
set -euo pipefail

SRC="${SRC:-/mnt/c/Projects/cookie/yedek}"
DST="${DST:-$HOME/emu-build}"

mkdir -p "$DST"
# build/ klasoru DST'de kalsin (artim derleme); kaynaklar senkron
rsync -a --delete --exclude 'build/' --exclude '.git/' "$SRC/" "$DST/"

echo "== emulator (Linux) =="
make -C "$DST/sdk/emu" -j"$(nproc)"

echo "== OS derleme kontrolu (wlf_host.o) =="
make -C "$DST" build/desktop/wlf_host.o -j"$(nproc)"

if [ "${1:-}" = "os" ]; then
    echo "== tam masaustu =="
    make -C "$DST" desktop -j"$(nproc)"
fi

echo "== IKILER =="
ls -la "$DST/build/emu/" | sed -n '1,12p'
