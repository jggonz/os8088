#!/usr/bin/env python3
"""A compressed file with NO HINT is still read as a compressed file
(SPEC.md 20.14.6, 20.14.6.1).

    python3 tests/rdcz.py [--adapter cga|herc] [--half fat|ram|both]

THE HINT IS A CACHE. §20.14 has said since it was written that a foreign tool
may drop those four directory bytes and that the read path checks the file's
own `'CZ'` header too — and it did not: `dskw_czexp` **validates** the hint,
it never **discovers** compression. The two halves below are the two ways a
file arrives without one, and they are the two the field reported:

  FAT   `README.TXT` on the shipped system disk, with its hint STRUCK OUT of
        the directory entry here on the host — which is what a copy by DOS,
        Windows or a Linux mount leaves behind. 8,088 packed bytes that must
        reach Note Pad as the 14,427 the host file is.

  RAM   the same file COPIED onto a mounted RAM disk, which has no directory
        entry to carry a hint AT ALL (SPEC.md 62.9). `.fsread` never looked
        at one in the first place, so this half was the worse of the two.
        The strike does not affect it either way — the file manager's copy
        goes through `api_file_find_raw` and moves the packed bytes whatever
        the directory says — which is why one boot can carry both halves.

**THE ASSERTION IS `np_len` AND NOT PIXELS**, `tests/lzfile.py`'s instrument
and for its reason: a window with a title and an empty note looks identical
to a window with the file in it, at every zoom. The measured red control is
**4,185** — neither the folded 14,427 nor the packed 8,088, because
`np_load` folds CRLF and stops at what a compressed stream is full of. So
what the field actually saw was a SHORT NOTE OF NONSENSE, and no length
carried in this file would have predicted that number. What discriminates is
`== want`, and the message says which of the three ways it missed.

**WHAT THIS ROW DELIBERATELY DOES NOT ASSERT** is `OSAPI_FILE_FIND`, and
SPEC.md 20.14.6.2.1 is why: FIND reports a hintless compressed file's PACKED
size, because sniffing there would cost a peek per directory entry and the
whole point of the sniff is that it costs no extra `int 13h`. Note Pad
survives that by claiming a fixed `NP_MAXKB` rather than sizing from what it
was told; an application that did size from FIND would get `FERR_BIG` rather
than garbage, which is the right way round to fail.

RED CONTROL, which is how this row earned its place (docs/WRITING-TESTS.md 1):
built at `elendilon`, the commit before the sniff, the FAT half reads 4,185
against the 14,427 it reads here.
"""
import argparse
import os
import shutil
import struct
import tempfile
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88build                                       # noqa: E402
import os88marty                                       # noqa: E402
import os88ui                                          # noqa: E402
from trackmove import pkg_syms                         # noqa: E402

MACHINE = {"cga": "os8088_5150_cga_gla", "herc": "os8088_5150_herc_gla"}

# THE SOURCE AND NOT build/readme-plain.txt, for tests/lzfile.py's reason: the
# Makefile writes CRLF onto the disk and np_load FOLDS it straight back off
# (SPEC.md 27.11), so what Note Pad ends up holding is the LF file byte for
# byte. Which makes it a better assertion than a length - 295 carriage
# returns had to arrive to be dropped.
PLAIN, DOC = "readme.txt", "README.TXT"

# kernel/disk.inc's directory-entry hint (SPEC.md 20.14.1). THREE cells and
# not one: the mark at 12 is what the read path tests, and 13/20 are the
# unpacked size it would otherwise believe. A strike that left either behind
# would be testing something else.
R_CZM, R_CZH, R_CZL, R_SIZE = 12, 13, 20, 28

# ctrl.inc's and page.inc's geometry, mirrored from tests/rdicon.py, which is
# where the mount sequence's coordinates are explained
CP_I0Y, CP_IROWH, CP_RX = 6, 14, 96
CP_DBY1, CP_DROWH, CP_IDRV = 20, 26, 2
RP_MNTX, RP_MNTW, RP_B0Y, RP_BH = 2, 64, 52, 16
DRVR_SZ, DRVR_SEG, RD_ROW = 16, 2, 3
TITLE_H = 18

fails = []


def say(*a):
    print(*a, flush=True)


def strike(img, name=DOC):
    """Zero the compression hint on `name`'s root-directory entry.

    It answers the entry's numbers as they were BEFORE the strike, so this
    row's arithmetic comes off the disk under test rather than out of this
    file - and a fixture that stops being compressed fails HERE, saying so,
    instead of failing an opaque length twenty steps later.
    """
    b = bytearray(open(img, "rb").read())
    bps = struct.unpack_from("<H", b, 11)[0]
    rsv = struct.unpack_from("<H", b, 14)[0]
    nfat, spf = b[16], struct.unpack_from("<H", b, 22)[0]
    nroot = struct.unpack_from("<H", b, 17)[0]
    root = (rsv + nfat * spf) * bps
    stem, ext = name.split(".")
    want = (stem.ljust(8) + ext.ljust(3)).encode()
    for i in range(nroot):
        o = root + i * 32
        if b[o] in (0, 0xE5) or bytes(b[o:o + 11]) != want:
            continue
        czm = b[o + R_CZM]
        if czm not in (0x5A, 0x5B):         # DSK_CZ_MARK + the format
            sys.exit("rdcz: %s is NOT compressed on %s (mark %#04x) - the "
                     "fixture this row stands on has changed, so striking "
                     "its hint would assert nothing" % (name, img, czm))
        packed = struct.unpack_from("<I", b, o + R_SIZE)[0]
        unpacked = struct.unpack_from("<H", b, o + R_CZL)[0] \
            | (b[o + R_CZH] << 16)
        b[o + R_CZM] = 0
        b[o + R_CZH] = 0
        struct.pack_into("<H", b, o + R_CZL, 0)
        open(img, "wb").write(bytes(b))
        return packed, unpacked
    sys.exit("rdcz: %s is not in %s's root directory" % (name, img))


def pseg_of(ui, w):
    """A window's owning package segment - W_SEG, which `os88geom.Win`
    decodes to read the title and does not keep."""
    import os88geom as geom
    rec = ui.m.read(ui.sym("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
    return rec[geom.W_SEG] | (rec[geom.W_SEG + 1] << 8)


def select(ui, name, win=None):
    """Single-click `name`'s row, so the next menu verb acts on it.

    BY NAME through `ui.entry` and `ui.scroll_to`, never at a remembered
    coordinate: how many rows a Disk window shows depends on its height, the
    adapter and the view mode, and a click that lands on the wrong row runs
    the verb on the wrong file and says nothing about having done so.
    """
    i, _ = ui.entry(name, win)
    row = ui.scroll_to(i, win=win)
    w = win if win is not None else ui.disk_window()
    ui.mo.click(*ui.row_xy(w, row))
    ui.settle(limit=10.0)


def note_len(ui):
    """Open README.TXT in the acting Disk window; answer Note Pad's fill."""
    P = pkg_syms("apps/notepad/notepad.asm")
    w = ui.open(DOC)                # resolved by NAME, and `open` waits for
    pseg = pseg_of(ui, w)           # the right thing by the entry's TYPE
    # np_len going non-zero is the expansion landing in the claim, so it is
    # waited for on the GUEST's clock and not given four host seconds
    # (docs/plans/SOAK-PARALLEL.md 1)
    try:
        os88marty.until(
            ui.m,
            lambda mm: int.from_bytes(mm.readseg(pseg, P["np_len"], 2),
                                      "little"),
            "Note Pad to hold the file", poll=0.1, guest=30.0)
    except os88marty.MartyError:
        pass
    return int.from_bytes(ui.m.readseg(pseg, P["np_len"], 2), "little")


def verdict(where, got, want, packed):
    if got == want:
        say("  %-4s ok   (np_len=%d - the sniff found the header the "
            "directory had lost)" % (where, got))
        return
    how = ("0, so the read REFUSED" if not got else
           "the PACKED bytes reached the reader - the sniff did not run. It "
           "is rarely %d exactly: np_load folds CRLF and stops at what a "
           "compressed stream is full of" % packed)
    say("  %-4s BAD  (np_len=%d, want %d; %s)" % (where, got, want, how))
    fails.append("%s: Note Pad holds %d bytes where the file's own header "
                 "says %d (%s)" % (where, got, want, how))


def mount_ramdisk(ui):
    """Control Panel -> Drivers -> Ram Disk -> its page -> Mount.

    Lifted from tests/rdicon.py, which is where these coordinates are
    explained; there is no `os88ui` verb for a Control Panel page yet.
    """
    m, mo = ui.m, ui.mo
    mo.menu(8, 8, 8, 40)
    ui.settle(limit=15.0)
    cp = ui.window("Control Panel")
    x0, y0 = cp.x + 1, cp.y + TITLE_H
    mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)     # Drivers
    ui.settle(limit=15.0)
    mo.click(x0 + CP_RX + 40,                                   # tick Ram Disk
             y0 + CP_DBY1 + RD_ROW * CP_DROWH + CP_DROWH // 2)
    ui.settle(limit=25.0)
    if not int.from_bytes(
            m.read(ui.sym("drv_tab") + RD_ROW * DRVR_SZ + DRVR_SEG, 2),
            "little"):
        sys.exit("rdcz: the RAM disk driver did not load")
    nst = m.read(ui.sym("cp_nst"), 1)[0]        # its page, then Mount
    mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7)
    ui.settle(limit=25.0)
    mo.click(x0 + CP_RX + RP_MNTX + RP_MNTW // 2, y0 + RP_B0Y + RP_BH // 2)
    ui.settle(limit=25.0)
    ui.close(cp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--adapter", default="cga", choices=sorted(MACHINE))
    ap.add_argument("--half", default="both", choices=("fat", "ram", "both"))
    a = ap.parse_args()

    src = os88build.at("build/os8088-360.img")
    want = os.path.getsize(os.path.join(os.path.dirname(__file__), "..",
                                        PLAIN))
    with tempfile.TemporaryDirectory() as tmp:
        return run(a, src, want, os.path.join(tmp, "rdcz-struck.img"))


def run(a, src, want, img):
    # A FRESH COPY EVERY RUN, in a temporary directory of its own. Two
    # things that a cached image in build/ would each get wrong: the strike
    # WRITES, and the shipped image is what every other row boots - and a
    # KEPT copy would be built out of whatever build/ held the first time,
    # so a kernel fix would read as having changed nothing
    # (tools/os88marty.py, scratch_disk: it has already cost this project a
    # wrong answer). 360KB copied twice a minute is not worth a cache.
    shutil.copyfile(src, img)
    packed, unpacked = strike(img)
    say("rdcz: %s is %d packed bytes, its own header says %d, the host file "
        "is %d; hint struck" % (DOC, packed, unpacked, want))

    with os88ui.boot(img, machine=MACHINE[a.adapter]) as ui:
        if a.half in ("fat", "both"):
            ui.open_drive("A")
            verdict("FAT", note_len(ui), want, packed)

        if a.half in ("ram", "both"):
            mount_ramdisk(ui)
            src_w = ui.open_drive("A")
            select(ui, DOC, win=src_w)
            ui.menu_pick("Edit", "Copy")
            dst_w = ui.open_drive("D")
            ui.menu_pick("Edit", "Paste")
            ui.settle(limit=40.0)
            ui.raise_window(dst_w)
            verdict("RAM", note_len(ui), want, packed)

    for f in fails:
        say("  FAIL: " + f)
    say("rdcz: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
