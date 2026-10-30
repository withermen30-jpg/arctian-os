# -*- coding: utf-8 -*-
"""
Standart kutuphaneden cgi yerine gelen form ayristirici.

Python 3.13'te `cgi` modulu kaldirildi; bu modul `cgi.FieldStorage` ile
ayni arayuzu saglar (yalnizca kullanilan kisimlar):

    fs = formdata.parse(rfile, headers, content_length)
    fs.getvalue("pw", "")      -> str
    "zip" in fs                -> bool
    fs["zip"].file.read(n)     -> bayt
    fs["zip"].filename         -> str

Yalnizca Python 3 standart kutuphanesi kullanilir.
"""
import io
import re
from urllib.parse import parse_qs, unquote_plus

_NAME_RE = re.compile(r'name="([^"]*)"')
_FILE_RE = re.compile(r'filename="([^"]*)"')


class Part(object):
    def __init__(self, data, filename=None):
        self.data = data if isinstance(data, bytes) else str(data).encode("utf-8")
        self.filename = filename
        self.file = io.BytesIO(self.data)

    @property
    def value(self):
        return self.data.decode("utf-8", "replace")


class Form(object):
    """Ayni isimde birden fazla parca destekler (coklu dosya yukleme).

      fs["zip"]             -> ilk Part (geriye donuk uyum)
      fs.getlist("shots")   -> o isimdeki TUM Part'larin listesi
    """
    def __init__(self, parts):
        self._parts = parts      # isim -> [Part, ...]

    def __contains__(self, name):
        return bool(self._parts.get(name))

    def __getitem__(self, name):
        lst = self._parts.get(name)
        return lst[0] if lst else None

    def getvalue(self, name, default=None):
        lst = self._parts.get(name)
        if not lst:
            return default
        return lst[0].value

    def getlist(self, name):
        return list(self._parts.get(name, []))

    def keys(self):
        return list(self._parts.keys())


def _parse_multipart(body, boundary):
    delim = b"--" + boundary
    parts = {}
    for seg in body.split(delim):
        if seg.startswith(b"--"):
            break
        seg = seg.lstrip(b"\r\n")
        if not seg or b"\r\n\r\n" not in seg:
            continue
        head, _, data = seg.partition(b"\r\n\r\n")
        if data.endswith(b"\r\n"):
            data = data[:-2]
        m = _NAME_RE.search(head.decode("utf-8", "replace"))
        if not m:
            continue
        fname = _FILE_RE.search(head.decode("utf-8", "replace"))
        filename = fname.group(1) if fname and fname.group(1) else None
        parts.setdefault(m.group(1), []).append(Part(data, filename))
    return parts


def parse(rfile, headers, content_length, max_size=64 * 1024 * 1024):
    content_length = int(content_length or 0)
    if content_length <= 0:
        return Form({})
    if content_length > max_size:
        raise ValueError("istek cok buyuk")
    ctype = headers.get("Content-Type", "") or ""
    body = rfile.read(content_length)

    if ctype.startswith("multipart/form-data"):
        m = re.search(r'boundary="?([^";]+)"?', ctype)
        if not m:
            raise ValueError("multipart sinir (boundary) yok")
        return Form(_parse_multipart(body, m.group(1).encode("utf-8")))

    # application/x-www-form-urlencoded (veya belirsiz)
    text = body.decode("utf-8", "replace")
    parts = {}
    for k, vals in parse_qs(text, keep_blank_values=True).items():
        parts[k] = [Part(v) for v in (vals or [""])]
    return Form(parts)
