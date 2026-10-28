#!/usr/bin/env python3
"""The installer reproduces the source disk's WHOLE tree, and its BYTES.

SPEC.md 52.10.13.

An install used to walk the root and one level of folders, so a folder inside
a folder was never made on the destination.  Two are shipped and both went
missing with no error anywhere:

  * SYSTEM/APPDATA (SPEC.md 19.9), which is EMPTY - so nothing that counted
    bytes could have noticed - and whose absence reads as "preferences do not
    stick on a hard-disk machine" in every application at once;
  * SYSTEM/DOS/OS88NET.COM (SPEC.md 62), which is not - and which the 360KB
    pair this row installs from no longer carries (SPEC.md 24.3.1.7).

A third is the deepest of them: SYSTEM/APPDATA/NOTES/WELCOME.TXT (SPEC.md
110.13), the Sticky Note's welcome note, three folder levels down with real
content in it on the volume the installer is copying FROM. Losing it is
silent in the same way the other two are - the pad on the installed machine
comes up with no notes and says nothing about why. (It was SYSTEM/FONTS until
the 360 KB system disk gave its typefaces to office360.img, SPEC.md 24.3.1.5,
and this row installs from that disk.)

So this drives a real install on MartyPC's XT-IDE machine and then reads the
partition back ON THE HOST, with a FAT reader that is not the one that wrote
it.  A screendump cannot answer this question: the installer says `Done` in
both the broken and the fixed case, and the difference is a directory entry.

**AND IT COMPARES README.TXT BYTE FOR BYTE**, because the same harness answers
a second question the same way and the installer got that one wrong too. The
shipped manual is COMPRESSED - 8,088 bytes on the floppy, 14,722 expanded -
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
import os
import struct
import sys

sys.path.insert(0, "tools")
import os88marty as M                                      # noqa: E402
from os88mouse import Mouse                                # noqa: E402
import instwiz as IW                                       # noqa: E402
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
# SYSTEM/DOS is not among them any more: OS88NET.COM left apps360.img for
# util360.img (SPEC.md 24.3.1.7), and the 360KB pair is what this installs
# from. SYSTEM/APPDATA/NOTES is the deeper case and still is one.
WANT_DIRS = ["SYSTEM", "APPS", "GAMES", "SYSTEM/APPDATA",
             "SYSTEM/APPDATA/NOTES"]
WANT_FILES = ["KERNEL.SYS", "SYSTEM/TASKMGR.O88",
              "SYSTEM/APPDATA/NOTES/WELCOME.TXT"]

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


# The two halves are separate because a row may want the wizard in hand
# between them - tests/instcross.py reads its caption after a refusal. One
# copy of the clicks rather than two that can drift: the coordinates are the
# awkward part of driving the Control Panel, and the wizard's own are
# tests/instwiz.py's.
def open_installer(m):
    """Control Panel -> Drivers -> tick Hard Drive -> its page -> Install.

    Install STARTS INSTALL.O88 off the boot volume (SPEC.md 52.10.4.4), so
    this is also the gate on that button: a click that opened anything else,
    or nothing, fails here by name.

    out: the wizard (tests/instwiz.py's Wizard), on its Welcome page.
    """
    mo = Mouse(marty=m)
    mo.menu(8, 8, 8, 40)                        # chip menu -> Control Panel
    cp = [w for w in wins(m) if w[3] >= 280 and w[4] >= 100]
    if not cp:
        sys.exit("no Control Panel")
    x0, y0 = content(cp[0])
    mo.click(x0 + 40, y0 + CP_I0Y + CP_IDRV * CP_IROWH + 7)     # Drivers
    if not int.from_bytes(m.read(m.sym("drv_tab") + 16 + 2, 2), "little"):
        mo.click(x0 + CP_RX + 40, y0 + CP_DBY1 + CP_DROWH + CP_DROWH // 2,
                 settle=0)                      # tick Hard Drive (row 1) -
                                                # ONLY if it is not up: SPEC.md
                                                # 51.3.2 loads it at boot on a
                                                # machine whose BIOS knows a
                                                # disk, and a click on a loaded
                                                # row UNLOADS it
        # (was a fixed 36 guest s: the load is done when the row has a
        # segment, and its page entry is drawn when the pass is over)
        M.until(m, lambda _: int.from_bytes(
            m.read(m.sym("drv_tab") + 16 + 2, 2), "little"),
            "HDD.DRV to load", poll=.05, limit=60)
        M.ui_done(m, "the Hard Drive tick", cap=13.5)
    nst = m.read(m.sym("cp_nst"), 1)[0]         # SPEC.md 31.9: the driver's
    mo.click(x0 + 40, y0 + CP_I0Y + nst * CP_IROWH + 7, settle=0)  # own page
    M.ui_done(m, "the driver's own page", cap=13.5)
    known = {w[0] for w in wins(m)}
    mo.click(x0 + CP_RX + HDP_B2X + HDP_BW2 // 2, y0 + HDP_BY + HDP_BH // 2,
             settle=0)                          # Install: the wizard, read
    # off the boot volume. The launch is POSTED (a hidden window's wake), so
    # the new window is the answer and nothing before it is
    try:
        M.until(m, lambda _: any(w[0] not in known and w[3] > 64
                                 for w in wins(m)),
                "the wizard's window", poll=.05, limit=120)
    except M.MartyError:
        sys.exit("the Install button never opened the wizard")
    M.ui_done(m, "the wizard's paint", cap=13.5)
    iw = next(w for w in wins(m) if w[0] not in known and w[3] > 64)
    print("  wizard window = %s" % (iw,))
    wz = IW.Wizard(m, lambda x, y: mo.click(x, y, settle=0), iw[0], iw[1],
                   iw[2])
    wz.wait(IW.P["P_WELCOME"], "after the Control Panel's Install")
    return wz


def run_install(m, wz, wipe=None, expect=("P_DONE", "P_APPS")):
    """The wizard from Welcome to the page after its log (tests/instwiz.py).

    `wipe` True erases the slot, False keeps its files, None leaves the
    wizard's own default - KEEP over a volume (SPEC.md 52.10.15).

    THE WIZARD SAYS WHEN IT IS FINISHED, which the old dialog did not: the
    rows that drove it watched the MBR change and then the drive go quiet,
    because the screen went still for the whole copy. The Installing page's
    [in_logend] is set when the verb has answered, so that is the wait, and
    the page Next goes on to is the outcome - Done, or the apps-disk question
    when no apps disk was in the machine."""
    return wz.run(wipe=wipe, expect=expect)


def install(m, wipe=True):
    """Drive the installer on `m`, and REFUSE a disk that is already installed.

    `wipe` picks Erase on the wizard's Keep or Erase page, which is what
    every row written before SPEC.md 52.10.15 meant by an install.

    The pre-flight is here because an already-installed disk makes every
    check after the install pass on the bytes that were already there.
    Naming it up front is one sentence with the cure in it.
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
    run_install(m, open_installer(m), wipe=True if wipe else None)


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for img in ("build/os8088-360.img", "build/apps360.img"):
        if not os.path.exists(img):
            sys.exit("no %s - `make` first" % img)
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=MACHINE, boot=False) as m:
        m.run(); M.kernel_up(m)       # a state gate: every act is a click
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
    # THE VOLUME BOOT RECORD'S SERIAL (SPEC.md 18.8.2.1, 52.10.2): the
    # installer's fresh VBR is built over a zeroed sector, and hd_bpb_fill
    # wrote BootSig 0x29 with nothing after it - so every installed volume
    # carried serial 0, whatever the format before it had stamped.
    vid = struct.unpack_from("<I", v.b, 39)[0]
    print("  VBR: BootSig %02X, BS_VolID %08X" % (v.b[38], vid))
    if v.b[38] != 0x29 or vid == 0:
        bad.append("the installed VBR carries BootSig %02X and serial %08X - "
                   "a fresh install must stamp one of its own (hd_bpb_fill, "
                   "SPEC.md 18.8.2.1)" % (v.b[38], vid))
    if "IO.SYS" in tree or "COMMAND.COM" in tree:
        bad.append("the DOS files the fixture partition carries are still "
                   "there - the install KEPT the volume instead of erasing it "
                   "(the Erase box, SPEC.md 52.10.15)")
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
