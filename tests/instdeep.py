#!/usr/bin/env python3
"""The installer reproduces the source disk's WHOLE tree, and its BYTES.

SPEC.md 52.10.13.

An install used to walk the root and one level of folders, so a folder inside
a folder was never made on the destination.  Two are shipped and both went
missing with no error anywhere:

  * SYSTEM/APPDATA (SPEC.md 19.9), which is EMPTY - so nothing that counted
    bytes could have noticed - and whose absence reads as "preferences do not
    stick on a hard-disk machine" in every application at once;
  * SYSTEM/DOS/OS88NET.COM (SPEC.md 62), which is not.

A third arrived later and is the widest of them: SYSTEM/FONTS (SPEC.md 19.8.1)
is eleven files on the SYSTEM disk rather than the apps one, so it is the first
nested folder with real content on the volume the installer is copying FROM.
Losing it is silent in the same way the other two are - every Font menu on the
installed machine falls back to the kernel's own 8x8 cell and says nothing
about why.

So this drives a real install on MartyPC's XT-IDE machine and then reads the
partition back ON THE HOST, with a FAT reader that is not the one that wrote
it.  A screendump cannot answer this question: the installer says `Done` in
both the broken and the fixed case, and the difference is a directory entry.

**AND IT COMPARES README.TXT BYTE FOR BYTE**, because the same harness answers
a second question the same way and the installer got that one wrong too. The
shipped manual is COMPRESSED - 8,850 bytes on the floppy, 16,304 expanded -
and the installer had two copy shapes, one raw and one not: a file that fitted
the buffer took OSAPI_FILE_READ, which is the TRANSPARENT one (SPEC.md
20.14.3), so it arrived expanded and was installed as a plain file with the
directory hint gone. The tree check above passes either way; only the bytes
say which happened.

    python3 tests/instdeep.py

REQUIRES A DISK: os8088_xt_hdd. The emulator writes THROUGH to its VHD, so
this used to keep a `.pristine` backup beside the shared one and restore it
before each install. It no longer needs to: `os88marty.launch` gives every
instance its own `media/hdds`, cloned from the staged tree's copy, which is
therefore never written and is the pristine master by construction. That is
also what lets two of these run at once - a shared VHD is one disk being
installed onto twice.
"""
import hashlib
import os
import struct
import sys

sys.path.insert(0, "tools")
import os88marty as M                                      # noqa: E402
from os88mouse import Mouse                                # noqa: E402
import os88build                                       # noqa: E402

MACHINE = "os8088_xt_hdd"
VHD_REL = "media/hdds/default_xtide.vhd"
# The staged tree's copy: read, never run in, never written. `launch()` clones
# it into each instance, so this is the "before" a diff is against.
BASE_VHD = os.path.join(M.base_run_dir(), VHD_REL)


def vhd(m):
    """THIS run's disk. One per instance, so two runs cannot share one."""
    return os.path.join(m.run_dir, VHD_REL)


SECTOR = 512
FOOTER = 512                        # the VHD footer, past the data area

# What must be on the installed volume, as PATHS - the point of the exercise
# is the ones with two components in them.
WANT_DIRS = ["SYSTEM", "APPS", "GAMES", "SYSTEM/APPDATA", "SYSTEM/DOS",
             "SYSTEM/FONTS"]
WANT_FILES = ["KERNEL.SYS", "SYSTEM/DOS/OS88NET.COM", "SYSTEM/TASKMGR.O88",
              "SYSTEM/FONTS/TALLX.F88"]

# ...and these must arrive BYTE FOR BYTE, hint and all. Any compressed file on
# the source disk would do; README.TXT is the one the shipped system disk has
# (SPEC.md 20.13.5), and it is the file the split shape actually expanded.
WANT_RAW = ["README.TXT"]


# --- a FAT reader that is not os88disk.py's ----------------------------------
class Vol:
    """One FAT12/FAT16 volume out of a blob, walked by directory."""

    def __init__(self, blob):
        self.b = blob
        (self.bps, self.spc, self.rsvd, self.nfats, self.rootn,
         self.tot16) = struct.unpack_from("<HBHBHH", blob, 11)
        self.fatsz = struct.unpack_from("<H", blob, 22)[0]
        self.tot32 = struct.unpack_from("<I", blob, 32)[0]
        tot = self.tot16 or self.tot32
        self.root_lba = self.rsvd + self.nfats * self.fatsz
        self.root_secs = (self.rootn * 32 + self.bps - 1) // self.bps
        self.data_lba = self.root_lba + self.root_secs
        clusters = (tot - self.data_lba) // self.spc
        self.bits = 12 if clusters < 4085 else 16

    def sec(self, lba, n=1):
        return self.b[lba * self.bps:(lba + n) * self.bps]

    def fat(self, n):
        f = self.rsvd * self.bps
        if self.bits == 16:
            return struct.unpack_from("<H", self.b, f + n * 2)[0]
        o = f + n + n // 2
        v = struct.unpack_from("<H", self.b, o)[0]
        return v >> 4 if n & 1 else v & 0xFFF

    def chain(self, first):
        end = 0xFF8 if self.bits == 12 else 0xFFF8
        c, out = first, []
        while 2 <= c < end and c not in out:
            out.append(c)
            c = self.fat(c)
        return out

    def entries(self, first):
        """(name, attr, cluster, size) of one directory, root when first is 0."""
        if first == 0:
            raw = self.sec(self.root_lba, self.root_secs)
        else:
            raw = b"".join(self.sec(self.data_lba + (c - 2) * self.spc, self.spc)
                           for c in self.chain(first))
        out = []
        for o in range(0, len(raw), 32):
            e = raw[o:o + 32]
            if not e or e[0] in (0x00, 0xE5):
                if e[0:1] == b"\x00":
                    break
                continue
            if e[11] & 0x08:                    # the volume label
                continue
            stem, ext = e[0:8].decode("latin1").rstrip(), e[8:11].decode("latin1").rstrip()
            name = stem + ("." + ext if ext else "")
            out.append((name, e[11],
                        struct.unpack_from("<H", e, 26)[0],
                        struct.unpack_from("<I", e, 28)[0]))
        return out

    def read(self, path):
        """One file's exact bytes, truncated to its directory size."""
        first, parts = 0, [p for p in path.split("/") if p]
        for i, want in enumerate(parts):
            for name, attr, clus, size in self.entries(first):
                if name.upper() != want.upper():
                    continue
                if i + 1 == len(parts):
                    data = b"".join(
                        self.sec(self.data_lba + (c - 2) * self.spc, self.spc)
                        for c in self.chain(clus))
                    return data[:size]
                first = clus
                break
            else:
                return None
        return None

    def mark(self, path):
        """The compression hint (+12) of a file's directory entry, or None.

        Read out of the RAW 32 bytes rather than through `entries`, which
        drops everything but name, attribute, cluster and size - and the hint
        is the field that says the installed copy is still the packed one
        (SPEC.md 20.14.1).
        """
        first, parts = 0, [p for p in path.split("/") if p]
        for i, want in enumerate(parts[:-1]):
            for name, attr, clus, size in self.entries(first):
                if name.upper() == want.upper():
                    first = clus
                    break
            else:
                return None
        if first == 0:
            raw = self.sec(self.root_lba, self.root_secs)
        else:
            raw = b"".join(self.sec(self.data_lba + (c - 2) * self.spc,
                                    self.spc) for c in self.chain(first))
        stem, _, ext = parts[-1].upper().partition(".")
        key = (stem.ljust(8) + ext.ljust(3)).encode()
        for o in range(0, len(raw), 32):
            if raw[o:o + 11] == key:
                return raw[o + 12]
        return None

    def tree(self, first=0, path="", depth=0):
        """Every path on the volume, as {PATH: (is_dir, size)}."""
        found = {}
        for name, attr, clus, size in self.entries(first):
            if name in (".", ".."):
                continue
            p = path + name
            isdir = bool(attr & 0x10)
            found[p] = (isdir, size)
            if isdir and depth < 8:
                found.update(self.tree(clus, p + "/", depth + 1))
        return found


def partition(vhd):
    """The active partition of a VHD, as a Vol."""
    blob = open(vhd, "rb").read()
    if len(blob) > FOOTER:
        blob = blob[:len(blob) - FOOTER]
    mbr = blob[:SECTOR]
    if mbr[510:512] != b"\x55\xAA":
        sys.exit("the VHD has no MBR signature - nothing was installed")
    for i in range(4):
        e = mbr[446 + i * 16:446 + (i + 1) * 16]
        if e[0] == 0x80:
            lba, secs = struct.unpack_from("<II", e, 8)
            print("  active slot %d: LBA %d, %d sectors, type %02X"
                  % (i, lba, secs, e[4]))
            return Vol(blob[lba * SECTOR:(lba + secs) * SECTOR])
    sys.exit("no active partition: the install never committed its MBR")


# --- the nine clicks --------------------------------------------------------
# EVERY COORDINATE IS A MODULE CONSTANT plus the window's own geometry, which
# is tests/hddcp.py's rule and its reason: tools/drive_install.py's fixed
# positions put the installer's Install button at y=233 on a 200-line screen
# after its drag, and an unreachable target reads like a dead button.
CP_I0Y, CP_IROWH = 6, 14           # ctrl.inc: the item list
CP_RX = 96                         # ...the right pane's left edge
CP_DBY1, CP_DROWH = 20, 26         # ...the Drivers page's hit bands
CP_IDRV = 2                        # SPEC.md 31.3
HDP_BY, HDP_BH = 82, 16            # page.inc: the Disks page's button row
HDP_B2X, HDP_BW2 = 74, 64          # ...Install, the middle of the three
                                   #    buttons (SPEC.md 52.4)
HIW_BY, HIW_BH = 96, 16            # inst.inc: the installer's own button row
HIW_B0X, HIW_BW0 = 8, 88           # ...and Install / Copy Apps, the first one
WIN_BORDER, WIN_TITLE = 1, 18      # a window's content origin, from its rect


def wins(m):
    from os88geom import WIN_SIZE, MAX_WIN
    b = m.read(m.sym("wm_wins"), WIN_SIZE * MAX_WIN)
    return [(i,) + tuple(struct.unpack_from("<H", b[i * WIN_SIZE:], o)[0]
                         for o in (2, 4, 6, 8))
            for i in range(MAX_WIN)
            if struct.unpack_from("<H", b[i * WIN_SIZE:], 0)[0] & 2]


def content(w):
    return w[1] + WIN_BORDER, w[2] + WIN_TITLE


# The two halves are separate because tests/instrest.py needs to LOOK at the
# window between them - the action button's caption is what it is about, and
# that caption changes as the stage machine runs (SPEC.md 52.10.6.1). One copy
# of the clicks rather than two that can drift: the coordinates are the
# awkward part of driving this dialog and a second opinion about them is what
# the module-constant rule above exists to prevent.
def open_installer(m):
    """Control Panel -> Drivers -> tick Hard Drive -> its page -> Install.

    out: (mouse, ix, iy) - the installer window's CONTENT origin.
    """
    mo = Mouse(marty=m)
    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    cp = [w for w in wins(m) if w[3] >= 280 and w[4] >= 100]
    if not cp:
        sys.exit("no Control Panel")
    x0, y0 = content(cp[0])
    mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)     # Drivers
    mo.click(x0 + CP_RX + 40, y0 + CP_DBY1 + CP_DROWH + CP_DROWH // 2,
             settle=8.0)                        # tick Hard Drive (row 1)
    nst = m.read(m.sym("cp_nst"), 1)[0]         # SPEC.md 31.9: the driver's
    mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7, settle=3.0)  # own page
    known = {w[0] for w in wins(m)}
    mo.click(x0 + CP_RX + HDP_B2X + HDP_BW2 // 2, y0 + HDP_BY + HDP_BH // 2,
             settle=20.0)                       # Install: a floppy read
    iw = next((w for w in wins(m) if w[0] not in known), None)
    if iw is None:
        sys.exit("the installer window never opened")
    print("  installer window = %s" % (iw,))
    ix, iy = content(iw)
    return mo, ix, iy


def run_install(m, mo, ix, iy):
    """Two clicks on the action button - arm, then go - and then WAIT."""
    bx, by = ix + HIW_B0X + HIW_BW0 // 2, iy + HIW_BY + HIW_BH // 2
    mo.click(bx, by, settle=3.0)                # Install: arms
    mo.click(bx, by, settle=0)                  # Install: goes

    # THE SCREEN GOES STILL WHILE IT WORKS - the copy holds the gfx lock for
    # its whole run - so `settle` would return five seconds in and the `with`
    # block would kill the emulator mid-install. Watch the DISK instead: the
    # MBR is the commit (SPEC.md 52.10.4), and the apps phase runs on past it,
    # so the wait is "our boot code is in sector 0" and then "the drive went
    # quiet".
    disk = vhd(m)
    # THIS INSTANCE'S OWN DISK, before the install - not BASE_VHD's.
    #
    # It used to read the shared master, on the docstring's reasoning that the
    # master "is never written and is the pristine master by construction". It
    # is written: any run of a commit that PREDATES per-instance isolation
    # installs straight onto it, which a bisect or a base-side classification
    # run does routinely. The master then carries an installed MBR, every
    # cloned disk starts with those same bytes, and the installer writes them
    # again - so `!= base` never becomes true and the row waits out the whole
    # 600 seconds. Measured, and the message it ends with is right about
    # itself: "either it is slower than the limit or the condition is asking
    # about the wrong thing".
    base = open(disk, "rb").read(446)
    took = M.until(m, lambda _: open(disk, "rb").read(446) != base,
                   "the installer to commit its MBR", limit=600.0)
    print("  MBR committed after %.0fs" % took)
    # THE DRIVE GOING QUIET IS `quiesce`, and on the GUEST's clock. This was
    # eight identical 8MB reads two HOST seconds apart, which asks the box for
    # 16 seconds of stillness and gets whatever fraction of the machine's own
    # work a loaded box happens to give it - so under contention the apps
    # phase's pause for a floppy read reads as the end of the install
    # (docs/plans/SOAK-PARALLEL.md 1). Same signal, same eight readings, the
    # interval and the budget in guest seconds: the pause it must see through
    # is the guest's, not the host's.
    #
    # A DIGEST, not the 8MB: `quiesce` compares with `==` and holds the last
    # reading, so hashing keeps two 8MB strings out of the loop for a
    # comparison that is exactly as strong.
    try:
        M.quiesce(m, lambda: hashlib.sha1(
                      open(disk, "rb").read(8 << 20)).digest(),
                  guest=2.0, stable=8, budget=480.0,
                  what="the drive to go quiet")
    except M.MartyError as e:
        sys.exit("the disk never stopped changing - the install did not "
                 "finish (%s)" % e)
    print("  the drive went quiet")


def install(m):
    """Drive the installer on `m`, and REFUSE a disk that is already installed.

    The pre-flight is here because the alternative is a 600-second timeout
    saying nothing useful: an already-installed disk gets the same MBR written
    to it again, so the commit is invisible and the wait never ends. Naming it
    up front turns ten minutes of nothing into one sentence with the cure in
    it.
    """
    disk = vhd(m)
    try:
        already = "SYSTEM" in {q.split("/")[0] for q in partition(disk).tree()}
    except SystemExit:
        already = False                 # no MBR at all is a pristine disk
    if already:
        sys.exit(
            "instdeep: the disk this instance was cloned from is ALREADY "
            "installed, so the install below would write the same bytes and "
            "the commit would be invisible.\n"
            "  The shared master is %s and it is meant to be pristine "
            "(nothing writes to it once every instance clones its own).\n"
            "  Something wrote through to it - running a commit that predates "
            "per-instance isolation does exactly that, which a bisect or a "
            "base-side classification run does routinely.\n"
            "  Restore it from its .pristine copy beside it, or delete it and "
            "re-run `make marty`." % BASE_VHD)
    mo, ix, iy = open_installer(m)
    run_install(m, mo, ix, iy)


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in ("build/os8088-360.img", "build/apps360.img"):
        if not os.path.exists(img):
            sys.exit("no %s - `make` first" % img)
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=MACHINE) as m:
        M.settle(m)
        install(m)
        # NO SCREENSHOT, and that is the point rather than an omission: the
        # window says `Done` in both the broken and the fixed case, so the
        # answer is not on the screen at all. It is a directory entry, and the
        # host reads it below. (A `tools/os88marty.py shot` here would also be
        # a SECOND client on this instance, which is refused rather than
        # served - docs/MARTYPC-DEBUG.md. Share `m`, or launch a second
        # emulator.)
        #
        # READ THE DISK INSIDE THE `with`. It is this instance's own copy now,
        # and leaving the block takes the instance's media with it.
        v = partition(vhd(m))
    tree = v.tree()
    print("  FAT%d volume, %d paths" % (v.bits, len(tree)))
    for p in sorted(tree):
        print("    %-28s %s" % (p, "<dir>" if tree[p][0] else tree[p][1]))

    bad = []
    for p in WANT_DIRS:
        if p not in tree:
            bad.append("%s is MISSING" % p)
        elif not tree[p][0]:
            bad.append("%s is a file, not a folder" % p)
    for p in WANT_FILES:
        if p not in tree:
            bad.append("%s is MISSING" % p)
        elif tree[p][0] or tree[p][1] == 0:
            bad.append("%s is empty or a folder" % p)

    # --- and the BYTES, for the files the source disk carries packed --------
    # THE SAME READER ON BOTH SIDES, so a difference is the install's and not
    # two implementations disagreeing.
    src = Vol(open(os88build.at("build/os8088-360.img"), "rb").read())
    for p in WANT_RAW:
        want, wmark = src.read(p), src.mark(p)
        if want is None or wmark not in (0x5A, 0x5B):
            bad.append("%s is not COMPRESSED on build/os8088-360.img (hint "
                       "%s) - this row would pass on any installer, so the "
                       "fixture is wrong rather than the machine"
                       % (p, wmark))
            continue
        got, gmark = v.read(p), v.mark(p)
        if got is None:
            bad.append("%s is MISSING" % p)
        elif got != want:
            bad.append("%s is %d bytes on the hard disk and %d on the floppy "
                       "it came from - an install must COPY a file, and "
                       "OSAPI_FILE_READ would have EXPANDED this one "
                       "(SPEC.md 20.14.3, 52.10.13)"
                       % (p, len(got), len(want)))
        elif gmark != wmark:
            bad.append("%s arrived byte for byte and its directory hint did "
                       "not: %02X on the hard disk against %02X on the floppy. "
                       "dskw_czstamp derives the mark from the bytes being "
                       "written (SPEC.md 20.14.4), so the file reads as PLAIN "
                       "and every application would be handed the packed bytes"
                       % (p, gmark or 0, wmark))
        else:
            print("    %-28s %d bytes, byte for byte, hint %02X"
                  % (p, len(got), gmark))
    for b in bad:
        print("  !! " + b)
    if bad:
        sys.exit("instdeep: %d of the source tree's paths did not survive the "
                 "install (SPEC.md 52.10.13)" % len(bad))
    print("instdeep: the installed volume carries every folder, including "
          "the nested and the empty ones, and every packed file is still "
          "packed")


if __name__ == "__main__":
    main()
