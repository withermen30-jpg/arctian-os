import base64
import math
import os
import re
import sys

SW, SH = 1440, 810


def fnum(s, default=0.0):
    if s is None:
        return default
    m = re.search(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", s)
    return float(m.group(0)) if m else default


def style_of(div_tag):
    m = re.search(r'style="([^"]*)"', div_tag)
    st = {}
    if not m:
        return st
    for part in m.group(1).split(';'):
        if ':' in part:
            k, v = part.split(':', 1)
            st[k.strip()] = v.strip()
    return st


class Mat:

    def __init__(self, a=1, b=0, c=0, d=0, e=1, f=0):
        self.m = (a, b, c, d, e, f)

    def mul(self, o):
        a, b, c, d, e, f = self.m
        a2, b2, c2, d2, e2, f2 = o.m
        return Mat(a * a2 + b * d2, a * b2 + b * e2, a * c2 + b * f2 + c,
                   d * a2 + e * d2, d * b2 + e * e2, d * c2 + e * f2 + f)

    @staticmethod
    def translate(x, y):
        return Mat(1, 0, x, 0, 1, y)

    @staticmethod
    def scale(sx, sy):
        return Mat(sx, 0, 0, 0, sy, 0)

    @staticmethod
    def rotate(deg):
        r = math.radians(deg)
        return Mat(math.cos(r), -math.sin(r), 0, math.sin(r), math.cos(r), 0)

    def apply(self, x, y):
        a, b, c, d, e, f = self.m
        return (a * x + b * y + c, d * x + e * y + f)

    def scale_factor(self):
        a, b, _, d, e, _ = self.m
        return math.sqrt(abs(a * e - b * d))


NUM = re.compile(r"[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?")


def transform_path_d(d, mat):
    out = []
    tokens = re.findall(r"[MmLlHhVvCcSsQqTtAaZz]|[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?", d)
    i = 0
    cmd = None
    cx = cy = 0.0
    sx = sy = 0.0

    def num():
        nonlocal i
        v = float(tokens[i])
        i += 1
        return v

    def emit(c, pts):
        parts = [c]
        for k in range(0, len(pts), 2):
            x, y = mat.apply(pts[k], pts[k + 1])
            parts.append(f"{x:.2f} {y:.2f}")
        out.append(" ".join(parts))

    while i < len(tokens):
        t = tokens[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t
            i += 1
        if cmd is None:
            break
        C = cmd.upper()
        rel = cmd.islower()
        if C == 'M':
            x, y = num(), num()
            if rel:
                x += cx
                y += cy
            cx, cy = x, y
            sx, sy = x, y
            emit('M', [x, y])
            cmd = 'l' if rel else 'L'
        elif C == 'L':
            x, y = num(), num()
            if rel:
                x += cx
                y += cy
            cx, cy = x, y
            emit('L', [x, y])
        elif C == 'H':
            x = num()
            if rel:
                x += cx
            cx = x
            emit('L', [cx, cy])
        elif C == 'V':
            y = num()
            if rel:
                y += cy
            cy = y
            emit('L', [cx, cy])
        elif C == 'C':
            p = [num(), num(), num(), num(), num(), num()]
            if rel:
                p = [p[0] + cx, p[1] + cy, p[2] + cx, p[3] + cy, p[4] + cx, p[5] + cy]
            cx, cy = p[4], p[5]
            emit('C', p)
        elif C == 'Q':
            p = [num(), num(), num(), num()]
            if rel:
                p = [p[0] + cx, p[1] + cy, p[2] + cx, p[3] + cy]
            cx, cy = p[2], p[3]
            emit('Q', p)
        elif C == 'Z':
            out.append('Z')
            cx, cy = sx, sy
        else:
            i += 1
    return " ".join(out)


def cstr(s):
    return s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '')


def parse_section_shapes(html):
    results = []
    div_re = re.compile(
        r'<div\s+class="([^"]*)"[^>]*?style="([^"]*)">|</div>|'
        r'<svg\s+class="shp"\s+viewBox="([^"]*)"\s+preserveAspectRatio="none"\s+'
        r'style="width:([\d.]+)px;height:([\d.]+)px[^"]*"[^>]*>(.*?)</svg>|'
        r'<image\s+href="data:image/svg\+xml;base64,([^"]+)"',
        re.S)
    stack = []
    for m in div_re.finditer(html):
        if m.group(0).startswith('</div>'):
            if stack:
                stack.pop()
            continue
        if m.group(1) is not None:
            stack.append({'cls': m.group(1), 'style': style_of('<div style="%s">' % m.group(2))})
            continue
        if m.group(3) is not None:
            results.append({'stack': list(stack), 'viewbox': m.group(3),
                            'rw': float(m.group(4)), 'rh': float(m.group(5)),
                            'inner': m.group(6)})
            continue
        if m.group(7) is not None:
            results.append({'stack': list(stack), 'image': m.group(7)})
    return results


def compose_mat(stack, extra_w=0, extra_h=0):
    mats = []
    for lvl in stack:
        st = lvl['style']
        left = fnum(st.get('left'))
        top = fnum(st.get('top'))
        w = fnum(st.get('width'))
        h = fnum(st.get('height'))
        m = Mat.translate(left, top)
        tr = st.get('transform', '')
        for mm in re.finditer(r'rotate\(\s*([-\d.]+)deg\s*\)', tr):
            ang = float(mm.group(1))
            m = m.mul(Mat.translate(w / 2, h / 2)).mul(Mat.rotate(ang)).mul(
                Mat.translate(-w / 2, -h / 2))
        for mm in re.finditer(r'scale\(\s*([-\d.]+)\s*(?:,\s*([-\d.]+)\s*)?\)', tr):
            sx = float(mm.group(1))
            sy = float(mm.group(2)) if mm.group(2) else sx
            m = m.mul(Mat.scale(sx, sy))
        for mm in re.finditer(r'translate\(\s*([-\d.eE]+)[ ,]+([-\d.eE]+)\s*\)', tr):
            m = m.mul(Mat.translate(float(mm.group(1)), float(mm.group(2))))
        mats.append(m)
    mat = Mat()
    for m in mats:
        mat = mat.mul(m)
    return mat


def build_slide_svg(shapes):
    parts = []
    for sh in shapes:
        stack = sh['stack']
        mat = compose_mat(stack)
        outer = stack[0]['style'] if stack else {}
        outer_w = fnum(outer.get('width'))
        outer_h = fnum(outer.get('height'))

        if 'image' in sh:
            raw = base64.b64decode(sh['image']).decode('utf-8', 'ignore')
            inner = re.sub(r'^.*?<svg[^>]*>', '', raw, flags=re.S)
            inner = re.sub(r'</svg>\s*$', '', inner, flags=re.S)
            inner = re.sub(r'<defs>.*?</defs>', '', inner, flags=re.S)
            sx = outer_w / 110.0
            sy = outer_h / 80.0
            tx, ty = mat.apply(0, 0)

            def fix_sw(m):
                return 'stroke-width="%s"' % (fnum(m.group(1)) / max(sx, 1e-6))

            inner = re.sub(r'stroke-width="([\d.]+)"', fix_sw, inner)
            parts.append(
                f'<g transform="translate({tx:.2f} {ty:.2f}) scale({sx:.4f} {sy:.4f})">'
                f'{inner}</g>')
            continue

        vb = sh['viewbox'].split()
        vbw = float(vb[2]) if len(vb) >= 4 else 1
        vbh = float(vb[3]) if len(vb) >= 4 else 1

        rw = sh.get('rw', outer_w)
        rh = sh.get('rh', outer_h)
        body = sh.get('inner', '')

        im = re.search(r'<image\s+href="data:image/svg\+xml;base64,([^"]+)"', body)
        if im:
            raw = base64.b64decode(im.group(1)).decode('utf-8', 'ignore')
            inner_svg = re.sub(r'^.*?<svg[^>]*>', '', raw, flags=re.S)
            inner_svg = re.sub(r'</svg>\s*$', '', inner_svg, flags=re.S)
            inner_svg = re.sub(r'<defs>.*?</defs>', '', inner_svg, flags=re.S)
            inner_svg = re.sub(r'<clipPath.*?</clipPath>', '', inner_svg, flags=re.S)
            body = inner_svg
            vbw, vbh = 110.0, 80.0

        mat = mat.mul(Mat.scale(rw / vbw, rh / vbh))

        for pm in re.finditer(r'<(path|polygon|rect|circle|line|ellipse)\b([^>]*)/?>', body):
            attrs = pm.group(2)
            tag = pm.group(1)
            fill = re.search(r'fill="([^"]*)"', attrs)
            stroke = re.search(r'stroke="([^"]*)"', attrs)
            sw = re.search(r'stroke-width="([^"]*)"', attrs)
            fo = re.search(r'fill-opacity="([^"]*)"', attrs)
            so = re.search(r'stroke-opacity="([^"]*)"', attrs)
            nss = 'vector-effect="non-scaling-stroke"' in attrs

            d = None
            if tag == 'path':
                dm = re.search(r'\sd="([^"]*)"', attrs)
                d = dm.group(1) if dm else None
            elif tag == 'polygon':
                pm2 = re.search(r'points="([^"]*)"', attrs)
                if pm2:
                    pts = [float(x) for x in re.findall(r'[-\d.]+', pm2.group(1))]
                    d = 'M ' + ' L '.join(f'{pts[i]} {pts[i+1]}' for i in range(0, len(pts), 2))
            elif tag == 'rect':
                x = fnum(re.search(r'\sx="([^"]*)"', attrs).group(1)) if re.search(r'\sx="', attrs) else 0
                y = fnum(re.search(r'\sy="([^"]*)"', attrs).group(1)) if re.search(r'\sy="', attrs) else 0
                w = fnum(re.search(r'width="([^"]*)"', attrs).group(1))
                h = fnum(re.search(r'height="([^"]*)"', attrs).group(1))
                r = fnum(re.search(r'\srx="([^"]*)"', attrs).group(1)) if re.search(r'\srx="', attrs) else 0
                if r > 0:
                    d = (f"M {x+r} {y} L {x+w-r} {y} Q {x+w} {y} {x+w} {y+r} "
                         f"L {x+w} {y+h-r} Q {x+w} {y+h} {x+w-r} {y+h} "
                         f"L {x+r} {y+h} Q {x} {y+h} {x} {y+h-r} "
                         f"L {x} {y+r} Q {x} {y} {x+r} {y} Z")
                else:
                    d = f"M {x} {y} L {x+w} {y} L {x+w} {y+h} L {x} {y+h} Z"
            if d is None:
                continue

            td = transform_path_d(d, mat)
            out = [f'<path d="{td}"']
            if fill and fill.group(1) != 'none':
                out.append(f'fill="{fill.group(1)}"')
            else:
                out.append('fill="none"')
            if fo:
                out.append(f'fill-opacity="{fo.group(1)}"')
            if stroke and stroke.group(1) != 'none':
                if nss:
                    wpx = fnum(sw.group(1)) if sw else 1.0
                elif sw:
                    wpx = fnum(sw.group(1)) * mat.scale_factor()
                else:
                    wpx = 1.0 * mat.scale_factor()
                if stroke.group(1).upper() == '#5E17EB':
                    continue
                out.append(f'stroke="{stroke.group(1)}" stroke-width="{wpx:.2f}"')
                if so:
                    out.append(f'stroke-opacity="{so.group(1)}"')
            out.append('/>')
            parts.append("".join(out))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" '
            'viewBox="0 0 %d %d">%s</svg>' % (SW, SH, SW, SH, "".join(parts)))


def build_all(html):
    secs = re.findall(r'<section class="slide"[^>]*>(.*?)</section>', html, re.S)
    return [build_slide_svg(parse_section_shapes(sec)) for sec in secs]


def main():
    src = sys.argv[1]
    outdir = sys.argv[2]
    with open(src, encoding='utf-8') as f:
        html = f.read()
    svgs = build_all(html)
    os.makedirs(outdir, exist_ok=True)
    for i, s in enumerate(svgs):
        with open(os.path.join(outdir, 'setup_slide%d.svg' % i), 'w',
                  encoding='utf-8') as f:
            f.write(s)
    print("slayt=%d" % len(svgs))


if __name__ == '__main__':
    main()
