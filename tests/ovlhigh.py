#!/usr/bin/env python3
"""A C package's OVERLAY is claimed from the top and declares itself movable.

    make cworddisk && python3 tests/ovlhigh.py

WHAT IT IS ABOUT (docs/plans/HEAP-UNPIN-PLAN.md 2.1.1 item 1, SPEC.md 50.3.2).
CWORD's overlay is 14,106 bytes - bigger than every kernel module in the tree
put together - and it took the LOW claim door for as long as overlays have
existed, so it sat pinned in the middle of the data arena for the program's
whole life. SPEC.md 50.3's own words for what that does are "one long-lived
data claim landing mid-heap permanently splits the space a package can be
loaded into", and 50.3.2's rule sends a claim whose base IS A CS to the top.

SINCE SPEC.md 73.14.2 THE OVERLAY IS A PART OF CWORD.O88, EAGER: op_load reads
it at launch into the parts CARVE, which op_claim takes through the top-down
door (SPEC.md 20.12.10.8), and crt0's cc_ovinit declares that carve movable
with cc_ovcarve as its proc - the carve holds the overlay and nothing else. So
the claim to look at appears at LAUNCH, not at the first command, and the row
finds it by its CONTENT rather than by timing: the one new claim of the
package's whose bytes, past the read's head slack, ARE build/CWORD.OVL - the
plain cut os88ovl.py made - which is also the whole decode checked byte for
byte.

FIVE ASSERTIONS, and each is a different half of the change:

  1. **The overlay is in RAM after the launch**, decoded: exactly one new claim
     owned by CWORD's region holds the module's bytes.
  2. **MC_HI is 1** - it came in through the top-down door, which is what the
     descending compaction pass reads (SPEC.md 66.4.1).
  3. **It is above every bottom-up claim, and packed against the ceiling with
     no hole above it.** MC_HI alone would pass on a kernel that stamped the
     byte and ignored it; these two are the placement said as facts about the
     arena. The package's own REGION is a top-down claim as well
     (docs/plans/HEAP-UNPIN-PLAN.md 2.1), so the carve lands directly BELOW it.
  4. **MC_RLOC is not 0** - the claim declared itself movable and the kernel
     took the declaration (SPEC.md 66.5.6.2: a refused declaration is a CF the
     caller discards, and MC_RLOC read from OUTSIDE is the only witness).
  5. **F5 claims nothing new.** F5 is `CW_K_F5` -> `CWA_GOTO` -> `ovl_dlg_open`,
     an overlay function: with the module already bound, cc_ovneed is the
     worker gate and nothing else, so the call runs on the claim above. It is
     a KEY and not a menu pick because CWORD draws its own menu bar inside its
     window (cwmenu.c).

VERIFIED TO FAIL: with the OSAPI_MEM_MOVABLE call taken out of cc_ovinit,
assertion 4 goes red; with CC_OVL_PART taken out of apps/cword/cword.asm (and
`part` out of the CC_PACKAGE call), the disk carries no overlay part, nothing
new holds the module after launch, and assertion 1 stops the row.

It is CWORD because CWORD is the C package with an overlay that fits a 360KB
floppy beside the system disk. The code under test is `apps/cc/crt0.asm`,
which every C package shares.
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
MODULE = "build/CWORD.OVL"            # the plain cut, kept for host readers
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
    need(MODULE)
    module = open(MODULE, "rb").read()
    S = os88sym.linear

    kw = {"machine": a.machine} if a.machine else {}
    with os88ui.boot("build/os8088-360.img", apps=DISK, **kw) as ui:
        m = ui.m
        before = claims(m, S)
        w = ui.path("B:/CWORD.O88")
        region = u16(m.read(os88geom.winptr(m, w, S) + os88geom.W_SEG, 2))
        if not check(region != 0, "CWORD has a region",
                     "nothing below can be located without it",
                     got=region, want="a segment"):
            done("ovlhigh")
        os88marty.settle(m)
        after = claims(m, S)

        # the overlay's carve is a claim that appeared at the launch, owned by
        # the package's own segment (SPEC.md 50.2), whose bytes - past the
        # read's head slack, a whole number of sectors under one cluster -
        # ARE the module
        seen = set(c[0] for c in before)
        new = [c for c in after if c[0] not in seen and c[2] == region]
        print("region %04x, %d claims -> %d" % (region, len(before), len(after)))
        for base, para, own, dma, rloc, hi in after:
            print("  %04x %6d KB owner %04x%s%s%s"
                  % (base, para // 64, own, " dma" if dma else "",
                     " MOVABLE" if rloc else "", "  HI" if hi else ""))
        hit = []
        for c in new:
            span = m.read(c[0] * 16, c[1] * 16)
            for slack in range(0, min(len(span), 32768), 512):
                if span[slack:slack + len(module)] == module:
                    hit.append((c, slack))
                    break
        if not check(len(hit) == 1,
                     "the launch read and decoded the overlay part into a "
                     "claim of the package's own",
                     "op_load reads every eager part before any C runs "
                     "(SPEC.md 20.12, 73.14.2); with no claim holding "
                     "build/CWORD.OVL's bytes the four assertions below would "
                     "all be about nothing",
                     got=["%04x" % c[0] for c, _ in hit] or
                         ["%04x" % c[0] for c in new],
                     want="exactly one claim holding the module"):
            done("ovlhigh")
        (base, para, own, dma, rloc, hi), slack = hit[0]
        print("overlay carve %04x, %d paragraphs, module at +%d"
              % (base, para, slack))

        check(hi == 1, "the overlay came in through the TOP-DOWN door",
              "SPEC.md 50.3.2: a claim whose base IS a CS goes high, and "
              "SPEC.md 66.4.1's descending pass reads this byte to know which "
              "way to pack it",
              got=hi, want=1)
        # ...and the byte only says which door was ASKED for. These two say
        # the allocator answered from that end, which is the whole point: an
        # 14KB pinned image in the middle of the arena is the barrier.
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

        m.key("F5")                             # CW_K_F5 -> CWA_GOTO ->
        os88marty.settle(m)                     # ovl_dlg_open, an overlay
        later = claims(m, S)                    # function
        grew = [c for c in later
                if c[2] == region and c[0] not in set(x[0] for x in after)]
        check(not grew,
              "an overlay call after the launch claims nothing: the module "
              "is already bound",
              "the eager arm's door is the worker gate and nothing else "
              "(SPEC.md 73.14.2); a claim here is a second load",
              got=["%04x" % c[0] for c in grew], want="none")
    done("ovlhigh")


if __name__ == "__main__":
    sys.exit(main())
