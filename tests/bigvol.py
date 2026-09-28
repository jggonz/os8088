#!/usr/bin/env python3
"""A volume past 32MB, used PAST 32MB (SPEC.md 18.7.5).

    python3 tests/bigvol.py

A volume was 65,535 sectors at most because a volume-relative LBA was a word.
It is FAT16's own ceiling now - just under 2GB - with the LBA's high word
carried beside it (`[dsk_lbahi]`, `[dsk_c2hi]`, DI into DSV_BLK). A volume
that is merely LARGE proves little: an install to an empty 321MB partition
puts every file in its first megabytes, where the high word is 0 and the old
code was right. So this row makes the high word UNAVOIDABLE:

  * MartyPC's XT-IDE takes a 654/16/63 VHD (321MB, one of the geometries its
    controller accepts), partitioned as one FAT16 type-06 slot at LBA 63;
  * the HOST formats it with mtools (a FAT writer that is not os8088's - 8KB
    clusters, TotSec32, so the mount has to fold TotSec32 in) and puts a 40MB
    FILLER.BIN at the front, then USER.TXT behind it: every free cluster the
    install can find starts past sector 65,536 of the volume;
  * the installer KEEPS the volume (SPEC.md 52.10.15, its default): it walks
    a 161-sector FAT raw, places KERNEL.SYS in the first free run - past 32MB,
    so BOOTHD_KOFS itself needs its high word - makes SYSTEM and APPS there
    (a new directory's cluster zeroed through dsk_wr1p_x) and writes every
    file there (the data walk's 32-bit cursor);
  * the partition is BOOTED: the VBR reads the kernel from past 32MB, the
    kernel adopts C: through int 13h with [dsk_lbahi] into the CHS divide,
    loads ASSOC.DAT from past 32MB - and then a package is LAUNCHED off C:,
    through a Disk window, from a folder past 32MB.

  * and the Disk window on C:'s root is READ (SPEC.md 22.7.1): FILLER.BIN is
    `40M` in the size column, USER.TXT `3700`, the status line `Size 40M`
    and `Free <n>M` - n the host's own count of free clusters - and the
    encoded FS_USED/FS_FREE words behind them are checked too. Broken on
    purpose (the column back on fm_ultoa_x) it reads bytes and goes red.

ASSERTED ON THE HOST, with instdeep's FAT reader: FILLER.BIN and USER.TXT
byte for byte, KERNEL.SYS the floppy's bytes in ONE RUN past sector 65,536,
the VBR's BOOTHD_KOFS naming it, and the tree there. ON THE GUEST: a desktop
from C:, and CALC.O88 opened out of C:/APPS.

REQUIRES mtools. It writes only its own staged VHD.
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88build                                            # noqa: E402
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
import hdboot as HB                                         # noqa: E402
import instdeep as ID                                       # noqa: E402
import instrest as IR                                       # noqa: E402

SECTOR = 512
C, H, S = 654, 16, 63               # at_formats.rs: 321MB, XT-IDE accepts it
BASE = S                            # the era's floor: cylinder 0 head 1
SECS = C * H * S - BASE             # to the last cylinder's end
FILLER_MB = 40
BOOTHD_KOFS = 504
USER_TXT = b"past the first thirty-two megabytes\r\n" * 100


def vhd_footer(c, h, s):
    size = c * h * s * SECTOR
    f = bytearray(512)
    f[0:8] = b"conectix"
    struct.pack_into(">IIQI", f, 8, 2, 0x00010000, 0xFFFFFFFFFFFFFFFF, 0)
    f[28:32] = b"mrty"
    struct.pack_into(">I", f, 32, 0x00010000)
    f[36:40] = b"Wi2k"
    struct.pack_into(">QQHBBI", f, 40, size, size, c, h, s, 2)
    f[68:84] = bytes(range(16))
    struct.pack_into(">I", f, 64, (~sum(f)) & 0xFFFFFFFF)
    return size, bytes(f)


def chs(lba):
    cyl, rem = divmod(lba, H * S)
    head, sec = divmod(rem, S)
    return bytes([head, (sec + 1) | ((cyl >> 8) << 6), cyl & 0xFF])


def filler():
    """40MB of a pattern a lost or moved cluster cannot reproduce."""
    blk = bytearray()
    for i in range(0, FILLER_MB * 1024 * 1024, 4096):
        blk += struct.pack("<I", i) * 1024
    return bytes(blk)


def stage(vhd):
    size, foot = vhd_footer(C, H, S)
    mbr = bytearray(SECTOR)
    mbr[446:462] = (bytes([0x00]) + chs(BASE) + bytes([0x06])
                    + chs(BASE + SECS - 1) + struct.pack("<II", BASE, SECS))
    mbr[510:512] = b"\x55\xAA"
    with tempfile.TemporaryDirectory() as td:
        img = os.path.join(td, "part.img")
        with open(img, "wb") as f:
            f.truncate(SECS * SECTOR)
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")

        def mt(*a):
            subprocess.run(a, check=True, env=env, stdout=subprocess.DEVNULL)
        mt("mformat", "-i", img, "-T", str(SECS), "-h", str(H), "-s", str(S),
           "-H", str(BASE), "-c", "16", "-r", "32", "-M", "512", "::")
        for name, data in (("FILLER.BIN", filler()), ("USER.TXT", USER_TXT)):
            open(os.path.join(td, name), "wb").write(data)
            mt("mcopy", "-i", img, os.path.join(td, name), "::/")
        part = open(img, "rb").read()
    with open(vhd, "wb") as f:
        f.write(mbr)
        f.seek(BASE * SECTOR)
        f.write(part)
        f.truncate(size)
        f.seek(size)
        f.write(foot)
    return hashlib.sha1(filler()).hexdigest()


def check(vhd, fsha):
    v = ID.partition(vhd)
    tree = v.tree()
    bad = []
    if v.bits != 16 or v.tot16 != 0:
        bad.append("the fixture is not a TotSec32 FAT16 (%d, %d) - the row "
                   "tests nothing" % (v.bits, v.tot16))
    got = v.read("FILLER.BIN")
    if got is None or hashlib.sha1(got).hexdigest() != fsha:
        bad.append("FILLER.BIN was not kept")
    if v.read("USER.TXT") != USER_TXT:
        bad.append("USER.TXT was not kept")
    for p in ("SYSTEM/TASKMGR.O88", "APPS/CALC.O88", "SYSTEM/FONTS/TALLX.F88",
              "ASSOC.DAT"):
        if p not in tree:
            bad.append("%s is missing" % p)
    src = ID.Vol(open(os88build.at("build/os8088-360.img"), "rb").read())
    if v.read("KERNEL.SYS") != src.read("KERNEL.SYS"):
        bad.append("KERNEL.SYS is not the floppy's bytes")
    first = [c for n, a, c, s in v.entries(0) if n == "KERNEL.SYS"][0]
    chain = v.chain(first)
    if chain != list(range(first, first + len(chain))):
        bad.append("KERNEL.SYS is not one run")
    kofs = (first - 2) * v.spc
    blob = open(vhd, "rb").read(BASE * SECTOR + SECTOR)
    vofs = struct.unpack_from("<I", blob, BASE * SECTOR + BOOTHD_KOFS)[0]
    if vofs != kofs:
        bad.append("BOOTHD_KOFS is %d, the kernel is %d in" % (vofs, kofs))
    if kofs < 65536:
        bad.append("the kernel went in below 32MB (%d) - the fixture did not "
                   "force the high word" % kofs)
    print("  FAT16 spc %d, TotSec32 %d; kernel at cluster %d = %d sectors "
          "into the data area, one run of %d" % (v.spc, v.tot32, first, kofs,
                                                  len(chain)))
    return bad


def units(m, ui, vhd):
    """SPEC.md 22.7.1, on the one volume in the suite that needs M: the Disk
    window's root lists FILLER.BIN as `40M` and USER.TXT as `3700`, and its
    status line reads `Size 40M   Free <n>M` - with FS_USED/FS_FREE, the
    encoded words, read out of the window's own block. Before 22.7.1 the
    free figure was the low 16 bits of 328,000-odd KB."""
    w = ui.open_drive("C")
    M.settle(m)
    base = ui._fsblk(w)
    free_w, used_w = struct.unpack("<HH", m.read(base + 20, 4))
    v = ID.partition(vhd)
    n = (v.tot32 - v.data_lba) // v.spc + 2     # one past the last cluster
    fc = sum(1 for c in range(2, n) if v.fat(c) == 0)
    free_mb = fc * v.spc // 2048
    bad = []
    if used_w != 0x8000 | FILLER_MB:
        bad.append("FS_USED is %04X, not 8000h|%d" % (used_w, FILLER_MB))
    if not (free_w & 0x8000 and free_mb <= (free_w & 0x7FFF) <= free_mb + 1):
        bad.append("FS_FREE is %04X against %dMB free on the host"
                   % (free_w, free_mb))
    tab = IR.glyph_table(m)

    def reads(text, name=None):
        """A COLUMN figure is looked for above the status line, with its
        file scrolled on screen first - `40M` is also inside `Size 40M`, and
        the root lists four folders ahead of the files."""
        h = w.h
        if name is not None:
            ui.scroll_to(ui.entry(name, w)[0], win=w)
            h -= 24                         # not the status line
        ui.mo.to(*IR.PARK)
        M.settle(m)
        rows = IR.screen(m)[2]
        ink = IR.render(text, tab)
        return any(IR.find(rows, w.x, w.y, w.w, h, k)
                   for k in (ink, [[1 - p for p in r] for r in ink]))
    for text, name in (("40M", "FILLER.BIN"), ("3700", "USER.TXT"),
                       ("Size 40M", None),
                       ("Free %dM" % (free_w & 0x7FFF), None)):
        if not reads(text, name):
            bad.append("the Disk window does not read %r%s"
                       % (text, " beside " + name if name else ""))
    print("  C:\\ reads FILLER.BIN 40M, USER.TXT 3700, Size 40M, Free %dM "
          "(FS_USED %04X, FS_FREE %04X; host %dMB free)"
          % (free_w & 0x7FFF, used_w, free_w, free_mb) if not bad else
          "  units: %s" % bad)
    return bad


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    run_dir = M.stage_run_dir("bigvol")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    fsha = stage(vhd)

    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE, run_dir=run_dir) as m:
        M.settle(m)
        mo, ix, iy = ID.open_installer(m)
        ID.run_install(m, mo, ix, iy)           # KEEP: the box's default
    bad = check(vhd, fsha)
    if bad:
        sys.exit("bigvol: " + "; ".join(bad))

    if not os.path.exists(HB.BLANK):
        with open(HB.BLANK, "wb") as f:
            f.write(bytes(368640))
    with M.launch(HB.BLANK, machine=ID.MACHINE, boot=0, run_dir=run_dir) as m:
        m.advance(frames=300)
        m.key("KeyC")
        for _ in range(160):
            m.advance(frames=30)
            w, h, px = m.fbuf()
            if len(HB.ink_groups(px, w, (4, 8))) > 20:
                break
        else:
            sys.exit("bigvol: FAIL - no desktop from the 321MB C:")
        print("  booted from C:")
        m.run()
        ui = os88ui.UI(m)
        ui.ready()
        bad = units(m, ui, vhd)
        if bad:
            sys.exit("bigvol: " + "; ".join(bad) + " (SPEC.md 22.7.1)")
        ui.path("C:/APPS/CALC.O88")             # raises naming what it saw
    print("bigvol: a 321MB FAT16 kept past 32MB, installed there, booted, "
          "and a package launched out of it")


if __name__ == "__main__":
    main()
