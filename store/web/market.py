#!/usr/bin/env python3
"""
Arctian Market — herkese acik magaza sitesi (market.kerem.site, port 47584).

Google Play tarzi cok sayfali magaza + Google Play Console tarzi onay sistemi.

Ozellikler:
  - Axiom tasarim dili (varsayilan ACIK tema), Play tarzi yatay kaydirma satirlari
  - Sayfalar: Ana sayfa, Tum uygulamalar, Siralama, Yeni gelenler, Kategori,
    Arama, Uygulama detayi, Konsol, Yayinla, Inceleme, Rehber, MCP
  - Google-only giris
  - Onay sistemi: basvuru -> inceleme -> onay/red
  - Kisi basina en fazla MAX_APPS_PER_USER (5) uygulama
  - MCP sunucusu: POST /mcp (JSON-RPC) + /mcp tanitim sayfasi

Yalnizca Python 3 standart kutuphanesi kullanir.
"""
import hashlib
import json
import os
import secrets
import sys
import time
import urllib.parse
import urllib.request
from http.cookies import SimpleCookie
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

_HERE = os.path.dirname(os.path.abspath(__file__))
_COMMON = os.path.join(os.path.dirname(_HERE), "common")
for _p in (_HERE, _COMMON):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import wlflib      # noqa: E402
import storemeta   # noqa: E402
import sdkdist     # noqa: E402
import tokens as tokenstore  # noqa: E402

BASE = os.environ.get("DUCKSTORE_BASE", "/opt/duckstore")
PACKAGES = os.path.join(BASE, "packages")
DATA = os.path.join(BASE, "data")
PORT = int(os.environ.get("DUCKSTORE_MARKET_PORT", "47584"))
BIND = os.environ.get("DUCKSTORE_MARKET_BIND", "0.0.0.0")
PUBLIC_URL = os.environ.get("DUCKSTORE_MARKET_URL", "https://market.kerem.site")

GOOGLE_CLIENT_ID = os.environ.get("GOOGLE_CLIENT_ID", "")
GOOGLE_CLIENT_SECRET = os.environ.get("GOOGLE_CLIENT_SECRET", "")
GOOGLE_REDIRECT_URI = os.environ.get(
    "GOOGLE_REDIRECT_URI", "https://market.kerem.site/auth/callback")
GOOGLE_AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth"
GOOGLE_TOKEN_URL = "https://oauth2.googleapis.com/token"
GOOGLE_SCOPE = "openid email profile"
ADMIN_EMAILS = [x.strip().lower() for x in
                os.environ.get("DUCKSTORE_ADMIN_EMAILS", "").split(",")
                if x.strip()]

SESSION_TTL = 7 * 86400
MAX_WLF = wlflib.WLF_MAX_PACKAGE
MAX_SHOTS = 4
MAX_APPS_PER_USER = 5

_sessions = {}
_states = {}


# =====================================================================
# sessions / google oauth
# =====================================================================
def new_session(user):
    tok = secrets.token_urlsafe(32)
    _sessions[tok] = {"sub": user["sub"], "email": user.get("email", ""),
                      "name": user.get("name", ""), "exp": time.time() + SESSION_TTL}
    return tok


def session_user(tok):
    if not tok:
        return None
    s = _sessions.get(tok)
    if not s or s["exp"] < time.time():
        _sessions.pop(tok, None)
        return None
    return s


def drop_session(tok):
    _sessions.pop(tok, None)


def google_configured():
    return bool(GOOGLE_CLIENT_ID and GOOGLE_CLIENT_SECRET)


def google_auth_url(state):
    return GOOGLE_AUTH_URL + "?" + urllib.parse.urlencode({
        "client_id": GOOGLE_CLIENT_ID,
        "redirect_uri": GOOGLE_REDIRECT_URI,
        "response_type": "code",
        "scope": GOOGLE_SCOPE,
        "state": state,
        "access_type": "online",
        "prompt": "select_account",
    })


def _post_form(url, fields):
    data = urllib.parse.urlencode(fields).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


def google_exchange(code):
    tok = _post_form(GOOGLE_TOKEN_URL, {
        "code": code, "client_id": GOOGLE_CLIENT_ID,
        "client_secret": GOOGLE_CLIENT_SECRET,
        "redirect_uri": GOOGLE_REDIRECT_URI,
        "grant_type": "authorization_code",
    })
    id_token = tok.get("id_token")
    if not id_token:
        raise ValueError("id_token alinamadi")
    claims = _verify_id_token(id_token)
    if not claims.get("sub"):
        raise ValueError("id_token gecersiz")
    return {"sub": str(claims["sub"]),
            "email": str(claims.get("email", "")),
            "name": str(claims.get("name", "") or claims.get("email", ""))}


def _b64url_decode(s):
    s += "=" * (-len(s) % 4)
    import base64
    return base64.urlsafe_b64decode(s.encode("ascii"))


_JWKS_CACHE = {"at": 0.0, "keys": {}}


def _google_key_for(kid):
    if time.time() - _JWKS_CACHE["at"] > 3600 or not _JWKS_CACHE["keys"]:
        req = urllib.request.Request("https://www.googleapis.com/oauth2/v3/certs")
        with urllib.request.urlopen(req, timeout=20) as r:
            data = json.loads(r.read().decode("utf-8"))
        keys = {}
        for k in data.get("keys", []):
            keys[k.get("kid")] = (_b64url_decode(k["n"]), _b64url_decode(k["e"]))
        _JWKS_CACHE["keys"] = keys
        _JWKS_CACHE["at"] = time.time()
    key = _JWKS_CACHE["keys"].get(kid)
    if not key:
        raise ValueError("bilinmeyen anahtar")
    return int.from_bytes(key[0], "big"), int.from_bytes(key[1], "big")


def _verify_id_token(id_token):
    parts = id_token.split(".")
    if len(parts) != 3:
        raise ValueError("bicim gecersiz")
    h_b64, p_b64, s_b64 = parts
    signing_input = (h_b64 + "." + p_b64).encode("ascii")
    sig = _b64url_decode(s_b64)
    header = json.loads(_b64url_decode(h_b64))
    claims = json.loads(_b64url_decode(p_b64))
    if header.get("alg") != "RS256":
        raise ValueError("algoritma")
    n, e = _google_key_for(header.get("kid"))
    k = (n.bit_length() + 7) // 8
    if len(sig) != k:
        raise ValueError("imza uzunlugu")
    s = int.from_bytes(sig, "big")
    if s >= n:
        raise ValueError("imza araligi")
    em = pow(s, e, n).to_bytes(k, "big")
    di = bytes.fromhex("3031300d060960864801650304020105000420")
    h = hashlib.sha256(signing_input).digest()
    ps_len = k - 2 - 1 - len(di) - 32
    expect = b"\x00\x01" + (b"\xff" * ps_len) + b"\x00" + di + h
    if em != expect:
        raise ValueError("imza gecersiz")
    if GOOGLE_CLIENT_ID:
        aud = claims.get("aud")
        if not ((isinstance(aud, list) and GOOGLE_CLIENT_ID in aud) or aud == GOOGLE_CLIENT_ID):
            raise ValueError("hedef kitle")
    if claims.get("iss") not in ("https://accounts.google.com", "accounts.google.com"):
        raise ValueError("kaynak")
    now = int(time.time())
    if int(claims.get("exp", 0)) < now or int(claims.get("iat", 0)) > now + 300:
        raise ValueError("sure")
    return claims


def is_admin(user):
    if not user:
        return False
    return user.get("email", "").lower() in ADMIN_EMAILS


# =====================================================================
# onay sistemi (pending / approved / rejected) + 5 uygulama hakki
# =====================================================================
SUBMISSIONS_FILE = os.path.join(DATA, "submissions.json")
PENDING_DIR = os.path.join(DATA, "pending")


def _load_subs():
    try:
        with open(SUBMISSIONS_FILE, "r", encoding="utf-8") as f:
            d = json.load(f)
        return d if isinstance(d, dict) else {}
    except Exception:
        return {}


def _save_subs(d):
    os.makedirs(os.path.dirname(SUBMISSIONS_FILE), exist_ok=True)
    tmp = SUBMISSIONS_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=2)
    os.replace(tmp, SUBMISSIONS_FILE)


def list_submissions(status=None, uploader_sub=None):
    out = []
    for sid, s in _load_subs().items():
        if status and s.get("status") != status:
            continue
        if uploader_sub and s.get("uploader_sub") != uploader_sub:
            continue
        out.append(s)
    out.sort(key=lambda x: -x.get("submitted_at", 0))
    return out


def get_submission(sid):
    return _load_subs().get(sid)


def owned_app_ids(user_sub):
    """Kullanicinin hak iddia ettigi uygulama id'leri (reddedilenler sayilmaz)."""
    ids = set()
    for s in _load_subs().values():
        if s.get("uploader_sub") == user_sub and s.get("status") != "rejected":
            ids.add(s.get("id"))
    return ids


def quota_used(user_sub):
    return len(owned_app_ids(user_sub))


class QuotaError(Exception):
    pass


def submit_app(user, data, description, shots_data):
    """Yeni basvuru olusturur (pending). 5 uygulama sinirini denetler."""
    info = wlflib.validate(data)
    owned = owned_app_ids(user["sub"])
    if info["id"] not in owned and len(owned) >= MAX_APPS_PER_USER:
        raise QuotaError(
            "Hesap basina en fazla %d uygulama yayinlanabilir. "
            "Yeni bir uygulama icin mevcutlarinizdan birini kaldirmaniz gerekir."
            % MAX_APPS_PER_USER)
    os.makedirs(PENDING_DIR, exist_ok=True)
    sid = secrets.token_hex(8)
    fn = "%s.wlf" % info["id"]
    existing = [s for s in list_submissions(status="pending", uploader_sub=user["sub"])
                if s.get("id") == info["id"]]
    for s in existing:
        try:
            os.remove(os.path.join(PENDING_DIR, s.get("file", "")))
        except OSError:
            pass
        subs = _load_subs()
        subs.pop(s["sid"], None)
        _save_subs(subs)
    signed = wlflib.sign(info)
    with open(os.path.join(PENDING_DIR, fn), "wb") as f:
        f.write(signed)
    shot_names = []
    for sd in shots_data[:MAX_SHOTS]:
        try:
            nm = storemeta.add_shot(DATA, info["id"], sd)
            shot_names.append(nm["file"])
        except Exception:
            pass
    try:
        storemeta.set_app(DATA, info["id"], description=description,
                          publisher=user.get("name") or user.get("email"))
    except Exception:
        pass
    sub = {
        "sid": sid,
        "id": info["id"],
        "title": info["title"],
        "version": info["version"],
        "category": info["category"],
        "author": info["author"],
        "size": len(signed),
        "permissions": info.get("permissions", ""),
        "icon_svg": info.get("icon_svg", ""),
        "uploader_sub": user["sub"],
        "uploader_email": user.get("email", ""),
        "uploader_name": user.get("name", ""),
        "status": "pending",
        "submitted_at": int(time.time()),
        "file": fn,
        "shots": shot_names,
        "description": description,
    }
    subs = _load_subs()
    subs[sid] = sub
    _save_subs(subs)
    return sub


def approve_submission(sid, reviewer):
    sub = get_submission(sid)
    if not sub or sub.get("status") != "pending":
        return None, "basvuru bulunamadi veya zaten islenmis"
    src = os.path.join(PENDING_DIR, sub.get("file", ""))
    if not os.path.isfile(src):
        return None, "paket dosyasi yok"
    data = open(src, "rb").read()
    try:
        info = wlflib.publish(PACKAGES, data)
    except wlflib.WlfError as ex:
        return None, "paket reddedildi: %s" % ex
    try:
        storemeta.set_app(DATA, info["id"],
                          description=sub.get("description", ""),
                          publisher=sub.get("uploader_name") or sub.get("uploader_email"))
    except Exception:
        pass
    try:
        os.remove(src)
    except OSError:
        pass
    subs = _load_subs()
    sub = subs.get(sid, sub)
    sub["status"] = "approved"
    sub["reviewed_at"] = int(time.time())
    sub["reviewer"] = reviewer
    sub["published_file"] = info["file"]
    subs[sid] = sub
    _save_subs(subs)
    invalidate_cache()
    return sub, None


def reject_submission(sid, reviewer, reason):
    sub = get_submission(sid)
    if not sub or sub.get("status") != "pending":
        return None, "basvuru bulunamadi veya zaten islenmis"
    try:
        os.remove(os.path.join(PENDING_DIR, sub.get("file", "")))
    except OSError:
        pass
    subs = _load_subs()
    sub = subs.get(sid, sub)
    sub["status"] = "rejected"
    sub["reviewed_at"] = int(time.time())
    sub["reviewer"] = reviewer
    sub["rejection_reason"] = (reason or "").strip()[:500]
    subs[sid] = sub
    _save_subs(subs)
    return sub, None


# =====================================================================
# uygulama listesi (onaylanmis = packages/)
# =====================================================================
_apps_cache = {"at": 0.0, "list": []}


def invalidate_cache():
    _apps_cache["at"] = 0.0


def list_apps():
    now = time.time()
    if now - _apps_cache["at"] < 5 and _apps_cache["list"]:
        return _apps_cache["list"]
    apps = wlflib.scan_packages(PACKAGES)
    meta = storemeta.load_meta(DATA)
    dl = _download_counts()
    out = []
    for a in apps:
        m = meta.get(a["id"], {})
        if m.get("hidden"):
            continue
        shots = storemeta.list_shots(DATA, a["id"])
        try:
            mtime = int(os.path.getmtime(os.path.join(PACKAGES, a["file"])))
        except OSError:
            mtime = 0
        out.append({
            "id": a["id"], "title": a["title"], "version": a["version"],
            "author": a["author"], "publisher": m.get("publisher") or a["author"] or "Arctian",
            "category": a["category"], "size": a["size"], "mtime": mtime,
            "permissions": a.get("permissions", ""), "icon": a.get("icon", b""),
            "icon_svg": a.get("icon_svg", ""), "description": m.get("description", ""),
            "shots": shots, "featured": bool(m.get("featured")),
            "downloads": dl.get(a["file"], 0), "file": a["file"],
        })
    _apps_cache["list"] = out
    _apps_cache["at"] = now
    return out


def _download_counts():
    try:
        import stats as stats_mod
        st = stats_mod.snapshot(DATA, _sizes())
        return {f["name"]: f.get("n", 0) for f in st.get("files", [])}
    except Exception:
        return {}


def _sizes():
    sizes = {}
    if os.path.isdir(PACKAGES):
        for fn in os.listdir(PACKAGES):
            if fn.endswith(".wlf"):
                try:
                    sizes[fn] = os.path.getsize(os.path.join(PACKAGES, fn))
                except OSError:
                    pass
    return sizes


def categories_of(apps):
    cats = {}
    for a in apps:
        cats.setdefault(a["category"] or "Genel", []).append(a)
    return dict(sorted(cats.items(), key=lambda kv: (-len(kv[1]), kv[0])))


def _num(n):
    try:
        return "{:,}".format(int(n)).replace(",", ".")
    except (TypeError, ValueError):
        return "0"


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


def ago_short(ts):
    if not ts:
        return ""
    d = int((time.time() - ts) / 86400)
    if d <= 0:
        return "bugün"
    if d == 1:
        return "dün"
    if d < 30:
        return "%d gün önce" % d
    if d < 365:
        return "%d ay önce" % (d // 30)
    return "%d yıl önce" % (d // 365)


# =====================================================================
# Axiom CSS — Play tarzi magaza (varsayilan ACIK tema)
# =====================================================================
CSS = r"""
@import url('https://fonts.googleapis.com/css2?family=Space+Grotesk:wght@400;500;600;700&family=IBM+Plex+Sans:ital,wght@0,400;0,500;0,600;1,400&display=swap');
:root,[data-theme="light"]{
  --bg:oklch(0.985 0.008 85);--bg-elevated:oklch(0.97 0.01 80);--bg-subtle:oklch(0.955 0.012 75);
  --surface:oklch(1 0 0);--border:oklch(0.9 0.015 70);--border-strong:oklch(0.82 0.02 65);
  --text:oklch(0.22 0.02 50);--text-muted:oklch(0.48 0.02 55);--text-subtle:oklch(0.62 0.015 60);
  --accent:oklch(0.49 0.108 9);--accent-hover:oklch(0.42 0.12 9);--accent-soft:oklch(0.95 0.03 15);
  --accent-text:oklch(0.98 0.01 90);--success:oklch(0.55 0.12 145);--warning:oklch(0.7 0.14 70);
  --danger:oklch(0.55 0.18 25);--star:oklch(0.72 0.15 70);
  --shadow-sm:0 1px 2px oklch(0.2 0.02 50 / 0.04);--shadow:0 2px 8px oklch(0.2 0.02 50 / 0.06);
  --shadow-lg:0 8px 24px oklch(0.2 0.02 50 / 0.08);
  --font-display:"Space Grotesk",system-ui,sans-serif;--font-body:"IBM Plex Sans",system-ui,sans-serif;
  --font-mono:ui-monospace,monospace;--radius-sm:4px;--radius:8px;--radius-lg:12px;--radius-xl:16px;
  --space-1:.25rem;--space-2:.5rem;--space-3:.75rem;--space-4:1rem;--space-5:1.5rem;--space-6:2rem;--space-7:3rem;--space-8:4rem;
}
[data-theme="dark"]{
  --bg:oklch(0.16 0.003 0);--bg-elevated:oklch(0.20 0.004 0);--bg-subtle:oklch(0.24 0.005 0);
  --surface:oklch(0.18 0.004 0);--border:oklch(0.30 0.006 0);--border-strong:oklch(0.40 0.008 0);
  --text:oklch(0.96 0.002 0);--text-muted:oklch(0.78 0.004 0);--text-subtle:oklch(0.62 0.005 0);
  --accent:oklch(0.82 0.06 9);--accent-hover:oklch(0.88 0.07 9);--accent-soft:oklch(0.28 0.02 9);
  --accent-text:oklch(0.12 0.01 0);--success:oklch(0.72 0.08 145);--warning:oklch(0.78 0.08 70);
  --danger:oklch(0.70 0.10 25);--star:oklch(0.8 0.12 70);
  --shadow-sm:0 1px 2px oklch(0 0 0 / 0.25);--shadow:0 2px 8px oklch(0 0 0 / 0.35);
  --shadow-lg:0 8px 24px oklch(0 0 0 / 0.45);
}
*{box-sizing:border-box}
html{font-size:16px;-webkit-font-smoothing:antialiased}
body{margin:0;background:var(--bg);color:var(--text);font-family:var(--font-body);
  font-size:1rem;line-height:1.5}
h1,h2,h3,h4{font-family:var(--font-display);font-weight:600;letter-spacing:-.02em;margin:0;color:var(--text)}
h1{font-size:1.75rem;font-weight:700}h2{font-size:1.35rem}h3{font-size:1.15rem}h4{font-size:1rem}
p{margin:0 0 var(--space-4);color:var(--text-muted)}
a{color:var(--accent);text-decoration:none}
code,.mono{font-family:var(--font-mono);font-size:.88em;background:var(--bg-subtle);
  padding:.12em .4em;border-radius:var(--radius-sm)}
img{max-width:100%}
.container{max-width:1240px;margin:0 auto;padding:0 var(--space-6)}

/* ---- top bar ---- */
.topbar{position:sticky;top:0;z-index:50;background:color-mix(in oklch,var(--bg) 88%,transparent);
  backdrop-filter:blur(14px);border-bottom:1px solid var(--border)}
.topbar-in{display:flex;align-items:center;gap:var(--space-5);padding:.7rem 0}
.brand{display:flex;align-items:center;gap:.6rem;font-family:var(--font-display);font-weight:700;
  font-size:1.15rem;letter-spacing:-.03em;color:var(--text);white-space:nowrap}
.brand .mark{width:30px;height:30px;border-radius:9px;background:var(--accent);color:var(--accent-text);
  display:grid;place-items:center;font-size:1rem}
.tb-search{flex:1;max-width:620px;position:relative}
.tb-search input{width:100%;padding:.62rem 1rem .62rem 2.7rem;border-radius:999px;
  border:1px solid var(--border);background:var(--surface);color:var(--text);
  font-family:var(--font-body);font-size:.92rem;transition:all .2s}
.tb-search input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px var(--accent-soft)}
.tb-search .ico{position:absolute;left:1.05rem;top:50%;transform:translateY(-50%);color:var(--text-subtle)}
.tb-right{display:flex;align-items:center;gap:var(--space-3);margin-left:auto}
.avatar{width:32px;height:32px;border-radius:50%;background:var(--accent-soft);color:var(--accent);
  display:grid;place-items:center;font-weight:600;font-size:.85rem;flex-shrink:0}

/* ---- main nav (Play ust menu satiri) ---- */
.tabs{display:flex;gap:var(--space-1);border-bottom:1px solid var(--border);overflow-x:auto;
  scrollbar-width:none}
.tabs::-webkit-scrollbar{display:none}
.tabs a{padding:.8rem 1.1rem;color:var(--text-muted);font-size:.9rem;font-weight:500;
  border-bottom:2px solid transparent;margin-bottom:-1px;white-space:nowrap;display:flex;
  align-items:center;gap:.45rem;transition:color .15s}
.tabs a:hover{color:var(--text)}
.tabs a.on{color:var(--accent);border-bottom-color:var(--accent);font-weight:600}

/* ---- buttons ---- */
.btn{display:inline-flex;align-items:center;justify-content:center;gap:.5rem;
  padding:.6rem 1.2rem;font-family:var(--font-display);font-size:.875rem;font-weight:500;
  border-radius:999px;border:1px solid transparent;cursor:pointer;text-decoration:none;
  transition:all .2s cubic-bezier(.4,0,.2,1);line-height:1.2;white-space:nowrap}
.btn-primary{background:var(--accent);color:var(--accent-text);box-shadow:var(--shadow-sm)}
.btn-primary:hover{background:var(--accent-hover);transform:translateY(-1px);box-shadow:var(--shadow)}
.btn-secondary{background:transparent;color:var(--text);border-color:var(--border-strong)}
.btn-secondary:hover{background:var(--bg-subtle)}
.btn-ghost{background:transparent;color:var(--text-muted)}
.btn-ghost:hover{color:var(--text);background:var(--bg-subtle)}
.btn-danger{background:var(--danger);color:#fff}.btn-danger:hover{filter:brightness(1.1)}
.btn-sm{padding:.4rem .85rem;font-size:.78rem}
.btn-lg{padding:.8rem 1.7rem;font-size:1rem}

/* ---- sections & rows (Play tarzi yatay satirlar) ---- */
.section{padding:var(--space-6) 0 0;animation:fadeUp .5s cubic-bezier(.4,0,.2,1) both}
.section:last-of-type{padding-bottom:var(--space-8)}
.sec-head{display:flex;align-items:baseline;justify-content:space-between;gap:var(--space-4);
  margin-bottom:var(--space-4)}
.sec-head h2{font-size:1.3rem}
.sec-head .sub{color:var(--text-subtle);font-size:.82rem;margin-top:.15rem}
.sec-head a.more{color:var(--accent);font-size:.85rem;font-weight:500}
@keyframes fadeUp{from{opacity:0;transform:translateY(10px)}to{opacity:1;transform:none}}
.row-wrap{position:relative}
.row{display:flex;gap:var(--space-5);overflow-x:auto;scroll-behavior:smooth;
  scroll-snap-type:x proximity;padding:.25rem .1rem var(--space-4);scrollbar-width:none}
.row::-webkit-scrollbar{display:none}
.row>*{scroll-snap-align:start}
.row-arrow{position:absolute;top:38%;transform:translateY(-50%);z-index:5;width:36px;height:36px;
  border-radius:50%;border:1px solid var(--border);background:var(--surface);color:var(--text);
  box-shadow:var(--shadow);cursor:pointer;display:none;place-items:center;font-size:1rem}
.row-wrap:hover .row-arrow{display:grid}
.row-arrow.l{left:-14px}.row-arrow.r{right:-14px}

/* ---- app card (Play tarzi: ikon + altinda metin) ---- */
.ac{width:168px;flex:0 0 168px;color:inherit;text-align:left;display:block}
.ac .aicon{width:112px;height:112px;border-radius:22px;overflow:hidden;background:var(--bg-subtle);
  border:1px solid var(--border);margin-bottom:.7rem;transition:transform .2s,box-shadow .2s;
  display:grid;place-items:center}
.ac .aicon svg,.ac .aicon img{width:100%;height:100%;display:block}
.ac:hover .aicon{transform:translateY(-3px);box-shadow:var(--shadow-lg)}
.ac .at{font-weight:500;font-size:.9rem;line-height:1.3;color:var(--text);
  display:-webkit-box;-webkit-line-clamp:1;-webkit-box-orient:vertical;overflow:hidden}
.ac .am{color:var(--text-subtle);font-size:.78rem;margin-top:.15rem;
  display:flex;align-items:center;gap:.4rem}
.ac .am .dot{opacity:.5}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:var(--space-6) var(--space-5)}
.grid .ac{width:auto;flex:none}
.grid .aicon{width:100%;aspect-ratio:1;height:auto}

/* ---- hero (ana sayfa one cikan banner) ---- */
.hero{margin-top:var(--space-6);border-radius:var(--radius-xl);overflow:hidden;
  border:1px solid var(--border);background:var(--surface);box-shadow:var(--shadow);
  display:grid;grid-template-columns:1.1fr 1fr;animation:fadeUp .5s cubic-bezier(.4,0,.2,1) both}
.hero .htxt{padding:var(--space-7)}
.hero .htxt h1{font-size:2rem;margin-bottom:.6rem;line-height:1.15}
.hero .htxt .hp{color:var(--accent);font-weight:600;font-size:.85rem;margin-bottom:.8rem}
.hero .hdesc{color:var(--text-muted);font-size:.95rem;min-height:3.4em}
.hero .hvis{background:var(--bg-subtle) center/cover no-repeat;display:grid;place-items:center;
  min-height:280px;border-left:1px solid var(--border)}
.hero .hvis .aicon{width:150px;height:150px;border-radius:30px;background:var(--surface);
  box-shadow:var(--shadow-lg)}
.hero .hmeta{display:flex;gap:var(--space-5);margin:var(--space-4) 0 var(--space-5);flex-wrap:wrap}
.hero .hmeta .st{font-size:.8rem;color:var(--text-muted)}
.hero .hmeta .st b{display:block;color:var(--text);font-size:1rem;font-family:var(--font-display)}
@media(max-width:860px){.hero{grid-template-columns:1fr}.hero .hvis{min-height:200px;border-left:none;
  border-top:1px solid var(--border)}}

/* ---- chips (kategori etiketleri) ---- */
.chips{display:flex;gap:.5rem;flex-wrap:wrap;margin-bottom:var(--space-5)}
.chip{padding:.42rem 1rem;border-radius:999px;border:1px solid var(--border);background:var(--surface);
  color:var(--text-muted);font-size:.83rem;font-weight:500;transition:all .15s}
.chip:hover{border-color:var(--border-strong);color:var(--text)}
.chip.on{background:var(--text);color:var(--bg);border-color:var(--text)}

/* ---- badges & alerts ---- */
.badge{display:inline-flex;align-items:center;gap:.3rem;padding:.22rem .65rem;font-size:.75rem;
  font-weight:500;border-radius:999px;background:var(--bg-subtle);color:var(--text-muted);
  border:1px solid var(--border)}
.badge-accent{background:var(--accent-soft);color:var(--accent);border-color:transparent}
.badge-success{background:oklch(0.92 0.05 145);color:oklch(0.42 0.1 145);border-color:transparent}
.badge-warning{background:oklch(0.95 0.06 70);color:oklch(0.5 0.11 60);border-color:transparent}
.badge-danger{background:oklch(0.93 0.06 25);color:oklch(0.5 0.15 25);border-color:transparent}
[data-theme="dark"] .badge-success{background:oklch(0.26 0.04 145);color:oklch(0.8 0.08 145)}
[data-theme="dark"] .badge-warning{background:oklch(0.28 0.04 70);color:oklch(0.85 0.07 75)}
[data-theme="dark"] .badge-danger{background:oklch(0.28 0.05 25);color:oklch(0.8 0.08 25)}
.alert{padding:var(--space-4);border-radius:var(--radius);border:1px solid var(--border);
  background:var(--bg-elevated);font-size:.9rem;margin-bottom:var(--space-4)}
.alert-success{border-left:3px solid var(--success)}
.alert-danger{border-left:3px solid var(--danger)}
.alert-warning{border-left:3px solid var(--warning)}

/* ---- cards / forms ---- */
.card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius-lg);
  padding:var(--space-5);box-shadow:var(--shadow-sm)}
.table{width:100%;border-collapse:collapse;font-size:.875rem}
.table th,.table td{padding:.75rem .9rem;text-align:left;border-bottom:1px solid var(--border);
  vertical-align:middle}
.table th{color:var(--text-subtle);font-weight:500;font-size:.72rem;text-transform:uppercase;
  letter-spacing:.05em}
.table tr:hover td{background:var(--bg-subtle)}
input,textarea,select{width:100%;padding:.65rem .9rem;font-family:var(--font-body);font-size:.95rem;
  background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);
  color:var(--text);transition:border-color .15s,box-shadow .15s}
input:focus,textarea:focus,select:focus{outline:none;border-color:var(--accent);
  box-shadow:0 0 0 3px var(--accent-soft)}
textarea{min-height:130px;resize:vertical}
.form-group{margin-bottom:var(--space-5)}
.form-group label{display:block;font-size:.85rem;font-weight:500;margin-bottom:.45rem}
.form-group .hint{font-size:.78rem;color:var(--text-subtle);margin-top:.35rem}
.drop{border:1.5px dashed var(--border-strong);border-radius:var(--radius-lg);
  padding:var(--space-7) var(--space-5);text-align:center;background:var(--bg-elevated);
  cursor:pointer;transition:all .2s}
.drop:hover,.drop.on{border-color:var(--accent);background:var(--accent-soft)}
.drop b{display:block;font-size:1.05rem;margin-bottom:.35rem;font-family:var(--font-display)}
.drop span{color:var(--text-muted);font-size:.83rem}
.drop input[type=file]{display:none}

/* ---- detay sayfasi ---- */
.detail-hero{display:flex;gap:var(--space-6);align-items:flex-start;padding:var(--space-7) 0 var(--space-5)}
.detail-hero .dicon{width:120px;height:120px;border-radius:26px;overflow:hidden;flex-shrink:0;
  background:var(--surface);border:1px solid var(--border);box-shadow:var(--shadow);display:grid;place-items:center}
.detail-hero .dicon svg,.detail-hero .dicon img{width:100%;height:100%}
.dmeta-strip{display:flex;gap:var(--space-6);margin-top:.8rem;flex-wrap:wrap}
.dmeta-strip .st{font-size:.78rem;color:var(--text-muted);text-align:left}
.dmeta-strip .st b{display:block;color:var(--text);font-size:.95rem;font-family:var(--font-display)}
.shots{display:flex;gap:var(--space-4);overflow-x:auto;scroll-snap-type:x proximity;
  padding-bottom:var(--space-4);scrollbar-width:none}
.shots::-webkit-scrollbar{display:none}
.shot{aspect-ratio:16/10;min-width:420px;max-width:420px;background:var(--bg-subtle) center/cover no-repeat;
  border:1px solid var(--border);border-radius:var(--radius-lg);overflow:hidden;scroll-snap-align:start}
@media(max-width:640px){.shot{min-width:82vw;max-width:82vw}}
.kv{display:flex;justify-content:space-between;gap:var(--space-4);padding:.7rem 0;
  border-bottom:1px solid var(--border);font-size:.875rem}
.kv:last-child{border-bottom:none}.kv .k{color:var(--text-muted)}
.detail-grid{display:grid;grid-template-columns:minmax(0,1fr) 340px;gap:var(--space-6);
  align-items:start;padding-bottom:var(--space-8)}
@media(max-width:900px){.detail-grid{grid-template-columns:1fr}}

/* ---- siralama ---- */
.rank-list{display:flex;flex-direction:column}
.rank-item{display:flex;align-items:center;gap:var(--space-4);padding:var(--space-3) var(--space-2);
  border-bottom:1px solid var(--border);color:inherit;border-radius:var(--radius)}
.rank-item:hover{background:var(--bg-subtle)}
.rank-item .rk{width:34px;text-align:center;font-family:var(--font-display);font-weight:700;
  font-size:1.15rem;color:var(--text-subtle);flex-shrink:0}
.rank-item:nth-child(1) .rk{color:var(--accent)}
.rank-item .ric{width:56px;height:56px;border-radius:14px;overflow:hidden;background:var(--bg-subtle);
  border:1px solid var(--border);flex-shrink:0;display:grid;place-items:center}
.rank-item .ric svg,.rank-item .ric img{width:100%;height:100%}
.rank-item .rmeta{flex:1;min-width:0}
.rank-item .rmeta .rt{font-weight:600;font-size:.95rem}
.rank-item .rmeta .rs{color:var(--text-subtle);font-size:.8rem}
.rank-item .rdl{color:var(--text-muted);font-size:.83rem;white-space:nowrap}

/* ---- misc ---- */
.empty{padding:var(--space-8) var(--space-5);text-align:center;color:var(--text-muted)}
.empty h3{margin-bottom:.5rem;color:var(--text)}
.theme-toggle{background:var(--bg-subtle);border:1px solid var(--border);border-radius:999px;
  padding:.45rem .8rem;font-size:.78rem;cursor:pointer;color:var(--text-muted);font-family:var(--font-body)}
.theme-toggle:hover{color:var(--text)}
.muted{color:var(--text-muted)}.subtle{color:var(--text-subtle)}
.flex{display:flex}.items-center{align-items:center}.justify-between{justify-content:space-between}
.gap-2{gap:.5rem}.gap-3{gap:.75rem}.gap-4{gap:1rem}
.mb-2{margin-bottom:.5rem}.mb-4{margin-bottom:1rem}.mb-6{margin-bottom:1.5rem}.mt-4{margin-top:1rem}
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:var(--space-5)}
@media(max-width:800px){.grid-2{grid-template-columns:1fr}}
.progress{height:8px;border-radius:999px;background:var(--bg-subtle);overflow:hidden}
.progress>div{height:100%;border-radius:999px;background:var(--accent)}
pre.code{background:var(--bg-subtle);border:1px solid var(--border);padding:var(--space-4);
  border-radius:var(--radius);font-family:var(--font-mono);font-size:.8rem;overflow:auto;line-height:1.6}
.footer{border-top:1px solid var(--border);margin-top:var(--space-8);padding:var(--space-7) 0;
  color:var(--text-subtle);font-size:.83rem}
.footer .fcols{display:grid;grid-template-columns:repeat(4,1fr);gap:var(--space-5);margin-bottom:var(--space-6)}
@media(max-width:700px){.footer .fcols{grid-template-columns:1fr 1fr}}
.footer h5{font-family:var(--font-display);font-size:.85rem;color:var(--text);margin:0 0 .7rem;font-weight:600}
.footer ul{list-style:none;padding:0;margin:0}
.footer li{margin-bottom:.45rem}
.footer a{color:var(--text-subtle)}
.footer a:hover{color:var(--text)}
@media(max-width:760px){
  .tb-search{display:none}
  .container{padding:0 var(--space-4)}
  .detail-hero{flex-direction:column}
}
"""

LOGO_MARK = ("<span class='mark'>"
             "<svg width='16' height='16' viewBox='0 0 24 24' fill='none'>"
             "<path d='M4 7l8-4 8 4v10l-8 4-8-4z' stroke='currentColor' stroke-width='2' "
             "stroke-linejoin='round'/><path d='M12 11v10M4 7l8 4 8-4' stroke='currentColor' "
             "stroke-width='2' stroke-linejoin='round'/></svg></span>")
GOOGLE_BTN_SVG = ("<svg width='16' height='16' viewBox='0 0 48 48'><path fill='#EA4335' "
 "d='M24 9.5c3.5 0 6.6 1.2 9 3.6l6.7-6.7C35.6 2.6 30.2 0 24 0 14.6 0 6.5 5.4 2.6 13.2l7.8 6.1C12.3 13.1 17.6 9.5 24 9.5z'/>"
 "<path fill='#4285F4' d='M46.98 24.55c0-1.6-.15-3.15-.42-4.55H24v9.02h12.9c-.58 2.96-2.26 5.48-4.78 7.18l7.73 6c4.51-4.18 7.09-10.36 7.09-17.65z'/>"
 "<path fill='#FBBC05' d='M10.4 28.5c-.48-1.45-.76-2.99-.76-4.5s.27-3.05.76-4.5l-7.8-6.1C.92 16.06 0 19.9 0 24c0 4.1.92 7.94 2.6 11.6l7.8-7.1z'/>"
 "<path fill='#34A853' d='M24 48c6.48 0 11.93-2.13 15.89-5.81l-7.73-6c-2.15 1.45-4.92 2.3-8.16 2.3-6.4 0-11.7-3.6-13.6-8.9l-7.8 7.1C6.5 42.6 14.6 48 24 48z'/></svg>")


def e(x):
    import html
    return html.escape(str(x), quote=True)


def icon_inner(app):
    """Yalnizca ikon govdesi (svg/img); boyutlandirma sarf eden span'a ait."""
    svg = (app.get("icon_svg") or "").strip()
    if svg:
        return svg
    raw = app.get("icon") or b""
    if isinstance(raw, str):
        raw = raw.encode("utf-8", "replace")
    if raw and wlflib.icon_content_type(raw) != "image/svg+xml":
        import base64
        b64 = base64.b64encode(raw).decode("ascii")
        return "<img src='data:%s;base64,%s' alt=''>" % (wlflib.icon_content_type(raw), b64)
    return ("<svg viewBox='0 0 40 40'><rect x='1' y='1' width='38' "
            "height='38' rx='10' fill='var(--bg-subtle)' stroke='var(--border)'/>"
            "<path d='M12 26l5-8 4 5 3-4 4 7z' fill='var(--accent)'/></svg>")


def icon_html(app, cls="aicon"):
    return "<span class='%s'>%s</span>" % (cls, icon_inner(app))


def shell(title, body, user=None, active=""):
    if user:
        initial = (user.get("name") or user.get("email") or "?")[:1].upper()
        authbox = ("""<a href='/console' title='Konsol' style='display:flex;align-items:center;gap:.5rem'>
          <span class='avatar'>%s</span></a>
          <a class='btn btn-secondary btn-sm' href='/publish'>+ Uygulama</a>
          <a class='btn btn-ghost btn-sm' href='/logout'>Çıkış</a>"""
                   % e(initial))
    else:
        authbox = ("<a class='btn btn-primary btn-sm' href='/login'>%s Google ile giriş</a>"
                   % GOOGLE_BTN_SVG)
    nav_items = [
        ("/", "Keşfet"),
        ("/apps", "Uygulamalar"),
        ("/charts", "Sıralama"),
        ("/new", "Yeni gelenler"),
        ("/sdk", "SDK"),
        ("/mcp", "MCP"),
        ("/console", "Konsol"),
    ]
    if user and is_admin(user):
        nav_items.append(("/review", "İnceleme"))
    nav_html = "".join(
        "<a href='%s' class='%s'>%s</a>" % (href, "on" if active == href else "", label)
        for href, label in nav_items)
    return ("<!doctype html><html lang='tr' data-theme='light'><head>"
            "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>%s — Arctian Market</title><style>%s</style>"
            "<script>function toggleTheme(){var h=document.documentElement;"
            "h.dataset.theme=h.dataset.theme==='dark'?'light':'dark';"
            "localStorage.setItem('theme',h.dataset.theme);}"
            "try{var t=localStorage.getItem('theme');if(t)document.documentElement.dataset.theme=t;}catch(e){}"
            "function rowScroll(id,d){var r=document.getElementById(id);if(r)r.scrollBy({left:d*r.clientWidth*.8,behavior:'smooth'});}"
            "</script>"
            "</head><body>"
            "<header class='topbar'><div class='container topbar-in'>"
            "<a class='brand' href='/'>%s Arctian Market</a>"
            "<form class='tb-search' action='/search' method='get'>"
            "<span class='ico'>⌕</span>"
            "<input type='text' name='q' placeholder='Uygulama ve geliştirici ara…' autocomplete='off'>"
            "</form>"
            "<div class='tb-right'><button class='theme-toggle' onclick='toggleTheme()' "
            "title='Açık/koyu tema'>◐ Tema</button>%s</div>"
            "</div></header>"
            "<div class='container'><nav class='tabs'>%s</nav></div>"
            "<main class='container'>%s</main>"
            "<footer class='footer'><div class='container'>"
            "<div class='fcols'>"
            "<div><h5>Mağaza</h5><ul><li><a href='/'>Keşfet</a></li>"
            "<li><a href='/apps'>Tüm uygulamalar</a></li><li><a href='/charts'>Sıralama</a></li>"
            "<li><a href='/new'>Yeni gelenler</a></li></ul></div>"
            "<div><h5>Geliştirici</h5><ul><li><a href='/publish'>Uygulama yayımla</a></li>"
            "<li><a href='/console'>Konsol</a></li><li><a href='/about'>Yayınlama rehberi</a></li>"
            "<li><a href='/mcp'>MCP sunucusu</a></li></ul></div>"
            "<div><h5>Güvenlik</h5><ul><li><a href='/about#policy'>İçerik politikası</a></li>"
            "<li><a href='/about#signing'>Paket imzalama</a></li>"
            "<li><a href='/about#review'>İnceleme süreci</a></li></ul></div>"
            "<div><h5>API</h5><ul><li><a href='/api/apps'>Uygulama dizini (JSON)</a></li>"
            "<li><a href='/mcp'>MCP uç noktası</a></li></ul></div>"
            "</div>"
            "Arctian Market · tüm yayınlar elle incelenir ve mağaza anahtarıyla yeniden imzalanır · "
            "varsayılan Arctian işletim sistemi sürümüyle çalışır"
            "</div></footer></body></html>"
            % (e(title), CSS, LOGO_MARK, authbox, nav_html, body))


# =====================================================================
# UI parcalari
# =====================================================================
def app_card(a):
    return ("<a class='ac' href='/app/%s'>"
            "%s<div class='at' title='%s'>%s</div>"
            "<div class='am'><span>%s</span></div>"
            "<div class='am'>★ <span style='color:var(--text-muted)'>%s indirilme</span></div>"
            "</a>"
            % (e(a["id"]), icon_html(a), e(a["title"]), e(a["title"]),
               e(a["category"]), _num(a.get("downloads", 0))))


def app_row(section_id, apps, title, subtitle="", more_href=""):
    if not apps:
        return ""
    more = ("<a class='more' href='%s'>Tümü →</a>" % e(more_href)) if more_href else ""
    cards = "".join(app_card(a) for a in apps[:18])
    return ("""<section class='section'>
      <div class='sec-head'><div><h2>%s</h2>%s</div>%s</div>
      <div class='row-wrap'>
        <button class='row-arrow l' onclick="rowScroll('%s',-1)" aria-label='Geri'>‹</button>
        <div class='row' id='%s'>%s</div>
        <button class='row-arrow r' onclick="rowScroll('%s',1)" aria-label='İleri'>›</button>
      </div></section>"""
            % (e(title), ("<div class='sub'>%s</div>" % e(subtitle)) if subtitle else "", more,
               section_id, section_id, cards, section_id))


def status_badge(status):
    return {"pending": "<span class='badge badge-warning'>● İncelemede</span>",
            "approved": "<span class='badge badge-success'>✓ Yayında</span>",
            "rejected": "<span class='badge badge-danger'>✕ Reddedildi</span>"}.get(
        status, "<span class='badge'>%s</span>" % e(status))


# =====================================================================
# MCP (JSON-RPC 2.0 streamable-http alt kumesi)
# =====================================================================
MCP_SERVER_NAME = "ArctianMCP"
MCP_VERSION = "1.0.0"

# ---------------------------------------------------------------------
# MCP araclari
#
# Yetki gerektiren araclar: whoami, publish_app, my_submissions.
# Bu araclar `Authorization: Bearer arct_...` basligi ister; jeton
# konsoldan uretilir ve olusturan kullanicinin kimligiyle calisir.
# ---------------------------------------------------------------------
MCP_TOOLS = [
    {"name": "get_sdk",
     "description": "libWolf SDK kurulum bilgisi: platforma gore indirme adresi, "
                    "kurulum komutu ve derleme adimlari. Linux/Windows paketleri "
                    "Arctian Emulator icerir (wf shot ile .wlf onizleme). "
                    "Yayinlamadan once SDK'yi kendi calisma ortamina kur.",
     "inputSchema": {"type": "object", "properties": {
         "platform": {"type": "string",
                      "enum": ["linux", "windows", "macos"],
                      "description": "Hedef platform (varsayilan: linux)"}}}},
    {"name": "get_project_template",
     "description": "Yeni bir .wlf uygulamasi icin C kaynak iskeleti ve wolf.json "
                    "uretir (libWolf wolf.h API'si).",
     "inputSchema": {"type": "object", "properties": {
         "name": {"type": "string",
                  "description": "Uygulama kimligi: a-z, 0-9, -, _"},
         "title": {"type": "string", "description": "Gorunen ad"},
         "category": {"type": "string", "description": "Kategori (varsayilan: Genel)"},
         "permissions": {"type": "array",
                         "items": {"type": "string", "enum": ["fs", "net", "exec"]},
                         "description": "Istenen izinler"},
         "width": {"type": "integer"}, "height": {"type": "integer"}}}},
    {"name": "publish_app",
     "description": "Derlenmis .wlf paketini inceleme kuyruguna gonderir. "
                    "Jeton gerekir. Kullanici onayindan sonra cagrilmalidir.",
     "inputSchema": {"type": "object", "properties": {
         "wlf_base64": {"type": "string", "description": ".wlf dosyasinin base64 icerigi"},
         "description": {"type": "string", "description": "Magaza aciklamasi"},
         "screenshots_base64": {"type": "array", "items": {"type": "string"},
                                "description": "En fazla 4 PNG/JPEG (base64)"}},
         "required": ["wlf_base64"]}},
    {"name": "my_submissions",
     "description": "Jeton sahibinin basvurularini ve durumlarini listeler. Jeton gerekir.",
     "inputSchema": {"type": "object", "properties": {
         "status": {"type": "string",
                    "enum": ["pending", "approved", "rejected", "all"],
                    "description": "Filtre (varsayilan: all)"}}}},
    {"name": "whoami",
     "description": "Jetonun bagli oldugu hesabi ve yayinlama kotasini dondurur. "
                    "Jeton gerekir.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "search_apps",
     "description": "Arctian Market'te uygulama ara (ad, gelistirici, kategori, aciklama).",
     "inputSchema": {"type": "object", "properties": {
         "query": {"type": "string", "description": "Arama terimi"}},
         "required": ["query"]}},
    {"name": "get_app",
     "description": "Bir uygulamanin tam detayini getirir (surum, izinler, paket adresi).",
     "inputSchema": {"type": "object", "properties": {
         "app_id": {"type": "string", "description": "Uygulama kimligi"}},
         "required": ["app_id"]}},
    {"name": "list_categories",
     "description": "Magazadaki kategorileri ve uygulama sayilarini listeler.",
     "inputSchema": {"type": "object", "properties": {}}},
    {"name": "top_apps",
     "description": "En cok indirilen uygulamalari listeler.",
     "inputSchema": {"type": "object", "properties": {
         "limit": {"type": "integer", "description": "Maksimum sonuc (varsayilan 10)"}}}},
]

# libWolf API'siyle yazilmis minimal uygulama iskeleti.
_TEMPLATE_C = """/* %(title)s - Arctian .wlf uygulamasi
 *
 * libWolf (wolf.h) uzerinden isletim sistemiyle konusur. C ile yazilir.
 * Derleme : sh build.sh          (Linux / macOS / WSL)
 *           build.bat            (Windows)
 * Test    : arctian-emu %(name)s.wlf
 */
#include "wolf.h"

static wolf_id g_status;

static void on_btn(wolf_id w, void *user) {
    (void)w; (void)user;
    static int n;
    char buf[64], num[16];
    n++;
    wolf_strcpy(buf, sizeof buf, "Tiklama: ");
    wolf_itoa(n, num, sizeof num);
    wolf_strcat(buf, sizeof buf, num);
    wolf_text(g_status, buf);
}

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);
    if (!wolf_ready()) return -1;

    wolf_win_title("%(title)s");
    wolf_win_size(%(w)d, %(h)d);

    wolf_id root = wolf_root();
    wolf_label(root, 24, 24, "%(title)s");
    g_status = wolf_label(root, 24, 56, "Hazir");
    wolf_id b = wolf_button(root, 24, 96, 180, 44, "Tikla");
    wolf_on_click(b, on_btn);
    return 0;
}
"""

_TEMPLATE_JSON = """{
  "name": "%(name)s",
  "title": "%(title)s",
  "version": "1.0",
  "publisher": "%(publisher)s",
  "category": "%(category)s",
  "w": %(w)d, "h": %(h)d,
  "permissions": %(perms)s
}
"""


def _slug(name):
    out = "".join(c if (c.isalnum() or c in "-_") else "-" for c in
                  (name or "").lower()).strip("-")
    out = "".join(c for c in out if c.isascii())
    return out[:32] or "uygulama"


def _app_public(a):
    return {
        "id": a["id"], "title": a["title"], "version": a["version"],
        "category": a["category"], "publisher": a["publisher"],
        "size": a["size"], "size_human": fmt_bytes(a["size"]),
        "downloads": a.get("downloads", 0),
        "description": a.get("description", ""),
        "permissions": a.get("permissions", ""),
        "url": "%s/app/%s" % (PUBLIC_URL, a["id"]),
        "package_url": "%s/packages/%s" % (PUBLIC_URL, a["file"]),
        "icon_url": "%s/icons/%s" % (PUBLIC_URL, a["id"]),
        "screenshots": ["%s/shots/%s/%s" % (PUBLIC_URL, a["id"], s["file"])
                        for s in a.get("shots", [])],
    }


def _need_user(user):
    if not user:
        raise PermissionError(
            "Bu arac icin jeton gerekli. market.kerem.site/console adresinden "
            "API jetonu uretin ve istekte 'Authorization: Bearer arct_...' "
            "basligini gonderin.")
    return user


def mcp_call_tool(name, args, user=None):
    apps = list_apps()

    # ---- SDK ----
    if name == "get_sdk":
        plat = str(args.get("platform") or "linux").lower()
        if plat not in ("linux", "windows", "macos"):
            raise ValueError("gecersiz platform: %s" % plat)
        cat = sdkdist.catalog(PUBLIC_URL)
        best = sdkdist.by_platform(plat)
        target = None
        if best:
            for a in cat["artifacts"]:
                if a["file"] == best["file"]:
                    target = a
                    break
        if not target:
            raise ValueError("%s icin kurulum dosyasi yayinda degil" % plat)
        return {
            "sdk": cat["sdk"], "version": cat["version"],
            "platform": plat,
            "download_url": target["url"],
            "sha256": target["sha256"], "size": target["size"],
            "install": cat["install"].get(plat, ""),
            "steps": [
                "1) Kurulum dosyasini indir: %s" % target["url"],
                "2) Kur: %s" % cat["install"].get(plat, "?"),
                "3) Proje olustur: get_project_template cagir",
                "4) Derle: sh build.sh  (Windows: wf.cmd build)",
                "5) Test et: wf shot <ad>.wlf  (PNG onizleme; paketle emulatordur)",
                "6) Kullanici onayindan sonra publish_app cagir",
            ],
            "build": cat["build"],
            "emulator": cat.get("emulator") or None,
            "abi_version": cat["abi_version"],
            "header": cat["header"],
            "entry": cat["entry"],
            "note": "SDK C tabanlidir; Python yalnizca paketleme (wlfpack.py) icin. "
                    "Linux/Windows paketlerinde Arctian Emulator gomludur; .wlf "
                    "pikseli pikseline OS ile ayni cizilir. "
                    "ABI v2 (SDK >= 1.3.0): wolf_style ile CSS benzeri durumlu stil "
                    "(WOLF_SEL + WOLF_SF_HOVER/PRESSED/USER1...), wolf_transition ile "
                    "durum gecisleri, wolf_anim_in/wolf_anim_pulse ile giris/nabiz "
                    "animasyonlari, WOLF_SP_FONT_PX ile serbest punto. Eski .wlf "
                    "paketleri v1 OS'te aynen calisir; v2 ozellikleri emulatorda de "
                    "birebir (ve deterministik) cizilir. v2 varligi "
                    "wolf_api_v2() ile sorgulanir.",
        }

    if name == "get_project_template":
        nm = _slug(args.get("name") or "uygulama")
        title = str(args.get("title") or nm)[:64]
        cat = str(args.get("category") or "Genel")[:24]
        try:
            w = max(240, min(1600, int(args.get("width") or 520)))
            h = max(180, min(1200, int(args.get("height") or 400)))
        except (TypeError, ValueError):
            w, h = 520, 400
        perms = args.get("permissions")
        if not isinstance(perms, list):
            perms = []
        perms = [p for p in perms if p in ("fs", "net", "exec")]
        perms_json = "[" + ", ".join('"%s"' % p for p in perms) + "]"
        publisher = (user or {}).get("name") or "Gelistirici"
        values = {"name": nm, "title": title, "category": cat,
                  "w": w, "h": h, "perms": perms_json,
                  "publisher": publisher}
        return {
            "files": {
                "main.c": _TEMPLATE_C % values,
                "wolf.json": _TEMPLATE_JSON % values,
            },
            "build": "sh build.sh   # -> %s.wlf" % nm,
            "note": "libWolf API'si wolf.h'ta; wolf_main(const wolf_api_t*) giris "
                    "noktasidir. Iskelet v1 API kullanir (her OS'te calisir); CSS "
                    "benzeri stil/gecisi/animasyon icin ABI v2'ye bakin: "
                    "wolf_style/wolf_transition/wolf_state_set/wolf_anim_in "
                    "(get_sdk notu + ornek: examples/axiom).",
        }

    # ---- yayinlama (jeton gerekir) ----
    if name == "whoami":
        u = _need_user(user)
        return {
            "sub": u["sub"], "email": u.get("email", ""),
            "name": u.get("name", ""),
            "quota": {"used": quota_used(u["sub"]), "limit": MAX_APPS_PER_USER},
            "apps": sorted(owned_app_ids(u["sub"])),
            "token_id": u.get("token_id", ""),
        }

    if name == "my_submissions":
        u = _need_user(user)
        st = str(args.get("status") or "all")
        if st not in ("pending", "approved", "rejected", "all"):
            st = "all"
        subs = list_submissions(uploader_sub=u["sub"],
                                status=None if st == "all" else st)
        return {
            "count": len(subs),
            "submissions": [{
                "sid": s["sid"], "id": s["id"], "title": s["title"],
                "version": s["version"], "category": s["category"],
                "status": s["status"],
                "submitted_at": s.get("submitted_at", 0),
                "reviewed_at": s.get("reviewed_at", 0),
                "rejection_reason": s.get("rejection_reason", ""),
                "url": "%s/app/%s" % (PUBLIC_URL, s["id"])
                       if s["status"] == "approved" else "",
            } for s in subs],
            "quota": {"used": quota_used(u["sub"]), "limit": MAX_APPS_PER_USER},
        }

    if name == "publish_app":
        u = _need_user(user)
        import base64 as _b64
        raw = args.get("wlf_base64") or ""
        if not isinstance(raw, str) or not raw.strip():
            raise ValueError("wlf_base64 bos")
        if raw.startswith("data:") and "," in raw:
            raw = raw.split(",", 1)[1]
        try:
            data = _b64.b64decode(raw, validate=True)
        except Exception:
            raise ValueError("wlf_base64 gecerli base64 degil")
        if len(data) > MAX_WLF:
            raise ValueError("paket cok buyuk (%d > %d bayt)" % (len(data), MAX_WLF))
        shots = []
        for sd in (args.get("screenshots_base64") or [])[:MAX_SHOTS]:
            if not isinstance(sd, str):
                continue
            if sd.startswith("data:") and "," in sd:
                sd = sd.split(",", 1)[1]
            try:
                shots.append(_b64.b64decode(sd, validate=True))
            except Exception:
                continue
        desc = str(args.get("description") or "")[:4000]
        try:
            sub = submit_app(u, data, desc, shots)
        except QuotaError as ex:
            return {"ok": False, "error": str(ex),
                    "quota": {"used": quota_used(u["sub"]), "limit": MAX_APPS_PER_USER}}
        invalidate_cache()
        return {
            "ok": True,
            "sid": sub["sid"], "id": sub["id"], "title": sub["title"],
            "version": sub["version"], "status": sub["status"],
            "size": sub["size"],
            "message": "Basvuru inceleme kuyruguna alindi. Onaylandiktan sonra "
                       "magazada yayinlanir.",
            "console_url": "%s/console" % PUBLIC_URL,
            "quota": {"used": quota_used(u["sub"]), "limit": MAX_APPS_PER_USER},
        }

    # ---- dizin (herkese acik) ----
    if name == "search_apps":
        q = str(args.get("query", "")).lower()
        hits = [a for a in apps if q in (a["title"] + " " + a["category"] + " " + a["id"] + " " +
                                         (a.get("description") or "") + " " + a["publisher"]).lower()]
        return {"query": args.get("query", ""), "count": len(hits),
                "apps": [_app_public(a) for a in hits[:20]]}
    if name == "get_app":
        aid = str(args.get("app_id", ""))
        for a in apps:
            if a["id"] == aid:
                return _app_public(a)
        raise ValueError("uygulama bulunamadi: %s" % aid)
    if name == "list_categories":
        cats = categories_of(apps)
        return {"categories": [{"name": k, "apps": len(v)} for k, v in cats.items()]}
    if name == "top_apps":
        try:
            limit = max(1, min(50, int(args.get("limit", 10))))
        except (TypeError, ValueError):
            limit = 10
        top = sorted(apps, key=lambda a: -a.get("downloads", 0))[:limit]
        return {"apps": [_app_public(a) for a in top]}
    raise ValueError("bilinmeyen arac: %s" % name)


def mcp_handle(req, user=None):
    """JSON-RPC mesajini isler; dondurulecek yanit veya None (no id)."""
    method = req.get("method", "")
    rid = req.get("id", None)
    if method == "initialize":
        return {"jsonrpc": "2.0", "id": rid, "result": {
            "protocolVersion": "2025-03-26",
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": MCP_SERVER_NAME, "version": MCP_VERSION},
            "instructions": (
                "ArctianMCP — Arctian Market uygulama yayinlama sunucusu.\n"
                "Akis: get_sdk ile SDK'yi kur -> get_project_template ile C iskeleti al -> "
                "derle (sh build.sh) -> arctian-emu ile test et -> kullanici onayi -> "
                "publish_app ile incelemeye gonder.\n"
                "publish_app, my_submissions ve whoami jeton ister "
                "(Authorization: Bearer arct_...). "
                "ABI v2 (SDK >= 1.3.0): wolf_style/wolf_transition/wolf_state_set/"
                "wolf_anim_in ile CSS benzeri stil, durum gecisi ve animasyon; "
                "varlik sorgusu wolf_api_v2(). Eski paketler v1 OS'te aynen calisir.")}}
    if method.startswith("notifications/"):
        return None
    if method == "ping":
        return {"jsonrpc": "2.0", "id": rid, "result": {}}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": rid, "result": {"tools": MCP_TOOLS}}
    if method == "tools/call":
        params = req.get("params") or {}
        try:
            res = mcp_call_tool(params.get("name", ""), params.get("arguments") or {},
                                user=user)
            return {"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text",
                             "text": json.dumps(res, ensure_ascii=False, indent=1)}],
                "isError": False}}
        except PermissionError as ex:
            return {"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text", "text": str(ex)}], "isError": True}}
        except Exception as ex:
            return {"jsonrpc": "2.0", "id": rid, "result": {
                "content": [{"type": "text", "text": "hata: %s" % ex}],
                "isError": True}}
    return {"jsonrpc": "2.0", "id": rid,
            "error": {"code": -32601, "message": "method bulunamadi: %s" % method}}

# =====================================================================
# handler
# =====================================================================
class Handler(BaseHTTPRequestHandler):
    server_version = "ArctianMarket/3.0"

    def _cookie(self, name):
        try:
            c = SimpleCookie(self.headers.get("Cookie", ""))
            if name in c:
                return c[name].value
        except Exception:
            pass
        return None

    def _user(self):
        return session_user(self._cookie("mkt"))

    def _send(self, code, body, ctype="text/html; charset=utf-8", extra=None):
        data = body.encode("utf-8") if isinstance(body, str) else body
        extra = list(extra or [])
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
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

    def _page(self, title, body, code=200, active=""):
        self._send(code, shell(title, body, self._user(), active))

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        qs = urllib.parse.parse_qs(parsed.query)

        if path == "/health":
            self._send(200, "ok\n", "text/plain; charset=utf-8")
        elif path.startswith("/shots/"):
            self._serve_shot(path)
        elif path == "/login":
            self._do_login()
        elif path == "/auth/callback":
            self._do_callback(qs)
        elif path == "/logout":
            drop_session(self._cookie("mkt"))
            self._redirect("/", "mkt=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax")
        elif path == "/":
            self._home()
        elif path == "/apps":
            self._all_apps(qs)
        elif path == "/charts":
            self._charts()
        elif path == "/new":
            self._newest()
        elif path == "/search":
            self._search(qs)
        elif path == "/category":
            self._category(qs)
        elif path.startswith("/app/"):
            self._app_detail(urllib.parse.unquote(path[len("/app/"):]))
        elif path == "/publish":
            self._publish_page()
        elif path == "/console":
            self._console()
        elif path == "/review":
            self._review_page()
        elif path == "/about":
            self._about()
        elif path == "/mcp":
            self._mcp_page()
        elif path == "/sdk":
            self._sdk_page()
        elif path == "/api/sdk":
            self._api_sdk()
        elif path.startswith("/downloads/sdk/"):
            self._serve_sdk(path)
        elif path == "/api/apps":
            self._api_apps()
        else:
            self._page("Bulunamadı", "<div class='empty'><h3>Sayfa yok</h3>"
                       "<p><a href='/'>Ana sayfaya dön</a></p></div>", 404)

    def do_POST(self):
        path = urllib.parse.urlparse(self.path).path
        if path == "/publish":
            self._do_publish()
        elif path == "/review/approve":
            self._do_approve()
        elif path == "/review/reject":
            self._do_reject()
        elif path == "/mcp":
            self._do_mcp()
        elif path == "/console/token":
            self._do_token_create()
        elif path == "/console/token/revoke":
            self._do_token_revoke()
        else:
            self._page("Bulunamadı", "<div class='empty'>Yok.</div>", 404)

    # ---- ana sayfa (Play tarzi satirlar) ----
    def _home(self):
        apps = list_apps()
        featured = [a for a in apps if a.get("featured")]
        popular = sorted(apps, key=lambda a: -a.get("downloads", 0))
        newest = sorted(apps, key=lambda a: -a.get("mtime", 0))
        cats = categories_of(apps)
        parts = []
        hero_app = featured[0] if featured else (popular[0] if popular else None)
        if hero_app:
            shots = hero_app.get("shots") or []
            if shots:
                vis = ("<div class='hvis' style=\"background-image:url('/shots/%s/%s')\"></div>"
                       % (e(hero_app["id"]), e(shots[0]["file"])))
            else:
                vis = "<div class='hvis'>%s</div>" % icon_html(hero_app)
            parts.append(
                """<div class='hero'>
                  <div class='htxt'>
                    <div class='hp'>★ ÖNE ÇIKAN UYGULAMA</div>
                    <h1>%s</h1>
                    <p class='hdesc'>%s</p>
                    <div class='hmeta'>
                      <div class='st'><b>%s</b>indirme</div>
                      <div class='st'><b>v%s</b>sürüm</div>
                      <div class='st'><b>%s</b>boyut</div>
                    </div>
                    <a class='btn btn-primary btn-lg' href='/app/%s'>İncele ve indir</a>
                  </div>%s</div>"""
                % (e(hero_app["title"]),
                   e((hero_app.get("description") or
                      "%s tarafından yayımlanan, mağaza onaylı bir uygulama." % hero_app["publisher"])[:280]),
                   _num(hero_app.get("downloads", 0)), e(hero_app["version"]),
                   fmt_bytes(hero_app["size"]), e(hero_app["id"]), vis))
        if not apps:
            parts.append("""<div class='empty'><h3>Mağaza henüz boş</h3>
              <p>İlk uygulamayı yayımlayan siz olun — <a href='/publish'>giriş yapın ve başvurun</a>.</p></div>""")
        else:
            if len(featured) > 1:
                parts.append(app_row("feat", featured, "Öne çıkanlar",
                                     "Editörlerin seçtiği uygulamalar", "/apps"))
            parts.append(app_row("pop", popular, "Popüler", "En çok indirilenler", "/charts"))
            parts.append(app_row("new", newest, "Yeni eklenenler", "Son yayımlananlar", "/new"))
            for i, (cat, items) in enumerate(cats.items()):
                parts.append(app_row("cat%d" % i, sorted(items, key=lambda a: -a.get("downloads", 0)),
                                     cat, "%d uygulama" % len(items),
                                     "/category?c=" + urllib.parse.quote(cat)))
            parts.append("""<section class='section'><div class='card' style='display:flex;
              justify-content:space-between;align-items:center;gap:1rem;flex-wrap:wrap'>
              <div><h3 style='margin-bottom:.3rem'>Geliştirici misiniz?</h3>
              <p style='margin:0;font-size:.9rem'>Uygulamanızı 3 adımda mağazaya taşıyın.
              Hesap başına <b>%d</b> uygulama hakkınız var.</p></div>
              <a class='btn btn-primary' href='/publish'>Uygulama yayımla</a></div></section>"""
                         % MAX_APPS_PER_USER)
        self._page("Keşfet", "".join(parts), active="/")

    def _rank_rows(self, apps, numbered=True):
        out = []
        for i, a in enumerate(apps, 1):
            out.append(
                """<a class='rank-item' href='/app/%s'>%s
                  <span class='ric'>%s</span>
                  <span class='rmeta'><span class='rt'>%s</span>
                  <span class='rs'>%s · %s · v%s</span></span>
                  <span class='rdl'>%s indirme</span></a>"""
                % (e(a["id"]),
                   ("<span class='rk'>%d</span>" % i) if numbered else "",
                   icon_inner(a),
                   e(a["title"]), e(a["publisher"]), e(a["category"]), e(a["version"]),
                   _num(a.get("downloads", 0))))
        return "".join(out)

    def _all_apps(self, qs):
        apps = list_apps()
        c = (qs.get("c", [""])[0] or "").strip()
        cats = categories_of(apps)
        chips = ["<a class='chip %s' href='/apps'>Tümü</a>" % ("on" if not c else "")]
        for cat in cats:
            chips.append("<a class='chip %s' href='/apps?c=%s'>%s</a>"
                         % ("on" if c == cat else "", urllib.parse.quote(cat), e(cat)))
        shown = [a for a in apps if a["category"] == c] if c else apps
        shown = sorted(shown, key=lambda a: -a.get("downloads", 0))
        body = ("<section class='section'><h1>%s</h1>"
                "<p class='muted'>%d uygulama</p>"
                "<div class='chips mt-4'>%s</div>"
                "<div class='grid'>%s</div></section>"
                % (e(c or "Tüm uygulamalar"), len(shown), "".join(chips),
                   "".join(app_card(a) for a in shown) or
                   "<div class='empty'>Uygulama yok.</div>"))
        self._page("Uygulamalar", body, active="/apps")

    def _charts(self):
        apps = sorted(list_apps(), key=lambda a: -a.get("downloads", 0))
        body = ("<section class='section'><h1>Sıralama</h1>"
                "<p class='muted'>Tüm zamanların indirme sayısına göre en popüler "
                "%d uygulama.</p><div class='card rank-list mt-4'>%s</div></section>"
                % (min(len(apps), 25),
                   self._rank_rows(apps[:25]) or "<div class='empty'>Henüz indirme yok.</div>"))
        self._page("Sıralama", body, active="/charts")

    def _newest(self):
        apps = sorted(list_apps(), key=lambda a: -a.get("mtime", 0))
        rows = []
        for a in apps:
            rows.append(
                """<a class='rank-item' href='/app/%s'>
                  <span class='ric'>%s</span>
                  <span class='rmeta'><span class='rt'>%s</span>
                  <span class='rs'>%s · %s · v%s · %s</span></span>
                  <span class='rdl'>%s indirme</span></a>"""
                % (e(a["id"]), icon_inner(a),
                   e(a["title"]), e(a["publisher"]), e(a["category"]), e(a["version"]),
                   e(ago_short(a.get("mtime"))), _num(a.get("downloads", 0))))
        body = ("<section class='section'><h1>Yeni gelenler</h1>"
                "<p class='muted'>Onaylanıp mağazaya eklenen en son uygulamalar.</p>"
                "<div class='card rank-list mt-4'>%s</div></section>"
                % ("".join(rows) or "<div class='empty'>Henüz uygulama yok.</div>"))
        self._page("Yeni gelenler", body, active="/new")

    def _search(self, qs):
        q = (qs.get("q", [""])[0] or "").strip()
        apps = list_apps()
        if q:
            ql = q.lower()
            apps = [a for a in apps if ql in (a["title"] + " " + a["category"] + " " +
                                              a["id"] + " " + (a.get("description") or "") +
                                              " " + a["publisher"]).lower()]
        apps = sorted(apps, key=lambda a: -a.get("downloads", 0))
        body = ("<section class='section'><h1>“%s” için sonuçlar</h1>"
                "<p class='muted'>%d sonuç%s</p>"
                "<div class='grid' style='margin-top:var(--space-5)'>%s</div></section>"
                % (e(q) if q else "Tüm uygulamalar", len(apps),
                   "" if apps else " — farklı bir terim deneyin",
                   "".join(app_card(a) for a in apps)))
        self._page("Arama: %s" % (q or "…"), body, active="/")

    def _category(self, qs):
        c = (qs.get("c", [""])[0] or "").strip()
        apps = [a for a in list_apps() if a["category"] == c] if c else list_apps()
        body = ("<section class='section'><h1>%s</h1><p class='muted'>%d uygulama</p>"
                "<div class='grid' style='margin-top:var(--space-5)'>%s</div></section>"
                % (e(c or "Kategoriler"), len(apps),
                   "".join(app_card(a) for a in apps) or "<div class='empty'>Bu kategoride uygulama yok.</div>"))
        self._page(c or "Kategoriler", body, active="/apps")

    def _app_detail(self, app_id):
        apps = list_apps()
        by_id = {a["id"]: a for a in apps}
        a = by_id.get(app_id)
        if not a:
            self._page("Bulunamadı", "<div class='empty'><h3>Uygulama yok</h3>"
                       "<p><a href='/'>Ana sayfaya dön</a></p></div>", 404)
            return
        shots = a.get("shots") or []
        shots_html = ""
        if shots:
            shots_html = ("<section class='section'><h3 class='mb-4'>Ekran görüntüleri</h3>"
                          "<div class='shots'>%s</div></section>"
                          % "".join("<div class='shot' style=\"background-image:url('/shots/%s/%s')\"></div>"
                                    % (e(a["id"]), e(s["file"])) for s in shots))
        kv_rows = [
            ("Sürüm", e(a["version"])),
            ("Yayıncı", e(a["publisher"])),
            ("Kategori", "<a href='/category?c=%s'>%s</a>" % (urllib.parse.quote(a["category"]), e(a["category"]))),
            ("Boyut", fmt_bytes(a["size"])),
            ("Paket", "<code>%s</code>" % e(a["file"])),
            ("Mağaza onayı", "<span class='badge badge-success'>✓ İncelemeden geçti</span>"),
            ("İmza", "<span class='badge badge-accent'>ASG1 · RSA-2048</span>"),
        ]
        if a.get("permissions"):
            kv_rows.append(("İzinler", e(a["permissions"])))
        kv_html = "".join("<div class='kv'><span class='k'>%s</span><span>%s</span></div>"
                          % (k, v) for k, v in kv_rows)
        desc = (a.get("description") or "").strip() or "Geliştirici açıklama girmedi."
        similar = sorted([x for x in apps if x["category"] == a["category"] and x["id"] != a["id"]],
                         key=lambda x: -x.get("downloads", 0))[:12]
        similar_html = app_row("sim", similar, "Benzer uygulamalar",
                               a["category"], "/category?c=" + urllib.parse.quote(a["category"]))
        body = ("""<section class='detail-hero'>
            %s
            <div style='flex:1;min-width:0'>
              <h1>%s</h1>
              <p class='muted' style='margin:.25rem 0 0'>%s</p>
              <div class='dmeta-strip'>
                <span class='st'><b>%s</b>indirme</span>
                <span class='st'><b>%s</b>boyut</span>
                <span class='st'><b>v%s</b>sürüm</span>
                <span class='st'><b>%s</b>kategori</span>
              </div>
              <div class='flex gap-2 mt-4'>
                <a class='btn btn-primary btn-lg' href='/packages/%s'>↓ .wlf indir</a>
                <a class='btn btn-secondary' href='/about#install'>Arctian OS'ta kurulum</a>
              </div>
            </div></section>
          <div class='detail-grid'>
            <div>
              <div class='card'><h4 class='mb-4'>Açıklama</h4>
                <p style='white-space:pre-wrap;margin:0'>%s</p></div>
              %s
            </div>
            <aside class='card'><h4 class='mb-4'>Uygulama bilgileri</h4>%s</aside>
          </div>%s"""
                % (icon_html(a), e(a["title"]), e(a["publisher"]),
                   _num(a.get("downloads", 0)), fmt_bytes(a["size"]), e(a["version"]),
                   e(a["category"]), e(a["file"]),
                   e(desc), shots_html, kv_html, similar_html))
        self._page(a["title"], body, active="")

    def _publish_page(self, err="", ok=""):
        user = self._user()
        if not user:
            self._redirect("/login?next=/publish")
            return
        msg = ""
        if err:
            msg = "<div class='alert alert-danger'>%s</div>" % e(err)
        if ok:
            msg = "<div class='alert alert-success'>%s</div>" % e(ok)
        used = quota_used(user["sub"])
        left = max(0, MAX_APPS_PER_USER - used)
        pct = min(100, int(100.0 * used / MAX_APPS_PER_USER))
        quota_box = ("""<div class='card mb-4'>
          <div class='flex justify-between items-center mb-2'>
            <b style='font-size:.9rem'>Yayınlama hakkı</b>
            <span class='muted' style='font-size:.85rem'>%d / %d kullanıldı</span></div>
          <div class='progress'><div style='width:%d%%'></div></div>
          <div class='hint' style='margin-top:.5rem;font-size:.8rem;color:var(--text-subtle)'>
            %s</div></div>"""
                     % (used, MAX_APPS_PER_USER, pct,
                        ("<b>%d</b> uygulama hakkınız kaldı. Reddedilen başvurular hakka sayılmaz; "
                         "mevcut uygulamanıza sürüm göndermek hak tüketmez." % left) if left
                        else "Hakkınız doldu. Yeni uygulama için mevcut bir başvuruyu kaldırmanız gerekir "
                             "veya reddedilen bir hakkın yerini kullanabilirsiniz."))
        subs = list_submissions(uploader_sub=user["sub"])
        my_rows = "".join(
            "<tr><td><strong>%s</strong><br><span class='muted' style='font-size:.8rem'>%s · v%s</span></td>"
            "<td>%s</td>"
            "<td class='muted' style='font-size:.85rem'>%s</td></tr>"
            % (e(s["title"]), e(s["id"]), e(s["version"]), status_badge(s["status"]),
               time.strftime("%d.%m.%Y %H:%M", time.localtime(s.get("submitted_at", 0))))
            for s in subs)
        my_table = ("<table class='table'><thead><tr><th>Başvuru</th><th>Durum</th><th>Tarih</th></tr>"
                    "<tbody>%s</tbody></table>" % my_rows) if subs else \
            "<div class='empty' style='padding:2rem'>Henüz başvuru yapmadınız.</div>"
        form = ""
        if left > 0:
            form = ("""<form method='post' action='/publish' enctype='multipart/form-data'>
                <div class='form-group'><label>Paket (.wlf) *</label>
                  <div class='drop' onclick="this.querySelector('input').click()">
                    <b>.wlf dosyasını buraya bırakın</b><span>veya tıklayıp seçin — sunucuda doğrulanır ve imzalanır</span>
                    <input type='file' name='wlf' accept='.wlf' required></div></div>
                <div class='form-group'><label>Açıklama</label>
                  <textarea name='description' maxlength='4000'
                    placeholder='Uygulamanız ne yapar? Kullanıcıya ne katıyor?'></textarea></div>
                <div class='form-group'><label>Ekran görüntüleri (en fazla 4)</label>
                  <input type='file' name='shots' accept='image/*' multiple>
                  <div class='hint'>16:10 önerilir; mağaza kartlarında ve detay sayfasında gösterilir.</div></div>
                <button class='btn btn-primary' type='submit'>İncelemeye gönder</button>
              </form>""")
        else:
            form = ("<div class='alert alert-warning'>Hesap başına %d uygulama sınırına ulaştınız. "
                    "Mevcut uygulamalarınıza <b>yeni sürüm</b> gönderebilirsiniz.</div>"
                    % MAX_APPS_PER_USER)
        body = ("""<section class='section'>
          <h1>Uygulama yayımla</h1>
          <p class='muted'>Hazır bir <code>.wlf</code> paketi yükleyin. Başvurunuz inceleme kuyruğuna
             düşer; onaylanınca mağazada ve işletim sistemi akışında görünür.</p>
          %s%s
          <div class='grid-2' style='align-items:start;margin-top:var(--space-5)'>
            <div class='card'><h3 class='mb-4'>Yeni başvuru</h3>%s</div>
            <div>
              <div class='card mb-4'><h3 class='mb-4'>Başvurularım</h3>%s</div>
              <div class='card'><h4 class='mb-4'>Nasıl .wlf üretirim?</h4>
                <pre class='code'>sh build.sh          # Linux / macOS / WSL
build.bat            # Windows
arctian-emu app.wlf  # masaüstünde test</pre>
                <p class='muted' style='font-size:.85rem;margin:.75rem 0 0'>
                  libWolf SDK'yi <a href='/sdk'>indirin</a>; uygulamalar <b>C</b> ile yazılır
                  (<code>wolf.h</code>). Sürüm, kategori ve pencere boyutu
                  <code>wolf.json</code>'dan gelir. Ayrıntılar:
                  <a href='/about'>yayınlama rehberi</a>.</p></div>
            </div></div></section>""" % (quota_box, msg, form, my_table))
        self._page("Yayımla", body, active="/")

    def _console(self):
        user = self._user()
        if not user:
            self._redirect("/login?next=/console")
            return
        subs = list_submissions(uploader_sub=user["sub"])
        used = quota_used(user["sub"])
        published = {s["id"] for s in subs if s["status"] == "approved"}
        pending_n = sum(1 for s in subs if s["status"] == "pending")
        rejected_n = sum(1 for s in subs if s["status"] == "rejected")
        stats = ("""<div class='grid' style='grid-template-columns:repeat(4,1fr);gap:1rem'>
          <div class='card'><h2>%d</h2><div class='muted' style='font-size:.8rem'>yayında</div></div>
          <div class='card'><h2>%d</h2><div class='muted' style='font-size:.8rem'>incelemede</div></div>
          <div class='card'><h2>%d</h2><div class='muted' style='font-size:.8rem'>reddedilen</div></div>
          <div class='card'><h2>%d/%d</h2><div class='muted' style='font-size:.8rem'>hak kullanımı</div></div>
        </div>""" % (len(published), pending_n, rejected_n, used, MAX_APPS_PER_USER))

        rows = "".join(
            "<tr><td><strong>%s</strong><br><span class='muted' style='font-size:.8rem'>%s · v%s</span></td>"
            "<td>%s</td>"
            "<td class='muted' style='font-size:.85rem'>%s</td>"
            "<td class='muted' style='font-size:.85rem'>%s</td></tr>"
            % (e(s["title"]), e(s["id"]), e(s["version"]), status_badge(s["status"]),
               time.strftime("%d.%m.%Y", time.localtime(s.get("submitted_at", 0))),
               (("Sebep: " + e(s.get("rejection_reason") or "belirtilmemiş"))
                if s["status"] == "rejected" else
                (time.strftime("%d.%m.%Y", time.localtime(s["reviewed_at"]))
                 if s.get("reviewed_at") else "—")))
            for s in subs)
        table = ("""<table class='table'><thead><tr><th>Başvuru</th><th>Durum</th>
          <th>Gönderim</th><th>İnceleme</th></tr></thead><tbody>%s</tbody></table>"""
                 % rows) if subs else \
            "<div class='empty'>Henüz başvuru yapmadınız.<br><br>" \
            "<a class='btn btn-primary' href='/publish'>İlk başvurunuzu gönderin</a></div>"

        tokens_html = self._tokens_section(user)

        body = ("""<section class='section'>
          <h1>Geliştirici konsolu</h1>
          <p class='muted'>Hoş geldiniz, %s. Başvurularınızı, yayınlama hakkınızı
             ve AI ajanları için API jetonlarınızı buradan yönetin.</p>
          <div class='mb-6'>%s</div>
          <div class='card'>%s</div>
          <div class='mt-4'><a class='btn btn-primary' href='/publish'>+ Yeni başvuru</a>
            <a class='btn btn-secondary' href='/sdk'>SDK indir</a></div>
        </section>%s""" % (e(user.get("name") or user.get("email", "")),
                           stats, table, tokens_html))
        self._page("Konsol", body, active="/console")

    def _tokens_section(self, user):
        toks = tokenstore.list_for(user["sub"])
        new_tok = getattr(self, "_new_token", "")
        banner = ""
        if new_tok:
            banner = ("""<div class='alert alert-success'>
              <b>Jeton oluşturuldu.</b> Bu değer yalnızca bir kez gösterilir:
              <div class='mt-4'><code style='font-size:1rem;word-break:break-all'>%s</code></div>
              <p class='muted' style='margin:.75rem 0 0;font-size:.85rem'>
                AI ajanınıza verin: <code>Authorization: Bearer &lt;jeton&gt;</code></p>
              </div>""" % e(new_tok))
        rows = "".join(
            "<tr><td><code>%s</code><br><span class='muted' style='font-size:.8rem'>%s</span></td>"
            "<td>%s</td>"
            "<td class='muted' style='font-size:.82rem'>%s</td>"
            "<td><form method='post' action='/console/token/revoke' style='margin:0'>"
            "<input type='hidden' name='tid' value='%s'>"
            "<button class='btn btn-danger btn-sm' type='submit'>İptal</button></form></td></tr>"
            % (e(t["masked"]), e(t["label"]),
               "<span class='badge badge-danger'>iptal</span>" if t["revoked"]
               else "<span class='badge badge-success'>aktif</span>",
               time.strftime("%d.%m.%Y %H:%M", time.localtime(t["created_at"]))
               + (" · son kullanım " + time.strftime("%d.%m.%y %H:%M",
                                                     time.localtime(t["last_used"]))
                  if t.get("last_used") else ""),
               e(t["id"]))
            for t in toks)
        table = ("""<table class='table'><thead><tr><th>Jeton</th><th>Durum</th>
          <th>Oluşturma</th><th></th></tr></thead><tbody>%s</tbody></table>""" % rows) \
            if toks else "<div class='empty' style='padding:1.5rem'>Henüz jeton yok.</div>"
        return ("""<section class='section'>
          <h2>API jetonları (ArctianMCP)</h2>
          <p class='muted'>AI ajanları bu jetonla mağazaya uygulama gönderebilir.
             Jetonu MCP yapılandırmasında <code>Authorization: Bearer …</code>
             başlığı olarak kullanın. Jetonlar yalnızca sizin hesabınız adına
             işlem yapar ve istediğiniz an iptal edilebilir.</p>
          %s
          <div class='card mb-4'>
            <form method='post' action='/console/token' class='flex gap-2'
                  style='align-items:flex-end;flex-wrap:wrap'>
              <div class='form-group' style='flex:1;min-width:220px;margin:0'>
                <label>Jeton adı</label>
                <input name='label' maxlength='60' placeholder='örn. Arctian Terminal'>
              </div>
              <button class='btn btn-primary' type='submit'>Jeton oluştur</button>
            </form>
          </div>
          <div class='card'>%s</div>
          <p class='muted' style='font-size:.85rem;margin-top:1rem'>
            MCP uç noktası: <code>POST %s/mcp</code> · sunucu adı
            <code>%s</code></p>
        </section>""" % (banner, table, e(PUBLIC_URL), MCP_SERVER_NAME))

    def _do_token_create(self):
        user = self._user()
        if not user:
            self._redirect("/login?next=/console")
            return
        fs = self._read_multipart(256 * 1024)
        label = fs.getvalue("label", "") or "API jetonu"
        try:
            rec = tokenstore.create(user, label)
            self._new_token = rec["token"]
        except Exception as ex:
            self._new_token = ""
            self._page("Konsol", "<div class='alert alert-danger'>%s</div>" % e(ex),
                       400, active="/console")
            return
        self._console()

    def _do_token_revoke(self):
        user = self._user()
        if not user:
            self._redirect("/login?next=/console")
            return
        fs = self._read_multipart(256 * 1024)
        tokenstore.revoke(fs.getvalue("tid", ""), user["sub"])
        self._console()

    def _review_page(self):
        user = self._user()
        if not is_admin(user):
            self._page("Erişim reddedildi", "<div class='empty'><h3>Yetkiniz yok</h3>"
                       "<p>Bu sayfa yalnızca yöneticiler içindir.</p></div>", 403)
            return
        pending = list_submissions(status="pending")
        rows = ""
        for s in pending:
            rows += ("""<tr>
              <td><strong>%s</strong><br><span class='muted' style='font-size:.8rem'>%s · v%s · %s · %s</span></td>
              <td style='width:64px'><span class='ric' style='display:grid;place-items:center;width:48px;height:48px;border-radius:12px;overflow:hidden;border:1px solid var(--border)'>%s</span></td>
              <td class='muted' style='font-size:.85rem'>%s<br>%s</td>
              <td style='min-width:260px'>
                <form method='post' action='/review/approve' style='display:inline'>
                  <input type='hidden' name='sid' value='%s'>
                  <button class='btn btn-primary btn-sm' type='submit'>✓ Onayla ve yayına al</button></form>
                <form method='post' action='/review/reject' style='margin-top:.5rem;display:flex;gap:.4rem'>
                  <input type='hidden' name='sid' value='%s'>
                  <input name='reason' placeholder='Red nedeni' style='flex:1'>
                  <button class='btn btn-danger btn-sm' type='submit'>Reddet</button></form>
              </td></tr>"""
                     % (e(s["title"]), e(s["id"]), e(s["version"]), e(s["category"]),
                        fmt_bytes(s.get("size", 0)),
                        icon_inner({"icon_svg": s.get("icon_svg", "")}),
                        e(s.get("uploader_name") or s.get("uploader_email", "")),
                        time.strftime("%d.%m.%Y %H:%M", time.localtime(s.get("submitted_at", 0))),
                        e(s["sid"]), e(s["sid"])))
        table = ("""<table class='table'><thead><tr><th>Başvuru</th><th>İkon</th>
          <th>Gönderen</th><th>İşlem</th></tr></thead><tbody>%s</tbody></table>"""
                 % rows) if pending else "<div class='empty'>Bekleyen başvuru yok. 🎉</div>"
        approved = list_submissions(status="approved")
        rejected = list_submissions(status="rejected")
        body = ("""<section class='section'><h1>İnceleme kuyruğu</h1>
          <p class='muted'>%d bekleyen · %d onaylanmış · %d reddedilmiş başvuru.</p>
          <div class='card'>%s</div>
          <div class='grid-2' style='margin-top:var(--space-5)'>
            <div class='card'><h4 class='mb-4'>Son onaylananlar</h4>
              <ul style='list-style:none;padding:0;margin:0'>%s</ul></div>
            <div class='card'><h4 class='mb-4'>Son reddedilenler</h4>
              <ul style='list-style:none;padding:0;margin:0'>%s</ul></div>
          </div></section>"""
                % (len(pending), len(approved), len(rejected), table,
                   "".join("<li style='padding:.45rem 0;border-bottom:1px solid var(--border)'>"
                           "<strong>%s</strong> <span class='muted' style='font-size:.8rem'>%s</span></li>"
                           % (e(s["title"]), e(s.get("reviewer", ""))) for s in approved[:8])
                   or "<li class='muted'>Yok</li>",
                   "".join("<li style='padding:.45rem 0;border-bottom:1px solid var(--border)'>"
                           "<strong>%s</strong> <span class='muted' style='font-size:.8rem'>%s</span></li>"
                           % (e(s["title"]), e(s.get("rejection_reason", ""))) for s in rejected[:8])
                   or "<li class='muted'>Yok</li>"))
        self._page("İnceleme", body, active="/review")

    def _about(self):
        body = """<section class='section' style='max-width:900px'>
          <h1>Yayınlama rehberi</h1>
          <p class='muted'>Arctian uygulamaları <b>C</b> ile yazılır ve
             <b>libWolf</b> (<code>wolf.h</code>) üzerinden işletim sistemiyle
             konuşur. Yayın için yalnızca mağaza anahtarıyla imzalanmış
             <code>.wlf</code> paketleri kabul edilir; her başvuru elle incelenir.</p>

          <div class='card mb-4'>
            <h3 class='mb-4'>1 · SDK'yı kur</h3>
            <p>Platformunuz için kurulum dosyasını indirin — hepsi
               <b>Arctian Emulator</b> içerir (Windows .zip; Linux kurulumda):</p>
            <div class='grid-2'>
              <div class='card'><h4>Windows 10/11</h4>
                <p class='muted' style='font-size:.85rem'>x64 · zip (emulatörlü)</p>
                <a class='btn btn-primary btn-sm' href='/downloads/sdk/libwolf-windows-x64.zip'>
                  ↓ .zip indir</a></div>
              <div class='card'><h4>Linux</h4>
                <p class='muted' style='font-size:.85rem'>x86_64 · gcc + binutils</p>
                <a class='btn btn-primary btn-sm' href='/downloads/sdk/libwolf-linux-x64.sh'>
                  ↓ .sh indir</a></div>
              <div class='card'><h4>macOS</h4>
                <p class='muted' style='font-size:.85rem'>Intel / Apple Silicon</p>
                <a class='btn btn-primary btn-sm' href='/downloads/sdk/libwolf-macos-universal.sh'>
                  ↓ .sh indir</a></div>
              <div class='card'><h4>Tümü + sha256</h4>
                <p class='muted' style='font-size:.85rem'>manifest.json</p>
                <a class='btn btn-secondary btn-sm' href='/sdk'>SDK sayfası →</a></div>
            </div>
            <pre class='code'># Linux / macOS
sh libwolf-linux-x64.sh          # macOS: libwolf-macos-universal.sh

# Windows: libwolf-windows-x64.zip — acin, PATH'e ekleyin</pre>
          </div>

          <div class='card mb-4'>
            <h3 class='mb-4'>2 · Proje oluştur ve derle</h3>
            <p>SDK içindeki örneklerden başlayın; her proje bir <code>main.c</code>
               ve bir <code>wolf.json</code> içerir.</p>
            <pre class='code'># Linux / macOS / WSL
cd ~/.local/share/Arctian/libWolf/examples/merhaba
sh build.sh            # -> merhaba.wlf
wf shot merhaba.wlf    # emulatörde PNG onizleme (pikseli pikseline OS)

# Windows (WSL uzerinden)
cd %%USERPROFILE%%\\libWolf\\examples\\merhaba
build.bat              # -> merhaba.wlf
wf.cmd shot merhaba.wlf</pre>
            <p class='muted' style='font-size:.85rem;margin-bottom:0'>
              Derleyici <code>gcc</code>, bağlayıcı <code>ld</code>, <code>objcopy</code>
              ve <code>python3</code> kullanılır (paketleme adımı için).</p>
          </div>

          <div class='card mb-4'>
            <h3 class='mb-4'>3 · Emülatörde test et</h3>
            <p>Paketi gerçek işletim sistemine kurmadan, aynı arayüzle masaüstünüzde
               çalıştırın:</p>
            <pre class='code'>arctian-emu merhaba.wlf</pre>
            <p class='muted' style='font-size:.85rem;margin-bottom:0'>
              Emülatör, OS ile aynı LVGL sürümünü ve aynı <code>wolf_api_t</code>
              tablosunu kullanır. Geliştirme paketleri imzasız çalışır.</p>
          </div>

          <div class='card mb-4' id='review'>
            <h3 class='mb-4'>4 · İncelemeye gönder</h3>
            <p>İki yol var:</p>
            <ul style='color:var(--text-muted);padding-left:1.2rem'>
              <li><a href='/publish'>Yayımlama formu</a> — dosyayı elle yükleyin.</li>
              <li><b>ArctianMCP</b> — AI ajanınız jetonla bağlanıp derlemeyi ve
                  yüklemeyi yapar. <a href='/mcp'>MCP sayfası →</a></li>
            </ul>
            <p class='muted' style='font-size:.85rem;margin-bottom:0'>
              Hesap başına <b>%d uygulama</b> sınırı vardır; mevcut uygulamaya yeni
              sürüm göndermek hak tüketmez, reddedilen başvurular hakka sayılmaz.</p>
          </div>

          <div class='card mb-4' id='signing'>
            <h3 class='mb-4'>5 · İmza ve yayın</h3>
            <p>Yerel derleme <b>imzasızdır</b> — mağaza özel anahtarı geliştiriciye
               verilmez. Onaydan sonra sunucu paketi doğrular, <b>mağaza anahtarıyla
               yeniden imzalar</b> (ASG1 · RSA-2048 · SHA-256) ve yayına alır.
               İşletim sistemi kurulumda bu imzayı doğrular.</p>
            <p class='muted' style='font-size:.85rem;margin-bottom:0'>
              Bu yüzden imzasız bir paket OS'te <code>wf install</code> ile kurulamaz;
              test için emülatörü kullanın.</p>
          </div>

          <div class='card' id='policy'>
            <h3 class='mb-4'>İçerik politikası</h3>
            <ul style='margin:0;padding-left:1.2rem;color:var(--text-muted)'>
              <li>Yalnızca <code>.wlf</code> paketleri kabul edilir; format ve bütünlük
                  sunucuda doğrulanır.</li>
              <li>İzinler (<code>fs</code> / <code>net</code> / <code>exec</code>)
                  manifestte açıkça belirtilmelidir.</li>
              <li>Kötü amaçlı kod, telif ihlali ve yanıltıcı içerik reddedilir.</li>
              <li>Hesap başına en fazla <b>%d</b> uygulama yayımlanabilir.</li>
              <li>Reddedilen başvuruların gerekçesi konsolda görünür.</li>
            </ul>
          </div>
        </section>""" % (MAX_APPS_PER_USER, MAX_APPS_PER_USER)
        self._page("Rehber", body, active="")

    def _mcp_page(self):
        user = self._user()
        tok_rows = ""
        if user:
            toks = [t for t in tokenstore.list_for(user["sub"]) if not t["revoked"]]
            tok_rows = ("<div class='alert alert-success' style='margin-top:1rem'>"
                        "Hesabınızda <b>%d</b> aktif jeton var. "
                        "<a href='/console'>Konsolda yönetin →</a></div>" % len(toks))
        else:
            tok_rows = ("<div class='alert' style='margin-top:1rem'>Yayınlama araçları için "
                        "<a href='/login?next=/mcp'>Google ile giriş</a> yapıp konsoldan "
                        "jeton oluşturun.</div>")
        tools_rows = "".join(
            "<tr><td><code>%s</code></td><td>%s</td><td>%s</td></tr>"
            % (e(t["name"]), e(t["description"]),
               "<span class='badge badge-warning'>jeton</span>"
               if t["name"] in ("whoami", "publish_app", "my_submissions")
               else "<span class='badge'>açık</span>")
            for t in MCP_TOOLS)
        body = ("""<section class='section' style='max-width:900px'>
          <h1>%s</h1>
          <p class='muted'>Mağaza ve yayınlama akışı, yapay zekâ ajanları için bir
             <b>Model Context Protocol</b> sunucusu üzerinden de açıktır. Ajanınız
             SDK'yı kurar, C kodunu yazar, derler, test eder ve <b>sizin onayınızla</b>
             incelemeye gönderir.</p>
          <div class='card mb-4'>
            <h3 class='mb-4'>Bağlantı</h3>
            <div class='kv'><span class='k'>Sunucu adı</span><span><code>%s</code></span></div>
            <div class='kv'><span class='k'>Uç nokta</span><span><code>POST %s/mcp</code></span></div>
            <div class='kv'><span class='k'>Taşıma</span><span>Streamable HTTP · JSON-RPC 2.0</span></div>
            <div class='kv'><span class='k'>Protokol</span><span><code>2025-03-26</code></span></div>
            <div class='kv'><span class='k'>Kimlik</span><span>Dizin açık · yayınlama <code>Bearer</code> jetonlu</span></div>
          </div>
          <div class='card mb-4'>
            <h3 class='mb-4'>Araçlar</h3>
            <table class='table'><thead><tr><th>Araç</th><th>İşlev</th><th>Yetki</th></tr></thead>
            <tbody>%s</tbody></table>
          </div>
          <div class='card mb-4'>
            <h3 class='mb-4'>Akış</h3>
            <div class='stepper' style='display:flex;gap:.5rem;flex-wrap:wrap'>
              <span class='badge badge-accent'>1 · get_sdk</span>
              <span class='badge badge-accent'>2 · get_project_template</span>
              <span class='badge badge-accent'>3 · derle</span>
              <span class='badge badge-accent'>4 · arctian-emu</span>
              <span class='badge badge-accent'>5 · kullanıcı onayı</span>
              <span class='badge badge-accent'>6 · publish_app</span>
            </div>
            <p class='muted' style='font-size:.85rem;margin:1rem 0 0'>
              Ajan SDK'yı kendi çalışma ortamına kurar (Linux / Windows / macOS),
              kodu C ile yazar, <code>sh build.sh</code> ile derler, emülatörde
              doğrular. Yükleme yalnızca kullanıcı onayından sonra yapılır.</p>
          </div>
          <div class='card mb-4'>
            <h3 class='mb-4'>İstemci yapılandırması</h3>
            <pre class='code'>{
  "mcpServers": {
    "ArctianMCP": {
      "type": "http",
      "url": "%s/mcp",
      "headers": {
        "Authorization": "Bearer arct_&lt;jeton&gt;"
      }
    }
  }
}</pre>
            <p class='muted' style='font-size:.85rem;margin:.75rem 0 0'>
              <code>Authorization</code> başlığı yalnızca yayınlama araçları için
              gerekir; dizin araçları jetonsuz çalışır. Jeton:
              <a href='/console'>Geliştirici konsolu → API jetonları</a>.</p>
            %s
          </div>
          <div class='card'>
            <h3 class='mb-4'>Örnek çağrı</h3>
            <pre class='code'>curl -s -X POST %s/mcp \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer arct_&lt;jeton&gt;' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call",
       "params":{"name":"get_sdk","arguments":{"platform":"linux"}}}'</pre>
            <p class='muted' style='font-size:.85rem;margin:.75rem 0 0'>
              Ham JSON dizin: <a href='/api/apps'>/api/apps</a> ·
              SDK kataloğu: <a href='/api/sdk'>/api/sdk</a></p>
          </div>
        </section>"""
                % (MCP_SERVER_NAME, MCP_SERVER_NAME, e(PUBLIC_URL),
                   tools_rows, e(PUBLIC_URL), tok_rows, e(PUBLIC_URL)))
        self._page("MCP", body, active="/mcp")

    # ---- SDK dagitimi ----
    def _sdk_page(self):
        cat = sdkdist.catalog("")
        rows = ""
        for a in cat["artifacts"]:
            rows += ("""<tr>
              <td><strong>%s</strong><br>
                <span class='muted' style='font-size:.8rem'>%s · %s</span></td>
              <td class='muted' style='font-size:.82rem'>%s</td>
              <td class='muted' style='font-size:.82rem'>%s</td>
              <td><a class='btn btn-primary btn-sm'
                     href='/downloads/sdk/%s'>↓ İndir</a></td>
            </tr>""" % (e(a["platform_label"]), e(a["arch"]), e(a["kind"]),
                        sdkdist.fmt_size(a["size"]),
                        "<code>%s</code>" % e(a["sha256"][:16]) if a["sha256"] else "-",
                        e(a["file"])))
        body = ("""<section class='section' style='max-width:980px'>
          <h1>libWolf SDK</h1>
          <p class='muted'>Arctian uygulamaları <b>C</b> ile yazılır. SDK; başlığı
             (<code>wolf.h</code>), istemci kütüphanesini, derleme betiklerini,
             paketleyiciyi ve <b>Arctian Emulator</b>&#39;ü içerir.
             Sürüm <b>%s</b> · ABI <b>%d</b>.</p>
          <div class='card mb-4'>
            <table class='table'><thead><tr><th>Platform</th><th>Boyut</th>
              <th>sha256</th><th></th></tr></thead><tbody>%s</tbody></table>
          </div>
          <div class='grid-2'>
            <div class='card'><h3 class='mb-4'>Kurulum</h3>
              <pre class='code'># Linux
sh libwolf-linux-x64.sh

# macOS
sh libwolf-macos-universal.sh

# Windows (taşınabilir, emulatörlü)
libwolf-windows-x64.zip</pre>
              <p class='muted' style='font-size:.85rem;margin-bottom:0'>
                Betikler SDK'yı ve emulatörü kullanıcı dizinine kurar,
                <code>PATH</code>'e ekler.</p>
            </div>
            <div class='card'><h3 class='mb-4'>Derleme</h3>
              <pre class='code'>cd examples/merhaba
sh build.sh        # Linux / macOS / WSL
wf.cmd build       # Windows

wf shot merhaba.wlf     # PNG önizleme</pre>
              <p class='muted' style='font-size:.85rem;margin-bottom:0'>
                Gereksinimler: <code>gcc</code> (veya clang), <code>ld</code>,
                <code>objcopy</code>, <code>python3</code>.</p>
            </div>
          </div>
          <div class='card mt-4'>
            <h3 class='mb-4'>Arctian Emulator · <span class='badge badge-accent'>v1.2</span></h3>
            <p class='muted'>Linux (X11 + headless) ve Windows (GDI + headless)
              ikilileri paketlerle gelir. İşletim sisteminin kendi kaynakları
              aynen derlendiği için çıktısı OS ile <b>pikseli pikseline</b> —
              Windows ve Linux görüntüleri baytı baytına aynıdır.</p>
            <pre class='code'>wf run  uygulama.wlf     # pencerede çalıştır
wf shot uygulama.wlf     # PNG ekran görüntüsü
wf info uygulama.wlf     # başlık + izinler + imza denetimi
arctian-emu app.wlf --headless --frames 150 \
    --click 10:90:82 --shot onay.png   # tiklamali otomatik test</pre>
          </div>
          <div class='card mt-4'>
            <h3 class='mb-4'>Giriş noktası ve API</h3>
            <pre class='code'>#include "wolf.h"

int wolf_main(const wolf_api_t *api) {
    wolf_bind(api);                  /* zorunlu */
    wolf_id root = wolf_root();
    wolf_label(root, 24, 24, "Merhaba Arctian");
    wolf_id b = wolf_button(root, 24, 64, 180, 44, "Tıkla");
    wolf_on_click(b, on_click);
    return 0;
}</pre>
            <p class='muted' style='font-size:.85rem;margin-bottom:0'>
              Widget, dosya sistemi, ağ, pano ve sistem çağrılarının tamamı
              <code>wolf_*</code> işlevleridir. <code>wolf_api_t</code> tablosu
              çalışma anında işletim sistemi tarafından doldurulur.</p>
          </div>
          <div class='card mt-4'>
            <h3 class='mb-4'>ABI v2 · stil, geçiş, animasyon</h3>
            <p class='muted'>SDK 1.3.0 ile <b>ABI 2</b>: durumlu stiller, durum
              geçişleri ve giriş animasyonları. Eski uygulamalar v1 OS'te
              aynen çalışır; yeni özellikler <code>wolf_api_v2()</code> ile yoklanır.</p>
            <pre class='code'>wolf_panel(root, 20, 20, 240, 96);
wolf_style(c, WOLF_SP_RADIUS, 12, 0);
wolf_style(c, WOLF_SP_BG_COLOR, 0xFFFFFF, 0);
wolf_style(c, WOLF_SP_FONT_PX, 15, 0);   /* serbest punto  */
wolf_style(c, WOLF_SP_BG_COLOR, 0x131111,
           WOLF_SEL(WOLF_SF_USER1, 0));  /* koyu tema hali */
wolf_transition(c, WOLF_TP_ALL, 250, 0, WOLF_EASE_OUT);
wolf_anim_in(c, 12, 500, 60);            /* fadeUp girisi  */
wolf_state_set(c, WOLF_SF_USER1, 1);     /* temayi degistir */</pre>
            <p class='muted' style='font-size:.85rem;margin-bottom:0'>
              Tam örnek: SDK içindeki <code>examples/axiom</code> — iki temalı,
              animasyonlu bir arayüz tek <code>main.c</code> içinde.</p>
          </div>
          <div class='card mt-4'>
            <h3 class='mb-4'>Doğrulama</h3>
            <p class='muted'>İndirdiğiniz dosyanın bütünlüğünü kontrol edin:</p>
            <pre class='code'>sha256sum libwolf-linux-x64.sh        # Linux
shasum -a 256 libwolf-macos-universal.sh   # macOS
certutil -hashfile libwolf-windows-x64.exe SHA256</pre>
            <p class='muted' style='font-size:.85rem;margin-bottom:0'>
              Makine okunur katalog: <a href='/api/sdk'>/api/sdk</a> ·
              <a href='/downloads/sdk/manifest.json'>manifest.json</a></p>
          </div>
        </section>""" % (e(cat["version"]), int(cat.get("abi_version") or 1), rows))
        self._page("SDK", body, active="/sdk")

    def _api_sdk(self):
        cat = sdkdist.catalog(PUBLIC_URL)
        self._send(200, json.dumps(cat, ensure_ascii=False),
                   "application/json; charset=utf-8")

    def _serve_sdk(self, path):
        fn = urllib.parse.unquote(path[len("/downloads/sdk/"):])
        full = sdkdist.safe_path(fn)
        if not full:
            self._send(404, b"not found\n", "text/plain; charset=utf-8")
            return
        ext = os.path.splitext(fn)[1].lower()
        ctype = {".sh": "application/x-sh", ".exe": "application/octet-stream",
                 ".json": "application/json", ".txt": "text/plain"}.get(
                     ext, "application/octet-stream")
        with open(full, "rb") as f:
            data = f.read()
        self._send(200, data, ctype, extra=[
            ("Content-Disposition", 'attachment; filename="%s"' % fn),
            ("Cache-Control", "public, max-age=300"),
        ])

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
                       extra=[("Cache-Control", "public, max-age=86400")])

    def _read_multipart(self, limit):
        import formdata
        length = int(self.headers.get("Content-Length", "0") or "0")
        if length > limit:
            raise ValueError("istek çok büyük")
        return formdata.parse(self.rfile, self.headers, length)

    # ---- islemler ----
    def _do_login(self):
        if not google_configured():
            self._page("Google girişi", "<div class='empty'><h3>Yapılandırılmamış</h3>"
                       "<p>GOOGLE_CLIENT_ID / GOOGLE_CLIENT_SECRET ayarlanmalı.</p></div>", 503)
            return
        state = secrets.token_urlsafe(16)
        _states[state] = time.time() + 600
        self._redirect(google_auth_url(state))

    def _do_callback(self, qs):
        if not google_configured():
            self._page("Hata", "<div class='empty'>OAuth yapılandırılmamış.</div>", 503)
            return
        code = (qs.get("code", [""])[0] or "")
        state = (qs.get("state", [""])[0] or "")
        err = (qs.get("error", [""])[0] or "")
        if err:
            self._page("Giriş iptal", "<div class='empty'><h3>İptal</h3><p>%s</p></div>" % e(err))
            return
        if not state or state not in _states or _states.pop(state) < time.time():
            self._page("Hata", "<div class='empty'><h3>Oturum süresi doldu</h3>"
                       "<p>Tekrar deneyin.</p></div>", 400)
            return
        if not code:
            self._page("Hata", "<div class='empty'>Kod alınamadı.</div>", 400)
            return
        try:
            user = google_exchange(code)
        except Exception as ex:
            self._page("Hata", "<div class='empty'><h3>Doğrulama başarısız</h3>"
                       "<p>%s</p></div>" % e(ex), 400)
            return
        tok = new_session(user)
        self._redirect("/", "mkt=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Lax"
                       % (tok, SESSION_TTL))

    def _do_publish(self):
        user = self._user()
        if not user:
            self._send(401, json.dumps({"error": "giriş gerekli"}),
                       "application/json; charset=utf-8")
            return
        try:
            fs = self._read_multipart(MAX_WLF + 8 * 1024 * 1024)
        except Exception as ex:
            self._publish_page(err="Yükleme okunamadı: %s" % ex)
            return
        if "wlf" not in fs:
            self._publish_page(err="Dosya seçilmedi (.wlf gerekli).")
            return
        data = fs["wlf"].file.read(MAX_WLF + 1)
        desc = (fs.getvalue("description", "") or "").strip()
        shot_parts = fs.getlist("shots") if hasattr(fs, "getlist") else []
        shots_data = [p.file.read(4 * 1024 * 1024 + 1) for p in shot_parts]
        try:
            sub = submit_app(user, data, desc, shots_data)
        except QuotaError as ex:
            self._publish_page(err=str(ex))
            return
        except wlflib.WlfError as ex:
            self._publish_page(err="Paket reddedildi: %s" % ex)
            return
        except Exception as ex:
            self._publish_page(err="Yayımlanamadı: %s" % ex)
            return
        invalidate_cache()
        self._publish_page(ok="Başvurunuz alındı: %s v%s — inceleme kuyruğunda. "
                           "Durumu konsolunuzdan takip edebilirsiniz."
                           % (sub["title"], sub["version"]))

    def _do_approve(self):
        user = self._user()
        if not is_admin(user):
            self._page("Erişim reddedildi", "<div class='empty'>Yetkiniz yok.</div>", 403)
            return
        fs = self._read_multipart(1024 * 1024)
        sid = fs.getvalue("sid", "")
        sub, err = approve_submission(sid, user.get("email", ""))
        if err:
            self._page("İnceleme", "<div class='alert alert-danger'>%s</div>" % e(err),
                       400, active="/review")
            return
        invalidate_cache()
        self._review_page()

    def _do_reject(self):
        user = self._user()
        if not is_admin(user):
            self._page("Erişim reddedildi", "<div class='empty'>Yetkiniz yok.</div>", 403)
            return
        fs = self._read_multipart(1024 * 1024)
        sid = fs.getvalue("sid", "")
        reason = fs.getvalue("reason", "")
        sub, err = reject_submission(sid, user.get("email", ""), reason)
        if err:
            self._page("İnceleme", "<div class='alert alert-danger'>%s</div>" % e(err),
                       400, active="/review")
            return
        self._review_page()

    def _bearer_user(self):
        """Authorization: Bearer arct_... -> kullanici sozlugu (yoksa None)."""
        hdr = self.headers.get("Authorization", "") or ""
        if hdr[:7].lower() != "bearer ":
            return None
        return tokenstore.verify(hdr[7:].strip())

    def _do_mcp(self):
        try:
            length = int(self.headers.get("Content-Length", "0") or "0")
            if length > 16 * 1024 * 1024:
                raise ValueError("istek cok buyuk")
            req = json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception as ex:
            self._send(400, json.dumps({"jsonrpc": "2.0", "id": None,
                                        "error": {"code": -32700, "message": str(ex)}}),
                       "application/json; charset=utf-8")
            return
        user = self._bearer_user()
        if isinstance(req, list):
            replies = [r for r in (mcp_handle(m, user=user) for m in req)
                       if r is not None]
            if not replies:
                self._send(202, "", "application/json")
                return
            self._send(200, json.dumps(replies, ensure_ascii=False),
                       "application/json; charset=utf-8")
            return
        reply = mcp_handle(req, user=user)
        if reply is None:
            self._send(202, "", "application/json")
            return
        self._send(200, json.dumps(reply, ensure_ascii=False),
                   "application/json; charset=utf-8")

    def _api_apps(self):
        out = [_app_public(a) for a in list_apps()]
        self._send(200, json.dumps({"apps": out}, ensure_ascii=False),
                   "application/json; charset=utf-8")

    def log_message(self, fmt, *args):
        sys.stderr.write("[market] " + (fmt % args) + "\n")


def main():
    os.makedirs(PACKAGES, exist_ok=True)
    os.makedirs(DATA, exist_ok=True)
    os.makedirs(PENDING_DIR, exist_ok=True)
    srv = ThreadingHTTPServer((BIND, PORT), Handler)
    n = len(list_apps())
    pending = len(list_submissions(status="pending"))
    print("Arctian Market http://%s:%d (%d uygulama, %d bekleyen, google=%s, admin=%s)" %
          (BIND, PORT, n, pending, "evet" if google_configured() else "HAYIR",
           ",".join(ADMIN_EMAILS) or "yok"))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
