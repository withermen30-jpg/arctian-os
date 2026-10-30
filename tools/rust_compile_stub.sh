#!/bin/bash
# Rust uygulama nesnesini derler. Uygulama no_std oldugu icin core/alloc/
# compiler_builtins kutuphanelerini de iceren bir staticlib uretilir, ardindan
# tek bir yeniden konumlanabilir nesneye (ld -r) indirgenir. Boylece make ile
# desktop/apps'e konan Rust uygulamalari da linklenebilir.
#   $1 = main.rs, $2 = cikis .o
set -e
SRC="$1"
OUT="$2"
RUSTC="${RUSTC:-rustc}"
TMP="$OUT.tmp.a"

"$RUSTC" "$SRC" --target x86_64-unknown-none --edition 2021 -O \
    --crate-type staticlib -o "$TMP" \
    -C panic=abort -C relocation-model=static -C code-model=kernel \
    -C target-feature=+sse2,-soft-float

rm -f "$OUT.rust.o" "$OUT.stub.s" "$OUT.stub.o"
ld -r -o "$OUT" --whole-archive "$TMP" --no-whole-archive
rm -f "$TMP"
