# -*- coding: utf-8 -*-
"""
DuckStore indirme istatistikleri.

Veri kaynagi: feed sunucusunun (`duckstore-feed` servisi) istek satirlari.
Sunucu koduna hic dokunulmaz; yalnizca systemd journal'u okunur ve sonuc
`data/stats.json` dosyasinda saklanir.

  - Ilk calistirmada son 90 gunluk journal taranir (tam kurulum).
  - Sonraki cagrilarda journal cursor'u ile yalnizca YENI kayitlar okunur
    (artimali, ~0.1 sn).
  - 6 saatte bir tam yeniden kurulum yapilir (gun penceresi tazelenir).

Yalnizca Python 3 standart kutuphanesi kullanilir.
"""
import json
import os
import re
import subprocess
import threading
import time

UNIT = os.environ.get("DUCKSTORE_STATS_UNIT", "duckstore-feed")
LOG_OVERRIDE = os.environ.get("DUCKSTORE_STATS_LOG", "")
DAY_WINDOW = 14          # grafikte gosterilen gun sayisi
FULL_AFTER = 6 * 3600    # tam yeniden kurulum araligi (sn)
MAX_RECENT = 40
MAX_DAYS_STORED = 90

_LINE_RE = re.compile(
    r'^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)\S*\s+\S+\s+\S+\s*:?\s+'
    r'\[duckstore\]\s+"GET\s+(\S+)\s+HTTP/[\d.]+"\s+(\d{3})')
_LINE_RE_LOOSE = re.compile(
    r'\[duckstore\]\s+"GET\s+(\S+)\s+HTTP/[\d.]+"\s+(\d{3})')

_lock = threading.Lock()
_state = None
_path = None
_payload = None
_payload_at = 0.0


def _day(offset_days=0):
    """Gun anahtari 'YYYY-MM-DD' (offset 0 = bugun)."""
    return time.strftime("%Y-%m-%d",
                         time.localtime(time.time() - offset_days * 86400))


# --------------------------------------------------------------- durum
def _empty():
    return {"version": 1, "last_full": 0.0, "cursor": "",
            "totals": {"pkg": 0, "wall": 0, "feed": 0, "failed": 0, "bytes": 0},
            "days": {}, "files": {}, "recent": []}


def _load(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            st = json.load(f)
        if not isinstance(st, dict) or "totals" not in st:
            raise ValueError
        st.setdefault("files", {})
        st.setdefault("days", {})
        st.setdefault("recent", [])
        tot = st.setdefault("totals", {})
        for k in ("pkg", "wall", "feed", "failed", "bytes"):
            tot.setdefault(k, 0)
        return st
    except Exception:
        return _empty()


def _save(path, st):
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(st, f, ensure_ascii=False)
        os.replace(tmp, path)
    except Exception:
        pass


# --------------------------------------------------------------- okuma
def _classify(path):
    p = path.lower()
    if p.startswith("/packages/") and p.endswith(".wlf"):
        return "pkg"
    if p.startswith("/shots/") or p.startswith("/screenshots/"):
        return "pkg"          # magaza sitesindeki uygulama fotograflari
    if p.startswith("/wallpapers/") and p.endswith((".png", ".jpg", ".jpeg", ".webp")):
        return "wall"
    if p.startswith("/v1/") or p.startswith("/icons/"):
        return "feed"
    return None


def _read_journal(after_cursor=None):
    cmd = ["journalctl", "-u", UNIT, "--no-pager", "-o", "short-iso",
           "--show-cursor"]
    if after_cursor:
        # --since ile --after-cursor birlikte kullanilamaz
        cmd += ["--after-cursor", after_cursor]
    else:
        cmd += ["--since", "-90 days"]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=45)
    out = proc.stdout or ""
    if proc.returncode != 0 and not out.strip():
        err = (proc.stderr or "").strip()
        raise RuntimeError(err.splitlines()[-1] if err else "journalctl basarisiz")
    cursor = ""
    for line in out.splitlines():
        if line.startswith("-- cursor: "):
            cursor = line[len("-- cursor: "):].strip()
    return out, cursor


def _read_log_file(path):
    """Yedek kaynak: duz log dosyasi (zaman damgasi opsiyonel)."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read(), ""


def _parse(text, sizes, st):
    """Okunan satirlari duruma isler. islenen satir sayisini dondurur."""
    now_day = _day()
    n = 0
    for line in text.splitlines():
        if "[duckstore]" not in line or '"GET ' not in line:
            continue
        m = _LINE_RE.match(line)
        if m:
            ts, path, status = m.group(1), m.group(2), int(m.group(3))
            day = ts[:10]
        else:
            m2 = _LINE_RE_LOOSE.search(line)
            if not m2:
                continue
            path, status = m2.group(1), int(m2.group(2))
            ts, day = now_day + "T00:00:00", now_day
        kind = _classify(path)
        if kind is None:
            continue
        n += 1
        key = os.path.basename(path)
        failed = status >= 400
        if failed:
            st["totals"]["failed"] += 1
        if not failed:
            st["totals"][kind] = st["totals"].get(kind, 0) + 1
            d = st["days"].setdefault(day, {"p": 0, "w": 0, "h": 0})
            d[{"pkg": "p", "wall": "w", "feed": "h"}[kind]] += 1
            if kind in ("pkg", "wall"):
                f = st["files"].setdefault(
                    key, {"k": kind, "n": 0, "last": "", "days": {}})
                f["n"] += 1
                f["last"] = ts
                fd = f.setdefault("days", {})
                fd[day] = fd.get(day, 0) + 1
                size = sizes.get(key) if sizes else None
                if size:
                    f["b"] = f.get("b", 0) + int(size)
                st["totals"]["bytes"] = st["totals"].get("bytes", 0) + (
                    int(size) if size else 0)
        if not failed or kind in ("pkg", "wall"):
            st["recent"].append({"t": ts, "p": path, "s": status})
    if len(st["recent"]) > MAX_RECENT * 2:
        st["recent"] = st["recent"][-MAX_RECENT:]
    return n


def _prune(st, keep_days=MAX_DAYS_STORED):
    limit = _day(keep_days)
    st["days"] = {k: v for k, v in st["days"].items() if k >= limit}
    for f in st["files"].values():
        days = f.get("days") or {}
        f["days"] = {k: v for k, v in days.items() if k >= limit}


# --------------------------------------------------------------- dis arayuz
def snapshot(data_dir, sizes=None, force=False):
    """Istatistikleri dondurur (60 sn icinde ayni sonucu onbellekler)."""
    global _state, _path, _payload, _payload_at
    now = time.time()
    with _lock:
        _path = os.path.join(data_dir, "stats.json")
        if _state is None:
            _state = _load(_path)
        if _payload is not None and not force and (now - _payload_at) < 60:
            return _payload

        source = "journal"
        err = None
        try:
            if LOG_OVERRIDE and os.path.isfile(LOG_OVERRIDE):
                text, cur = _read_log_file(LOG_OVERRIDE)
                source, full = "log", True
            else:
                full = (not _state.get("cursor") or
                        (now - float(_state.get("last_full", 0))) > FULL_AFTER)
                try:
                    text, cur = _read_journal(
                        None if full else _state.get("cursor"))
                except Exception:
                    # imlecle okuma basarisizsa tam yeniden kuruluma don
                    text, cur = _read_journal(None)
                    full = True
            if full:
                # tam yeniden kurulum: once sifirla, sonra journal'i bastan isl
                _state["totals"] = {"pkg": 0, "wall": 0, "feed": 0,
                                    "failed": 0, "bytes": 0}
                _state["days"] = {}
                _state["recent"] = []
                for f in _state["files"].values():
                    f["n"] = 0
                    f["days"] = {}
                    f["b"] = 0
                    f["last"] = ""
            n = _parse(text, sizes or {}, _state)
            if cur:
                _state["cursor"] = cur
            if full:
                _prune(_state)
                _state["last_full"] = now
            if full or n:
                _save(_path, _state)
        except Exception as ex:            # journal yoksa panel calismaya devam eder
            err = str(ex) or ex.__class__.__name__
            source = "yok"
            n = 0

        _payload = _build(_state, source, err, sizes or {}, now)
        _payload_at = now
        return _payload


def _days(st, window=DAY_WINDOW):
    out = []
    for i in range(window - 1, -1, -1):
        d = _day(i)
        v = st.get("days", {}).get(d, {})
        out.append({"d": d, "p": v.get("p", 0), "w": v.get("w", 0),
                    "h": v.get("h", 0),
                    "t": v.get("p", 0) + v.get("w", 0) + v.get("h", 0)})
    return out


def _sum_days(st, key):
    """(toplam, bugun, son 7 gun) - gun anahtarina gore."""
    today = _day()
    week = _day(6)

    def pick(v):
        return (v.get("p", 0) + v.get("w", 0) + v.get("h", 0)
                if key == "t" else v.get(key, 0))

    days = st.get("days", {})
    return (sum(pick(v) for v in days.values()),
            sum(pick(v) for k, v in days.items() if k == today),
            sum(pick(v) for k, v in days.items() if k >= week))


def _build(st, source, err, sizes, now):
    files = []
    for name, f in st.get("files", {}).items():
        kind = f.get("k", "pkg")
        size = sizes.get(name)
        count = int(f.get("n", 0))
        if size:
            byte_total = count * int(size)
        else:
            byte_total = int(f.get("b", 0))
        days = f.get("days") or {}
        today = days.get(_day(), 0)
        week = sum(v for k, v in days.items() if k >= _day(6))
        files.append({"name": name, "kind": kind, "n": count,
                      "bytes": byte_total, "last": f.get("last", ""),
                      "today": today, "week": week})
    files.sort(key=lambda x: -x["n"])

    _, t_today, t_week = _sum_days(st, "t")
    _, p_today, p_week = _sum_days(st, "p")
    _, w_today, w_week = _sum_days(st, "w")

    return {
        "source": source,
        "error": err,
        "updated": int(now),
        "window": DAY_WINDOW,
        "totals": {"pkg": int(st["totals"].get("pkg", 0)),
                   "wall": int(st["totals"].get("wall", 0)),
                   "feed": int(st["totals"].get("feed", 0)),
                   "failed": int(st["totals"].get("failed", 0)),
                   "bytes": int(st["totals"].get("bytes", 0))},
        "today": {"all": t_today, "pkg": p_today, "wall": w_today},
        "week": {"all": t_week, "pkg": p_week, "wall": w_week},
        "recent": list(reversed(st.get("recent", [])[-MAX_RECENT:])),
        "days": _days(st),
        "files": files,
    }


def reset():
    global _state, _payload, _payload_at
    with _lock:
        _state = None
        _payload = None
        _payload_at = 0.0
