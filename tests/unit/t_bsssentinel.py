#!/usr/bin/env python3
"""A sentinel byte read OUTSIDE its writer's bracket must be seeded (SPEC.md 12.8.5.1).

    python3 tests/unit/t_bsssentinel.py

`.bss` in this kernel is `nobits`, so `-f bin` emits nothing for it (SPEC.md
2.6) and what lands on those bytes at boot is the image's own inter-section
padding: ZERO.  There is no boot-time clear to hook a value onto.  A byte whose
resting value is not zero therefore cannot live in `.bss` at all - it lives in
`.text`, seeded, which is `api_name`'s rule and `spl_fp`'s.

WHAT WENT WRONG, and it is the reason this is a gate rather than a comment.
`fsx_cur` is the fullscreen-exclusive mode id, and 0xFF spells "unswitched" in
every comparison in the tree.  It was `.bss` on an argument that was true when
it was written - *"written by fsx_run before anything reads it"* - because
every reader it had was INSIDE an fsx bracket, where `fsx_run` has already
stored 0xFF.

SPEC.md 12.8.5 then added `fpg_arm`'s fourth refusal, `[fsx_cur] != 0xFF`, and
that is the first reader OUTSIDE a bracket: it runs on a machine that has never
launched a fullscreen app.  There the byte was 0 - which is `FSXM_TEXT80`, a
perfectly good mode id - so the file-progress widget read a foreign mode on the
glass and refused EVERY file operation on the machine, from boot until some fsx
app had run and restored.  Measured on `os8088_xt_hdd`: `[fsx_cur]` = 0x00 on a
settled desktop, `[fpg_on]` = 0 for the whole of a hard-disk install.

NOTHING ELSE HERE COULD SEE IT.  It assembles cleanly, nothing errors, and the
operation completes - the widget simply never appears.  On the target machine
that reads as a LOCK, because an install is minutes of `int 13h` behind a held
`gfx_lock` and the widget is the only thing on screen that says the machine is
alive.  It was reported as one.

IT READS THE BINARY, NOT THE SOURCE, for `t_api_abi.py`'s reason: the defect is
a disagreement between what somebody meant and what NASM actually placed, and a
scan of the source re-derives the intent rather than the outcome.  Both halves
come off the same map - `os88sym` asserts byte-identity with `build/kernel.bin`
before it answers, so the section a symbol is in and the byte at its address
describe one binary and not two.

BOTH HALVES, and the second is not redundant.  The section test is the
actionable one - it names `.bss` and somebody knows immediately what to do -
but it would pass a byte correctly placed in `.text` and seeded `db 0`.  The
image test is the one that answers the machine's question.

THE TABLE IS THE MAINTAINED PART.  A row is a claim that some routine compares
this byte against this value while the byte is at rest, and it is cheap to add
one: the cost of getting it wrong is a feature that silently does nothing.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from harness import check, done                            # noqa: E402
import os88layout                                          # noqa: E402
import os88sym                                             # noqa: E402

# symbol, the byte it must hold at rest, and WHO reads it outside the bracket
# that writes it.  The third column is the point of the row: it names the
# reader whose existence is what makes the seed load-bearing.
SENTINELS = [
    ("fsx_cur", 0xFF,
     "fpg_arm's fourth refusal (SPEC.md 12.8.5) compares it against 0xFF on a "
     "machine that has never entered an fsx bracket; 0 is FSXM_TEXT80 and "
     "refuses the file-progress widget for every file operation after boot"),
]


def main():
    binp = os.path.join(ROOT, "build", "kernel.bin")
    if not os.path.exists(binp):
        print("t_bsssentinel: no build/kernel.bin - `make` first")
        sys.exit(1)

    syms = os88sym.syms()
    sect = os88sym.sections()             # the same map, already assembled
    blob = open(binp, "rb").read()

    for name, want, why in SENTINELS:
        if not check(name in syms, "%s is a kernel symbol" % name,
                     "the row below cannot be checked without it"):
            continue
        addr = syms[name]

        # Which section, said separately from the byte below because it is the
        # actionable half: `.bss` names the mistake and the remedy at once.
        check(sect.get(name) == ".text",
              "%s is in .text, not .bss" % name,
              why, got=sect.get(name), want=".text")

        off = os88layout.text_at(addr, ROOT)
        if not check(off < len(blob),
                     "%s's address is inside kernel.bin" % name,
                     "the image is shorter than the symbol map says"):
            continue
        got = blob[off]
        check(got == want,
              "%s rests at 0x%02X in the built image" % (name, want),
              why, got="0x%02X at file offset %d" % (got, off),
              want="0x%02X" % want)

    done("t_bsssentinel")


if __name__ == "__main__":
    main()
