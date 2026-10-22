#!/usr/bin/env python3
"""A DRIVER's own claims are on the memory page (SPEC.md 28.4.6, 28.4.10).

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
the driver to test it with; `ETHER.DRV`'s pool is the same shape on a machine
no emulator here can host. Its ring used to be CLAIMED AT ATTACH, so a bare
desktop had one; since SPEC.md 34.5.2 an idle card holds nothing but its
image, so `SBTEST.O88` opens a stream first and leaves it open - a linear
stream is double-buffered, so the driver then owns two claims, the staging
pool and the 8KB buffer.

FOUR CHECKS:

  1. there IS a driver claim - a live record owned by the segment `drv_tab`
     names for the sound driver, separate from the image's own record. Without
     this the three below are vacuous on a machine whose driver never
     attached, which is exactly how this page looked correct for so long;
  2. EVERY live record is in a group, and the driver's claims are in ITS
     group. Since the heap page merged into the memory page (SPEC.md
     28.4.10) the page classifies its snapshot once per paint, one group id
     per record in `[tm_cgid]`, and a record with none is on no row - which
     is what the ring was before SPEC.md 28.4.6. Read with the SYSTEM view
     up and every group open (`+ALL`), where a driver is a group of its own
     named by its class;
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
TMG_N, TMG_DRV, DRVC_SOUND = 32, 16, 1  # taskmgr.asm's groups, os88api.inc
TM_PEN, TMM_HDR_Y = 8, 66               # apps/taskmgr/taskmgr.asm
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


def u32(b, i=0):
    return int.from_bytes(b[i:i + 4], "little")


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
                            "-o", tmp + ".bin", tmp],
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
        for f in (tmp, mp, tmp + ".bin"):
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
    asks the VGA BIOS for its ROM 8x8 set and points `[font_seg]:[font_base]`
    at glyph 32 of it (SPEC.md 6) - so the only thing that knows what this machine letters
    with is this machine. A test that rendered fonts/tallx.f8 here would be
    asserting against a typeface `make FONT=<name>` has to be asked for, and
    would find nothing on any ordinary build.
    """
    seg = int.from_bytes(m.read(S("font_seg"), 2), "little")
    off = int.from_bytes(m.read(S("font_base"), 2), "little")
    raw = m.read(seg * 16 + off, FONT_BYTES)
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

        # --- a stream, so the driver HAS claims (SPEC.md 34.5.2) ------------
        # An idle card holds nothing but its image since the double buffer
        # became per-stream, so SBTEST opens its 2 s tone and leaves it open:
        # a linear stream is double-buffered, and the driver then owns the
        # staging pool AND the 8KB buffer - two claims for the page to file.
        dispcp.open_drive(m, mo, S, M.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        dx0, dy0, _, _ = dispcp.win_rect(m, S, dslot)
        before = set(w.i for w in geom.windows(m, S) if w.visible)
        dispcp.open_named(m, mo, S, M.settle, dx0, dy0, name="SBTEST.O88")
        tick(m, 8 * 60)
        M.settle(m)
        sbw = [w for w in geom.windows(m, S)
               if w.visible and w.i not in before]
        if not sbw:
            sys.exit("heapdrv: SBTEST never opened a window")
        sbw = sbw[0]
        mo.click(sbw.x + sbw.w // 2, sbw.y + sbw.h - 20)    # open the stream
        tick(m, 4 * 60)
        M.settle(m)

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
                     want="at least one (the staging pool and the double "
                          "buffer of SBTEST's open stream, SPEC.md 34.5.2)"):
            return report()

        # --- the Task Manager, and its memory page --------------------------
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

        # The memory page (the heap page merged into it, SPEC.md 28.4.10):
        # one click cycles 0 -> 1. Then its SYSTEM view, where each driver is
        # a group of its own, and +ALL, so the driver's group is open and its
        # claims are rows - both switches are on the header, off the page's
        # own origin and rise.
        cx, cy = wx + 40, wy + 36            # the captions, above the list
        for _ in range(2):
            if tm.byte("tm_view") == 1:
                break
            mo.click(cx, cy)
            tick(m, 40)
        if not check(tm.byte("tm_view") == 1,
                     "a click reaches the memory page",
                     "the click cycles 0 -> 1 -> 0"):
            return report()
        hy = tm.word("tm_cy") + TMM_HDR_Y - tm.word("tm_xoff") + 4
        if tm.byte("tm_mview") != 1:
            mo.click(tm.word("tm_cx") + TM_PEN + 12, hy)        # SYSTEM
            tick(m, 40)
        if u32(tm.blob("tm_exp", 4)) != 0xFFFFFFFF:
            mo.click(tm.word("tm_cx") + TM_PEN + 22 * 8 + 4, hy)  # +ALL
            tick(m, 40)
        tick(m, SLOW_FRAMES)        # one unattended walk of the open list
        if not check(tm.byte("tm_mview") == 1
                     and u32(tm.blob("tm_exp", 4)) == 0xFFFFFFFF,
                     "the header's switches give the SYSTEM view, every group "
                     "open",
                     got="view %d, groups %08X" % (tm.byte("tm_mview"),
                                                   u32(tm.blob("tm_exp", 4)))):
            return report()

        # --- 2: every live record is in a group, the driver's in ITS group --
        # The page classifies its snapshot once per paint (tm_classify): one
        # group id per record in [tm_cgid], FFh for a free one. A live record
        # with no group is in no row and in no total, which is the defect
        # SPEC.md 28.4.6 fixed; and the ring must be the SOUND driver's group
        # (TMG_DRV + DRVC_SOUND), which is what names it on the page.
        # ...WITH THE GUEST STOPPED, so the snapshot and the ids are one
        # paint's: the machine runs between reads.
        m.pause()
        try:
            snap = tm.blob("tm_claims", MEM_MAX * 6)
            gid = tm.blob("tm_cgid", MEM_MAX)
        finally:
            m.run()
        recs = [(i, u16(snap, i * 6), u16(snap, i * 6 + 4))
                for i in range(MEM_MAX) if u16(snap, i * 6)]
        lost = [r for r in recs if gid[r[0]] >= TMG_N]
        check(not lost,
              "every live claim is in a group of the list",
              "a claim in no group is in no row and no total (SPEC.md 28.4.6)",
              got="%d of %d records with no group: %s"
                  % (len(lost), len(recs),
                     " ".join("%04X/%04X" % (r[1], r[2]) for r in lost)),
              want="none")
        mine = [r for r in recs if r[2] == sndseg]
        check(mine and all(gid[r[0]] == TMG_DRV + DRVC_SOUND for r in mine),
              "the driver's claims are in the SOUND.DRV group",
              "a driver is a group of its own, named by its class "
              "(SPEC.md 28.4.10)",
              got=" ".join("%04X->%d" % (r[1], gid[r[0]]) for r in mine),
              want="every one %d" % (TMG_DRV + DRVC_SOUND))

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
