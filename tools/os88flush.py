#!/usr/bin/env python3
"""os88flush: get the guest's floppy back OUT of MartyPC, and read it here.

MartyPC mounts a floppy by reading the file once, and every sector the guest
writes after that lands in an in-memory `DiskImage` and nowhere else. The
eframe frontend has a Media menu to spend those writes - Save Floppy As - and
a headless run had nothing at all, so a scripted session could make os8088
save a document and then had to ask *os8088* whether it had worked. That is
not a test: the writer and the reader are the same FAT12 code, so the one bug
a save can have that matters - both halves agreeing on the same wrong thing -
is exactly the bug that cannot be seen from inside.

This is the outside. `flush` is the debug server's new command (the same
`fluxfox::ImageWriter` the menu item uses, docs/MARTYPC-DEBUG.md); everything
here is what to do with the bytes once they are on this side of the wire:

    python3 tools/os88flush.py 127.0.0.1:9001 disks
    python3 tools/os88flush.py 127.0.0.1:9001 save 0 /tmp/after.img
    python3 tools/os88flush.py 127.0.0.1:9001 ls 1 APPS
    python3 tools/os88flush.py 127.0.0.1:9001 get 0 SYSTEM.CFG /tmp/cfg.bin
    python3 tools/os88flush.py 127.0.0.1:9001 diff 0
    python3 tools/os88flush.py 127.0.0.1:9001 verify 0

or, sharing one connection with a scripted session (`Mouse(marty=m)`'s idiom,
and for the same reason - THE DEBUG SERVER TAKES ONE CLIENT, and a second one
does not error, it hangs):

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img") as m:
        f = os88flush.Flush(marty=m)
        ...drive the UI, save a file...
        assert "NOTES.TXT" in f.volume(1).names()

FOUR THINGS ABOUT IT ARE WORTH KNOWING BEFORE YOU TRUST A RESULT.

**It pauses the machine, and it has to.** A flush is a read of the image at
the instant it is asked for, and a save on this OS is a multi-sector commit -
data, then the FAT, then the directory entry (SPEC.md 18.4). Grabbed in the
middle of one, the volume that comes out is genuinely inconsistent, and it
reads afterwards as a corrupt disk rather than as a mistimed capture. Every
verb here pauses, flushes, and puts the machine back the way it found it.

**`writes` IS NOT A DIRTY FLAG, whatever it looks like.** The count in
`disks` is fluxfox's own `write_ct`, and on a raw sector image MartyPC loads
at BitStream resolution the one call that would advance it is COMMENTED OUT
upstream (`BitStreamTrack::add_write`, fluxfox `marty_consumer_0.34`) - so it
reads 1 after the mount and 1 for ever, through a Control Panel close that
demonstrably wrote three sectors. It is reported because it is the machine's
own number and it may come back to life upstream; nothing here decides
anything on it. `dirty()` compares the CONTENT against the image the drive
was mounted from, which is slower, always right, and cannot rot.

**A bare `save(drive)` WRITES BACK OVER THE MOUNTED IMAGE** - the menu's Save
Floppy rather than its Save Floppy As, which is usually exactly what you want
and is also the one verb here that destroys evidence. Under `launch()` that
file is the session's private copy in the run tree, and it is the only
pristine copy in existence: `diff` and `dirty` compare against it, so once it
has been written back they compare the disk with itself and report, perfectly
confidently, that nothing has changed. Name a path when the reference matters.

**The emulator writes the file, so the path is the EMULATOR's.** It is a
local instrument - relative paths resolve in the run tree, not in your
working directory - so everything here hands the server an absolute path and
reads the result back itself.
"""
import argparse
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
from os88disk import SECTOR, fat_get                        # noqa: E402

# One FAT12 decoder in this tree, not two: `fat_get` is os88disk's, where the
# images are built, and a reader that disagreed with the builder about a
# 12-bit entry would be a bug that looks exactly like a kernel bug.

ATTR_RDONLY = 0x01
ATTR_HIDDEN = 0x02
ATTR_SYS    = 0x04
ATTR_LABEL  = 0x08
ATTR_DIR    = 0x10
ATTR_LFN    = 0x0F          # the whole byte, not a bit: an LFN slot is
                            # RDONLY|HIDDEN|SYS|LABEL together


class FlushError(Exception):
    pass


class Entry(object):
    """One 8.3 directory entry, as the host sees it."""

    def __init__(self, name, attr, cluster, size, path):
        self.name = name            # 'MINES.O88', display form
        self.attr = attr
        self.cluster = cluster
        self.size = size
        self.path = path            # 'GAMES/MINES.O88'

    @property
    def is_dir(self):
        return bool(self.attr & ATTR_DIR)

    @property
    def is_system(self):
        """Hidden or system: what SPEC.md 19.6 marks and disk_mount filters.

        The kernel's own species filter drops these from every listing, so a
        file that IS one is invisible inside os8088 by design - and this is
        the only place it can be seen at all.
        """
        return bool(self.attr & (ATTR_HIDDEN | ATTR_SYS))

    def __repr__(self):
        return "<Entry %s %s %d>" % (self.path, "dir" if self.is_dir else "file", self.size)


class Volume(object):
    """A FAT12/FAT16 volume, read on the HOST out of a flat sector image.

    Deliberately independent of the kernel: it walks the BPB, the FAT and the
    directories itself, so what it reports is what is ON THE DISK rather than
    what os8088's own mount decided to make of it. That independence is the
    whole point - it is what lets a test catch a file the OS wrote and cannot
    read back, and a file the OS lists and never actually wrote.
    """

    def __init__(self, img, name="<image>"):
        if len(img) < SECTOR:
            raise FlushError("%s: shorter than one sector" % name)
        self.img = img
        self.name = name
        b = img
        self.bps      = struct.unpack_from("<H", b, 11)[0]
        self.spc      = b[13]
        self.rsvd     = struct.unpack_from("<H", b, 14)[0]
        self.nfats    = b[16]
        self.rootent  = struct.unpack_from("<H", b, 17)[0]
        tot16         = struct.unpack_from("<H", b, 19)[0]
        self.fatsz    = struct.unpack_from("<H", b, 22)[0]
        self.spt      = struct.unpack_from("<H", b, 24)[0]
        self.heads    = struct.unpack_from("<H", b, 26)[0]
        tot32         = struct.unpack_from("<I", b, 32)[0]
        self.total    = tot16 or tot32
        for what, v in (("bytes per sector", self.bps), ("sectors per cluster", self.spc),
                        ("FAT count", self.nfats), ("FAT size", self.fatsz),
                        ("total sectors", self.total)):
            if not v:
                raise FlushError("%s: BPB %s is zero - not a FAT volume" % (name, what))
        if self.bps != SECTOR:
            raise FlushError("%s: %d bytes per sector; this reads 512" % (name, self.bps))

        self.root_lba = self.rsvd + self.nfats * self.fatsz
        self.root_secs = (self.rootent * 32 + SECTOR - 1) // SECTOR
        self.data_lba = self.root_lba + self.root_secs
        clusters = (self.total - self.data_lba) // self.spc
        self.clusters = clusters
        # Cluster COUNT decides the FAT width, per the Microsoft spec and per
        # SPEC.md 18.2 rule 10 - never the media byte and never the size.
        self.fat12 = clusters < 4085
        self.eoc = 0xFF8 if self.fat12 else 0xFFF8
        self.fat = self._sectors(self.rsvd, self.fatsz)

    # --- geometry ------------------------------------------------------------

    def _sectors(self, lba, count):
        off = lba * SECTOR
        return self.img[off:off + count * SECTOR]

    def _cluster_bytes(self, n):
        lba = self.data_lba + (n - 2) * self.spc
        return self._sectors(lba, self.spc)

    def chain(self, first):
        """Every cluster of a file, with the two guards that matter.

        A chain read off a disk the guest has been writing to is exactly the
        thing that can be circular or run off the end, and this is a HOST tool
        looking at a possibly-broken volume - so a bad chain is a message, not
        a hang and not a traceback.
        """
        out, seen, n = [], set(), first
        while 2 <= n < self.clusters + 2 and n < self.eoc:
            if n in seen:
                raise FlushError("%s: cluster chain loops at %d" % (self.name, n))
            seen.add(n)
            out.append(n)
            n = fat_get(self.fat, self.fat12, n)
        return out

    # --- directories ---------------------------------------------------------

    def _dir_bytes(self, cluster):
        if cluster == 0:                       # the root is not in the chain
            return self._sectors(self.root_lba, self.root_secs)
        return b"".join(self._cluster_bytes(c) for c in self.chain(cluster))

    def listdir(self, path=""):
        """The entries of one directory. `path` is 'APPS' or 'A/B', '' = root.

        The on-disk '.' and '..' entries are dropped, as are deleted entries,
        long-name slots and the volume label - the same species filter
        `disk_mount` applies (SPEC.md 19), so that what comes back is what a
        Disk window would show, PLUS the hidden and system files it would not
        (see Entry.is_system).
        """
        cluster, prefix = 0, ""
        for part in [p for p in path.replace("\\", "/").split("/") if p]:
            hit = None
            for e in self._entries(cluster, prefix):
                if e.is_dir and e.name.upper() == part.upper():
                    hit = e
                    break
            if hit is None:
                raise FlushError("%s: no folder '%s' in '%s'" % (self.name, part, prefix or "root"))
            cluster, prefix = hit.cluster, hit.path + "/"
        return self._entries(cluster, prefix)

    def _entries(self, cluster, prefix):
        raw, out = self._dir_bytes(cluster), []
        for i in range(0, len(raw) - 31, 32):
            d = raw[i:i + 32]
            if d[0] == 0x00:                   # end of directory: nothing past it
                break
            if d[0] == 0xE5:                   # deleted
                continue
            attr = d[11]
            if attr == ATTR_LFN or (attr & ATTR_LABEL and not attr & ATTR_DIR):
                continue                       # long-name slot, or volume label
            stem, ext = d[0:8].rstrip(b" "), d[8:11].rstrip(b" ")
            if stem in (b".", b".."):
                continue
            try:
                name = stem.decode("ascii") + ("." + ext.decode("ascii") if ext else "")
            except UnicodeDecodeError:
                continue                       # not ours; a hostile disk is a
                                               # thing this OS assumes (SPEC.md 19)
            out.append(Entry(name, attr,
                             struct.unpack_from("<H", d, 26)[0],
                             struct.unpack_from("<I", d, 28)[0],
                             prefix + name))
        return out

    def walk(self, path=""):
        """Every entry on the volume, depth first, folders included."""
        for e in self.listdir(path):
            yield e
            if e.is_dir:
                for sub in self.walk(e.path):
                    yield sub

    def names(self, path=""):
        """Just the paths, for an `assert 'NOTES.TXT' in ...`."""
        return [e.path for e in self.walk(path)]

    def read(self, path):
        """A file's exact bytes, truncated to the size in its directory entry.

        The truncation is not cosmetic: os8088's loader depends on the entry's
        size being exact (a package is never padded), so a reader that handed
        back whole clusters would quietly disagree with the kernel about where
        a file ends.
        """
        parts = [p for p in path.replace("\\", "/").split("/") if p]
        if not parts:
            raise FlushError("read: no file named")
        for e in self.listdir("/".join(parts[:-1])):
            if not e.is_dir and e.name.upper() == parts[-1].upper():
                if e.size == 0:
                    return b""
                data = b"".join(self._cluster_bytes(c) for c in self.chain(e.cluster))
                return data[:e.size]
        raise FlushError("%s: no file '%s'" % (self.name, path))

    def find(self, name):
        """Every entry whose 8.3 name matches, wherever it is on the volume."""
        return [e for e in self.walk() if e.name.upper() == name.upper()]


class Flush(object):
    """The live floppies of a running MartyPC.

    Either attaches on its own (`Flush("127.0.0.1:9001")`) or SHARES a session's
    connection (`Flush(marty=m)`). Sharing is not a convenience: the debug
    server accepts one client and a second connection hangs rather than
    failing, so a script that drives the UI and then wants to look at the disk
    must pass its own `Marty` in.
    """

    def __init__(self, addr=None, marty=None, run_dir=None, timeout=None):
        if marty is None and addr is None:
            raise FlushError("give an address or a Marty to share")
        self.m = marty or os88marty.Marty(
            addr, timeout=timeout or os88marty.DEFAULT_TIMEOUT)
        self._own = marty is None
        if run_dir is None:
            here = os.path.dirname(os.path.abspath(__file__))
            run_dir = os.path.join(os.path.dirname(here), "build", "martypc", "run")
        self.run_dir = run_dir

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self._own:
            self.m.close()

    # --- the flush itself ----------------------------------------------------

    def disks(self):
        return self.m.disks()

    def _drive(self, drive):
        for d in self.disks():
            if d["drive"] == drive:
                return d
        raise FlushError("this machine has no floppy drive %d" % drive)

    def source_path(self, drive=0):
        """The host file the drive was mounted from, absolute.

        Under `launch()` that is the session's private copy in the run tree
        (`media/floppies/run0.img`), which is why it is worth resolving: it is
        both the reference a diff is against and the file a bare
        `save(drive)` writes back over.
        """
        d = self._drive(drive)
        p = d.get("path")
        if not p:
            raise FlushError(
                "drive %d was not mounted by the debug server, so there is no "
                "source image to compare against - pass one" % drive)
        return p if os.path.isabs(p) else os.path.join(self.run_dir, p)

    def save(self, drive=0, path=None, fmt="raw"):
        """Flush a drive to a file, with the machine held still.

        `path` defaults to the image the drive was mounted from - the menu's
        Save Floppy rather than its Save Floppy As. Note what that costs: it
        overwrites the only pristine copy in the run tree, so `diff` and
        `dirty` have nothing left to compare against afterwards. Name a path
        when you want to keep the reference.
        """
        if path is not None:
            path = os.path.abspath(path)
        st = self.m.status()["state"]
        running = st == "running"
        if running:
            self.m.pause()
        try:
            return self.m.flush(drive=drive, path=path, fmt=fmt)
        finally:
            if running:
                self.m.run()

    def snapshot(self, drive=0):
        """The drive's live image, as bytes, leaving nothing behind."""
        fd, tmp = tempfile.mkstemp(prefix="os88flush", suffix=".img")
        os.close(fd)
        try:
            self.save(drive, tmp)
            with open(tmp, "rb") as f:
                return f.read()
        finally:
            try:
                os.unlink(tmp)
            except OSError:
                pass

    def volume(self, drive=0):
        """The drive's live image, parsed - the verb most tests want."""
        return Volume(self.snapshot(drive), "drive %d" % drive)

    # --- what changed --------------------------------------------------------

    def diff(self, drive=0, ref=None):
        """What the guest has done to this disk since it was mounted.

        Answers at both altitudes, because they catch different things: the
        FILE list is what a test asserts on, and the SECTOR count is what says
        something happened at all when the file list is unchanged - a rewrite
        in place, a FAT touched, a directory entry restamped.
        """
        if ref is None:
            ref = self.source_path(drive)
        with open(ref, "rb") as f:
            before = f.read()
        after = self.snapshot(drive)

        sectors = sorted({i // SECTOR for i in range(min(len(before), len(after)))
                          if before[i] != after[i]})
        out = {"ref": ref, "sectors": sectors, "bytes": len(after),
               "added": [], "removed": [], "changed": []}
        try:
            va, vb = Volume(before, ref), Volume(after, "drive %d" % drive)
        except FlushError as e:
            out["note"] = "sector diff only: %s" % e
            return out

        old = {e.path: e for e in va.walk()}
        new = {e.path: e for e in vb.walk()}
        out["added"] = sorted(set(new) - set(old))
        out["removed"] = sorted(set(old) - set(new))
        for p in sorted(set(old) & set(new)):
            if old[p].is_dir or new[p].is_dir:
                continue
            if old[p].size != new[p].size or va.read(p) != vb.read(p):
                out["changed"].append(p)
        return out

    def dirty(self, drive=0, ref=None):
        """Has the guest written to this disk? By CONTENT, never by `writes`.

        The counter in `disks` cannot answer this (see the module docstring),
        and a test built on it passes for the wrong reason for ever.
        """
        return bool(self.diff(drive, ref)["sectors"])

    def verify(self, drive=0):
        """Flush and run os88disk's structural fsck over the result.

        The independent witness: os88disk walks the volume with no kernel code
        anywhere near it, so a disk os8088 is perfectly happy with and a disk
        that is actually coherent stop being the same claim.
        """
        import subprocess
        fd, tmp = tempfile.mkstemp(prefix="os88flush", suffix=".img")
        os.close(fd)
        try:
            self.save(drive, tmp)
            here = os.path.dirname(os.path.abspath(__file__))
            r = subprocess.run([sys.executable, os.path.join(here, "os88disk.py"),
                                "--verify", tmp],
                               capture_output=True, text=True)
            return r.returncode, (r.stdout + r.stderr).strip()
        finally:
            try:
                os.unlink(tmp)
            except OSError:
                pass


def _fmt_entry(e):
    kind = "<DIR>" if e.is_dir else "%7d" % e.size
    flags = "".join(c for c, bit in (("r", ATTR_RDONLY), ("h", ATTR_HIDDEN),
                                     ("s", ATTR_SYS)) if e.attr & bit)
    return "  %-14s %8s  %s" % (e.name, kind, flags)


def main():
    ap = argparse.ArgumentParser(
        description="Flush a running MartyPC's floppy to the host and read it.")
    ap.add_argument("addr", help="host:port of the debug server (e.g. 127.0.0.1:9001)")
    ap.add_argument("--run-dir", default=None,
                    help="MartyPC run tree, for resolving a mounted path "
                         "(default build/martypc/run)")
    sub = ap.add_subparsers(dest="op", required=True)

    sub.add_parser("disks", help="what is in each drive")
    p = sub.add_parser("save", help="write a drive's live image to a file")
    p.add_argument("drive", type=int)
    p.add_argument("out", nargs="?", default=None,
                   help="default: back over the image it was mounted from")
    p.add_argument("--format", default="raw",
                   choices=("raw", "imd", "psi", "pri", "mfm", "hfe", "86f"))
    p = sub.add_parser("ls", help="list a folder on the live disk")
    p.add_argument("drive", type=int)
    p.add_argument("path", nargs="?", default="")
    p.add_argument("-R", "--recursive", action="store_true")
    p = sub.add_parser("get", help="copy a file off the live disk")
    p.add_argument("drive", type=int)
    p.add_argument("file")
    p.add_argument("out")
    p = sub.add_parser("diff", help="what the guest changed since mount")
    p.add_argument("drive", type=int)
    p.add_argument("--against", default=None, help="reference image")
    p = sub.add_parser("verify", help="flush, then os88disk --verify")
    p.add_argument("drive", type=int)
    a = ap.parse_args()

    with Flush(a.addr, run_dir=a.run_dir) as f:
        if a.op == "disks":
            for d in f.disks():
                if not d["present"]:
                    print("fd%d: empty" % d["drive"])
                    continue
                print("fd%d: %s, %d cyl %d head, %s" %
                      (d["drive"], d.get("path", "?"), d.get("cylinders", 0),
                       d.get("heads", 0), d.get("format", "?")))
            return 0

        if a.op == "save":
            r = f.save(a.drive, a.out, a.format)
            print("fd%d -> %s (%d bytes, %s)" %
                  (a.drive, r["path"], r["bytes"], r["format"]))
            return 0

        if a.op == "ls":
            v = f.volume(a.drive)
            ents = list(v.walk(a.path)) if a.recursive else v.listdir(a.path)
            print("drive %d:%s  (%s, %d clusters of %d bytes)" %
                  (a.drive, "/" + a.path if a.path else "",
                   "FAT12" if v.fat12 else "FAT16", v.clusters, v.spc * SECTOR))
            for e in sorted(ents, key=lambda x: x.path):
                print(_fmt_entry(e) if not a.recursive
                      else "  %-28s %8s" % (e.path, "<DIR>" if e.is_dir else e.size))
            print("  %d entr%s" % (len(ents), "y" if len(ents) == 1 else "ies"))
            return 0

        if a.op == "get":
            data = f.volume(a.drive).read(a.file)
            with open(a.out, "wb") as fh:
                fh.write(data)
            print("%s -> %s (%d bytes)" % (a.file, a.out, len(data)))
            return 0

        if a.op == "diff":
            d = f.diff(a.drive, a.against)
            print("drive %d against %s" % (a.drive, d["ref"]))
            if "note" in d:
                print("  %s" % d["note"])
            print("  %d sector(s) differ%s" %
                  (len(d["sectors"]),
                   (": " + ", ".join(map(str, d["sectors"][:16])) +
                    (" ..." if len(d["sectors"]) > 16 else "")) if d["sectors"] else ""))
            for tag, key in (("added", "added"), ("removed", "removed"),
                             ("changed", "changed")):
                for p in d[key]:
                    print("  %-8s %s" % (tag, p))
            if not d["sectors"]:
                print("  the guest has not written to this disk")
            return 0

        if a.op == "verify":
            rc, out = f.verify(a.drive)
            print(out)
            return rc

    return 0


if __name__ == "__main__":
    sys.exit(main())
