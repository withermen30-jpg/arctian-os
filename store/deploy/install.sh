#!/bin/bash
# DuckStore sunucu kurulumu (Ubuntu/Debian, root).
# /root/dsdeploy icine acilmis paketleri /opt/duckstore altina kurar ve
# systemd servislerini 7/24 calisacak sekilde etkinlestirir.
set -e

SRC="$(cd "$(dirname "$0")/.." && pwd)"

export DEBIAN_FRONTEND=noninteractive
echo "[1/6] Paketler kuruluyor..."
apt-get update -qq
apt-get install -y -qq build-essential python3 lld >/dev/null

echo "[2/6] duckstore kullanicisi..."
if ! id duckstore >/dev/null 2>&1; then
    useradd --system --home /opt/duckstore --shell /usr/sbin/nologin duckstore
fi
# indirme istatistikleri feed loglarini (systemd journal) okuyabilsin
usermod -aG systemd-journal duckstore

echo "[3/6] Dosyalar /opt/duckstore icine kopyalaniyor..."
mkdir -p /opt/duckstore/toolchain /opt/duckstore/server /opt/duckstore/web \
         /opt/duckstore/common /opt/duckstore/packages /opt/duckstore/data \
         /opt/duckstore/logs
cp -a "$SRC/toolchain/." /opt/duckstore/toolchain/ 2>/dev/null || true
cp -a "$SRC/server/duckstore_server.py" /opt/duckstore/server/
cp -a "$SRC/common/." /opt/duckstore/common/
cp -a "$SRC/web/." /opt/duckstore/web/
if [ -d "$SRC/packages" ]; then cp -a "$SRC/packages/." /opt/duckstore/packages/ 2>/dev/null || true; fi

# magaza imzalama anahtari (OS'a gomulu acik anahtarla ayni modul)
mkdir -p /opt/duckstore/keys
if [ -f "$SRC/../tools/keys/dev_rsa2048.txt" ]; then
    cp -a "$SRC/../tools/keys/dev_rsa2048.txt" /opt/duckstore/keys/
fi

echo "[4/6] systemd birimleri..."
cp -a "$SRC/deploy/duckstore-web.service" /etc/systemd/system/
cp -a "$SRC/deploy/duckstore-feed.service" /etc/systemd/system/
cp -a "$SRC/deploy/duckstore-market.service" /etc/systemd/system/
chown -R duckstore:duckstore /opt/duckstore
chmod -R u+rwX,go+rX /opt/duckstore
chmod 600 /opt/duckstore/keys/* 2>/dev/null || true
chown duckstore:duckstore /opt/duckstore/keys/* 2>/dev/null || true
systemctl daemon-reload

echo "[5/6] Servisler baslatiliyor..."
systemctl enable --now duckstore-feed.service
systemctl enable --now duckstore-web.service
systemctl enable --now duckstore-market.service
sleep 2

echo "[6/6] Durum:"
systemctl is-active duckstore-feed.service || true
systemctl is-active duckstore-web.service || true
systemctl is-active duckstore-market.service || true
ss -tlnp 2>/dev/null | grep -E ':129|:8090|:47584' || true
echo
echo "Feed  : http://<IP>:8090/v1/feed   (OS bu adrese baglanir)"
echo "Panel : http://<IP>:129/           (ilk giriste sifre belirleyin)"
echo "Market: https://market.kerem.site  (Google ile giris; tunnel -> :47584)"
