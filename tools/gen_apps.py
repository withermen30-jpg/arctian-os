

import os, json

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
apps_dir = os.path.join(root, "desktop", "apps")
out = os.path.join(root, "build", "obj", "desktop", "apps_registry.c")
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
        ip = os.path.join(d, icon)
        svg = open(ip, encoding="utf-8").read() if os.path.isfile(ip) else ""
        try:
            ww = int(m.get("w", 0))
            wh = int(m.get("h", 0))
        except Exception:
            ww, wh = 0, 0
        entries.append((aname, title, cat, hp, svg, ww, wh))

lines = ['#include "app.h"\n']
for e in entries:
    lines.append("extern void app_%s_entry(lv_obj_t*, asi_t*);\n" % e[0])
lines.append("const arctian_app_t arctian_apps[] = {\n")
if not entries:
    lines.append('  { {0}, {0}, {0}, "", "", 0, 0, 0 }\n')
for an, t, cat, hp, svg, ww, wh in entries:
    lines.append('  {"%s","%s","%s","%s","%s", app_%s_entry, %d, %d},\n'
                 % (an, t, cat, cstr(hp), cstr(svg), an, ww, wh))
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
