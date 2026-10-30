#!/usr/bin/env python3
"""
storemeta — uygulama yan-verisi (aciklama + fotograf) ve yardimcilar.

`.wlf` dosyasi yalnizca tek bir ikon tasir; mağazadaki uzun aciklama ve
ekran fotograflari burada, paketin YANINDA tutulur:

    data/appmeta.json              -> { "<id>": {description, shots, ...} }
    data/screenshots/<id>/NN.png   -> ekran fotograflari (01.png, 02.png, ...)

Bu dosya hem yonetim paneli (port 129) hem de herkese acik magaza sitesi
(port 47584) tarafindan kullanilir. Yalnizca Python 3 stdlib.
"""
import json
import os
import re
import time

_MAX_SHOTS = 6          # uygulama basina en fazla fotograf
_MAX_DESC = 4000        # aciklama en fazla bu kadar karakter
_IMG_KINDS = {
    b"\x89PNG\r\n\x1a\n": ("png", "image/png"),
    b"\xff\xd8\xff": ("jpg", "image/jpeg"),
}
_ID_OK = re.compile(r"^[a-z0-9_]{1,31}$")


class MetaError(ValueError):
    pass


def _data_dir(data_dir):
    os.makedirs(data_dir, exist_ok=True)
    return data_dir


def _meta_path(data_dir):
    return os.path.join(_data_dir(data_dir), "appmeta.json")


def _shots_dir(data_dir, app_id):
    return os.path.join(_data_dir(data_dir), "screenshots", app_id)


def load_meta(data_dir):
    """Tum appmeta sozlugu. Bozuk dosyada bos doner (kayip veri yok)."""
    try:
        with open(_meta_path(data_dir), "r", encoding="utf-8") as f:
            d = json.load(f)
        return d if isinstance(d, dict) else {}
    except Exception:
        return {}


def save_meta(data_dir, meta):
    p = _meta_path(data_dir)
    tmp = p + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)
    os.replace(tmp, p)


def get_app(data_dir, app_id):
    return load_meta(data_dir).get(app_id, {})


def clean_id(app_id):
    app_id = str(app_id or "").strip().lower()
    if not _ID_OK.match(app_id):
        raise MetaError("geçersiz uygulama adı")
    return app_id


def set_app(data_dir, app_id, **fields):
    """Bir uygulamanin yan-verisini gunceller (kismi guncelleme).

    Desteklenen alanlar: description, publisher, featured, hidden.
    """
    app_id = clean_id(app_id)
    meta = load_meta(data_dir)
    rec = meta.get(app_id, {})
    if "description" in fields:
        d = str(fields["description"] or "").strip()
        if len(d) > _MAX_DESC:
            raise MetaError("açıklama çok uzun (en fazla %d karakter)" % _MAX_DESC)
        rec["description"] = d
    if "publisher" in fields and fields["publisher"] is not None:
        rec["publisher"] = str(fields["publisher"]).strip()[:60]
    if "featured" in fields:
        rec["featured"] = bool(fields["featured"])
    if "hidden" in fields:
        rec["hidden"] = bool(fields["hidden"])
    rec.setdefault("updated", int(time.time()))
    meta[app_id] = rec
    save_meta(data_dir, meta)
    return rec


def drop_app(data_dir, app_id):
    """Bir uygulamanin yan-verisini ve fotograflarini siler."""
    app_id = clean_id(app_id)
    meta = load_meta(data_dir)
    if app_id in meta:
        meta.pop(app_id, None)
        save_meta(data_dir, meta)
    import shutil
    shutil.rmtree(_shots_dir(data_dir, app_id), ignore_errors=True)


# ---------------------------------------------------------------- fotograflar
def sniff_image(data):
    """(uzanti, content_type) dondurur; taninmayan gorselde MetaError."""
    for magic, kind in _IMG_KINDS.items():
        if data[:len(magic)] == magic:
            return kind
    if data[:4] == b"RIFF" and data[8:12] == b"WEBP":
        return ("webp", "image/webp")
    raise MetaError("görsel PNG, JPG veya WEBP olmalı")


def list_shots(data_dir, app_id):
    """Uygulamanin fotograflarini (siralı) listeler."""
    d = _shots_dir(data_dir, app_id)
    if not os.path.isdir(d):
        return []
    out = []
    for fn in sorted(os.listdir(d)):
        if fn.lower().endswith((".png", ".jpg", ".jpeg", ".webp")):
            try:
                out.append({"file": fn, "size": os.path.getsize(
                    os.path.join(d, fn))})
            except OSError:
                pass
    return out


def add_shot(data_dir, app_id, data, max_bytes=4 * 1024 * 1024):
    """Bir fotograf ekler. Eklenen kaydin bilgisi dondurur."""
    app_id = clean_id(app_id)
    if len(data) > max_bytes:
        raise MetaError("görsel çok büyük (en fazla 4 MB)")
    ext, ctype = sniff_image(data)
    d = _shots_dir(data_dir, app_id)
    os.makedirs(d, exist_ok=True)
    existing = list_shots(data_dir, app_id)
    if len(existing) >= _MAX_SHOTS:
        raise MetaError("en fazla %d fotoğraf eklenebilir" % _MAX_SHOTS)
    # siradaki numarayi bul
    used = set()
    for s in existing:
        m = re.match(r"^(\d+)", s["file"])
        if m:
            used.add(int(m.group(1)))
    n = 1
    while n in used:
        n += 1
    fn = "%02d.%s" % (n, ext)
    with open(os.path.join(d, fn), "wb") as f:
        f.write(data)
    return {"file": fn, "size": len(data), "type": ctype}


def shot_path(data_dir, app_id, filename):
    """Guvenli fotograf yolu (yoksa None). dizin gezintisini engeller."""
    fn = os.path.basename(str(filename or "").replace("\\", "/"))
    if not re.match(r"^[A-Za-z0-9_.\-]{1,40}\.(png|jpg|jpeg|webp)$", fn, re.I):
        return None
    try:
        app_id = clean_id(app_id)
    except MetaError:
        return None
    full = os.path.join(_shots_dir(data_dir, app_id), fn)
    return full if os.path.isfile(full) else None


def drop_shot(data_dir, app_id, filename):
    p = shot_path(data_dir, app_id, filename)
    if not p:
        return False
    try:
        os.remove(p)
        return True
    except OSError:
        return False


def description_of(data_dir, app_id, default=""):
    return str(load_meta(data_dir).get(app_id, {}).get("description") or default)
