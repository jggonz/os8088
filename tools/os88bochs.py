#!/usr/bin/env python3
"""Run kern_mega under Bochs and start packages until one faults
(docs/plans/KERN-MEGA-PLAN.md 10.1 items 13 and 16).

    python3 tools/os88bochs.py sweep [--batch N] [--out DIR] PKG.o88 ...
    python3 tools/os88bochs.py boot [--out DIR]     boot and leave it running
    python3 tools/os88bochs.py stop                 end every Bochs on the display
    python3 tools/os88bochs.py dbg [--out DIR] CMD...  break in, run debugger
                                                    commands, print, carry on

    --sb        give the machine a Sound Blaster 16 (port 220h, IRQ 5, DMA 1),
                its output written to OUT/sb.wav
    --ne2k      an NE2000 at 300h on Bochs's own slirp: the host is 10.0.2.2
    --c MODE    C: is `run` (RUN/ and the packages), `blank` (zeros, for the
                installer) or `keep` (the disk the last boot in --out left)
    --bootc     boot from C: instead of A:;  --noa  no disk in A:
    --csys      C: carries the system floppy's root files as well (the
                modules and drivers): a boot off C: alone that can hibernate
    --b IMG     a floppy for B: in place of build/apps120.img - any of the
                four sizes, and --btype 1_2 or 1_44 is the drive it is in
    --linger N  sweep: leave each boot running N seconds after MEGARUN's
                last start, so the packages' own loops reach a census
    --build DIR the tree mega.img, megak/, megarun.o88 and mbr.bin come
                from (default build/) - `make megatrap` is build/megatrap,
                kern_mega with MEGATRAP=1's #GP census on E9
                (tools/megatraps.py ranks it)
    --put F:P   one more file on C:, as os88disk.py's FOLDER:path (repeat it):
                `--put RUN/A/0:build/runcpm-disk/A/0/INFO.COM` is a drive
                for a package that is started out of RUN/
    --cfg BITS  write a SYSTEM.CFG onto A: wanting the drivers in BITS - one
                bit per driver by kernel/driver.inc's drv_cfgbit, sound being
                bit 0 (the Makefile's system.cfg rules are the same bytes);
                CDROM.DRV is bit 8, so `--cfg 0x101` is sound and the CD
    --a IMG     A:'s image in place of the tree's mega.img - a kern_mega
                system disk with something changed (`make megacdtest`)
    --cd ISO    an ISO 9660 image in an ATAPI CD-ROM, the secondary master
                at 170h. DATA ONLY: Bochs 2.7 answers PLAY AUDIO and a SubQ
                READ SUB-CHANNEL "not implemented", so CDRack there shows the
                drive's refusal - 86Box's `286-mega-cdrom` plays the disc

WHY BOCHS. QEMU's TCG checks no segment on a data access and lets code write
through CS, so on QEMU kern_mega runs code that faults on a real 286 - every
one of the Victory 286's first five faults ran clean there. Bochs checks every
access the way the CPU does, and reproduced those faults to the CS:IP and SP
of the photographs. Its CPU is a 386, so the reflection takes the CR0 path and
the 286 RESET path stays the field machine's to prove.

SWEEP builds a hard disk whose one folder, RUN/, holds tests/megarun's
MEGARUN (as 0RUN.O88, so it is the first file in the listing) and a batch of
packages; boots build/mega.img in A: with that disk as C:; opens C:, RUN and
0RUN.O88 with the mouse; and follows MEGARUN's lines on port E9, which name
each package before it is started. A `kern_mega FAULT` line ends the boot and
is charged to the package last named; so is a minute with no new line (a
package that took the screen, or a hang). The next boot carries on with the
packages after it. Every started package gets a screenshot in --out.

NEEDS: bochs (2.7: `BIOS-bochs-legacy` - the default BIOS's 32-bit init
crashes here), Xvfb, xdotool, xwd and ImageMagick's convert; `apt-get install
bochs bochsbios vgabios bochs-x xvfb xdotool x11-apps imagemagick`. The
Debian build has the debugger compiled in and stops at reset, so `c` goes in
on stdin.
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# THE X DISPLAY this instance owns: $OS88_BOCHS_DISPLAY, ":99" by default.
# Every process this tool counts or ends is ONE OF ITS OWN - a bochs-bin or a
# fifo holder whose environment names this display - so two agents (or two
# terminals) on two displays do not end each other's machine. It used to be
# `pgrep -x bochs-bin` and `pkill -f "^sleep 86400"`, box-wide.
DISPLAY = os.environ.get("OS88_BOCHS_DISPLAY", ":99")
ENV = dict(os.environ, DISPLAY=DISPLAY)
GEOM = (615, 4, 17)             # a 20MB disk: C/H/S as the BIOS sees it

# THE MACHINE'S RAM, in MB: 16 by default; $OS88_BOCHS_MEGS for a smaller
# machine, which is where the heap fills and compactions run (the Victory 286
# has 4)
MEGS = int(os.environ.get("OS88_BOCHS_MEGS", "16"))
# --ne2k's slirp, configured: $OS88_BOCHS_SLIRP names a Bochs slirp config
# file (`hostfwd = tcp::2121-:21`, one rule a line, five at most), which is
# how a HOST client reaches a server in the guest - FTPD (KERN-MEGA-PLAN 14.46)
SLIRP = os.environ.get("OS88_BOCHS_SLIRP")
BOCHSRC = """megs: {megs}
romimage: file=/usr/share/bochs/BIOS-bochs-legacy
vgaromimage: file=/usr/share/vgabios/vgabios.bin
vga: extension=vbe
pci: enabled=0
floppya: 1_44={a}, status={astat}
floppyb: type={btype}, {bmedia}={b}, status=inserted
ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14
ata0-master: type=disk, path={c}, mode=flat, cylinders={cy}, heads={hd}, spt={sp}
{cd}boot: {boot}
port_e9_hack: enabled=1
mouse: enabled=1, type=ps2, toggle=ctrl+mbutton
clock: sync=realtime, time0=local
{sb}log: {log}
panic: action=report
error: action=report
info: action=ignore
display_library: x
"""


def xd(*a):
    subprocess.run(["xdotool"] + [str(x) for x in a], env=ENV, check=True)


def rel(dx, dy):
    xd("mousemove_relative", "--", dx, dy)
    time.sleep(0.04)


def goto(x, y):
    """Pin at the guest's top-left corner, then walk: the guest moves one
    pixel per two X pixels (measured), in small steps."""
    for _ in range(40):
        rel(-60, -60)
    time.sleep(0.3)
    x2, y2 = 2 * x, 2 * y
    while x2 or y2:
        sx, sy = max(-30, min(30, x2)), max(-30, min(30, y2))
        rel(sx, sy)
        x2 -= sx
        y2 -= sy
    time.sleep(0.5)


def dbl(x, y):
    goto(x, y)
    xd("click", "--repeat", 2, "--delay", 120, 1)


def shot(path):
    subprocess.run("xwd -root -silent | convert xwd:- -crop 650x500+5+40 "
                   "+repage '%s'" % path, shell=True, env=ENV, check=True)


def xvfb():
    n = DISPLAY.lstrip(":").split(".")[0]
    sock = "/tmp/.X11-unix/X" + n
    # A LIVE Xvfb on THIS display, not a socket file: a server that died (or
    # was killed with its row) leaves the socket and its lock behind, and a
    # socket with nobody listening made every Bochs row on that display panic
    # with "cannot connect to X server" in under a second.
    live = subprocess.run(["pgrep", "-f", "^Xvfb :%s( |$)" % n],
                          capture_output=True).returncode == 0
    if not live:
        for stale in (sock, "/tmp/.X%s-lock" % n):
            try:
                os.remove(stale)
            except OSError:
                pass
        subprocess.Popen(["Xvfb", DISPLAY, "-screen", "0", "1024x768x24"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):             # until it is listening, not a guess
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        time.sleep(0.5)


def _ours(pid):
    """Is PID's environment on this instance's display?"""
    try:
        with open("/proc/%d/environ" % pid, "rb") as f:
            env = f.read().split(b"\0")
    except OSError:
        return False
    return ("DISPLAY=" + DISPLAY).encode() in env


def _pids(*pgrep):
    r = subprocess.run(["pgrep"] + list(pgrep), capture_output=True,
                       text=True)
    return [p for p in map(int, r.stdout.split()) if _ours(p)]


def running():
    """PIDs of every Bochs on the box. ONE AT A TIME: the mouse, the keys and
    the screenshots all go to the X display, and with two machines on it they
    land on whichever window is on top - which reads exactly like a guest that
    froze. `bochs` execs `bochs-bin`, so that is the name to look for."""
    return _pids("-x", "bochs-bin")


def dbg(out, cmds):
    """Break into the running Bochs, run CMDS, print what they said, and
    continue it."""
    e9 = os.path.join(out, "e9.out")
    n = os.path.getsize(e9)
    for p in running():
        os.kill(p, 2)               # SIGINT: Bochs stops at its prompt
    time.sleep(1.5)
    with open(os.path.join(out, "dbg.fifo"), "w") as fh:
        fh.write("\n".join(cmds) + "\nc\n")
    time.sleep(2 + 0.3 * len(cmds))
    with open(e9, "rb") as f:
        f.seek(n)
        sys.stdout.write(f.read().decode("latin-1"))


def stop():
    for p in _pids("-f", "^sleep 86400"):
        os.kill(p, 15)
    for p in running():
        os.kill(p, 15)
    end = time.time() + 10
    while running() and time.time() < end:
        time.sleep(0.5)
    for p in running():
        os.kill(p, 9)


class Box(object):
    """One Bochs, its disks in OUT, its E9 output in OUT/e9.out."""

    def __init__(self, out, pkgs, sb=False, cfg=None, bimg=None, ne2k=False,
                 btype="1_2", cmode="run", bootc=False, noa=False,
                 build=None, csys=False, put=(), cd=None, aimg=None):
        if running():
            sys.exit("os88bochs: a Bochs is already running (pids %s) - every "
                     "click would land on whichever window is on top. "
                     "`os88bochs.py stop` ends it" %
                     " ".join(map(str, running())))
        bld = build or os.path.join(ROOT, "build")
        self.out = out
        self.e9 = os.path.join(out, "e9.out")
        a, b = os.path.join(out, "a.img"), os.path.join(out, "b.img")
        c = os.path.join(out, "c.img")
        shutil.copy(aimg or os.path.join(bld, "mega.img"), a)
        shutil.copy(bimg or os.path.join(ROOT, "build", "apps120.img"), b)
        if cfg is not None:
            f = os.path.join(out, "SYSTEM.CFG")
            with open(f, "wb") as fh:
                fh.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
                         bytes([1, 2]) + cfg.to_bytes(2, "little") + b"\0\0")
            subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88fat.py"),
                            "del", a, "SYSTEM.CFG"], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
            subprocess.run([sys.executable, os.path.join(ROOT, "tools",
                                                         "os88fat.py"),
                            "add", a, f], check=True,
                           stdout=subprocess.DEVNULL)
        stage = os.path.join(out, "stage")
        shutil.rmtree(stage, ignore_errors=True)
        os.makedirs(stage)
        args = ["RUN:" + self._stage(stage, os.path.join(bld,
                                                         "megarun.o88"),
                                     "0run.o88")]
        for p in pkgs:
            args.append("RUN:" + self._stage(stage, p,
                                             os.path.basename(p).lower()))
        args += list(put)          # --put: FOLDER:path, as os88disk.py takes it
        if csys:
            args += self._sysfiles(a, stage)
        if cmode == "keep" and os.path.exists(c):
            pass                    # the disk as the last boot left it
        elif cmode == "blank":
            with open(c, "wb") as fh:   # nothing on it: no MBR, no partition
                fh.truncate(GEOM[0] * GEOM[1] * GEOM[2] * 512)
        else:
            subprocess.run(
                [sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                 "-o", c, "--hdd",
                 "--mbr", os.path.join(bld, "mbr.bin"),
                 "--boot", os.path.join(bld, "megak", "boothd.bin"),
                 "--kernel", os.path.join(bld, "megak", "kernel.sys"),
                 "--geometry", "%d/%d/%d" % GEOM] + args,
                check=True, stdout=subprocess.DEVNULL)
        rc = os.path.join(out, "bochsrc")
        with open(rc, "w") as f:
            f.write(BOCHSRC.format(
                megs=MEGS, a=a, b=b, c=c, cy=GEOM[0], hd=GEOM[1], sp=GEOM[2],
                boot="disk" if bootc else "floppy",
                astat="ejected" if noa else "inserted",
                btype=btype, bmedia={368640: "360k", 737280: "720k",
                                     1228800: "1_2", 1474560: "1_44"}
                [os.path.getsize(b)],
                log=os.path.join(out, "bochs.log"),
                cd=("ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15\n"
                    "ata1-master: type=cdrom, path=%s, status=inserted\n" %
                    os.path.abspath(cd)) if cd else "",
                sb=("sound: waveoutdrv=file, waveout=%s\n"
                    "sb16: enabled=1, midimode=0, wavemode=1, "
                    "dmatimer=600000\n" % os.path.join(out, "sb.wav"))
                if sb else "") + (
                "ne2k: ioaddr=0x300, irq=10, mac=52:54:00:12:34:56, "
                "ethmod=slirp%s\n" % (", script=" + os.path.abspath(SLIRP)
                                       if SLIRP else "") if ne2k else ""))
        for n in os.listdir(out):
            if n.endswith(".lock"):
                os.remove(os.path.join(out, n))
        xvfb()
        self.f = open(self.e9, "wb")
        # The debugger reads a FIFO, held open by a `sleep` of its own so the
        # machine outlives this process (`boot`) and `dbg` can still reach it
        fifo = os.path.join(out, "dbg.fifo")
        if os.path.exists(fifo):
            os.remove(fifo)
        os.mkfifo(fifo)
        self.p = subprocess.Popen(["bochs", "-q", "-f", rc], cwd=out,
                                  stdin=os.open(fifo, os.O_RDWR),
                                  stdout=self.f, stderr=subprocess.STDOUT,
                                  env=ENV)
        self.hold = subprocess.Popen("exec sleep 86400 > '%s'" % fifo,
                                     shell=True, env=ENV)
        with open(fifo, "w") as fh:
            fh.write("c\n")

    @staticmethod
    def _sysfiles(img, stage):
        """--csys: every file in the ROOT of the system floppy (the
        modules, the drivers, ASSOC.DAT, SYSTEM.CFG), extracted for C:'s
        root, so a boot off C: alone can load HIBER.DRV - the hibernate's
        transport has to be the kernel's own boot partition under Bochs,
        where HDD.DRV takes the native-IDE rung (KERN-MEGA-DOS-PLAN 8.2)"""
        fat = os.path.join(ROOT, "tools", "os88fat.py")
        ls = subprocess.run([sys.executable, fat, "ls", img], check=True,
                            capture_output=True, text=True).stdout
        out = []
        for line in ls.splitlines()[2:]:
            f = line.split()
            if len(f) < 5 or "<dir>" in line or not f[1].isdigit():
                continue
            if f[0] == "KERNEL.SYS":
                continue            # os88disk.py --kernel puts its own
            dst = os.path.join(stage, f[0])
            subprocess.run([sys.executable, fat, "cat", "-o", dst, img, f[0]],
                           check=True)
            out.append(dst)
        return out

    @staticmethod
    def _stage(stage, src, name):
        dst = os.path.join(stage, name)
        shutil.copy(src, dst)
        return dst

    def lines(self):
        with open(self.e9, "rb") as f:
            t = f.read().decode("latin-1")
        return [l for l in t.splitlines() if l.strip()]

    def wait_for(self, text, secs):
        end = time.time() + secs
        while time.time() < end:
            if any(text in l for l in self.lines()):
                return True
            time.sleep(1)
        return False

    def capture(self):
        w = subprocess.run(["xdotool", "search", "--name", "Bochs"], env=ENV,
                           capture_output=True, text=True).stdout.split()[0]
        xd("windowfocus", "--sync", w)
        xd("mousemove", 330, 300)
        xd("keydown", "ctrl")
        xd("click", 2)
        xd("keyup", "ctrl")
        time.sleep(1)

    def stop(self):
        self.hold.kill()
        self.p.terminate()
        try:
            self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
        self.f.close()


def sweep(pkgs, batch, out, quiet_secs=60, sb=False, cfg=None, build=None,
          cap_secs=900, linger=0, put=(), cd=None):
    os.makedirs(out, exist_ok=True)
    left = list(pkgs)
    report = []
    boot = 0
    while left:
        boot += 1
        todo, left = left[:batch], left[batch:]
        d = os.path.join(out, "boot%02d" % boot)
        os.makedirs(d, exist_ok=True)
        box = Box(d, todo, sb, cfg, build=build, put=put, cd=cd)
        try:
            if not box.wait_for("kern_mega:", 240):
                report.append(("(boot)", "no kern_mega line in 240s"))
                left = todo + left
                break
            time.sleep(15)          # the desktop's first frame
            box.capture()
            dbl(591, 160)           # C:
            time.sleep(10)
            dbl(150, 123)           # RUN, the root's one folder
            time.sleep(8)
            # 0RUN.O88, under '..' - and under any folder --put made in RUN/,
            # a listing showing folders first (16 pixels a row)
            subs = {q.split(":", 1)[0].split("/")[1] for q in put
                    if q.split(":", 1)[0].startswith("RUN/")}
            dbl(160, 139 + 16 * len(subs))
            seen, last, quiet = 0, None, time.time()
            began = quiet
            started = []
            while True:
                ls = box.lines()
                new = ls[seen:]
                seen = len(ls)
                fault = None
                for l in new:
                    if l.startswith("MEGARUN: ") and l not in (
                            "MEGARUN: ok", "MEGARUN: done") \
                            and not l.startswith("MEGARUN: no "):
                        last = l[9:]
                        started.append(last)
                    elif l == "MEGARUN: ok" and last:
                        time.sleep(4)
                        shot(os.path.join(d, last.replace(".", "_") +
                                          ".png"))
                        report.append((last, "ok"))
                    elif l.startswith("MEGARUN: no "):
                        report.append((last, "refused " + l[12:]))
                    elif l.startswith("kern_mega FAULT"):
                        fault = l + " | " + " ".join(
                            ls[ls.index(l) + 1:ls.index(l) + 2])
                    elif l == "MEGARUN: done":
                        fault = "done"
                if new:
                    quiet = time.time()
                if fault == "done":
                    if linger:
                        # MEGATRAP=1: let what was started run on, so its
                        # loops land in the census, then a picture of it
                        time.sleep(linger)
                        shot(os.path.join(d, "LINGER.png"))
                    break
                if fault:
                    shot(os.path.join(d, "FAULT.png"))
                    report.append((last or "(before MEGARUN)",
                                   "FAULT " + fault))
                    break
                if time.time() - began > cap_secs:
                    # a MEGATRAP=1 boot never goes quiet while a site loops
                    shot(os.path.join(d, "CAP.png"))
                    report.append((last or "(MEGARUN did not start)",
                                   "still going after %ds" % cap_secs))
                    break
                if time.time() - quiet > quiet_secs:
                    shot(os.path.join(d, "QUIET.png"))
                    report.append((last or "(MEGARUN did not start)",
                                   "no E9 line in %ds" % quiet_secs))
                    break
                time.sleep(1)
            # what MEGARUN never reached goes round again
            done = {os.path.basename(p).upper() for p in todo
                    if os.path.basename(p).upper() in started}
            left = [p for p in todo
                    if os.path.basename(p).upper() not in done] + left
            if not started:
                break               # nothing moved: do not loop for ever
        finally:
            box.stop()
    for name, what in report:
        print("%-14s %s" % (name, what))
    if left:
        print("not reached: " + " ".join(os.path.basename(p) for p in left))


def main():
    if sys.argv[1:2] == ["dbg"]:
        rest, out = sys.argv[2:], os.path.join(ROOT, "build", "bochs")
        if rest[:1] == ["--out"]:
            out, rest = rest[1], rest[2:]
        dbg(os.path.abspath(out), rest)
        return
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("sweep", "boot", "stop"))
    ap.add_argument("pkgs", nargs="*")
    ap.add_argument("--batch", type=int, default=6)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "bochs"))
    ap.add_argument("--sb", action="store_true")
    ap.add_argument("--ne2k", action="store_true")
    ap.add_argument("--c", dest="cmode", default="run",
                    choices=("run", "blank", "keep"),
                    help="C: - RUN/ and the packages (the default), BLANK "
                    "(zeros: for the installer), or KEEP the last boot's")
    ap.add_argument("--bootc", action="store_true", help="boot from C:")
    ap.add_argument("--noa", action="store_true", help="A: empty")
    ap.add_argument("--csys", action="store_true", help="C: carries the "
                    "system floppy's root files too (a hibernate off C:)")
    ap.add_argument("--btype", default="1_2", help="B:'s DRIVE: 1_2 or 1_44 "
                    "(the media is the image's own size)")
    ap.add_argument("--cfg", type=lambda v: int(v, 0))
    ap.add_argument("--build", help="the tree to boot (default build/)")
    ap.add_argument("--linger", type=int, default=0, help="sweep: seconds to "
                    "leave a boot running after MEGARUN is done (a census)")
    ap.add_argument("--b", dest="bimg", help="a 1.2MB image for B: in place "
                    "of build/apps120.img")
    ap.add_argument("--put", action="append", default=[],
                    help="FOLDER:path - one more file on C:")
    ap.add_argument("--a", dest="aimg", help="a 1.44MB system disk for A: "
                    "in place of the tree's mega.img (`make megacdtest`)")
    ap.add_argument("--cd", help="an ISO 9660 image in an ATAPI CD-ROM, the "
                    "secondary master at 170h (CDROM.DRV, SPEC.md 119): "
                    "data only - Bochs 2.7 implements no audio command")
    a = ap.parse_args()
    a.out = os.path.abspath(a.out)
    a.pkgs = [os.path.abspath(p) for p in a.pkgs]
    if a.build:
        a.build = os.path.abspath(a.build)
    if a.cmd == "stop":
        stop()
    elif a.cmd == "sweep":
        sweep(a.pkgs, a.batch, a.out, sb=a.sb, cfg=a.cfg, build=a.build,
              linger=a.linger, put=a.put, cd=a.cd)
    else:
        os.makedirs(a.out, exist_ok=True)
        box = Box(a.out, a.pkgs, a.sb, a.cfg,
                  os.path.abspath(a.bimg) if a.bimg else None, a.ne2k,
                  a.btype, a.cmode, a.bootc, a.noa, a.build, a.csys, a.put,
                  a.cd, os.path.abspath(a.aimg) if a.aimg else None)
        box.wait_for("kern_mega:", 240)
        print("running: DISPLAY=%s, E9 in %s" % (DISPLAY, box.e9))


if __name__ == "__main__":
    main()
