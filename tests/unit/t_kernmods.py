#!/usr/bin/env python3
"""tools/kernsize.py's PER-MODULE pass still measures, and measures everything.

    python3 tests/unit/t_kernmods.py

t_kernbudget guards the blessed baseline; this guards the report that produces
it one level down.  `--modules` and `--bless` instrument a temporary copy of
kernel.asm with a bare label in each section around every `%include`, then
assemble the instrumented copy AND the original and compare them byte for
byte before believing a single number.  That compare is the right design and
it worked: when the markers started emitting into the wrong section it refused
to report.

WHAT NOTHING GUARDED IS THAT THE REFUSAL GETS FIXED.  `--modules` prints the
refusal on stderr and exits 0, `--bless` exits 1 without writing - so the
baseline could not be blessed at all, while t_kernbudget went on advising
"one command, tools/kernsize.py --bless".  Nothing in any tier ran the pass,
because `make` runs the plain report, so it stayed broken and the advice
stayed false.

The specific rot, for the record: the markers ended in `section .text` on the
reasoning that "every %include in kernel.asm sits at .text scope".  SPEC.md
2.9.4 moved the loading screen into stage 2, so kernel.asm now reads
`section .boot2` / `%include "splash.inc"` / `section .text` and splash.inc
carries no section directive of its own - 1,859 bytes into the wrong section,
and every byte after them moved.

Four things are checked, and the FIRST is the byte compare itself:

1. the pass measures at all - which is that compare, run
2. the attribution is complete: kernel.asm's own residual is non-negative in
   every section, so no set of modules claims more than the section holds
3. no module reads as FREE.  A module measuring zero everywhere is what the
   splash defect looked like once the section tracking was right, and it is
   invisible in a report that only ever gets longer
4. every module has a theme, because `render_themes` drops what it cannot
   place and the total silently stops adding up
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                           # noqa: E402
import kernsize                                           # noqa: E402

# Compiled only behind a knob (`make BAND=1`, `BOOTPROF=1`, `MOUDIAG=1`), so
# a default build measures them at zero and that is the correct answer.  A
# module joining this list is a decision: it is saying "this ships in no
# kernel any disk carries", which is exactly what a knob is.
#
# **vmmouse.inc IS THE ONE ENTRY THAT IS NOT A KNOB** (SPEC.md 9.11.7), and it
# is named here rather than exempted quietly because the distinction matters:
# `make emu` is a SHIPPED PRODUCT - kern_big plus the VMware absolute pointer,
# for v86 in a browser and for a desktop hypervisor - so build/emu.img is a
# disk that carries it, which is precisely what the four above are saying is
# not true of them. What it shares with them is the only thing this test can
# see: it measures zero on the DEFAULT build, because the whole file is inside
# %ifdef KERN_EMU and the target machine is a 4.77 MHz 8088 that can neither
# speak a 32-bit backdoor protocol nor be spoken to. Its bytes are on the emu
# variant's row instead - `tools/kernsize.py --modules -DKERN_BIG -DKERN_EMU`
# prints them, exactly as `--modules -DKERN_SMALL` prints kern_small's.
KNOB_ONLY = ("band.inc", "bootprof.inc", "moudiag.inc", "stkdiag.inc",
             "vmmouse.inc")

# ...and a second reason for a zero row, which is NOT a knob: a file whose
# whole contribution is an on-demand module IMAGE (SPEC.md 2.8).  Those
# sections are cut out of kernel.bin by tools/os88mod.py and shipped as a
# `.DRV`, so they are not in MOD_SECTIONS and never will be - the report is
# about what a machine carries, and a module image is not it.  Every other
# module file has a resident half as well (a thunk, a string, a `mod_tab`
# row) and so measures somewhere; compress.inc is the first that is PURELY
# the image, and its resident half lives on mod.inc's and files.inc's rows.
# A file joining this list is saying "nothing of this is in KERNEL.SYS".
IMAGE_ONLY = ("compress.inc", "dockmod.inc")

# ...and a THIRD reason for a zero row, which is neither a knob nor an image:
# a file whose whole contribution is a MACRO.  `mouproto.inc` (SPEC.md 9.5) is
# the Microsoft serial packet's arithmetic as `MOU_DECODE_MS`, and it emits not
# one byte of its own - the bytes are charged to the EXPANSION SITE, inside
# `mou_byte` in mouse.inc, which is exactly where a reader wants to see them.
# It is a macro rather than a proc on purpose: the kernel's copy is reached by
# fall-through from the phase machine above it in an ISR, and a `call` there
# would cost a return address on the shared mouse stack SPEC.md 9.10 exists to
# keep shallow.  It has TWO hosts - kernel/mouse.inc and kerndos/kdmouse.inc -
# which cannot call each other, so what they share they share as SOURCE.
#
# A file joining this list is saying "this has no bytes of its own ANYWHERE",
# which is a stronger claim than the two above and cheap to check: it is true
# exactly when the file contains no `section` of its own.  Say that in the
# entry, because the tempting wrong member is a file that emits under an
# %ifdef nobody defines - that one is KNOB_ONLY, with the knob named.
MACRO_ONLY = ("mouproto.inc",)


def main():
    per, err = kernsize.measure_modules()
    check(per is not None,
          "tools/kernsize.py's per-module pass measures this kernel",
          "the pass assembles an instrumented copy of kernel.asm and the "
          "original and compares them byte for byte; a refusal here means "
          "--bless cannot write the baseline at all, and t_kernbudget's "
          "advice - `one command, tools/kernsize.py --bless` - is false",
          got=str(err), want="a measurement for every %include")
    if per is None:
        done("t_kernmods")
        return

    cur, merr = kernsize.measure()
    check(cur is not None, "the section totals came out of NASM",
          "the residual below is a subtraction from them", got=str(merr),
          want="kernel.asm's own ks: line")
    if cur is None:
        done("t_kernmods")
        return

    rows = kernsize.module_rows(per, cur)
    resid = rows[-1]
    check(resid[0] == kernsize.RESIDUAL,
          "the last row is kernel.asm's own residual",
          "module_rows appends it last and everything here indexes on that",
          got=resid[0], want=kernsize.RESIDUAL)

    # 2. Complete: what the modules did not claim is kernel.asm's, and that
    #    cannot be negative in any section.
    for i, key in enumerate(("text", "cold", "code", "bss", "lowbss",
                             "boot2")):
        check(resid[2 + i] >= 0,
              "kernel.asm's residual `.%s` is not negative" % key,
              "a negative residual means the modules between them claim more "
              "than the section holds - the shape a mis-attributed %include "
              "makes, and the table would still add up to the right total",
              got="%s = %d" % (key, resid[2 + i]), want=">= 0")

    # 3. Nothing reads as free.
    # ...and a MACRO_ONLY claim is CHECKED rather than taken: "no bytes
    # anywhere" is true exactly when the file opens no section of its own, so
    # the exemption cannot quietly cover a file that has started emitting.
    for name in MACRO_ONLY:
        p = os.path.join(ROOT, "kernel", name)
        src = open(p, errors="replace").read() if os.path.exists(p) else ""
        secs = re.findall(r"(?m)^\s*section\s+\.", src)
        check(os.path.exists(p) and not secs,
              "%s really is macro-only: it opens no section of its own" % name,
              "MACRO_ONLY says the file's bytes are charged to the expansion "
              "site, which is only true while it emits none itself; a file "
              "here that has grown a `section` is a module measuring zero for "
              "a DIFFERENT reason and belongs in KNOB_ONLY or on a row",
              got=("%d section(s)" % len(secs)) if os.path.exists(p)
                  else "no such file", want="none")

    for name, v in sorted(per.items()):
        if name in KNOB_ONLY or name in IMAGE_ONLY or name in MACRO_ONLY:
            continue
        check(any(v[s] for s in kernsize.MOD_SECTIONS),
              "%s measures somewhere" % name,
              "a module reporting zero in every section is either compiled "
              "out - in which case it belongs in this test's KNOB_ONLY, with "
              "the knob named - or its bytes are landing in a section the "
              "report does not track, which is how splash.inc's 1,859 came "
              "to read as nothing",
              got="0 in " + ", ".join("." + s for s in kernsize.MOD_SECTIONS),
              want="a byte in at least one of them")

    # 4. Every module is in a theme.
    _, missing = kernsize.render_themes(rows)
    check(not missing, "every module has a theme in tools/kernsize.py",
          "render_themes drops what it cannot place, so the theme table's "
          "total quietly stops being the module table's",
          got=", ".join(missing) or "-", want="all placed in THEMES")

    print("t_kernmods: %d modules measured, residual clean, all themed"
          % len(per))
    done("t_kernmods")


if __name__ == "__main__":
    main()
