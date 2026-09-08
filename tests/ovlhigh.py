#!/usr/bin/env python3
"""A C package's OVERLAY is claimed from the top and declares itself movable.

    make cworddisk && python3 tests/ovlhigh.py

WHAT IT IS ABOUT (docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 1, SPEC.md 50.3.2).
`CWORD.OVL` is 18,565 bytes - bigger than every kernel module in the tree put
together - and it took the LOW claim door for as long as overlays have
existed, so it sat pinned in the middle of the data arena for the program's
whole life. SPEC.md 50.3's own words for what that does are "one long-lived
data claim landing mid-heap permanently splits the space a package can be
loaded into", and 50.3.2's rule sends a claim whose base IS A CS to the top.
This is that rule applied one layer out from the two driver images 50.3.2.1
fixed.

THREE ASSERTIONS, and each is a different half of the change:

  1. **MC_HI is 1** - it came in through the top-down door, which is what the
     descending compaction pass reads (SPEC.md 66.4.1).
  2. **It is above every bottom-up claim, and packed against the ceiling with
     no hole above it.** MC_HI alone would pass on a kernel that stamped the
     byte and ignored it; these two are the placement said as facts about the
     arena. Note that the package's own REGION is a top-down claim as well
     (docs/plans/HEAP-UNPIN-PLAN.md 2.1), so the overlay lands directly BELOW
     it - abutting the CS-based group at the top rather than above the whole
     of it, which is what "highest fit" means when the top is occupied.
  3. **MC_RLOC is not 0** - the claim declared itself movable and the kernel
     took the declaration. This is the assertion SPEC.md 66.5.6.2 exists
     because nobody made: `mem_movable`'s fence is "yours, or not at all", so
     a declaration with the wrong owner returns CF=1 and writes nothing, and
     the caller that discards the flag cannot tell. Reading MC_RLOC from
     OUTSIDE the guest is the only way to know.

WHY IT PRESSES A KEY. The overlay is loaded ON DEMAND (SPEC.md 73.14) - it is
not in memory when CWORD opens - so a row that launched the package and looked
at the claim map would find no claim at all and pass vacuously. F5 is
`CW_K_F5` -> `CWA_GOTO` -> `ovl_dlg_open`, which is an overlay function, so
pressing it is what makes `cc_ovneed` claim, read and bind. It is a KEY and
not a menu pick because CWORD draws its own menu bar inside its window
(cwmenu.c) rather than through OSAPI_MENU_*, so the kernel's bar - which
os88ui.menu_pick resolves against - carries only Apple and the app's name.

VERIFIED TO FAIL. With `OSAPI_MEM_CLAIM_HI` put back to `OSAPI_MEM_CLAIM` in
apps/cc/crt0.asm, assertions 1 and 2 both go red; with the three lines that
call `OSAPI_MEM_MOVABLE` removed, assertion 3 does.

It is CWORD because CWORD is the C package with an overlay that fits a 360KB
floppy beside the system disk. The code under test is `apps/cc/crt0.asm`,
which every C package shares, so the assertion is about all of them.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "unit"))
import os88fixture                                          # noqa: E402
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402
import os88ui                                               # noqa: E402
from harness import check, done                             # noqa: E402

need = os88fixture.need

DISK = "build/cword360.img"
MC_SIZE = os88geom.MC_SIZE
MEM_MAX = os88geom.MEM_MAX


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def claims(m, S):
    """(base, paragraphs, owner, dma, rloc, hi) for every live claim."""
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        r = raw[i * MC_SIZE:(i + 1) * MC_SIZE]
        if u16(r, 0):
            # MC_HI IS NOT A BYTE OF ITS OWN. It was `r[10]`, one past the end
            # of a 10-byte record (MC_SIZE), and reading it raised IndexError
            # before a single assertion ran. On this branch the top-down door
            # is MC_DMA's TOP BIT - tools/os88geom.py says so in as many words
            # - so the head is the low fifteen bits and `hi` is bit 15.
            dma = u16(r, 6)
            out.append((u16(r, 0), u16(r, 2), u16(r, 4),
                        dma & os88geom.MC_DMA_HEAD, u16(r, 8),
                        1 if dma & os88geom.MC_DMA_HI else 0))
    return sorted(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine")          # default: os88ui.boot's own,
                                          # which resolves an IBM romset
                                          # name to its GLaBIOS twin
    a = ap.parse_args()
    need(DISK)
    S = os88sym.linear

    kw = {"machine": a.machine} if a.machine else {}
    with os88ui.boot("build/os8088-360.img", apps=DISK, **kw) as ui:
        m = ui.m
        w = ui.path("B:/CWORD.O88")
        region = u16(m.read(os88geom.winptr(m, w, S) + os88geom.W_SEG, 2))
        before = claims(m, S)
        if not check(region != 0, "CWORD has a region",
                     "nothing below can be located without it",
                     got=region, want="a segment"):
            done("ovlhigh")

        m.key("F5")                             # CW_K_F5 -> CWA_GOTO ->
        os88marty.settle(m)                     # ovl_dlg_open, an overlay
                                                # function
        after = claims(m, S)

        # the overlay is the claim that appeared, owned by the package's own
        # segment (SPEC.md 50.2) - a package's data claims carry the segment
        # it runs in
        seen = set(c[0] for c in before)
        new = [c for c in after if c[0] not in seen and c[2] == region]
        print("region %04x, %d claims -> %d" % (region, len(before), len(after)))
        for base, para, own, dma, rloc, hi in after:
            print("  %04x %6d KB owner %04x%s%s%s"
                  % (base, para // 64, own, " dma" if dma else "",
                     " MOVABLE" if rloc else "", "  HI" if hi else ""))
        if not check(len(new) == 1,
                     "picking Edit -> Search... claimed the overlay",
                     "the menu pick did not reach cc_ovneed, so the three "
                     "assertions below would all be about nothing",
                     got=[hex(c[0]) for c in new], want="exactly one claim"):
            done("ovlhigh")

        base, para, own, dma, rloc, hi = new[0]
        check(hi == 1, "the overlay came in through the TOP-DOWN door",
              "SPEC.md 50.3.2: a claim whose base IS a CS goes high, and "
              "SPEC.md 66.4.1's descending pass reads this byte to know which "
              "way to pack it",
              got=hi, want=1)
        # ...and the byte only says which door was ASKED for. These two say
        # the allocator answered from that end, which is the whole point: an
        # 18KB pinned image in the middle of the arena is the barrier.
        lowtop = max([c[0] + c[1] for c in after if not c[5]] or [0])
        check(base >= lowtop,
              "...and it landed above EVERY bottom-up claim",
              "SPEC.md 50.3's own words for the defect: 'one long-lived data "
              "claim landing mid-heap permanently splits the space a package "
              "can be loaded into'. This is that sentence as an assertion",
              got="overlay at %04x, the low arena ends at %04x"
                  % (base, lowtop),
              want="above the low arena")
        top = u16(m.read(S("mem_top"), 2))
        above = [c[0] for c in after if c[0] >= base + para]
        check(base + para == (min(above) if above else top),
              "...packed against the ceiling with no hole above it",
              "mem_claim_hi takes the HIGHEST fit, so a gap between this "
              "claim and whatever is above it would mean it went high and "
              "still landed somewhere that splits the free run",
              got="%04x..%04x, next %04x"
                  % (base, base + para, min(above) if above else top),
              want="adjacent")
        check(rloc != 0, "...and the kernel TOOK the movable declaration",
              "mem_movable's fence is 'yours, or not at all' and a refusal is "
              "a CF the caller discards (SPEC.md 66.5.6.2). MC_RLOC read from "
              "outside is the only witness",
              got=rloc, want="a non-zero relocation handle")
    done("ovlhigh")


if __name__ == "__main__":
    sys.exit(main())
