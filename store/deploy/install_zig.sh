#!/bin/bash
# DuckStore sunucusuna Zig derleyicisini kurar (toolchain/zig/zig).
set -e
apt-get install -y -qq xz-utils >/dev/null 2>&1 || true
mkdir -p /opt/duckstore/toolchain/zig
cd /opt/duckstore/toolchain/zig
if [ ! -x zig-linux-x86_64-0.13.0/zig ]; then
    echo "Zig indiriliyor..."
    python3 -c "import urllib.request;urllib.request.urlretrieve('https://ziglang.org/download/0.13.0/zig-linux-x86_64-0.13.0.tar.xz','/tmp/zig.tar.xz')"
    tar -xJf /tmp/zig.tar.xz
    rm -f /tmp/zig.tar.xz
fi
ln -sf zig-linux-x86_64-0.13.0/zig /opt/duckstore/toolchain/zig/zig
chown -R duckstore:duckstore /opt/duckstore/toolchain/zig
echo -n "zig version: "
runuser -u duckstore -- /opt/duckstore/toolchain/zig/zig version
