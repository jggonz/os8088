#!/usr/bin/env python3
"""INSTALL, the wizard, installs and then UPGRADES - by its own buttons.

    python3 tests/install.py

SPEC.md 117. The wizard has no install engine of its own: every step is the
Control Panel's, reached through OSAPI_DRV_USE and HDD.DRV's HDV_TOOL. So the
question this row asks is not "does the installer work" - instdeep, instkeep
and their siblings ask that - but "does the WIZARD drive it": the right page
after every click, the doors answering, and the disk ending up as the
Control Panel's own Install would have left it.

On a cycle-accurate 4.77 MHz XT with an XT-IDE disk (os8088_xt_hdd), whose
fixture partition is DOS 3.3:

  1. FROM THE FLOPPY: launch A:\\INSTALL.O88; Welcome; the hard disk (HDD.DRV
     is up already - the BIOS knows the disk and DOS's FAT mounts); the
     partition, which is DOS's C: and carries NO os8088; Keep or Erase, where
     the default over a volume is KEEP (SPEC.md 52.10.15) - and the row picks
     ERASE with the Down key, which is the wizard's keyboard as well; Confirm;
     Install. The apps disk is in B:, so the engine finds it and the next page
     is Done. On the HOST: the system and apps trees are on the partition and
     DOS's files are gone. Then Close - and HDDTOOL.DRV's claim must be given
     back (OSAPI_DRV_USE's DRVU_DONE), read as HDD.DRV's own [hd_tseg].
  2. A USER FILE is planted on the installed volume with mtools.
  3. FROM C:, with the system disk in B: (A: holds a blank so the BIOS offers
     C:): launch C:\\INSTALL.O88. It must say UPGRADE (booted from the hard
     disk), find os8088 on slot 1 and preselect it, and on Keep or Erase have
     ERASE GREYED - the volume is running. Install; with no apps disk in the
     machine the wizard asks for one, and Skip (HDI_QUIT) is Done. On the
     HOST: the
     user's file is byte for byte, KERNEL.SYS is the floppy's, and the MBR was
     rewritten (a marker byte in its unused tail, instkeep's trick).
  4. ...and C: still boots.

Every page is read out of the package's own memory ([in_page], through the
window record's segment and build/install.map), so a click that did not land
is a named failure at the step, not a timeout twenty steps on.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
import os88geom as geom                                     # noqa: E402
import instdeep as ID                                       # noqa: E402
import hdboot as HB                                         # noqa: E402
import instkeep as IK                                       # noqa: E402
import os88build                                            # noqa: E402

SECTOR = 512
USER_TXT = b"a file of the user's, which an upgrade must keep\r\n" * 30

# --- the wizard's geometry and pages, read from its source --------------------
SRC = open("apps/install/install.asm").read()


def const(name):
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, SRC, re.M)
    if not m:
        sys.exit("install: %s is not an `equ` in apps/install/install.asm"
                 % name)
    return int(m.group(1))


P = {n: const(n) for n in ("P_WELCOME", "P_DRIVE", "P_SLOT", "P_MODE",
                            "P_CONFIRM", "P_APPS", "P_DONE", "P_STOP",
                            "P_BUSY")}
NAMES = {v: k for k, v in P.items()}
BX0, BW, BGAP, BY, BH = (const(n) for n in
                         ("IN_BX0", "IN_BW", "IN_BGAP", "IN_BY", "IN_BH"))
LOGN, LOGC = const("IN_LOGN"), const("IN_LOGC")

# --- ...and the progress widget's, read from the kernel's (SPEC.md 12.8) -----
FPGSRC = open("kernel/fprog.inc").read()
FPG = {}
for _n in ("FPG_ICO_W", "FPG_ISP", "FPG_BOX_W", "FPG_BOX_Y"):
    _m = re.search(r"^%s\s+equ\s+(\d+)" % _n, FPGSRC, re.M)
    if not _m:
        sys.exit("install: %s is not an `equ` in kernel/fprog.inc" % _n)
    FPG[_n] = int(_m.group(1))


def symmap():
    """The package's symbols, off the map its build rule writes."""
    out = {}
    path = os88build.at("build/install.map")
    for line in open(path):
        p = line.split()
        if len(p) == 3 and re.match(r"^[0-9A-F]+$", p[0]):
            out[p[2]] = int(p[0], 16)
    return out


SYM = symmap()


def hd_tseg_off():
    """[hd_tseg]'s offset in HDD.DRV's image (the HMA build is the same code
    assembled at DRV_ORG = E000h)."""
    for line in open(os88build.at("build/hdd.hma.map")):
        p = line.split()
        if len(p) == 3 and p[2] == "hd_tseg":
            return int(p[0], 16) - 0xE000
    sys.exit("install: no hd_tseg in build/hdd.hma.map")


class Wizard(object):
    """The INSTALL window on one machine: its pages, read and clicked."""

    def __init__(self, ui, w):
        self.ui, self.m, self.w = ui, ui.m, w
        base = ui._S("wm_wins") + w.i * geom.WIN_SIZE
        self.seg = struct.unpack("<H", bytes(self.m.read(base + geom.W_SEG,
                                                          2)))[0]

    def byte(self, name):
        return self.m.readseg(self.seg, SYM[name], 1)[0]

    def word(self, name, add=0):
        b = bytes(self.m.readseg(self.seg, SYM[name] + add, 2))
        return struct.unpack("<H", b)[0]

    def page(self):
        return self.byte("in_page")

    def caption(self):
        return bytes(self.m.readseg(self.seg, SYM["in_cap"], 48)) \
            .split(b"\0")[0].decode("latin-1")

    def button(self, n):
        x0, y0 = self.w.x + 1, self.w.y + geom.TITLE_H
        x = x0 + BX0 + n * (BW + BGAP) + BW // 2
        y = y0 + BY + BH // 2
        self.ui.mo.click(x, y)

    def wait(self, want, what, limit=60.0):
        def ok(_):
            pg = self.page()
            if pg == P["P_STOP"] and want != P["P_STOP"]:
                sys.exit("install: FAIL - the install STOPPED %s: %r"
                         % (what, self.caption()))
            return pg == want
        M.until(self.m, ok, "INSTALL to show %s (%s)" % (NAMES[want], what),
                limit=limit)
        print("  %-10s %s" % (NAMES[want][2:], what))

    def next(self, want, what, limit=60.0):
        self.button(1)
        self.wait(want, what, limit)

    def log(self):
        """The lines the Installing page keeps (SPEC.md 117.4.1), oldest
        first - the last IN_LOGN of the run."""
        n = self.byte("in_logn")
        raw = bytes(self.m.readseg(self.seg, SYM["in_log"],
                                   LOGN * (LOGC + 1)))
        return [raw[i * (LOGC + 1):i * (LOGC + 1) + LOGC].decode("latin-1")
                .rstrip() for i in range(n)]

    def install(self, want, what, limit=900.0, move=False):
        """Install, from Confirm: the Installing page writes its log while
        the verb runs and STAYS when it answers, Next live (SPEC.md 117.1),
        until Next goes on to the page the answer named.

        `move` DRAGS THE WINDOW before that Next. A drag is replayed from the
        drag cache with no W_PAINT (SPEC.md 11.96.12.0), so a wizard that
        laid its buttons out only when it painted hit-tested the press
        against where the window USED to be: "Finished - click Next", and
        Next did nothing until a minimize and restore repainted it."""
        self.button(1)
        bar = BarWatch(self.m)

        def ended(_):
            pg = self.page()
            if pg not in (P["P_CONFIRM"], P["P_BUSY"]):
                sys.exit("install: FAIL - Install left the Installing page "
                         "by itself, for %s: its log must stay until Next"
                         % NAMES.get(pg, pg))
            done = pg == P["P_BUSY"] and self.byte("in_logend")
            if pg == P["P_BUSY"] and not done:
                bar.sample()
            return done
        M.until(self.m, ended, "the Installing page to end (%s)" % what,
                limit=limit)
        bar.verdict(what)
        lines = self.log()
        print("  BUSY       %s; the log ends:" % what)
        for ln in lines:
            print("             | %s" % ln)
        if move:
            x0, y0 = self.w.x, self.w.y
            self.w = self.ui._refresh(self.ui.drag_window(self.w, 48, 24))
            print("  MOVED      the window %d,%d -> %d,%d, no repaint"
                  % (x0, y0, self.w.x, self.w.y))
        self.next(want, "after Next on the log" + (" - at the window's NEW "
                  "place" if move else ""))
        return lines


class BarWatch(object):
    """IS THE PROGRESS BAR ON THE GLASS while the copy runs (SPEC.md 12.8.6)?

    The wizard draws its Installing page through its window's clip region and
    runs HDI_GO in the same lock hold, so the region used to be armed under
    every one of the widget's primitives: [fpg_on] said the widget was up for
    the whole copy and not one pixel of it reached the menu bar. Guest state
    cannot see that - only the framebuffer can. So each poll that finds the
    widget ARMED reads the CGA framebuffer and asks for the box's top frame
    row: FPG_BOX_W pixels of one colour at FPG_BOX_Y with the bed beside it
    in the other, a picture nothing else on the bar draws (menu text keeps
    off the widget's cells while it is up).
    """

    def __init__(self, m):
        self.m = m
        self.on = m.sym("fpg_on")
        self.x0 = m.sym("fpg_x0")
        self.armed = self.seen = 0

    def sample(self):
        if not self.m.read(self.on, 1)[0] & 1:
            return
        x0 = struct.unpack("<H", bytes(self.m.read(self.x0, 2)))[0]
        _, _, rows = self.m.vram("cga")
        left = x0 + FPG["FPG_ICO_W"] + FPG["FPG_ISP"]
        row = rows[FPG["FPG_BOX_Y"]]
        run = row[left:left + FPG["FPG_BOX_W"]]
        self.armed += 1
        # the frame in one colour, and the BED beside it (the icon -> box
        # gap) in the other: a bare bar is one colour across both, so this
        # cannot pass on a widget that was never drawn, whichever polarity
        # the card stores ink in
        if len(run) == FPG["FPG_BOX_W"] and len(set(run)) == 1 \
                and row[left - 2] != run[0]:
            self.seen += 1

    def verdict(self, what):
        print("  BAR        %s: armed at %d sample(s), on the glass at %d"
              % (what, self.armed, self.seen))
        if not self.armed:
            sys.exit("install: FAIL - the progress widget never ARMED during "
                     "the copy (%s): nothing to look at" % what)
        if not self.seen:
            sys.exit("install: FAIL - the progress widget was armed for the "
                     "whole copy and NEVER on the glass (%s): its primitives "
                     "were clipped to the wizard's window (SPEC.md 12.8.6)"
                     % what)


def launch_wizard(ui, path):
    w = ui.path(path)
    if not w.title.upper().startswith("INSTALL"):
        sys.exit("install: %s opened %r, not the wizard" % (path, w.title))
    return Wizard(ui, w)


def plant(vhd):
    """USER.TXT into the root of the active partition, through mtools."""
    blob = bytearray(open(vhd, "rb").read())
    for i in range(4):
        e = blob[446 + i * 16:462 + i * 16]
        if e[0] == 0x80:
            lba, secs = struct.unpack_from("<II", e, 8)
            break
    else:
        sys.exit("install: no active partition to plant a file on")
    part = bytes(blob[lba * SECTOR:(lba + secs) * SECTOR])
    with tempfile.TemporaryDirectory() as td:
        img = os.path.join(td, "part.img")
        open(img, "wb").write(part)
        open(os.path.join(td, "USER.TXT"), "wb").write(USER_TXT)
        IK.mtools("mcopy", "-i", img, os.path.join(td, "USER.TXT"), "::/")
        part = open(img, "rb").read()
    blob[lba * SECTOR:(lba + secs) * SECTOR] = part
    blob[400] = 0xA5                   # the MBR's unused tail: a marker the
    open(vhd, "wb").write(blob)        # upgrade's commit must overwrite


def tool_seg(m):
    """HDDTOOL.DRV's claim, as HDD.DRV holds it: 0 = given back."""
    row = m.sym("drv_tab") + 1 * 16    # row 1, the hard disk (drv_tab order)
    seg = struct.unpack("<H", bytes(m.read(row + 2, 2)))[0]
    if not seg:
        return None                    # the driver itself is not loaded
    return struct.unpack("<H", bytes(m.readseg(seg, hd_tseg_off(), 2)))[0]


def main():
    if not os.path.exists("build/martypc/run/martypc_headless"):
        sys.exit("no MartyPC - `make marty` first")
    for f in ("build/os8088-360.img", "build/apps360.img",
              "build/install.map", "build/hdd.hma.map"):
        if not os.path.exists(os88build.at(f)):
            sys.exit("no %s - `make` first" % f)
    run_dir = M.stage_run_dir("install")
    vhd = os.path.join(run_dir, ID.VHD_REL)
    src = ID.Vol(open(os88build.at("build/os8088-360.img"), "rb").read())

    # --- 1. from the floppy: ERASE the DOS partition -------------------------
    with M.launch("build/os8088-360.img", apps="build/apps360.img",
                  machine=ID.MACHINE, run_dir=run_dir) as m:
        ui = os88ui.UI(m)
        ui.ready()                               # ...and the saver OFF: an
        wz = launch_wizard(ui, "A:/INSTALL.O88") # install is minutes untouched
        wz.wait(P["P_WELCOME"], "on launch")
        if wz.byte("in_sysboot"):
            sys.exit("install: FAIL - a floppy-booted machine was offered an "
                     "UPGRADE (OSAPI_VOL_SYS, SPEC.md 117.5)")
        wz.next(P["P_DRIVE"], "after Welcome: the driver and the drives",
                limit=120.0)
        if wz.byte("in_drvst") == 2:
            sys.exit("install: FAIL - the hard disk driver did not start")
        wz.next(P["P_SLOT"], "after the drive", limit=120.0)
        if wz.byte("in_osmask"):
            sys.exit("install: FAIL - os8088 was 'found' on the DOS partition "
                     "(mask %02X): the KERNEL.SYS probe is reading the wrong "
                     "volume (SPEC.md 117.5)" % wz.byte("in_osmask"))
        wz.next(P["P_MODE"], "after the partition")
        if wz.byte("in_wipe") != 0:
            sys.exit("install: FAIL - over a FAT volume the default must be "
                     "KEEP (SPEC.md 52.10.15, 117.5)")
        m.key("ArrowDown")                       # the keyboard picks Erase
        M.until(m, lambda _: wz.byte("in_wipe") == 1,
                "the Down key to pick Erase", limit=20.0)
        print("  MODE       Down picked Erase")
        wz.next(P["P_CONFIRM"], "after keep or erase")
        log = wz.install(P["P_DONE"], "after Install: both disks copied",
                         move=True)
        sums = [ln for ln in log if ln.startswith("Copied ")]
        m_ = re.match(r"Copied (\d+) files, (\d+) KB", sums[0]) if sums \
            else None
        if not log or log[-1] != "Finished." or not m_ \
                or int(m_.group(1)) < 20 or "Writing ASSOC.DAT" not in log:
            sys.exit("install: FAIL - the log does not end as an install "
                     "does (ASSOC.DAT, the totals, Finished.): %r" % log)
        v = ID.partition(vhd)
        tree = v.tree()
        bad = [p for p in ("KERNEL.SYS", "BOOTHD.SYS", "INSTALL.O88",
                           "SYSTEM/TASKMGR.O88") if p not in tree]
        if not [p for p in tree if p.startswith("APPS/") and p.endswith(
                ".O88") and p not in src.tree()]:
            bad.append("no package from the APPS disk")
        if "IO.SYS" in tree or "COMMAND.COM" in tree:
            bad.append("DOS survived an ERASE")
        if bad:
            sys.exit("install: FAIL after the erase-install - %s"
                     % "; ".join(bad))
        print("  host: %d paths on the partition, DOS gone, both disks there"
              % len(tree))
        before = tool_seg(m)
        wz.button(2)                             # Close
        M.until(m, lambda _: not [w for w in ui.windows()
                                  if w.title.upper().startswith("INSTALL")
                                  and w.visible],
                "the wizard to close", limit=30.0)
        after = tool_seg(m)
        if before in (None, 0):
            sys.exit("install: the tool was not loaded while the wizard ran "
                     "(hd_tseg %r) - the row is not measuring DRVU_DONE"
                     % before)
        if after:
            sys.exit("install: FAIL - HDDTOOL.DRV is still held after Close "
                     "(hd_tseg %04X): DRVU_DONE did not give it back "
                     "(SPEC.md 117.3)" % after)
        print("  closed: HDDTOOL.DRV given back (hd_tseg %04X -> 0)" % before)

    # --- 2. a user's file -------------------------------------------------------
    plant(vhd)

    # --- 3. from C:, UPGRADE it -------------------------------------------------
    if not os.path.exists(HB.BLANK):
        with open(HB.BLANK, "wb") as f:
            f.write(bytes(368640))
    with M.launch(HB.BLANK, apps="build/os8088-360.img", machine=ID.MACHINE,
                  boot=0, run_dir=run_dir) as m:
        IK.boot_c(m, "after the wizard's install")
        m.run()
        ui = os88ui.UI(m)
        ui.ready()
        wz = launch_wizard(ui, "C:/INSTALL.O88")
        wz.wait(P["P_WELCOME"], "on launch, from C:")
        if not wz.byte("in_sysboot"):
            sys.exit("install: FAIL - booted from C: and not offered an "
                     "UPGRADE (SPEC.md 117.5)")
        wz.next(P["P_DRIVE"], "after Welcome", limit=120.0)
        wz.next(P["P_SLOT"], "after the drive", limit=120.0)
        if not wz.byte("in_osmask") & 1 or wz.word("in_slot") != 0:
            sys.exit("install: FAIL - os8088 on slot 1 was not found and "
                     "picked (mask %02X, slot %d)" % (wz.byte("in_osmask"),
                                                       wz.word("in_slot")))
        wz.next(P["P_MODE"], "after the partition")
        dis = wz.word("in_rad", 16)              # OS88UI_RD_DIS
        if wz.byte("in_wipe") != 0 or not dis & 2:
            sys.exit("install: FAIL - the running volume must be KEEP with "
                     "Erase greyed (wipe %d, grey bits %X)"
                     % (wz.byte("in_wipe"), dis))
        wz.next(P["P_CONFIRM"], "after keep or erase")
        # A: holds a blank and B: the system disk, so there is no apps disk
        # in the machine: the system part commits and the wizard ASKS for it
        # (SPEC.md 52.10.10). Skip is HDI_QUIT, and the disk boots without
        # the programs.
        log = wz.install(P["P_APPS"], "after Install: the system part, "
                         "upgraded")
        if log[-3:] != ["The disk starts os8088 now",
                        "No apps disk in a drive", "Next: the apps disk."]:
            sys.exit("install: FAIL - the upgrade's log does not end at its "
                     "commit and its apps-disk question: %r" % log)
        wz.button(2)                             # Skip
        wz.wait(P["P_DONE"], "after Skip")
        if not wz.byte("in_skip"):
            sys.exit("install: FAIL - Done after Skip does not say the apps "
                     "disk was left off")
    blob = open(vhd, "rb").read(SECTOR)
    v = ID.partition(vhd)
    bad = []
    if v.read("USER.TXT") != USER_TXT:
        bad.append("USER.TXT was not kept")
    if v.read("KERNEL.SYS") != src.read("KERNEL.SYS"):
        bad.append("KERNEL.SYS is not the floppy's")
    if blob[400] != 0:
        bad.append("the MBR was never rewritten - the upgrade did not commit")
    if bad:
        sys.exit("install: FAIL after the upgrade - %s" % "; ".join(bad))
    print("  host: USER.TXT kept, KERNEL.SYS the floppy's, the MBR committed")

    # --- 4. ...and it boots ---------------------------------------------------
    with M.launch(HB.BLANK, machine=ID.MACHINE, boot=0, run_dir=run_dir) as m:
        IK.boot_c(m, "after the wizard's upgrade")
    print("install: the wizard erase-installed from the floppy, upgraded the "
          "running system from C: keeping the user's file, gave its tool "
          "back, and the disk boots")


if __name__ == "__main__":
    main()
