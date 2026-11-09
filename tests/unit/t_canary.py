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
paragraphs further up - and every run was cut at the 64KB DMA page of its
destination as well as at the cylinder (until SPEC.md 2.9.13.7 met the page
with a bounce instead, which takes the page out of the walk: a run is the
cylinder and what is left, wherever it lands). On a 9-sector disk file sector 21 was
in the head-1 half of cylinder 1, and every run containing it began after that
cylinder's flip: the canary was INERT on the 360KB and 720KB disks of both
shipped kernels while this row passed. So the walk below is the loader's own,
per kernel - its blob length, KZ_HEADSEC, R and segment out of that kernel's
kernel.kz.json - and the question is the precise one: is there a head flip
strictly after the start of the canary's run and at or before the canary?

It is AN INDEPENDENT READER (the big review's boot-10): since then the BUILD
CHOOSES the sector - tools/os88ksig.py walks the runs per build arm and writes
$(BUILD)/ksig.inc - and this row imports nothing from it. It reads the number
each kernel was really ASSEMBLED with out of that kernel's own map (KSIG_OFF,
an absolute equ), the signature the build injected out of each disk's own boot
sector (`mov di, KSIG`), the geometry out of each disk's own BPB, and walks the
runs with its own code. A generator and a gate that share a model share its
mistakes; BR-1 was exactly that.

AND IT HOLDS EVERY DISK TO THE DMA BOUNCE (SPEC.md 2.9.13.7): read_run reads a
run the 64KB page would cut whole to the next page's start, and boot2.asm's
guards and BLOB_LIFT are sized for B2_BMAXSEC sectors of it - so a shipped disk
whose BPB gives a longer cylinder fails here, read off that kernel's map.
Break that on purpose with B2_BMAXSEC read as 30: the three 1.44MB disks fail.

Break it on purpose: make os88ksig.py's `live()` answer True for every sector
(so it chooses the first tail sector, file sector 22 on kern_big) and rebuild -
this row must go red on every system disk.
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
          ("small360.img", "smallk"), ("small720.img", "smallk"),
          # the sound debug disk (SPEC.md 127): the system disk's boot and
          # kernel at the system disk's LBA, on all four geometries
          ("debug-sound-360.img", ""), ("debug-sound-720.img", ""),
          ("debug-sound-120.img", ""), ("debug-sound.img", "")]
# ...and kern_mega's and kern_emu's, which `make mega` and `make emu` build
# and `all` does not: walked IN FULL when the image carries its tree's
# current KERNEL.SYS, and named as stale otherwise - a tree that has run
# `make` since its last `make mega` holds a mega image of an older kernel,
# and that is not a defect of this one
OPTIONAL = [("mega.img", "megak"), ("mega120.img", "megak"),
            ("emu.img", "emuk")]
KERNEL_SEG = 0x0060


def mapnum(sub, name):
    """An absolute equ out of build/<sub>/kernel.map's No Section table - the
    number the kernel was ASSEMBLED with, not what any source says."""
    p = BUILD / sub / "kernel.map"
    if not p.exists():
        sys.exit(f"t_canary: no {p.relative_to(ROOT)} - run make first")
    m = re.search(r"^([0-9A-F]{8})\s+%s\s*$" % name,
                  p.read_text(errors="replace"), re.M)
    if not m:
        sys.exit(f"t_canary: {p.relative_to(ROOT)} lists no {name}")
    return int(m.group(1), 16)


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
    run bound and the sectors still wanted - the 64KB DMA page is a BOUNCE
    there, not a bound (SPEC.md 2.9.13.7), so it cuts nothing. Under KZIP the
    head is read first at the TRACK (BR-1 part 2) and the tail restarts
    behind it, R paragraphs further up."""
    out = []

    def load(lba, left, runmax, which):
        while left:
            n = min(runmax - lba % runmax, left)
            out.append((lba, n, which))
            lba += n
            left -= n
        return lba

    lba = lba0 + k["blob"] // 512 if k else lba0
    if k is None:
        load(lba, nokzip_secs, spt * heads, "plain")
        return out
    lba = load(lba, k["headsecs"], spt, "head")
    load(lba, k["ksecs"] - k["headsecs"], spt * heads, "tail")
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


def kernel_on(raw, data, kf):
    """Does the image carry THIS tree's KERNEL.SYS, from the data area on?"""
    k = kf.read_bytes()
    return raw[data * 512:data * 512 + len(k)] == k


def main():
    missing = [n for n, _ in IMAGES if not (BUILD / n).exists()]
    if missing:
        sys.exit("t_canary: shipped image(s) not built: %s - run make first; "
                 "a geometry this cannot see is a geometry the canary is not "
                 "checked on" % ", ".join(missing))

    bad, lines, stale, trees = [], [], [], {}
    for name, sub in IMAGES + [m for m in OPTIONAL if (BUILD / m[0]).exists()]:
        img = BUILD / name
        k = kz(sub)
        kf = BUILD / sub / "kernel.sys"
        if not kf.exists():
            if (name, sub) in OPTIONAL:
                continue
            sys.exit(f"t_canary: no {kf.relative_to(ROOT)} - run make first")
        off = mapnum(sub, "KSIG_OFF")       # what the kernel ASSEMBLED
        blob = mapnum(sub, "BOOT2_PAD") // 512
        if off % 512:
            bad.append(f"{name}: KSIG_OFF {off} is not a whole sector")
            continue
        fsec = off // 512 + blob
        trees[sub] = (kf, off, fsec)
        d, data, spt, heads = bpb(img)
        raw = img.read_bytes()
        if not kernel_on(raw, data, kf):
            if (name, sub) in OPTIONAL:
                # `make mega`/`make emu` are on demand: a tree that has run
                # `make` since holds an image of an OLDER kernel, which is not
                # a defect of this one
                stale.append(name)
                continue
            bad.append(f"{name}: does not carry build/{sub + '/' if sub else ''}"
                       f"kernel.sys from its data area (LBA {data}) - the walk "
                       f"below would be about another kernel")
            continue
        if heads < 2:
            bad.append(f"{name}: one head - nothing to cross")
            continue
        # ...AND THE BOUNCE HOLDS THIS DISK'S LONGEST RUN (SPEC.md 2.9.13.7).
        # read_run reads a run the DMA page would cut to that page's start,
        # and boot2.asm's guards and BLOB_LIFT are sized for B2_BMAXSEC
        # sectors of it - the cylinder of the largest geometry. A disk whose
        # BPB says more would bounce past what they proved
        bmax = mapnum(sub, "B2_BMAXSEC")
        if spt * heads > bmax:
            bad.append(f"{name}: a cylinder of {spt * heads} sectors, and "
                       f"stage 2's DMA bounce is sized for {bmax} "
                       f"(B2_BMAXSEC, kernel.asm)")
            continue
        c = data + fsec
        # THE SECTOR THE BUILD REALLY ARMED: the boot sector's `mov di, KSIG`
        # must name this disk's own file sector `fsec`
        s = ssum(raw[c * 512:c * 512 + 512])
        hits = [i for i in range(0, 509)
                if d[i] == 0xBF and d[i + 1] | (d[i + 2] << 8) == s]
        if len(hits) != 1:
            bad.append(f"{name}: its boot sector has {len(hits)} `mov di, "
                       f"0x{s:04X}`, the sum of file sector {fsec} - the "
                       f"canary the boot sector carries is not the sector the "
                       f"kernel was assembled to check (KSIG_OFF {off} in the "
                       f"map against KSIGDEF2's ksig.inc)")
            continue
        if k is None:
            # NOKZIP: one plain read of the rest of the file behind the blob
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
        lines.append(f"{name} sector {fsec} LBA {c} in {which} "
                     f"{lba}..{lba + n - 1}")
        if not ok:
            bad.append(f"{name} ({sub or 'kern_big'}, blob {blob} sectors): "
                       f"file sector {fsec} is LBA {c}, in the run "
                       f"{lba}..{lba + n - 1}, which crosses no head before it")
    if bad:
        sys.exit(
            "t_canary: the canary is NOT read after a head flip, or is not the "
            "sector it claims, or the disk's runs outgrow stage 2's DMA bounce "
            "(SPEC.md 2.9.13.7), on:\n  " + "\n  ".join(bad) + "\n- it would "
            "be loaded CORRECTLY on a machine whose FDC flips at the wrong "
            "sector, and the canary would pass on the one fault it is for "
            "(SPEC.md 18.93.1)"
        )

    # ...and it has to be tellable from its neighbours, or a shifted read
    # matches - in EVERY kernel's file that boots off a floppy. The signature
    # is the SECTOR'S SUM (the Makefile's KSIGDEF2): kern_mega's first WORD
    # there was 0, and so was the first word of the wrong sector a 286 ROM
    # delivered in its place, which is how that sector passed the canary
    # (SPEC.md 18.93.1)
    for sub, (kf, off, fsec) in trees.items():
        kk = kf.read_bytes()
        fileoff = fsec * 512
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
    print("canary: read after a head flip on %d system disks (%s), each "
          "kernel's sector distinct from both neighbours (%s)%s"
          % (len(lines), "; ".join(lines),
             ", ".join("%s KSIG_OFF %d" % (sub or "kern_big", o)
                       for sub, (_k, o, _f) in trees.items()),
             "" if not stale else "; not walked, holding an older kernel: "
             + ", ".join(stale)))

main()
