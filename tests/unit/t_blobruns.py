#!/usr/bin/env python3
"""How many int 13h calls does stage 1 spend on the blob - and stage 2 on the
kernel behind it? (SPEC.md 15.3.8.5, 2.9.14)

    python3 tests/unit/t_blobruns.py [--sectors N] [--tail N]

`--tail N` prices N more PACKED sectors in stage 2's tail - what a body moved
into `.boot3`, KERNEL.SYS's last section, costs after the bar (SPEC.md
2.9.15, BOOT3-PLAN 10.2). `--sectors N` prices a blob this file's kernel does not have, which is what
somebody about to spend a byte of `.boot2` wants to know: `--sectors 14` is
SPLSTARS' blob and shows the 720KB disk taking a third call for one sector.
It prices BOTH reads, because a longer blob moves every kernel sector behind
it and stage 2's read is cylinder-bounded: `--sectors 12` is free for stage 1
on every geometry and costs stage 2 a call on 720KB.

PERFORMANCE.md's rule is that disk work is priced in CALLS and not in sectors -
one int 13h is 1-2 disk revolutions whatever it moves, near enough 400 ms on
the field machine - and stage 1's read of stage 2 is the very first of them.

THE COUNT IS NOT A FUNCTION OF THE SECTOR COUNT ALONE, which is the whole
reason this file exists. boot/boot.asm bounds a run at the TRACK, and
KERNEL.SYS starts wherever the BPB puts the data area - so the same
BOOT2_SECS splits differently on each geometry, and the boundary is in a
different place on each:

    BOOT2_SECS   360KB          720KB          1.2MB        1.44MB
        13       6 + 7          4 + 9          1 + 12       3 + 10
        14       6 + 8          4 + 9 + 1      1 + 13       3 + 11
        19       6 + 9 + 4      4 + 9 + 6      1 + 15 + 3   3 + 16
        22       6 + 9 + 7      4 + 9 + 9      1 + 15 + 6   3 + 18 + 1
        23       6 + 9 + 8      4 + 9 + 9 + 1  1 + 15 + 7   3 + 18 + 2

**720KB is the tight one at 13 and 13 is exactly its last sector.** The 14th
costs a whole extra int 13h to move ONE sector, which is the worst shape a read
has. That was found by measuring SPLSTARS=1 (which took 14, SPEC.md 15.3.8.5)
after it had been asserted in a commit message that the sector was free, on
the strength of the 360KB arithmetic alone.

So this asserts a CALL COUNT PER GEOMETRY, not one number for all of them - the
table above is why, and one number was the shape of the original mistake.
SPEC.md 2.9.12 spent the third call on the two 9-sector geometries; 1.44MB is
still two and 21 is the last size that keeps it there. It is a ratchet rather
than a law: growing the blob past a boundary is a decision somebody may take,
and then THAT GEOMETRY's row moves with a reason beside it - what it may not be
is a thing nobody noticed.

Host-side and exact: it reads the images `make` just built, finds KERNEL.SYS
in the root directory, and walks the runs the way the sector does - bounded
at the track AND at the 64KB DMA page of the destination, which is the blob's
own segment (HEAP_PARA, the map's BLOB_SEG_AT: boot.asm's two bounds), and
for EACH KERNEL'S OWN blob length, read off its kernel.kz.json. Both were
missing until BOOT3-PLAN wave 0 moved this row to the FAST tier: it read the
literal 13 out of `BOOT2_SECS equ 13 + ...` for every kernel, so it priced
kern_small's 11-sector blob as 13, and it walked no page, so it reported
small360's blob - 0xF200..0x10800, across the page at 0x10000 - as two calls
where stage 1 makes three. No emulator
- and MartyPC cannot host a 720KB drive with the ROM sets in this tree anyway,
which is precisely how the boundary got missed.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

# ...AND THE RATCHET IS PER GEOMETRY, because the four do not share an answer
# and never did - the paragraph above is the whole reason this file exists, and
# one number for all of them was the shape of the mistake it was written to
# catch. SPEC.md 2.9.12 took the blob from 13 sectors to 19 for the boot-only
# bodies in docs/plans/LAST-DROP-BYTES.md, which spends the third call on the two
# 9-sector geometries and none on the release one.
#
# **AND THEN SPEC.md 2.5.2 TOOK IT BACK TO 8**, by splitting the overlay by
# lifetime: the bodies that are dead at the first mount ride the kernel's own
# read into the FAT window instead, and only the 1,319 bytes that must outlive
# it stay in the blob. So the third call is given back on both 9-sector
# geometries and the ratchet closes behind it - all of them are TWO now, and
# the 1.44MB row has been two throughout.
#
# The numbers each row cites are the sizes at which its own count changes, and
# they are the reason to keep the rows apart rather than collapse them to one
# figure: 13 is the last two-call blob on 720KB, 15 on 360KB, 16 on 1.2MB and
# 21 on 1.44MB, so a growth that is free on the release geometry can still buy
# a call on the other three - and 720KB is still the one that binds.
#
# **BOOT3-PLAN WAVE 4 TOOK EVERY KERNEL'S BLOB TO SEVEN** (2026-11-08): `.boot2`
# back to the loader, the splash, the first tick's probe and the status line,
# everything else to `.boot3` (tests/blobbodies.txt keeps it there). Seven is
# two calls on every geometry - 4 + 3, 4 + 3, 1 + 6, 3 + 4 - so these rows do
# not move for kern_big; what moves is SEVEN FEWER SECTORS before the bar, and
# kern_small's third call (below).
IMAGES = {
    "os8088-360.img": (2, "4 + 3 at 7 sectors (4 + 9 at 13; 6 + 2 at 8 and "
                          "6 + 9 + 4 at 19); 13 is the last two-call blob"),
    "os8088-720.img": (2, "4 + 3 at 7 (4 + 9 at 13, this geometry's last "
                          "two-call blob: the 14th sector was the price)"),
    "os8088-120.img": (2, "1 + 6 at 7 - the data area starts one sector "
                          "short of a cylinder end here, so the first call "
                          "is a single sector and 16 is the last two-call "
                          "blob"),
    "os8088.img":     (2, "3 + 4 at 7, and 21 is the last size that is two "
                          "calls here - the next one is not free"),
    # the 128KB machine's (SPEC.md 24.5.7): kern_small's disks put KERNEL.SYS
    # at LBA 12, two sectors earlier than the 360KB and 720KB disks above
    # ...and at kern_small's OWN length, 11, with stage 1's page bound: its
    # blob is read to 0xF200 and the 64KB page ends at 0x10000, 7 sectors in,
    # so the second track's run splits - 6 + 1 + 4. The row read 2 while the
    # model priced a 13-sector blob and walked no page (BOOT3-PLAN 12, "found
    # on the way"); the model now walks what boot.asm does, and the count is
    # PREDICTED until os88boot on small360 confirms it (the final pass).
    # ...AND BOOT3-PLAN WAVE 4 GAVE IT BACK (2026-11-08): the blob is seven
    # sectors and kernel.asm's BLOB_LIFT rounds it up onto the page when it
    # would straddle one (its page term; at wave 4's lift it would have sat at
    # 0xF800 and read 4 + 2 + 1), so it is read at 0x10000 as 6 + 1
    "small360.img":   (2, "6 + 1 at 7, the blob at 0x10000 - on the DMA page "
                          "rather than across it (6 + 1 + 4 at 11, at "
                          "0xF600, was the third call)"),
    "small720.img":   (2, "6 + 1 at 7, the 360KB small disk's shape"),
}
# ...whose kernel is build/smallk's, so its stage-2 read is priced off that
# kernel's own packed layout and not the big one's
SMALL = {"small360.img", "small720.img"}


def boot2_secs(sub=""):
    """The blob's length IN THE KERNEL ON THAT DISK: kernel.kz.json's `blob`
    (BOOT2_PAD), which tools/os88kz.py writes off the assembly itself. It was
    the literal `13` read out of `BOOT2_SECS equ 13 + BOOT2_KNOBSEC -
    BOOT2_SMALLSEC`, which is kern_big's and was applied to kern_small's 11."""
    k = kz(sub)
    if k is None:
        raise SystemExit("t_blobruns: no build/%skernel.kz.json - run make "
                         "first" % (sub + "/" if sub else ""))
    if k["blob"] % 512:
        raise SystemExit("t_blobruns: %s's blob is %d bytes, not whole sectors"
                         % (sub or "build", k["blob"]))
    return k["blob"] // 512


def blobseg(sub=""):
    """Where stage 1 reads the blob - HEAP_PARA, the map's BLOB_SEG_AT - for
    its DMA-page bound."""
    import re
    path = os.path.join(ROOT, "build", sub, "kernel.map")
    m = re.search(r"^([0-9A-F]{8})\s+BLOB_SEG_AT\s*$",
                  open(path, errors="replace").read(), re.M)
    if not m:
        raise SystemExit("t_blobruns: no BLOB_SEG_AT in %s" % path)
    return int(m.group(1), 16)


def runs(path, nsec, dest=None):
    """The (cyl, head, sector, count) runs stage 1 asks for, off the image."""
    d = open(path, "rb").read()
    bps = struct.unpack("<H", d[11:13])[0]
    spc = d[13]
    res = struct.unpack("<H", d[14:16])[0]
    nfat = d[16]
    rootn = struct.unpack("<H", d[17:19])[0]
    fatsz = struct.unpack("<H", d[22:24])[0]
    spt = struct.unpack("<H", d[24:26])[0]
    heads = struct.unpack("<H", d[26:28])[0]
    rootlba = res + nfat * fatsz
    datalba = rootlba + (rootn * 32 + bps - 1) // bps

    start = None
    for i in range(rootn):
        off = rootlba * bps + i * 32
        e = d[off:off + 32]
        if e[0] == 0x00:
            break
        if e[0:11] == b"KERNEL  SYS":
            start = datalba + (struct.unpack("<H", e[26:28])[0] - 2) * spc
            break
    if start is None:
        raise SystemExit("t_blobruns: no KERNEL.SYS in %s" % path)
    if start != datalba:
        raise SystemExit("t_blobruns: KERNEL.SYS is not the first file in %s - "
                         "boot/boot.asm derives its LBA as the data area's and "
                         "would read somebody else's sectors" % path)

    out, lba, left = [], start, nsec
    while left:
        cyl, rem = divmod(lba, spt * heads)
        head, sec = divmod(rem, spt)
        run = min(spt - sec, left)          # sec is 0-based here
        if dest is not None:                # ...and boot.asm's second bound,
            page = (-((dest << 4) & 0xFFFF)) & 0xFFFF   # the 64KB DMA page
            if page:
                run = min(run, page >> 9)
            dest += run * 32
        out.append((cyl, head, sec + 1, run))
        lba += run
        left -= run
    return spt, out, (start, heads)


# --- STAGE 2's READ, which the blob's length MOVES (SPEC.md 2.9.14) ----------
# A longer blob pushes every kernel sector after it one sector along, and
# stage 2's read of the kernel is bounded at the CYLINDER (SPEC.md 18.91.1) -
# so a sector of blob that costs stage 1 nothing can still cost stage 2 a call,
# at the far end of the read where nobody looks. This walks the read exactly as
# boot/boot2.asm's read_run does: on an 8088 the run ends at the first of the
# cylinder's end, the sectors still wanted and the 64KB DMA page of the
# DESTINATION, and the destination starts at KERNEL_SEG and advances 32
# paragraphs a sector - with the packed kernel (SPEC.md 2.9.13) the plain head
# is read first and the packed tail lands KZ_RPARA paragraphs further up.
KERNEL_SEG = 0x0060


def kz(sub=""):
    path = os.path.join(ROOT, "build", sub, "kernel.kz.json")
    if not os.path.exists(path):
        return None                         # NOKZIP: the plain read
    import json
    return json.load(open(path))


def stage2_calls(start, spt, heads, nsec_blob, k, tail=0):
    runmax = spt * heads                    # an 8088's bound: the cylinder
    lba = start + nsec_blob
    dest = KERNEL_SEG
    if k:
        # the plain head at the TRACK bound since the big review's BR-1 part 2
        # (boot2.asm: the splash's first tick runs those sectors before the
        # canary has looked at anything), the tail at the gate's cylinder
        parts = [(k["headsecs"], 0, spt),
                 (k["ksecs"] - k["headsecs"] + tail, k["rpara"], runmax)]
    else:
        return None
    calls = 0
    for left, rpara, bound in parts:
        dest += rpara
        while left:
            run = min(left, bound - lba % bound)
            page = (-((dest << 4) & 0xFFFF)) & 0xFFFF
            if page:
                run = min(run, page >> 9)
            calls += 1
            lba += run
            left -= run
            dest += run * 32
    return calls


# ...AND THE RATCHET FOR IT IS PER GEOMETRY TOO, for the blob's own reason.
# The counts are the shipped build's at SPEC.md 2.9.14 (BOOT2_SECS 10, a
# 155-sector packed kernel read as a 9-sector plain head and a tail), and the
# model was checked against MartyPC's FDC rather than trusted: os88boot on the
# 360KB disk reads 165 sectors in 13 READ commands, which is the blob's 2 and
# these 11. The plain head costs a call of its own and the DMA pages one more,
# which is why it is not the cylinder count alone. 720KB is the tight one: an
# eleventh blob sector is free on every geometry and a twelfth costs 720KB a
# call (`--sectors 12`).
#
# **SPEC.md 26.10.5 TOOK THE BLOB TO ELEVEN SECTORS AND MOVED NONE OF THESE.**
# On the kernel it was written against the eleventh pushed 720KB's read over a
# cylinder (11 -> 12 calls, +213 ms measured, and the owner took it); on the
# kernel it landed on, the same sector costs no call anywhere - os88boot on
# os8088_5150_herc_sb_720_gla reads the loader in 14 int 13h either way, 5,889
# against 5,930 ms. Which way it falls is the KERNEL's length and not the
# blob's, so the next kernel that grows may move this row, with a reason.
#
# **THE BIG REVIEW'S BR-1 (part 2) SPENT ONE ON FOUR OF THE SIX, ON PURPOSE.**
# The plain head is read at the TRACK bound now, because the splash's first
# tick runs those sectors before the canary has checked anything, and on a
# BIOS that flips at the wrong sector a head read across a flip ran the other
# head's bytes as vid_setmode. Where the head crosses a track that is one call
# more - kern_big's 1.2MB (head LBA 42..50) and 1.44MB (46..54) disks and both
# kern_small disks (23..31) - and where it does not (kern_big 360KB and 720KB,
# 27..35, one track) it is nothing. PREDICTED, ~170-400 ms an 8088 boot on
# those four, until os88boot measures it in the final pass.
#
# **BOOT3-PLAN WAVE 4 GAVE THREE OF THEM BACK, AND ONE MORE** (2026-11-08). A
# 7-sector blob moves the plain head six sectors earlier on every disk, and
# the head is now the resident window itself (the Makefile's KZ_HEADMIN 0: 5
# sectors, 6 on kern_mega), chosen per disk off this model - so the 1.2MB and
# 1.44MB heads are one track run again and the tails start better placed:
# 1.2MB 9 -> 7, 1.44MB 8 -> 7, both small disks 8 -> 7. 360KB and 720KB stay
# at 11 and are now EXACTLY FULL: the packed tail ends on the cylinder at LBA
# 180, so ONE more packed sector of KERNEL.SYS costs those two disks a call
# after the bar (`--tail 1` shows it; 1.2MB has 15 to spare, 1.44MB 17, the
# small disks 5). That is where cylinders fall, not slack anybody spent; a
# head of 8 or 9 would buy 17 sectors of it for two calls more. PREDICTED,
# like the rest of this table, until os88boot measures it in the final pass.
STAGE2 = {
    "os8088-360.img": 11,   # head 5 at LBA 21..25, the tail 26..180: 0 free
    "os8088-720.img": 11,   # (the 360KB disk's shape)
    "os8088-120.img": 7,    # 9 at blob 13 / head 9 (BR-1's track-bound head)
    "os8088.img": 7,        # 8 at blob 13 / head 9
    "small360.img": 7,      # kern_small, 98 sectors packed, after its OWN
    "small720.img": 7,      # 7-sector blob; 8 at blob 11
}


def main():
    argv = sys.argv[1:]
    asked = tail = None
    while argv:
        if argv[0] in ("--sectors", "--tail") and len(argv) > 1:
            if argv[0] == "--sectors":
                asked = int(argv[1])
            else:
                tail = int(argv[1])
            argv = argv[2:]
        else:
            raise SystemExit("usage: t_blobruns.py [--sectors N] [--tail N]")
    if asked is not None:
        print("  (asked for %d blob sectors, not each kernel's own)" % asked)
    if tail:
        print("  (and %d more packed sectors in stage 2's tail)" % tail)
    fail = []
    seen = []
    for img, (cap, why) in IMAGES.items():
        sub = "smallk" if img in SMALL else ""
        nsec = asked if asked is not None else boot2_secs(sub)
        path = os.path.join(ROOT, "build", img)
        if not os.path.exists(path):
            # a FAILURE, not a skip: this row used to print "not built" and
            # pass, so a geometry nobody had built was a geometry whose
            # blob ratchet was never read at all
            fail.append("%s: not built - run make first; the ratchet for "
                        "this geometry (%s) cannot be read off a disk that "
                        "is not there" % (img, why))
            continue
        spt, r, (start, heads) = runs(path, nsec, blobseg(sub))
        seen.append(len(r))
        print("  %-16s SPT=%-2d  %d call(s) of %d for %d sectors: %s"
              % (img, spt, len(r), cap, nsec,
                 " + ".join("%d@C%dH%dS%d" % (n, c, h, s) for c, h, s, n in r)))
        if len(r) > cap:
            fail.append("%s: the blob takes %d int 13h calls and this "
                        "geometry's ratchet is %d (%s). One call is 1-2 "
                        "revolutions WHATEVER it moves (PERFORMANCE.md Part "
                        "2), so the last run here costs a whole one to move %d "
                        "sector(s). Either the blob fits again or THIS "
                        "geometry's row moves with a reason beside it "
                        "(SPEC.md 15.3.8.5, 2.9.12)"
                        % (img, len(r), cap, why, r[-1][3]))

        k = kz(sub)
        n2 = stage2_calls(start, spt, heads, nsec, k, tail or 0)
        if n2 is not None:
            cap2 = STAGE2[img]
            print("  %-16s stage 2 reads the kernel in %d call(s) of %d"
                  % ("", n2, cap2))
            if n2 > cap2:
                fail.append("%s: stage 2's kernel read takes %d int 13h calls "
                            "and this geometry's ratchet is %d. The blob's "
                            "length moves every kernel sector after it, and "
                            "the read is cylinder-bounded (SPEC.md 18.91.1, "
                            "2.9.14) - so either the kernel or the blob grew "
                            "past a cylinder, and that is a decision with a "
                            "reason beside this row, not a drift"
                            % (img, n2, cap2))

    for f in fail:
        print("FAIL: %s" % f)
    print("blobruns: %s" % ("FAILED" if fail else
                            "the blob is %s int 13h call(s), 360/720/1.2/1.44/small360/small720"
                            % "/".join(str(n) for n in seen)))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
