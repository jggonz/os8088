#!/usr/bin/env python3
"""os88marty.pkg_read never answers from a base the compactor has RETIRED.

    make && python3 tests/pkgread.py            # green
    python3 tests/pkgread.py --break            # the idle check removed: RED
    python3 tests/pkgread.py --break readseg    # readseg ignores LiveSeg: RED

THE TORN PAIR (tools/os88marty.py, the banner over seg_snap). A compaction
copies a block FIRST - `mem_bcopy` - and only then calls `mem_reloc_call`,
whose relocation proc rewrites the records that name the block. Between the
two, a window record's W_SEG still names the OLD base while the bytes there
are already the shifted copy, so a harness that reads W_SEG and then reads
through it as a second step is handed plausible rubbish: call5's exit code
161, dirwshed's 29,888 KB arena. Three rows were fixed one at a time; this is
the gate for the one shared fix, os88marty.seg_snap / pkg_read / Pkg /
LiveSeg, which every shared helper (os88ui, os88geom.windows, dispapps,
dosmap, dispcp's listings) now reads through.

THE INSTANT IS MADE, NOT WAITED FOR. CALC opens first and takes the ceiling,
NOTEPAD opens under it, and closing CALC leaves the hole NOTEPAD's region moves
up into when a compaction is posted from outside at the trivial rank
(tests/pkgmove.py's door: [mem_cpq_lvl] + [mem_cpq]). The hole is smaller
than NOTEPAD's region, so the copy overlaps its own source and the old base's
tail really is the shifted copy. An exec breakpoint on `mem_reloc_call`
stops every relocation; the one whose BX (the old base) is NOTEPAD's W_SEG is
the torn instant itself - BX old, DX new, the bytes already at DX and the
record still saying BX. A soak that hits it once in nine runs hits it here
every run.

FOUR ASSERTIONS, all taken with the guest standing at that instruction (and
1b: the IMPLICIT readers - os88geom.windows, m.readseg on a LiveSeg - do not
step a machine the caller is holding there):

  1. THE INSTANT IS REAL: W_SEG = BX != DX, and the image at DX already
     matches the one hashed before the move. Without this the rest is vacuous
     and the row says so rather than passing.
  2. seg_snap ANSWERS THE NEW BASE AND THE RIGHT BYTES: it sees
     [mem_cp_busy] up, steps the pass out, and returns (DX, the image). With
     --break (the busy check made to read "no compactor") it returns BX - the
     red arm, which must fail on exactly this line.
  3. A BANKED LiveSeg FOLLOWS: one taken BEFORE the move, still the int BX,
     reads the image at DX afterwards, by .read and by m.readseg - what
     dosmap.instance and dispapps.pkg_seg hand out, so the ~50 rows that
     m.readseg(seg, ...) one are covered without an edit.
  4. THE SHARED HELPERS AGREE AFTERWARDS: os88geom.windows still titles the
     window "Note Pad", and pkg_read and Pkg read the same image.

What the naive pair (W_SEG, then a read through it) returns at the instant is
PRINTED, not asserted: whether the old bytes are already overwritten depends
on how far the region moved against its own length, and the gate is about the
helper, not about how unlucky a naive read can be.

And a fifth, informational line: the ROUND TRIPS a pkg_read costs on an idle
guest against the naive pair's, counted at the socket - measured 1 against 2,
the claim that the fix is free (cheaper, once the base has been seen) when
nothing is moving.
"""
import argparse
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402

# CALC first, so it takes the ceiling and its close leaves a hole SMALLER than
# NOTEPAD's region: the move overlaps its own source, and the old base's tail
# really is the shifted copy - the naive pair's rubbish, not a clean old image
FIRST, TARGET, TITLE = "B:/APPS/CALC.O88", "B:/APPS/NOTEPAD.O88", "Note Pad"
HUNT = 200                      # mem_reloc_call stops before giving up


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def count_trips(m, fn):
    """(result, socket round trips) of fn() - a batch is ONE."""
    n, inb = [0], [0]
    cmd, batch = m.cmd, m.batch

    def c(**kw):
        n[0] += not inb[0]
        return cmd(**kw)

    def b(cmds):
        n[0] += 1
        inb[0] += 1
        try:
            return batch(cmds)
        finally:
            inb[0] -= 1
    m.cmd, m.batch = c, b
    try:
        return fn(), n[0]
    finally:
        del m.cmd, m.batch


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--break", dest="brk", nargs="?", const="busy",
                    choices=("busy", "readseg"),
                    help="RED ARMS: `busy` makes seg_snap believe there is "
                    "no compactor, which removes its idle check - assertion "
                    "2 must fail; `readseg` puts Marty.readseg back to a "
                    "plain read of the int - assertion 3 must fail")
    a = ap.parse_args()
    if a.brk == "busy":
        os88marty._busy_addr = lambda m, sym=None: None
        print("--break: os88marty's [mem_cp_busy] check is REMOVED")
    elif a.brk == "readseg":
        os88marty.Marty.readseg = lambda self, seg, off, n: self.read(
            (int(seg) << 4) + off, n)
        print("--break readseg: Marty.readseg reads the banked int again")
    fails = []

    def check(ok, what):
        print("  %s %s" % ("ok " if ok else "BAD", what))
        if not ok:
            fails.append(what)

    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                     machine=a.machine, quick=True) as ui:
        m, S = ui.m, ui._S
        first = ui.path(FIRST)
        tgt = ui.path(TARGET)
        os88marty.ui_done(m)
        img = u16(os88marty.pkg_read(m, tgt, 8, 2, S))
        image = os88marty.pkg_read(m, tgt, 0, img, S)
        seg0 = os88marty.pkg_seg(m, tgt, S)
        word = os88marty._winword(m, tgt, S)
        banked = os88marty.LiveSeg(seg0, m, word)
        print("%s at %04X, image %d bytes; %s at %04X above it" % (
            TARGET, seg0, img, FIRST, os88marty.pkg_seg(m, first, S)))

        # --- 5 (informational): what a read costs when nothing moves -------
        def naive():
            s = u16(m.read(word, 2))
            return m.read((s << 4) + 0x10, 16)
        _, t_naive = count_trips(m, naive)
        _, t_pkg = count_trips(
            m, lambda: os88marty.pkg_read(m, tgt, 0x10, 16, S))
        print("  idle guest: naive pair %d round trips, pkg_read %d"
              % (t_naive, t_pkg))

        ui.close(first)
        os88marty.ui_done(m)

        # --- the torn instant -----------------------------------------------
        m.bp_exec("mem_reloc_call")
        wptr = (S("wm_wins") + tgt.i * os88geom.WIN_SIZE
                - os88geom.KERNEL_SEG * 16) & 0xFFFF
        m.pause()
        m.write(S("mem_cpq_lvl"), bytes([0xFC]))
        m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
        torn = None
        mark = m.go()
        for _ in range(HUNT):
            if m.wait_stop(limit=30.0, since=mark) is None:
                break
            r = m.regs()
            if r["bx"] == u16(m.read(word, 2)) and r["bx"] != r["dx"]:
                torn = (r["bx"], r["dx"])
                break
            mark = m.go()
        if torn is None:
            m.bp_exec()
            m.run()
            sys.exit("pkgread: no relocation of the target's region in %d "
                     "mem_reloc_call stops - the compaction did not move it, "
                     "so the row proved nothing (W_SEG %04X)"
                     % (HUNT, u16(m.read(word, 2))))
        old, new = torn
        busy = m.read(S("mem_cp_busy"), 1)[0]
        print("stopped at mem_reloc_call: region %04X -> %04X, W_SEG %04X, "
              "[mem_cp_busy] %d" % (old, new, u16(m.read(word, 2)), busy))
        at_new = m.read(new << 4, img)
        check(at_new == image and busy,
              "1 the torn instant: the image is already at the NEW base and "
              "the record still names the old one")
        stale = m.read(old << 4, img)
        diff = sum(1 for x, y in zip(stale, image) if x != y)
        print("     the naive pair here (W_SEG, then a read through it): "
              "%d of %d image bytes wrong at %04X" % (diff, img, old))

        c0 = int(m.status().get("cycles", 0))
        os88geom.windows(m, S)
        m.readseg(banked, 0, 16)
        c1 = int(m.status().get("cycles", 0))
        check(c0 == c1, "1b the IMPLICIT readers (windows, m.readseg) leave a "
              "machine the caller holds mid-pass where it was (%d cycles "
              "run)" % (c1 - c0))

        m.bp_exec()                     # the helper steps the pass out
        seg, got = os88marty.seg_snap(m, word, [(0, img)], sym=S)
        check(seg == new and got[0] == image,
              "2 seg_snap answers %04X (want %04X, never the retired %04X) "
              "and %s image" % (seg, new, old,
                                "the right" if got[0] == image else "a WRONG"))
        m.run()
        os88marty.ui_done(m)
        check(int(banked) == old and banked.read(0, img) == image
              and m.readseg(banked, 0, img) == image,
              "3 a LiveSeg banked at %04X reads the image where it is now, "
              "through .read and through m.readseg" % int(banked))
        p = os88marty.Pkg(m, tgt, sym=S)
        titles = [w.title for w in os88geom.windows(m, S) if w.i == tgt.i]
        check(titles == [TITLE] and p.read(0, img) == image
              and os88marty.pkg_read(m, tgt, 0, img, S) == image
              and p.seg == new,
              "4 afterwards: windows() titles it %r, pkg_read and Pkg read "
              "the image at %04X" % (titles, p.seg))
    if fails:
        sys.exit("pkgread: %d of 5 FAILED: %s" % (len(fails), "; ".join(fails)))
    print("pkgread: ok")


if __name__ == "__main__":
    main()
