#!/usr/bin/env python3
"""Compress KERNEL.SYS's tail so stage 2 reads fewer sectors (SPEC.md 2.9.13).

    python3 tools/os88kz.py build/kernel.bin -o build/kernel.sys --json ...

**THE FILE IS THREE PARTS AND ONLY THE LAST IS PACKED.**

    [ BOOT2_PAD    the blob        ] stage 1 reads it, and it holds the loader
    [ KZ_HEAD      the plain head  ] the splash's first tick probes the adapter
    [ ...          LZ4 blocks      ] and this is what stage 2 expands
                                      into where the head ends

Each block is `word packed, word D + unpacked, word D/16, the stream, padding
to a paragraph`, and **no block produces more than KZ_BLK bytes** - so stage 2's
decoder never leaves a segment and needs none of kernel/lz.inc's crossing
machinery. MEASURED: one stream is 73,832 bytes and two blocks are 74,908, so
the whole cost of that simplification is **1,076 bytes, two sectors of the 42
it saves**. Machine code matches locally; docs/plans/O88-COMPRESSION-PLAN.md
13.4 measured the same split costing 44% on a MOD, whose sample data matches
back tens of KB.

**D IS THE BLOCK'S DICTIONARY** (SPEC.md 2.9.13.6): the D bytes of FINAL
output immediately below where the block lands, which its matches may reach
back into. Stage 2 points ES D bytes below the landing and starts DI at D, so
the block's last byte is DI = D + unpacked - which is why D + unpacked has to
fit a word and D is capped at 0xFFF0 less the block. That buys back most of
what cutting the tail into blocks cost: a block no longer starts on an empty
window. BLOCK 0'S D IS ZERO, ALWAYS, and so is every byte of the PLAIN HEAD:
stage 2 writes into the head before kz_all runs (`spl_fseg`'s segment half,
the boot timer at +12, boot_cylrun at +4, spl_tick's adapter variables, the
splash ISR's flags), so the head in memory is not the head the compressor saw,
and a match into it would copy the write. A dictionary inside the TAIL is safe
because nothing writes there until the kernel runs: it is all past
SPL_RESIDENT, and kz_all expands it in order.

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


HDR = 6                         # word packed, word D + unpacked, word D/16
BLK = 0xF000                    # KZ_BLK in boot/boot2.asm: the most one block
                                # may produce, so the decoder stays inside one
                                # segment. 61,440 rather than 65,536 because
                                # the destination advances by whole paragraphs
                                # and a block ending AT the boundary would put
                                # the next one's base at the segment after


def stage2(packed: bytes, blob: int, head: int, r: int, nblk: int) -> bytes:
    """What boot/boot2.asm's kz_all leaves at KERNEL_SEG, simulated IN PLACE
    on one array: the plain head at 0, the packed blocks R above its end,
    each block read through its own header (`packed, D + unpacked, D/16`)
    and expanded by kz_expand's own loop - literals, the `DI >= DX` stop,
    then a match copied a byte at a time as `rep movsb` copies it - into the
    same bytes the source sits in. So a header the loader would misread, a
    dictionary that is not what the encoder saw, or a margin too small for
    the source to stay ahead of the write pointer all fail HERE, on the
    host, rather than as a kernel with a wrong instruction in it. It shares
    nothing with the encoder but the file.

    THE HEAD IS SCRAMBLED FIRST, because stage 2 writes into it before
    kz_all runs (`spl_fseg`, the timer at +12, boot_cylrun at +4, the
    first tick's adapter variables): a block whose match reached into the
    head - a dictionary over it, or block 0 reaching below its own start -
    would copy those writes, and here it copies 0xCC instead and fails.
    Returns the expanded TAIL alone."""
    body = packed[blob + head:]
    mem = bytearray(max(head + r + len(body), head + nblk * BLK) + 16)
    mem[:head] = b"\xCC" * head
    src = head + r
    mem[src:src + len(body)] = body
    land = head
    for _ in range(nblk):
        zlen = mem[src] | mem[src + 1] << 8
        end = mem[src + 2] | mem[src + 3] << 8
        es = land - (mem[src + 4] | mem[src + 5] << 8) * 16
        si, di = src + HDR, (mem[src + 4] | mem[src + 5] << 8) * 16
        while True:
            tok = mem[si]; si += 1
            n = tok >> 4
            if n == 15:
                while True:
                    b = mem[si]; si += 1; n += b
                    if b != 255:
                        break
            for _k in range(n):
                mem[es + di] = mem[si]; si += 1; di += 1
            if di >= end:
                break
            off = mem[si] | mem[si + 1] << 8; si += 2
            n = tok & 15
            if n == 15:
                while True:
                    b = mem[si]; si += 1; n += b
                    if b != 255:
                        break
            for _k in range(n + 4):
                mem[es + di] = mem[es + di - off]; di += 1
        src += (zlen + HDR + 15) & ~15
        land += BLK
    return bytes(mem[head:land])


def build(image: bytes, blob: int, head: int, margin: int):
    """(the packed file, the numbers stage 2 needs)."""
    if len(image) <= blob + head:
        fail(f"the image is {len(image)} bytes, which is not past the blob "
             f"({blob}) and the plain head ({head}) - there is nothing to pack")
    if blob % 512 or head % 512:
        fail("the blob and the head are both read in whole SECTORS")
    tail = image[blob + head:]

    body, worst, need, blocks, sizes, dicts = bytearray(), 0, 0, [], [], []
    for at in range(0, len(tail), BLK):
        raw = tail[at:at + BLK]
        # THE DICTIONARY (SPEC.md 2.9.13.6): the tail's own bytes below the
        # landing, as many as keep D + unpacked inside the segment kz_expand
        # addresses, in whole paragraphs because ES carries it - and NONE for
        # block 0, whose bytes below are the plain head stage 2 has written
        # into. `at` caps it at the tail's own start, so no block's dictionary
        # reaches the head however small a later block is
        d = 0 if at == 0 else min(at, 0xFFF0 - len(raw)) & ~15
        if d and (d > at or d + len(raw) > 0xFFFF):
            fail(f"block at {at}: a dictionary of {d} leaves the tail or the "
                 f"segment")
        # THE CLASSIC BLOCK, with no raw tail (SPEC.md 20.13.7): kz_expand is
        # boot/boot2.asm's own copy of the LZ4 arm and reads the ending a
        # standard block has, so this is the one stream in the tree that still
        # carries an in-place margin, and KZ_MARGIN is what reserves it
        if d:
            z = os88lz.lz4_compress(tail[at - d:at + len(raw)], tail=False,
                                    prefix=d)
        else:
            z = os88lz.lz4_compress(raw, tail=False)
        if os88lz.lz4_decompress(z, len(raw), tail=False,
                                 prefix=tail[at - d:at]) != raw:
            fail("lz4 did not round-trip the kernel - tools/os88lz.py is the "
                 "reference boot/boot2.asm copies, so this is a bug there")
        # ...and the in-place margin OF THAT STREAM. A dictionary moves no
        # pointer the margin is about: the reads it adds are of FINAL bytes
        # below the landing, and the race is between the write pointer and
        # the unread source ABOVE it - so the measurement is the same
        # simulation over the stream that ships, not over a plain packing
        m = os88lz.in_place_margin(raw, os88lz.LZ4, packed=z, tail=False)
        worst = max(worst, m)
        # A BLOCK BUT THE LAST MUST SHRINK: kz_all lands block i at head +
        # i * KZ_BLK, so every one before the last is exactly BLK and always
        # does. The LAST is whatever is left, and a remainder of a few dozen
        # bytes can come out LONGER than it went in - MEGATRAP=1's kern_mega
        # did, 56 bytes to 58, after the merge of two size passes. That is
        # legitimate: what in-place expansion needs is the GAP, and R is ours
        # to set, so the deficit is added to it below (kern_mega size pass 1)
        if len(z) + HDR >= len(raw) and at + BLK < len(tail):
            fail(f"block at {at} does not get smaller ({len(z)} vs {len(raw)})")
        start = len(body)
        body += (len(z).to_bytes(2, "little")
                 + (d + len(raw)).to_bytes(2, "little")
                 + (d // 16).to_bytes(2, "little"))
        body += z
        pad = -len(body) % 16
        body += b"\xE5" * pad                   # ...to a paragraph, so the
        blocks.append((len(z), len(raw)))        # next block's SEGMENT is exact
        dicts.append(d)
        sizes.append((len(raw), len(body) - start))
        need = max(need, m + pad)
    # THE MARGIN IS PER BLOCK, PLUS ITS PADDING. Block i's header sits gap(i)
    # above its landing, and the telescoping below leaves gap(i) >= (unpacked
    # - header - stream - padding) of block i itself plus the margin, the
    # blocks after it adding a non-negative amount (EXTRA sees to that). The
    # decoder needs gap(i) + header >= (unpacked - stream) + its measured
    # margin, so the header cancels and what is left is that the margin must
    # cover the block's own figure PLUS ITS PADDING - at most 15 bytes. (This
    # said "plus the 4-byte header" while the header was 4; it was never the
    # header, and the 64 reserved covered either reading.)
    if need > margin:
        fail(f"a block needs {need} bytes of in-place margin (its measured "
             f"{worst} plus its padding) and stage 2 reserves {margin}")
    # ...and that telescoping assumed every suffix of blocks saves bytes. Where
    # one does not (a last block that grew), gap(i) falls short of the margin
    # by that suffix's deficit, so R carries it: EXTRA is 0 - and the file
    # byte-identical - for every kernel whose blocks all shrink
    extra, suffix = 0, 0
    for u, p in reversed(sizes):
        suffix += u - p
        extra = max(extra, -suffix)
    # ROUNDED TO A SECTOR AND NOT A PARAGRAPH. read_run's third bound divides
    # the destination's physical address by 512 to find the DMA page's end,
    # and its own comment rests on every destination being 512-ALIGNED - so an
    # R that is merely paragraph-aligned makes that shift truncate, and a run
    # of ZERO sectors is an infinite loop rather than a wrong picture. It cost
    # a boot to find. The extra bytes are at most 496, once.
    r = (len(tail) - len(body) + margin + extra + 511) & ~511
    out = image[:blob + head] + bytes(body)
    try:
        got = stage2(out, blob, head, r, len(blocks))
    except IndexError:          # a header or a match that left the array
        got = b""
    if got[:len(tail)] != tail:
        bad = next((i for i in range(len(tail))
                    if i >= len(got) or got[i] != tail[i]), len(tail))
        fail(f"kz_all, simulated in place over the file just written, does "
             f"not give back the tail (first wrong byte at {head + bad:#x} "
             f"past KERNEL_SEG) - the header, a dictionary or the margin is "
             f"wrong, or a block reaches into the plain head")
    return out, {
        "image": len(image),
        "blob": blob,
        "head": head,
        "unpacked_tail": len(tail),
        "packed_tail": len(body),
        "blocks": blocks,
        "dicts": dicts,
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
                    help="the plain head, max(SPL_RESIDENT, KZ_HEADMIN) * 512: the sectors the splash's first "
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
