#!/usr/bin/env python3
"""os88partpad.py - what the parts standard's alignment costs a disk.

docs/plans/completed/PART-PACKING-PLAN.md's instrument. Every part of a parted
`.O88` (SPEC.md 20.12.2) starts on a granule of the file - a 16-byte
paragraph, or a 512-byte sector in a file packed before SPEC.md 20.12.2.1 -
so between the end of the image's stored bytes and part 0, and between each
part's STORED bytes (the packed length on an OP_COMP row) and the next part,
there is padding the packer fills with 0xE5. This walks every FAT12/16 image
named, recursively, finds every file whose image carries a part table (either
magic), and reports per file:

  pad     bytes of padding the file carries (the last part is never padded)
  tight   the file's length with every part laid back to back (PACKED) or
          on a 16-byte boundary (PARA) - the two layouts the plan prices
  clus    clusters the file takes now, and with each layout

and per disk the clusters a layout would return. Read-only: it never writes.

    python3 tools/os88partpad.py build/*.img
    python3 tools/os88partpad.py --files build/kdos/DOS.O88 build/c64.o88
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88pkg                      # noqa: E402
import os88parts                    # noqa: E402
from os88fat import Fat12           # noqa: E402

OP_COMP = os88parts.EQU["OP_COMP"]


def layout(raw):
    """(stored image bytes, [(start, stored)] per filed part) or None."""
    if len(raw) < 12 or raw[:2] != b"O8" or not raw[3] & 4:
        return None
    try:
        rows = os88parts.image_rows(raw)
    except (ValueError, Exception):
        return None
    if raw[3] & os88pkg.PKG_COMP_BIT:
        img = struct.unpack_from("<H", raw, 4)[0]       # the link word
    else:
        img = struct.unpack_from("<H", raw, 8)[0]
    g = os88parts.gran(os88pkg.image_unwrap(raw))
    parts = []
    for r in rows:
        if not r["off"]:
            continue
        stored = r["zkb"] if r["flags"] & OP_COMP else r["len"]
        parts.append((r["off"] * g, stored))
    if not parts:
        return None
    parts.sort()
    return img, parts


def tight(img, parts, align):
    at = img
    for _, stored in parts:
        at = (at + align - 1) // align * align + stored
    return at


def pad_of(raw, img, parts):
    pad, at = 0, img
    for start, stored in parts:
        pad += start - at
        at = start + stored
    return pad + (len(raw) - at)


def walk(fat, first_dir=None, path=""):
    """Yield (path, bytes) for every file, recursively (FAT12/16 root +
    subdirectories)."""
    csz = fat.spc * fat.bps

    def entries(buf):
        for o in range(0, len(buf), 32):
            e = buf[o:o + 32]
            if len(e) < 32 or e[0] == 0:
                return
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 8:
                continue
            yield e

    if first_dir is None:
        buf = bytes(fat.img[fat.root_off:fat.root_off + fat.nroot * 32])
    else:
        buf = b"".join(bytes(fat.img[fat.cluster_off(c):fat.cluster_off(c)
                                     + csz]) for c in fat.chain(first_dir))
    for e in entries(buf):
        name = Fat12.pretty(e[:11])
        if name in (".", ".."):
            continue
        first = struct.unpack_from("<H", e, 26)[0]
        if e[11] & 0x10:
            if first:
                yield from walk(fat, first, path + name + "/")
            continue
        size = struct.unpack_from("<I", e, 28)[0]
        if not first:
            continue
        body = b"".join(bytes(fat.img[fat.cluster_off(c):fat.cluster_off(c)
                                      + csz]) for c in fat.chain(first))
        yield path + name, body[:size]


def clus(n, csz):
    return (n + csz - 1) // csz


def report(name, raw, csz, totals):
    lay = layout(raw)
    if lay is None:
        return
    img, parts = lay
    pad = pad_of(raw, img, parts)
    tp = tight(img, parts, 1)
    t16 = tight(img, parts, 16)
    now, cp, c16 = clus(len(raw), csz), clus(tp, csz), clus(t16, csz)
    print(f"  {name:28} {len(parts):2} parts {len(raw):7} B  pad {pad:5}  "
          f"packed {tp:7} para {t16:7}  clus {now:4} -> {cp:4} / {c16:4}"
          f"  (-{now - cp} / -{now - c16})")
    totals[0] += pad
    totals[1] += now - cp
    totals[2] += now - c16
    totals[3] += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("imgs", nargs="*")
    ap.add_argument("--files", nargs="*", default=[])
    ap.add_argument("--cluster", type=int, default=512)
    a = ap.parse_args()
    grand = [0, 0, 0, 0]
    for f in a.files:
        raw = open(f, "rb").read()
        t = [0, 0, 0, 0]
        report(os.path.basename(f), raw, a.cluster, t)
        for k in range(4):
            grand[k] += t[k]
    for img in a.imgs:
        try:
            fat = Fat12(img)
        except Exception as e:      # not a floppy image (a hard disk, a ROM)
            print(f"{img}: skipped ({e.__class__.__name__})")
            continue
        csz = fat.spc * fat.bps
        print(f"{img}: {csz}-byte clusters, {len(fat.free_clusters())} free")
        t = [0, 0, 0, 0]
        for name, raw in walk(fat):
            report(name, raw, csz, t)
        if t[3]:
            print(f"  = {t[3]} parted files, {t[0]} bytes of padding, "
                  f"{t[1]} clusters back packed, {t[2]} back at 16")
        for k in range(4):
            grand[k] += t[k]
    print(f"ALL: {grand[3]} parted files, {grand[0]} bytes of padding, "
          f"{grand[1]} clusters back packed, {grand[2]} back at 16")


if __name__ == "__main__":
    main()
