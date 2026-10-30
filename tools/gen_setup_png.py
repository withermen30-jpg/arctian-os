import os
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_setup_svg

SW, SH = 1440, 810
CREAM = (0xFC, 0xF0, 0xDD)


def png_write(path, w, h, rows):
    def chunk(tag, payload):
        return (struct.pack('>I', len(payload)) + tag + payload +
                struct.pack('>I', zlib.crc32(tag + payload) & 0xffffffff))
    body = b''.join(b'\x00' + row for row in rows)
    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(body, 9))
    out += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(out)


def raw_to_rows(data, w, h, bg):
    px = struct.unpack('<%dI' % (w * h), data)
    br, bgc, bb = bg
    rows = []
    for y in range(h):
        row = bytearray(3 * w)
        base = y * w
        i = 0
        for x in range(w):
            c = px[base + x]
            a = c >> 24
            r = (c >> 16) & 255
            g = (c >> 8) & 255
            b = c & 255
            if a == 0:
                r, g, b = br, bgc, bb
            elif a < 255:
                r = (r * a + br * (255 - a)) // 255
                g = (g * a + bgc * (255 - a)) // 255
                b = (b * a + bb * (255 - a)) // 255
            row[i] = r
            row[i + 1] = g
            row[i + 2] = b
            i += 3
        rows.append(bytes(row))
    return rows


def main():
    html_path, renderer, out_c = sys.argv[1], sys.argv[2], sys.argv[3]
    with open(html_path, encoding='utf-8') as f:
        svgs = gen_setup_svg.build_all(f.read())

    tmp = tempfile.mkdtemp(prefix='setupsvg')
    blobs = []
    for i, svg in enumerate(svgs):
        sp = os.path.join(tmp, 'slide%d.svg' % i)
        rp = os.path.join(tmp, 'slide%d.raw' % i)
        pp = os.path.join(tmp, 'slide%d.png' % i)
        with open(sp, 'w', encoding='utf-8') as f:
            f.write(svg)
        subprocess.check_call([renderer, sp, str(SW), str(SH), rp])
        with open(rp, 'rb') as f:
            raw = f.read()
        png_write(pp, SW, SH, raw_to_rows(raw, SW, SH, CREAM))
        with open(pp, 'rb') as f:
            blobs.append(f.read())

    os.makedirs(os.path.dirname(out_c), exist_ok=True)
    with open(out_c, 'w', encoding='ascii') as f:
        f.write('#include <stdint.h>\n')
        for i, blob in enumerate(blobs):
            f.write('const unsigned char arctian_setup_png%d[] = {\n' % i)
            for j in range(0, len(blob), 16):
                f.write('  ' + ','.join(str(b) for b in blob[j:j + 16]) + ',\n')
            f.write('};\n')
            f.write('const unsigned int arctian_setup_png%d_len = %d;\n'
                    % (i, len(blob)))
        f.write('const unsigned char *const arctian_setup_png[] = {\n')
        for i in range(len(blobs)):
            f.write('  arctian_setup_png%d,\n' % i)
        f.write('};\n')
        f.write('const unsigned int arctian_setup_png_len[] = {')
        f.write(','.join(str(len(b)) for b in blobs))
        f.write('};\n')
        f.write('const unsigned arctian_setup_png_n = %d;\n' % len(blobs))
    print('setup png: %d slayt, png toplam %d bayt'
          % (len(blobs), sum(len(b) for b in blobs)))


if __name__ == '__main__':
    main()
