#!/usr/bin/env python3
"""SPEC.md 54.7.5 - every shipped volume's ASSOC.DAT lies inside ONE TRACK.

The mount reads ASSOC.DAT on every volume switch (asc_use), and SPEC.md 18.95's
read-ahead answers a miss by reading to the end of the track - so a file inside
one track is one int 13h and a file across a boundary is two, the second a
whole track of whatever follows.  It used to be the LAST chain on the disk:
cylinder 34 of the 360KB apps floppy, straddling a track, a seek across the
disk and back on every mount.  tools/os88disk.py now places it at the earliest
allocation boundary that holds it in one track; this reads the result back
with a FAT reader of its own rather than asking the writer.

Scoped to $(SHIPIMGS), read out of the Makefile the way t_volsig does, for that
row's reason: build/ also holds on-demand disks and images a test flushed.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness                                              # noqa: E402
from t_volsig import ship_images, BUILD                    # noqa: E402

NAME = b"ASSOC   DAT"


def placement(raw):
    """(first LBA, sector count, [track of each sector]) or None if absent."""
    bps, spc = struct.unpack_from("<HB", raw, 11)
    rsvd, nfats, root_ent = struct.unpack_from("<HBH", raw, 14)
    fatsz, spt = struct.unpack_from("<HH", raw, 22)
    root_lba = rsvd + nfats * fatsz
    data_lba = root_lba + (root_ent * 32 + bps - 1) // bps
    tot = struct.unpack_from("<H", raw, 19)[0] or struct.unpack_from("<I", raw, 32)[0]
    fat12 = (tot - data_lba) // spc < 4085
    fat = raw[rsvd * bps:(rsvd + fatsz) * bps]

    def nxt(cl):
        if fat12:
            v = struct.unpack_from("<H", fat, cl + cl // 2)[0]
            return v >> 4 if cl & 1 else v & 0xFFF
        return struct.unpack_from("<H", fat, cl * 2)[0]

    for i in range(root_ent):
        e = raw[root_lba * bps + i * 32:root_lba * bps + i * 32 + 32]
        if e[0] == 0:
            break
        if e[:11] != NAME:
            continue
        cl, size = struct.unpack_from("<HI", e, 26)
        want = (size + bps - 1) // bps
        lbas = []
        while 2 <= cl < (0xFF8 if fat12 else 0xFFF8) and len(lbas) < want:
            base = data_lba + (cl - 2) * spc
            lbas.extend(range(base, base + spc))
            cl = nxt(cl)
        lbas = lbas[:want]
        return lbas[0], len(lbas), sorted({l // spt for l in lbas}), spt
    return None


def main():
    names = ship_images()
    harness.check(len(names) >= 8, "the Makefile's $(SHIPIMGS) resolved",
                  why="an empty list is a green row about nothing", got=len(names))
    n = 0
    for name in names:
        p = BUILD / name
        if not p.exists():
            continue
        raw = p.read_bytes()
        if len(raw) < 512 or raw[510:512] != b"\x55\xAA":
            continue
        got = placement(raw)
        if got is None:
            continue                    # a volume with no packages has none
        n += 1
        lba, secs, tracks, spt = got
        harness.check(
            len(tracks) == 1,
            "%s: ASSOC.DAT (LBA %d, %d sectors) is inside one track" % (name, lba, secs),
            why="SPEC.md 54.7.5: the mount reads it on every volume switch and "
                "the read-ahead fills to the END of a track, so a file across a "
                "boundary costs a second int 13h and a whole extra track. "
                "tools/os88disk.py places it at the first boundary that holds it",
            got="tracks %s at %d sectors a track" % (tracks, spt), want="one track")
    harness.check(n >= 8, "at least 8 built volumes carry an ASSOC.DAT",
                  why="`make` first - otherwise this row checked nothing", got=n)
    print("t_ascplace: %d volume(s) checked" % n)
    harness.done("t_ascplace")


if __name__ == "__main__":
    main()
