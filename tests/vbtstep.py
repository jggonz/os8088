"""Video Battle on MartyPC IN LOCKSTEP: the machine never runs free.

Not a row: the drive tests/vbtownsnd.py and tests/vbtsndend.py share (and
tests/unit/t_registry.py's UNREGISTERED says so).

The emulator is bit-exact in GUEST time and nothing else, so a row that runs
the guest free and presses a key when the host gets round to it presses it at
a different guest instant every run - and a battle's timeline (the card's
blocks, the hook's frames, where a clip's sound begins in the card's ring)
is exactly what such a key moves. vbtownsnd went red one run in five that
way, the same tree each time, until the owner's rule: no row may depend on
host timing. So here:

  * the battle is OPENED BY THE BOOT - a link in C:\\AUTOSTRT (SPEC.md
    26.10.5) - not by a host-timed click on the desktop;
  * every wait is `advance()` in fixed guest steps and a look between them
    (`Step.until`), every pause a fixed `advance()` (`Step.rest`);
  * every key goes in while the machine is STOPPED (`Step.key`), so it lands
    at a guest cycle the script chose.

The same disk, scripts and tree give the same cycles, the same reads and the
same verdict on every run, on an idle box and a loaded one.
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build, os88geom as geom, os88lnk, os88marty, os88ui   # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
MACHINE = "os8088_xt_hdd_sb"
HZ = os88marty.GUEST_HZ
STEP = int(HZ / 100)                # a look every 10 guest ms


def disk(tmp, vbt):
    """C: with the kernel, its drivers, VBATTLE.O88 and the battle `vbt` as
    BATTLE.VBT in the root, and \\AUTOSTRT\\VB.LNK naming VBATTLE.O88 - so
    the boot opens the battle's window itself. Answers the VHD's path"""
    vhd = os.path.join(tmp, "c.vhd")
    subprocess.run(
        [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
         "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
         "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
         "--file", "VBATTLE.O88=" + os88build.at("build/vbattle.o88"),
         "--file", "BATTLE.VBT=" + vbt], check=True, capture_output=True,
        cwd=ROOT)
    lba = struct.unpack_from("<I", open(vhd, "rb").read(512), 0x1BE + 8)[0]
    lnk = os.path.join(tmp, "VB.LNK")
    open(lnk, "wb").write(os88lnk.make("VBATTLE.O88"))
    img = "%s@@%d" % (vhd, lba * 512)       # (the volume, past the MBR)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    subprocess.run(["mmd", "-i", img, "::/AUTOSTRT"], check=True, env=env)
    subprocess.run(["mcopy", "-i", img, lnk, "::/AUTOSTRT/"], check=True,
                   env=env)
    return vhd


class Step(object):
    """A Marty driven in lockstep, and Video Battle's variables read through
    its window's record - afresh each read, since a compaction may move the
    package's region between plays (SPEC.md 66.6.1.1)"""

    def __init__(self, m, syms):
        self.m = m
        self.syms = syms
        self.ui = os88ui.UI(m)

    def cyc(self):
        return int(self.m.status()["cycles"])

    def secs(self):
        return self.cyc() / HZ

    def until(self, cond, what, guest=120.0, step=STEP):
        """Advance `step` cycles at a time until cond() - which is looked at
        on a STOPPED machine, so what it read is still true on return"""
        c0 = self.cyc()
        while not cond():
            self.m.advance(cycles=step)
            spent = (self.cyc() - c0) / HZ
            if spent > guest:
                raise os88marty.MartyError(
                    "%s did not happen in %g GUEST seconds (lockstep, so "
                    "the same answer every run)" % (what, guest))

    def rest(self, secs):
        self.m.advance(cycles=int(secs * HZ))

    def key(self, name, hold=0.06):
        """Down, `hold` guest seconds, up - pressed on a stopped machine"""
        self.m.key(name, up=False)
        self.rest(hold)
        self.m.key(name, down=False)

    def window(self):
        """The battle's window: VBATTLE.O88's own title (a read before
        kmain has cleared the table finds rubbish, so by name)"""
        try:
            for w in self.ui.windows():
                if w.visible and "BATTLE" in (w.title or "").upper():
                    return w
        except Exception:
            pass
        return None

    def opened(self, guest=240.0):
        """The boot to the battle's window, opened by \\AUTOSTRT, and a
        moment for its title picture to be read"""
        self.until(lambda: self.window() is not None,
                   "the battle's window (\\AUTOSTRT\\VB.LNK)", guest=guest)
        self.rest(3.0)
        self.wrec = self.ui._S("wm_wins") + self.window().i * geom.WIN_SIZE

    def base(self):
        rec = self.m.read(self.wrec, geom.WIN_SIZE)
        return struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4

    def rb(self, nm, off=0):
        return self.m.read(self.base() + self.syms[nm] + off, 1)[0]

    def rw(self, nm, off=0):
        return struct.unpack_from(
            "<H", self.m.read(self.base() + self.syms[nm] + off, 2))[0]

    def rd(self, nm):
        return struct.unpack_from(
            "<I", self.m.read(self.base() + self.syms[nm], 4))[0]

    def to_title(self):
        """Enter at the title picture: the play, to its title going round"""
        self.key("Enter")
        self.until(lambda: self.rb("vb_ready") == 1, "the play")
        self.until(lambda: self.rb("vb_tplay") == 1, "the title")
