#!/usr/bin/env python3
"""A C package declares a claim movable, and the compactor moves it.

    make chello && make build/cmemmove360.img && python3 tests/cmemmove.py

UNTIL os88_mem_movable() EXISTED, `os88_mem_claim` was the whole of the C
SDK's heap surface (docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 3). Every claim a
C package made was therefore pinned BY CONSTRUCTION and no author could change
it - C64's 64KB of RAM, RunCPM's 64KB Z80 space, Weave's bundle and canvas,
Loom's project buffers - each one a wall in the middle of the arena for as
long as the program ran.

THE ROUND TRIP IS WHAT THIS PROVES, and it is longer than any other callback's:
C calls a thunk, the thunk names `cc_onmove` to the kernel, the kernel calls
`cc_onmove` from inside `mem_reloc_call` in the middle of a compaction, and
`cc_onmove` calls back into C with two arguments pushed cdecl. Until something
built out of that reports a move that actually happened, the trampoline's
argument order is inference from emitted assembly - which is the same sentence
tests/chello's own header uses about the crosshair, and the reason this is
CHELLO rather than an application.

paintmove.py's recipe: heapfrag owns the floor, chello lands above it,
heapfrag's death opens the floor, and heapfrag re-run makes the claim that
forces the pass.

FOUR ASSERTIONS:

  1. THE CLAIM WAS DECLARED - MC_RLOC is non-zero in the kernel's own table.
     Read from OUTSIDE, because `mem_movable`'s fence is "yours, or not at
     all" and a refusal is a flag the package discards (SPEC.md 66.5.6.2).
  2. IT MOVED, and `[ch_seg]` followed it - the C handler ran and assigned.
  3. `[ch_was]` IS THE OLD BASE and `[ch_seg]` the new one, which is the pair
     that catches a trampoline pushing `now` before `was`: a swapped pair
     counts the same moves and reads just as plausibly.
  4. THE CONTENTS SURVIVED, hashed at both addresses. The pattern is
     w * 0x0101 per word, so a copy that shifted the block's contents inside
     itself fails where a constant fill would pass.
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
DISK = "build/cmemmove360.img"
PKG_HEAPFRAG, PKG_CHELLO = "HEAPFRAG.O88", "CHELLO.O88"


def pkg_syms(src, incs=("apps/", "build/")):
    """CHELLO's symbols, by re-assembling its shim.

    A C package's statics are labels in build/<name>.gen.asm, which is
    generated - so there is nothing to grep and nothing that stays put across
    an edit. Asking nasm is the only answer that cannot go stale.
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


def uncovered(m, S, win, prefer_title=False):
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
    CH = pkg_syms("tests/chello/chello.asm")

    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        dslot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, dslot)
        mo.drag(wx + 60, wy + 9, wx + 60 + 215, wy + 9)
        os88marty.settle(m)
        disk = dispcp.win_rect(m, S, dslot)[:2]
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
        print("heapfrag at %04x" % (hf_seg or 0))

        # --- then chello, which lands ABOVE it ------------------------------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_CHELLO)
        time.sleep(6)
        os88marty.settle(m)
        ch_seg, ch_win = pkg_seg(m, S, "C Hello")
        if ch_seg is None:
            print("FAIL: CHELLO never opened a window")
            return 1

        def cword(name):
            return u16(m.read(ch_seg * 16 + CH[name], 2))

        base0 = cword("_ch_seg")
        held = [c for c in claims(m, S) if c[0] == base0 and c[2] == ch_seg]
        print("chello at %04x, its claim %04x" % (ch_seg, base0))
        if base0 == 0 or not held:
            print("FAIL: [ch_seg] is %04x, which is no claim CHELLO holds - "
                  "os88_mem_claim was refused, or os88_mem_movable was and "
                  "the package gave the block back" % base0)
            return 1
        size = held[0][1]
        h0 = hashlib.md5(m.read(base0 * 16, size * 16)).hexdigest()

        bad = 0
        rloc = held[0][3]
        print("  1 declared movable    %s"
              % ("MC_RLOC=%04x" % rloc if rloc else
                 "NO  <-- os88_mem_movable was refused and the package "
                 "could not tell"))
        bad += not rloc

        # --- close heapfrag: the floor under chello opens up ----------------
        pt = uncovered(m, S, hf_win, prefer_title=True)
        if pt is None:
            print("FAIL: heapfrag is wholly covered; cannot raise it")
            return 1
        mo.click(*pt)
        os88marty.settle(m)
        mo.click(hf_win.x + 8, hf_win.y + 9)
        os88marty.settle(m)
        if any(w.i == hf_win.i and w.visible
               for w in os88geom.windows(m, S)):
            print("FAIL: heapfrag did not close, so no hole opened")
            return 1

        # --- and run it again, whose big claim forces the compaction --------
        raise_disk()
        dispcp.open_named(m, mo, S, os88marty.settle, *disk, name=PKG_HEAPFRAG)
        time.sleep(22)
        os88marty.settle(m)

        base1, moves, was = cword("_ch_seg"), cword("_ch_moves"), cword("_ch_was")
        print("  claim %04x -> %04x, os88_onmove called %d time(s), was=%04x"
              % (base0, base1, moves, was))

        moved = base1 != base0
        print("  2 moved, and C followed  %s"
              % ("%04x -> %04x after %d call(s)" % (base0, base1, moves)
                 if moved and moves else
                 "NO  <-- the run proves nothing"))
        bad += not (moved and moves)

        ok3 = was == base0 and base1 != was
        print("  3 (was, now) not swapped %s"
              % ("OK" if ok3 else
                 "WRONG: was=%04x, old base was %04x" % (was, base0)))
        bad += not ok3

        if moved:
            h1 = hashlib.md5(m.read(base1 * 16, size * 16)).hexdigest()
            ok4 = h1 == h0
            print("  4 contents survived      %s  (%s)"
                  % ("OK" if ok4 else "CORRUPT", h1[:12]))
            bad += not ok4
        print("VERDICT:", "OK" if not bad else "%d PROBLEM(S)" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
