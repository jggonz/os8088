#!/usr/bin/env python3
"""Stage 1's RAM floor is the kernel's own ladder (SPEC.md 2.7.1).

    python3 tests/unit/t_bootfloor.py

`boot/boot.asm` refuses a machine that cannot hold stage 2's blob at HEAP_SEG
under the 2,560 bytes stage 1 keeps live at the ceiling.  It cannot compute
HEAP_SEG - `LOW_PARA` is a nobits size that is not in kernel.bin, and the
sector is assembled before the kernel it boots is measured - so the Makefile
injects it as `-DHEAP_PARA`, out of `tools/kernsize.py --json`.

THIS ROW EXISTS BECAUSE GUARD 5c NO LONGER CAN.  That guard reconciled stage
1's bound with the blob's home from inside the kernel's own assembly, which
worked only while the two were written in different terms; now that stage 1
tests the exact condition, the guard reduces to `x > x + 160` and can only
pass.  What it cannot see, and never could, is the half that can still go
wrong: an INJECTED value going stale, or being measured with the wrong knobs.

That second one is not hypothetical.  `kernsize.py` re-ASSEMBLES the kernel
to measure it, so `--json` with no defines describes the SHIPPED kernel
whatever is in build/ - and the same mistake on the hard-disk side published
a volume boot record that loaded its blob on top of its own image (SPEC.md
52.10.2.1).  Here it would hand a kern_small sector kern_big's floor and
refuse machines kern_small runs on.  The Makefile passes $(VIDDEF) to guard
against it; this row is what notices if that stops happening.

THE SECOND HALF IS THE CLAMP, and it is the one with teeth.  A FLAT_PAYLOAD
image (SPEC.md 2.9.3) has no stage 2 and no heap floor, so its bound is its
own read - and for a SMALL payload that lands below RELOC_ADJ, where the
`sub ax, RELOC_ADJ` after the compare underflows and sends the sector to the
top of a 1MB machine that is not there.  Three of the four diagnostics are
small enough today.  They are the images whose entire job is a machine that
will not boot, so a silent failure there is the worst place in the tree for
one, and nothing else in this tree assembles them at a size that would show
it.
"""
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

BUILD = os.path.join(ROOT, "build")
RELOC_ADJ = 0x07E0
KERNEL_SEG = 0x0060
# The sector's own body plus its stack - the 2,560 bytes that stay live at the
# ceiling until kmain sets SS:SP. Mirrored in boot/boot.asm and kernel.asm,
# where tests/unit/t_mirror.py compares them; here they are the arithmetic.
TRANSIENT_PARA = (512 + 2048) // 16


def bound(path):
    """The `cmp ax, imm16` the .nomem test compares against.

    Located by the pair rather than by offset: `3D lo hi` followed by `72`
    (jb) is the whole of the test, and pinning an offset would make every
    unrelated byte moved above it look like this check failing.  More than one
    match is not resolved by guessing - it is reported, because a second
    cmp/jb pair means the shape this row reads is no longer the shape the
    sector has.
    """
    d = open(path, "rb").read()
    hits = [(i, d[i + 1] | (d[i + 2] << 8))
            for i in range(len(d) - 3) if d[i] == 0x3D and d[i + 3] == 0x72]
    return d, hits


def ladder(*defines):
    out = subprocess.check_output(
        ["python3", os.path.join(ROOT, "tools", "kernsize.py"),
         "--json", "--build", BUILD] + list(defines), cwd=ROOT)
    return json.loads(out)


def boot2_secs():
    src = open(os.path.join(ROOT, "kernel", "kernel.asm")).read()
    m = re.search(r"^BOOT2_SECS  *equ  *(\d+)", src, re.M)
    return int(m.group(1))


# --- 1. the shipped sectors carry THIS kernel's floor ----------------------
# ONE blob length: the blob lives at HEAP_SEG, where stage 1 READ it, under
# the 2,560 bytes stage 1 keeps live at the ceiling. It was briefly two, when
# the blob was read to the top and copied down and the far jump at the end of
# that copy had to survive being copied over (SPEC.md 2.9.5).
k = ladder()
want = k["kend"] + boot2_secs() * 32 + TRANSIENT_PARA

for name in ("boot.bin", "boot360.bin", "boot120.bin"):
    path = os.path.join(BUILD, name)
    if not os.path.exists(path):
        check(False, "%s exists" % name,
              "`make` builds all three kernel sectors; without them this row "
              "is asserting nothing", got="missing", want=path)
        continue
    d, hits = bound(path)
    check(len(d) == 512, "%s is 512 bytes" % name,
          "a boot sector that is not one sector is not bootable",
          got=len(d), want=512)
    check(len(hits) == 1, "%s has exactly one cmp ax,imm16 / jb" % name,
          "the .nomem test is the only one; a second pair means this row is "
          "reading the wrong instruction and its verdict is meaningless",
          got=[hex(v) for _, v in hits], want="one pair")
    if len(hits) == 1:
        got = hits[0][1]
        check(got == want, "%s refuses below the kernel's own heap floor" % name,
              "HEAP_PARA is injected from kernsize --json; a mismatch means "
              "the sector was assembled against a DIFFERENT kernel than the "
              "one in build/ - stale, or measured without this build's knobs "
              "(SPEC.md 2.7.1)",
              got="0x%04X (%d KB)" % (got, -(-got // 64)),
              want="0x%04X (%d KB)" % (want, -(-want // 64)))

# --- 2. ...and the FLAT_PAYLOAD arm never underflows the relocation --------
# Assembled here rather than read off build/, because `all` does not build the
# diagnostics and the sizes that matter are the SMALL ones.
nasm = os.environ.get("NASM", "nasm")
for secs in (1, 8, 40, 55, 56, 207):
    out = os.path.join(BUILD, "t_bootfloor_flat.bin")
    try:
        subprocess.run([nasm, "-f", "bin", "-w+error", "-DFLAT_PAYLOAD",
                        "-DKERNEL_SECTORS=%d" % secs,
                        "-o", out, os.path.join(ROOT, "boot", "boot.asm")],
                       check=True, cwd=ROOT, stdout=subprocess.DEVNULL)
    except subprocess.CalledProcessError as e:
        check(False, "FLAT_PAYLOAD assembles at %d sectors" % secs,
              "the four diagnostics boot through this sector", got=str(e),
              want="a clean assembly")
        continue
    d, hits = bound(out)
    if len(hits) != 1:
        check(False, "FLAT_PAYLOAD@%d has one cmp/jb" % secs, "as above",
              got=[hex(v) for _, v in hits], want="one pair")
        continue
    got = hits[0][1]
    payload = KERNEL_SEG + secs * 32 + TRANSIENT_PARA
    check(got == max(payload, RELOC_ADJ),
          "FLAT_PAYLOAD@%d sectors bounds on max(payload, RELOC_ADJ)" % secs,
          "below RELOC_ADJ the `sub ax, RELOC_ADJ` after the compare "
          "underflows and the sector relocates to the top of a 1MB machine "
          "that is not there - on the images whose job is a machine that "
          "will not boot",
          got="0x%04X" % got, want="0x%04X" % max(payload, RELOC_ADJ))
    check(got >= RELOC_ADJ, "...and never below RELOC_ADJ", "as above",
          got="0x%04X" % got, want=">= 0x%04X" % RELOC_ADJ)
if os.path.exists(os.path.join(BUILD, "t_bootfloor_flat.bin")):
    os.remove(os.path.join(BUILD, "t_bootfloor_flat.bin"))

done("bootfloor")
