#!/usr/bin/env python3
"""The disk tool's size line: Format takes as much as the user types
(SPEC.md 52.2.7).

    python3 tests/hdsize.py

The fixture is a BLANK 321MB drive on MartyPC's XT-IDE - a 654/16/63 VHD
(bigvol's geometry) whose MBR carries a signature and four empty entries - so
the whole disk is one free hole and slot 1's extent is all of it:
659,232 - 63 = 659,169 sectors, 321 MB.

  1. The tool opens on slot 1 and the line reads `Size [321] MB of 321 -
     type a number`, the 321 in the box INVERTED (nothing typed: the whole
     extent, which the first digit replaces).
  2. The keys `0 1 0 0 5 Backspace 0` are typed. A leading 0 is refused, 1005
     passes the extent and is refused, Backspace takes 100 to 10, and the last
     0 makes it 100 again - so the box reads `100_` in plain ink, and it
     would read something else if any one of those four rules were missing.
  3. Format, twice (the confirm). Slot 1 must come out at LBA 63 for 205,569
     sectors: 100 x 2,048 from LBA 63 ends at 204,863, which is rounded UP to
     the cylinder boundary 205,632 (204 x 1,008).
  4. Slot 2 is picked, and the box must read an inverted `221` beside `MB of
     221` - the number was
     reset to 'all' and re-measured against the hole that is left. Format it:
     LBA 205,632 for the remaining 453,600 sectors.

Both slots are asserted on the HOST, off the MBR and each volume's boot
sector: FAT16, and the BPB's sector count is the entry's.

BREAK IT ON PURPOSE: with hd_tw_cap's call taken out of hd_tw_format, slot 1
is the whole 659,169 sectors and slot 2 finds no room; with the leading-zero
or the extent refusal taken out of hd_tw_key, step 2's box reads `0`/`1005`
rather than `100`.

OS88_SHOT=<dir> writes the tool window as a PNG at each of the three reads.
"""
import hashlib
import os
import struct
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
from os88mouse import Mouse                                 # noqa: E402
import instdeep as ID                                       # noqa: E402
import instrest as IR                                       # noqa: E402
import bigvol as BV                                         # noqa: E402

SECTOR = 512
C, H, S = BV.C, BV.H, BV.S
CYL = H * S

# tool.inc's geometry, content-relative
HTW_LX, HTW_R0Y, HTW_ROWH = 4, 22, 12
HTW_SZY, HTW_SZBX, HTW_SZBW = 106, 38, 46
HTW_BY, HTW_BH, HTW_B0X, HTW_BW0 = 124, 16, 4, 64
HDP_B0X, HDP_BW0 = 2, 64            # cppage.inc: the page's Format button


def stage(vhd):
    size, foot = BV.vhd_footer(C, H, S)
    mbr = bytearray(SECTOR)
    mbr[510:512] = b"\x55\xAA"
    with open(vhd, "wb") as f:
        f.write(mbr)
        f.truncate(size)
        f.seek(size)
        f.write(foot)


def entry(vhd, slot):
    mbr = open(vhd, "rb").read(SECTOR)
    e = mbr[446 + 16 * slot:462 + 16 * slot]
    return e[4], struct.unpack_from("<I", e, 8)[0], \
        struct.unpack_from("<I", e, 12)[0]


def open_tool(m):
    """Control Panel -> Drivers -> Hard Drive -> its page -> Format."""
    mo = Mouse(marty=m)
    mo.menu(8, 8, 8, 40)
    cp = [w for w in ID.wins(m) if w[3] >= 280 and w[4] >= 100]
    if not cp:
        sys.exit("hdsize: no Control Panel")
    x0, y0 = ID.content(cp[0])
    mo.click(x0 + 40, y0 + ID.CP_I0Y + ID.CP_IDRV * ID.CP_IROWH + 7)
    mo.click(x0 + ID.CP_RX + 40,
             y0 + ID.CP_DBY1 + ID.CP_DROWH + ID.CP_DROWH // 2, settle=8.0)
    nst = m.read(m.sym("cp_nst"), 1)[0]
    mo.click(x0 + 40, y0 + ID.CP_I0Y + nst * ID.CP_IROWH + 7, settle=3.0)
    known = {w[0] for w in ID.wins(m)}
    mo.click(x0 + ID.CP_RX + HDP_B0X + HDP_BW0 // 2,
             y0 + ID.HDP_BY + ID.HDP_BH // 2, settle=8.0)
    tw = next((w for w in ID.wins(m) if w[0] not in known), None)
    if tw is None:
        sys.exit("hdsize: the disk tool never opened")
    tx, ty = ID.content(tw)
    return mo, tx, ty


def line_says(m, mo, tx, ty, tab, box, typed, total, shot=None):
    """The size line: the box holds `box` - inverted when nothing is typed,
    plain with a caret when something is - and the hint names the extent."""
    IR.park(mo)
    M.settle(m)
    rows = IR.screen(m)[2]
    if shot and os.environ.get("OS88_SHOT"):
        from PIL import Image
        h = min(180, len(rows) - (ty - 14))     # CGA: the screen ends first
        img = Image.new("L", (320, h))
        img.putdata([255 * rows[ty - 14 + y][tx - 2 + x]
                     for y in range(h) for x in range(320)])
        img.resize((960, 3 * h)).save(os.path.join(os.environ["OS88_SHOT"],
                                                 shot + ".png"))
    ink = IR.render(box + ("_" if typed else ""), tab)
    if not typed:
        ink = [[1 - p for p in r] for r in IR.render(box, tab)]
    y0 = ty + HTW_SZY - 1
    inbox = IR.find(rows, tx + HTW_SZBX, y0, HTW_SZBW, 10, ink)
    hint = IR.find(rows, tx + HTW_SZBX + HTW_SZBW, y0, 300 - HTW_SZBX
                   - HTW_SZBW - 4, 10,
                   IR.render("MB of %d - type a number" % total, tab))
    label = IR.find(rows, tx, y0, HTW_SZBX, 10, IR.render("Size", tab))
    return inbox and hint and label


def format_slot(m, mo, tx, ty, vhd, slot):
    bx, by = tx + HTW_B0X + HTW_BW0 // 2, ty + HTW_BY + HTW_BH // 2
    base = open(vhd, "rb").read(SECTOR)
    mo.click(bx, by, settle=3.0)                # arms
    mo.click(bx, by, settle=0)                  # goes
    M.until(m, lambda _: open(vhd, "rb").read(SECTOR) != base,
            "Format to write slot %d's entry" % (slot + 1), limit=300.0)
    t, lba, n = entry(vhd, slot)

    def vbr():
        with open(vhd, "rb") as f:
            f.seek(lba * SECTOR)
            return f.read(SECTOR)
    M.until(m, lambda _: vbr()[510:512] == b"\x55\xAA",
            "slot %d's boot sector" % (slot + 1), limit=600.0)
    M.quiesce(m, lambda: hashlib.sha1(open(vhd, "rb").read(4 << 20)).digest(),
              guest=2.0, stable=6, budget=300.0, what="the drive to go quiet")
    b = vbr()
    tot = struct.unpack_from("<H", b, 19)[0] or struct.unpack_from("<I", b, 32)[0]
    return t, lba, n, tot, b[54:62]


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    run_dir = M.stage_run_dir("hdsize")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    stage(vhd)
    bad = []
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE, run_dir=run_dir) as m:
        M.settle(m)
        tab = IR.glyph_table(m)
        mo, tx, ty = open_tool(m)
        if not line_says(m, mo, tx, ty, tab, "321", False, 321, "open"):
            bad.append("the tool did not open on `Size [321] MB of 321`, "
                       "the 321 inverted")
        for k in ("Digit0", "Digit1", "Digit0", "Digit0", "Digit5",
                  "Backspace", "Digit0"):
            m.key(k)
            m.advance(frames=6)
        m.run()                                 # advance() left it paused
        if not line_says(m, mo, tx, ty, tab, "100", True, 321, "typed"):
            bad.append("`0 1 0 0 5 Bksp 0` did not leave `100_` in the box")
        else:
            print("  typed: Size [100_] MB of 321")

        t, lba, n, tot, fs = format_slot(m, mo, tx, ty, vhd, 0)
        print("  slot 1: type %02Xh, LBA %d, %d sectors, BPB %d, %r"
              % (t, lba, n, tot, fs))
        want = ((63 + 100 * 2048 + CYL - 1) // CYL) * CYL - 63
        if (lba, n) != (63, want):
            bad.append("slot 1 is LBA %d for %d sectors, not 63 for %d"
                       % (lba, n, want))
        if tot != n or fs != b"FAT16   ":
            bad.append("slot 1's volume is %r, %d sectors" % (fs, tot))
        if bad:                                 # slot 2 would only wait out
            sys.exit("hdsize: " + "; ".join(bad))   # a Format with no room

        mo.click(tx + 40, ty + HTW_R0Y + HTW_ROWH + 4, settle=2.0)  # slot 2
        rest = C * CYL - (63 + want)
        if not line_says(m, mo, tx, ty, tab, str(rest >> 11), False,
                         rest >> 11, "slot2"):
            bad.append("slot 2 did not read `Size [%d] MB of %d`"
                       % (rest >> 11, rest >> 11))
        t, lba, n, tot, fs = format_slot(m, mo, tx, ty, vhd, 1)
        print("  slot 2: type %02Xh, LBA %d, %d sectors, BPB %d, %r"
              % (t, lba, n, tot, fs))
        if (lba, n) != (63 + want, rest):
            bad.append("slot 2 is LBA %d for %d sectors, not %d for %d"
                       % (lba, n, 63 + want, rest))
        if tot != n or fs != b"FAT16   ":
            bad.append("slot 2's volume is %r, %d sectors" % (fs, tot))
    if bad:
        sys.exit("hdsize: " + "; ".join(bad) + " (SPEC.md 52.2.7)")
    print("hdsize: a typed 100MB came out 100MB on a cylinder, and the rest "
          "of the drive was still there for slot 2")


if __name__ == "__main__":
    main()
