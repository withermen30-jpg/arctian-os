#!/usr/bin/env python3
"""
sdkdist — libWolf SDK dagitim katalogu (sunucu tarafi).

store/public/sdk/ altindaki kurulum dosyalarini ve manifest.json'u okur.
Kurulum dosyalari `tools/sdk_dist.py` ile uretilir.

Yalnizca Python 3 standart kutuphanesi.
"""
import hashlib
import json
import os

_HERE = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_PUBLIC = os.path.join(os.path.dirname(_HERE), "public", "sdk")

PLATFORM_LABEL = {
    "windows": "Windows 10/11 (x64)",
    "linux": "Linux (x86_64)",
    "macos": "macOS (Intel / Apple Silicon)",
}


def public_dir():
    """SDK dagitim klasoru (DUCKSTORE_BASE varsa oradan)."""
    base = os.environ.get("DUCKSTORE_BASE", "")
    if base:
        cand = os.path.join(base, "public", "sdk")
        if os.path.isdir(cand):
            return cand
    env = os.environ.get("DUCKSTORE_SDK_DIR")
    if env and os.path.isdir(env):
        return env
    for cand in (_DEFAULT_PUBLIC,
                 "/opt/duckstore/public/sdk",
                 "/opt/duckstore/sdk"):
        if os.path.isdir(cand):
            return cand
    return _DEFAULT_PUBLIC


class SdkError(Exception):
    pass


def load_manifest():
    """manifest.json icerigini dondurur; yoksa bos iskelet."""
    path = os.path.join(public_dir(), "manifest.json")
    try:
        with open(path, "r", encoding="utf-8") as f:
            man = json.load(f)
    except (OSError, ValueError):
        return {"sdk": "libWolf", "version": "?", "artifacts": []}
    if not isinstance(man, dict):
        return {"sdk": "libWolf", "version": "?", "artifacts": []}
    man.setdefault("artifacts", [])
    return man


def artifacts():
    """Dogrulanmis artifact listesi (sha256 + boyut yeniden hesaplanir)."""
    d = public_dir()
    out = []
    for a in load_manifest().get("artifacts", []):
        fn = a.get("file") or ""
        if not fn or "/" in fn or "\\" in fn or fn.startswith("."):
            continue                       # yol kacisi engeli
        full = os.path.join(d, fn)
        if not os.path.isfile(full):
            continue
        try:
            size = os.path.getsize(full)
        except OSError:
            continue
        out.append({
            "file": fn,
            "platform": a.get("platform", "?"),
            "platform_label": PLATFORM_LABEL.get(a.get("platform", ""), "?"),
            "arch": a.get("arch", ""),
            "kind": a.get("kind", ""),
            "size": size,
            "sha256": a.get("sha256", ""),
            "notes": a.get("notes", ""),
        })
    return out


def safe_path(fn):
    """Istenen dosya adini dagitim klasorune guvenle cozer."""
    if not fn or "/" in fn or "\\" in fn or fn.startswith("."):
        return None
    full = os.path.join(public_dir(), fn)
    if not os.path.isfile(full):
        return None
    return full


def sha256_of(full_path):
    h = hashlib.sha256()
    with open(full_path, "rb") as f:
        for chunk in iter(lambda: f.read(256 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def by_platform(platform):
    """Platformin onerilen paketi. Windows'ta zip (emulatorlu) exe'ye onerilir."""
    best = None
    order = {"zip": 0, "shell": 1, "exe": 2}
    for a in artifacts():
        if a["platform"] != platform:
            continue
        if best is None or order.get(a["kind"], 9) < order.get(best["kind"], 9):
            best = a
    return best


def catalog(base_url=""):
    """MCP ve API icin tek bicimli katalog."""
    man = load_manifest()
    arts = []
    for a in artifacts():
        item = dict(a)
        item["url"] = "%s/downloads/sdk/%s" % (base_url.rstrip("/"), a["file"])
        arts.append(item)
    return {
        "sdk": man.get("sdk", "libWolf"),
        "version": man.get("version", "?"),
        "abi_version": (man.get("api") or {}).get("abi_version", 1),
        "header": (man.get("api") or {}).get("header", "include/wolf.h"),
        "entry": (man.get("api") or {}).get("entry",
                                            "int wolf_main(const wolf_api_t *api)"),
        "install": man.get("install", {}),
        "build": man.get("build", {}),
        "emulator": man.get("emulator", {}),
        "artifacts": arts,
    }


def fmt_size(n):
    try:
        n = float(n)
    except (TypeError, ValueError):
        return "-"
    for u in ("B", "KB", "MB", "GB"):
        if n < 1024 or u == "GB":
            return ("%.1f %s" % (n, u)) if u != "B" else ("%d B" % n)
        n /= 1024.0
    return "-"
