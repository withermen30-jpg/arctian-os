#!/usr/bin/env python3
"""
tokens — Arctian Market API erisim jetonlari (AI ajanlari / otomasyon).

Jeton bicimi:  arct_<40 hex>
Saklama      :  yalnizca SHA-256 ozeti (data/tokens.json)
Yetki        :  jeton, olusturan kullanicinin kimligiyle islem yapar
                (yayin basvurusu, durum sorgulama).

MCP sunucusu bu jetonlari `Authorization: Bearer arct_...` ile dogrular.
Yalnizca Python 3 standart kutuphanesi.
"""
import hashlib
import json
import os
import secrets
import time

_HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.environ.get("DUCKSTORE_BASE", "/opt/duckstore")

TOKEN_PREFIX = "arct_"
MAX_TOKENS_PER_USER = 10


def _data_dir():
    base = os.environ.get("DUCKSTORE_BASE", BASE)
    return os.path.join(base, "data")


def _path():
    return os.path.join(_data_dir(), "tokens.json")


def _load():
    try:
        with open(_path(), "r", encoding="utf-8") as f:
            d = json.load(f)
        return d if isinstance(d, dict) else {}
    except (OSError, ValueError):
        return {}


def _save(d):
    p = _path()
    os.makedirs(os.path.dirname(p), exist_ok=True)
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(d, f, ensure_ascii=False, indent=2)
    os.replace(tmp, p)


def _hash(token):
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def _mask(token):
    return token[:9] + "…" + token[-4:]


def create(user, label=""):
    """Yeni jeton uretir. Donen sozlukte `token` alani YALNIZCA burada gorunur."""
    sub = str(user.get("sub") or "")
    if not sub:
        raise ValueError("kullanici kimligi yok")
    d = _load()
    mine = [k for k, v in d.items() if v.get("sub") == sub and not v.get("revoked")]
    if len(mine) >= MAX_TOKENS_PER_USER:
        raise ValueError("en fazla %d aktif jeton" % MAX_TOKENS_PER_USER)

    raw = TOKEN_PREFIX + secrets.token_hex(20)
    tid = secrets.token_hex(6)
    rec = {
        "id": tid,
        "label": (label or "API jetonu").strip()[:60],
        "sub": sub,
        "email": user.get("email", ""),
        "name": user.get("name", ""),
        "created_at": int(time.time()),
        "last_used": 0,
        "revoked": False,
        "masked": _mask(raw),
    }
    d[_hash(raw)] = rec
    _save(d)
    out = dict(rec)
    out["token"] = raw
    return out


def verify(token):
    """Jetonu dogrular; kullanici sozlugu dondurur (yoksa None)."""
    if not token or not token.startswith(TOKEN_PREFIX):
        return None
    h = _hash(token.strip())
    d = _load()
    rec = d.get(h)
    if not rec or rec.get("revoked"):
        return None
    # last_used'i her istekte yazmak gereksiz I/O; 60 sn'de bir guncelle.
    now = int(time.time())
    if now - int(rec.get("last_used") or 0) > 60:
        rec["last_used"] = now
        d[h] = rec
        try:
            _save(d)
        except OSError:
            pass
    return {"sub": rec["sub"], "email": rec.get("email", ""),
            "name": rec.get("name", ""), "token_id": rec["id"]}


def list_for(sub):
    d = _load()
    out = []
    for h, v in d.items():
        if v.get("sub") != sub:
            continue
        out.append({k: v.get(k) for k in
                    ("id", "label", "masked", "created_at", "last_used", "revoked")})
    out.sort(key=lambda x: -x.get("created_at", 0))
    return out


def list_all():
    """Yonetim paneli icin TUM jetonlar (ozet alanlar, hash'siz)."""
    out = []
    for v in _load().values():
        out.append({k: v.get(k) for k in
                    ("id", "label", "masked", "sub", "email", "name",
                     "created_at", "last_used", "revoked")})
    out.sort(key=lambda x: -x.get("created_at", 0))
    return out


def revoke(tid, sub):
    d = _load()
    for h, v in list(d.items()):
        if v.get("id") == tid and v.get("sub") == sub:
            v["revoked"] = True
            d[h] = v
            _save(d)
            return True
    return False


def count_for(sub):
    return len([1 for v in _load().values()
                if v.get("sub") == sub and not v.get("revoked")])
