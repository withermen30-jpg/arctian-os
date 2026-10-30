#!/bin/bash
# Arctian kurulum testi: ISO + sanal hedef disk ile QEMU baslatir.
# WSL icinde calistirin:  bash tools/test_install.sh
set -e
cd "$(dirname "$0")/.."
IMG=build/target.img

[ -f build/arctian.iso ] || { echo "Once derleyin: make"; exit 1; }
[ -f "$IMG" ] || { echo "Hedef disk olusturuluyor: $IMG (64MB)"; truncate -s 64M "$IMG"; }

echo "================================================"
echo " Kurulum testi basliyor"
echo " - Kurulum ekraninda hedef diski secin (1,2,... tuslayin)"
echo " - Kurulum bitince makine otomatik yeniden baslar"
echo " - Sonra diskten acmak icin: bash tools/run_installed.sh"
echo "================================================"
qemu-system-x86_64 -cdrom build/arctian.iso \
  -drive file="$IMG",format=raw,index=0,media=disk \
  -m 512M -vga std
