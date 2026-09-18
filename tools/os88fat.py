#!/usr/bin/env python3
"""EDIT a FAT12 floppy image in place, and say what a real drive can reach.

    python3 tools/os88fat.py ls    IMG                 # every file, with its cylinder
    python3 tools/os88fat.py add   IMG FILE [NAME.EXT] # add one, disturbing nothing
    python3 tools/os88fat.py del   IMG NAME...         # remove, freeing the chain
    python3 tools/os88fat.py cat   IMG NAME [-o OUT]   # extract one, exactly
    python3 tools/os88fat.py head  IMG NAME [-n 16]    # ...just its first bytes
    python3 tools/os88fat.py reach IMG [--cylinders N] # what a drive cannot read
    python3 tools/os88fat.py --selfcheck               # no emulator, no network

`tools/os88disk.py` BUILDS an image and is the right tool for anything this
project ships.  This one EDITS an image somebody else built, which is a
different job with a different constraint: a bootable DOS floppy keeps IBMBIO
and IBMDOS exactly where SYS put them, and a third-party game disk keeps every
file where its own installer put it, so nothing here rewrites, compacts or
re-orders anything.  It marks a directory entry, walks a chain, and writes the
clusters it was given.

WHY IT EXISTS.  Debugging a DOS program means putting an INSTRUMENT on the
program's own disk - a TSR beside the game, a probe beside the data files it
reads - and the disk is somebody's original that must otherwise not change.
docs/DOS-DEBUGGING.md is the method; this is the disk half of it.

`reach` IS THE HALF NOBODY EXPECTS TO NEED, and it cost most of a day.  Every
MartyPC 5150 profile in this tree has 40-cylinder drives, and a 720KB image
has 80 cylinders of content.  The first half reads perfectly; the second half
answers an `int 13h` error that `apps/dos`'s read window turns into END OF FILE
(SPEC.md 96.11.4), so a program is handed a file that is SHORTER than it is
and blames the file.  Nine of Prince of Persia's files sit past cylinder 39.
Nothing anywhere reports that as a configuration problem, so `reach` does.
"""

import argparse
import os
import struct
import sys
import tempfile


# --- the geometry, read rather than assumed ---------------------------------
class Fat12:
    """One FAT12 volume, edited in memory and written back whole."""

    def __init__(self, path):
        self.path = path
        self.img = bytearray(open(path, "rb").read())
        b = self.img
        if len(b) < 512:
            raise ValueError("%s is not a floppy image (%d bytes)" % (path, len(b)))
        self.bps = struct.unpack_from("<H", b, 11)[0]
        self.spc = b[13]
        self.rsv = struct.unpack_from("<H", b, 14)[0]
        self.nfat = b[16]
        self.nroot = struct.unpack_from("<H", b, 17)[0]
        self.total = struct.unpack_from("<H", b, 19)[0]
        self.spf = struct.unpack_from("<H", b, 22)[0]
        self.spt = struct.unpack_from("<H", b, 24)[0]
        self.heads = struct.unpack_from("<H", b, 26)[0]
        for n, v in (("bytes per sector", self.bps), ("sectors per cluster", self.spc),
                     ("FATs", self.nfat), ("sectors per FAT", self.spf),
                     ("sectors per track", self.spt), ("heads", self.heads)):
            if not v:
                raise ValueError("%s: %s is 0 - this is not a FAT12 floppy" % (path, n))
        self.root_lba = self.rsv + self.nfat * self.spf
        self.data_lba = self.root_lba + (self.nroot * 32 + self.bps - 1) // self.bps
        self.nclus = (self.total - self.data_lba) // self.spc
        self.root_off = self.root_lba * self.bps
        self.data_off = self.data_lba * self.bps

    # --- the FAT, both copies ----------------------------------------------
    def get(self, n):
        o = self.rsv * self.bps + n + n // 2
        v = struct.unpack_from("<H", self.img, o)[0]
        return (v >> 4) if (n & 1) else (v & 0xFFF)

    def put(self, n, val):
        """Written to EVERY FAT: DOS reads the first and `chkdsk` reads both,
        so a one-sided edit is an image that passes here and fails there."""
        for f in range(self.nfat):
            o = (self.rsv + f * self.spf) * self.bps + n + n // 2
            v = struct.unpack_from("<H", self.img, o)[0]
            v = ((val << 4) | (v & 0x000F)) if (n & 1) else ((v & 0xF000) | val)
            struct.pack_into("<H", self.img, o, v)

    def chain(self, first):
        out, seen = [], set()
        c = first
        while 2 <= c < 0xFF8:
            if c in seen or c >= self.nclus + 2:
                raise ValueError("cluster chain from %d is broken at %d" % (first, c))
            seen.add(c)
            out.append(c)
            c = self.get(c)
        return out

    def free_clusters(self):
        return [c for c in range(2, self.nclus + 2) if self.get(c) == 0]

    def cluster_off(self, c):
        return self.data_off + (c - 2) * self.spc * self.bps

    def cluster_lba(self, c):
        return self.data_lba + (c - 2) * self.spc

    def cylinder(self, lba):
        return lba // (self.spt * self.heads)

    # --- the root directory -------------------------------------------------
    def entries(self, want_deleted=False):
        """Root directory only.  Every disk this tool is pointed at - a DOS
        system floppy, a game's original - keeps its programs in the root, and
        a recursive walk would invite editing a subdirectory, which needs
        cluster allocation for the directory itself and is a different job."""
        for i in range(self.nroot):
            o = self.root_off + i * 32
            e = self.img[o:o + 32]
            if e[0] == 0:
                continue
            if e[0] == 0xE5 and not want_deleted:
                continue
            yield i, o, bytes(e)

    @staticmethod
    def pretty(raw11):
        base = raw11[:8].decode("latin1").rstrip()
        ext = raw11[8:].decode("latin1").rstrip()
        return base + ("." + ext if ext else "")

    @staticmethod
    def raw11(name):
        """'FOO.BAR' or an already-padded 'FOO     BAR' -> the 11 directory
        bytes.  Both spellings are accepted because both get typed."""
        if len(name) == 11 and "." not in name:
            return name.upper().encode("latin1")
        base, _, ext = name.upper().partition(".")
        if len(base) > 8 or len(ext) > 3:
            raise ValueError("%r is not an 8.3 name" % name)
        return (base.ljust(8) + ext.ljust(3)).encode("latin1")

    def find(self, name):
        want = self.raw11(name)
        for i, o, e in self.entries():
            if e[:11] == want:
                return i, o, e
        return None, None, None

    def read(self, name):
        i, o, e = self.find(name)
        if e is None:
            raise KeyError("%s is not on %s" % (name, self.path))
        size = struct.unpack_from("<I", e, 28)[0]
        first = struct.unpack_from("<H", e, 26)[0]
        if not first:
            return b""
        csz = self.spc * self.bps
        body = b"".join(bytes(self.img[self.cluster_off(c):self.cluster_off(c) + csz])
                        for c in self.chain(first))
        return body[:size]

    # --- the edits ----------------------------------------------------------
    def add(self, src, name, date=0x5C21):
        body = open(src, "rb").read()
        raw = self.raw11(name)
        if self.find(self.pretty(raw))[2] is not None:
            raise ValueError("%s is already on %s - `del` it first"
                             % (self.pretty(raw), self.path))
        csz = self.spc * self.bps
        need = (len(body) + csz - 1) // csz
        free = self.free_clusters()
        if len(free) < need:
            raise ValueError("need %d cluster(s), %d free" % (need, len(free)))
        use = free[:need]
        for k, c in enumerate(use):
            self.put(c, 0xFFF if k == need - 1 else use[k + 1])
            o = self.cluster_off(c)
            chunk = body[k * csz:(k + 1) * csz]
            self.img[o:o + len(chunk)] = chunk
        slot = next((i for i in range(self.nroot)
                     if self.img[self.root_off + i * 32] in (0, 0xE5)), None)
        if slot is None:
            raise ValueError("the root directory of %s is full" % self.path)
        ent = bytearray(32)
        ent[0:11] = raw
        ent[11] = 0x20                                  # archive
        struct.pack_into("<H", ent, 24, date)           # a valid date; time 0
        struct.pack_into("<H", ent, 26, use[0] if need else 0)
        struct.pack_into("<I", ent, 28, len(body))
        self.img[self.root_off + slot * 32:self.root_off + slot * 32 + 32] = ent
        return use, len(free) - need

    def delete(self, name):
        i, o, e = self.find(name)
        if e is None:
            return 0
        first = struct.unpack_from("<H", e, 26)[0]
        freed = 0
        if first:
            for c in self.chain(first):
                self.put(c, 0)
                freed += 1
        self.img[o] = 0xE5
        return freed

    def save(self):
        open(self.path, "wb").write(bytes(self.img))


# --- the verbs ---------------------------------------------------------------
def _rows(v):
    out = []
    for i, o, e in v.entries():
        if e[11] & 0x08:                                # the volume label
            out.append((-1, v.pretty(e[:11]), 0, None, None, "label"))
            continue
        first = struct.unpack_from("<H", e, 26)[0]
        size = struct.unpack_from("<I", e, 28)[0]
        lba = v.cluster_lba(first) if first else None
        out.append((first, v.pretty(e[:11]), size, lba,
                    v.cylinder(lba) if lba is not None else None,
                    "dir" if e[11] & 0x10 else ""))
    out.sort(key=lambda r: (r[0] if r[0] >= 0 else -1))
    return out


def cmd_ls(a):
    v = Fat12(a.image)
    print("%s: %d bytes, %d spt, %d heads, %d cylinders, %d-byte clusters, "
          "%d cluster(s), data at LBA %d"
          % (a.image, len(v.img), v.spt, v.heads,
             v.total // (v.spt * v.heads), v.spc * v.bps, v.nclus, v.data_lba))
    print("  %-13s %9s %8s %6s %5s" % ("name", "size", "cluster", "LBA", "cyl"))
    for first, name, size, lba, cyl, kind in _rows(v):
        if kind == "label":
            print("  %-13s %9s %8s %6s %5s   volume label" % (name, "", "", "", ""))
            continue
        print("  %-13s %9d %8d %6s %5s%s"
              % (name, size, first, lba if lba is not None else "-",
                 cyl if cyl is not None else "-", "   <dir>" if kind == "dir" else ""))
    print("  %d of %d cluster(s) free" % (len(v.free_clusters()), v.nclus))
    return 0


def cmd_reach(a):
    """What a drive with N cylinders cannot read.  Default 40, which is every
    MartyPC 5150 profile in this tree and every real 5.25" DD drive."""
    v = Fat12(a.image)
    limit = a.cylinders
    last = limit * v.spt * v.heads - 1
    print("%s: %d cylinders of content; a %d-cylinder drive reaches LBA 0..%d"
          % (a.image, v.total // (v.spt * v.heads), limit, last))
    bad = []
    for first, name, size, lba, cyl, kind in _rows(v):
        if lba is None or kind == "label":
            continue
        # the LAST cylinder the file touches, not the first: a file may START
        # inside the drive and run off the end of it, which reads as a file
        # that truncates in the middle and is harder to spot than one that
        # cannot be opened at all
        try:
            end = max(v.cluster_lba(c) + v.spc - 1 for c in v.chain(first))
        except ValueError:
            end = lba
        if v.cylinder(end) >= limit:
            bad.append((name, size, cyl, v.cylinder(end)))
    if not bad:
        print("  every file is reachable")
        return 0
    print("  %d file(s) a %d-cylinder drive cannot read WHOLE:" % (len(bad), limit))
    for name, size, c0, c1 in bad:
        print("    %-13s %8d bytes, cylinders %d..%d%s"
              % (name, size, c0, c1, "" if c0 >= limit else "   <-- STARTS inside"))
    print("  This is not reported anywhere else, and apps/dos turns the int 13h")
    print("  refusal into END OF FILE (SPEC.md 96.11.4) - so the program is handed")
    print("  a short file and blames the file.  Use a drive that fits the image:")
    print("    os8088_5150_herc_sb_720_gla is this tree's 720KB machine.")
    return 1


def cmd_add(a):
    v = Fat12(a.image)
    name = a.name or os.path.basename(a.file)
    use, left = v.add(a.file, name)
    v.save()
    lba = v.cluster_lba(use[0]) if use else 0
    print("added %s to %s: %d bytes, %d cluster(s) from %d (LBA %d, cylinder %d); "
          "%d free left" % (Fat12.pretty(Fat12.raw11(name)), a.image,
                            os.path.getsize(a.file), len(use), use[0] if use else 0,
                            lba, v.cylinder(lba), left))
    if v.cylinder(lba) >= 40:
        print("  NOTE: cylinder %d - a 40-cylinder drive cannot read this. "
              "`reach` explains." % v.cylinder(lba))
    return 0


def cmd_del(a):
    v = Fat12(a.image)
    total = 0
    for n in a.names:
        f = v.delete(n)
        total += f
        print("  %-13s %s" % (n, "freed %d cluster(s)" % f if f else "not there"))
    v.save()
    print("%s: %d cluster(s) freed, %d free now"
          % (a.image, total, len(v.free_clusters())))
    return 0


def cmd_cat(a):
    v = Fat12(a.image)
    body = v.read(a.name)
    if a.out:
        open(a.out, "wb").write(body)
        print("%s: %d bytes -> %s" % (a.name, len(body), a.out))
    else:
        sys.stdout.buffer.write(body)
    return 0


def cmd_head(a):
    v = Fat12(a.image)
    body = v.read(a.name)
    n = min(a.bytes, len(body))
    print("%s: %d bytes; first %d:" % (a.name, len(body), n))
    for off in range(0, n, 16):
        row = body[off:off + 16]
        print("  %04X  %-47s  |%s|"
              % (off, " ".join("%02X" % c for c in row),
                 "".join(chr(c) if 32 <= c < 127 else "." for c in row)))
    if len(body) >= 6:
        w = struct.unpack_from("<3H", body, 0)
        print("  as three words: %04X %04X %04X" % w)
    return 0


# --- the self-check ----------------------------------------------------------
def selfcheck():
    """Round-trips an image this file built itself, so it needs no fixture and
    no emulator.  It checks the two things that are easy to get wrong and
    silent when wrong: that a SECOND FAT is written, and that adding a file
    leaves every other file's bytes exactly where they were."""
    fails = []

    def ck(name, ok, why=""):
        print("  %-42s %s%s" % (name, "ok" if ok else "FAIL", "  " + why if why else ""))
        if not ok:
            fails.append(name)

    d = tempfile.mkdtemp(prefix="os88fat-")
    img = os.path.join(d, "t.img")
    # a bare 360KB FAT12 volume, written by hand so the check owns its fixture
    spt, heads, cyls, spc, nroot, spf, rsv, nfat = 9, 2, 40, 2, 112, 2, 1, 2
    total = spt * heads * cyls
    b = bytearray(total * 512)
    b[0:3] = b"\xeb\x3c\x90"
    b[3:11] = b"OS88FAT "
    struct.pack_into("<H", b, 11, 512)
    b[13] = spc
    struct.pack_into("<H", b, 14, rsv)
    b[16] = nfat
    struct.pack_into("<H", b, 17, nroot)
    struct.pack_into("<H", b, 19, total)
    b[21] = 0xFD
    struct.pack_into("<H", b, 22, spf)
    struct.pack_into("<H", b, 24, spt)
    struct.pack_into("<H", b, 26, heads)
    for f in range(nfat):
        o = (rsv + f * spf) * 512
        b[o:o + 3] = b"\xfd\xff\xff"
    open(img, "wb").write(bytes(b))

    payload = {"AAA.DAT": os.urandom(3000), "BBB.DAT": os.urandom(70),
               "CCC.DAT": os.urandom(12000)}
    for n, data in payload.items():
        p = os.path.join(d, n)
        open(p, "wb").write(data)
        Fat12(img).__class__  # noqa - readability only
        v = Fat12(img)
        v.add(p, n)
        v.save()

    v = Fat12(img)
    ck("every file reads back byte for byte",
       all(v.read(n) == data for n, data in payload.items()))

    # the second FAT must agree with the first, entry for entry
    f0 = bytes(v.img[rsv * 512:(rsv + spf) * 512])
    f1 = bytes(v.img[(rsv + spf) * 512:(rsv + 2 * spf) * 512])
    ck("both FATs are written", f0 == f1,
       "" if f0 == f1 else "a one-sided edit passes here and fails chkdsk")

    # deleting the middle file must free its clusters and disturb nothing else
    before = {n: v.read(n) for n in payload}
    v.delete("BBB.DAT")
    v.save()
    v = Fat12(img)
    ck("delete frees the chain and leaves the others",
       v.find("BBB.DAT")[2] is None
       and all(v.read(n) == before[n] for n in ("AAA.DAT", "CCC.DAT")))

    # ...and the freed clusters are reused, without moving anybody
    p = os.path.join(d, "DDD.DAT")
    data = os.urandom(60)
    open(p, "wb").write(data)
    v.add(p, "DDD.DAT")
    v.save()
    v = Fat12(img)
    ck("a later add reuses free space and moves nothing",
       v.read("DDD.DAT") == data
       and all(v.read(n) == before[n] for n in ("AAA.DAT", "CCC.DAT")))

    # 8.3 both ways
    ck("'FOO.BAR' and 'FOO     BAR' name the same entry",
       Fat12.raw11("foo.bar") == Fat12.raw11("FOO     BAR") == b"FOO     BAR")

    # reach: a file placed past the drive is reported
    v = Fat12(img)
    ck("reach counts the LAST cylinder a file touches",
       v.cylinder(v.cluster_lba(v.nclus)) > v.cylinder(v.data_lba))

    for n in os.listdir(d):
        os.unlink(os.path.join(d, n))
    os.rmdir(d)
    print("os88fat: %s" % ("ok - %d check(s)" % 6 if not fails
                           else "FAILED: " + ", ".join(fails)))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(
        description="edit a FAT12 floppy image in place; say what a drive can reach")
    ap.add_argument("--selfcheck", action="store_true",
                    help="round-trip an image this file builds itself, then exit")
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("ls", help="every file, with its cluster, LBA and cylinder")
    p.add_argument("image")
    p.set_defaults(fn=cmd_ls)

    p = sub.add_parser("reach", help="what a drive with N cylinders cannot read")
    p.add_argument("image")
    p.add_argument("--cylinders", type=int, default=40,
                   help="the DRIVE's cylinders (default 40: every MartyPC 5150 "
                        "profile here, and every real 5.25\" DD drive)")
    p.set_defaults(fn=cmd_reach)

    p = sub.add_parser("add", help="add a file, disturbing nothing already there")
    p.add_argument("image")
    p.add_argument("file")
    p.add_argument("name", nargs="?", help="the 8.3 name on the disk (default: "
                                           "the source file's own)")
    p.set_defaults(fn=cmd_add)

    p = sub.add_parser("del", help="remove files, freeing their chains")
    p.add_argument("image")
    p.add_argument("names", nargs="+")
    p.set_defaults(fn=cmd_del)

    p = sub.add_parser("cat", help="extract one file exactly")
    p.add_argument("image")
    p.add_argument("name")
    p.add_argument("-o", "--out", help="write here instead of stdout")
    p.set_defaults(fn=cmd_cat)

    p = sub.add_parser("head", help="the first bytes of one file, hex and words")
    p.add_argument("image")
    p.add_argument("name")
    p.add_argument("-n", "--bytes", type=int, default=32)
    p.set_defaults(fn=cmd_head)

    a = ap.parse_args()
    if a.selfcheck:
        return selfcheck()
    if not a.cmd:
        ap.print_help()
        return 2
    try:
        return a.fn(a)
    except (ValueError, KeyError) as e:
        print("os88fat: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
