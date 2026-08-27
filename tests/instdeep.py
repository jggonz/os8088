#!/usr/bin/env python3
"""The installer reproduces the source disk's WHOLE tree (SPEC.md 52.10.13).

An install used to walk the root and one level of folders, so a folder inside
a folder was never made on the destination.  Two are shipped and both went
missing with no error anywhere:

  * SYSTEM/APPDATA (SPEC.md 19.9), which is EMPTY - so nothing that counted
    bytes could have noticed - and whose absence reads as "preferences do not
    stick on a hard-disk machine" in every application at once;
  * SYSTEM/DOS/OS88NET.COM (SPEC.md 62), which is not.

So this drives a real install on MartyPC's XT-IDE machine and then reads the
partition back ON THE HOST, with a FAT reader that is not the one that wrote
it.  A screendump cannot answer this question: the installer says `Done` in
both the broken and the fixed case, and the difference is a directory entry.

    python3 tests/instdeep.py

REQUIRES A DISK: os8088_xt_hdd, and a pristine VHD - the emulator writes
through to media/hdds/default_xtide.vhd and nothing copies it per run, so the
backup beside it is taken once and restored before each install.
"""
import os
import shutil
import struct
import sys
import time

sys.path.insert(0, "tools")
import os88marty as M                                      # noqa: E402
from os88mouse import Mouse                                # noqa: E402

MACHINE = "os8088_xt_hdd"
RUN = "build/martypc/run"
VHD = os.path.join(RUN, "media/hdds/default_xtide.vhd")
PRISTINE = VHD + ".pristine"
SECTOR = 512
FOOTER = 512                        # the VHD footer, past the data area

# What must be on the installed volume, as PATHS - the point of the exercise
# is the ones with two components in them.
WANT_DIRS = ["SYSTEM", "APPS", "GAMES", "SYSTEM/APPDATA", "SYSTEM/DOS"]
WANT_FILES = ["KERNEL.SYS", "SYSTEM/DOS/OS88NET.COM", "SYSTEM/TASKMGR.O88"]


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
    base = open(PRISTINE, "rb").read(446)
    took = M.until(m, lambda _: open(VHD, "rb").read(446) != base,
                   "the installer to commit its MBR", limit=600.0)
    print("  MBR committed after %.0fs" % took)
    quiet, last = 0, None
    for _ in range(120):
        time.sleep(2.0)
        now = open(VHD, "rb").read(8 << 20)
        quiet = quiet + 1 if now == last else 0
        last = now
        if quiet >= 8:                          # 16s: the apps phase pauses
                                                # for a floppy read and a
                                                # shorter window calls that
                                                # the end of the install
            break
    else:
        sys.exit("the disk never stopped changing - the install did not finish")
    print("  the drive went quiet")


def install(m):
    mo, ix, iy = open_installer(m)
    run_install(m, mo, ix, iy)


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in ("build/os8088-360.img", "build/apps360.img"):
        if not os.path.exists(img):
            sys.exit("no %s - `make` first" % img)
    if not os.path.exists(PRISTINE):
        shutil.copy2(VHD, PRISTINE)
        print("  took a pristine copy of the VHD")
    shutil.copy2(PRISTINE, VHD)

    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=MACHINE) as m:
        M.settle(m)
        install(m)
        # NO SCREENSHOT, and that is the point rather than an omission: the
        # window says `Done` in both the broken and the fixed case, so the
        # answer is not on the screen at all. It is a directory entry, and the
        # host reads it below. (A `tools/os88marty.py shot` here would also be
        # a SECOND client on :9001, which hangs rather than erroring -
        # docs/MARTYPC-DEBUG.md.)

    v = partition(VHD)
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
    for b in bad:
        print("  !! " + b)
    if bad:
        sys.exit("instdeep: %d of the source tree's paths did not survive the "
                 "install (SPEC.md 52.10.13)" % len(bad))
    print("instdeep: the installed volume carries every folder, "
          "including the nested and the empty ones")


if __name__ == "__main__":
    main()
