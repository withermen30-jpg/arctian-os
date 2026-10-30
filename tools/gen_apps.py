#!/usr/bin/env python3
"""desktop/apps/*/ klasorlerini tarayip apps_registry.c uretir."""
import os, json

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
apps_dir = os.path.join(root, "desktop", "apps")
objdir = os.environ.get("ARCTIAN_OBJ", os.path.join("build", "obj"))
out = os.path.join(root, objdir, "desktop", "apps_registry.c")
os.makedirs(os.path.dirname(out), exist_ok=True)


def cstr(s):
    r = ""
    for ch in s:
        if ch == "\\": r += "\\\\"
        elif ch == '"': r += '\\"'
        elif ch == "\n": r += "\\n"
        elif ch == "\r": r += "\\r"
        elif ch == "\t": r += "\\t"
        else: r += ch
    return r


def load_icon(app_dir, icon_name):
    ip = os.path.join(app_dir, icon_name)
    if not os.path.isfile(ip):
        alt = "icon.png" if str(icon_name).lower().endswith(".svg") else "icon.svg"
        cand = os.path.join(app_dir, alt)
        if os.path.isfile(cand):
            ip = cand
        else:
            cand2 = os.path.join(app_dir, "icon.png")
            if os.path.isfile(cand2):
                ip = cand2
    if not os.path.isfile(ip):
        return "", b""
    raw = open(ip, "rb").read()
    if raw[:8] == b"\x89PNG\r\n\x1a\n":
        return "", raw
    try:
        return raw.decode("utf-8"), b""
    except Exception:
        return raw.decode("latin-1", "replace"), b""


entries = []
if os.path.isdir(apps_dir):
    for name in sorted(os.listdir(apps_dir)):
        d = os.path.join(apps_dir, name)
        mf = os.path.join(d, "app.json")
        if not os.path.isfile(mf):
            continue
        try:
            m = json.load(open(mf, encoding="utf-8"))
        except Exception as e:
            print("  UYARI: %s okunamadi: %s" % (mf, e))
            continue
        aname = m.get("name", name)
        title = m.get("title", aname)
        cat = m.get("category", "Genel")
        icon = m.get("icon", "icon.svg")
        hp = m.get("handles", "")
        if isinstance(hp, list):
            hp = ",".join(str(x).lstrip(".") for x in hp)
        else:
            hp = ",".join(t.strip().lstrip(".") for t in str(hp).split(",") if t.strip())
        svg, png = load_icon(d, icon)
        try:
            ww = int(m.get("w", 0))
            wh = int(m.get("h", 0))
        except Exception:
            ww, wh = 0, 0
        fl = m.get("flags", [])
        if isinstance(fl, str):
            fl = [x.strip() for x in fl.split(",") if x.strip()]
        fbits = 0
        if "no_desktop" in fl:
            fbits |= 0x1
        if "engine" in fl:
            fbits |= 0x2
        entries.append((aname, title, cat, hp, svg, png, ww, wh, fbits))

lines = ['#include "app.h"\n']
for e in entries:
    if e[5]:
        data = ",".join(str(b) for b in e[5])
        lines.append("static const unsigned char app_%s_icon_png[] = {%s};\n" % (e[0], data))
for e in entries:
    lines.append("extern void app_%s_entry(lv_obj_t*, asi_t*);\n" % e[0])
lines.append("const arctian_app_t arctian_apps[] = {\n")
if not entries:
    lines.append('  { {0}, {0}, {0}, "", "", 0, 0, 0, 0, 0, 0 }\n')
for an, t, cat, hp, svg, png, ww, wh, fbits in entries:
    pvar = ("app_%s_icon_png" % an) if png else "0"
    plen = len(png) if png else 0
    lines.append('  {"%s","%s","%s","%s","%s", app_%s_entry, %d, %d, %du, %s, %du},\n'
                 % (an, t, cat, cstr(hp), cstr(svg), an, ww, wh, fbits, pvar, plen))
lines.append("};\n")
lines.append("const int arctian_app_count = %d;\n" % len(entries))
content = "".join(lines)

old = None
if os.path.isfile(out):
    try:
        old = open(out, encoding="utf-8").read()
    except Exception:
        old = None

if old == content:
    print("gen_apps: %d uygulama (degisiklik yok)" % len(entries))
else:
    with open(out, "w", encoding="utf-8") as f:
        f.write(content)
    print("gen_apps: %d uygulama -> %s" % (len(entries), out))
