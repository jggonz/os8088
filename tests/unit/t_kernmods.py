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
KNOB_ONLY = ("band.inc", "bootprof.inc", "moudiag.inc")


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
    for name, v in sorted(per.items()):
        if name in KNOB_ONLY:
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
