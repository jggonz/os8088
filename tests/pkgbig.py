#!/usr/bin/env python3
"""SPEC.md 19.1's size rule, and the loader half that makes it safe.

    make pkgbig && python3 tests/pkgbig.py [machine] [system-image]

`APP_MAX_SIZE` bounds the PRIMARY SEGMENT's image + bss. It used to bound the
FILE as well, because the two were the same thing, and SPEC.md 19.1 typed a
`*.O88` as a package only when its size dword's high word was zero - with the
tradeoff written down: *"a >=64KB *.O88 reads 'Bad package' rather than 'Too
large' - it cannot be a package, so the message is truthful."* A package that
carries PARTS beyond its own segment ends the second half of that sentence,
so the bound moved out to `PKG_FILE_HI` (1MB) and the message has to become
truthful again.

**Lifting the mount's rule alone is a defect, and that is the whole of what
this gate is for.** The staged entry has carried a 32-bit size since SPEC.md
19.1 and `LD_DE_SIZE` names only its low half. A 70KB file's low word is
4,608 - a perfectly plausible small package - so a loader that reads it
without testing the high one sizes a region from a wrapped length, reads a
truncated image and reports `Bad package` about a file whose only fault is
its size. The high word is tested TWICE: `ld_take` (step 1) refuses anything
at or past `PKG_FILE_HI`, and `ld_check_hdr` (step 3) refuses a non-zero high
word outright unless the header's flags bit 2 says the file is longer than
its image on purpose. BIGPKG is 70KB, so step 1 passes it and step 3 is the
one that answers.

TWO FILES, AND THE PAIR IS THE EXPERIMENT. Neither alone answers anything:

  1. `BIGPKG.O88`, 70,144 bytes - over `APP_MAX_SIZE`, under 1MB. The mount
     must type it 1 and the loader must answer **LD_EBIG**. On its own this
     is also what a rule that types every `*.O88` as a package looks like.

     Its header's image word is the file size's LOW word, 0x1200, and that
     matters: it used to be `min(total, 0xFFFF)`, which is over
     `APP_MAX_SIZE`, so `ld_check_hdr` refused the file on the image bound
     and the high-word test this row is named for was never reached. A/B on
     the machine, with `or dx,dx / jne .toobig` deleted from the kernel: the
     clamped fixture still PASSES this row and the truncated one fails it.
  2. `HUGE.O88`, 1,048,576 bytes - exactly `PKG_FILE_HI << 16`, the first
     size the mount itself refuses. It must type 0 and reach the loader as
     **LD_EBAD**. On its own this is also what the OLD `high word == 0` rule
     looks like.

Only both together say the bound is where it is meant to be.

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

# THE FIXTURE IS 1.44MB MEDIA AND NEEDS THE 1.44MB MACHINE. HUGE.O88 is a
# megabyte on its own, so this disk cannot exist in either smaller geometry -
# and every other MartyPC machine here takes `pcxt_2_360k_floppies`, which is
# a drive that cannot read it. This row ran on the CGA machine and passed
# anyway for as long as the mount only ever had to read the ROOT DIRECTORY:
# both refusals are decided from the directory entry's size dword and neither
# file is opened, so nothing ever asked the drive for a sector the media has
# and the drive has not. That is a row proving its point by accident, one
# fixture change away from proving nothing.
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla_144"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/pkgbig.img"
S = os88sym.linear

LD_OK, LD_EDISK, LD_EBAD, LD_EBIG, LD_EABORT, LD_ENOMEM = range(6)
NAMES = ["LD_OK", "LD_EDISK", "LD_EBAD", "LD_EBIG", "LD_EABORT", "LD_ENOMEM"]
fails = []


def say(s):
    print("  " + s)


def verdict(m):
    return m.read(S("ld_status"), 1)[0]


def name_of(v):
    return NAMES[v] if v < len(NAMES) else "?%d" % v


with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]
    rows = dispcp.listing(m, S)
    say("B:\\ = %r" % rows)

    have = {n.upper(): t for n, t in rows}
    for want in ("BIGPKG.O88", "HUGE.O88"):
        if want not in have:
            sys.exit("pkgbig: %s is not on this disk - run `make pkgbig`. "
                     "The listing is %r" % (want, [n for n, _ in rows]))

    # --- the TYPE WORD, which is the rule itself ----------------------------
    # Read before either click, because a double-click that missed its row
    # leaves [ld_status] holding whatever the last load said, and these two
    # numbers are what say the mount classified the pair the way SPEC.md 19.1
    # now describes. They are also the only half a click cannot fake.
    say("types: BIGPKG.O88 = %d (want 1)   HUGE.O88 = %d (want 0)"
        % (have["BIGPKG.O88"], have["HUGE.O88"]))
    if have["BIGPKG.O88"] != 1:
        fails.append("BIGPKG.O88 (70,144 bytes) typed %d, not 1: the mount "
                     "still refuses a *.O88 over 64KB, so PKG_FILE_HI is not "
                     "in dsk_synth's test (SPEC.md 19.1)"
                     % have["BIGPKG.O88"])
    if have["HUGE.O88"] != 0:
        fails.append("HUGE.O88 (1,048,576 bytes) typed %d, not 0: the bound "
                     "is not AT PKG_FILE_HI, it is somewhere above it - the "
                     "rule types files it must refuse (SPEC.md 19.1)"
                     % have["HUGE.O88"])

    # --- and the loader's verdict on each ------------------------------------
    for name, want in (("BIGPKG.O88", LD_EBIG), ("HUGE.O88", LD_EBAD)):
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, name)
        os88marty.settle(m)
        got = verdict(m)
        say("%-11s -> %-9s (want %s)" % (name, name_of(got), name_of(want)))
        if got == want:
            continue
        if name == "BIGPKG.O88" and got == LD_EBAD:
            fails.append(
                "BIGPKG.O88 was refused as Bad package. The mount typed it 1 "
                "and the loader then believed LD_DE_SIZE's LOW word - 4,608 - "
                "so it sized a region from a wrapped length and rejected the "
                "truncated image. This is SPEC.md 21.4's hazard and it is what "
                "ld_check_hdr's `or dx,dx / jne .toobig` exists to stop")
        else:
            fails.append("%s -> %s, want %s (SPEC.md 21 steps 1 and 3)"
                         % (name, name_of(got), name_of(want)))

    # --- nothing was loaded, and nothing was left behind ---------------------
    wins = dispcp.win_list(m, S)
    say("windows after both refusals: %r" % wins)
    if len(wins) != 1:
        fails.append("a refused load left %d windows where the Disk window "
                     "should be alone: something was launched, or a failure "
                     "path did not clean up (SPEC.md 21 step 10)" % len(wins))

if fails:
    print("\npkgbig: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\npkgbig: the mount types a package by PKG_FILE_HI and the loader "
      "reads 32 bits of its size - PASS on %s" % MACHINE)
