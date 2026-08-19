#!/usr/bin/env python3
"""Compact the heap out from under a LIVE Paint canvas (SPEC.md 66.2/42).

    make && make build/heapfrag360.img && python3 tests/paintmove.py

heapfrag proves the mechanism against a package written for it. This proves it
against a real one, and Paint is the case worth proving: its canvas base is not
held in one word at all - `[pt_base]`, then ONE SEGMENT PER ROW in `pt_rowseg`
because a canvas can be bigger than a segment, then `[pt_undelta]` linking it
to the undo image. `pt_reloc` has to put all three right, and only two of them
can be checked by reading memory. The third is checked by looking.

THE SEQUENCE MATTERS, and the first attempt at it proved nothing. Claims are
first fit from the BOTTOM, so a Paint launched before anything else sits at
the floor of the arena with no hole under it and compaction correctly leaves
it exactly where it is. To make Paint's canvas actually move there has to be a
hole BELOW it, so heapfrag goes first, Paint lands above it, and heapfrag's
death opens the floor.

Three assertions, and the third is the one a memory dump cannot make:

  1. Paint's canvas claim CHANGED ADDRESS. Without it the rest is vacuous, and
     this script says so rather than passing.
  2. Its CONTENTS survived the move, hashed either side at the two addresses.
  3. Paint REPAINTS THE SAME PICTURE afterwards - which is the only thing that
     tests `pt_rowseg`, because the row table is what a repaint draws through
     and a stale one produces a plausible-looking wrong picture rather than a
     crash.
"""
import sys, os, time, hashlib, argparse, subprocess, tempfile
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp

MC_SIZE, MEM_MAX = 10, 32
PKG_HEAPFRAG, PKG_PAINT = "HEAPFRAG.O88", "PAINT.O88"


def pkg_syms(src, incs=("apps/",)):
    """A package's symbols, by re-assembling it - os88sym.py's trick.

    Paint's bss offsets are `equ os88_image_end + N` behind three macros, so
    they are neither greppable nor stable against an edit. Asking nasm is the
    only answer that cannot go stale: a wrong offset here reads a plausible
    word out of the middle of another variable and the test then reports the
    kernel as broken.
    """
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read()
                            + "\n[map symbols %s]\n" % mp)
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
    """The claims owned by a package's own segment, biggest first."""
    return sorted([c for c in cl if c[2] == seg], key=lambda c: -c[1])


def uncovered(m, S, win, prefer_title=False):
    """A pixel of `win` that no window ABOVE it covers, or None.

    "Covers" is a Z-ORDER question and not a geometry one - the Disk window
    overlaps everything it launches and spends most of a session BELOW them -
    and getting that wrong is how a click meant for one window silently lands
    on another's canvas and draws on it. That is not a hypothetical: it is
    what made this script report Paint's canvas CORRUPT after a move that had
    not happened.
    """
    zn = m.read(S("wm_zn"), 1)[0]
    zord = list(m.read(S("wm_zord"), zn))
    if win.i not in zord:
        return None
    wins = {w.i: w for w in os88geom.windows(m, S) if w.visible}
    above = [wins[i] for i in zord[zord.index(win.i) + 1:] if i in wins]
    ys = ([win.y + 1] if prefer_title
          else range(win.y + os88geom.TITLE_H, win.y + win.h - 2))
    for y in ys:
        for x in range(win.x + 4, win.x + win.w - 18):   # clear of the boxes
            if not any(o.covers(x, y) for o in above):   # and the scroll bar
                return x, y
    return None


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

    with os88marty.launch("build/os8088-360.img", apps="build/heapfrag360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)

        # SHOVE IT RIGHT, once, before anything else opens. A Disk window is
        # 320 wide at x=103 and a package opens at x=135, so the browser sits
        # squarely on top of everything it launches - which makes heapfrag's
        # close box unreachable later and every attempt to raise it a click on
        # somebody else's window. 640x200 has room for exactly this: the
        # browser on the right, the packages on the left.
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        print("disk window moved to (%d,%d)" % (wx, wy))
        disk = (wx, wy)

        def raise_disk():
            """Bring the browser to the front to click a row in it."""
            dw = [w for w in os88geom.windows(m, S) if w.i == dslot][0]
            pt = uncovered(m, S, dw)
            if pt is None:
                raise RuntimeError("the Disk window is wholly covered")
            mo.click(*pt)
            os88marty.settle(m)

        # --- heapfrag first, so it owns the floor of the arena --------------
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)
        hf_seg, hf_win = pkg_seg(m, S, "Heap")
        print("heapfrag at %04x" % (hf_seg or 0))

        # --- then Paint, which lands ABOVE it -------------------------------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_PAINT)
        time.sleep(6)
        os88marty.settle(m)
        pt_seg, pt_win = pkg_seg(m, S, "Paint")
        if pt_seg is None:
            print("FAIL: Paint never opened a window")
            return 1
        before = mine(claims(m, S), pt_seg)
        print("Paint at %04x holds %s"
              % (pt_seg, ["%04x/%dKB%s" % (b, p // 64, "*" if r else "")
                          for b, p, _, r in before]))
        if not any(r for _, _, _, r in before):
            print("FAIL: Paint declared no claim movable")
            return 1

        # --- draw, so the canvas holds something a stale row table wrecks ---
        cx0, cy0, cx1, cy1 = pt_win.content
        for i in range(4):
            y = cy0 + 24 + i * 12
            mo.drag(cx0 + 20, y, cx0 + 150, y + 8)
        os88marty.settle(m)

        # WHICH claim is the canvas is asked of Paint, not guessed from the
        # map: the undo image is claimed at the SAME size, so "the biggest
        # block Paint holds" is ambiguous between two - and picking the undo
        # image reads as CORRUPT the moment anything touches it.
        P = pkg_syms("apps/paint/paint.asm")

        def pword(name):
            return u16(m.read(pt_seg * 16 + P[name], 2))

        cbase = pword("pt_base")
        csize = [c[1] for c in before if c[0] == cbase]
        if not csize:
            print("FAIL: [pt_base] %04x is not a claim Paint holds" % cbase)
            return 1
        canvas = (cbase, csize[0])
        nrows = pword("pt_ch")
        rows0 = m.read(pt_seg * 16 + P["pt_rowseg"], nrows * 2)
        # the ROW TABLE is checked as DELTAS off the base: that is exactly what
        # pt_geom would recompute, so it must survive a move unchanged
        d0 = [(u16(rows0, i * 2) - cbase) & 0xFFFF for i in range(nrows)]
        h0 = hashlib.md5(m.read(cbase * 16, canvas[1] * 16)).hexdigest()
        shot0 = m.vram("cga")
        print("canvas [pt_base]=%04x %dKB rows=%d  md5 %s"
              % (cbase, canvas[1] // 64, nrows, h0[:12]))

        # --- close heapfrag: the floor under Paint opens up -----------------
        #
        # Its close box is UNDER Paint by now, and a click there goes to
        # Paint's canvas - which draws a dot, changes nothing about the heap,
        # and leaves the rest of the run measuring an arena that never opened
        # a hole. So find a pixel of heapfrag that nothing covers, raise it
        # with that, and only then aim at the box.
        pt = uncovered(m, S, hf_win, prefer_title=True)
        if pt is None:
            print("FAIL: heapfrag is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        mo.click(hf_win.x + 8, hf_win.y + 9)          # now its close box
        os88marty.settle(m)
        if any(w.i == hf_win.i and w.visible
               for w in os88geom.windows(m, S)):
            print("FAIL: heapfrag did not close, so no hole opened under Paint")
            return 1

        # --- and run it again, whose big claim forces the compaction --------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)

        after = mine(claims(m, S), pt_seg)
        print("Paint now holds %s"
              % ["%04x/%dKB" % (b, p // 64) for b, p, _, r in after])

        bad = 0
        newbase = pword("pt_base")
        moved = newbase != cbase
        print("  1 canvas moved        %s"
              % ("%04x -> %04x" % (cbase, newbase) if moved
                 else "NO  <-- the run proves nothing"))
        bad += not moved
        if newbase not in [c[0] for c in after]:
            print("      ...and [pt_base] names no claim Paint holds")
            bad += 1

        h1 = hashlib.md5(m.read(newbase * 16, canvas[1] * 16)).hexdigest()
        ok2 = h1 == h0
        print("  2 contents survived   %s  (%s)"
              % ("OK" if ok2 else "CORRUPT", h1[:12]))
        bad += not ok2

        rows1 = m.read(pt_seg * 16 + P["pt_rowseg"], nrows * 2)
        d1 = [(u16(rows1, i * 2) - newbase) & 0xFFFF for i in range(nrows)]
        ok3 = d0 == d1
        print("  3 row table followed  %s  (%d rows)"
              % ("OK" if ok3 else "STALE", nrows))
        bad += not ok3

        und = pword("pt_unseg")
        ok4 = (und == 0) or (((und - newbase) & 0xFFFF) == pword("pt_undelta"))
        print("  4 undo delta correct  %s" % ("OK" if ok4 else "WRONG"))
        bad += not ok4

        # --- raise Paint: the repaint is what reads pt_rowseg ---------------
        mo.click(pt_win.x + 60, pt_win.y + 9)
        os88marty.settle(m)
        shot1 = m.vram("cga")

        def band(v):
            _, _, rows = v
            return b"".join(bytes(rows[y][cx0 // 8:cx1 // 8])
                            for y in range(cy0, min(cy1, len(rows))))

        same = band(shot0) == band(shot1)
        print("  5 repaint identical   %s" % ("OK" if same else "DIFFERENT"))
        bad += not same
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
