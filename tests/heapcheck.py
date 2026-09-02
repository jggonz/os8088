#!/usr/bin/env python3
"""Drive tests/heapfrag and read its verdict out of the guest (SPEC.md 66.8).

    make && python3 tests/heapcheck.py
    make HEAPCOMPACT=0 && \
        python3 tests/heapcheck.py --expect-off

WHY IT READS BSS RATHER THAN THE SCREEN. heapfrag draws PASS/FAIL because a
human running it wants to see something, but a screenshot is the weakest
possible instrument for this: the interesting failures are a claim that
succeeded over corrupted contents and a block that moved without its holder
being told, and both draw exactly like a pass until you read the words. So
this walks the package's own bss - the same bytes it drew from - and, more
importantly, walks the KERNEL's claim map either side of the compaction, which
is the one view from outside that can say the arena really packed.

THE A/B IS THE POINT. With HEAPCOMPACT=0 the kernel has no compactor at all
(the body, not merely the call), so checks 7 and 10 must FAIL while 8 and 9
still pass: nothing moved, so nothing was corrupted and nothing was granted.
A suite that passes against both kernels is measuring something else.

Check 11 passes in BOTH builds and that is not a hole in the A/B - it asserts
that the number of relocation calls equals the number of blocks that moved,
which with the compactor gone is 0 == 0. It is vacuously true there, which is
exactly why check 10 ("something moved at all") is a separate assertion: 11
alone cannot tell a working compactor from an absent one.
"""
import sys, time, argparse
sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")
import os88marty, os88mouse, os88sym, os88geom, dispcp
from os88fixture import need

KERNEL_SEG = 0x0060
MC_SIZE = 10
MEM_MAX = 32
# every window-record offset comes from os88geom, which checks itself
# against wm.inc at import - WIN_SIZE has moved 18 -> 28 over this tree

LABELS = ["worker hired", "room", "comb built", "pattern round-trip",
          "declare movable", "break the comb", "heap IS fragmented",
          "the big claim", "contents intact", "pinned block held",
          "something moved", "told once per move"]
# With the compactor removed these two must go the other way. Check 11 is NOT
# here: 0 moves and 0 notifications agree, so it passes honestly in both.
OFF_MUST_FAIL = {7, 10}


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def claims(m, S):
    """The kernel's claim map, as (base, para, owner, dma, rloc) tuples."""
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        r = raw[i * MC_SIZE:(i + 1) * MC_SIZE]
        if u16(r, 0):
            out.append(tuple(u16(r, k) for k in (0, 2, 4, 6, 8)))
    return sorted(out)


def largest_run(cl, base, top):
    """The biggest hole, in paragraphs - the figure mem_claim actually needs."""
    best, fill = 0, base
    for b, p, *_ in cl:
        if b > fill:
            best = max(best, b - fill)
        fill = max(fill, b + p)
    return max(best, top - fill)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--expect-off", action="store_true",
                    help="the kernel was built HEAPCOMPACT=0")
    ap.add_argument("--expect-nopark", action="store_true",
                    help="the kernel was built HEAPPARK=0 (SPEC.md 66.5)")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()

    # os88sym re-assembles the kernel to build its map and asserts the result
    # is byte-identical to build/kernel.bin, so a knob build needs its define
    # here too - without it every address below is a plausible wrong one
    defines = ()
    if a.expect_off:
        defines = ("NOCOMPACT",)
    elif a.expect_nopark:
        defines = ("NOPARK",)
    # both knobs strand the same two checks, and that is the point: heapfrag
    # runs its suite with a live worker, so a kernel that compacts but cannot
    # park reaches none of that package's claims
    expect_fail = OFF_MUST_FAIL if (a.expect_off or a.expect_nopark) else set()

    def S(name):
        return os88sym.linear(name, defines)

    need("build/heapfrag360.img")   # `all` builds nothing under tests/

    with os88marty.launch("build/os8088-360.img", apps="build/heapfrag360.img",
                          machine=a.machine, boot=False) as m:
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)

        base = u16(m.read(S("mem_base"), 2))
        top = u16(m.read(S("mem_top"), 2))
        before = claims(m, S)
        print("heap %04x..%04x  (%d KB), %d claims, largest run %d KB"
              % (base, top, (top - base) // 64, len(before),
                 largest_run(before, base, top) // 64))

        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        w = dispcp.win_list(m, S)
        print("after open_drive:", os88geom.windows(m, S))
        if not w:
            print("FAIL: the Disk window never opened")
            return 1
        wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "HEAPFRAG.O88")

        # the suite runs on the first W_PAINT and fills a heap-sized buffer
        # twice over on a 4.77MHz machine: give it real time, then settle
        time.sleep(20)
        os88marty.settle(m)

        print("after launch:  ", os88geom.windows(m, S))
        seg = None
        for slot in dispcp.win_list(m, S):
            s = u16(m.read(os88geom.winptr(m, slot, S) + os88geom.W_SEG, 2))
            if s:
                seg = s
        if seg is None:
            print("FAIL: heapfrag never opened a window")
            return 1

        # its bss, at the offsets heapfrag.asm's own table declares
        img = u16(m.read(seg * 16 + 8, 2))       # +8 = image size (the header)
        b = m.read(seg * 16 + img, 128)
        n = u16(b, 0)
        res = b[40:40 + n]
        nmoved, nrel, nbad = u16(b, 20), u16(b, 16), u16(b, 18)
        got = u16(b, 12)
        print("pin=block %d" % u16(b, 30))
        for i in range(got):
            print("   block %d  %04x -> %04x%s"
                  % (i, u16(b, 72 + i * 2), u16(b, 56 + i * 2),
                     "   (freed)" if u16(b, 56 + i * 2) == 0 else ""))
        print("L0=%dK S=%dK L1=%dK want=%dK  moved=%d told=%d stranger=%d"
              % (u16(b, 6), u16(b, 4), u16(b, 8), u16(b, 10),
                 nmoved, nrel, nbad))

        after = claims(m, S)
        print("after: %d claims, largest run %d KB  (got %d of %d blocks)"
              % (len(after), largest_run(after, base, top) // 64,
                 u16(b, 12), 8))
        fill = base
        for bs, pa, ow, dm, rl in after:
            if bs > fill:
                print("      %5d KB HOLE" % ((bs - fill) // 64))
            print("  %04x %5d KB owner %04x%s%s"
                  % (bs, pa // 64, ow, " dma" if dm else "",
                     " MOVABLE" if rl else ""))
            fill = max(fill, bs + pa)
        if top > fill:
            print("      %5d KB HOLE (to the top)" % ((top - fill) // 64))

        bad = 0
        for i, r in enumerate(res):
            want_fail = i in expect_fail
            ok = (r != 0) if want_fail else (r == 0)
            name = LABELS[i] if i < len(LABELS) else "?"
            print("  %2d %-22s %s%s" % (i + 1, name,
                                        "PASS" if r == 0 else "FAIL",
                                        "" if ok else "   <-- UNEXPECTED"))
            bad += not ok
        if n < len(LABELS):
            print("  suite stopped after %d of %d checks" % (n, len(LABELS)))
            bad += 1
        if nbad:
            print("  a relocation call named a block heapfrag does not hold")
            bad += 1
        print("VERDICT:", "OK" if not bad else "%d UNEXPECTED" % bad)
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
