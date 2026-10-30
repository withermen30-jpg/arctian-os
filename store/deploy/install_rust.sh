#!/bin/bash
# DuckStore sunucusuna Rust (rustup, x86_64-unknown-none) kurar.
set -e
export RUSTUP_HOME=/opt/rust/rustup
export CARGO_HOME=/opt/rust/cargo
apt-get install -y -qq curl ca-certificates >/dev/null 2>&1 || true
if [ ! -x /opt/rust/cargo/bin/rustup ]; then
    echo "rustup indiriliyor..."
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs -o /tmp/rustup-init.sh
    sh /tmp/rustup-init.sh -y --profile minimal --default-toolchain stable --no-modify-path >/dev/null
fi
/opt/rust/cargo/bin/rustup target add x86_64-unknown-none >/dev/null 2>&1 || true
TC=$(ls -d /opt/rust/rustup/toolchains/stable-*/ | head -n1)
ln -sf "${TC}bin/rustc" /usr/local/bin/rustc
chmod -R a+rX /opt/rust
echo -n "rustc: "
/usr/local/bin/rustc --version
/opt/rust/cargo/bin/rustup target list --installed 2>/dev/null | grep x86_64-unknown-none && echo TARGET_OK
