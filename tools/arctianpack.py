
















import json
import os
import shutil
import struct
import subprocess
import sys

AMOD_MAGIC = 0x444F4D41
AMOD_ABI = 2
SB_MAGIC = 0x54435241
SB_VERSION = 1
SECTOR = 512


def cksum(data: bytes) -> int:
    s = 0x12345678
    for b in data:
        s = ((s << 1) & 0xFFFFFFFF) ^ (s >> 31) ^ b
        s &= 0xFFFFFFFF
    return s


def read(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def pad(data: bytes, align: int = SECTOR) -> bytes:
    r = len(data) % align
    return data + b"\x00" * (align - r) if r else data


def elf_bss_size(path: str) -> int:
    data = read(path)
    if data[:4] != b"\x7fELF":
        return 0
    e_shoff = struct.unpack_from("<Q", data, 0x28)[0]
    e_shentsize = struct.unpack_from("<H", data, 0x3A)[0]
    e_shnum = struct.unpack_from("<H", data, 0x3C)[0]
    total = 0
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        sh_type = struct.unpack_from("<I", data, off + 4)[0]
        sh_size = struct.unpack_from("<Q", data, off + 0x20)[0]
        if sh_type == 8:
            total += sh_size
    return total


def build_amod(name, mtype, load_addr, image, bss_size, flags=0):
    hdr = struct.pack(
        "<IHHIIIII32s16sI",
        AMOD_MAGIC, AMOD_ABI, mtype, load_addr, len(image), bss_size, 0,
        cksum(image),
        name.encode()[:31].ljust(32, b"\x00"),
        b"0.1".ljust(16, b"\x00"),
        flags,
    )
    return hdr + b"\x00" * (SECTOR - len(hdr)) + image


def build_iso(cfg, stage2, main_kernel, module_payloads, img_bytes=b"", hd_boot=b""):
    stage1_iso = bytearray(read(cfg["boot"]["stage1_iso"]))

    payload = bytes(stage2) + main_kernel
    words = (len(payload) + 1) // 2
    mag = stage1_iso.find(b"PBYT")
    if mag < 2:
        print("  UYARI: stage1_iso 'PBYT' isareti bulunamadi; ISO payload yamalanmadi")
    else:
        struct.pack_into("<H", stage1_iso, mag - 2, words & 0xFFFF)

    boot_iso = pad(bytes(stage1_iso) + payload)
    boot_sectors = len(boot_iso) // SECTOR

    iso_root = os.path.join(os.path.dirname(cfg["output"]["img"]), "iso_root")
    shutil.rmtree(iso_root, ignore_errors=True)
    os.makedirs(iso_root, exist_ok=True)
    with open(os.path.join(iso_root, "boot_iso.img"), "wb") as f:
        f.write(boot_iso)
    
    with open(os.path.join(iso_root, "hdimg.bin"), "wb") as f:
        f.write(hd_boot)
    for fname, disk in module_payloads:
        with open(os.path.join(iso_root, fname), "wb") as f:
            f.write(disk)

    out_iso = cfg["output"]["iso"]
    cmd = [
        "xorriso", "-as", "mkisofs", "-R", "-r", "-J",
        "-b", "boot_iso.img", "-no-emul-boot",
        "-boot-load-size", str(boot_sectors),
        "-o", out_iso, iso_root,
    ]
    try:
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"  ISO     : {out_iso} (boot {boot_sectors} sektor)")
    except Exception as e:
        print(f"  ISO uretilemedi: {e}")


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    cfg = json.load(open(os.path.join(root, "config", "layout.json"), encoding="utf-8"))

    stage1 = read(cfg["boot"]["stage1"])
    stage2 = bytearray(read(cfg["boot"]["stage2"]))
    main_kernel = read(cfg["boot"]["main_kernel"])

    if len(stage1) != 512:
        print(f"  UYARI: stage1 {len(stage1)} byte (512 olmali)")

    
    struct.pack_into("<Q", stage2, len(stage2) - 8, len(main_kernel))

    
    boot_image = pad(stage1 + bytes(stage2) + main_kernel)
    boot_sectors = len(boot_image) // SECTOR
    sb_lba = cfg["superblock_lba"]
    if boot_sectors > sb_lba:
        print(f"  HATA: boot imaji {boot_sectors} sektor, superblock LBA {sb_lba} ile cakisiyor!")
        sys.exit(1)

    next_lba = cfg["module_data_start_lba"]
    module_images = []
    module_entries = []
    module_payloads = []
    for m in cfg["modules"]:
        image = read(m["bin"])
        bss = elf_bss_size(m["elf"]) if m.get("elf") else 0
        load_addr = int(m["load_addr"], 16)
        flags = m.get("flags", 0)
        disk = pad(build_amod(m["name"], m["type"], load_addr, image, bss, flags))
        sectors = len(disk) // SECTOR
        module_images.append((next_lba, disk))
        module_payloads.append((m.get("iso_name", m["name"] + ".bin"), disk))
        module_entries.append(
            struct.pack(
                "<32sHHIIIIIII",
                m["name"].encode()[:31].ljust(32, b"\x00"),
                m["type"], AMOD_ABI, flags, load_addr, next_lba, sectors,
                len(image), 0, cksum(image),
            )
        )
        next_lba += sectors

    module_table = b"".join(module_entries)
    module_table_sectors = max(1, (len(module_table) + SECTOR - 1) // SECTOR)
    module_table = pad(module_table)

    total_sectors = max(next_lba + 64, cfg.get("min_sectors", 131072))
    img = bytearray(total_sectors * SECTOR)
    img[0:len(boot_image)] = boot_image

    sb = struct.pack(
        "<IHHIIIIIII",
        SB_MAGIC, SB_VERSION, struct.calcsize("<IHHIIIIIII"),
        0, len(module_entries), cfg["module_table_lba"], module_table_sectors,
        cfg["state_lba"], 0, 0,
    )
    sb = sb[:-4] + struct.pack("<I", cksum(sb[:-4]))
    img[sb_lba * SECTOR: sb_lba * SECTOR + len(sb)] = sb

    mt = cfg["module_table_lba"] * SECTOR
    img[mt:mt + len(module_table)] = module_table
    for lba, disk in module_images:
        off = lba * SECTOR
        img[off:off + len(disk)] = disk

    out_img = cfg["output"]["img"]
    os.makedirs(os.path.dirname(out_img), exist_ok=True)
    with open(out_img, "wb") as f:
        f.write(img)

    print("  Arctian paketlendi")
    print(f"  Boot    : {boot_sectors} sektor ({len(boot_image)} byte)")
    print(f"  SB      : LBA {sb_lba} ({len(module_entries)} modul)")
    for m in module_entries:
        name = struct.unpack_from("<32s", m, 0)[0].split(b"\x00")[0].decode()
        lba = struct.unpack_from("<I", m, 44)[0]
        sec = struct.unpack_from("<I", m, 48)[0]
        print(f"  Modul   : {name:<12} LBA {lba} ({sec} sektor)")
    print(f"  Imaj    : {out_img} ({total_sectors} sektor, {total_sectors * 512 // 1024} KB)")

    
    build_iso(cfg, stage2, main_kernel, module_payloads, bytes(img), boot_image)


if __name__ == "__main__":
    main()
