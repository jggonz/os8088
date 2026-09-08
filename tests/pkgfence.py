#!/usr/bin/env python3
"""SPEC.md 21 steps 4 and 6's WRITE BOUND: `image + bss` is seventeen bits.

    make pkgbig && python3 tests/pkgfence.py [machine] [system-image]

SPEC.md 21 states the bound twice, as an invariant - a package's image and
its bss are read and cleared into the region step 5 claimed and "never a
neighbour's region". The only thing enforcing it is `ld_check_hdr`'s fence,
and until size pass 3 that fence was:

    mov dx, [si+LD_H_BSS]
    cmp dx, APP_MAX_SIZE        ; each operand separately bounded...
    ja .toobig
    add dx, ax                  ; "img+bss <= 0x1E000: no wrap"
    cmp dx, APP_MAX_SIZE        ; ...and the SUM compared in sixteen bits
    ja .toobig

0x1E000 is seventeen bits. The comment is the defect written down as its own
proof: the sum wraps, the wrapped value is small, and a small value passes.
The repair is one instruction - `add dx, ax / jc .toobig` - and the bss-alone
test above it becomes redundant, because `image >= LD_HDR_SIZE` means an
oversize bss either carries out of the add or lands above the bound below.

**THE REPAIR SHIPPED WITHOUT A ROW AND THIS IS THAT ROW.** Nothing else in
the tree can see it: every gate that loads a package loads a WELL-FORMED one,
and `os88pkg.py` refuses to build a malformed `.O88` at all - which is the
point, because the input this is about does not come from `os88pkg.py`. It
comes off a disk, and SPEC.md 19 says every byte read off one is hostile.

TWO FILES, AND THE PAIR IS THE EXPERIMENT:

  1. `BSSWRAP.O88` - image = bss = 0xF000, file 61,440 bytes. The sum is
     0x1E000, which wraps to 0xE000: BELOW `APP_MAX_SIZE`, so the old fence
     passed it. Step 4 then sized a 56KB claim and step 6 read the whole
     61,440-byte file into it - 4KB past the end.
  2. `BSSWORST.O88` - image = 0xF000, bss = 0x1001, the same 61,440 bytes.
     The sum wraps to **1**. Step 4 rounds that to one 512-byte page and
     claims ONE KILOBYTE; step 6 reads 120 sectors into it and 60,416 bytes
     land through whatever `mem_claim_hi` put underneath - a resident
     package's CODE, because it places top-down.

The first alone is what a reader would try and would under-state the fault by
a factor of fifteen; the second alone looks like a contrived corner. Together
they say the wrap is unbounded, not a small overrun.

Both must answer **LD_EBIG**, and both are refused by the same instruction -
which is the honest statement of what this row covers: one `jc`, from two
directions that a sixteen-bit compare cannot tell apart.

WHAT A REGRESSION LOOKS LIKE, measured rather than predicted. The `jc` was
deleted from `ld_check_hdr` and this row re-run on the reference machine:
both files answer **LD_OK**. The loader accepts them, claims 56KB and 1KB
respectively, reads the whole 61,440-byte file into each, far-calls the
dispatcher and comes back to record success - so the overrun has happened and
the status byte is clean. That is the good case for a gate: the row reads
LD_OK against a wanted LD_EBIG and says so in one line. It is not guaranteed,
because what those 60,416 bytes land on is whatever `mem_claim_hi` placed
below, and a different heap could take the desktop with it - so the row's
timeout is the backstop and not the assertion.

THE INSTRUMENT IS `[ld_status]`, read out of the guest - the loader's own
verdict, not the pixels of a toast. It reads no framebuffer, so it answers
for all three adapters out of one run.
"""
import sys
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88sym
import dispcp

# 1.44MB media and the 1.44MB machine - build/pkgbig.img carries HUGE.O88,
# a megabyte on its own, so this disk exists in no smaller geometry. See
# tests/pkgbig.py's note: every other MartyPC machine here has 360KB drives.
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/pkgbig.img"
S = os88sym.linear

LD_OK, LD_EDISK, LD_EBAD, LD_EBIG, LD_EABORT, LD_ENOMEM = range(6)
NAMES = ["LD_OK", "LD_EDISK", "LD_EBAD", "LD_EBIG", "LD_EABORT", "LD_ENOMEM"]

# (file, image, bss, wrapped sum, what the old fence did with it)
CASES = (
    ("BSSWRAP.O88",  0xF000, 0xF000, 0xE000,
     "a 56KB claim and 4,096 bytes past it"),
    ("BSSWORST.O88", 0xF000, 0x1001, 0x0001,
     "a ONE KILOBYTE claim and 60,416 bytes past it"),
)
fails = []


def say(s):
    print("  " + s)


def name_of(v):
    return NAMES[v] if v < len(NAMES) else "?%d" % v


with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    rows = dispcp.listing(m, S)
    say("B:\\ = %r" % rows)

    have = {n.upper(): t for n, t in rows}
    for fname, _, _, _, _ in CASES:
        if fname not in have:
            sys.exit("pkgfence: %s is not on this disk - run `make pkgbig`. "
                     "The listing is %r" % (fname, [n for n, _ in rows]))
        # The MOUNT must type it 1, or the loader is never reached and a
        # green run would mean nothing: an untyped file answers LD_EBAD at
        # step 1 and never sees ld_check_hdr at all.
        if have[fname] != 1:
            fails.append("%s typed %d, not 1: the mount refused it before the "
                         "loader saw it, so this row proves nothing about the "
                         "fence (SPEC.md 19.1)" % (fname, have[fname]))

    for fname, img, bss, wrapped, damage in CASES:
        # ...and it must REFUSE: this row is the size fence (SPEC.md 19.1).
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, fname,
                          expect="refusal")
        os88marty.settle(m)
        got = m.read(S("ld_status"), 1)[0]
        say("%-13s image %#06x + bss %#06x = %#07x, wraps to %#06x -> %s "
            "(want LD_EBIG)" % (fname, img, bss, img + bss, wrapped,
                                name_of(got)))
        if got == LD_EBIG:
            continue
        fails.append(
            "%s -> %s, want LD_EBIG. image %#06x + bss %#06x is %#07x, which "
            "is SEVENTEEN BITS; if ld_check_hdr compares the sum in sixteen "
            "it sees %#06x and lets the load through, and step 6 then gives "
            "%s (SPEC.md 21 steps 4 and 6)"
            % (fname, name_of(got), img, bss, img + bss, wrapped, damage))

    # --- nothing was loaded, and nothing was left behind ---------------------
    wins = dispcp.win_list(m, S)
    say("windows after both refusals: %r" % wins)
    if len(wins) != 1:
        fails.append("a refused load left %d windows where the Disk window "
                     "should be alone: something was launched, or a failure "
                     "path did not clean up (SPEC.md 21 step 10)" % len(wins))

if fails:
    print("\npkgfence: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\npkgfence: image + bss is fenced in SEVENTEEN bits, so neither wrap "
      "reaches a claim - PASS on %s" % MACHINE)
