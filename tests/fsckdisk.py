#!/usr/bin/env python3
"""System Check's disk tasks (SPEC.md 118.14) - python3 tests/fsckdisk.py

Check the disk, Check and repair the disk and Defragment the disk, driven on
MartyPC the way a user drives them: A: is the 360KB system disk, B: a copy
of util360.img - where SYSCHECK.O88 and FSCHECK.DRV ship side by side at the
root, which is the case SPEC.md 118.14.1 exists for - and System Check is
opened off B: and checks B:, the disk it was launched from.

Every verdict is held to an INDEPENDENT FAT reader below (`Vol`), which
walks the BPB, both FAT copies and every folder itself and knows nothing of
the kernel's code - so the engine is judged against a second opinion, not
against its own report.

LEG 1 - Check, then repair, a disk with one of every fault planted:
  LOST.        three allocated clusters nothing names        Lost clusters
               (XB's and INFREE's orphaned second clusters make it 5 in 3)
  LONG.TXT     3 clusters, size says 1                       File chains, warning
  SHORT.TXT    2 clusters, size says 5000 bytes              File chains, broken
  XA/XB.TXT    XB's chain runs into XA's (XB's own 2nd lost) Cross-linked, broken
  INFREE.TXT   its chain runs into a free cluster            File chains, broken
  FAT2         one sector differs from FAT1                  FAT, warning
  SYSTEM\\DOS   its '..' names cluster 5, not the root        Folders, warning
The check must report each and write NOTHING (the image is compared byte for
byte, bar its own SYSCHECK.TXT); the repair must leave a disk the independent
reader finds clean but for the cross-link, which SPEC.md 118.14.4 leaves to
the user, with the lost chains saved as FILEnnnn.CHK.

LEG 2 - Defragment a clean disk with two files in pieces: afterwards no
movable file is in pieces, every file's bytes are what they were, both FAT
copies agree and the reader finds nothing wrong.

LEG 4 - A LONG chain whose tail a LATER file shares (SPEC.md 118.14.4):
  SHA.TXT      2 clusters, size says 1; SHB.TXT starts at SHA's second
  SZA.TXT      2 clusters, size says 0; SZB.TXT starts at SZA's second
  LONGOK.TXT   3 clusters, size says 1, shared with nothing
Check and repair must cut LONGOK.TXT and leave both pairs whole: SHA/SZA
still LONG, SHB/SZB still cross-linked, every byte of SHB and SZB intact -
the repair once freed the shared tail before it reached the second entry,
and then cut that entry to nothing as a chain into free space.

LEG 5 - A live file in a folder nested FSK_MAXDEP deep, and a lost chain
(SPEC.md 118.14.4): the folder is not gone into, so its file's chain looks
lost. Check and repair must save and free nothing (the file is still there,
byte for byte, and no FILEnnnn.CHK appears), and Defragment must refuse,
both saying a folder's contents were not checked.

    python3 tests/fsckdisk.py            every leg
    python3 tests/fsckdisk.py --leg 1    one of them
    python3 tests/fsckdisk.py --build D  write the fixtures into D and stop
"""
import argparse
import hashlib
import os
import random
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))


def b(name):
    import os88build
    return os.path.join(ROOT, os88build.at(os.path.join("build", name)))


# =============================================================================
# an independent FAT12/16 reader and editor
# =============================================================================
class Vol(object):
    def __init__(self, img):
        self.img = bytearray(img)
        b = self.img
        self.bps = struct.unpack_from("<H", b, 11)[0]
        self.spc = b[13]
        self.rsv = struct.unpack_from("<H", b, 14)[0]
        self.nfat = b[16]
        self.nroot = struct.unpack_from("<H", b, 17)[0]
        tot = struct.unpack_from("<H", b, 19)[0] or \
            struct.unpack_from("<I", b, 32)[0]
        self.spf = struct.unpack_from("<H", b, 22)[0]
        self.root_lba = self.rsv + self.nfat * self.spf
        self.data_lba = self.root_lba + (self.nroot * 32 + 511) // 512
        self.nclus = (tot - self.data_lba) // self.spc
        self.maxc = self.nclus + 1
        self.fat16 = self.nclus >= 4085
        self.cb = self.spc * self.bps
        self.eoc = 0xFFF8 if self.fat16 else 0xFF8
        self.bad = 0xFFF7 if self.fat16 else 0xFF7

    # --- the FAT -----------------------------------------------------------
    def _fo(self, copy):
        return (self.rsv + copy * self.spf) * self.bps

    def get(self, n, copy=0):
        o = self._fo(copy)
        if self.fat16:
            return struct.unpack_from("<H", self.img, o + 2 * n)[0]
        v = struct.unpack_from("<H", self.img, o + n + n // 2)[0]
        return (v >> 4) if n & 1 else (v & 0xFFF)

    def put(self, n, val, copies=None):
        for f in (range(self.nfat) if copies is None else copies):
            o = self._fo(f)
            if self.fat16:
                struct.pack_into("<H", self.img, o + 2 * n, val)
                continue
            p = o + n + n // 2
            v = struct.unpack_from("<H", self.img, p)[0]
            v = ((val << 4) | (v & 0xF)) if n & 1 else ((v & 0xF000) | val)
            struct.pack_into("<H", self.img, p, v)

    def free(self):
        return [c for c in range(2, self.maxc + 1) if self.get(c) == 0]

    def coff(self, c):
        return (self.data_lba + (c - 2) * self.spc) * self.bps

    # --- folders -----------------------------------------------------------
    def dir_offsets(self, clus):
        """The byte offset of every entry of the folder at `clus` (0 = root),
        following its chain - stopping, like any sane reader, on a cluster
        out of range or seen twice."""
        if clus == 0:
            base = self.root_lba * self.bps
            return [base + 32 * i for i in range(self.nroot)]
        out, seen, c = [], set(), clus
        while 2 <= c <= self.maxc and c not in seen:
            seen.add(c)
            base = self.coff(c)
            out += [base + 32 * i for i in range(self.cb // 32)]
            c = self.get(c)
        return out

    def entries(self, clus):
        for o in self.dir_offsets(clus):
            e = self.img[o:o + 32]
            if e[0] == 0:
                return
            if e[0] == 0xE5 or e[11] == 0x0F or e[11] & 0x08:
                continue
            yield o, bytes(e)

    @staticmethod
    def name(e):
        n = e[:8].decode("latin1").rstrip()
        x = e[8:11].decode("latin1").rstrip()
        return n + ("." + x if x else "")

    def walk(self, clus=0, path="", depth=0):
        """(path, offset, entry, parent) for every file and folder."""
        for o, e in self.entries(clus):
            if e[0] == ord("."):
                continue
            p = path + "\\" + self.name(e)
            yield p, o, e, clus
            if e[11] & 0x10 and depth < 16:
                c = struct.unpack_from("<H", e, 26)[0]
                if 2 <= c <= self.maxc:
                    yield from self.walk(c, p, depth + 1)

    def find(self, path):
        for p, o, e, _ in self.walk():
            if p.upper() == path.upper():
                return o, e
        return None, None

    def chain(self, first):
        out, c = [], first
        while 2 <= c <= self.maxc and c not in out and len(out) <= self.maxc:
            out.append(c)
            v = self.get(c)
            if v >= self.eoc or v == 0 or v == self.bad:
                break
            c = v
        return out

    def read(self, path):
        o, e = self.find(path)
        size = struct.unpack_from("<I", e, 28)[0]
        data = b"".join(bytes(self.img[self.coff(c):self.coff(c) + self.cb])
                        for c in self.chain(struct.unpack_from("<H", e, 26)[0]))
        return data[:size]

    # --- the edits the fixtures plant ----------------------------------------
    def _slot(self, parent):
        """A free entry's offset in the folder at `parent` (0 = the root);
        taking an end mark keeps the next entry one."""
        offs = self.dir_offsets(parent)
        for k, o in enumerate(offs):
            if self.img[o] == 0:
                if k + 1 < len(offs):
                    self.img[offs[k + 1]] = 0
                return o
            if self.img[o] == 0xE5:
                return o
        raise ValueError("the folder is full")

    @staticmethod
    def entry(name, attr, first, size):
        if name in (".", ".."):
            n, x = name, ""
        else:
            n, _, x = name.partition(".")
        ent = bytearray(32)
        ent[0:11] = (n.ljust(8) + x.ljust(3)).encode("latin1")
        ent[11] = attr
        struct.pack_into("<H", ent, 24, 0x5C21)
        struct.pack_into("<H", ent, 26, first)
        struct.pack_into("<I", ent, 28, size)
        return ent

    def write_chain(self, use, data):
        need = len(use)
        for k, c in enumerate(use):
            self.put(c, self.eoc | 0xF if k == need - 1 else use[k + 1])
            chunk = data[k * self.cb:(k + 1) * self.cb]
            self.img[self.coff(c):self.coff(c) + self.cb] = \
                chunk + bytes(self.cb - len(chunk))

    def add(self, name, data, clusters=None, parent=0, attr=0x20):
        need = (len(data) + self.cb - 1) // self.cb
        use = clusters if clusters is not None else self.free()[:need]
        assert len(use) == need, "not enough room on the fixture"
        self.write_chain(use, data)
        o = self._slot(parent)
        self.img[o:o + 32] = self.entry(name, attr, use[0] if use else 0,
                                        len(data))
        return o, use

    def mkdir(self, name, parent=0):
        c = self.free()[0]
        self.put(c, self.eoc | 0xF)
        self.img[self.coff(c):self.coff(c) + self.cb] = bytes(self.cb)
        o = self._slot(parent)
        self.img[o:o + 32] = self.entry(name, 0x10, c, 0)
        d = self.coff(c)
        self.img[d:d + 32] = self.entry(".", 0x10, c, 0)
        self.img[d + 32:d + 64] = self.entry("..", 0x10, parent, 0)
        return c

    def relocate(self, path, use):
        """Move a file's bytes onto the clusters `use` (free ones), freeing
        the old chain: how a fixture makes a file FRAGMENTED or puts it
        anywhere it likes. Returns (old first cluster, entry offset)."""
        o, e = self.find(path)
        data = self.read(path)
        old = self.chain(struct.unpack_from("<H", e, 26)[0])
        for c in old:
            self.put(c, 0)
        self.write_chain(use, data)
        struct.pack_into("<H", self.img, o + 26, use[0])
        return old[0], o

    def delete(self, name):
        o, e = self.find("\\" + name)
        for c in self.chain(struct.unpack_from("<H", e, 26)[0]):
            self.put(c, 0)
        self.img[o] = 0xE5


def check(v):
    """What a CHKDSK finds, by the independent reader: a dict of lists."""
    r = {"lost": [], "xlink": [], "long": [], "short": [], "broken": [],
         "frag": [], "dfrag": [], "dot": [], "fatdiff": 0, "files": 0,
         "dirs": 0}
    owner = {}
    for path, o, e, parent in v.walk():
        isdir = bool(e[11] & 0x10)
        r["dirs" if isdir else "files"] += 1
        fc = struct.unpack_from("<H", e, 26)[0]
        size = struct.unpack_from("<I", e, 28)[0]
        if isdir:
            c = fc
            for k, (oo, ee) in enumerate(list(v.entries(c))[:2]):
                want = c if k == 0 else parent
                if ee[0] == ord(".") and \
                        struct.unpack_from("<H", ee, 26)[0] != want:
                    r["dot"].append(path)
        n, c, prev, pieces, ok = 0, fc, None, 0, True
        while c:
            if not 2 <= c <= v.maxc:
                r["broken"].append(path)
                ok = False
                break
            if c in owner:
                r["xlink"].append(path)
                ok = False
                break
            val = v.get(c)
            if val == 0 or val == v.bad:
                r["broken"].append(path)
                ok = False
                break
            owner[c] = path
            n += 1
            if prev is None or c != prev + 1:
                pieces += 1
            prev = c
            if val >= v.eoc:
                break
            c = val
        if isdir and ok and pieces > 1:
            r["dfrag"].append(path)
        if not ok or isdir:
            continue
        need = (size + v.cb - 1) // v.cb
        if n > need:
            r["long"].append(path)
        elif n < need:
            r["short"].append(path)
        if pieces > 1:
            r["frag"].append(path)
    for c in range(2, v.maxc + 1):
        val = v.get(c)
        if val and val != v.bad and c not in owner:
            r["lost"].append(c)
    a = v.img[v._fo(0):v._fo(0) + v.spf * v.bps]
    if v.nfat > 1:
        bb = v.img[v._fo(1):v._fo(1) + v.spf * v.bps]
        r["fatdiff"] = sum(1 for s in range(v.spf)
                           if a[s * 512:(s + 1) * 512] != bb[s * 512:(s + 1) * 512])
    return r


# =============================================================================
# the fixtures
# =============================================================================
def blob(n, seed):
    rnd = random.Random(seed)
    return bytes(rnd.getrandbits(8) for _ in range(n))


def broken_fixture(base=None):
    v = Vol(open(base or b("util360.img"), "rb").read())
    cb = v.cb
    assert len(v.free()) > 40, "util360.img has no room for the fixture"
    # LOST: three chained clusters, no entry
    lost = v.free()[:3]
    v.put(lost[0], lost[1])
    v.put(lost[1], lost[2])
    v.put(lost[2], v.eoc | 0xF)
    # LONG: three clusters, a size that needs one
    o, _ = v.add("LONG.TXT", blob(3 * cb, 1))
    struct.pack_into("<I", v.img, o + 28, 100)
    # SHORT: two clusters, a size that needs five
    o, _ = v.add("SHORT.TXT", blob(2 * cb, 2))
    struct.pack_into("<I", v.img, o + 28, 5 * cb)
    # XA, XB: XB's first cluster links into XA's chain
    _, xa = v.add("XA.TXT", blob(2 * cb, 3))
    _, xb = v.add("XB.TXT", blob(2 * cb, 4))
    v.put(xb[0], xa[0])
    # INFREE: its first cluster links to a free one
    _, fr = v.add("INFREE.TXT", blob(2 * cb, 5))
    v.put(fr[0], v.free()[-1])
    # SYSTEM\DOS's '..' names cluster 5
    o, e = v.find("\\SYSTEM\\DOS")
    assert e is not None, "util360.img has no SYSTEM\\DOS"
    c = struct.unpack_from("<H", e, 26)[0]
    dd = v.dir_offsets(c)[1]
    assert v.img[dd:dd + 2] == b"..", "SYSTEM\\DOS's second entry is not '..'"
    assert struct.unpack_from("<H", v.img, dd + 26)[0] != 5
    struct.pack_into("<H", v.img, dd + 26, 5)
    # FAT2 differs: a far, free cluster marked in the second copy alone
    v.put(v.free()[-2], 0x123, copies=[1])
    return bytes(v.img)


def folder_in_pieces(v):
    """\\FOLD: a folder of two clusters FAR APART, holding a file and a
    child folder (with a file of its own), so moving it has a '.' to
    rewrite, a child's '..' to rewrite and bytes to keep (SPEC.md 118.14.5)."""
    fold = v.mkdir("FOLD")
    far = v.free()[-3]
    v.put(fold, far)
    v.put(far, v.eoc | 0xF)
    v.img[v.coff(far):v.coff(far) + v.cb] = bytes(v.cb)
    v.add("NOTE.TXT", blob(v.cb + 50, 16), parent=fold)
    child = v.mkdir("CHILD", parent=fold)
    v.add("DEEP.TXT", blob(300, 17), parent=child)
    return fold


def shared_tail_fixture(base=None):
    """LEG 4: two LONG chains whose tails a LATER entry shares, one of them
    of size 0, and a LONG chain shared with nothing (SPEC.md 118.14.4)."""
    v = Vol(open(base or b("util360.img"), "rb").read())
    cb = v.cb
    for a, bn, size, seed in (("SHA.TXT", "SHB.TXT", cb, 21),
                              ("SZA.TXT", "SZB.TXT", 0, 23)):
        o, use = v.add(a, blob(2 * cb, seed))
        struct.pack_into("<I", v.img, o + 28, size)
        o = v._slot(0)                  # the later entry: the tail is its
        v.img[o:o + 32] = v.entry(bn, 0x20, use[1], cb)
    o, _ = v.add("LONGOK.TXT", blob(3 * cb, 25))
    struct.pack_into("<I", v.img, o + 28, 100)
    r = check(v)
    assert sorted(r["long"]) == ["\\LONGOK.TXT", "\\SHA.TXT", "\\SZA.TXT"], r
    assert sorted(r["xlink"]) == ["\\SHB.TXT", "\\SZB.TXT"], r
    return bytes(v.img)


DEEPN = 16                              # FSK_MAXDEP: the 16th is not gone into


def deep_fixture(base=None):
    """LEG 5: folders D1..D16 nested, a live file in the last, and a lost
    chain of two clusters. Returns (image, the deep file's path)."""
    v = Vol(open(base or b("util360.img"), "rb").read())
    parent, path = 0, ""
    for k in range(1, DEEPN + 1):
        name = "D%d" % k
        parent = v.mkdir(name, parent=parent)
        path += "\\" + name
    v.add("LIVE.TXT", blob(2 * v.cb + 40, 31), parent=parent)
    lost = v.free()[:2]
    v.put(lost[0], lost[1])
    v.put(lost[1], v.eoc | 0xF)
    return bytes(v.img), path + "\\LIVE.TXT"


def frag_fixture(base=None):
    v = Vol(open(base or b("util360.img"), "rb").read())
    cb = v.cb
    v.add("F1.BIN", blob(4 * cb, 11))
    v.add("F2.BIN", blob(3 * cb, 12))
    v.add("F3.BIN", blob(4 * cb, 13))
    v.delete("F2.BIN")
    v.add("F4.BIN", blob(6 * cb + 77, 14))     # F2's three, and past F3
    v.delete("F1.BIN")
    v.add("F5.BIN", blob(5 * cb + 9, 15))      # F1's four, and one more
    folder_in_pieces(v)
    r = check(v)
    assert set(r["frag"]) >= {"\\F4.BIN", "\\F5.BIN"}, r["frag"]
    assert r["dfrag"] == ["\\FOLD"], r["dfrag"]
    return bytes(v.img)


# =============================================================================
# driving System Check on MartyPC
# =============================================================================
TASKS = {1: "Check the disk", 2: "Check and repair the disk",
         3: "Defragment the disk"}


def run_task(ui, flush, task, first, fails, limit=900.0, also=None):
    """Choose B: and task (1-3), Scan, then Save Report until the report
    names this task and a result. Returns the report's text."""
    import os88marty
    m = ui.m
    if not first:
        m.key("KeyS")                   # Scan after a scan: the chooser
        os88marty.guest_sleep(m, 1.0)
    else:
        m.key("ArrowDown")              # A: is offered (it booted); B: next
        os88marty.guest_sleep(m, 0.5)
    m.key("Digit%d" % (task + 1))       # the task's row: 1 is Release files
    os88marty.guest_sleep(m, 0.5)
    m.key("Enter")
    want = "Task: " + TASKS[task]
    waited = 0.0
    while waited < limit:
        os88marty.guest_sleep(m, 5.0)
        waited += 5.0
        m.key("KeyR")                   # greyed while the scan runs
        os88marty.guest_sleep(m, 3.0)
        waited += 3.0
        try:
            text = flush.volume(1).read("SYSCHECK.TXT").decode("latin1")
        except Exception:               # noqa: BLE001 - not written yet
            continue
        if want in text and "Result:" in text and \
                (also is None or also in text):
            return text
    fails.append("task %d (%s) never wrote its report in %ds of guest time"
                 % (task, TASKS[task], limit))
    return ""


def need(fails, text, words, what):
    for w in words:
        if w not in text:
            fails.append("%s: the report does not say %r" % (what, w))


def leg1(work, fails):
    import os88flush
    import os88ui
    img = os.path.join(work, "broken.img")
    open(img, "wb").write(broken_fixture())
    before = check(Vol(open(img, "rb").read()))
    assert before["lost"] and before["xlink"] and before["long"] and \
        before["short"] and before["broken"] and before["fatdiff"] and \
        before["dot"], before
    v0 = Vol(open(img, "rb").read())
    sysclus = struct.unpack_from("<H", v0.find("\\SYSTEM")[1], 26)[0]
    with os88ui.boot(b("os8088-360.img"), apps=img) as ui:
        flush = os88flush.Flush(marty=ui.m)
        ui.path("B:/SYSCHECK.O88")
        pristine = flush.snapshot(1)
        # --- check: every fault named, nothing written -----------------------
        text = run_task(ui, flush, 1, True, fails)
        print(text)
        need(fails, text, [
            "FAT copies differ in 1 of",
            "B:\\SYSTEM\\DOS\\  dot entry names cluster 5, not %d"
            % sysclus,
            "B:\\LONG.TXT  3 clusters, its size needs 1",
            "B:\\SHORT.TXT  size 5,120 bytes, chain holds 2,048",
            "B:\\XB.TXT  shares cluster",
            "B:\\INFREE.TXT  chain runs into free cluster",
            "5 lost clusters in 3 chains",
            "not asked",
        ], "check")
        after = flush.snapshot(1)
        va, vb = Vol(pristine), Vol(after)
        if check(vb)["fatdiff"] != before["fatdiff"] or \
                check(vb)["lost"] != before["lost"]:
            fails.append("Check the disk changed the disk")
        # --- repair ------------------------------------------------------------
        text = run_task(ui, flush, 2, False, fails)
        print(text)
        need(fails, text, ["(fixed)", "saved", "8 repairs made",
                           "Result: 1 broken, 0 warnings",
                           "B:\\XB.TXT  shares cluster"], "repair")
        v = Vol(flush.snapshot(1))
    r = check(v)
    for k in ("lost", "long", "short", "broken", "dot"):
        if r[k]:
            fails.append("after repair the reader still finds %s: %r" % (k, r[k]))
    if r["fatdiff"]:
        fails.append("after repair the two FAT copies still differ")
    if r["xlink"] != ["\\XB.TXT"]:
        fails.append("the cross-link should be left for the user: %r"
                     % r["xlink"])
    chk = sorted(p for p, _, e, _ in v.walk() if p.endswith(".CHK"))
    if len(chk) != 3:
        fails.append("the three lost chains should be FILEnnnn.CHK: %r" % chk)
    sz = struct.unpack_from("<I", v.find("\\SHORT.TXT")[1], 28)[0]
    if sz != 2 * v.cb:
        fails.append("SHORT.TXT's size should be cut to %d, is %d"
                     % (2 * v.cb, sz))
    if len(v.chain(struct.unpack_from("<H", v.find("\\LONG.TXT")[1], 26)[0])) != 1:
        fails.append("LONG.TXT's chain should be cut to one cluster")


def leg2(work, fails):
    """Defragment, twice. Disk window 1 stands IN \\FOLD and the Task
    Manager is open: the first run moves files and must leave folders where
    they are, saying why (a program may hold a folder's cluster where the
    kernel cannot see it, SPEC.md 118.14.5). Then the Task Manager closes and
    the second run moves \\FOLD - and window 1's FS_CWD, a kernel word, must
    follow it to the new run."""
    import os88flush
    import os88ui
    img = os.path.join(work, "frag.img")
    open(img, "wb").write(frag_fixture())
    v0 = Vol(open(img, "rb").read())
    sums = {p: hashlib.sha1(v0.read(p)).hexdigest()
            for p, _, e, _ in v0.walk() if not e[11] & 0x10}
    fold0 = struct.unpack_from("<H", v0.find("\\FOLD")[1], 26)[0]
    with os88ui.boot(b("os8088-360.img"), apps=img) as ui:
        flush = os88flush.Flush(marty=ui.m)
        wf = ui.path("B:/FOLD")                 # window 1, standing in FOLD
        if ui.fs_of(wf)[1] != fold0:
            fails.append("window 1 is not standing in FOLD")
        wr = ui.open_drive("B")                 # window 2, at the root:
        tm = ui.open("TASKMGR.O88", win=wr)     # another program open, and
        ui.raise_window(wr)                     # System Check from the same
        sc = ui.open("SYSCHECK.O88", win=wr)    # window (a program can cover
                                                # the drive's desktop zone)
        text = run_task(ui, flush, 3, True, fails)
        print(text)
        need(fails, text, ["files moved, 0 left in pieces",
                           "folders not moved: close other programs (1)"],
             "defragment with a program open")
        v1 = Vol(flush.snapshot(1))
        if struct.unpack_from("<H", v1.find("\\FOLD")[1], 26)[0] != fold0:
            fails.append("FOLD moved while another program was open")
        ui.close(tm)
        ui.raise_window(sc)
        text = run_task(ui, flush, 3, False, fails, also="folders moved")
        print(text)
        need(fails, text, ["folders moved", "0 left in pieces"],
             "defragment with nothing else open")     # FOLD, and any folder
                                                      # pass 2 packs lower
        v = Vol(flush.snapshot(1))
        fold = struct.unpack_from("<H", v.find("\\FOLD")[1], 26)[0]
        if fold == fold0:
            fails.append("FOLD did not move")
        cwd = ui.fs_of(wf)[1]
        if cwd != fold:
            fails.append("window 1's FS_CWD names %d, FOLD is at %d (was %d)"
                         % (cwd, fold, fold0))
    r = check(v)
    for k in ("lost", "xlink", "long", "short", "broken", "dot", "dfrag"):
        if r[k]:
            fails.append("after defragmenting the reader finds %s: %r"
                         % (k, r[k]))
    if r["fatdiff"]:
        fails.append("after defragmenting the FAT copies differ")
    movable = [p for p, _, e, _ in v.walk()
               if not e[11] & 0x10 and not p.endswith(".IMG")
               and not p.endswith("SYSCHECK.TXT")]
    left = [p for p in r["frag"] if p in movable]
    if left:
        fails.append("still in pieces after defragmenting: %r" % left)
    for p, s in sums.items():
        if p.endswith("SYSCHECK.TXT"):
            continue
        if hashlib.sha1(v.read(p)).hexdigest() != s:
            fails.append("%s's bytes changed in the move" % p)


def leg3(work, fails):
    """A WRITE BETWEEN TWO STEPS ENDS THE RUN (SPEC.md 118.14.6), both halves:
    disk_write_x moves B:'s write generation (System Check's own report is a
    write), and a generation that moved mid-run - another writer, simulated by
    bumping [dsk_wgen+1] while a check runs - ends it with 'the disk changed'
    rather than a picture of a disk that is no longer there."""
    import os88flush
    import os88marty
    import os88ui
    img = os.path.join(work, "frag.img")
    open(img, "wb").write(frag_fixture())
    with os88ui.boot(b("os8088-360.img"), apps=img) as ui:
        m = ui.m
        flush = os88flush.Flush(marty=m)
        gen = m.sym("dsk_wgen") + 1                     # B:'s
        ui.path("B:/SYSCHECK.O88")
        m.key("ArrowDown")
        os88marty.guest_sleep(m, 0.5)
        m.key("Digit2")                                 # Check the disk
        os88marty.guest_sleep(m, 0.5)
        m.key("Enter")
        for _ in range(16):                             # "somebody wrote B:",
            os88marty.guest_sleep(m, 0.25)              # again and again: one
            g = m.read(gen, 1)[0]                       # before the run's
            m.write(gen, bytes([(g + 1) & 0xFF]))       # BEGIN is harmless,
        before = m.read(gen, 1)[0]                      # one inside it is not
        text = run_task_wait(ui, flush, "Check the disk", fails,
                             also="the disk changed")
        print(text)
        need(fails, text, ["the disk changed - scan again"],
             "a write mid-run")
        if m.read(gen, 1)[0] == before:
            fails.append("saving SYSCHECK.TXT onto B: did not move B:'s write "
                         "generation - disk_write_x does not bump it")


def leg4(work, fails):
    """A LONG chain's tail shared by a later entry is not cut (118.14.4)."""
    import os88flush
    import os88ui
    img = os.path.join(work, "shared.img")
    open(img, "wb").write(shared_tail_fixture())
    v0 = Vol(open(img, "rb").read())
    keep = {p: v0.read(p) for p in ("\\SHB.TXT", "\\SZB.TXT")}
    with os88ui.boot(b("os8088-360.img"), apps=img) as ui:
        flush = os88flush.Flush(marty=ui.m)
        ui.path("B:/SYSCHECK.O88")
        text = run_task(ui, flush, 2, True, fails)
        print(text)
        need(fails, text, [
            "B:\\LONGOK.TXT  3 clusters, its size needs 1 (fixed)",
            "B:\\SHA.TXT  2 clusters, its size needs 1",
            "B:\\SZA.TXT  2 clusters, its size needs 0",
            "B:\\SHB.TXT  shares cluster",
            "B:\\SZB.TXT  shares cluster",
        ], "repair with shared tails")
        for p in ("SHA", "SZA"):
            if "B:\\%s.TXT  2 clusters, its size needs %d (fixed)" \
                    % (p, 1 if p == "SHA" else 0) in text:
                fails.append("%s.TXT's shared tail was cut" % p)
        v = Vol(flush.snapshot(1))
    r = check(v)
    if sorted(r["long"]) != ["\\SHA.TXT", "\\SZA.TXT"]:
        fails.append("after repair LONG should be SHA and SZA alone: %r"
                     % r["long"])
    if sorted(r["xlink"]) != ["\\SHB.TXT", "\\SZB.TXT"]:
        fails.append("the cross-links should be left for the user: %r"
                     % r["xlink"])
    for k in ("lost", "short", "broken"):
        if r[k]:
            fails.append("after repair the reader finds %s: %r" % (k, r[k]))
    for p, data in keep.items():
        o, e = v.find(p)
        if e is None:
            fails.append("%s is gone" % p)
        elif v.read(p) != data:
            fails.append("%s lost its bytes: size %d, was %d"
                         % (p, struct.unpack_from("<I", e, 28)[0], len(data)))
    if len(v.chain(struct.unpack_from("<H", v.find("\\LONGOK.TXT")[1],
                                      26)[0])) != 1:
        fails.append("LONGOK.TXT's chain should be cut to one cluster")


def leg5(work, fails):
    """A folder not gone into: lost chains stay, no defragment (118.14.4)."""
    import os88flush
    import os88ui
    img = os.path.join(work, "deep.img")
    raw, live = deep_fixture()
    open(img, "wb").write(raw)
    v0 = Vol(raw)
    data = v0.read(live)
    clus = v0.chain(struct.unpack_from("<H", v0.find(live)[1], 26)[0])
    lost0 = check(v0)["lost"]
    with os88ui.boot(b("os8088-360.img"), apps=img) as ui:
        flush = os88flush.Flush(marty=ui.m)
        ui.path("B:/SYSCHECK.O88")
        text = run_task(ui, flush, 2, True, fails)
        print(text)
        need(fails, text, ["nested %d deep - its contents not checked" % DEEPN,
                           "not done: a folder's contents were not checked"],
             "repair with a folder too deep")
        if "lost clusters freed" in text or " clusters saved" in text:
            fails.append("lost clusters were saved or freed after a folder "
                         "was left unread")
        v = Vol(flush.snapshot(1))
        if v.read(live) != data or \
                v.chain(struct.unpack_from("<H", v.find(live)[1], 26)[0]) != clus:
            fails.append("%s lost its chain or its bytes" % live)
        if any(v.get(c) == 0 for c in clus + lost0):
            fails.append("a cluster the unread folder may name was freed")
        chk = [p for p, _, e, _ in v.walk() if p.endswith(".CHK")]
        if chk:
            fails.append("lost chains were saved as %r" % chk)
        text = run_task(ui, flush, 3, False, fails,
                        also="not done: a folder's")
        print(text)
        need(fails, text, ["not done: a folder's contents were not checked"],
             "defragment with a folder too deep")
        v = Vol(flush.snapshot(1))
    if v.read(live) != data:
        fails.append("%s's bytes changed in a refused defragment" % live)


def run_task_wait(ui, flush, task, fails, also=None, limit=300.0):
    """run_task's waiting half, for a run already started."""
    import os88marty
    m = ui.m
    waited = 0.0
    while waited < limit:
        os88marty.guest_sleep(m, 4.0)
        waited += 4.0
        m.key("KeyR")
        os88marty.guest_sleep(m, 2.0)
        waited += 2.0
        try:
            text = flush.volume(1).read("SYSCHECK.TXT").decode("latin1")
        except Exception:               # noqa: BLE001 - not written yet
            continue
        if "Task: " + task in text and (also is None or also in text):
            return text
    fails.append("the %s run never wrote its report" % task)
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--leg", type=int, choices=(1, 2, 3, 4, 5))
    ap.add_argument("--build", metavar="DIR")
    a = ap.parse_args()
    if a.build:
        os.makedirs(a.build, exist_ok=True)
        open(os.path.join(a.build, "broken.img"), "wb").write(broken_fixture())
        open(os.path.join(a.build, "frag.img"), "wb").write(frag_fixture())
        open(os.path.join(a.build, "shared.img"), "wb").write(
            shared_tail_fixture())
        open(os.path.join(a.build, "deep.img"), "wb").write(deep_fixture()[0])
        print("fsckdisk: fixtures in %s" % a.build)
        return 0
    work = tempfile.mkdtemp(prefix="fsck-")
    fails = []
    try:
        if a.leg in (None, 1):
            leg1(work, fails)
        if a.leg in (None, 2):
            leg2(work, fails)
        if a.leg in (None, 3):
            leg3(work, fails)
        if a.leg in (None, 4):
            leg4(work, fails)
        if a.leg in (None, 5):
            leg5(work, fails)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        for f in fails:
            print("FAIL: " + f)
        return 1
    print("fsckdisk: checked, repaired and defragmented, every result held "
          "to an independent FAT reader")
    return 0


if __name__ == "__main__":
    sys.exit(main())
