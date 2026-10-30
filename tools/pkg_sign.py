"""Arctian paket imzalama yardimcisi.

.dpk / .wlf dosyalarinin sonuna 260 baytlik imza blogu ekler:

    [govde] [ "ASG1" | RSA-2048 PKCS#1 v1.5 imza (256) ]

Gelistirme anahtari tools/keys/dev_rsa2048.txt icindedir. Yayin oncesi
tools/sig_keygen.py ile yeni anahtar uretilmeli ve imaj yeniden derlenmelidir.
"""
import hashlib
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY_PATH = os.path.join(ROOT, "tools", "keys", "dev_rsa2048.txt")

MAGIC = b"ASG1"
SIG_LEN = 256
TRAILER = len(MAGIC) + SIG_LEN
DI = bytes.fromhex("3031300d060960864801650304020105000420")

_key = None


def _load_key():
    global _key
    if _key is None:
        kv = {}
        with open(KEY_PATH, "r") as f:
            for line in f:
                line = line.strip()
                if "=" in line:
                    k, v = line.split("=", 1)
                    kv[k] = v
        _key = (int(kv["n"], 16), int(kv["e"], 16), int(kv["d"], 16))
    return _key


def sign_bytes(body: bytes) -> bytes:
    n, _e, d = _load_key()
    em = b"\x00\x01" + b"\xff" * (SIG_LEN - 3 - len(DI) - 32) + b"\x00" + DI
    em += hashlib.sha256(body).digest()
    if len(em) != SIG_LEN:
        raise ValueError("EM uzunlugu hatali: %d" % len(em))
    sig = pow(int.from_bytes(em, "big"), d, n).to_bytes(SIG_LEN, "big")
    return MAGIC + sig


def sign_file(path: str) -> int:
    with open(path, "rb") as f:
        data = f.read()
    if len(data) > TRAILER and data[-TRAILER:-TRAILER + 4] == MAGIC:
        data = data[:-TRAILER]          # yeniden imzala
    signed = data + sign_bytes(data)
    with open(path, "wb") as f:
        f.write(signed)
    return len(signed)


def main():
    import sys
    if len(sys.argv) < 2:
        print("kullanim: pkg_sign.py <dosya> [...]")
        return 1
    for p in sys.argv[1:]:
        print("%s -> %d bayt (imzali)" % (p, sign_file(p)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
