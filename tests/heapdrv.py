#!/usr/bin/env python3
"""A DRIVER's own claims are on the heap page (SPEC.md 28.4.6).

    make && make build/sndmove360.img && python3 tests/heapdrv.py

WHAT WAS WRONG. `SOUND.DRV`'s image was on the page and the 8KB DMA ring it
claims for itself was on no row at all. The image is tagged `MEM_K_DRV`, so it
files under System by the arm that takes every `0xFB..0xFF`; the ring carries
the driver's own IMAGE SEGMENT as its owner, because `mem_own` stamps a claim
with the calling segment (SPEC.md 50.3) - a plain conventional number that is
neither a kernel tag nor an instance slot nor any `tm_ispt`. Every arm of
`tm_hmatch` refused it and there was no third answer, so the claim was drawn
nowhere.

WHY IT IS A DEFECT AND NOT A GAP, and it is the reason this row asserts
ARITHMETIC rather than a word. `tm_hsplit` walks the snapshot directly and
adds EVERY live record to `HELD` or `PURGE`, so the ring was in the caption's
total and in no column under it. A reader closing this page's own two figures
against its rows could not, on the one page in the OS whose job is explaining
the heap. A row that only looked for the string `DrvBuf` would go green on a
page that still lost the 8KB somewhere else, which is why check 2 below counts
records against ROWS and check 3 is the label on top of it rather than instead
of it.

IT WANTS A SOUND BLASTER, which in a container means `os8088_5150_sb_gla` -
MartyPC models one, so this is not on CLAUDE.md's QEMU list. `SOUND.DRV` is
the driver to test it with because its ring is CLAIMED AT ATTACH and lives for
the session, so a bare desktop has it; `ETHER.DRV`'s pool is the same shape on
a machine no emulator here can host.

FOUR CHECKS:

  1. there IS a driver claim - a live record owned by the segment `drv_tab`
     names for the sound driver, separate from the image's own record. Without
     this the three below are vacuous on a machine whose driver never
     attached, which is exactly how this page looked correct for so long;
  2. EVERY live record is on a row. The page's own `[tm_hrows]` is what the
     scroll bar measures against, and the headings and the pad rows come out
     of it exactly, so the claim rows it leaves are countable - and before
     SPEC.md 28.4.6 that count was short by the driver's claims and by
     nothing else;
  3. the ring's row reads `DrvBuf` and not `Data` - matched as PIXELS against
     the kernel's own 8x8 glyphs, because the TYPE column is half the fix and
     SPEC.md 28.4.3's rule is that a debug page must not label a claim as the
     nearest thing it recognises;
  4. the machine still draws afterwards.

WHAT IT DOES NOT COVER, said plainly: a driver's SECOND image (SPEC.md
52.11.7) is the other hop `tm_hdrv` takes and no driver in the tree loads one,
so that arm is reasoned and not measured here.
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88build                                        # noqa: E402
import os88fixture                                      # noqa: E402
import os88geom as geom                                 # noqa: E402
import os88marty as M                                   # noqa: E402
import os88pkg                                          # noqa: E402
import os88sym                                          # noqa: E402
from os88mouse import Mouse                             # noqa: E402
import dispcp                                           # noqa: E402
import heaphi                                           # noqa: E402

S = os88sym.linear
APP, NAME = "taskmgr", b"TaskMgr"
DISK = "build/sndmove360.img"
SND_ROW = 0                     # driver.inc: drv_tab row 0 is the sound driver
CHIP, MI_TASKS = (12, 8), (60, 60)      # menu-bar coordinates (see heapscrl)
# One unattended paint of this page, in CGA frames. TM_INT is 9 ticks and
# TM_SLOW is 4 of those (SPEC.md 28.6), so a refresh is 36 ticks = 1.98 s and a
# 60 Hz card draws ~119 frames in that. Derived rather than typed: the page's
# refresh rate has moved once already and a fixed count would measure nothing.
TICKS_HZ, CARD_HZ, TM_INT, TM_SLOW = 18.2, 60.0, 9, 4
SLOW_FRAMES = int(TM_INT * TM_SLOW / TICKS_HZ * CARD_HZ)
MEM_MAX, MC_SIZE = 32, geom.MC_SIZE
INST_MAX = 12
FAILED = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(cond, what, why="", got=None, want=None):
    if cond:
        say("  ok   %s" % what)
        return True
    msg = what
    if got is not None or want is not None:
        msg += "\n        want: %s\n        got:  %s" % (want, got)
    if why:
        msg += "\n        why:  " + why
    FAILED.append(msg)
    say("  FAIL %s" % what)
    return False


def report():
    if FAILED:
        say("heapdrv: %d FAILED" % len(FAILED))
        for m in FAILED:
            say("  FAIL: %s" % m)
        sys.exit(1)
    say("heapdrv: pass")
    sys.exit(0)


def u16(b, i=0):
    return int.from_bytes(b[i:i + 2], "little")


def tick(m, frames):
    """Let the guest run for `frames` and leave it running.

    NOT settle: this window repaints twice a second (SPEC.md 28.6), so the
    screen never stops and a settle waits out its whole budget before failing
    at something that is working. heapscrl carries the same note.
    """
    m.advance(frames=frames)
    m.run()


# --- the package's own state (heapscrl's shape) -------------------------------
_MAP = {}


def sym(name):
    """A taskmgr symbol's link address, out of NASM's own map."""
    if not _MAP:
        src = os.path.join(ROOT, "apps", APP, APP + ".asm")
        tmp = tempfile.mktemp(suffix=".asm")
        mp = tempfile.mktemp(suffix=".map")
        open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
        r = subprocess.run(["nasm", "-f", "bin", "-w+error",
                            "-I", os.path.join(ROOT, "apps") + os.sep,
                            "-o", os.devnull, tmp],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("heapdrv: could not map %s:\n%s" % (APP, r.stderr[:400]))
        for line in open(mp):
            p = line.split()                # "<vaddr> <raddr> <name>", HEX
            if len(p) == 3:
                try:
                    _MAP[p[2]] = int(p[0], 16)
                except ValueError:
                    pass
        for f in (tmp, mp):
            if os.path.exists(f):
                os.remove(f)
    if name not in _MAP:
        sys.exit("heapdrv: taskmgr has no symbol %s" % name)
    return _MAP[name]


def img_size():
    """The package's image size - THE FILE IS NOT THE IMAGE (SPEC.md 20.13.5)."""
    raw = open(os88build.at("build/%s.o88" % APP), "rb").read()
    return len(os88pkg.image_unwrap(raw))


class Tm:
    """The window's live state, read out of its own bss."""

    def __init__(self, m, slot, seg):
        self.m, self.slot, self.seg = m, slot, seg
        self.bss = (seg << 4) + img_size()

    def _at(self, name, off=0):
        return self.bss + sym(name) - sym("os88_image_end") + off

    def word(self, name, i=0):
        return u16(self.m.read(self._at(name, 2 * i), 2))

    def byte(self, name, i=0):
        return self.m.read(self._at(name, i), 1)[0]

    def blob(self, name, n):
        return self.m.read(self._at(name), n)


def pkg_slot(m, name):
    """(slot, segment) of the visible window whose package is called `name`."""
    t = m.read(S("wm_wins"), geom.MAX_WIN * geom.WIN_SIZE)
    for i in range(geom.MAX_WIN):
        b = i * geom.WIN_SIZE
        if u16(t, b + geom.W_FLAGS) & 3 != 3:
            continue
        seg = u16(t, b + geom.W_SEG)
        if seg and m.read((seg << 4) + 16, len(name)) == name:
            return i, seg
    return None


# --- finding a text run in the framebuffer ------------------------------------
FONT_FIRST, FONT_BYTES = 32, 760        # kernel/font.inc: glyphs 32..126


def guest_font(m):
    """The kernel's LIVE glyph table, read out of the machine (font.inc).

    NOT a host-side .f8. A plain build bakes no typeface at all - `font_init`
    asks the VGA BIOS for its ROM 8x8 set and copies glyphs 32..126 into
    `font_glyphs` - so the only thing that knows what this machine letters
    with is this machine. A test that rendered fonts/tallx.f8 here would be
    asserting against a typeface `make FONT=<name>` has to be asked for, and
    would find nothing on any ordinary build.
    """
    raw = m.read(S("font_glyphs"), FONT_BYTES)
    return {FONT_FIRST + i: list(raw[i * 8:i * 8 + 8])
            for i in range(FONT_BYTES // 8)}


def glyph_rows(text, font):
    """`text` as 8 rows of bits, one list of 0/1 per screen row."""
    out = []
    for y in range(8):
        row = []
        for ch in text:
            bits = font[ord(ch)][y]
            row += [(bits >> (7 - x)) & 1 for x in range(8)]
        out.append(row)
    return out


def find_run(rows, w, h, want):
    """(x, y) of `want` drawn at an 8-row cell, either polarity, or None.

    Either polarity because this page letters BLACK ON WHITE and vram() hands
    back the card's bits rather than the OS's ink - so an assertion written
    against one of them is an assertion about the adapter.
    """
    n = len(want[0])
    for y in range(h - 7):
        for x in range(w - n + 1):
            for ink in (1, 0):
                if all(rows[y + dy][x + dx] == (want[dy][dx] ^ (ink ^ 1))
                       for dy in range(8) for dx in range(n)):
                    return x, y
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_sb_gla")
    a = ap.parse_args()
    os.chdir(ROOT)
    os88fixture.need(DISK)

    with M.launch("build/os8088-360.img", apps=DISK,
                  machine=a.machine, boot=False) as m:
        m.run()
        M.settle(m, gate=M.desktop_up)
        M.no_saver(m)
        mo = Mouse(marty=m)

        sndseg = u16(m.read(S("drv_tab") + SND_ROW * heaphi.DRVR_SZ
                            + heaphi.DRVR_SEG, 2))
        say("heapdrv: %s - SOUND.DRV image segment %04X" % (a.machine, sndseg))

        # --- 1: there IS a driver claim to look for -------------------------
        raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
        live = [(u16(raw, i * MC_SIZE), u16(raw, i * MC_SIZE + 2),
                 u16(raw, i * MC_SIZE + 4))
                for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]
        for seg, para, own in live:
            say("    claim %04X  %5d para  owner %04X%s"
                % (seg, para, own, "  <- the driver's" if own == sndseg else ""))
        drv = [c for c in live if c[2] == sndseg]
        if not check(sndseg and drv,
                     "the sound driver is attached and holds a claim of its own",
                     "without one every check below is vacuous - which is how "
                     "this page looked correct for so long",
                     got="%d claims owned by %04X" % (len(drv), sndseg),
                     want="at least one (the 8KB DMA ring, SPEC.md 34.6.1)"):
            return report()

        # --- the Task Manager, and its heap page ----------------------------
        mo.menu(CHIP[0], CHIP[1], MI_TASKS[0], MI_TASKS[1])
        tick(m, 150)
        got = pkg_slot(m, NAME)
        if got is None:
            sys.exit("heapdrv: the Task Manager did not open")
        slot, seg = got
        tm = Tm(m, slot, seg)
        wx, wy, ww, wh = dispcp.win_rect(m, S, slot)
        say("heapdrv: Task Manager window %d at (%d,%d) %dx%d, segment %04X"
            % (slot, wx, wy, ww, wh, seg))

        cx, cy = wx + 40, wy + wh - 12       # inside the content, low and left
        for _ in range(2):
            mo.click(cx, cy)
            tick(m, 40)
        tick(m, SLOW_FRAMES)        # one unattended walk, so [tm_hrows] is
                                    # this page's and not the memory view's
        if not check(tm.byte("tm_view") == 2,
                     "two clicks reach the heap page",
                     "the click cycles 0 -> 1 -> 2"):
            return report()
        if not check(tm.word("tm_htop") == 0,
                     "the page is at the top of its list",
                     "the row arithmetic below is written for an unscrolled "
                     "walk, where [tm_hskip] is spent on nothing"):
            return report()

        # --- 2: every live record is on a row -------------------------------
        # The page's own count, less what is not a claim. tm_rows_heap builds
        # [tm_hrows] as (rows composed) - (pad rows) + [tm_cols] - 1, and the
        # rows it composed are one heading per group plus one row per MATCHED
        # claim - so the pads cancel and what is left is countable exactly.
        # ...WITH THE GUEST STOPPED. [tm_hrows], [tm_cols], the snapshot and
        # the instance table are four reads of one instant, and the machine
        # runs between them: a claim made or freed in that gap makes the pair
        # describe two different heaps and the check fail for no defect.
        m.pause()
        try:
            snap = tm.blob("tm_claims", MEM_MAX * 6)
            ist = tm.blob("tm_ist", INST_MAX)
            hrows, hcols = tm.word("tm_hrows"), tm.word("tm_cols")
        finally:
            m.run()
        shown = [(u16(snap, i * 6), u16(snap, i * 6 + 2), u16(snap, i * 6 + 4))
                 for i in range(MEM_MAX) if u16(snap, i * 6)]
        groups = 1 + sum(1 for b in ist if b)       # System, then each instance
        rows = hrows - hcols + 1 - groups
        check(rows == len(shown),
              "every live claim is on a row of the list",
              "a claim in no group is still in tm_hsplit's HELD total, so the "
              "caption and the rows under it cannot be closed (SPEC.md 28.4.6)",
              got="%d claim rows for %d live records (%d group headings, "
                  "[tm_hrows] %d, [tm_cols] %d)"
                  % (rows, len(shown), groups, hrows, hcols),
              want="one row per record")

        # --- 3: and the ring's row says DrvBuf ------------------------------
        w, h, px = m.vram()
        font = guest_font(m)
        want = glyph_rows("DrvBuf", font)
        at = find_run(px, w, h, want)
        if at is None:                  # say what IS there, so a miss names
            for probe in ("DrvImg", "Data", "System", "FATwin", "HEAP"):
                say("    probe %-8s %s"
                    % (probe, find_run(px, w, h, glyph_rows(probe, font))))
        check(at is not None,
              "the driver's claim is labelled DrvBuf, not Data",
              "both carry a bare segment as their owner and only the claim "
              "table tells them apart (SPEC.md 28.4.3)",
              got="not on screen" if at is None else "at %d,%d" % at,
              want="one 'DrvBuf' run in the framebuffer")

        # --- 4: the machine still draws -------------------------------------
        check(any(any(r) for r in px) and not all(all(r) for r in px),
              "the screen still has a picture on it")

    return report()


main()
