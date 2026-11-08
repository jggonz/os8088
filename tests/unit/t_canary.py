#!/usr/bin/env python3
"""SPEC.md 18.93.1 - the boot canary must sit in a sector that a transfer run
reads AFTER A HEAD FLIP, in a run that began BEFORE it, on every shipped
system disk.

This is the row that exists because the first version of the canary did not.
Offset 16384 is "past the first head flip" in every geometry and is still in
the FIRST half of a transfer run in every one - the half that loads correctly
on exactly the machine the canary exists to catch. It passed there, and the
kernel it let through was scrambled.

THE BAND IS AN INTERSECTION, so a NEW GEOMETRY can invalidate an offset that
nothing touched.  Adding SPEC.md 19's 1.2MB 5.25" disk did exactly that, and
this row caught it - which it can only keep doing if every shipped system image
is in IMAGES below.

**AND WHERE A RUN BEGINS IS THE LOADER'S BUSINESS, NOT THE GEOMETRY'S** - which
is what this row did not know for two months (the big review's BR-1,
2026-11-08). It walked cylinder-aligned runs from the DATA AREA, so it answered
"crosses" for file sector 21 on every disk. Stage 2 does not read that way.
Under KZIP (the default since 2026-09-05) it starts at KERNEL.SYS + the blob,
reads the KZ_HEADSEC plain sectors in runs of their own (at the TRACK bound
since BR-1's part 2), and starts the packed tail afresh behind them, R
paragraphs further up - and every run is cut at the 64KB DMA page of its
destination as well as at the cylinder. On a 9-sector disk file sector 21 was
in the head-1 half of cylinder 1, and every run containing it began after that
cylinder's flip: the canary was INERT on the 360KB and 720KB disks of both
shipped kernels while this row passed. So the walk below is the loader's own,
per kernel - its blob length, KZ_HEADSEC, R and segment out of that kernel's
kernel.kz.json - and the question is the precise one: is there a head flip
strictly after the start of the canary's run and at or before the canary?

It is AN INDEPENDENT READER (the big review's boot-10): it imports nothing the
build uses to place the canary, and it reads the injected signature back out of
each disk's own boot sector (`mov di, KSIG`) and sums that disk's own
KERNEL.SYS sector to find it, so what it walks is the sector the build really
armed, not the one the Makefile's arithmetic says it armed.

boot/boot2.asm types KSIG_OFF out a second time, and there is no linker here,
so the two are checked against each other before anything else is asked.

Break it on purpose: KSIG_OFF back at 4096 (file sector 21) must turn
os8088-360, os8088-720, small360 and small720 red.
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
# (image, kernel tree under build/) - every SHIPPED system disk. The 128KB
# machine's (SPEC.md 24.5.7) are kern_small's, so they are walked with that
# kernel's own blob and packed layout. small.img (1.44MB, on demand) is not
# shipped and not here
IMAGES = [("os8088-360.img", ""), ("os8088-720.img", ""),
          ("os8088-120.img", ""), ("os8088.img", ""),
          ("small360.img", "smallk"), ("small720.img", "smallk")]
# ...and kern_mega's, which `make mega` builds and `all` does not: walked when
# present, with the run model only - its boot sectors are left to its own
# rows, because a tree that has run `make` since its last `make mega` holds
# a mega image armed for an older KSIG_OFF, and that is not a defect
MEGA = [("mega.img", "megak"), ("mega120.img", "megak")]
KERNEL_SEG = 0x0060


def ksig_off():
    m = re.search(r"^KSIG_OFF\s*:=\s*(\d+)", (ROOT / "Makefile").read_text(), re.M)
    if not m:
        sys.exit("t_canary: no KSIG_OFF in the Makefile")
    off = int(m.group(1))
    b2 = re.search(r"^KSIG_OFF\s+equ\s+(\d+)",
                   (ROOT / "boot" / "boot2.asm").read_text(), re.M)
    if not b2:
        sys.exit("t_canary: no KSIG_OFF equ in boot/boot2.asm")
    if int(b2.group(1)) != off:
        sys.exit(f"t_canary: the Makefile says KSIG_OFF {off} and "
                 f"boot/boot2.asm says {b2.group(1)} - the build would inject "
                 f"the word from one offset and the sector compare the other")
    return off


def shipped_blob():
    """kern_big's blob length, the literal KSIG_OFF is written against: the
    canary's FILE sector is KSIG_OFF/512 + this, on every kernel (the
    Makefile's per-arm KSIG_OFF moves the memory offset with the blob)."""
    src = (ROOT / "kernel" / "kernel.asm").read_text()
    m = re.search(r"^BOOT2_SECS\s+equ\s+(\d+)", src, re.M)
    if not m:
        sys.exit("t_canary: no BOOT2_SECS in kernel/kernel.asm")
    return int(m.group(1))


def kz(sub):
    p = BUILD / sub / "kernel.kz.json"
    return json.loads(p.read_text()) if p.exists() else None


def bpb(img):
    d = img.read_bytes()[:512]
    rsvd = struct.unpack_from("<H", d, 14)[0]
    nfat, fatsz = d[16], struct.unpack_from("<H", d, 22)[0]
    rootent = struct.unpack_from("<H", d, 17)[0]
    spt, heads = struct.unpack_from("<H", d, 24)[0], struct.unpack_from("<H", d, 26)[0]
    data = rsvd + nfat * fatsz + (rootent + 15) // 16   # boot.asm's derivation
    return d, data, spt, heads


def walk(lba0, spt, heads, k, nokzip_secs=None):
    """The runs (lba, count, which) stage 2 asks for on an 8088, which takes
    the CYLINDER bound (boot2.asm's gate): read_run cuts at the first of the
    run bound, the sectors still wanted and the 64KB DMA page of the
    destination. Under KZIP the head is read first at the TRACK (BR-1 part 2)
    and the tail restarts behind it, R paragraphs further up."""
    out = []

    def load(lba, dest, left, runmax, which):
        while left:
            n = min(runmax - lba % runmax, left)
            page = (-((dest << 4) & 0xFFFF)) & 0xFFFF
            if page:
                n = min(n, page >> 9)
            out.append((lba, n, which))
            lba += n
            left -= n
            dest += n * 32
        return lba, dest

    lba = lba0 + k["blob"] // 512 if k else lba0
    if k is None:
        load(lba, KERNEL_SEG, nokzip_secs, spt * heads, "plain")
        return out
    lba, dest = load(lba, KERNEL_SEG, k["headsecs"], spt, "head")
    load(lba, dest + k["rpara"], k["ksecs"] - k["headsecs"], spt * heads, "tail")
    return out


def live(runs, c, spt):
    """Is LBA `c` read after a head flip, inside a run that began before it?
    A run never crosses a cylinder, so any track boundary in (start, c] is a
    head flip."""
    for lba, n, which in runs:
        if lba <= c < lba + n:
            return (c // spt != lba // spt), (lba, n, which)
    return False, None


def ssum(b):
    return sum(int.from_bytes(b[i:i + 2], "little")
               for i in range(0, 512, 2)) & 0xFFFF


def main():
    off = ksig_off()                     # kern_big's MEMORY offset
    fsec = off // 512 + shipped_blob()   # ...its FILE sector, on every kernel
    if off % 512:
        sys.exit(f"t_canary: KSIG_OFF {off} is not a whole sector")
    fileoff = fsec * 512

    missing = [n for n, _ in IMAGES if not (BUILD / n).exists()]
    if missing:
        sys.exit("t_canary: shipped image(s) not built: %s - run make first; "
                 "a geometry this cannot see is a geometry the canary is not "
                 "checked on" % ", ".join(missing))

    bad, lines = [], []
    for name, sub in IMAGES + [m for m in MEGA if (BUILD / m[0]).exists()]:
        img = BUILD / name
        k = kz(sub)
        kf = BUILD / sub / "kernel.sys"
        if not kf.exists():
            sys.exit(f"t_canary: no {kf.relative_to(ROOT)} - run make first")
        d, data, spt, heads = bpb(img)
        if heads < 2:
            bad.append(f"{name}: one head - nothing to cross")
            continue
        raw = img.read_bytes()
        c = data + fsec
        if name not in dict(MEGA):
            # THE SECTOR THE BUILD REALLY ARMED: the boot sector's `mov di,
            # KSIG` must name this disk's own file sector `fsec`
            s = ssum(raw[c * 512:c * 512 + 512])
            hits = [i for i in range(0, 509)
                    if d[i] == 0xBF and d[i + 1] | (d[i + 2] << 8) == s]
            if len(hits) != 1:
                bad.append(f"{name}: its boot sector has {len(hits)} `mov di, "
                           f"0x{s:04X}`, the sum of file sector {fsec} - the "
                           f"canary the build injected is not the sector this "
                           f"row walks (KSIG_OFF {off} against the Makefile's "
                           f"per-kernel arms and KSIGDEF2)")
                continue
        if k is None:
            # NOKZIP: one plain read of the rest of the file behind the blob,
            # whose length is the map's BOOT2_PAD
            m = re.search(r"^([0-9A-F]{8})\s+BOOT2_PAD\s*$",
                          (BUILD / sub / "kernel.map").read_text(errors="replace"),
                          re.M)
            if not m:
                sys.exit(f"t_canary: no kernel.kz.json and no BOOT2_PAD in "
                         f"build/{sub + '/' if sub else ''}kernel.map")
            blob = int(m.group(1), 16) // 512
            r = walk(data + blob, spt, heads, None,
                     (kf.stat().st_size + 511) // 512 - blob)
        else:
            r = walk(data, spt, heads, k)
        ok, run = live(r, c, spt)
        if run is None:
            bad.append(f"{name}: file sector {fsec} (LBA {c}) is past the end "
                       f"of stage 2's read")
            continue
        lba, n, which = run
        if which == "head":
            bad.append(f"{name}: file sector {fsec} (LBA {c}) is in the KZIP "
                       f"plain head, which is read at the track bound and "
                       f"crosses nothing")
            continue
        lines.append(f"{name} LBA {c} in {which} run {lba}..{lba + n - 1}")
        if not ok:
            bad.append(f"{name} ({sub or 'kern_big'}, blob "
                       f"{(k or {}).get('blob', 0) // 512} sectors): file sector "
                       f"{fsec} is LBA {c}, in the run {lba}..{lba + n - 1}, "
                       f"which crosses no head before it")
    if bad:
        sys.exit(
            f"t_canary: KSIG_OFF {off} (file sector {fsec}) is NOT read after a "
            f"head flip on:\n  " + "\n  ".join(bad) + "\n- it would be loaded "
            f"CORRECTLY on a machine whose FDC flips at the wrong sector, and "
            f"the canary would pass on the one fault it is for (SPEC.md 18.93.1)"
        )

    # ...and it has to be tellable from its neighbours, or a shifted read
    # matches - in EVERY kernel's file that boots off a floppy. The signature
    # is the SECTOR'S SUM (the Makefile's KSIGDEF2): kern_mega's first WORD
    # there was 0, and so was the first word of the wrong sector a 286 ROM
    # delivered in its place, which is how that sector passed the canary
    # (SPEC.md 18.93.1)
    kern = BUILD / "kernel.sys"
    for kf in (kern, BUILD / "smallk" / "kernel.sys",
               BUILD / "megak" / "kernel.sys"):
        if not kf.exists():
            if kf.parent.name == "megak":
                continue            # `make mega` is on demand
            sys.exit(f"t_canary: no {kf.relative_to(ROOT)} - run make first")
        kk = kf.read_bytes()
        if len(kk) < fileoff + 512:
            sys.exit(f"t_canary: {kf.relative_to(ROOT)} is {len(kk)} bytes, "
                     f"shorter than the canary's file offset {fileoff}")
        for step in (-512, 512):
            if 0 <= fileoff + step and fileoff + step + 512 <= len(kk) \
                    and ssum(kk[fileoff:fileoff + 512]) == \
                    ssum(kk[fileoff + step:fileoff + step + 512]):
                sys.exit(
                    f"t_canary: {kf.name} under {kf.parent.name}/: the sector "
                    f"at {fileoff} sums the same as the one at "
                    f"{fileoff + step}, so a read shifted by a sector still "
                    f"matches (SPEC.md 18.93.1)"
                )
    k = kern.read_bytes()
    print(f"canary: memory offset {off}, file sector {fsec}, read after a head "
          f"flip on {len(lines)} system disks ({'; '.join(lines)}), sum "
          f"0x{ssum(k[fileoff:fileoff + 512]):04X} distinct from both "
          f"neighbours")


main()
