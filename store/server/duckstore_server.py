#!/usr/bin/env python3
"""
DuckStore feed sunucusu (Arctian uygulama magazasi) — 0.7 / .wlf

Bagimlilik: yalnizca Python 3 standart kutuphanesi + common/wlflib.

Yaptigi is:
  - packages/*.wlf dosyalarini tarar, .wlf basligini + meta verisini ayristirir
    (bicim ve butunluk wlflib.validate ile OS gozunden dogrulanir)
  - /v1/feed, /v1/search uclarinda satir tabanli DSF feed'ini uretir
  - /packages/<id>.wlf uzerinden paketleri, /icons/<id> uzerinden ikonlari sunar
  - /v1/update: imzali surum manifesti (imzasiz manifest kabul edilmez)

Bu sunucu OS ile arasindaki ssozlesmenin sahibidir; OS `http://<IP>:8090`
adresine baglanir (kendi agi icinde, domain degil).

Calistirma:
    python3 duckstore_server.py --host 0.0.0.0 --port 8090 --packages ./packages
"""
import argparse
import os
import sys
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

_HERE = os.path.dirname(os.path.abspath(__file__))
_COMMON = os.path.join(os.path.dirname(_HERE), "common")
if _COMMON not in sys.path:
    sys.path.insert(0, _COMMON)
import wlflib  # noqa: E402


def update_manifest(apps, version, notes, packages_dir):
    """Arctian guncelleme manifesti (satir tabanli, imzali).

    Her modul icin gercek boyut + dosyanin tamami uzerinde CRC32 (IEEE)
    degerini bildirir; istemci indirdikten sonra bunlari dogrular.
    Manifestin govdesi (sig= satiri HARIC) magaza anahtariyla imzalanir;
    OS imzasiz manifesti kabul etmez.
    """
    body = ["#arctian-update 1\n",
            "version=" + wlflib.enc(version) + "\n",
            "notes=" + wlflib.enc(notes) + "\n",
            "released=%d\n" % int(time.time())]
    for a in apps:
        full = os.path.join(packages_dir, a["file"])
        try:
            with open(full, "rb") as f:
                data = f.read()
        except OSError:
            continue
        crc = zlib.crc32(data) & 0xFFFFFFFF
        body.append("@file\n")
        body.append("id=" + wlflib.enc(a["id"]) + "\n")
        body.append("version=" + wlflib.enc(a["version"]) + "\n")
        body.append("size=%d\n" % len(data))
        body.append("crc=%08x\n" % crc)
        body.append("package=/packages/" + a["file"] + "\n")
    text = "".join(body)
    # imza: sig= satiri govdenin SONUNA eklenir; OS o satiri cikarip dogrular.
    digest_hex = _sign_manifest(text)
    return text + "sig=" + digest_hex + "\n"


def _sign_manifest(text):
    """Manifest govdesi uzerinde RSA/SHA-256 imzasi (hex, 512 karakter)."""
    import hashlib
    n, e, d = wlflib.load_key()
    body = text.encode("utf-8")
    h = hashlib.sha256(body).digest()
    di = bytes.fromhex("3031300d060960864801650304020105000420")
    ps_len = wlflib.SIG_RSA_LEN - 2 - 1 - len(di) - 32
    em = b"\x00\x01" + (b"\xff" * ps_len) + b"\x00" + di + h
    s = pow(int.from_bytes(em, "big"), d, n)
    return s.to_bytes(wlflib.SIG_RSA_LEN, "big").hex()


def matches(a, q):
    q = q.lower()
    for field in ("title", "author", "category", "id"):
        if q in str(a.get(field, "")).lower():
            return True
    if q in str(a.get("description", "")).lower():
        return True
    return False


class Handler(BaseHTTPRequestHandler):
    server_version = "DuckStore/2.0"
    packages_dir = "packages"
    wallpapers_dir = "wallpapers"
    version_file = ""
    version = "0.7.0"
    cache = []
    cache_mtime = None

    def _apps(self):
        try:
            mt = max((os.path.getmtime(os.path.join(self.packages_dir, f))
                      for f in os.listdir(self.packages_dir)
                      if f.endswith(".wlf")), default=0)
        except OSError:
            mt = 0
        if mt != self.cache_mtime:
            self.cache = wlflib.scan_packages(self.packages_dir)
            self.cache_mtime = mt
        return self.cache

    def _send(self, code, body, ctype="text/plain; charset=utf-8"):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        u = urlparse(self.path)
        path = u.path
        qs = parse_qs(u.query)
        apps = self._apps()

        if path == "/health" or path == "/":
            self._send(200, "duckstore ok\n")
            return

        if path == "/v1/feed":
            self._send(200, wlflib.feed_text(apps))
            return

        if path == "/v1/search":
            q = (qs.get("q", [""])[0] or "").strip()
            res = [a for a in apps if q and matches(a, q)]
            self._send(200, wlflib.feed_text(res))
            return

        if path == "/v1/update":
            ver = self.version
            vf = getattr(self, "version_file", "")
            if vf and os.path.isfile(vf):
                try:
                    with open(vf, "r", encoding="utf-8") as f:
                        ver = f.read().strip() or ver
                except OSError:
                    pass
            notes = "Arctian sistem ve modül güncellemeleri"
            self._send(200, update_manifest(apps, ver, notes,
                                            self.packages_dir))
            return

        if path.startswith("/v1/app/"):
            app_id = unquote(path[len("/v1/app/"):])
            for a in apps:
                if a["id"] == app_id:
                    self._send(200, "#duckstore-feed 1\n" +
                               wlflib.app_to_feed(a))
                    return
            self._send(404, "app not found\n")
            return

        if path.startswith("/packages/"):
            fn = os.path.basename(path)
            full = os.path.join(self.packages_dir, fn)
            if os.path.isfile(full) and fn.endswith(".wlf"):
                with open(full, "rb") as f:
                    self._send(200, f.read(), "application/octet-stream")
            else:
                self._send(404, "package not found\n")
            return

        if path.startswith("/icons/"):
            app_id = os.path.splitext(os.path.basename(path))[0]
            for a in apps:
                if a["id"] == app_id and a.get("icon"):
                    self._send(200, a["icon"],
                               wlflib.icon_content_type(a["icon"]))
                    return
            self._send(404, "icon not found\n")
            return

        if path == "/v1/wallpapers":
            self._send(200, self._wall_feed())
            return

        if path.startswith("/wallpapers/"):
            fn = os.path.basename(path)
            full = os.path.join(self.wallpapers_dir, fn)
            if os.path.isfile(full) and fn.lower().endswith(".png"):
                with open(full, "rb") as f:
                    self._send(200, f.read(), "image/png")
            else:
                self._send(404, "wallpaper not found\n")
            return

        self._send(404, "not found\n")

    def _wall_feed(self):
        import json
        walls = []
        wd = self.wallpapers_dir
        if os.path.isdir(wd):
            meta = {}
            mf = os.path.join(wd, "wallpapers.json")
            if os.path.isfile(mf):
                try:
                    meta = json.load(open(mf, encoding="utf-8"))
                except Exception:
                    meta = {}
            for fn in sorted(os.listdir(wd)):
                if not fn.lower().endswith(".png"):
                    continue
                stem = os.path.splitext(fn)[0]
                m = meta.get(stem, {})
                walls.append({
                    "id": stem,
                    "title": m.get("title", stem.replace("_", " ").title()),
                    "author": m.get("author", "Arctian"),
                    "size": os.path.getsize(os.path.join(wd, fn)),
                    "file": fn,
                })
        out = ["#duckstore-feed 1\n"]
        for w in walls:
            out.append("@wall\n")
            out.append("id=" + wlflib.enc(w["id"]) + "\n")
            out.append("title=" + wlflib.enc(w["title"]) + "\n")
            out.append("author=" + wlflib.enc(w["author"]) + "\n")
            out.append("size=%d\n" % w["size"])
            out.append("image=/wallpapers/" + w["file"] + "\n")
        return "".join(out)

    def log_message(self, fmt, *args):
        sys.stderr.write("[duckstore] " + (fmt % args) + "\n")


def main():
    ap = argparse.ArgumentParser(description="DuckStore feed sunucusu (.wlf)")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--packages", default=os.path.join(_HERE, "packages"))
    ap.add_argument("--wallpapers", default=os.path.join(_HERE, "wallpapers"))
    ap.add_argument("--version-file", default=os.path.join(_HERE, "version.txt"))
    ap.add_argument("--version", default="0.7.0")
    args = ap.parse_args()

    Handler.packages_dir = args.packages
    Handler.wallpapers_dir = args.wallpapers
    Handler.version_file = args.version_file
    Handler.version = args.version
    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    n = len(wlflib.scan_packages(args.packages))
    print("DuckStore feed http://%s:%d (%d .wlf paket)" %
          (args.host, args.port, n))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
