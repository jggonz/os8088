#!/usr/bin/env python3
"""mksd.py - a microSD card image for libpmhost's PicoMEM (docs/plans/completed/PICOMEM-EMU-PLAN.md 4.5).

    python3 tools/picomem/mksd.py OUT.img [--mb 32] [FILE ...] [DIR/NAME=SRC ...]
                                  [--fdd N:IMG] [--hdd N:IMG] [--cfg FIELD=VALUE]
    python3 tools/picomem/mksd.py --selfcheck

The card reads its SD through FatFS (lib/FatFs_SPI, pm_disk.cpp): a
`config.txt` and a binary `PICOMEM.CFG` at the root, floppy images in
`/FLOPPY` and hard disk images in `/HDD`. This writes a FAT16 volume holding
exactly what it is given, with those two folders always present, as a
SUPERFLOPPY - the boot sector at LBA 0, which FatFS mounts as it mounts a
partitioned card. Every name is 8.3 and upper case; FatFS matches the
firmware's `0:/config.txt` against it case-blind.

    mksd.py build/picomem/sd.img                         # an empty card
    mksd.py sd.img config.txt FLOPPY/DOS.IMG=dos33.img   # with a config and a floppy
    mksd.py sd.img --fdd 1:build/apps360.img             # B: an image off the card
    mksd.py sd.img --hdd 0:hd.img --cfg FastBoot=1       # C: one, and no key wait

--fdd/--hdd copy the image into /FLOPPY or /HDD and name it in a PICOMEM.CFG,
the card's binary saved setup (PMCFG_t): what the BIOS's setup menu would have
written after a user picked the image. Everything not set is the firmware's
own default, which is zero (PM_ResetConfig). The layout is PMCFG_t's and
--selfcheck compares it with the offsets libpmhost reports (pm_cfg_fields),
so a firmware that moves a field fails there rather than on a card.

An empty card is not the same machine as no card: with none, the firmware's
CMD_FDD_Mount never counts the PC's floppies and the BIOS then clears them
from the equipment word (the plan's 6.3) - so every profile gets one.

Deterministic: the volume serial and every timestamp are fixed, so the same
inputs give the same bytes. GPL-2.0-or-later, like the firmware it feeds.
"""
import argparse
import os
import struct
import sys

SECTOR = 512
SERIAL = 0x0588_0EED
STAMP_DATE = ((2026 - 1980) << 9) | (10 << 5) | 29   # 2026-10-29, fixed
STAMP_TIME = 0


# PMCFG_t (src/pm_defines.h), as offsets: (offset, kind). FSizeName_t is a
# u16 size and a 14-byte name; the paths are 64 bytes each.
CFG_SIZE = 644
CFG = {"FDD": (0, "fsn2"), "HDD": (32, "fsn4"), "FDD_Attribute": (96, "b2"),
       "HDD_Attribute": (98, "b4"), "MEM_Map": (138, "b64"), "EMS_Port": (202, "b"),
       "EMS_Addr": (203, "b"), "FastBoot": (207, "b"), "PMBOOT": (208, "b"),
       "EnableUSB": (210, "b"), "Boot_HDD": (213, "b"), "EnableWifi": (214, "b"),
       "ne2000IRQ": (215, "b"), "ne2000Port": (216, "w"), "Adlib": (229, "b"),
       "TDYPort": (230, "w"), "CMSPort": (232, "w"), "SBPort": (234, "w"),
       "SBIRQ": (236, "b"), "MMBPort": (238, "w"), "UseRTC": (241, "b"),
       "FDDPath": (260, "p2"), "HDDPath": (388, "p4")}


def read_root_file(img, want):
    """A root-directory file's bytes off a FAT16 volume - this tool's or one
    the card's FatFS has written to since - or None"""
    with open(img, "rb") as f:
        bs = f.read(SECTOR)
        bps, spc, res, nfats, rootn = struct.unpack_from("<HBHBH", bs, 11)
        fatsz = struct.unpack_from("<H", bs, 22)[0]
        f.seek(res * bps)
        fat = f.read(fatsz * bps)
        f.seek((res + nfats * fatsz) * bps)
        root = f.read(rootn * 32)
        data0 = res + nfats * fatsz + rootn * 32 // bps
        for i in range(0, len(root), 32):
            e = root[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] & 0x18 or e[:11] != name83(want):
                continue
            clus, size = struct.unpack_from("<HI", e, 26)
            out = b""
            while 2 <= clus < 0xFFF8 and len(out) < size:
                f.seek((data0 + (clus - 2) * spc) * bps)
                out += f.read(spc * bps)
                clus = struct.unpack_from("<H", fat, 2 * clus)[0]
            return out[:size]
    return None


def picomem_cfg(fdd=(), hdd=(), sets=(), base=None):
    """PICOMEM.CFG: zeros (or BASE, a card's own saved file), then FDD/HDD n
    named (n, NAME, size in KB), then FIELD=VALUE for the byte and word
    fields"""
    c = bytearray(base[:CFG_SIZE].ljust(CFG_SIZE, b"\0") if base else CFG_SIZE)
    for kind, base, lst in (("FDD", 0, fdd), ("HDD", 32, hdd)):
        for n, name, kb in lst:
            o = base + 16 * n
            # size FExxh means "a physical drive, redirected" - never that
            struct.pack_into("<H14s", c, o, min(kb, 0xFDFF), name.encode("ascii"))
    for k, v in sets:
        if k not in CFG or CFG[k][1] not in ("b", "w"):
            raise SystemExit("mksd: %r is not a byte or word field of PMCFG_t (%s)"
                             % (k, ", ".join(x for x in CFG if CFG[x][1] in ("b", "w"))))
        o, t = CFG[k]
        struct.pack_into("<B" if t == "b" else "<H", c, o, int(v, 0))
    return bytes(c)


def name83(name):
    base, _, ext = name.upper().rpartition(".") if "." in name else (name.upper(), "", "")
    if not base:
        base, ext = ext, ""
    if len(base) > 8 or len(ext) > 3 or not base:
        raise SystemExit("mksd: %r is not an 8.3 name" % name)
    return base.ljust(8).encode("ascii") + ext.ljust(3).encode("ascii")


def dirent(n11, attr, clus, size):
    return struct.pack("<11sBBBHHHHHHHI", n11, attr, 0, 0, STAMP_TIME, STAMP_DATE,
                       STAMP_DATE, 0, STAMP_TIME, STAMP_DATE, clus, size)


def build(out, files, mb=32):
    """files: [(folder or "", NAME, bytes)]"""
    total = mb * 1024 * 1024 // SECTOR
    spc = 4 if mb <= 64 else 8 if mb <= 128 else 16 if mb <= 256 else 32
    reserved, nfats, root_ent = 1, 2, 512
    root_secs = root_ent * 32 // SECTOR
    fatsz = 1
    while True:                                 # the FAT that fits what it maps
        data = total - reserved - nfats * fatsz - root_secs
        clusters = data // spc
        need = ((clusters + 2) * 2 + SECTOR - 1) // SECTOR
        if need <= fatsz:
            break
        fatsz = need
    if not 4085 <= clusters < 65525:
        raise SystemExit("mksd: %d MB gives %d clusters, not a FAT16 volume" % (mb, clusters))
    csize = spc * SECTOR
    fat = [0xFFF8, 0xFFFF] + [0] * clusters
    data_start = reserved + nfats * fatsz + root_secs
    img = bytearray(total * SECTOR) if False else None
    blobs = {}                                  # cluster -> bytes
    nxt = [2]

    def alloc(payload):
        n = max(1, (len(payload) + csize - 1) // csize)
        first = nxt[0]
        if first + n - 2 > clusters:
            raise SystemExit("mksd: the card is full - use a bigger --mb")
        for i in range(n):
            fat[first + i] = first + i + 1 if i < n - 1 else 0xFFFF
        for i in range(n):
            blobs[first + i] = payload[i * csize:(i + 1) * csize]
        nxt[0] += n
        return first

    folders = {"FLOPPY": [], "HDD": []}
    root = []
    for folder, name, payload in files:
        if folder:
            folders.setdefault(folder.upper(), []).append((name, payload))
        else:
            root.append((name, payload))
    rootdir = [struct.pack("<11sB", b"PICOMEM SD ", 0x08) + bytes(20)]
    for folder in sorted(folders):
        entries = folders[folder]
        # the folder's own cluster first, then its files, then its listing
        fclus = alloc(bytes(csize))
        lst = [dirent(b".          ", 0x10, fclus, 0), dirent(b"..         ", 0x10, 0, 0)]
        for name, payload in entries:
            c = alloc(payload) if payload else 0
            lst.append(dirent(name83(name), 0x20, c, len(payload)))
        body = b"".join(lst)
        if len(body) > csize:
            raise SystemExit("mksd: too many files in %s" % folder)
        blobs[fclus] = body
        rootdir.append(dirent(name83(folder), 0x10, fclus, 0))
    for name, payload in root:
        c = alloc(payload) if payload else 0
        rootdir.append(dirent(name83(name), 0x20, c, len(payload)))
    if len(rootdir) > root_ent:
        raise SystemExit("mksd: too many files at the root")

    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x3C\x90"
    bs[3:11] = b"OS8088SD"
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, spc, reserved, nfats, root_ent,
                     total if total < 65536 else 0, 0xF8, fatsz, 63, 255, 0,
                     total if total >= 65536 else 0)
    struct.pack_into("<BBBI11s8s", bs, 36, 0x80, 0, 0x29, SERIAL, b"PICOMEM SD ", b"FAT16   ")
    bs[510:512] = b"\x55\xAA"
    fatb = struct.pack("<%dH" % len(fat), *fat)
    fatb += bytes(fatsz * SECTOR - len(fatb))

    with open(out, "wb") as f:
        f.write(bs)
        f.write(bytes((reserved - 1) * SECTOR))
        for _ in range(nfats):
            f.write(fatb)
        rd = b"".join(rootdir)
        f.write(rd + bytes(root_secs * SECTOR - len(rd)))
        for c in sorted(blobs):
            f.seek((data_start + (c - 2) * spc) * SECTOR)
            f.write(blobs[c])
        f.truncate(total * SECTOR)
    return clusters


def selfcheck():
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "sd.img")
        build(p, [("", "config.txt", b"BIOS D000\n"), ("FLOPPY", "A.IMG", bytes(368640))])
        b = open(p, "rb").read()
        ok = b[510:512] == b"\x55\xAA" and b[54:62] == b"FAT16   " and \
            len(b) == 32 * 1024 * 1024 and b.find(b"CONFIG  TXT") > 0 and \
            b.find(b"BIOS D000") > 0
        q = os.path.join(d, "sd2.img")
        build(q, [("", "config.txt", b"BIOS D000\n"), ("FLOPPY", "A.IMG", bytes(368640))])
        ok = ok and open(q, "rb").read() == b
    # the layout against the firmware's own, when libpmhost is built
    lib = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))), "build", "picomem", "v1", "libpmhost-v1.so")
    if os.path.exists(lib):
        import ctypes
        L = ctypes.CDLL(lib)
        L.pm_cfg_fields.restype = ctypes.c_char_p
        fw = dict(kv.split("=") for kv in L.pm_cfg_fields().decode().split(","))
        bad = ["%s %s/%d" % (k, fw[k], CFG[k][0]) for k in CFG
               if k in fw and int(fw[k]) != CFG[k][0]]
        if int(fw["size"]) != CFG_SIZE:
            bad.append("size %s/%d" % (fw["size"], CFG_SIZE))
        print("mksd: PMCFG_t layout against libpmhost: %s" % (", ".join(bad) or "agrees"))
        ok = ok and not bad
    print("mksd: selfcheck %s" % ("ok" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", nargs="?")
    ap.add_argument("items", nargs="*", help="FILE (at the root) or FOLDER/NAME=SRC")
    ap.add_argument("--mb", type=int, default=32)
    ap.add_argument("--fdd", action="append", default=[], metavar="N:IMG",
                    help="floppy N (0 = A:, 1 = B:) is IMG, off the card")
    ap.add_argument("--hdd", action="append", default=[], metavar="N:IMG",
                    help="hard disk N (0 = 80h) is IMG, off the card")
    ap.add_argument("--cfg", action="append", default=[], metavar="FIELD=VALUE",
                    help="a byte or word field of PICOMEM.CFG")
    ap.add_argument("--cfg-from", metavar="SDIMG",
                    help="start PICOMEM.CFG from the one on SDIMG - a card's own, "
                         "saved by its BIOS on a boot, so its memory map is the "
                         "machine's (the BIOS turns EMS off against any other)")
    ap.add_argument("--no-wifi", action="store_true",
                    help="no WIFI.TXT: the card's NE2000 never starts")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    if a.selfcheck:
        return selfcheck()
    if not a.out:
        ap.error("OUT is required")
    files = []
    for it in a.items:
        if "=" in it:
            dst, src = it.split("=", 1)
        else:
            dst, src = os.path.basename(it), it
        folder, _, name = dst.rpartition("/")
        files.append((folder, name, open(src, "rb").read()))
    fdd, hdd = [], []
    for lst, opt, folder in ((fdd, a.fdd, "FLOPPY"), (hdd, a.hdd, "HDD")):
        for spec in opt:
            num, _, src = spec.partition(":")
            base = os.path.splitext(os.path.basename(src))[0].upper()[:8]
            data = open(src, "rb").read()
            files.append((folder, base + ".IMG", data))
            lst.append((int(num), base, len(data) // 1024))
    # the Pico W's Wi-Fi credentials: without WIFI.TXT the firmware never
    # starts its NE2000 (PM_ConnectWifi), and what they say does not matter
    # to libpmhost, whose network is the emulator's (pmhost_wifi.cpp)
    if not a.no_wifi and not any(n.upper() == "WIFI.TXT" for f, n, _ in files if not f):
        files.append(("", "WIFI.TXT", b"os8088\r\nos8088-picomem\r\n"))
    base = None
    if a.cfg_from:
        base = read_root_file(a.cfg_from, "PICOMEM.CFG")
        if base is None:
            raise SystemExit("mksd: %s has no PICOMEM.CFG" % a.cfg_from)
    if fdd or hdd or a.cfg or base:
        files.append(("", "PICOMEM.CFG", picomem_cfg(fdd, hdd,
                                                     [kv.split("=", 1) for kv in a.cfg],
                                                     base)))
    n = build(a.out, files, a.mb)
    print("mksd: %s, %d MB FAT16, %d clusters, %d file(s)" % (a.out, a.mb, n, len(files)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
