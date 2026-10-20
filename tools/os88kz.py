#!/usr/bin/env python3
"""Compress KERNEL.SYS's tail so stage 2 reads fewer sectors (SPEC.md 2.9.13).

    python3 tools/os88kz.py build/kernel.bin -o build/kernel.sys --json ...

**THE FILE IS THREE PARTS AND ONLY THE LAST IS PACKED.**

    [ BOOT2_PAD    the blob        ] stage 1 reads it, and it holds the loader
    [ KZ_HEAD      the plain head  ] the splash's first tick probes the adapter
    [ ...          LZ4 blocks      ] and this is what stage 2 expands
                                      into where the head ends

Each block is `word packed, word unpacked, the stream, padding to a paragraph`,
and **no block produces more than KZ_BLK bytes** - so stage 2's decoder never
leaves a segment and needs none of kernel/lz.inc's crossing machinery. MEASURED:
one stream is 73,832 bytes and two blocks are 74,908, so the whole cost of that
simplification is **1,076 bytes, two sectors of the 42 it saves**. Machine code
matches locally; docs/plans/O88-COMPRESSION-PLAN.md 13.4 measured the same split
costing 44% on a MOD, whose sample data matches back tens of KB.

The blob cannot be packed because it is what does the unpacking. The head
cannot be packed because `spl_tick` far-calls viddet.inc while the rest of the
kernel is still landing (SPEC.md 15, SPL_RESIDENT) - so those sectors have to
be at their linked addresses during the read, not after it.

**THE STREAM IS UNBOUNDED LZ4 AND THAT IS A DECISION** (SPEC.md 2.9.13.2). Every
other decoder in this system is bounds-checked because every byte off a disk is
hostile (SPEC.md 19); this one is not, because the thing it produces is the
kernel and the alternative to expanding it wrongly is JUMPING INTO IT wrongly.
A bound cannot make that safe and 18.93.1's canary already covers the failure
that actually happens - a BIOS that returns the wrong head's sectors - by
checking the TRANSFER, before a byte is decoded.

The tail expands IN PLACE: stage 2 reads it R bytes above where it belongs and
walks down onto it, exactly as the package loader does (SPEC.md 20.13). R is
printed here and injected into the loader, because the loader cannot compute
what it has not read yet.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88lz                                                 # noqa: E402


def fail(msg):
    sys.exit("os88kz: " + msg)


BLK = 0xF000                    # KZ_BLK in boot/boot2.asm: the most one block
                                # may produce, so the decoder stays inside one
                                # segment. 61,440 rather than 65,536 because
                                # the destination advances by whole paragraphs
                                # and a block ending AT the boundary would put
                                # the next one's base at the segment after


def build(image: bytes, blob: int, head: int, margin: int):
    """(the packed file, the numbers stage 2 needs)."""
    if len(image) <= blob + head:
        fail(f"the image is {len(image)} bytes, which is not past the blob "
             f"({blob}) and the plain head ({head}) - there is nothing to pack")
    if blob % 512 or head % 512:
        fail("the blob and the head are both read in whole SECTORS")
    tail = image[blob + head:]

    body, worst, blocks = bytearray(), 0, []
    for at in range(0, len(tail), BLK):
        raw = tail[at:at + BLK]
        # THE CLASSIC BLOCK, with no raw tail (SPEC.md 20.13.7): kz_expand is
        # boot/boot2.asm's own copy of the LZ4 arm and reads the ending a
        # standard block has, so this is the one stream in the tree that still
        # carries an in-place margin, and KZ_MARGIN is what reserves it
        z = os88lz.compress(raw, os88lz.LZ4, tail=False)
        if os88lz.decompress(z, os88lz.LZ4, len(raw), tail=False) != raw:
            fail("lz4 did not round-trip the kernel - tools/os88lz.py is the "
                 "reference boot/boot2.asm copies, so this is a bug there")
        m = os88lz.in_place_margin(raw, os88lz.LZ4, tail=False)
        worst = max(worst, m)
        if len(z) + 4 >= len(raw):
            fail(f"block at {at} does not get smaller ({len(z)} vs {len(raw)})")
        body += len(z).to_bytes(2, "little") + len(raw).to_bytes(2, "little")
        body += z
        body += b"\xE5" * (-len(body) % 16)     # ...to a paragraph, so the
        blocks.append((len(z), len(raw)))        # next block's SEGMENT is exact
    # THE MARGIN IS PER BLOCK PLUS THE HEADER. The telescoping below leaves
    # gap(i) >= sum over the blocks from i on of (unpacked - packed - 4), plus
    # the margin - so the smallest gap is at the LAST block and is the margin
    # itself, and what a block needs inside itself is its own measured figure
    # plus those 4 bytes.
    if worst + 4 > margin:
        fail(f"a block needs {worst} bytes of in-place margin and stage 2 "
             f"reserves {margin} less the 4-byte header")
    # ROUNDED TO A SECTOR AND NOT A PARAGRAPH. read_run's third bound divides
    # the destination's physical address by 512 to find the DMA page's end,
    # and its own comment rests on every destination being 512-ALIGNED - so an
    # R that is merely paragraph-aligned makes that shift truncate, and a run
    # of ZERO sectors is an infinite loop rather than a wrong picture. It cost
    # a boot to find. The extra bytes are at most 496, once.
    r = (len(tail) - len(body) + margin + 511) & ~511
    out = image[:blob + head] + bytes(body)
    return out, {
        "image": len(image),
        "blob": blob,
        "head": head,
        "unpacked_tail": len(tail),
        "packed_tail": len(body),
        "blocks": blocks,
        "nblk": len(blocks),
        "margin": worst,
        "r": r,
        "rpara": r // 16,
        # what stage 2 reads: everything past the blob, in whole sectors
        "ksecs": (len(out) - blob + 511) // 512,
        "ksecs_plain": (len(image) - blob + 511) // 512,
        "headsecs": head // 512,
        "file": len(out),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("input", metavar="KERNEL.bin")
    ap.add_argument("-o", "--output", metavar="OUT", required=True)
    ap.add_argument("--blob", type=int, required=True,
                    help="BOOT2_PAD: the bytes stage 1 already has")
    ap.add_argument("--head", type=int, required=True,
                    help="SPL_RESIDENT * 512: the sectors the splash's first "
                         "tick reaches, which stay plain")
    ap.add_argument("--margin", type=int, default=64,
                    help="KZ_MARGIN in boot/boot2.asm")
    ap.add_argument("--json", metavar="OUT.json")
    ap.add_argument("--defines", action="store_true",
                    help="print the nasm -D line instead of a report")
    a = ap.parse_args()

    out, n = build(open(a.input, "rb").read(), a.blob, a.head, a.margin)
    open(a.output, "wb").write(out)
    if a.json:
        json.dump(n, open(a.json, "w"), indent=1)
    if a.defines:
        print("-DKZ_SECS=%d -DKZ_RPARA=%d -DKZ_HEADSEC=%d -DKZ_NBLK=%d"
              % (n["ksecs"], n["rpara"], n["headsecs"], n["nblk"]))
        return 0
    print("os88kz: %s %d -> %d bytes, stage 2 reads %d sectors instead of %d "
          "(%d fewer); tail %d -> %d (%.1f%%) in %d block(s), margin %d, R %d"
          % (os.path.basename(a.output), n["image"], n["file"], n["ksecs"],
             n["ksecs_plain"], n["ksecs_plain"] - n["ksecs"],
             n["unpacked_tail"], n["packed_tail"],
             100.0 * n["packed_tail"] / n["unpacked_tail"], n["nblk"],
             n["margin"], n["r"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
