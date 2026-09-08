#!/usr/bin/env python3
"""Compact the heap out from under a LIVE Sheet (SPEC.md 66.2, 24).

    make && make build/sheetmove360.img && python3 tests/sheetmove.py

SHEET WAS THE LARGEST UNDECLARED HOLDER IN THE TREE
(docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 2): six unconditional claims taken at
its entry proc, ~99KB, every one pinned for the session. SPEC.md 66.5.10.2's
closing line - "the arena below the top now has no barrier in it at all - every
claim there is movable or purgeable" - was true of the configuration it was
measured on and false the moment a sheet opened.

Five of the six are declared now. The sixth, `sh_stgseg`, is the ES:BX of all
seven of this package's OSAPI_FILE_READ/WRITE calls (SPEC.md 66.9 reason 4) and
stays pinned; this row asserts that too, because "we meant to leave that one"
and "we forgot that one" are the same picture.

paintmove.py's recipe, and it is the recipe rather than a preference: claims
are first fit from the bottom, so a Sheet launched into an empty arena sits on
the floor with no hole under it and compaction correctly leaves it exactly
where it is. heapfrag goes first, Sheet lands above it, heapfrag's death opens
the floor, and heapfrag re-run makes the claim that forces the pass.

FOUR ASSERTIONS:

  1. SHEET'S CELL CLAIM MOVED. Without it the rest is vacuous.
  2. ITS CONTENTS SURVIVED, hashed either side at the two addresses.
  3. EVERY ONE OF THE FIVE WORDS FOLLOWED, and `sh_stgseg` did NOT move -
     read out of the package's own bss at offsets nasm computed, not guessed.
     This is the assertion `sh_reloc` is a table for: SPEC.md 66.1 is the
     record of a word-poke design that failed because a claim had a SECOND
     naming word, and Sheet has three of those in os88chart.inc's borrowed
     copies.
  4. SHEET REPAINTS THE SAME PICTURE. The grid is drawn out of the cell store
     through `sh_cellseg`, so a stale word here draws a plausible wrong sheet
     rather than crashing - which is the whole reason assertion 2 is not
     enough.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402
import dispcp                                           # noqa: E402

MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX
PKG_HEAPFRAG, PKG_SHEET = "HEAPFRAG.O88", "SHEET.O88"
DISK = "build/sheetmove360.img"

# the five that are declared, and the one that is deliberately not
MOVABLE = ["sh_cellseg", "sh_txtseg", "sh_bordseg", "sh_noteseg",
           "sh_chartseg"]
PINNED = "sh_stgseg"


def pkg_syms(src, incs=("apps/",)):
    """A package's symbols, by re-assembling it - paintmove.py's trick.

    Sheet's bss offsets are an `equ` chain off `os88_image_end`, so they are
    neither greppable nor stable against an edit. A wrong offset here reads a
    plausible word out of the middle of another variable and the row then
    reports the kernel as broken.
    """
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def claims(m, S):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return [tuple(u16(raw, i * MC_SIZE + k) for k in (0, 2, 4, 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)]


def mine(cl, seg):
    return sorted([c for c in cl if c[2] == seg], key=lambda c: -c[1])


def uncovered(m, S, win, prefer_title=False):
    """A pixel of `win` that no window ABOVE it covers, or None."""
    zn = m.read(S("wm_zn"), 1)[0]
    zord = list(m.read(S("wm_zord"), zn))
    if win.i not in zord:
        return None
    wins = {w.i: w for w in os88geom.windows(m, S) if w.visible}
    above = [wins[i] for i in zord[zord.index(win.i) + 1:] if i in wins]
    ys = ([win.y + 1] if prefer_title
          else range(win.y + os88geom.TITLE_H, win.y + win.h - 2))
    for y in ys:
        for x in range(win.x + 4, win.x + win.w - 18):
            if not any(o.covers(x, y) for o in above):
                return x, y
    return None


def park(mo, m):
    """Put the POINTER somewhere neither capture is looking.

    The arrow is drawn into the framebuffer, so it is part of any picture
    compared - and the two captures below are taken after different gestures,
    so it is somewhere different in each. This row failed on exactly that and
    the failure was six pixels wide: `differs at screen x 115..120, y 63..64`,
    which is the tail of the arrow the Sheet drag left behind, one row inside
    the content rectangle. Bottom-left of the desktop is clear of every window
    this row opens.
    """
    mo.to(5, 195)
    os88marty.settle(m)


def pkg_seg(m, S, title):
    for w in os88geom.windows(m, S):
        if w.title.startswith(title):
            raw = m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2)
            return u16(raw), w
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    S = os88sym.linear
    os88fixture.need(DISK)

    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)   # out of the way
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        disk = (wx, wy)
        print("disk window moved to (%d,%d)" % disk)

        def raise_disk():
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        # --- heapfrag first, so it owns the floor of the arena --------------
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)
        hf_seg, hf_win = pkg_seg(m, S, "Heap")
        # SHOVE IT TO THE LEFT EDGE, before Sheet opens. Sheet's window is
        # 562 wide at x=55 on a 640x200 screen and heapfrag's is 250 at 135:
        # it is covered ENTIRELY, close box and all, and every later attempt
        # to raise it is a click on Sheet's grid instead - which edits a cell,
        # changes nothing about the heap, and leaves the rest of the run
        # measuring an arena that never opened a hole. At x=0 its left 55
        # columns stay clear of Sheet whatever Sheet does.
        mo.drag(hf_win.x + 60, hf_win.y + 9, 60, hf_win.y + 9)
        os88marty.settle(m)
        hf_seg, hf_win = pkg_seg(m, S, "Heap")
        print("heapfrag at %04x rect %s"
              % (hf_seg or 0, (hf_win.x, hf_win.y, hf_win.w, hf_win.h)))

        # --- then Sheet, which lands ABOVE it -------------------------------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_SHEET)
        time.sleep(8)
        os88marty.settle(m)
        sh_seg, sh_win = pkg_seg(m, S, "Sheet")
        if sh_seg is None:
            print("FAIL: Sheet never opened a window")
            return 1
        # AND SHOVE SHEET DOWN. Its window is 562x155 on a 640x200 screen, so
        # in x it can cover everything except a strip narrower than either
        # other window - there is no arrangement in x alone where BOTH the
        # Disk window and heapfrag keep a pixel of their own. In y there is
        # room for exactly one step, and one is enough: Sheet at y=45 leaves
        # rows 20..44 to the two windows behind it, one at each side.
        mo.drag(sh_win.x + 60, sh_win.y + 9, sh_win.x + 60, sh_win.y + 9 + 25)
        os88marty.settle(m)
        sh_seg, sh_win = pkg_seg(m, S, "Sheet")
        print("sheet rect %s" % ((sh_win.x, sh_win.y, sh_win.w, sh_win.h),))
        before = mine(claims(m, S), sh_seg)
        print("Sheet at %04x holds %s"
              % (sh_seg, ["%04x/%dKB%s" % (b, p // 64, "*" if r else "")
                          for b, p, _, r in before]))
        ndecl = sum(1 for _, _, _, r in before if r)
        if ndecl != len(MOVABLE):
            print("FAIL: Sheet declared %d claims movable and this row expects "
                  "%d - a declaration mem_movable REFUSED writes no MC_RLOC "
                  "and looks identical from inside the package (SPEC.md "
                  "66.5.6.2)" % (ndecl, len(MOVABLE)))
            return 1

        # --- put something in the store a stale segment would lose ----------
        cx0, cy0, cx1, cy1 = sh_win.content
        m.type_text("12345")
        os88marty.settle(m)
        m.key("Enter")
        os88marty.settle(m)

        SH = pkg_syms("apps/sheet/sheet.asm")

        def sword(name):
            return u16(m.read(sh_seg * 16 + SH[name], 2))

        w0 = dict((n, sword(n)) for n in MOVABLE + [PINNED])
        sizes = dict((c[0], c[1]) for c in before)
        for n in MOVABLE + [PINNED]:
            if w0[n] not in sizes:
                print("FAIL: [%s] = %04x is not a claim Sheet holds"
                      % (n, w0[n]))
                return 1
        h0 = dict((n, hashlib.md5(m.read(w0[n] * 16, sizes[w0[n]] * 16))
                   .hexdigest()) for n in MOVABLE)
        print("  " + "  ".join("%s=%04x" % (n[3:], w0[n])
                               for n in MOVABLE + [PINNED]))
        park(mo, m)
        shot0 = m.vram("cga")

        # --- close heapfrag: the floor under Sheet opens up -----------------
        pt = uncovered(m, S, hf_win, prefer_title=True)
        if pt is None:
            print("FAIL: heapfrag is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        mo.click(hf_win.x + 8, hf_win.y + 9)          # its close box
        os88marty.settle(m)
        if any(w.i == hf_win.i and w.visible
               for w in os88geom.windows(m, S)):
            print("FAIL: heapfrag did not close, so no hole opened under Sheet")
            return 1

        # --- and run it again, whose big claim forces the compaction --------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)

        after = mine(claims(m, S), sh_seg)
        live = set(c[0] for c in after)
        w1 = dict((n, sword(n)) for n in MOVABLE + [PINNED])
        print("Sheet now holds %s"
              % ["%04x/%dKB" % (b, p // 64) for b, p, _, _ in after])
        print("  " + "  ".join("%s=%04x" % (n[3:], w1[n])
                               for n in MOVABLE + [PINNED]))

        bad = 0
        moved = [n for n in MOVABLE if w1[n] != w0[n]]
        print("  1 a declared claim moved   %s"
              % (", ".join(n for n in moved) if moved
                 else "NO  <-- the run proves nothing"))
        bad += not moved

        for n in moved:
            h1 = hashlib.md5(m.read(w1[n] * 16, sizes[w0[n]] * 16)).hexdigest()
            if h1 != h0[n]:
                print("  2 %-22s CORRUPT (%s -> %s)"
                      % (n, h0[n][:12], h1[:12]))
                bad += 1
        if not bad:
            print("  2 contents survived        OK  (%d block(s) hashed)"
                  % len(moved))

        stale = [n for n in MOVABLE if w1[n] not in live]
        print("  3 every word names a claim %s"
              % ("OK" if not stale else "STALE: " + ", ".join(stale)))
        bad += bool(stale)
        pin_ok = w1[PINNED] == w0[PINNED]
        print("  3b %s stayed put         %s"
              % (PINNED, "OK" if pin_ok else "MOVED <-- it is a file target"))
        bad += not pin_ok

        # --- raise Sheet: the repaint is what reads sh_cellseg --------------
        # ON AN UNCOVERED PIECE OF ITS TITLE BAR, and both halves of that
        # sentence cost a run. By now heapfrag is front and left and the Disk
        # window front and right, so a click at the conventional (x+60, y+9)
        # lands on heapfrag and raises THAT - and the "repaint" compared below
        # is heapfrag's window sitting on top of Sheet's grid. And an
        # uncovered pixel of the CONTENT is worse: raising a Sheet by clicking
        # its grid moves the cell cursor, so the picture legitimately differs.
        # Both failed looking exactly like a stale sh_cellseg.
        pt = uncovered(m, S, sh_win, prefer_title=True)
        if pt is None:
            print("FAIL: Sheet's title bar is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        park(mo, m)
        shot1 = m.vram("cga")

        def band(v):
            """Sheet's content rectangle, ONE BYTE PER PIXEL.

            m.vram() answers a row per scanline and a BYTE per pixel - the
            colour index, not packed bits - so the slice is x directly. The
            `x // 8` spelling that reads like a byte offset is what several
            older rows here use, and against a 640-wide frame it silently
            compares the leftmost 70 PIXELS of each row instead of the window:
            this row failed that way with 129 of 135 rows differing, and what
            it was comparing was the desktop where heapfrag's window used to
            be.
            """
            _, _, rows = v
            return b"".join(bytes(rows[y][cx0:cx1])
                            for y in range(cy0, min(cy1, len(rows))))

        b0, b1 = band(shot0), band(shot1)
        same = b0 == b1
        if not same:
            zn = m.read(S("wm_zn"), 1)[0]
            print("      z-order %s (top last), sheet is slot %d"
                  % (list(m.read(S("wm_zord"), zn)), sh_win.i))
            w = cx1 - cx0
            rows = [y for y in range(len(b0) // w)
                    if b0[y * w:(y + 1) * w] != b1[y * w:(y + 1) * w]]
            xs = set()
            for y in rows:
                r0, r1 = b0[y * w:(y + 1) * w], b1[y * w:(y + 1) * w]
                xs |= set(i for i in range(w) if r0[i] != r1[i])
            print("      differs at screen x %d..%d, y %d..%d (%d of %d rows)"
                  % (cx0 + min(xs), cx0 + max(xs), cy0 + rows[0],
                     cy0 + rows[-1], len(rows), len(b0) // w))
        print("  4 repaint identical        %s"
              % ("OK" if same else "DIFFERENT"))
        bad += not same
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
