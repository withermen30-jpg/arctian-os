#!/bin/bash
# 0.7: sdk/examples/* altindaki ornek .wlf uygulamalarini derler.
# Cikti: build/wlf/<ad>.wlf  (QEMU testinde HTTP ile servis edilir)
#
#   bash tools/build_wlf_examples.sh
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/wlf"
mkdir -p "$OUT"

for d in "$ROOT"/sdk/examples/*/; do
    [ -f "$d/main.c" ] || continue
    [ -f "$d/wolf.json" ] || continue
    name=$(python3 -c "import json,sys;print(json.load(open(sys.argv[1]))['name'])" "$d/wolf.json")
    bash "$ROOT/sdk/build.sh" "$d" >/dev/null
    cp -f "$d/$name.wlf" "$OUT/$name.wlf"
    echo "  $name.wlf"
done

echo "Hazir: $OUT"
