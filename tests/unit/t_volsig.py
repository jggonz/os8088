#!/usr/bin/env python3
"""SPEC.md 18.8.2 - NO TWO SHIPPED VOLUMES MAY SIGN THE SAME.

The kernel's whole "is this the same disk?" is `dsk_bpb_sig`, a rotate-add sum
over LBA 0 and nothing else. SPEC.md 18.95's sector cache is keyed on it, and
so is SPEC.md 18.8's FAT window - so two volumes that sign alike are ONE
VOLUME as far as a running machine is concerned: swap one for the other and
the previous disk's directory sectors and FAT stay valid against the new
platter, and a write puts the old FAT onto it.

**THIS ROW EXISTS BECAUSE THE TREE FAILED IT FOR YEARS AND NOTHING SAID SO.**
`os88disk.py` pinned BS_VolID to 0x88000888 on every image it built, for
reproducibility, which made every non-bootable disk of a geometry byte-identical
in its boot sector: 23 images signing 0x2D68, and at 360KB that is every data
floppy the project ships. 18.8.2 called it an accepted residual on the ground
that "a full mount re-validates" - the full mount does re-read LBA 0, and it
re-reads the SAME 512 BYTES, so it could never have caught it.

**IT CHECKS THE PROPERTY, NOT THE FIELD.** A row that asserted "BS_VolID is
derived" would pass for a derivation that collided, and would fail for a future
scheme that distinguished volumes some other way. What matters is the signature
the kernel actually computes, so that is what this computes - the same rotate-add
the kernel does, on the bytes `make` just wrote.

Two images with IDENTICAL CONTENT are allowed to share a signature and are
asserted to: they are the same disk, and nothing on the machine could act on a
difference that does not exist. That half is not a concession - it is what
makes the row also a reproducibility check.
"""
import glob
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness                                                  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"


def sig(bs):
    """dsk_bpb_sig, in Python: add each word, rotate left 1, never answer 0."""
    ax = 0
    for i in range(256):
        ax = (ax + struct.unpack_from("<H", bs, i * 2)[0]) & 0xFFFF
        ax = ((ax << 1) | (ax >> 15)) & 0xFFFF
    return ax or 1


def ship_images():
    """The basenames $(SHIPIMGS) expands to, read out of the Makefile.

    Derived rather than restated, for fork rule 3's reason one level along: the
    set grew by three when the category disks landed, and a second copy of the
    list is one that goes stale without failing.
    """
    # BACKSLASH CONTINUATIONS JOINED FIRST: $(SHIPIMGS) is three lines, and a
    # line-at-a-time read finds four of the twelve and reports a green row over
    # a third of the set.
    mk = re.sub(r"\\\n\s*", " ", (ROOT / "Makefile").read_text())
    var = {}
    for m in re.finditer(r"^([A-Z0-9_]+)\s*:?=\s*(.+?)\s*$", mk, re.M):
        var.setdefault(m.group(1), m.group(2))
    out, seen = [], set()

    def expand(text, depth=0):
        if depth > 8:
            return
        for tok in text.split():
            m = re.fullmatch(r"\$\(([A-Z0-9_]+)\)", tok)
            if m:
                if m.group(1) in var and m.group(1) not in seen:
                    seen.add(m.group(1))
                    expand(var[m.group(1)], depth + 1)
            elif tok.endswith(".img"):
                out.append(Path(tok).name)

    expand(var.get("SHIPIMGS", ""))
    return sorted(set(out))


def main():
    # **$(SHIPIMGS), NOT A GLOB OF build/.** The on-demand disks (c64, weave,
    # loom, wire) and a soak's leftovers live there too, and so do the images
    # a TEST WROTE by flushing a guest - which are a running machine's output
    # and not something `make` can make distinct. CLAUDE.md names this trap
    # about the 360KB set and it is the same one.
    names = ship_images()
    harness.check(len(names) >= 8, "the Makefile's $(SHIPIMGS) resolved",
                  why="this row is scoped to the images `make` ships; if the "
                      "list cannot be read the row is silently testing nothing",
                  got=len(names))
    imgs = {}
    for n in names:
        p = BUILD / n
        if not p.exists():
            continue                    # `make` has not built it yet
        raw = p.read_bytes()
        if len(raw) < 512 or raw[510:512] != b"\x55\xAA":
            continue                    # not a volume this kernel would mount
        imgs[n] = raw

    harness.check(len(imgs) >= 8,
                  "at least 8 built images to compare",
                  why="the row is vacuous on a tree that has not been built - "
                      "`make` first",
                  got=len(imgs))

    print("t_volsig: %d shipped volume(s) compared" % len(imgs))

    by_sig = {}
    for name, raw in imgs.items():
        by_sig.setdefault(sig(raw[:512]), []).append(name)

    for s, names in sorted(by_sig.items()):
        if len(names) < 2:
            continue
        bodies = {imgs[n][512:] for n in names}
        harness.check(
            len(bodies) == 1,
            "images signing %04X are one volume: %s" % (s, ", ".join(names)),
            why="dsk_bpb_sig (SPEC.md 18.8.2) cannot tell these disks apart, "
                "so SPEC.md 18.95's sector cache and SPEC.md 18.8's FAT window "
                "stay valid across a swap between them - the new disk is read "
                "with the old one's directory and FAT, and a write commits the "
                "old FAT onto it. os88disk.vol_id derives the serial from the "
                "volume's own content; a collision here means that derivation "
                "stopped reaching these images",
            got="%d different volumes" % len(bodies), want="1")

    # ...and the other half: identical content MUST still sign alike, which is
    # reproducibility stated where it can be checked.
    by_body = {}
    for name, raw in imgs.items():
        by_body.setdefault(raw[512:], []).append(name)
    for names in by_body.values():
        if len(names) < 2:
            continue
        sigs = {sig(imgs[n][:512]) for n in names}
        harness.check(
            len(sigs) == 1,
            "identical volumes sign alike: %s" % ", ".join(sorted(names)),
            why="two images with the same content are the same disk and must "
                "build the same bytes - a serial that varied between them "
                "would mean `make` is not reproducible",
            got=sorted("%04X" % x for x in sigs), want="one signature")

    harness.done("t_volsig")


if __name__ == "__main__":
    main()
