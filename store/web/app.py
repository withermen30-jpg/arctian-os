#!/usr/bin/env python3
"""
DuckStore yonetim paneli (varsayilan port 129).

Ozellikler:
  - Ilk giriste yonetici sifresi belirleme
  - Sifre ile giris (oturum cerezi)
  - .wlf yukleyerek uygulama yayinlama: paket wlflib.validate ile dogrulanir,
    magaza anahtariyla yeniden imzalanir ve packages/ altina yazilir
    (feed sunucusu + magaza sitesi otomatik servis eder)
  - Uygulama aciklamasi + ekran fotografi yonetimi (magaza sitesinde gorunur)
  - Yayindaki uygulamalari listeleme/silme, duvar kagidi + istatistik

Yalnizca Python 3 standart kutuphanesi kullanir.
"""
import base64
import hashlib
import hmac
import html
import io
import json
import os
import re
import secrets
import shutil
import sys
import threading
import time
import urllib.parse
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

_HERE = os.path.dirname(os.path.abspath(__file__))
for _p in (_HERE, os.path.join(os.path.dirname(_HERE), "common")):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import formdata
import stats as stats_mod
import wlflib
import storemeta
import tokens as tokenstore
import market        # magaza basvuru kuyrugu (ayni DUCKSTORE_BASE)

BASE = os.environ.get("DUCKSTORE_BASE", "/opt/duckstore")
PACKAGES = os.path.join(BASE, "packages")
DATA = os.path.join(BASE, "data")
ADMIN_FILE = os.path.join(DATA, "admin.json")
UI_FILE = os.path.join(DATA, "ui.json")
# Duvar kagidi klasoru (feed sunucusunun da taradigi dizin)
WALLS_DIR = next(
    (p for p in (os.path.join(BASE, "server", "wallpapers"),
                 os.path.join(BASE, "wallpapers"),
                 os.path.join(os.path.dirname(BASE), "store", "server", "wallpapers"))
     if os.path.isdir(p)),
    os.path.join(BASE, "server", "wallpapers"))
PORT = int(os.environ.get("DUCKSTORE_WEB_PORT", "129"))
BIND = os.environ.get("DUCKSTORE_WEB_BIND", "0.0.0.0")
MARKET_PORT = int(os.environ.get("DUCKSTORE_MARKET_PORT", "47584"))

MAX_UPLOAD = wlflib.WLF_MAX_PACKAGE
SESSION_TTL = 12 * 3600

_lock = threading.Lock()
_sessions = {}
_last_log = ["(henuz islem yapilmadi)"]


# ---------------------------------------------------------------- yardimci
def load_json(path, default):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return default


def save_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
    os.replace(tmp, path)


def hash_pw(pw, salt_hex):
    dk = hashlib.pbkdf2_hmac("sha256", pw.encode("utf-8"),
                             bytes.fromhex(salt_hex), 120000)
    return dk.hex()


def admin_exists():
    return os.path.isfile(ADMIN_FILE)


def verify_login(pw):
    a = load_json(ADMIN_FILE, None)
    if not a:
        return False
    return hmac.compare_digest(hash_pw(pw, a["salt"]), a["hash"])


def new_session():
    tok = secrets.token_urlsafe(32)
    with _lock:
        _sessions[tok] = time.time() + SESSION_TTL
    return tok


def session_valid(tok):
    if not tok:
        return False
    with _lock:
        exp = _sessions.get(tok)
        if not exp:
            return False
        if exp < time.time():
            _sessions.pop(tok, None)
            return False
        return True


def drop_session(tok):
    with _lock:
        _sessions.pop(tok, None)


def stats_sizes():
    """Istatistiklerin bayt hesabi icin gecerli dosya boyutlari."""
    sizes = {}
    for d, exts in ((PACKAGES, (".wlf",)),
                    (WALLS_DIR, (".png", ".jpg", ".jpeg", ".webp"))):
        if not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            if fn.lower().endswith(exts):
                try:
                    sizes[fn] = os.path.getsize(os.path.join(d, fn))
                except OSError:
                    pass
    return sizes


def stats_snapshot(force=False):
    try:
        return stats_mod.snapshot(DATA, stats_sizes(), force=force)
    except Exception as ex:
        return {"source": "yok", "error": str(ex), "updated": int(time.time()),
                "window": 14, "totals": {}, "today": {}, "week": {},
                "recent": [], "days": [], "files": []}


def list_packages():
    """Yayindaki .wlf uygulamalarini (yan-veri + indirme) listeler."""
    out = []
    try:
        dl = {f["name"]: f for f in stats_snapshot().get("files", [])}
    except Exception:
        dl = {}
    meta = storemeta.load_meta(DATA)
    for a in wlflib.scan_packages(PACKAGES):
        m = meta.get(a["id"], {})
        shots = storemeta.list_shots(DATA, a["id"])
        info = dict(a)
        info["description"] = m.get("description", "")
        info["publisher"] = m.get("publisher") or a.get("author") or "Arctian"
        info["featured"] = bool(m.get("featured"))
        info["hidden"] = bool(m.get("hidden"))
        info["shots"] = shots
        info["downloads"] = dl.get(a["file"], {}).get("n", 0)
        info["last_dl"] = dl.get(a["file"], {}).get("last", "")
        out.append(info)
    return out


def find_pkg(app_id):
    for a in wlflib.scan_packages(PACKAGES):
        if a["id"] == app_id:
            return a
    return None


_ID_RE = re.compile(r"^[a-z0-9_]{1,31}$")


# ---------------------------------------------------------------- HTML
CSS = """
*,*::before,*::after{box-sizing:border-box}
:root{
  --txt:#eef1f7; --muted:#98a1b3; --line:rgba(255,255,255,.09);
  --card:rgba(17,19,28,.72); --card2:rgba(26,29,41,.62);
  --acc:#3b82f6; --acc2:#8b5cf6; --ok:#22c55e; --warn:#f59e0b; --bad:#ef4444;
  --rad:16px;
}
html,body{margin:0;padding:0}
body{
  min-height:100vh;color:var(--txt);background:#06070d;
  font:15px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
  -webkit-font-smoothing:antialiased;
}
/* ---- arka plan: secilen duvar kagidi + koyu gradyan ---- */
body::before{
  content:"";position:fixed;inset:0;z-index:-2;pointer-events:none;
  background:var(--bg-img) center/cover no-repeat #06070d;
}
body::after{
  content:"";position:fixed;inset:0;z-index:-1;pointer-events:none;
  background:
    radial-gradient(1100px 620px at 12% -12%, rgba(59,130,246,.26), transparent 62%),
    radial-gradient(900px 520px at 102% -4%, rgba(139,92,246,.22), transparent 58%),
    linear-gradient(180deg, rgba(6,7,13,.80) 0%, rgba(6,7,13,.93) 42%, rgba(6,7,13,.97) 100%);
}
a{color:#7dd3fc;text-decoration:none}
a:hover{text-decoration:underline}

/* ---- ust bar ---- */
.top{position:sticky;top:0;z-index:30;border-bottom:1px solid var(--line);
  background:rgba(9,11,18,.74);backdrop-filter:blur(18px);-webkit-backdrop-filter:blur(18px)}
.top-in{max-width:1180px;margin:0 auto;padding:11px 20px;display:flex;align-items:center;gap:14px}
.brand{display:flex;align-items:center;gap:10px;font-weight:700;font-size:17px;letter-spacing:.2px;white-space:nowrap}
.brand .logo{width:30px;height:30px;border-radius:9px;display:grid;place-items:center;
  background:linear-gradient(135deg,var(--acc),var(--acc2));box-shadow:0 6px 16px rgba(59,130,246,.35)}
.brand small{display:block;font-size:10.5px;font-weight:500;color:var(--muted);letter-spacing:.6px;text-transform:uppercase}
nav{display:flex;gap:5px;margin-left:8px;flex:1;overflow-x:auto;scrollbar-width:none}
nav::-webkit-scrollbar{display:none}
nav a{color:var(--muted);text-decoration:none;padding:8px 14px;border-radius:999px;font-size:14px;
  font-weight:500;white-space:nowrap;border:1px solid transparent;transition:.15s}
nav a:hover{color:#fff;background:rgba(255,255,255,.07);text-decoration:none}
nav a.on{color:#fff;background:linear-gradient(135deg,var(--acc),var(--acc2));
  box-shadow:0 8px 20px rgba(59,130,246,.30)}
.status{display:flex;align-items:center;gap:8px;font-size:12.5px;color:var(--muted);white-space:nowrap}
.dot{width:8px;height:8px;border-radius:50%;background:var(--ok);box-shadow:0 0 0 4px rgba(34,197,94,.16)}
.dot.off{background:var(--bad);box-shadow:0 0 0 4px rgba(239,68,68,.16)}

/* ---- sayfa ---- */
.wrap{max-width:1180px;margin:0 auto;padding:26px 20px 64px}
.page-h{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin:4px 0 20px;flex-wrap:wrap}
h1.t{margin:0;font-size:27px;font-weight:700;letter-spacing:-.4px}
.sub{color:var(--muted);font-size:14px;margin-top:5px}
h3{margin:0 0 12px;font-size:16.5px;font-weight:650;letter-spacing:-.1px}

.card{background:var(--card);border:1px solid var(--line);border-radius:var(--rad);
  padding:20px;margin-bottom:18px;backdrop-filter:blur(16px);-webkit-backdrop-filter:blur(16px);
  box-shadow:0 18px 44px rgba(0,0,0,.32)}
.card.tight{padding:0;overflow:hidden}
.card h3 .hint{font-weight:400;font-size:13px;color:var(--muted);margin-left:8px}
.grid{display:grid;gap:18px}
.g2{grid-template-columns:repeat(auto-fit,minmax(340px,1fr))}
.g3{grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}
.g4{grid-template-columns:repeat(auto-fit,minmax(190px,1fr))}

/* ---- istatistik kutulari ---- */
.tile{position:relative;overflow:hidden;background:var(--card);border:1px solid var(--line);
  border-radius:var(--rad);padding:17px 18px;backdrop-filter:blur(16px);transition:.18s;
  box-shadow:0 14px 34px rgba(0,0,0,.28)}
.tile:hover{transform:translateY(-3px);border-color:rgba(59,130,246,.4)}
.tile .ico{width:34px;height:34px;border-radius:10px;display:grid;place-items:center;margin-bottom:11px;
  background:linear-gradient(135deg,rgba(59,130,246,.24),rgba(139,92,246,.24));
  border:1px solid rgba(139,92,246,.28)}
.tile .val{font-size:27px;font-weight:720;letter-spacing:-.9px;line-height:1.1;font-variant-numeric:tabular-nums}
.tile .lbl{color:var(--muted);font-size:12.5px;margin-top:3px}
.tile .spark{position:absolute;right:-10px;bottom:-8px;opacity:.16;pointer-events:none}
.tile.b .ico{background:linear-gradient(135deg,rgba(34,197,94,.24),rgba(59,130,246,.22));border-color:rgba(34,197,94,.3)}
.tile.c .ico{background:linear-gradient(135deg,rgba(245,158,11,.24),rgba(239,68,68,.20));border-color:rgba(245,158,11,.3)}

/* ---- grafik ---- */
.chart{width:100%;height:190px;display:block}
.chart .bar{fill:url(#gbar);transition:.15s}
.chart .bar:hover{fill:url(#gbar2)}
.chart text{fill:#8b93a7;font-size:11px;font-family:inherit}
.chart .gl{stroke:rgba(255,255,255,.07);stroke-width:1}
.legend{display:flex;gap:16px;flex-wrap:wrap;font-size:12.5px;color:var(--muted);margin-top:8px}
.legend i{width:10px;height:10px;border-radius:3px;display:inline-block;margin-right:6px;vertical-align:-1px}

/* ---- tablolar ---- */
.tw{overflow-x:auto}
table{width:100%;border-collapse:collapse}
th,td{text-align:left;padding:12px 14px;font-size:14px;border-bottom:1px solid var(--line);vertical-align:middle}
thead th{color:var(--muted);font-weight:500;font-size:12px;letter-spacing:.55px;text-transform:uppercase;
  background:rgba(255,255,255,.03);position:sticky;top:0}
tbody tr:hover{background:rgba(59,130,246,.07)}
tbody tr:last-child td{border-bottom:0}
td.num,th.num{text-align:right;font-variant-numeric:tabular-nums}
.appcell{display:flex;align-items:center;gap:12px}
.icon{width:38px;height:38px;border-radius:10px;background:rgba(255,255,255,.05);
  border:1px solid var(--line);display:grid;place-items:center;flex:0 0 auto;overflow:hidden}
.icon svg,.icon img{width:100%;height:100%;display:block}
.ttl{font-weight:600}
.meta{color:var(--muted);font-size:12.5px}
.chip{display:inline-block;padding:3px 10px;border-radius:999px;font-size:11.5px;font-weight:600;
  background:rgba(139,92,246,.16);border:1px solid rgba(139,92,246,.3);color:#c4b5fd}
.chip.c2{background:rgba(59,130,246,.14);border-color:rgba(59,130,246,.3);color:#bfdbfe}
.chip.c3{background:rgba(34,197,94,.14);border-color:rgba(34,197,94,.3);color:#bbf7d0}
.chip.c4{background:rgba(245,158,11,.14);border-color:rgba(245,158,11,.3);color:#fde68a}
.rk{display:inline-flex;align-items:center;justify-content:center;min-width:22px;height:22px;padding:0 7px;
  border-radius:7px;background:rgba(255,255,255,.06);border:1px solid var(--line);font-size:11.5px;color:var(--muted)}

/* ---- butonlar / formlar ---- */
.btn{display:inline-flex;align-items:center;gap:8px;background:linear-gradient(135deg,var(--acc),var(--acc2));
  color:#fff;border:0;border-radius:10px;padding:10px 17px;cursor:pointer;font-size:14px;font-weight:600;
  transition:.15s;box-shadow:0 8px 20px rgba(59,130,246,.26)}
.btn:hover{filter:brightness(1.1);transform:translateY(-1px);text-decoration:none}
.btn.sm{padding:7px 12px;font-size:12.5px;border-radius:9px;box-shadow:none}
.btn.ghost{background:rgba(255,255,255,.06);border:1px solid var(--line);box-shadow:none}
.btn.danger{background:linear-gradient(135deg,#ef4444,#b91c1c);box-shadow:none}
.btn.wide{width:100%;justify-content:center}
input[type=password],input[type=text],input[type=number],select,textarea{
  width:100%;background:rgba(10,12,19,.75);border:1px solid var(--line);color:var(--txt);
  border-radius:11px;padding:12px 13px;font-size:14.5px;outline:none;transition:.15s;font-family:inherit}
input:focus,select:focus,textarea:focus{border-color:rgba(59,130,246,.65);box-shadow:0 0 0 4px rgba(59,130,246,.15)}
input[type=file]{width:100%;background:rgba(10,12,19,.75);border:1px dashed rgba(255,255,255,.18);
  border-radius:11px;padding:13px;color:var(--muted);font-size:14px}
label{display:block;font-size:12.5px;color:var(--muted);margin:12px 0 6px;font-weight:500}
.drop{border:1.5px dashed rgba(255,255,255,.2);border-radius:14px;padding:26px 18px;text-align:center;
  background:rgba(255,255,255,.03);transition:.18s;cursor:pointer}
.drop.on{border-color:var(--acc);background:rgba(59,130,246,.12);transform:scale(1.01)}
.drop b{display:block;font-size:15.5px;margin-bottom:5px}
.drop span{color:var(--muted);font-size:13px}
.err{background:rgba(239,68,68,.12);border:1px solid rgba(239,68,68,.34);color:#fecaca;
  border-radius:11px;padding:12px 14px;font-size:14px;margin-bottom:14px;white-space:pre-wrap;word-break:break-word}
.ok{background:rgba(34,197,94,.12);border:1px solid rgba(34,197,94,.34);color:#bbf7d0;
  border-radius:11px;padding:12px 14px;font-size:14px;margin-bottom:14px}
.muted{color:var(--muted);font-size:13px}
code{background:rgba(8,10,16,.8);border:1px solid var(--line);border-radius:8px;padding:3px 8px;
  font-size:13px;color:#a5d6ff;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;word-break:break-all}
pre{background:rgba(8,10,16,.82);border:1px solid var(--line);border-radius:12px;padding:14px;
  overflow:auto;color:#a5d6ff;font-size:12.7px;line-height:1.5;max-height:340px;
  font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;white-space:pre-wrap;word-break:break-word}

/* ---- duvar kagidi galerisi ---- */
.gal{display:grid;gap:16px;grid-template-columns:repeat(auto-fill,minmax(230px,1fr))}
.wcard{background:var(--card2);border:1px solid var(--line);border-radius:14px;overflow:hidden;transition:.18s}
.wcard:hover{transform:translateY(-3px);border-color:rgba(139,92,246,.45);box-shadow:0 18px 38px rgba(0,0,0,.4)}
.wcard .shot{height:132px;background:#0b0d14 center/cover no-repeat;position:relative}
.wcard .shot::after{content:"";position:absolute;inset:0;background:linear-gradient(180deg,transparent 45%,rgba(6,7,13,.75))}
.wcard.on .shot{outline:2px solid var(--ok);outline-offset:-2px}
.wcard .bd{padding:12px 13px 14px}
.wcard .nm{font-weight:640;font-size:14.5px}
.wcard .acts{display:flex;gap:7px;margin-top:11px;flex-wrap:wrap}

/* ---- giris / kurulum ---- */
.auth{max-width:400px;margin:8vh auto 0}
.auth .card{padding:28px}
.auth .brandbig{display:flex;align-items:center;justify-content:center;gap:11px;margin-bottom:6px;font-size:22px;font-weight:750}
.auth .brandbig .logo{width:38px;height:38px;border-radius:12px;display:grid;place-items:center;
  background:linear-gradient(135deg,var(--acc),var(--acc2));box-shadow:0 8px 22px rgba(59,130,246,.4)}
.auth .tag{text-align:center;color:var(--muted);font-size:13.5px;margin-bottom:22px}

/* ---- misc ---- */
.list{list-style:none;margin:0;padding:0}
.list li{display:flex;align-items:center;gap:12px;padding:11px 4px;border-bottom:1px solid var(--line);font-size:14px}
.list li:last-child{border-bottom:0}
.list .grow{flex:1;min-width:0}
.trunc{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bar-mini{height:6px;border-radius:99px;background:rgba(255,255,255,.08);overflow:hidden;min-width:70px}
.bar-mini i{display:block;height:100%;border-radius:99px;background:linear-gradient(90deg,var(--acc),var(--acc2))}
.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
.spacer{flex:1}
.pill{display:inline-flex;align-items:center;gap:6px;padding:5px 11px;border-radius:999px;font-size:12.5px;
  background:rgba(255,255,255,.06);border:1px solid var(--line);color:var(--muted)}
.kbd{background:rgba(255,255,255,.08);border:1px solid var(--line);border-bottom-width:2px;border-radius:6px;
  padding:1px 6px;font-size:12px;color:#cbd5e1}
.foot{color:var(--muted);font-size:12.5px;text-align:center;margin-top:26px}
.foot a{color:#7dd3fc}
.spin{display:inline-block;width:14px;height:14px;border:2px solid rgba(255,255,255,.25);
  border-top-color:var(--acc);border-radius:50%;animation:sp .8s linear infinite;vertical-align:-2px}
@keyframes sp{to{transform:rotate(360deg)}}
@media (max-width:760px){
  .top-in{flex-wrap:wrap;gap:8px}
  nav{order:3;width:100%;margin-left:0}
  h1.t{font-size:23px}
  .wrap{padding:20px 14px 50px}
}
"""


def e(x):
    return html.escape(str(x))


def fmt_bytes(n):
    try:
        n = float(n)
    except (TypeError, ValueError):
        return "-"
    for u in ("B", "KB", "MB", "GB"):
        if n < 1024 or u == "GB":
            return ("%.1f %s" % (n, u)) if u != "B" else ("%d B" % n)
        n /= 1024.0
    return "%.1f GB" % n


def _num(n):
    try:
        return "{:,}".format(int(n)).replace(",", ".")
    except (TypeError, ValueError):
        return "0"


def _svg(path, stroke="#93c5fd", w=17):
    return ("<svg width='%d' height='%d' viewBox='0 0 24 24' fill='none' "
            "stroke='%s' stroke-width='1.7' stroke-linecap='round' "
            "stroke-linejoin='round'>%s</svg>" % (w, w, stroke, path))


ICO = {
    "box": _svg("<path d='M21 8l-9-5-9 5 9 5 9-5z'/><path d='M3 8v8l9 5 9-5V8'/>"
                "<path d='M12 13v8'/>"),
    "dl": _svg("<path d='M12 3v12'/><path d='M7 11l5 5 5-5'/>"
               "<path d='M4 20h16'/>", "#86efac"),
    "today": _svg("<rect x='3' y='5' width='18' height='16' rx='2.5'/>"
                  "<path d='M3 10h18M8 3v4M16 3v4'/>", "#86efac"),
    "week": _svg("<path d='M4 19V9M10 19V5M16 19v-7M22 19H2'/>", "#86efac"),
    "wall": _svg("<rect x='3' y='4' width='18' height='16' rx='2.5'/>"
                 "<circle cx='8.5' cy='9.5' r='1.6'/><path d='M4 17l5-5 4 4 3-3 4 4'/>",
                 "#c4b5fd"),
    "feed": _svg("<path d='M4 11a9 9 0 0 1 9 9'/><path d='M4 4a16 16 0 0 1 16 16'/>"
                 "<circle cx='5' cy='19' r='1.6'/>", "#c4b5fd"),
    "disk": _svg("<rect x='3' y='5' width='18' height='14' rx='2.5'/>"
                 "<path d='M7 9h10M7 13h6'/><circle cx='17' cy='15' r='1.3'/>"),
}


def chart_svg(days, width=920, height=210):
    """Gunluk indirme grafigi (paket + duvar kagidi, yiginli bar)."""
    days = list(days or [])
    pad_l, pad_r, pad_b, pad_t = 40, 14, 34, 16
    iw = width - pad_l - pad_r
    ih = height - pad_b - pad_t
    if not days:
        days = [{"d": "", "p": 0, "w": 0}]
    n = max(1, len(days))
    mx = max([d.get("p", 0) + d.get("w", 0) for d in days] + [1])
    slot = iw / float(n)
    bw = min(34.0, slot * 0.56)

    out = ["<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %d %d' "
           "width='100%%' height='%d' role='img' aria-label='gunluk indirme'>"
           % (width, height, height)]
    out.append("<defs>"
               "<linearGradient id='gb' x1='0' y1='0' x2='0' y2='1'>"
               "<stop offset='0' stop-color='#60a5fa'/>"
               "<stop offset='1' stop-color='#3b82f6'/></linearGradient>"
               "<linearGradient id='gw' x1='0' y1='0' x2='0' y2='1'>"
               "<stop offset='0' stop-color='#a78bfa'/>"
               "<stop offset='1' stop-color='#8b5cf6'/></linearGradient>"
               "</defs>")
    # izgara + y ekseni
    for i in range(3):
        y = pad_t + ih - ih * (i / 2.0)
        out.append("<line class='gl' x1='%d' y1='%.1f' x2='%d' y2='%.1f' "
                   "stroke='rgba(255,255,255,.07)'/>"
                   % (pad_l, y, width - pad_r, y))
        out.append("<text x='%d' y='%.1f' text-anchor='end' fill='#7b8496' "
                   "font-size='11'>%d</text>"
                   % (pad_l - 8, y + 4, int(round(mx * i / 2.0))))
    for i, d in enumerate(days):
        p = int(d.get("p", 0))
        w = int(d.get("w", 0))
        x = pad_l + i * slot + (slot - bw) / 2.0
        hp = ih * (p / float(mx)) if mx else 0
        hw = ih * (w / float(mx)) if mx else 0
        yw = pad_t + ih - hw
        yp = yw - hp
        label = str(d.get("d", ""))[5:]
        if w:
            out.append("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='3' "
                       "fill='url(#gw)'><title>%s · duvar kağıdı %d</title></rect>"
                       % (x, yw, bw, hw, e(d.get("d", "")), w))
        if p:
            out.append("<rect x='%.1f' y='%.1f' width='%.1f' height='%.1f' rx='3' "
                       "fill='url(#gb)'><title>%s · paket %d</title></rect>"
                       % (x, yp, bw, hp, e(d.get("d", "")), p))
        if p + w:
            out.append("<text x='%.1f' y='%.1f' text-anchor='middle' "
                       "fill='#cbd5e1' font-size='10.5'>%d</text>"
                       % (x + bw / 2.0, yp - 5, p + w))
        if n <= 14 or i % 2 == 0:
            out.append("<text x='%.1f' y='%d' text-anchor='middle' fill='#7b8496' "
                       "font-size='10.5'>%s</text>"
                       % (x + bw / 2.0, height - 12, label))
    out.append("</svg>")
    return "".join(out)


def ui_prefs():
    """Panel tercihleri (arka plan secimi vs.)."""
    return load_json(UI_FILE, {"bg": "", "bg_enabled": True})


def save_ui(d):
    save_json(UI_FILE, d)


def current_bg():
    """Secilen duvar kagidinin dosya adi; 'none' = arka plan kapali."""
    pref = ui_prefs()
    name = (pref.get("bg") or "").strip()
    if name == "none":
        return ""
    if name and wall_file(name):
        return name
    walls = list_walls()
    return walls[0]["file"] if walls else ""


def wall_file(name):
    """Guvenli duvar kagidi dosya adi -> tam yol (yoksa None)."""
    fn = os.path.basename(str(name or "").replace("\\", "/"))
    if not re.match(r"^[A-Za-z0-9_\- ]{1,64}\.(png|jpg|jpeg|webp)$", fn, re.I):
        return None
    full = os.path.join(WALLS_DIR, fn)
    return full if os.path.isfile(full) else None


def list_walls():
    """Duvar kagidilarini meta verisiyle listeler."""
    out = []
    if not os.path.isdir(WALLS_DIR):
        return out
    meta = {}
    mf = os.path.join(WALLS_DIR, "wallpapers.json")
    if os.path.isfile(mf):
        meta = load_json(mf, {})
    for fn in sorted(os.listdir(WALLS_DIR)):
        if not fn.lower().endswith((".png", ".jpg", ".jpeg", ".webp")):
            continue
        stem = os.path.splitext(fn)[0]
        m = meta.get(stem, {}) if isinstance(meta, dict) else {}
        st = os.stat(os.path.join(WALLS_DIR, fn))
        out.append({"file": fn, "id": stem,
                    "title": m.get("title", stem.replace("_", " ").title()),
                    "author": m.get("author", "Arctian"),
                    "size": st.st_size, "mtime": int(st.st_mtime)})
    out.sort(key=lambda w: -w["mtime"])
    return out


def icon_html(app):
    """Uygulama ikonunu guvenli sekilde gosterir (SVG varsa gom, yoksa PNG data-uri)."""
    svg = (app.get("icon_svg") or "").strip()
    if svg:
        return svg
    raw = app.get("icon") or b""
    if isinstance(raw, str):
        raw = raw.encode("utf-8", "replace")
    if raw:
        import base64
        ct = wlflib.icon_content_type(raw)
        if ct != "image/svg+xml":
            b64 = base64.b64encode(raw).decode("ascii")
            return "<img src='data:%s;base64,%s' alt=''>" % (ct, b64)
    return ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 40 40'>"
            "<rect x='1' y='1' width='38' height='38' rx='10' fill='#16171C' "
            "stroke='#2C2F38'/>"
            "<path d='M12 26l5-8 4 5 3-4 4 7z' fill='#3B82F6'/>"
            "<circle cx='27' cy='14' r='2.5' fill='#8B5CF6'/></svg>")


LOGO = ("<span class='logo'><svg width='17' height='17' viewBox='0 0 24 24' "
        "fill='none'><path d='M4.2 15.4a6 6 0 0 1 6-6h3.2a4.9 4.9 0 0 1 0 9.8H9.4"
        "a6 6 0 0 1-5.2-3.8z' fill='#fff'/><circle cx='15.6' cy='11.4' r='1.25' "
        "fill='#0b0d14'/><path d='M17.8 12.4l3.6-1.3-2.7 3z' fill='#fbbf24'/></svg></span>")

NAV = [("", "Panel", "/"),
       ("apps", "Uygulamalar", "/apps"),
       ("review", "İnceleme", "/review"),
       ("tokens", "Jetonlar", "/tokens"),
       ("walls", "Duvar Kağıtları", "/walls"),
       ("stats", "İstatistikler", "/stats")]

JS = """
(function(){
  var d=document.querySelector('.drop');
  if(d){
    var i=d.querySelector('input[type=file]');
    d.addEventListener('click',function(ev){
      if(ev.target.tagName!=='INPUT'&&i) i.click();
    });
    ['dragenter','dragover'].forEach(function(ev){
      d.addEventListener(ev,function(x){x.preventDefault();d.classList.add('on');});
    });
    ['dragleave','drop'].forEach(function(ev){
      d.addEventListener(ev,function(x){x.preventDefault();d.classList.remove('on');});
    });
    d.addEventListener('drop',function(x){
      if(x.dataTransfer&&x.dataTransfer.files.length&&i){
        i.files=x.dataTransfer.files;
        var t=d.querySelector('b'); if(t) t.textContent=x.dataTransfer.files[0].name;
      }
    });
  }
  var c=document.querySelector('[data-copy]');
  if(c) c.addEventListener('click',function(){
    var v=c.getAttribute('data-copy');
    if(navigator.clipboard) navigator.clipboard.writeText(v);
    var o=c.textContent; c.textContent='Kopyalandı';
    setTimeout(function(){c.textContent=o;},1400);
  });
})();
"""


def shell(title, body, active=None, host=""):
    items = []
    for key, label, href in NAV:
        on = " class='on'" if active == key else ""
        items.append("<a href='%s'%s>%s</a>" % (href, on, label))
    feed = ""
    if host:
        feed = ("http://%s:8090" % host.split(":")[0])
    status = ("<span class='status'><span class='dot'></span>Çevrimiçi</span>"
              if host else "")
    feedpill = ("<a class='pill' href='%s/v1/feed' target='_blank' rel='noopener' "
                "data-copy='%s' title='Feed adresini kopyala'>Feed :8090</a>"
                % (feed, feed)) if feed else ""
    top = ("<div class='top'><div class='top-in'>"
           "<a class='brand' href='/' style='text-decoration:none;color:inherit'>"
           "%s<span>DuckStore<small>Arctian konsolu</small></span></a>"
           "<nav>%s</nav>%s%s"
           "<a class='btn sm ghost' href='/logout'>Çıkış</a>"
           "</div></div>") % (LOGO, "".join(items), feedpill, status)
    bg = current_bg()
    bgcss = "url('/bg/%s')" % html.escape(bg) if bg else "none"
    return ("<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta name='color-scheme' content='dark'>"
            "<title>%s</title><style>%s</style></head>"
            "<body style='--bg-img:%s'>%s<div class='wrap'>%s</div>"
            "<script>%s</script></body></html>"
            % (html.escape(title), CSS, bgcss, top, body, JS))


def page(title, body):
    return shell(title, body)


class Handler(BaseHTTPRequestHandler):
    server_version = "DuckStoreAdmin/1.0"

    # ---- ortak ----
    def _cookie_token(self):
        raw = self.headers.get("Cookie", "")
        try:
            c = SimpleCookie(raw)
            if "ds_session" in c:
                return c["ds_session"].value
        except Exception:
            pass
        return None

    def _authed(self):
        return session_valid(self._cookie_token())

    def _send(self, code, body, ctype="text/html; charset=utf-8", extra=None):
        data = body.encode("utf-8") if isinstance(body, str) else body
        extra = list(extra or [])
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("X-Content-Type-Options", "nosniff")
        if not any(k.lower() == "cache-control" for k, _ in extra):
            self.send_header("Cache-Control", "no-store")
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def _redirect(self, loc, cookie=None):
        self.send_response(302)
        self.send_header("Location", loc)
        if cookie:
            self.send_header("Set-Cookie", cookie)
        self.end_headers()

    def _read_form(self):
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length > MAX_UPLOAD + 1024 * 1024:
            raise ValueError("istek cok buyuk")
        return formdata.parse(self.rfile, self.headers, length)

    def _same_origin(self):
        host = self.headers.get("Host", "")
        origin = self.headers.get("Origin")
        if not origin:
            return True  # bazi tarayicilar form gonderiminde Origin koymaz
        try:
            from urllib.parse import urlparse
            return urlparse(origin).netloc == host
        except Exception:
            return False

    # ---- GET ----
    def _page(self, title, body, active=None):
        return shell(title, body, active, self.headers.get("Host", ""))

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/health":
            self._send(200, "ok\n", "text/plain; charset=utf-8")
            return
        if path.startswith("/bg/"):
            # arka plan gorseli (giris/kurulum sayfasinda da kullanilir)
            full = wall_file(path[len("/bg/"):])
            if not full:
                self._send(404, b"not found\n", "text/plain; charset=utf-8")
                return
            ext = os.path.splitext(full)[1].lower()
            ctype = {"png": "image/png", "jpg": "image/jpeg",
                     "jpeg": "image/jpeg", "webp": "image/webp"}.get(
                         ext[1:], "application/octet-stream")
            with open(full, "rb") as f:
                data = f.read()
            self._send(200, data, ctype,
                       extra=[("Cache-Control", "public, max-age=3600")])
            return
        if path == "/logout":
            drop_session(self._cookie_token())
            self._redirect("/", "ds_session=; Path=/; Max-Age=0")
            return
        if not admin_exists():
            self._send(200, self._page("Kurulum", self._setup_html()))
            return
        if not self._authed():
            if path == "/api/stats":
                self._send(401, "{}\n", "application/json; charset=utf-8")
                return
            if path == "/chart.svg":
                self._send(401, "yetkisiz\n", "text/plain; charset=utf-8")
                return
            self._send(200, self._page("Giriş", self._login_html()))
            return
        if path in ("/", "/admin"):
            self._send(200, self._page("Panel", self._admin_html()))
            return
        if path == "/apps":
            self._send(200, self._page("Uygulamalar", self._apps_html(), "apps"))
            return
        if path == "/walls":
            self._send(200, self._page("Duvar Kağıtları", self._walls_html(), "walls"))
            return
        if path == "/stats":
            self._send(200, self._page("İstatistikler", self._stats_html(), "stats"))
            return
        if path == "/review":
            self._review_page()
            return
        if path == "/tokens":
            self._tokens_page()
            return
        if path.startswith("/app/"):
            app_id = urllib.parse.unquote(path[len("/app/"):])
            self._send(200, self._page(
                "Uygulama", self._app_detail_html(app_id), "apps"))
            return
        if path.startswith("/shots/"):
            self._serve_shot(path)
            return
        if path == "/chart.svg":
            self._chart()
            return
        if path == "/api/stats":
            self._api_stats()
            return
        self._send(404, self._page(
            "Bulunamadi",
            "<div class='card'>Sayfa yok. <a href='/'>Panele dön</a></div>"))

    # ---- POST ----
    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if not self._same_origin():
            self._send(403, self._page("Hata",
                                       "<div class='card err'>Geçersiz istek.</div>"))
            return
        if path == "/setup":
            self._do_setup()
            return
        if path == "/login":
            self._do_login()
            return
        if not self._authed():
            self._redirect("/")
            return
        if path == "/upload":
            self._do_upload()
            return
        if path == "/delete":
            self._do_delete()
            return
        if path == "/review/approve":
            self._do_review_approve()
            return
        if path == "/review/reject":
            self._do_review_reject()
            return
        if path == "/tokens/revoke":
            self._do_token_revoke()
            return
        if path == "/app/meta":
            self._do_app_meta()
            return
        if path == "/app/shot":
            self._do_app_shot()
            return
        if path == "/app/shot/delete":
            self._do_app_shot_delete()
            return
        if path == "/ui":
            self._do_ui()
            return
        if path == "/wall/upload":
            self._do_wall_upload()
            return
        if path == "/wall/delete":
            self._do_wall_delete()
            return
        self._send(404, self._page("Bulunamadi", "<div class='card'>Yok.</div>"))

    # ---- sayfalar ----
    def _setup_html(self, err=""):
        errtxt = "<div class='err'>%s</div>" % e(err) if err else ""
        return """
<div class='auth'>
  <div class='card'>
    <div class='brandbig'>%s DuckStore</div>
    <div class='tag'>Arctian mağaza konsolu · ilk kurulum</div>
    %s
    <form method='post' action='/setup'>
      <label>Yönetici şifresi</label>
      <input type='password' name='pw' placeholder='En az 6 karakter' autofocus>
      <label>Şifre tekrar</label>
      <input type='password' name='pw2' placeholder='Şifreyi tekrar yazın'>
      <div style='margin-top:20px'>
        <button class='btn wide' type='submit'>Şifreyi Belirle</button>
      </div>
    </form>
    <p class='muted' style='margin-top:18px;text-align:center;line-height:1.5'>
      Bu şifreyle SSH kullanmadan uygulama ve arka plan yükleyebilirsiniz.</p>
  </div>
</div>""" % (LOGO, errtxt)

    def _login_html(self, err=""):
        errtxt = "<div class='err'>%s</div>" % e(err) if err else ""
        return """
<div class='auth'>
  <div class='card'>
    <div class='brandbig'>%s DuckStore</div>
    <div class='tag'>Yönetici girişi</div>
    %s
    <form method='post' action='/login'>
      <label>Şifre</label>
      <input type='password' name='pw' placeholder='Şifre' autofocus>
      <div style='margin-top:20px'>
        <button class='btn wide' type='submit'>Giriş Yap</button>
      </div>
    </form>
    <p class='muted' style='margin-top:18px;text-align:center'>
      Feed ve mağaza <a href='/health'>çevrimiçi</a> · panel oturumu 12 saat</p>
  </div>
</div>""" % (LOGO, errtxt)

    def _admin_html(self):
        pkgs = list_packages()
        walls = list_walls()
        st = stats_snapshot()
        feed = self._feed_origin()
        host_hdr = self.headers.get("Host", "SUNUCU_IP")
        host = host_hdr.split(":")[0]
        port = host_hdr.split(":")[1] if ":" in host_hdr else "129"

        tiles = self._tiles(st, len(pkgs), len(walls))
        recent = self._recent_html(st)
        table = self._pkg_table(pkgs, with_actions=True, limit=8)
        log = "\n".join(_last_log[-1:]) or "-"

        return """
<div class='page-h'>
  <div>
    <h1 class='t'>Genel Bakış</h1>
    <div class='sub'>Arctian uygulama mağazası ve arka plan deposu</div>
  </div>
  <div class='row'>
    <a class='btn ghost' href='/stats'>İstatistikler</a>
    <a class='btn' href='/apps'>Uygulamalar</a>
  </div>
</div>

<div class='grid g4'>%s</div>

<div class='card' style='margin-top:18px'>
  <div class='row' style='margin-bottom:6px'>
    <h3 style='margin:0'>Son %d gün indirme</h3>
    <span class='spacer'></span>
    <a class='pill' href='/stats'>Detay →</a>
  </div>
  <img class='chart' src='/chart.svg' alt='indirme grafigi'>
  <div class='legend'>
    <span><i style='background:#3b82f6'></i>Uygulama paketi</span>
    <span><i style='background:#8b5cf6'></i>Duvar kağıdı</span>
  </div>
</div>

<div class='grid g2'>
  <div class='card'>
    <h3>Uygulama yükle <span class='hint'>.wlf</span></h3>
    <p class='muted'>Hazır <code>.wlf</code> paketini yükleyin. Sunucu formatı ve bütünlüğü
       doğrular, magaza anahtarıyla yeniden imzalar ve anında yayına alır.
       Paketi <code>sdk/wlfpack.py</code> ile üretin.</p>
    <form method='post' action='/upload' enctype='multipart/form-data'>
      <div class='drop'><b>.wlf dosyasını buraya bırak</b>
        <span>veya tıklayıp seçin</span>
        <input type='file' name='wlf' accept='.wlf' required>
      </div>
      <label>Mağaza açıklaması (isteğe bağlı)</label>
      <textarea name='description' maxlength='4000'
        placeholder='Uygulama ne yapar?'></textarea>
      <label>Ekran görüntüleri (isteğe bağlı, en fazla 4)</label>
      <input type='file' name='shots' accept='image/*' multiple>
      <div class='row' style='margin-top:14px'>
        <button class='btn' type='submit'>Yükle ve Yayınla</button>
      </div>
    </form>
  </div>

  <div class='card'>
    <h3>Mağaza adresi</h3>
    <p class='muted'>Arctian'daki DuckStore bu feed'e bağlıdır (OS'a elle girilmesi gerekmez):</p>
    <div class='row'>
      <code style='flex:1'>%s</code>
      <button class='btn sm ghost' data-copy='%s'>Kopyala</button>
    </div>
    <div class='row' style='margin-top:16px'>
      <span class='pill'><span class='dot'></span>%s</span>
      <span class='pill'>Panel :%s</span>
      <a class='pill' href='%s' target='_blank' rel='noopener'>Herkese açık site :%d</a>
    </div>
    <h3 style='margin-top:22px'>Nasıl .wlf üretirim?</h3>
    <pre>python sdk/wlfpack.py --bytecode prog.wlfb \
    --json wolf.json -o uygulama.wlf</pre>
    <p class='muted'>Wolf bytecode (<code>wf build</code>) veya native
       <code>--bin</code> ile. İkona, sürüme, kategoriye <code>wolf.json</code>
       üzerinden karar verilir.</p>
  </div>
</div>

<div class='card'>
  <div class='row' style='margin-bottom:4px'>
    <h3 style='margin:0'>Yayındaki uygulamalar</h3>
    <span class='spacer'></span>
    <a class='pill' href='/apps'>Tümü (%d) →</a>
  </div>
  %s
</div>

<div class='grid g2'>
  <div class='card'>
    <h3>Son istekler</h3>
    %s
  </div>
  <div class='card'>
    <h3>Son işlem</h3>
    <pre>%s</pre>
  </div>
</div>
""" % (tiles, st.get("window", 14), e(feed), e(feed), e(host), e(port),
       e(self._market_origin()), MARKET_PORT,
       len(pkgs), table, recent, e(log))

    # ---- ortak parcalar ----
    def _feed_origin(self):
        host = self.headers.get("Host", "SUNUCU_IP").split(":")[0]
        return "http://%s:8090" % host

    def _market_origin(self):
        host = self.headers.get("Host", "SUNUCU_IP").split(":")[0]
        return "http://%s:%d" % (host, MARKET_PORT)

    def _tiles(self, st, n_apps=0, n_walls=0):
        t = st.get("totals", {}) or {}
        td = st.get("today", {}) or {}
        wk = st.get("week", {}) or {}
        src = st.get("source", "yok")
        badge = {"journal": "", "log": "", "yok": " c"}.get(src, "")
        out = [
            self._tile("", ICO["box"], str(n_apps), "Yayındaki uygulama"),
            self._tile(badge, ICO["dl"], _num(t.get("pkg", 0)),
                       "Toplam paket indirmesi"),
            self._tile("b", ICO["today"], _num(td.get("all", 0)),
                       "Bugünkü indirme"),
            self._tile("c", ICO["wall"], _num(t.get("wall", 0)),
                       "Duvar kağıdı indirmesi"),
        ]
        return "".join(out)

    def _tile(self, cls, ico, val, lbl, field=""):
        f = " data-f='%s'" % field if field else ""
        return ("<div class='tile %s'><div class='ico'>%s</div>"
                "<div class='val'%s>%s</div><div class='lbl'>%s</div></div>"
                % (cls, ico, f, val, lbl))

    def _recent_html(self, st):
        items = (st.get("recent") or [])[:9]
        if not items:
            return ("<p class='muted'>Henüz istek kaydı yok. İstatistikler, "
                    "feed sunucusunun loglarından okunur.</p>")
        li = []
        for it in items:
            code = int(it.get("s", 0))
            cls = "c3" if code == 200 else ("c4" if code < 500 else "")
            ts = str(it.get("t", ""))[5:16].replace("T", " ")
            li.append("<li><span class='chip %s'>%d</span>"
                      "<span class='grow trunc' title='%s'>%s</span>"
                      "<span class='meta'>%s</span></li>"
                      % (cls, code, e(it.get("p", "")), e(it.get("p", "")), e(ts)))
        return "<ul class='list'>%s</ul>" % "".join(li)

    def _pkg_table(self, pkgs, with_actions=True, limit=None):
        if not pkgs:
            return ("<p class='muted'>Henüz yayında uygulama yok. Yukarıdan "
                    "bir .wlf yükleyerek başlayın.</p>")
        rows = []
        for a in (pkgs[:limit] if limit else pkgs):
            cat_cls = {"Internet": "c2", "Araçlar": "c3",
                       "Araclar": "c3", "Oyun": "c4"}.get(a["category"], "")
            nshots = len(a.get("shots") or [])
            shot_note = (" · %d foto" % nshots) if nshots else ""
            act = ("<td class='num'><a class='btn sm ghost' href='/app/%s'>Düzenle</a> "
                   % e(a["id"]))
            if with_actions:
                act += ("<form method='post' action='/delete' "
                        "onsubmit=\"return confirm('%s yayindan silinsin mi?')\" "
                        "style='margin:0;display:inline-block;margin-left:6px'>"
                        "<input type='hidden' name='id' value='%s'>"
                        "<button class='btn sm danger' type='submit'>Sil</button>"
                        "</form></td>" % (e(a["id"]), e(a["id"])))
            else:
                act += "</td>"
            rows.append(
                "<tr><td><div class='appcell'><span class='icon'>%s</span>"
                "<span><span class='ttl'>%s</span><br>"
                "<span class='meta'>%s · %s%s</span></span></div></td>"
                "<td><span class='chip %s'>%s</span></td>"
                "<td>%s</td><td class='num'>%s</td>"
                "<td class='num'><span class='ttl'>%s</span></td>%s</tr>"
                % (icon_html(a), e(a["title"]), e(a["id"]), e(a["publisher"]),
                   shot_note, cat_cls, e(a["category"]), e(a["version"]),
                   fmt_bytes(a["size"]), _num(a.get("downloads", 0)), act))
        head = ("<thead><tr><th>Uygulama</th><th>Kategori</th><th>Sürüm</th>"
                "<th class='num'>Boyut</th><th class='num'>İndirme</th>%s</tr>"
                "</thead>" % ("<th class='num'></th>" if with_actions else ""))
        return "<div class='tw'><table>%s<tbody>%s</tbody></table></div>" % (
            head, "".join(rows))

    def _apps_html(self):
        pkgs = list_packages()
        total_size = sum(a["size"] for a in pkgs)
        total_dl = sum(a.get("downloads", 0) for a in pkgs)
        head = """
<div class='page-h'>
  <div>
    <h1 class='t'>Uygulamalar</h1>
    <div class='sub'>%d paket · %s · %s toplam indirme</div>
  </div>
  <div class='row'>
    <a class='btn ghost' href='/'>Panel</a>
    <a class='btn' href='#yukle'>Yeni yükle</a>
  </div>
</div>""" % (len(pkgs), fmt_bytes(total_size), _num(total_dl))
        table = ("<div class='card tight'>%s</div>"
                 % self._pkg_table(pkgs, with_actions=True))
        up = """
<div class='card' id='yukle'>
  <h3>Yeni uygulama yükle <span class='hint'>.wlf</span></h3>
  <form method='post' action='/upload' enctype='multipart/form-data'>
    <div class='drop'><b>.wlf dosyasını buraya bırak</b>
      <span>veya tıklayıp seçin</span>
      <input type='file' name='wlf' accept='.wlf' required>
    </div>
    <label>Mağaza açıklaması (isteğe bağlı)</label>
    <textarea name='description' maxlength='4000'
      placeholder='Uygulama ne yapar?'></textarea>
    <label>Ekran görüntüleri (isteğe bağlı, en fazla 4)</label>
    <input type='file' name='shots' accept='image/*' multiple>
    <div class='row' style='margin-top:14px'>
      <button class='btn' type='submit'>Yükle ve Yayınla</button>
      <span class='muted'>Paket doğrulanır ve magaza anahtarıyla yeniden imzalanır.</span>
    </div>
  </form>
</div>"""
        return head + table + up

    def _walls_html(self):
        walls = list_walls()
        st = stats_snapshot()
        active = current_bg()
        feed = self._feed_origin()
        dlmap = {f["name"]: f for f in (st.get("files") or [])}
        cards = []
        for w in walls:
            on = " on" if w["file"] == active else ""
            cur = " (aktif)" if w["file"] == active else ""
            n = dlmap.get(w["file"], {}).get("n", 0)
            cards.append(
                "<div class='wcard%s'>"
                "<div class='shot' style=\"background-image:url('/bg/%s')\"></div>"
                "<div class='bd'>"
                "<div class='nm'>%s%s</div>"
                "<div class='meta'>%s · %s</div>"
                "<div class='meta' style='margin-top:5px'>%s indirme</div>"
                "<div class='acts'>"
                "<form method='post' action='/ui' style='margin:0'>"
                "<input type='hidden' name='bg' value='%s'>"
                "<button class='btn sm%s' type='submit'>%s</button></form>"
                "<a class='btn sm ghost' href='%s/wallpapers/%s' target='_blank'"
                " rel='noopener'>Önizle</a>"
                "<form method='post' action='/wall/delete' style='margin:0'"
                " onsubmit=\"return confirm('%s silinsin mi?')\">"
                "<input type='hidden' name='file' value='%s'>"
                "<button class='btn sm danger' type='submit'>Sil</button>"
                "</form></div></div></div>"
                % (on, e(w["file"]), e(w["title"]), e(cur), e(w["author"]),
                   fmt_bytes(w["size"]), _num(n), e(w["file"]),
                   "" if w["file"] == active else " ghost",
                   "Kullanılıyor" if w["file"] == active else "Arkapan yap",
                   e(feed), e(w["file"]), e(w["file"]), e(w["file"])))
        if not cards:
            cards.append("<div class='card'><p class='muted'>Henüz duvar kağıdı "
                         "yok. Aşağıdan PNG yükleyin.</p></div>")

        head = """
<div class='page-h'>
  <div>
    <h1 class='t'>Duvar Kağıtları</h1>
    <div class='sub'>%d arka plan · panelin ve OS'un arka plan kaynağı</div>
  </div>
  <div class='row'>
    <a class='btn ghost' href='/'>Panel</a>
    <a class='btn' href='#yukle'>Yeni arka plan</a>
  </div>
</div>""" % len(walls)

        toggle = """
<div class='row' style='margin-bottom:16px'>
  <span class='muted'>Panel arka planı:</span>
  <form method='post' action='/ui' style='margin:0'>
    <input type='hidden' name='bg' value='none'>
    <button class='btn sm ghost' type='submit'>Arka planı kapat</button>
  </form>
  <span class='pill'>seçili: <b>%s</b></span>
</div>""" % (e(active) if active else "yok")

        up = """
<div class='card' id='yukle'>
  <h3>Yeni arka plan yükle <span class='hint'>PNG / JPG / WEBP · en fazla 6 MB</span></h3>
  <p class='muted'>Yükleme, feed sunucusunun <code>/wallpapers/</code> dizinine yazılır;
     Arctian'daki duvar kağıdı listesine anında düşer. Panel arka planı olarak da
     kullanılabilir.</p>
  <form method='post' action='/wall/upload' enctype='multipart/form-data'>
    <div class='drop'><b>Görseli buraya bırak</b>
      <span>veya tıklayıp seçin</span>
      <input type='file' name='file' accept='.png,.jpg,.jpeg,.webp' required>
    </div>
    <div class='grid g3' style='margin-top:14px;gap:12px'>
      <div><label>Başlık</label><input type='text' name='title'
            placeholder='Örn. Gece Sisleri' maxlength='60'></div>
      <div><label>Çizen / kaynağı</label><input type='text' name='author'
            placeholder='Arctian' maxlength='40'></div>
      <div><label>Dosya adı (isteğe bağlı)</label><input type='text' name='name'
            placeholder='ornek' maxlength='40'></div>
    </div>
    <div class='row' style='margin-top:14px'>
      <button class='btn' type='submit'>Yükle ve Yayınla</button>
      <span class='muted'>Yüklemeden sonra "Arkapan yap" ile panelde kullanın.</span>
    </div>
  </form>
</div>"""
        gal = "<div class='gal'>%s</div>" % "".join(cards)
        return head + toggle + gal + up

    def _stats_html(self):
        st = stats_snapshot()
        files = st.get("files") or []
        t = st.get("totals", {}) or {}
        td = st.get("today", {}) or {}
        wk = st.get("week", {}) or {}
        src = st.get("source", "yok")
        err = st.get("error")

        badge = {"journal": "<span class='chip c3'>journal</span>",
                 "log": "<span class='chip c4'>log dosyası</span>",
                 "yok": "<span class='chip'>veri yok</span>"}.get(
                     src, "<span class='chip'>%s</span>" % e(src))
        if err:
            note = ("<div class='err'>İstatistik kaynağı okunamadı: %s<br>"
                    "<span class='muted'>Çözüm: <code>usermod -aG systemd-journal "
                    "duckstore</code> çalıştırıp servisi yeniden başlatın, ya da "
                    "<code>journalctl -u duckstore-feed</code> erişimini verin.</span>"
                    "</div>" % e(err))
        else:
            note = ""

        tiles = "".join([
            self._tile("", ICO["dl"], _num(t.get("pkg", 0)),
                       "Toplam paket indirmesi", "totals.pkg"),
            self._tile("b", ICO["today"], _num(td.get("all", 0)),
                       "Bugün", "today.all"),
            self._tile("b", ICO["week"], _num(wk.get("all", 0)),
                       "Son 7 gün", "week.all"),
            self._tile("c", ICO["wall"], _num(t.get("wall", 0)),
                       "Duvar kağıdı indirmesi", "totals.wall"),
            self._tile("c", ICO["feed"], _num(t.get("feed", 0)),
                       "Feed isteği", "totals.feed"),
            self._tile("", ICO["disk"], fmt_bytes(t.get("bytes", 0)),
                       "Aktarılan veri (tahmini)", "totals.bytes"),
        ])

        maxn = max([f.get("n", 0) for f in files] + [1])
        rows = []
        for i, f in enumerate(files[:20]):
            pct = int(100 * f.get("n", 0) / maxn)
            kind = ("duvar kağıdı" if f.get("kind") == "wall" else "paket")
            rows.append(
                "<tr><td class='num'><span class='rk'>%d</span></td>"
                "<td><div class='appcell'><span class='grow'>"
                "<span class='ttl'>%s</span><br><span class='meta'>%s</span></span></div></td>"
                "<td class='num'>%s</td>"
                "<td class='num'>%s</td>"
                "<td class='num'>%s</td>"
                "<td class='num meta'>%s</td>"
                "<td style='width:120px'><div class='bar-mini'><i style='width:%d%%'>"
                "</i></div></td></tr>"
                % (i + 1, e(f["name"]), e(kind), _num(f.get("n", 0)),
                   _num(f.get("today", 0)), fmt_bytes(f.get("bytes", 0)),
                   e(str(f.get("last", ""))[5:16].replace("T", " ")), pct))
        ftable = ("<div class='tw'><table><thead><tr><th class='num'>#</th>"
                  "<th>Dosya</th><th class='num'>Toplam</th>"
                  "<th class='num'>Bugün</th><th class='num'>Veri</th>"
                  "<th class='num'>Son indirme</th><th></th></tr></thead>"
                  "<tbody>%s</tbody></table></div>" % "".join(rows)) if rows else (
                      "<p class='muted'>Henüz indirme kaydı yok.</p>")

        head = """
<div class='page-h' data-live>
  <div>
    <h1 class='t'>İstatistikler</h1>
    <div class='sub'>Feed sunucusu loglarından · %s · son güncelleme
      <span data-f='updated'>%s</span></div>
  </div>
  <div class='row'>
    <span class='pill'>15 sn'de bir yenilenir</span>
    <button class='btn sm ghost' id='now'>Şimdi yenile</button>
  </div>
</div>""" % (badge, e(time.strftime("%H:%M:%S", time.localtime(st.get("updated", time.time())))))

        return (note + "<div class='grid g4'>" + tiles + "</div>" + """
<div class='card' style='margin-top:18px'>
  <div class='row' style='margin-bottom:6px'>
    <h3 style='margin:0'>Günlük indirme <span class='hint'>son %d gün</span></h3>
    <span class='spacer'></span>
    <span class='muted'>bugün: <b data-f='today.all'>%s</b> · hafta: <b data-f='week.all'>%s</b></span>
  </div>
  <img class='chart' id='chart' src='/chart.svg' alt='gunluk indirme grafigi'>
  <div class='legend'>
    <span><i style='background:#3b82f6'></i>Uygulama paketi</span>
    <span><i style='background:#8b5cf6'></i>Duvar kağıdı</span>
  </div>
</div>
<div class='card'>
  <div class='row' style='margin-bottom:4px'>
    <h3 style='margin:0'>Dosya bazında indirme</h3>
    <span class='spacer'></span>
    <span class='muted'>toplam <b data-f='totals.pkg'>%s</b> paket ·
      <b data-f='totals.wall'>%s</b> duvar kağıdı</span>
  </div>
  %s
</div>
<div class='grid g2'>
  <div class='card'>
    <h3>Son istekler</h3>
    <div data-f='recent'>%s</div>
  </div>
  <div class='card'>
    <h3>Nasıl ölçülüyor?</h3>
    <p class='muted'>Sayılar, <code>duckstore-feed</code> servisinin istek
      günlüklerinden (systemd journal) okunur. Feed sunucusu koduna dokunulmaz;
      sayaçlar panel tarafında tutulur ve <code>data/stats.json</code> dosyasında
      saklanır.</p>
    <ul class='list'>
      <li><span class='grow'>Kaynak</span><span data-f='source'>%s</span></li>
      <li><span class='grow'>Başarısız istek (404 vb.)</span>
          <b data-f='totals.failed'>%s</b></li>
      <li><span class='grow'>Feed isteği (/v1/*)</span>
          <b data-f='totals.feed'>%s</b></li>
      <li><span class='grow'>Ölçüm penceresi</span><b>%d gün</b></li>
    </ul>
  </div>
</div>
<script>
(function(){
  function set(f,v){
    var n=document.querySelectorAll("[data-f='"+f+"']");
    for(var i=0;i<n.length;i++) n[i].textContent=v;
  }
  function num(x){ return (x||0).toLocaleString("tr-TR"); }
  function bytes(b){
    b=Number(b)||0; var u=["B","KB","MB","GB"],i=0;
    while(b>=1024&&i<3){b/=1024;i++;}
    return (i===0?num(Math.round(b)):(b.toFixed(1).replace(".",",")+" "+u[i]));
  }
  function render(s){
    set("totals.pkg",num(s.totals.pkg)); set("totals.wall",num(s.totals.wall));
    set("totals.feed",num(s.totals.feed)); set("totals.failed",num(s.totals.failed));
    set("totals.bytes",bytes(s.totals.bytes));
    set("today.all",num(s.today.all)); set("week.all",num(s.week.all));
    set("updated",new Date().toLocaleTimeString("tr-TR"));
    var c=document.getElementById("chart");
    if(c) c.src="/chart.svg?t="+Date.now();
    var r=document.querySelector("[data-f='recent']");
    if(r&&s.recent&&s.recent.length){
      var h="";
      for(var i=0;i<Math.min(9,s.recent.length);i++){
        var it=s.recent[i], code=Number(it.s);
        var cls=code===200?"c3":(code<500?"c4":"");
        var ts=String(it.t||"").slice(5,16).replace("T"," ");
        h+="<li><span class='chip "+cls+"'>"+code+"</span>"+
           "<span class='grow trunc' title='"+it.p+"'>"+it.p+"</span>"+
           "<span class='meta'>"+ts+"</span></li>";
      }
      r.innerHTML="<ul class='list'>"+h+"</ul>";
    }
  }
  function load(){
    fetch("/api/stats",{credentials:"same-origin"})
      .then(function(r){return r.json();}).then(render).catch(function(){});
  }
  var t=setInterval(load,15000);
  var b=document.getElementById("now");
  if(b) b.addEventListener("click",load);
})();
</script>""" % (st.get("window", 14), _num(td.get("all", 0)), _num(wk.get("all", 0)),
               _num(t.get("pkg", 0)), _num(t.get("wall", 0)), ftable,
               self._recent_html(st), badge, _num(t.get("failed", 0)),
               _num(t.get("feed", 0)), st.get("window", 14)))

    def _api_stats(self):
        st = stats_snapshot()
        self._send(200, json.dumps(st, ensure_ascii=False),
                   "application/json; charset=utf-8")

    def _chart(self):
        st = stats_snapshot()
        self._send(200, chart_svg(st.get("days") or []),
                   "image/svg+xml; charset=utf-8",
                   extra=[("Cache-Control", "no-cache")])

    # ---- islemler ----
    def _do_setup(self):
        if admin_exists():
            self._redirect("/")
            return
        fs = self._read_form()
        pw = fs.getvalue("pw", "")
        pw2 = fs.getvalue("pw2", "")
        if len(pw) < 6 or pw != pw2:
            self._send(200, self._page(
                "Kurulum",
                self._setup_html("Şifreler uyuşmuyor ya da çok kısa (en az 6 karakter).")))
            return
        salt = secrets.token_bytes(16).hex()
        save_json(ADMIN_FILE, {"salt": salt, "hash": hash_pw(pw, salt),
                               "created": int(time.time())})
        tok = new_session()
        self._redirect("/admin", "ds_session=%s; Path=/; HttpOnly; SameSite=Lax" % tok)

    def _do_login(self):
        fs = self._read_form()
        pw = fs.getvalue("pw", "")
        if not verify_login(pw):
            time.sleep(0.6)
            self._send(200, self._page("Giriş", self._login_html("Hatalı şifre.")))
            return
        tok = new_session()
        self._redirect("/admin", "ds_session=%s; Path=/; HttpOnly; SameSite=Lax" % tok)

    def _do_upload(self):
        """Ham .wlf yukler: dogrula -> magaza anahtariyla imzala -> yayina al."""
        fs = self._read_form()
        if "wlf" not in fs:
            self._send(400, self._page("Hata", "<div class='card err'>Dosya yok (.wlf gerekli).</div>"))
            return
        data = fs["wlf"].file.read(MAX_UPLOAD + 1)
        if len(data) > MAX_UPLOAD:
            self._send(400, self._page("Hata", "<div class='card err'>Dosya çok büyük.</div>"))
            return
        try:
            with _lock:
                info = wlflib.publish(PACKAGES, data)
            desc = (fs.getvalue("description", "") or "").strip()
            storemeta.set_app(DATA, info["id"], description=desc)
            # opsiyonel ekran fotograflari
            n = 0
            for item in fs.getlist("shots"):
                try:
                    storemeta.add_shot(DATA, info["id"],
                                       item.file.read(4 * 1024 * 1024 + 1))
                    n += 1
                except storemeta.MetaError:
                    pass
            _last_log.append("Yayımlandı: %s v%s (%d bayt, %d foto)" %
                             (info["id"], info["version"], info["size"], n))
        except wlflib.WlfError as ex:
            _last_log.append("HATA: %s" % ex)
            self._send(200, self._page("Sonuc",
                "<div class='card err'><b>Paket reddedildi:</b><pre>%s</pre></div>"
                "<p><a href='/apps'>Geri</a></p>" % e(ex)))
            return
        except Exception as ex:
            _last_log.append("HATA: %s" % ex)
            self._send(200, self._page("Sonuc",
                "<div class='card err'><b>Yüklenemedi:</b><pre>%s</pre></div>"
                "<p><a href='/apps'>Geri</a></p>" % e(ex)))
            return
        self._redirect("/apps")

    # ---- magaza inceleme kuyrugu + jetonlar ----
    def _review_page(self):
        try:
            subs = market.list_submissions()
        except Exception as ex:
            self._send(500, self._page("İnceleme",
                                       "<div class='card err'>%s</div>" % e(ex)))
            return
        pend = [s for s in subs if s["status"] == "pending"]
        done = [s for s in subs if s["status"] != "pending"][:15]

        def kb(n):
            try:
                return "%.0f KB" % (float(n) / 1024.0)
            except (TypeError, ValueError):
                return "-"

        if pend:
            rows = []
            for s in pend:
                who = s.get("uploader_email") or s.get("uploader_name") or "-"
                rows.append(
                    "<tr><td><strong>%s</strong>"
                    "<div class='muted' style='font-size:12px'>%s · v%s · %s · %s</div></td>"
                    "<td><div style='font-size:13px'>%s</div>"
                    "<div class='muted' style='font-size:12px'>%s</div></td>"
                    "<td class='num' style='white-space:nowrap'>"
                    "<form method='post' action='/review/approve' style='display:inline'>"
                    "<input type='hidden' name='sid' value='%s'>"
                    "<button class='btn sm' type='submit'>Onayla</button></form></td>"
                    "<td style='white-space:nowrap'>"
                    "<form method='post' action='/review/reject' style='display:inline'>"
                    "<input type='hidden' name='sid' value='%s'>"
                    "<input name='reason' maxlength='200' placeholder='gerekçe' "
                    "style='width:130px;margin-right:6px'>"
                    "<button class='btn sm danger' type='submit'>Reddet</button></form></td></tr>"
                    % (e(s.get("title") or s.get("id")), e(s.get("id")),
                       e(s.get("version", "?")), e(s.get("category", "-")),
                       kb(s.get("size")),
                       e(who),
                       time.strftime("%d.%m.%Y %H:%M",
                                     time.localtime(s.get("submitted_at", 0))),
                       e(s["sid"]), e(s["sid"])))
            ptable = ("<table><thead><tr><th>Başvuru</th><th>Gönderen</th>"
                      "<th class='num'></th><th></th></tr></thead>"
                      "<tbody>%s</tbody></table>" % "".join(rows))
        else:
            ptable = "<p class='muted'>Bekleyen başvuru yok. 🎉</p>"

        if done:
            drows = []
            for s in done:
                ok = s["status"] == "approved"
                badge = ("<span class='chip c3'>onaylandı</span>" if ok
                         else "<span class='chip'>reddedildi</span>")
                note = s.get("rejection_reason") or ""
                drows.append(
                    "<tr><td><strong>%s</strong>"
                    "<div class='muted' style='font-size:12px'>%s · v%s</div></td>"
                    "<td>%s</td>"
                    "<td class='muted' style='font-size:12px'>%s%s</td></tr>"
                    % (e(s.get("title") or s.get("id")), e(s.get("id")),
                       e(s.get("version", "?")), badge,
                       time.strftime("%d.%m.%Y",
                                     time.localtime(s.get("reviewed_at", 0))),
                       (" — " + e(note)) if note else ""))
            dtable = ("<table><thead><tr><th>Başvuru</th><th>Durum</th>"
                      "<th>İnceleme</th></tr></thead><tbody>%s</tbody></table>"
                      % "".join(drows))
        else:
            dtable = "<p class='muted'>Henüz incelenen başvuru yok.</p>"

        body = """
<div class='page-h'><div>
  <h1 class='t'>İnceleme Kuyruğu</h1>
  <div class='sub'>Arctian Market başvuruları — onaylanan paketler mağaza anahtarıyla
  imzalanıp yayına alınır</div>
</div><div class='row'>
  <span class='pill'>%d bekleyen</span>
  <a class='btn ghost' href='/tokens'>API jetonları</a>
</div></div>
<div class='card'>%s</div>
<h3 style='margin:22px 0 10px'>Son incelenenler</h3>
<div class='card'>%s</div>""" % (len(pend), ptable, dtable)
        self._send(200, self._page("İnceleme", body, "review"))

    def _do_review_approve(self):
        fs = self._read_form()
        sid = fs.getvalue("sid", "")
        try:
            subm, err = market.approve_submission(sid, "panel")
        except Exception as ex:
            subm, err = None, str(ex)
        if err:
            self._send(400, self._page(
                "İnceleme", "<div class='card err'>Onaylanamadı: %s</div>"
                "<p><a href='/review'>Kuyruğa dön</a></p>" % e(err), "review"))
            return
        _last_log.append("Onaylandı: %s v%s" % (subm["id"], subm["version"]))
        self._redirect("/review")

    def _do_review_reject(self):
        fs = self._read_form()
        sid = fs.getvalue("sid", "")
        reason = fs.getvalue("reason", "") or ""
        try:
            subm, _err = market.reject_submission(sid, "panel", reason)
        except Exception:
            subm = None
        if subm:
            _last_log.append("Reddedildi: %s" % subm.get("id", sid))
        self._redirect("/review")

    def _tokens_page(self):
        try:
            toks = tokenstore.list_all()
        except Exception as ex:
            self._send(500, self._page("Jetonlar",
                                       "<div class='card err'>%s</div>" % e(ex)))
            return
        active = [t for t in toks if not t["revoked"]]
        if toks:
            rows = []
            for t in toks:
                state = ("<span class='chip'>iptal</span>" if t["revoked"]
                         else "<span class='chip c3'>aktif</span>")
                act = ("<form method='post' action='/tokens/revoke' style='display:inline'>"
                       "<input type='hidden' name='tid' value='%s'>"
                       "<button class='btn sm danger' type='submit'>İptal et</button></form>"
                       % e(t["id"])) if not t["revoked"] else ""
                rows.append(
                    "<tr><td><code>%s</code>"
                    "<div class='muted' style='font-size:12px'>%s</div></td>"
                    "<td style='font-size:13px'>%s<div class='muted' "
                    "style='font-size:12px'>%s</div></td>"
                    "<td>%s</td>"
                    "<td class='muted' style='font-size:12px;white-space:nowrap'>%s"
                    "<div>son: %s</div></td>"
                    "<td style='white-space:nowrap'>%s</td></tr>"
                    % (e(t["masked"]), e(t["label"]),
                       e(t.get("name") or "-"), e(t.get("email") or "-"),
                       state,
                       time.strftime("%d.%m.%y %H:%M",
                                     time.localtime(t.get("created_at", 0))),
                       (time.strftime("%d.%m.%y %H:%M",
                                      time.localtime(t["last_used"]))
                        if t.get("last_used") else "-"),
                       act))
            table = ("<table><thead><tr><th>Jeton</th><th>Kullanıcı</th>"
                     "<th>Durum</th><th>Oluşturma</th><th></th></tr></thead>"
                     "<tbody>%s</tbody></table>" % "".join(rows))
        else:
            table = ("<p class='muted'>Henüz jeton üretilmedi. Geliştiriciler "
                     "<a href='https://market.kerem.site/console' target='_blank' "
                     "rel='noopener'>market.kerem.site/console</a> adresinden üretir.</p>")

        body = """
<div class='page-h'><div>
  <h1 class='t'>API Jetonları</h1>
  <div class='sub'>ArctianMCP üzerinden yayın yapan AI ajanlarının erişim jetonları
  (yalnızca özetleri saklanır)</div>
</div><div class='row'><span class='pill'>%d aktif</span></div></div>
<div class='card'>%s</div>""" % (len(active), table)
        self._send(200, self._page("Jetonlar", body, "tokens"))

    def _do_token_revoke(self):
        fs = self._read_form()
        tid = fs.getvalue("tid", "")
        d = tokenstore._load()
        for h, v in list(d.items()):
            if v.get("id") == tid:
                v["revoked"] = True
                d[h] = v
                tokenstore._save(d)
                _last_log.append("Jeton iptal edildi: %s" % v.get("masked", tid))
                break
        self._redirect("/tokens")

    def _do_delete(self):
        fs = self._read_form()
        app_id = fs.getvalue("id", "")
        if not _ID_RE.match(app_id):
            self._redirect("/apps")
            return
        with _lock:
            try:
                os.remove(os.path.join(PACKAGES, app_id + ".wlf"))
            except OSError:
                pass
            storemeta.drop_app(DATA, app_id)
        _last_log.append("Silindi: %s" % app_id)
        self._redirect("/apps")

    # ---- uygulama yan-veri (aciklama + fotograflar) ----
    def _serve_shot(self, path):
        rest = path[len("/shots/"):]
        parts = rest.split("/", 1)
        if len(parts) != 2:
            self._send(404, b"not found\n", "text/plain; charset=utf-8")
            return
        full = storemeta.shot_path(DATA, parts[0], parts[1])
        if not full:
            self._send(404, b"not found\n", "text/plain; charset=utf-8")
            return
        with open(full, "rb") as f:
            head = f.read(16)
        try:
            _, ctype = storemeta.sniff_image(head)
        except storemeta.MetaError:
            ctype = "application/octet-stream"
        with open(full, "rb") as f:
            self._send(200, f.read(), ctype,
                       extra=[("Cache-Control", "public, max-age=3600")])

    def _app_detail_html(self, app_id, err="", ok=""):
        apps = {a["id"]: a for a in list_packages()}
        a = apps.get(app_id)
        if not a:
            return "<div class='card'>Bu uygulama bulunamadı. <a href='/apps'>Geri</a></div>"
        msg = ""
        if err:
            msg = "<div class='err'>%s</div>" % e(err)
        if ok:
            msg = "<div class='ok'>%s</div>" % e(ok)
        shots = storemeta.list_shots(DATA, app_id)
        shot_cells = "".join(
            "<div class='wcard'><div class='shot' style=\"background-image:url('/shots/%s/%s')\"></div>"
            "<div class='bd'><div class='meta'>%s · %s</div>"
            "<div class='acts'><form method='post' action='/app/shot/delete' style='margin:0' "
            "onsubmit=\"return confirm('Fotoğraf silinsin mi?')\">"
            "<input type='hidden' name='id' value='%s'>"
            "<input type='hidden' name='file' value='%s'>"
            "<button class='btn sm danger' type='submit'>Sil</button></form></div></div></div>"
            % (e(app_id), e(s["file"]), e(s["file"]), fmt_bytes(s["size"]),
               e(app_id), e(s["file"]))
            for s in shots)
        if not shot_cells:
            shot_cells = "<p class='muted'>Henüz ekran görüntüsü yok.</p>"
        return """
<div class='page-h'>
  <div><h1 class='t'>%s</h1><div class='sub'>%s · v%s · %s</div></div>
  <div class='row'><a class='btn ghost' href='/apps'>Uygulamalar</a>
    <a class='btn ghost' href='/packages/%s.wlf'>.wlf indir</a></div>
</div>
<div class='grid g2'>
  <div class='card'>
    <div class='appcell' style='margin-bottom:14px'><span class='icon'>%s</span>
      <span><span class='ttl'>%s</span><br><span class='meta'>%s</span></span></div>
    %s
    <h3>Mağaza açıklaması</h3>
    <form method='post' action='/app/meta'>
      <input type='hidden' name='id' value='%s'>
      <textarea name='description' maxlength='4000' placeholder='Uygulama ne yapar?'>%s</textarea>
      <div class='row' style='margin-top:12px'>
        <button class='btn' type='submit'>Kaydet</button>
        <span class='muted'>Magaza sitesinde ve OS feed'inde görünür.</span>
      </div>
    </form>
  </div>
  <div class='card'>
    <h3>Ekran görüntüleri <span class='hint'>en fazla 6</span></h3>
    <div class='gal' style='margin-bottom:14px'>%s</div>
    <form method='post' action='/app/shot' enctype='multipart/form-data'>
      <input type='hidden' name='id' value='%s'>
      <div class='drop'><b>Görselleri buraya bırak</b><span>PNG / JPG / WEBP · çoklu seçim</span>
        <input type='file' name='shots' accept='image/*' multiple required></div>
      <div class='row' style='margin-top:12px'>
        <button class='btn' type='submit'>Yükle</button></div>
    </form>
  </div>
</div>""" % (e(a["title"]), e(a["id"]), e(a["version"]),
             fmt_bytes(a["size"]), e(a["id"]), icon_html(a), e(a["title"]),
             e(a["publisher"]), msg, e(app_id),
             e(a.get("description", "")), shot_cells, e(app_id))

    def _do_app_meta(self):
        fs = self._read_form()
        app_id = fs.getvalue("id", "")
        if not _ID_RE.match(app_id):
            self._redirect("/apps")
            return
        try:
            storemeta.set_app(DATA, app_id,
                              description=fs.getvalue("description", ""))
            _last_log.append("Açıklama güncellendi: %s" % app_id)
        except storemeta.MetaError as ex:
            self._send(200, self._page("Hata", self._app_detail_html(
                app_id, err=str(ex))))
            return
        self._redirect("/app/" + app_id)

    def _do_app_shot(self):
        fs = self._read_form()
        app_id = fs.getvalue("id", "")
        if not _ID_RE.match(app_id):
            self._redirect("/apps")
            return
        err = ""
        n = 0
        for item in fs.getlist("shots"):
            try:
                storemeta.add_shot(DATA, app_id,
                                   item.file.read(4 * 1024 * 1024 + 1))
                n += 1
            except storemeta.MetaError as ex:
                err = str(ex)
                break
        if err:
            self._send(200, self._page(
                "Hata", self._app_detail_html(app_id, err=err)))
            return
        _last_log.append("Fotoğraf eklendi: %s (+%d)" % (app_id, n))
        self._redirect("/app/" + app_id)

    def _do_app_shot_delete(self):
        fs = self._read_form()
        app_id = fs.getvalue("id", "")
        fn = fs.getvalue("file", "")
        if _ID_RE.match(app_id):
            storemeta.drop_shot(DATA, app_id, fn)
        self._redirect("/app/" + app_id)

    # ---- arka plan / panel tercihleri ----
    def _do_ui(self):
        fs = self._read_form()
        bg = (fs.getvalue("bg", "") or "").strip()
        pref = ui_prefs()
        if bg == "none":
            pref["bg"] = "none"
        elif bg and wall_file(bg):
            pref["bg"] = os.path.basename(bg)
        else:
            pref["bg"] = ""
        save_ui(pref)
        self._redirect("/walls")

    def _do_wall_upload(self):
        MAX_WALL = 6 * 1024 * 1024
        fs = self._read_form()
        if "file" not in fs:
            self._send(400, self._page("Hata",
                                       "<div class='err'>Dosya seçilmedi.</div>"))
            return
        data = fs["file"].file.read(MAX_WALL + 1)
        if len(data) > MAX_WALL:
            self._send(400, self._page(
                "Hata", "<div class='err'>Dosya çok büyük (en fazla 6 MB).</div>"))
            return
        if data[:8] == b"\x89PNG\r\n\x1a\n":
            ext = ".png"
        elif data[:3] == b"\xff\xd8\xff":
            ext = ".jpg"
        elif data[:4] == b"RIFF" and data[8:12] == b"WEBP":
            ext = ".webp"
        else:
            self._send(400, self._page(
                "Hata", "<div class='err'>Geçersiz görsel: PNG, JPG veya WEBP "
                        "olmalı.</div>"))
            return
        orig = os.path.basename(getattr(fs["file"], "filename", "") or "arkaplan")
        stem = re.sub(r"[^a-z0-9_]+", "_",
                      (fs.getvalue("name", "") or "").strip().lower()).strip("_")
        if not stem:
            stem = re.sub(r"[^a-z0-9_]+", "_",
                          os.path.splitext(orig)[0].lower()).strip("_")
        if not stem:
            stem = "arkaplan"
        stem = stem[:40]
        title = (fs.getvalue("title", "") or "").strip()[:60] or stem.replace("_", " ").title()
        author = (fs.getvalue("author", "") or "").strip()[:40] or "Arctian"

        try:
            os.makedirs(WALLS_DIR, exist_ok=True)
            fn = stem + ext
            i = 1
            while os.path.exists(os.path.join(WALLS_DIR, fn)):
                fn = "%s_%d%s" % (stem, i, ext)
                i += 1
            tmp = os.path.join(WALLS_DIR, fn + ".part")
            with open(tmp, "wb") as f:
                f.write(data)
            os.replace(tmp, os.path.join(WALLS_DIR, fn))
            mf = os.path.join(WALLS_DIR, "wallpapers.json")
            meta = load_json(mf, {})
            if not isinstance(meta, dict):
                meta = {}
            meta[os.path.splitext(fn)[0]] = {"title": title, "author": author}
            save_json(mf, meta)
        except Exception as ex:
            self._send(500, self._page(
                "Hata", "<div class='err'>Yüklenemedi: %s</div>" % e(ex)))
            return
        _last_log.append("Arka plan yüklendi: %s (%s, %s)" % (fn, title, author))
        self._redirect("/walls")

    def _do_wall_delete(self):
        fs = self._read_form()
        full = wall_file(fs.getvalue("file", ""))
        if not full:
            self._redirect("/walls")
            return
        base = os.path.basename(full)
        stem = os.path.splitext(base)[0]
        try:
            os.remove(full)
            mf = os.path.join(WALLS_DIR, "wallpapers.json")
            meta = load_json(mf, {})
            if isinstance(meta, dict) and stem in meta:
                meta.pop(stem, None)
                save_json(mf, meta)
        except OSError:
            pass
        pref = ui_prefs()
        if pref.get("bg") == base:
            pref["bg"] = ""
            save_ui(pref)
        _last_log.append("Arka plan silindi: %s" % base)
        self._redirect("/walls")

    def log_message(self, fmt, *args):
        sys.stderr.write("[ds-web] " + (fmt % args) + "\n")


def main():
    for d in (PACKAGES, DATA, WALLS_DIR):
        try:
            os.makedirs(d, exist_ok=True)
        except OSError:
            pass
    srv = ThreadingHTTPServer((BIND, PORT), Handler)
    print("DuckStore web paneli http://%s:%d" % (BIND, PORT))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
