#!/bin/bash
# Kurulu diski (target.img) tek basina QEMU'da acar (kurulum atlanmali).
# WSL icinde calistirin:  bash tools/run_installed.sh
set -e
cd "$(dirname "$0")/.."
IMG=build/target.img
[ -f "$IMG" ] || { echo "Once kurulum yapin: bash tools/test_install.sh"; exit 1; }
qemu-system-x86_64 -drive file="$IMG",format=raw,index=0,media=disk -m 512M -vga std
