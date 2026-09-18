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

CHECK 13 NEEDED A SECOND A/B, because HEAPCOMPACT=0 is too blunt for it: with
no compactor at all everything past check 6 goes red, so that arm cannot tell
"the descending pass works" from "some pass works". It was therefore verified
by AMPUTATION - mem_compact's `.flip` arm patched to `jmp .undo` and nothing
else changed - and the result is the assertion in one line: checks 1..12 pass
and only 13 goes red. It is not a knob because it would be a permanent knob
for a question asked once; the amputation is two lines and reproducible from
this paragraph (docs/WRITING-TESTS.md 1).
"""
import os, sys, time, argparse
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))   # NEVER an absolute
sys.path.insert(0, HERE)                                # path: a row that names
                                                        # one checkout imports
                                                        # ANOTHER tree's library
                                                        # when it runs in a
                                                        # worktree and reports
                                                        # that tree's answer
import os88marty, os88mouse, os88sym, os88geom, dispcp
from os88fixture import need

KERNEL_SEG = 0x0060
MC_SIZE = os88geom.MC_SIZE
MEM_MAX = 32
# every window-record offset comes from os88geom, which checks itself
# against wm.inc at import - WIN_SIZE has moved 18 -> 28 over this tree

LABELS = ["worker hired", "room", "comb built", "pattern round-trip",
          "declare movable", "break the comb", "heap IS fragmented",
          "the big claim", "contents intact", "pinned block held",
          "something moved", "told once per move",
          "ceiling packed up", "dma lands page-safe",
          # SPEC.md 66.4.3 - the combined plan, the what-if and the post
          "mem_avail is claimable", "avail_max >= avail",
          "the post round-tripped", "the wake's avail is not short"]

# ...and the KEY-driven region suite, which needs a hole above heapfrag's own
# region and so needs PAINT opened before it and closed after (SPEC.md 66.4.3).
RLABELS = ["avail_max >= avail", "the post was accepted",
           "MY REGION MOVED UP", "the wake's avail is not short"]
# With the compactor removed these THREE must go the other way. Check 11 is NOT
# here: 0 moves and 0 notifications agree, so it passes honestly in both.
# Check 12 is the descending pass (SPEC.md 66.4): its ask can only be funded by
# merging the run under a ceiling block with the hole above it, so with no
# compactor at all there is nothing to merge and the claim is refused. Check 13
# is SPEC.md 66.4.2's page-constrained move, and its ask is the same shape.
OFF_MUST_FAIL = {7, 10, 12, 13}


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


def both_passes(cl, base, top):
    """The largest run BOTH compaction passes would leave - the ASCENDING
    sweep over each claim at the base the DESCENDING pass would have left it
    at (SPEC.md 66.4.3.1). Written here from the spec rather than imported
    from the kernel's arithmetic, which is the point of it.

    `cl` is (base, para, owner, dma, rloc); rloc != 0 is movable and dma's top
    bit is the top-down door.

    **PURGEABLE CLAIMS ARE MODELLED AS DROPPED, AND THEY DID NOT USED TO BE.**
    This docstring said they were not, and called the result "a LOWER bound on
    what the guest may report - one-sided by construction, and one-sided the
    safe way". That was true and it stopped being enough the day the heap grew
    a cache in this scenario: SPEC.md 25.9's machine-wide icon store claims
    `MEM_P_ICO` (owner **fb01**, 5 KB here), the kernel's what-if correctly
    counts it as droppable, and the region arm below - which asserts EQUALITY,
    on the stated premise that the asker is the only package with claims -
    read 266K against a model of 261K and called the kernel's answer memory
    the pass cannot produce. The 5 KB was the icon store, and the pass really
    can produce it.

    A record is a cache when its high byte is in [MEM_PG_MIN, MEM_PG_MAX] =
    [0xFB, 0xFE] (kernel/memory.inc): 0xFF is an ordinary claim and an
    instance slot is a small number, so both fall outside by construction.
    An ordinary claimant ranks MEM_LVL_TOP and so takes ANY cache, which is
    the level these two questions are asked at - so every one of them comes
    out of the map before the sweep. That keeps the model EXACT rather than
    merely safe, which is what lets the region arm keep asserting equality
    instead of being weakened to an inequality that would no longer catch the
    27KB-short error it was written for."""
    MC_DMA_HI = 0x8000
    MEM_PG_MIN, MEM_PG_MAX = 0xFB, 0xFE
    live = sorted(c for c in cl
                  if not (MEM_PG_MIN <= (c[2] >> 8) <= MEM_PG_MAX))
    def ceilmover(c):
        return c[4] != 0 and (c[3] & MC_DMA_HI)
    def newbase(c):
        B = top
        for d in live:
            if d[0] > c[0] and not ceilmover(d) and d[0] < B:
                B = d[0]
        return B - sum(d[1] for d in live if ceilmover(d) and c[0] <= d[0] < B)
    best, fill = 0, base
    for c in live:
        if c[4] != 0 and not (c[3] & MC_DMA_HI):
            fill += c[1]                        # a bottom-up mover packs down
            continue
        at = newbase(c) if ceilmover(c) else c[0]
        if at > fill:
            best = max(best, at - fill)
        fill = at + c[1]
    return max(best, top - fill if top > fill else 0)


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
        # PAINT FIRST, and that ordering is the whole of the region test
        # (SPEC.md 66.4.3). A region is claimed TOP-DOWN, so whichever package
        # launches first takes the ceiling: open Paint, then heapfrag lands
        # underneath it, and closing Paint later leaves a hole ABOVE
        # heapfrag's own region that only heapfrag moving can reach. That is
        # the reported scenario with a package standing in for the unmounted
        # driver - and heapfrag cannot build it for itself, being the topmost
        # claim on the heap for as long as it is the only thing running.
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "PAINT.O88")
        pw = [w for w in os88geom.windows(m, S) if "Paint" in (w.title or "")]
        os88marty.settle(m)
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
        b = m.read(seg * 16 + img, 176)
        n = u16(b, 0)
        res = b[128:128 + n]
        nmoved, nrel, nbad = u16(b, 20), u16(b, 16), u16(b, 18)
        got = u16(b, 12)
        print("pin=block %d" % u16(b, 30))
        for i in range(got):
            print("   block %d  %04x -> %04x%s"
                  % (i, u16(b, 72 + i * 2), u16(b, 56 + i * 2),
                     "   (freed)" if u16(b, 56 + i * 2) == 0 else ""))
        print("dma landed at page offset %d paragraphs" % u16(b, 114))
        print("L0=%dK S=%dK L1=%dK want=%dK  moved=%d told=%d stranger=%d"
              % (u16(b, 6), u16(b, 4), u16(b, 8), u16(b, 10),
                 nmoved, nrel, nbad))

        # --- SPEC.md 66.4.3: the guest's mem_avail against an INDEPENDENT
        # model of the same claim map. mem_cp_both is new arithmetic and its
        # dangerous error is an OVER-report - memory promised that mem_claim
        # cannot produce - so a second reader is worth more here than another
        # assertion inside the package. tools/heapwhatif.py is where the model
        # is checked against both passes actually run.
        av, avmax, avwake = u16(b, 40), u16(b, 42), u16(b, 44)
        print("mem_avail %dK, avail_max %dK, on the wake %dK (posted=%d woke=%d)"
              % (av, avmax, avwake, b[46], b[47]))
        after = claims(m, S)
        model = both_passes(after, base, top) // 64
        print("the host's model of the same map after both passes: %dK" % model)
        if avwake > model:
            print("  FAIL: the guest reported %dK where the model says %dK - "
                  "an OVER-report is memory mem_claim cannot produce" % (avwake, model))
            over = 1
        else:
            over = 0
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

        # --- THE REGION'S OWN MOVE (SPEC.md 66.4.3) -------------------------
        # Close Paint to open the hole above us, then one keystroke: heapfrag's
        # W_ONKEY asks both mem_avail questions, posts, and answers R3/R4 on
        # the wake. Programmatic, like the rest - the verdict comes back as
        # bytes in its bss and not off the screen.
        rbad = 0
        if pw:
            ui = dispcp._ui(m, mo, None, S)
            ui.close(pw[-1])

            # --- THE MAP AS IT IS AT THE ASK, and why it is read HERE ------
            # Every assertion the PACKAGE can make about avail_max is
            # one-sided: R1 can only say "not less than plain avail" and R4
            # "the wake was not short of it", because the guest has no model
            # of its own heap to check itself against. Both passed for a
            # cycle while the what-if read 27KB SHORT of what the pass then
            # produced - it pinned every claim the ASKER holds, its worker
            # being live at the ask (SPEC.md 66.4.3.2) - and neither could
            # see it.
            #
            # So the exact test is here: both_passes() is written from the
            # spec rather than from the kernel, and in THIS scenario it is
            # exactly what avail_max must answer, because the asker is the
            # only package with claims by the time Paint has closed - so the
            # worker and nest questions the model cannot see have only one
            # subject, and that subject is excused.
            os88marty.settle(m)
            askmap = claims(m, S)
            askmodel = both_passes(askmap, base, top) // 64
            m.key("KeyR")
            # POLL THE PACKAGE'S OWN COUNTER, not a sleep: the wake writes no
            # pixels, so a settle returns at once and a fixed delay is the
            # thing docs/WRITING-TESTS.md warns about - it hands a loaded box
            # less work and then fails looking like the feature.
            # ...AND RE-READ W_SEG EVERY TIME ROUND. If the feature works the
            # package's bss is not where it was: the region moved, and reading
            # the old base gives the bytes it used to occupy - which decode
            # as a base that did not move, so the row would report the exact
            # failure it is meant to catch.
            now = seg
            for _ in range(40):
                for slot in dispcp.win_list(m, S):
                    sg = u16(m.read(os88geom.winptr(m, slot, S)
                                    + os88geom.W_SEG, 2))
                    if sg:
                        now = sg
                b2 = m.read(now * 16 + img, 176)
                if u16(b2, 152) >= len(RLABELS):
                    break
                time.sleep(0.5)
            rn = u16(b2, 152)
            seg = now
            ravail, rmax, rwake = u16(b2, 156), u16(b2, 158), u16(b2, 160)
            print("region: avail %dK, avail_max %dK, wake %dK, base %04x -> %04x"
                  % (ravail, rmax, rwake, u16(b2, 154), seg))
            print("   the host's model of that map, both passes: %dK" % askmodel)
            if rmax != askmodel:
                # **PRINT THE MAP THE MODEL WAS BUILT FROM.** The map above is
                # the one AFTER the pass; `askmodel` came from `askmap`, read
                # BEFORE it, and the two are different maps by construction -
                # so a reader reasoning from what is printed cannot reproduce
                # the number that failed. Every disagreement here is an
                # arithmetic question about the PRE-pass map, and it was the
                # one thing this row did not show.
                print("   ...and the map it was built from (pre-pass), "
                      "base %04x top %04x:" % (base, top))
                for bs, pa, ow, dm, rl in sorted(askmap):
                    print("      %04x %5d KB owner %04x%s%s"
                          % (bs, pa // 64, ow, "  dma" if dm else "",
                             "  MOVABLE" if rl else "  PINNED"))
                owners = sorted({ow for _, _, ow, _, _ in askmap})
                print("   owners in it: %s  (the equality above assumes the "
                      "ASKER is the only package with claims - see the note "
                      "beside askmodel)"
                      % " ".join("%04x" % o for o in owners))
                print("  FAIL: the what-if said %dK where the model says %dK - "
                      "%s (SPEC.md 66.4.3.2)"
                      % (rmax, askmodel,
                         "SHORT, so a package would skip a post that would "
                         "have worked" if rmax < askmodel else
                         "OVER, which is memory the pass cannot produce"))
                rbad += 1
            else:
                print("   Rx avail_max IS the model          PASS")
            for i, r in enumerate(b2[148:148 + rn]):
                print("  R%d %-28s %s" % (i + 1, RLABELS[i] if i < len(RLABELS)
                                          else "?", "PASS" if r == 0 else "FAIL"))
                rbad += r != 0
            if rn < len(RLABELS):
                print("  region suite stopped after %d of %d"
                      % (rn, len(RLABELS)))
                rbad += 1
        else:
            print("  PAINT never opened - the region rows cannot be asked")
            rbad += 1

        bad = over + rbad
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
