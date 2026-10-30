#!/usr/bin/env python3
"""Emulator derleme envanteri ureticisi (Windows icin snapshot).

Linux/WSL tarafinda sdk/emu/Makefile OS Makefile'i gibi `find` kullanir ve
dogrudan tools/bin2c.py uretir. Windows'ta (w64devkit) find/bin2c zinciri
olmadigindan ayni liste ve gomulu font ONECDEN uretilip sdk/emu/gen/
altina yazilir; Makefile.win bu anlik goruntuyu tuketir.

Kullanim:
    python3 tools/emu_srcs.py              # LVGL kaynak listesini basar
    python3 tools/emu_srcs.py --write gen  # gen/lvgl_sources.txt + gen/font_data.c

Eleme kurallari kok Makefile'daki `find ... ! -path` listesiyle BIREBIRDIR.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

LV_SRC_REL = "third_party/lvgl/src"
DRAW_SKIP = {"nanovg", "vg_lite", "nema_gfx", "opengles", "sdl", "dma2d",
             "nxp", "renesas", "eve", "sifli"}
DBG_SKIP = {"vg_lite_tvg", "profiler", "sysmon", "test", "monkey"}
OSAL_SKIP = {"lv_linux.c", "lv_pthread.c", "lv_windows.c", "lv_sdl2.c",
             "lv_freertos.c", "lv_rtthread.c", "lv_cmsis_rtos2.c", "lv_mqx.c"}
FS_SKIP = {"lv_fs_stdio.c", "lv_fs_posix.c", "lv_fs_win32.c", "lv_fs_uefi.c",
           "lv_fs_frogfs.c", "lv_fs_littlefs.c", "lv_fs_fatfs.c"}


def _skip_dir(parts):
    if "drivers" in parts or "libs" in parts:
        return True
    for key, skip in (("draw", DRAW_SKIP), ("debugging", DBG_SKIP),
                      ("font", {"freetype"})):
        if key in parts:
            i = parts.index(key)
            if i + 1 < len(parts) and parts[i + 1] in skip:
                return True
    return False


def lvgl_sources():
    base = os.path.join(ROOT, *LV_SRC_REL.split("/"))
    out = []
    for dirpath, _dirs, files in os.walk(base):
        rel = os.path.relpath(dirpath, base).replace("\\", "/")
        parts = [] if rel == "." else rel.split("/")
        if _skip_dir(parts):
            continue
        for f in sorted(files):
            if not f.endswith(".c"):
                continue
            if "osal" in parts and f in OSAL_SKIP:
                continue
            if "fs" in parts and f in FS_SKIP:
                continue
            out.append(LV_SRC_REL + ("/" + rel if rel != "." else "") + "/" + f)
    return sorted(out)


def font_data_c():
    ttf = os.path.join(ROOT, "design", "font", "Inter.ttf")
    data = open(ttf, "rb").read()
    lines = ["/* Otomatik uretildi: tools/emu_srcs.py (Inter.ttf gomulus) */",
             "const unsigned char font_ttf[] = {"]
    for i in range(0, len(data), 16):
        lines.append("  " + ",".join(str(b) for b in data[i:i + 16]) + ",")
    lines.append("};")
    lines.append("const unsigned int font_ttf_len = %d;" % len(data))
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "--write":
        outdir = sys.argv[2]
        if not os.path.isabs(outdir):
            outdir = os.path.join(os.getcwd(), outdir)
        os.makedirs(outdir, exist_ok=True)
        srcs = lvgl_sources()
        with open(os.path.join(outdir, "lvgl_sources.txt"), "wb") as f:
            f.write(("\n".join(srcs) + "\n").encode("ascii"))
        with open(os.path.join(outdir, "font_data.c"), "wb") as f:
            f.write(font_data_c().encode("ascii"))
        print("emu_srcs: %d LVGL dosyasi + font -> %s" % (len(srcs), outdir))
        return 0
    for s in lvgl_sources():
        print(s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
