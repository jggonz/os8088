#!/usr/bin/env python3
"""WHAT THE MACHINE LEARNS IS WRITTEN BACK TO ASSOC.DAT (SPEC.md 54.7.6).

    make && python3 tests/assocflush.py

A harvest that reads a package the volume's cache does not hold, and a search
that finds a program the cache did not place, used to teach RAM and nothing
else - so every boot paid for them again. Now asc_post marks the volume and
FORMAT.DRV's flush rebuilds the file. Five boots on MartyPC's 5150, the 720KB
system disk in A: and a scratch copy of apps720 in B:, every leg read OFF THE
FLOPPY the guest wrote (os88flush) and never off the kernel that wrote it:

  WRITE      apps720 with ASSOC.DAT deleted; browse B:\\APPS. Exactly one
             flush, and the file it writes holds the folder's rows exactly as
             tools/os88disk.py's own builder made them - size, folder cluster,
             icon, glyph - and the same association rows.
  QUIET      ...browse B:\\APPS again: no second flush (the store holds every
             body now, so nothing is taken).
  ONE FLUSH  the shipped cache plus ONE new package in B:\\ - and the flush's
             own int 13h count against WRITE's, where SEVENTEEN were new. It
             must not grow with what it found (54.7.6.2's whole point): the
             harvest pays a read per package, the flush pays directories.
  REBOOT     a fresh boot off the floppy WRITE wrote: from B:\\MEDIA, before
             APPS is ever browsed, .TEX opens with TEXPAD and .HTM with
             BROWSER; browsing B:\\APPS costs a third of the reads and posts
             nothing.
  LOCATE     the shipped cache with TEXPAD's row pointed at GAMES: a
             double-click on A:\\HELLO.TEX sweeps, finds it in B:\\APPS, and
             the flush corrects the row on disk. Then APPS and GAMES are
             browsed: no flush - a warm disk posts nothing.
  REFUSED    no cache and a FULL disk: one flush, the write refused, and
             browsing GAMES (more packages the store has never seen) posts no
             second one - [asc_wno] (54.7.6.4).

Broken on purpose, each must go red: the harvest's `call asc_learn` taken out
fails WRITE and REBOOT; the capture not kept fails ONE FLUSH (the flush reads
every declaring package's header again); the location trigger taken out
fails LOCATE; `[asc_wno]` never set fails REFUSED.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88ui, os88build, os88flush              # noqa: E402
from os88fat import Fat12                                     # noqa: E402
import fatdel                                                 # noqa: E402

MACHINE = "os8088_5150_herc_720_gla"
SYS = "build/os8088-720.img"
APPS = "build/apps720.img"
TAG = os.getpid()
SCRATCH = []


def scratch(name, src=APPS):
    p = os.path.join(ROOT, "build", "assocflush-%d-%s.img" % (TAG, name))
    with open(os88build.at(src), "rb") as f:
        data = f.read()
    with open(p, "wb") as f:
        f.write(data)
    SCRATCH.append(p)
    return p


def parse(b):
    """(rows, exts): rows keyed by stem, exts as (ext, stem) pairs."""
    if b[:6] != b"OS88AC" or b[6] != 2:
        raise ValueError("not a version 2 ASSOC.DAT: %r" % b[:7])
    na, ne = b[7], b[8]
    rows = {}
    order = []
    for i in range(na):
        o = 16 + i * 88
        r = dict(size=struct.unpack_from("<H", b, o + 8)[0],
                 clus=struct.unpack_from("<H", b, o + 10)[0],
                 icon=bytes(b[o + 16:o + 80]), glyph=bytes(b[o + 80:o + 88]))
        stem = bytes(b[o:o + 8])
        rows[stem] = r
        order.append(stem)
    eo = 16 + na * 88
    exts = set((bytes(b[eo + 4 * j:eo + 4 * j + 3]), order[b[eo + 4 * j + 3]])
               for j in range(ne))
    return rows, exts


def assoc_of(img_or_vol):
    v = img_or_vol
    if isinstance(v, str):
        with open(v, "rb") as f:
            v = os88flush.Volume(f.read())
    return parse(v.read("ASSOC.DAT"))


def first_cluster(fat, name):
    """A ROOT entry's first cluster (a folder's, or a file's)."""
    i, o, e = fat.find(name)
    if e is None:
        raise KeyError(name)
    return struct.unpack_from("<H", e, 26)[0]


class Flushes(object):
    """Every asc_flush the guest makes, and the floppy controller's count for
    each one alone - bracketed by breakpoints on the routine's entry and on
    `.drop`, the instruction after its `call .go`, so the harvest that posted
    it and the navigation around it are not in the figure. (It was
    `asc_flush + 3` until the hold mode put a test in front of the call; a
    hold or a release reaches `.drop` too, and no leg here makes one.)"""

    def __init__(self, m):
        self.m = m
        self.a = m.sym("asc_flush")
        self.b = m.sym("asc_flush.drop")
        self.done = []

    def hit(self, mm, rec):
        if rec.get("addr") == self.a:
            mm.disk(reset=True)
        elif rec.get("addr") == self.b:
            d = mm.disk()
            self.done.append(dict(reads=d["reads"], writes=d["writes"],
                                  cycles=rec.get("cycles")))

    def __enter__(self):
        self.tr = os88marty.bp_trace(self.m, self.a, self.b, on_hit=self.hit)
        self.tr.__enter__()
        return self

    def __exit__(self, *e):
        return self.tr.__exit__(*e)

    def wait(self, n, what, bad, limit=120.0):
        """CF-style: False and a FAIL line when the flush never comes."""
        try:
            self.tr.until(lambda: len(self.done) >= n, what, limit=limit)
            return True
        except os88marty.MartyError as e:
            bad.append("no flush: %s (%s)" % (what, e))
            return False

    def settle(self, guest=6.0):
        """No wait to time out on when the answer is NO flush: give the UI
        task the passes it would have spent and let the count stand."""
        os88marty.pace(self.m, guest)


NAPP, NEXT = 16, 32                 # ASSOC_NAPP, ASSOC_NEXT (kernel/assoc.inc)


def ext_map(ui):
    raw = ui.m.read(ui._S("assoc_ext"), NEXT * 4)
    stems = ui.m.read(ui._S("assoc_stem"), NAPP * 8)
    out = {}
    for j in range(NEXT):
        e = raw[4 * j:4 * j + 3]
        if e[0] == 0:
            continue
        ix = raw[4 * j + 3] & 0x7F
        out[bytes(e).decode()] = bytes(stems[8 * ix:8 * ix + 8]).decode().strip()
    return out


def leg_write(bad):
    """WRITE + QUIET: a cold disk, one browse, one flush, the right file."""
    img = scratch("all")
    fat = Fat12(img)
    fat.delete("ASSOC.DAT")
    fat.save()
    after = scratch("after")
    with os88ui.boot(SYS, apps=img, machine=MACHINE) as ui:
        fl = os88flush.Flush(marty=ui.m)
        with Flushes(ui.m) as w:
            ui.path("B:/APPS")
            w.wait(1, "the flush after B:\\APPS", bad)
            ui.path("B:/")
            ui.path("B:/APPS")
            w.settle()
            n = len(w.done)
            print("   WRITE: %d flush(es); the first %r"
                  % (n, w.done[0] if w.done else None))
            if n > 1:
                bad.append("QUIET: browsing B:\\APPS again flushed again "
                           "(%d flushes)" % n)
        fl.save(1, after)
    orig_rows, orig_exts = assoc_of(APPS)
    apps_clus = first_cluster(Fat12(img), "APPS")
    try:
        rows, exts = assoc_of(after)
    except Exception as e:
        bad.append("WRITE: no ASSOC.DAT on B: after the browse (%s)" % e)
        return None, w.done[0] if w.done else None
    # what the builder made for B:\APPS, less the iconless packages: a
    # package with no icon is never TAKEN into the store, so nothing in RAM
    # has learned it and an incremental flush leaves it out (54.7.6.4)
    want = {s: r for s, r in orig_rows.items()
            if r["clus"] == apps_clus and r["icon"] != bytes(64)}
    for s, r in want.items():
        if s not in rows:
            bad.append("WRITE: %s missing from the written file" % s)
        elif rows[s] != r:
            diff = [k for k in r if r[k] != rows[s][k]]
            bad.append("WRITE: %s differs from os88disk's row in %s"
                       % (s.decode().strip(), diff))
    extra = set(rows) - set(want)
    if extra:
        bad.append("WRITE: rows the folder does not hold: %s" % extra)
    if not rows:
        bad.append("WRITE: an empty file")
    want_exts = set(x for x in orig_exts if x[1] in rows)
    if exts != want_exts:
        bad.append("WRITE: association rows differ: only written %s, only "
                   "built %s" % (exts - want_exts, want_exts - exts))
    print("   WRITE: %d rows, %d association rows, all equal to the "
          "builder's: %s" % (len(rows), len(exts), not bad))
    return after, w.done[0] if w.done else None


def leg_one(bad, all_flush):
    """ONE FLUSH: one new package; the flush's cost against seventeen.

    ...and one package GONE: a package that is no longer on the disk must
    leave the file, and the walk is what drops it. (It was also what kept
    this leg off the cap while ASC_NAPP was 32 and apps720 held 32 packages;
    the cap is 55 now, SPEC.md 54.7.1, so that reason has gone and this one
    stands.)"""
    img = scratch("one")
    orig_rows, _ = assoc_of(APPS)
    fat = Fat12(img)
    folders = {}
    for i, o, e in fat.entries():
        if e[11] & 0x10:
            folders[struct.unpack_from("<H", e, 26)[0]] = \
                e[:8].decode().strip()
    gone = list(orig_rows)[-1]          # the file's last row is a PLAIN one
    fatdel.delete(img, "%s/%s.O88" % (folders[orig_rows[gone]["clus"]],
                                       gone.decode().strip()))
    fat = Fat12(img)
    fat.add(os88build.at("build/mines.o88"), "MINES2.O88")
    fat.save()
    after = scratch("oneafter")
    with os88ui.boot(SYS, apps=img, machine=MACHINE) as ui:
        fl = os88flush.Flush(marty=ui.m)
        with Flushes(ui.m) as w:
            ui.path("B:/")
            w.wait(1, "the flush after B:\\", bad)
            w.settle()
        fl.save(1, after)
    if not w.done:
        return
    one = w.done[0]
    print("   ONE FLUSH: one new -> %d reads %d writes; seventeen new -> "
          "%d reads %d writes" % (one["reads"], one["writes"],
                                  all_flush["reads"], all_flush["writes"]))
    if len(w.done) != 1:
        bad.append("ONE FLUSH: %d flushes for one browse" % len(w.done))
    # the 17-new flush may read the THREE built-in programs' headers (a slot
    # and no record) and finds no old file; one more for slack. A flush that
    # re-read the declaring packages it learned would be 5+ reads over
    if all_flush["reads"] > one["reads"] + 4:
        bad.append("ONE FLUSH: seventeen new packages cost %d reads against "
                   "one's %d - the flush is reading packages again"
                   % (all_flush["reads"], one["reads"]))
    rows, _ = assoc_of(after)
    if b"MINES2  " not in rows:
        bad.append("ONE FLUSH: MINES2 not written")
    if gone in rows:
        bad.append("ONE FLUSH: %s is off the disk and still in the file"
                   % gone.decode().strip())
    lost = [s for s in orig_rows if s not in rows and s != gone]
    changed = [s for s in orig_rows if s in rows and rows[s] != orig_rows[s]]
    if lost or changed:
        bad.append("ONE FLUSH: the old file's rows were not carried: lost "
                   "%s changed %s" % (lost, changed))


def leg_reboot(bad, after):
    """REBOOT: the next boot knows what the last one learned."""
    with os88ui.boot(SYS, apps=after, machine=MACHINE) as ui:
        with Flushes(ui.m) as w:
            ui.path("B:/MEDIA")
            e = ext_map(ui)
            ui.m.disk(reset=True)
            ui.path("B:/APPS")
            d = ui.m.disk()
            w.settle()
        print("   REBOOT: from B:\\MEDIA .TEX -> %s, .HTM -> %s; B:\\APPS "
              "%d reads; %d flushes" % (e.get("TEX"), e.get("HTM"),
                                         d["reads"], len(w.done)))
        if e.get("TEX") != "TEXPAD" or e.get("HTM") != "BROWSER":
            bad.append("REBOOT: the declarations were not on disk: TEX -> "
                       "%s, HTM -> %s" % (e.get("TEX"), e.get("HTM")))
        if d["reads"] > 12:
            bad.append("REBOOT: B:\\APPS still cost %d reads - the harvest "
                       "read its packages again" % d["reads"])
        if w.done:
            bad.append("REBOOT: a warm disk flushed (%d)" % len(w.done))


def leg_locate(bad):
    """LOCATE: a sweep's answer is written back, and a warm disk is quiet."""
    sysimg = scratch("sys", SYS)
    src = os.path.join(ROOT, "build", "assocflush-%d.tex" % TAG)
    SCRATCH.append(src)
    with open(src, "wb") as f:
        f.write(b"\\title{Hello}\n\nFound by a sweep.\n")
    fat = Fat12(sysimg)
    fat.add(src, "HELLO.TEX")
    fat.save()
    img = scratch("loc")
    fat = Fat12(img)
    games = first_cluster(fat, "GAMES")
    apps = first_cluster(fat, "APPS")
    data = bytearray(fat.read("ASSOC.DAT"))
    na = data[7]
    for i in range(na):
        o = 16 + i * 88
        if data[o:o + 8] == b"TEXPAD  ":
            struct.pack_into("<H", data, o + 10, games)
    pos = 0
    for c in fat.chain(first_cluster(fat, "ASSOC.DAT")):
        off = fat.cluster_off(c)
        n = min(fat.spc * fat.bps, len(data) - pos)
        fat.img[off:off + n] = data[pos:pos + n]
        pos += n
    fat.save()
    after = scratch("locafter")
    with os88ui.boot(sysimg, apps=img, machine=MACHINE) as ui:
        fl = os88flush.Flush(marty=ui.m)
        with Flushes(ui.m) as w:
            ui.path("A:")
            ui.open("HELLO.TEX", expect=None)
            os88marty.until(ui.m, lambda mm: any(
                "TeXPad" in t for t in ui.titles()), "TeXPad",
                poll=0.3, limit=300.0, guest=120.0)
            w.wait(1, "the flush the search posted", bad)
            ui.close(ui.window("TeXPad"))   # it covers B:'s desktop zone
            ui.path("B:/APPS")
            ui.path("B:/GAMES")
            w.settle()
            n = len(w.done)
        fl.save(1, after)
    rows, _ = assoc_of(after)
    got = rows.get(b"TEXPAD  ", {}).get("clus")
    print("   LOCATE: %d flush(es); TEXPAD's row names cluster %s (APPS is "
          "%d, GAMES %d)" % (n, got, apps, games))
    if got != apps:
        bad.append("LOCATE: TEXPAD's row was not corrected (cluster %s, "
                   "APPS is %d)" % (got, apps))
    if n != 1:
        bad.append("LOCATE: %d flushes - the search's one, then a warm disk "
                   "browsed should add none" % n)


def leg_refused(bad):
    """REFUSED: a full disk is tried once a session."""
    img = scratch("full")
    fat = Fat12(img)
    fat.delete("ASSOC.DAT")
    free = len(fat.free_clusters()) * fat.spc * fat.bps
    fill = os.path.join(ROOT, "build", "assocflush-%d.fill" % TAG)
    SCRATCH.append(fill)
    with open(fill, "wb") as f:
        f.write(b"\0" * free)
    fat.add(fill, "FILL.BIN")
    fat.save()
    with os88ui.boot(SYS, apps=img, machine=MACHINE) as ui:
        fl = os88flush.Flush(marty=ui.m)
        with Flushes(ui.m) as w:
            ui.path("B:/APPS")
            w.wait(1, "the flush after B:\\APPS", bad)
            ui.path("B:/GAMES")
            w.settle()
            n = len(w.done)
        wno = ui._byte("asc_wno")
        names = fl.volume(1).names()
    print("   REFUSED: %d flush(es), [asc_wno] = %d, ASSOC.DAT %s"
          % (n, wno, "written" if "ASSOC.DAT" in names else "absent"))
    if n != 1:
        bad.append("REFUSED: %d flushes on a full disk - it should be tried "
                   "once a session" % n)
    if wno != 1:
        bad.append("REFUSED: [asc_wno] = %d, not B:" % wno)
    if "ASSOC.DAT" in names:
        bad.append("REFUSED: a file was written on a full disk")


def main():
    os.chdir(ROOT)
    bad = []
    try:
        after, all_flush = leg_write(bad)
        if all_flush:
            leg_one(bad, all_flush)
        if after:
            leg_reboot(bad, after)
        leg_locate(bad)
        leg_refused(bad)
    finally:
        for p in SCRATCH:
            if os.path.exists(p):
                os.remove(p)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
