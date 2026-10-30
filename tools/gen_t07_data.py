"""0.7 oz-test verisi uretir: imzali test paketi + imzali manifesto.

Cikti: desktop/t07_sig_data.h
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pkg_sign

OUT = os.path.join(ROOT, "desktop", "t07_sig_data.h")

PACKAGE_BODY = b"T07-PAKET-GOVDESI-v1|ornek icerik|0123456789abcdef"

MAN_LINES = [
    "#arctian-update 1",
    "version=0.8",
    "notes=0.7 oz-test manifestosu",
    "released=1793000000",
    "@image",
    "id=system",
    "size=4194304",
    "sha256=" + hashlib.sha256(b"test-image").hexdigest(),
    "package=/os/0.8.img",
]
MAN_BODY = ("\n".join(MAN_LINES) + "\n").encode()
sig_hex = pkg_sign.sign_bytes(MAN_BODY)[4:].hex()
MAN_FULL = MAN_BODY + ("sig=%s\n" % sig_hex).encode()


def carr(name, data):
    out = ["static const unsigned char %s[] = {" % name]
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        out.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    out.append("};")
    out.append("#define %s_SIZE %du" % (name, len(data)))
    return "\n".join(out)


def cstr(name, text):
    esc = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    # uzun satiri parcalara bol
    parts = [esc[i:i + 100] for i in range(0, len(esc), 100)]
    body = '"%s"' % '" "'.join(parts)
    return "static const char %s[] =\n    %s;" % (name, body)


def main():
    pkg = PACKAGE_BODY + pkg_sign.sign_bytes(PACKAGE_BODY)
    with open(OUT, "w", newline="\n") as f:
        f.write("/* Otomatik uretildi: tools/gen_t07_data.py - DEGISTIRMEYIN. */\n")
        f.write("#ifndef ARCTIAN_T07_SIG_DATA_H\n#define ARCTIAN_T07_SIG_DATA_H\n\n")
        f.write(carr("T07_PKG", pkg) + "\n\n")
        f.write(carr("T07_MANIFEST_FULL", MAN_FULL) + "\n\n")
        f.write(carr("T07_MANIFEST_UNSIGNED", MAN_BODY) + "\n\n")
        f.write("#endif\n")
    print("yazildi: %s (paket=%d, manifesto=%d)" % (OUT, len(pkg), len(MAN_FULL)))


if __name__ == "__main__":
    main()
