#!/usr/bin/env python3
"""
Arctian Writer
==============

Arctian OS'u bir fiziksel diske (SSD/HDD/USB) yazar. Disk, BIOS uzerinden
direkt Arctian'dan boot edilir; kurulum/ISO adimi gerekmez ve dosya sistemi
(kalici AFS) diskte kalir.

ONEMLI: Yazilacak dosya `build/arctian.img` OLMALIDIR (ISO degil).
  ISO, LBA0'inda 0x55AA onyukleme imzasi tasimaz; bu yuzden disk olarak
  yazildiginda BIOS onu boot edemez. `img` ise stage1 onyukleyicisini icerir.

Kullanim:
    python arctian_writer.py                 # interaktif (disk listesi + secim)
    python arctian_writer.py --list          # sadece diskleri listele
    python arctian_writer.py --disk 2 --yes  # surucu numarasi ile dogrudan
    python arctian_writer.py --disk /dev/sdb --yes

Gereksinim: Windows'ta yonetici (Administrator) yetkisi.
"""
import argparse
import os
import struct
import sys

SECTOR = 512
SB_LBA = 2048
SB_MAGIC = 0x54435241          # "ARCT"
SB_FLAG_INSTALLED = 0x00000001
# superblock_t: magic,version,header_size,flags,module_count,
#               module_tab_lba,module_tab_sectors,state_lba,reserved
DT_HDR = "<IHHIIIIII"
DT_HDR_SIZE = struct.calcsize(DT_HDR)

IS_WINDOWS = sys.platform.startswith("win")
IS_LINUX = sys.platform.startswith("linux")
IS_MAC = sys.platform == "darwin"


# ------------------------------------------------------------------ #
# yardimcilar                                                          #
# ------------------------------------------------------------------ #
def human(n):
    for u in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024 or u == "TB":
            return "%.1f %s" % (n, u)
        n /= 1024.0
    return "%.1f TB" % n


def amod_checksum(data: bytes) -> int:
    s = 0x12345678
    for b in data:
        s = ((s << 1) & 0xFFFFFFFF) ^ (s >> 31) ^ b
        s &= 0xFFFFFFFF
    return s


# ------------------------------------------------------------------ #
# disk listeleme                                                       #
# ------------------------------------------------------------------ #
class Disk(object):
    def __init__(self, path, number, model, size, bus, removable,
                 system=False, boot=False):
        self.path = path
        self.number = number
        self.model = model
        self.size = size
        self.bus = bus
        self.removable = removable
        self.system = system
        self.boot = boot

    def tag(self):
        t = []
        if self.system:
            t.append("SISTEM DISKI")
        if self.boot:
            t.append("ONCEKI ACILIS DISKI")
        if self.removable:
            t.append("HAREKLI")
        return ", ".join(t) if t else "-"

    def line(self, idx):
        warn = "  <== UYARI" if self.system else ""
        return ("  [%d] %-28s %10s  %-8s %s%s"
                % (idx, self.model[:28], human(self.size), self.bus,
                   self.tag(), warn))


def list_windows():
    import subprocess
    import json
    disks = []
    script = ("Get-Disk | Select-Object Number,FriendlyName,BusType,"
              "@{n='Size';e={$_.Size}},IsBoot,IsSystem,IsOffline | "
              "ConvertTo-Json -Compress")
    try:
        r = subprocess.run(["powershell", "-NoProfile", "-Command", script],
                           capture_output=True, text=True, timeout=60)
        data = json.loads(r.stdout.strip() or "[]")
        if isinstance(data, dict):
            data = [data]
    except Exception as e:
        print("Disk listelenemedi: %s" % e, file=sys.stderr)
        return disks
    for d in data:
        if d.get("IsOffline"):
            continue
        num = int(d["Number"])
        disks.append(Disk(r"\\.\PhysicalDrive%d" % num, num,
                          str(d.get("FriendlyName") or "?"),
                          int(d.get("Size") or 0),
                          str(d.get("BusType") or "?"), False,
                          bool(d.get("IsSystem")), bool(d.get("IsBoot"))))
    return disks


def list_linux():
    disks = []
    base = "/sys/block"
    root_dev = ""
    try:
        root_dev = os.path.realpath("/")
    except Exception:
        pass
    try:
        names = sorted(os.listdir(base))
    except Exception as e:
        print("Disk listelenemedi: %s" % e, file=sys.stderr)
        return disks
    skip_prefix = ("loop", "ram", "zram", "dm-", "md", "fd", "sr")
    min_size = 64 * 1024 * 1024
    for name in names:
        if name.startswith(skip_prefix):
            continue
        p = os.path.join(base, name)
        try:
            with open(os.path.join(p, "size")) as f:
                sectors = int(f.read().strip())
        except Exception:
            continue
        if sectors * 512 < min_size:
            continue
        model = ""
        for f in ("device/model", "device/vendor"):
            try:
                with open(os.path.join(p, f)) as fh:
                    v = fh.read().strip()
                if v:
                    model = (model + " " + v).strip()
            except Exception:
                pass
        removable = False
        try:
            with open(os.path.join(p, "removable")) as f:
                removable = f.read().strip() == "1"
        except Exception:
            pass
        dev = "/dev/" + name
        is_root = root_dev.startswith(dev)
        disks.append(Disk(dev, name, model or name, sectors * 512,
                          "USB" if removable else "SATA/NVMe", removable,
                          is_root, is_root))
    return disks


def list_mac():
    import subprocess
    disks = []
    try:
        r = subprocess.run(["diskutil", "list", "-plist"], capture_output=True,
                           text=True, timeout=60)
        text = r.stdout
    except Exception as e:
        print("Disk listelenemedi: %s" % e, file=sys.stderr)
        return disks
    import re
    for m in re.finditer(r"<dict>(.*?)</dict>", text, re.S):
        blk = m.group(1)
        dev = re.search(r"<key>DeviceIdentifier</key>\s*<string>(.*?)</string>", blk)
        name = re.search(r"<key>DeviceName</key>\s*<string>(.*?)</string>", blk)
        size = re.search(r"<key>Size</key>\s*<integer>(\d+)</integer>", blk)
        bus = re.search(r"<key>BusProtocol</key>\s*<string>(.*?)</string>", blk)
        internal = re.search(r"<key>Internal</key>\s*<true ?/>", blk)
        if not dev:
            continue
        disks.append(Disk("/dev/" + dev.group(1), dev.group(1),
                          name.group(1) if name else dev.group(1),
                          int(size.group(1)) if size else 0,
                          (bus.group(1) if bus else "?"),
                          False,
                          bool(internal), bool(internal)))
    return disks


def list_disks():
    if IS_WINDOWS:
        return list_windows()
    if IS_LINUX:
        return list_linux()
    if IS_MAC:
        return list_mac()
    return []


# ------------------------------------------------------------------ #
# imgi hazirlama                                                        #
# ------------------------------------------------------------------ #
def patch_image(img: bytes, add_partition=True) -> bytes:
    """Superblock'a INSTALLED bayragi koyar (kurulum adimi atlanir) ve
    istege bagli olarak MBR'ye bir bolum girdisi yazar."""
    out = bytearray(img)

    # 1) MBR imzasi (yoksa ekle)
    if out[510:512] != b"\x55\xAA":
        out[510] = 0x55
        out[511] = 0xAA

    # 2) Superblock: LBA 2048
    off = SB_LBA * SECTOR
    if off + DT_HDR_SIZE > len(out):
        raise SystemExit("HATA: imaj superbolgeyi icermiyor (bu arctian.img degil).")
    magic, = struct.unpack_from("<I", out, off)
    if magic != SB_MAGIC:
        raise SystemExit("HATA: LBA %d icinde ARCT superblogu bulunamadi." % SB_LBA)

    (magic, ver, hs, flags, mc, mt, mts, st, rs) = struct.unpack_from(
        DT_HDR, out, off)
    flags |= SB_FLAG_INSTALLED
    head = struct.pack(DT_HDR, SB_MAGIC, ver, hs, flags, mc, mt, mts, st, rs)
    ck = amod_checksum(head)
    out[off:off + DT_HDR_SIZE + 4] = head + struct.pack("<I", ck)

    # 3) MPR bolum girdisi (0x1BE) - BIOS uyumlulugu icin
    if add_partition and out[0x1BE:0x1BE + 16] == b"\x00" * 16:
        start_lba = 2048
        sectors = max(1, len(out) // SECTOR - start_lba)
        entry = struct.pack("<B3sB3sII",
                            0x80,                 # aktif/bootable
                            b"\xFE\xFF\xFF",       # CHS baslangic: LBA kullan
                            0x83,                 # Linux
                            b"\xFE\xFF\xFF",       # CHS bitis: LBA kullan
                            start_lba, sectors)
        out[0x1BE:0x1BE + 16] = entry

    return bytes(out)


def open_disk_write(path):
    """Disk acilisini açar. Windows'ta 64-bit HANDLE dogrusu icin
    argtypes/restype tanimlanir (aksi halde yazma 1 baytla sinirlenir)."""
    if IS_WINDOWS:
        import ctypes
        from ctypes import wintypes
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)

        k32.CreateFileW.argtypes = [
            wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
            wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        k32.CreateFileW.restype = wintypes.HANDLE
        k32.WriteFile.argtypes = [
            wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
        k32.WriteFile.restype = wintypes.BOOL
        k32.ReadFile.argtypes = [
            wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
        k32.ReadFile.restype = wintypes.BOOL
        k32.CloseHandle.argtypes = [wintypes.HANDLE]
        k32.FlushFileBuffers.argtypes = [wintypes.HANDLE]

        GENERIC_WRITE = 0x40000000
        OPEN_EXISTING = 3
        SHARE_ALL = 1 | 2 | 4          # READ|WRITE|DELETE
        h = k32.CreateFileW(path, GENERIC_WRITE, SHARE_ALL, None,
                            OPEN_EXISTING, 0, None)
        if h in (None, 0, -1, ctypes.c_void_p(-1).value):
            err = ctypes.get_last_error()
            msg = {
                5: "Erişim reddedildi. Yonetici (Administrator) olarak calistirin.",
                32: "Disk baska bir program tarafindan kullaniliyor. "
                    "Gezgin'den surucu harfini kaldirip tekrar deneyin.",
                21: "Disk yazmaya kapali (salt okunur).",
                1117: "MBR/boot kaydi bulunamadi.",
            }.get(err, "Hata kodu %d" % err)
            raise SystemExit("HATA: diske acilamadi: %s\n       (%s)" % (path, msg))
        return ("win", h, k32)
    return ("fd", os.open(path, os.O_WRONLY), None)


def _win_write(k32, h, data):
    import ctypes
    from ctypes import wintypes
    written = wintypes.DWORD(0)
    buf = (ctypes.c_char * len(data)).from_buffer_copy(data)
    ok = k32.WriteFile(h, buf, len(data), ctypes.byref(written), None)
    if not ok:
        return 0
    return written.value


def write_image(img: bytes, disk_path: str):
    kind, h, k32 = open_disk_write(disk_path)
    total = len(img)
    done = 0
    step = 1024 * 1024
    try:
        for i in range(0, total, step):
            chunk = img[i:i + step]
            written = 0
            for attempt in range(4):
                if kind == "win":
                    written = _win_write(k32, h, chunk)
                else:
                    written = os.write(h, chunk)
                if written == len(chunk):
                    break
                # kismi yazildiysa kalani yaz
                while written < len(chunk):
                    rest = chunk[written:]
                    n = (_win_write(k32, h, rest) if kind == "win"
                         else os.write(h, rest))
                    if n <= 0:
                        break
                    written += n
                if written == len(chunk):
                    break
                import time
                time.sleep(0.5)
            if written != len(chunk):
                raise SystemExit(
                    "\nHATA: yazma basarisiz (offset %d, %d/%d bayt).\n"
                    "       Diski Gezgin'den 'guvenli olarak kaldir' deyip tekrar deneyin."
                    % (i, written, len(chunk)))
            done += written
            pct = done * 100 // total
            sys.stdout.write("\r  Yaziliyor... %3d%% (%s / %s)"
                             % (pct, human(done), human(total)))
            sys.stdout.flush()
        if kind == "win":
            k32.FlushFileBuffers(h)
            k32.CloseHandle(h)
        else:
            os.fsync(h)
            os.close(h)
    finally:
        sys.stdout.write("\n")
    return done


def read_lba(disk_path, lba):
    """Diskten tek sektor (512 bayt) okur."""
    try:
        if IS_WINDOWS:
            import ctypes
            from ctypes import wintypes
            k32 = ctypes.WinDLL("kernel32", use_last_error=True)
            k32.CreateFileW.restype = wintypes.HANDLE
            k32.SetFilePointerEx.argtypes = [
                wintypes.HANDLE, ctypes.c_longlong, ctypes.POINTER(ctypes.c_longlong),
                wintypes.DWORD]
            k32.ReadFile.argtypes = [
                wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
            k32.ReadFile.restype = wintypes.BOOL
            k32.CloseHandle.argtypes = [wintypes.HANDLE]
            h = k32.CreateFileW(disk_path, 0x80000000, 1 | 2, None, 3, 0, None)
            if h in (None, 0, -1):
                return b""
            pos = ctypes.c_longlong(lba * SECTOR)
            k32.SetFilePointerEx(h, ctypes.c_longlong(lba * SECTOR),
                                ctypes.byref(pos), 0)
            buf = ctypes.create_string_buffer(SECTOR)
            n = wintypes.DWORD(0)
            k32.ReadFile(h, buf, SECTOR, ctypes.byref(n), None)
            k32.CloseHandle(h)
            return buf.raw
        fd = os.open(disk_path, os.O_RDONLY)
        try:
            os.lseek(fd, lba * SECTOR, os.SEEK_SET)
            return os.read(fd, SECTOR)
        finally:
            os.close(fd)
    except Exception:
        return b""


# ------------------------------------------------------------------ #
# akis                                                                  #
# ------------------------------------------------------------------ #
def find_image():
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [
        os.path.join(here, "build", "arctian.img"),
        os.path.join(here, "arctian.img"),
        os.path.join(os.path.dirname(here), "build", "arctian.img"),
        os.path.join(os.getcwd(), "build", "arctian.img"),
    ]
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def show(disks, all_disks=False):
    if not disks:
        print("Kullanilabilir disk bulunamadi.")
        if not all_disks:
            print("(Sistem diski gibi gorunumek isterseniz --all kullanin.)")
        return
    if all_disks:
        print("Bulunan fiziksel diskler (sistem dahil):")
    else:
        print("Yazilabilir diskler (sistem diski gizlendi):")
    for i, d in enumerate(disks):
        print(d.line(i))
    hidden = 0
    try:
        import arctian_writer as _self
        allc = _self.list_disks()
        hidden = len(allc) - len(disks)
    except Exception:
        pass
    if hidden > 0:
        print("\n  (%d adet sistem/boot diski koruma amaciyla gizlendi;"
              " gormek icin --all)" % hidden)


def usable_disks(all_disks=False):
    """Sistem ve boot diski varsayilan olarak gizlenir (yanlislikla
    sistem diskinin silinmesini onlemek icin)."""
    disks = list_disks()
    if all_disks:
        return disks, disks
    return [d for d in disks if not d.system and not d.boot], disks


def pick(disks, all_disks=False):
    show(disks, all_disks)
    if not disks:
        return None
    while True:
        try:
            k = input("\nHedef disk numarasi (0-%d), 'q' iptal: "
                      % (len(disks) - 1)).strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return None
        if k.lower() == "q":
            return None
        if k.isdigit() and int(k) < len(disks):
            return disks[int(k)]
        print("Gecersiz secim.")


def confirm(disk):
    print("")
    print("=" * 58)
    print("  HEDEF DISK: %s" % disk.model)
    print("  Yol      : %s" % disk.path)
    print("  Boyut    : %s" % human(disk.size))
    print("  Tur      : %s" % disk.bus)
    if disk.bus == "USB":
        print("")
        print("  NOT: USB diski kullaniyorsaniz, Gezgin'den surucu")
        print("       harfini 'Guvenli olarak kaldir' deyip yazilimi")
        print("       tekrar calistirin.")
    print("")
    print("  >> Bu diskteki TUM VERILER SILINECEK. <<")
    print("=" * 58)
    need = disk.model.split()[0][:8] if disk.model else "ARCTIAN"
    try:
        t = input("Onaylamak icin '%s' yazin (bos = iptal): " % need).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return False
    return t.upper() == need.upper()


def main():
    ap = argparse.ArgumentParser(
        description="Arctian OS'u fiziksel diske yazar")
    ap.add_argument("--list", action="store_true", help="diskleri listele")
    ap.add_argument("--disk", help="hedef (Windows: 0,1,.. / Linux: /dev/sdX)")
    ap.add_argument("--image", help="yazilacak img (varsayilan: build/arctian.img)")
    ap.add_argument("--yes", action="store_true", help="onayi atla")
    ap.add_argument("--all", action="store_true",
                    help="sistem/boot disklerini de goster (varsayilan: gizli)")
    ap.add_argument("--no-partition", action="store_true",
                    help="MBR bolum girdisi ekleme")
    args = ap.parse_args()

    print("=" * 58)
    print("  ARCTIAN WRITER")
    print("=" * 58)
    print("")

    if args.list:
        usable, _all = usable_disks(all_disks=True)
        show(usable, all_disks=args.all)
        return 0

    img_path = args.image or find_image()
    if not img_path or not os.path.isfile(img_path):
        print("HATA: arctian.img bulunamadi.")
        print("  Once 'make' ile derleyin (build/arctian.img uretilir).")
        print("  Veya --image ile yol verin.")
        return 1

    with open(img_path, "rb") as f:
        raw = f.read()
    if len(raw) < (SB_LBA + 1) * SECTOR:
        print("HATA: gecerli bir Arctian imaji degil (%s)" % img_path)
        return 1

    img = patch_image(raw, add_partition=not args.no_partition)
    print("Imaj : %s (%s)" % (img_path, human(len(raw))))
    print("Superblock: INSTALLED (kurulum adimi atlanir)")
    print("")

    disks, all_list = usable_disks(all_disks=args.all)
    if args.disk:
        target = None
        key = args.disk.strip()
        for d in disks:
            if key == str(d.number) or key.lower() == d.path.lower() or \
               key.lower() == str(d.number).lower():
                target = d
                break
        if not target:
            print("HATA: uygun disk bulunamadi: %s" % args.disk)
            if not args.all:
                print("      (Sistem diski gizli; gormek icin --all kullanin.)")
            return 1
    else:
        target = pick(disks, all_disks=args.all)
        if not target:
            print("Iptal edildi.")
            return 1

    if target.system or target.boot:
        print("")
        print("!!! DIKKAT: Bu disk SISTEM/BOOT diski olarak isaretli.")
        print("    Yanlislikla sistem diskinizi silmek istemezsiniz.")
        if not args.yes and not confirm(target):
            print("Iptal edildi.")
            return 1
    elif not args.yes:
        if not confirm(target):
            print("Iptal edildi.")
            return 1

    if target.size and target.size < len(raw):
        print("HATA: disk imajdan kucuk (%s < %s)"
              % (human(target.size), human(len(raw))))
        return 1

    print("")
    written = write_image(img, target.path)
    print("Yazildi: %s (%s)" % (human(written), target.path))

    ok = True
    boot = read_lba(target.path, 0)
    if boot[510:512] == b"\x55\xAA":
        print("Dogrulama 1/2: onyukleme imzasi (0x55AA) OK")
    else:
        print("Dogrulama 1/2: onyukleme imzasi YOK!")
        ok = False

    sb = read_lba(target.path, SB_LBA)
    if len(sb) >= 16:
        magic, = struct.unpack_from("<I", sb, 0)
        flags, = struct.unpack_from("<I", sb, 8)
        if magic == SB_MAGIC and (flags & SB_FLAG_INSTALLED):
            print("Dogrulama 2/2: superblock OK (INSTALLED, kurulum atlanir)")
        else:
            print("Dogrulama 2/2: superblock HATALI (magic=%08X flags=%08X)"
                  % (magic, flags))
            ok = False
    else:
        print("Dogrulama 2/2: superblock okunamadi")
        ok = False

    if not ok:
        print("")
        print("Dogrulama basarisiz. Diski bilgisayardan cikarip tekrar deneyin.")
        return 2

    print("")
    print("=" * 58)
    print("  TAMAMLANDI")
    print("=" * 58)
    print("  1) Bilgisayari kapatin.")
    print("  2) Diski takin.")
    print("  3) Boot sirasi: bu disk (Legacy/BIOS).")
    print("  4) Arctian acilacak; dosyalar kalici olarak diskte kalir.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nIptal edildi.")
        sys.exit(1)
