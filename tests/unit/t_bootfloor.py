#!/usr/bin/env python3
"""Stage 1's RAM floor is the kernel's own ladder (SPEC.md 2.7.1).

    python3 tests/unit/t_bootfloor.py [--fence]

`--fence` is the half that runs on EVERY BUILD (the fast-tier `bootfence`
row), and the half `B2CALL` rests on (SPEC.md 2.9.15.2): `.boot3` calls back
into the blob as `call BLOB_SEG_AT:x`, a constant, where everything before it
asked `[spl_fseg]` - so a stage 1 or a volume boot record that read the blob
ANYWHERE ELSE would send `.boot3` into the wrong RAM with no error at all.
It reads the shipped sectors' own encodings (stage 1's `jmp HEAP_PARA:0`,
its `.nomem` bound, boothd's two loads of BLOB_SEG) against each kernel's
MAP - no assembly, which is what keeps it cheap enough for the fast tier.
The owner's choice over SPLCALL's 19 bytes a site (BOOT3-PLAN 15 q9).

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


def mapeq(tree, name):
    """An equate out of `tree`'s kernel.map - the ASSEMBLY'S own value, so a
    kernel's arm (kern_small's 11-sector blob, a lifted BLOB_SEG_AT) is the
    one read. This used to take BOOT2_SECS's first literal out of
    kernel.asm's text, which is kern_big's 13 whatever was built."""
    path = os.path.join(tree, "kernel.map")
    m = re.search(r"^([0-9A-F]{8})\s+%s\s*$" % name,
                  open(path, errors="replace").read(), re.M)
    return int(m.group(1), 16) if m else None


def boot2_secs(tree=BUILD):
    return mapeq(tree, "BOOT2_SECS")


def fence():
    """B2CALL's fence: every shipped loader puts the blob at the map's
    BLOB_SEG_AT, per kernel tree."""
    trees = [(BUILD, ("boot.bin", "boot360.bin", "boot120.bin"), "boothd.bin")]
    small = os.path.join(BUILD, "smallk")
    if os.path.exists(os.path.join(small, "kernel.map")):
        trees.append((small, ("boot.bin", "boot360.bin"), None))
    # kern_emu is B3_ON too, so its B2CALLs rest on ITS loaders. `all` does
    # not build it - `make emu` does, as boot.bin and boothd.bin beside the
    # map they were made from - so it is checked whenever that tree exists.
    emu = os.path.join(BUILD, "emuk")
    if os.path.exists(os.path.join(emu, "kernel.map")):
        trees.append((emu, ("boot.bin",), "boothd.bin"))
    for tree, sectors, vbr in trees:
        rel = os.path.relpath(tree, ROOT)
        if not os.path.exists(os.path.join(tree, "kernel.map")):
            check(False, "%s/kernel.map exists" % rel, "`make` writes it; "
                  "without it the fence asserts nothing", got="missing",
                  want="a map")
            continue
        blob, n2 = mapeq(tree, "BLOB_SEG_AT"), boot2_secs(tree)
        check(blob is not None and n2 is not None,
              "%s/kernel.map has BLOB_SEG_AT and BOOT2_SECS" % rel,
              "the fence compares the loaders against them", got=(blob, n2),
              want="two equates")
        if blob is None or n2 is None:
            continue
        want = blob + n2 * 32 + TRANSIENT_PARA
        jmp = bytes([0xEA, 0, 0]) + blob.to_bytes(2, "little")
        for name in sectors:
            path = os.path.join(tree, name)
            if not os.path.exists(path):
                check(False, "%s/%s exists" % (rel, name), "`make` builds it",
                      got="missing", want=path)
                continue
            d, hits = bound(path)
            check(d.count(jmp) == 1,
                  "%s/%s jumps to the blob at BLOB_SEG_AT %04X" % (rel, name,
                                                                   blob),
                  "stage 1 reads the blob to HEAP_PARA and enters it there; "
                  "`.boot3`'s B2CALL is `call BLOB_SEG_AT:x`, so a sector "
                  "that put it anywhere else sends every one of them into "
                  "the wrong RAM (SPEC.md 2.9.15.2)",
                  got="%d far jump(s) there" % d.count(jmp), want="exactly 1")
            check(len(hits) == 1 and hits[0][1] == want,
                  "%s/%s refuses below BLOB_SEG_AT + the blob + 2,560" % (
                      rel, name),
                  "the .nomem bound is HEAP_PARA's other use; a different "
                  "value is a sector built against a different kernel",
                  got=[hex(v) for _, v in hits], want=hex(want))
        if vbr:
            path = os.path.join(tree, vbr)
            if not os.path.exists(path):
                check(False, "%s/%s exists" % (rel, vbr), "`make` builds it",
                      got="missing", want=path)
                continue
            d = open(path, "rb").read()
            lo = blob.to_bytes(2, "little")
            check(bytes([0xBA]) + lo in d and bytes([0xB8]) + lo in d,
                  "%s/%s loads BLOB_SEG = BLOB_SEG_AT %04X" % (rel, vbr, blob),
                  "the volume boot record reads the blob to BLOB_SEG and "
                  "far-calls kz_hd there (SPEC.md 2.9.13.5); B2CALL rests on "
                  "the same number (SPEC.md 52.10.2.1's incident is the "
                  "shape of the failure)",
                  got="absent", want="mov dx/ax, %04X" % blob)


if sys.argv[1:] == ["--fence"]:
    fence()
    done("bootfence")
    sys.exit(0)
fence()


# --- 1. the shipped sectors carry THIS kernel's floor ----------------------
# ONE blob length: the blob lives at HEAP_SEG, where stage 1 READ it, under
# the 2,560 bytes stage 1 keeps live at the ceiling. It was briefly two, when
# the blob was read to the top and copied down and the far jump at the end of
# that copy had to survive being copied over (SPEC.md 2.9.5).
# ...and the blob is LIFTED off HEAP_SEG by BLOB_LIFT where `.cold` is the
# top rung (SPEC.md 2.10.3): kernsize's `blobseg` is where stage 1 reads it,
# and `kend` only on a kernel that predates ROM_COLD
k = ladder()
want = k.get("blobseg", k["kend"]) + boot2_secs() * 32 + TRANSIENT_PARA

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
