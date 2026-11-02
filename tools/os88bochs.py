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
each package before it is started. A fatal kern_mega fault - read out of the
kernel's state ([mega_infault], Box.fault), or the `kern_mega FAULT` line a
MEGATRAP=1 or MEGADIAG=1 tree also writes to E9 - ends the boot and is charged to the package last named; so is a minute with no new line (a
package that took the screen, or a hang). The next boot carries on with the
packages after it. Every started package gets a screenshot in --out.

NEEDS: bochs (2.7: `BIOS-bochs-legacy` - the default BIOS's 32-bit init
crashes here), Xvfb, xdotool, xwd and ImageMagick's convert; `apt-get install
bochs bochsbios vgabios bochs-x xvfb xdotool x11-apps imagemagick`. The
Debian build has the debugger compiled in and stops at reset, so `c` goes in
on stdin.
"""
import argparse
import re
import os
import shutil
import struct
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
# THE MOUSE: Bochs's PS/2 one by default; $OS88_BOCHS_MOUSE=serial puts a
# Microsoft serial mouse on COM1 instead - the transport every MartyPC machine
# has, and the only one kern_dos's INT 33h serves (kerndos/kdmouse.inc)
MOUSE = os.environ.get("OS88_BOCHS_MOUSE", "ps2")
# THE CLOCK'S START: the host's local time by default; $OS88_BOCHS_TIME0 = a
# Unix time pins it, for a row whose answer depends on the hour it is run at
# (tests/dosdir.py sets the time BACK to 13:45:30 - KERN-MEGA-PLAN 14.114)
TIME0 = os.environ.get("OS88_BOCHS_TIME0", "local")

# A FILE-SIZE CAP on every Bochs this module starts (KERN-MEGA-PLAN 14.115):
# `error: action=report` writes a line per fault, so a real-mode #GP that
# irets back into itself grew one run's bochs.log at ~500MB a minute and
# filled a 252GB volume shared by four agents. Past the cap the write gets
# SIGXFSZ and Bochs dies - a dead machine names itself, a full disk breaks
# every process on the box. $OS88_BOCHS_FSIZE_MB overrides (0 = no cap).
FSIZE_MB = int(os.environ.get("OS88_BOCHS_FSIZE_MB", "2048"))


def _fsize_cap():
    if FSIZE_MB > 0:
        import resource
        n = FSIZE_MB << 20
        # NEVER ABOVE THE HARD LIMIT IT INHERITED: an operator's `ulimit -f`
        # lower than the cap made setrlimit raise, and the row died in
        # Popen ("Exception occurred in preexec_fn") before Bochs existed
        _, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
        if hard != resource.RLIM_INFINITY:
            n = min(n, hard)
        resource.setrlimit(resource.RLIMIT_FSIZE, (n, n))
BOCHSRC = """megs: {megs}
romimage: file=/usr/share/bochs/BIOS-bochs-legacy
vgaromimage: file=/usr/share/vgabios/vgabios.bin
vga: extension=vbe{vgahz}
pci: enabled=0
floppya: 1_44={a}, status={astat}
floppyb: type={btype}, {bmedia}={b}, status=inserted
{ata0}{cd}boot: {boot}
port_e9_hack: enabled=1
mouse: enabled=1, type={mouse}, toggle=ctrl+mbutton
{com1}{com}{cpu}clock: sync={sync}, time0={time0}
{sb}{rcextra}log: {log}
panic: action=report
error: action=report
info: action=ignore
debug: action=ignore{flogdbg}
display_library: x
"""


# THE CLOCK (docs/BOCHS.md, *The clock*): `sync=slowdown` makes Bochs's time
# its INSTRUCTION COUNT - IPS_N a guest second, HLT included - and only caps
# it at real time, so a stall on the host (Bochs descheduled, a debugger stop)
# is time the guest never sees, and its instructions per tick are the same on
# any box. `realtime` ran the PIT off the host's clock instead: a starved
# Bochs was a slower machine to the guest (Tracker refused its Play on the
# calibration) and a stall was caught up in ticks afterwards (a double-click
# read 18-50 ticks against a window of 9) - every kern_mega row that failed
# under load and passed alone. $OS88_BOCHS_SYNC=realtime is the A/B, and
# os88megabochs reads its clock the old way under it.
#
# THE SPEED is Bochs's own default, 4M instructions a guest second (a
# 386SX-16, give or take - faster than the 286 kern_mega is for). Under
# `realtime` the CPU ran as fast as this host runs Bochs, ~100M a wall second,
# so two rows had been racing that CPU against a fixed pause and lost at 4M
# (mega-ethernet, mega-vidvga4 - both now wait on the state they meant). 100M
# was tried and REFUSED: a busy phase of mega-trkcompact (ten Trackers) runs
# ~6M a host second here, so a 100M guest fell to 1/17 of real time and
# os88marty.pace's backstop fired; a loaded host runs a quarter of that. The
# guest-time waits absorb a guest slower than real time, but only down to
# that backstop, so the speed is one a loaded host can still keep.
# $OS88_BOCHS_IPS changes it - a row whose subject is a real-time load on a
# faster 386
SYNC = os.environ.get("OS88_BOCHS_SYNC", "slowdown")
IPS_N = int(os.environ.get("OS88_BOCHS_IPS") or 4000000)
IPS = "cpu: ips=%d\n" % IPS_N

# A STOP COSTS THE GUEST NOTHING - but only with tools/bochs/bxclock.c
# preloaded (docs/BOCHS.md, *The clock*). `slowdown`'s cap reads time(NULL),
# the WALL clock, so host seconds spent stopped at the debugger (every read,
# every screenshot, every breakpoint) put the guest behind it, and on the
# resume Bochs ran flat out until it had caught up - up to a guest second at
# many times real speed, after every stop that crossed a second of the wall
# clock. How much guest time a host-timed interval then held depended on how
# long the stops had taken, which is host load: mega-vidfskeysvga4 looked at
# its toast 7 times where it needs 8. The preload replaces time() with a
# clock that does not move while Bochs does not run. $OS88_BXCLOCK=0 is the
# A/B (the wall clock again).
BXCLOCK = os.environ.get("OS88_BXCLOCK", "1") != "0"
BXCLOCK_SRC = os.path.join(ROOT, "tools", "bochs", "bxclock.c")


def bxclock():
    """The path of the built clock preload, building it if it is not there:
    build/bochs/bxclock-<hash of the source>.so, so a changed source is a
    new file and two machines starting at once never see a half-written
    one. RuntimeError naming the compiler when it cannot be built -
    `os88build.have_bochs()` asks for one first, so a row skips instead."""
    import hashlib
    with open(BXCLOCK_SRC, "rb") as f:
        h = hashlib.sha1(f.read()).hexdigest()[:12]
    d = os.path.join(ROOT, "build", "bochs")
    so = os.path.join(d, "bxclock-%s.so" % h)
    if os.path.exists(so):
        return so
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        raise RuntimeError("os88bochs: no C compiler (cc) to build %s, the "
                           "preload a stopped Bochs needs (docs/BOCHS.md, "
                           "*The clock*); OS88_BXCLOCK=0 runs without it"
                           % BXCLOCK_SRC)
    os.makedirs(d, exist_ok=True)
    tmp = "%s.%d.tmp" % (so, os.getpid())
    r = subprocess.run([cc, "-O2", "-Wall", "-shared", "-fPIC", "-o", tmp,
                        BXCLOCK_SRC, "-ldl"], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError("os88bochs: %s failed on %s:\n%s"
                           % (cc, BXCLOCK_SRC, r.stderr))
    os.replace(tmp, so)
    return so


INPUT_GAP = 0.03    # s: Bochs's GUI takes X events every ~10 ms of its own.
# It was 0.06, and under sync=realtime a stop was paid in GUEST time (the
# timers caught up on the host clock afterwards), so a gesture of five stepped inputs - fmarrows'
# click, Down, click inside FM_DBLCLK's 9 ticks - read 8-9 ticks at 0.06
# and 5-6 at 0.03, every edge still confirmed off mouse_btn
GLASS = 0.45        # s: two of Bochs's VGA updates (5 Hz) - see Box.shot
# The capture toggle, as a press and a release with NOTHING between them:
# xdotool's `click` holds the button ~100 ms (measured: `click 2` is 126 ms
# of xdotool against 3 ms for `mousedown`), and since a debugger stop drops
# the capture, a row that alternates input and stops paid that on every
# input - in GUEST time while the clock was sync=realtime
# THE SENTINEL, which ends every xdotool chain (_Input.xd): a key Bochs's X
# GUI maps to nothing, so it hands the guest nothing and logs instead -
# `xkeypress(): keysym 1008ff49 unhandled!` at `error: action=report` (gui/
# x.cc, the switch's default). Its line in bochs.log says Bochs has PROCESSED
# every event sent before it. Reading the X socket dry said only that Bochs
# had READ them: xcb pulls events off the socket while Bochs is blocked
# writing a frame to a slow X server, and a debugger stop taken then left
# them unprocessed - and a stop DROPS THE CAPTURE, so on the resume a button
# or a motion went nowhere and the GRAB toggled the capture back ON behind
# the harness's back, whose next GRAB toggled it OFF. Only on a loaded box,
# which is where the X server is slow. (The A/B, $OS88_BOCHS_TAKEN=socket,
# at four Bochs rows to a box: mega-assocopen and -minesrc x6 each, 1 of 12
# failed the old way - minesrc's G, a press that did not take - and 0 of 12
# the new; a stronger wait either way, and a cheap one.)
# $OS88_BOCHS_TAKEN=socket is the A/B: `taken` as it was, the sentinel only
# counted, and a stop taken with a chain still unprocessed said on stderr
TAKEN = os.environ.get("OS88_BOCHS_TAKEN", "sentinel")
SENTINEL = "XF86Launch9"
SENTINEL_LOG = b"keysym 1008ff49 unhandled"
GRAB = ("keydown", "ctrl", "mousedown", 2, "mouseup", 2, "keyup", "ctrl")


class _Input(object):
    """One X display's input state. A DEBUGGER STOP RELEASES THE MOUSE
    (Bochs drops its capture whenever the debugger takes over): a Box that
    has captured it sets `regrab` after a stop, and the next xdotool call -
    whoever makes it - re-captures FIRST, in the same call. X delivers the
    toggle ahead of the input, so the click lands captured, and a read costs
    no re-capture of its own. `last` is when input was last sent: a stop
    waits INPUT_GAP after it, so nothing is still in the X queue."""

    def __init__(self, display):
        self.display = display
        self.env = dict(os.environ, DISPLAY=display)
        self.regrab = False
        self.last = 0.0
        self.pid = None         # the Bochs reading this display (Box sets it)
        self.sent = False       # input sent that Bochs may not have READ yet
        self.log = None         # its bochs.log, for the SENTINEL (Box sets it)
        self.marks = 0          # sentinels sent to this Bochs...
        self.seen = 0           # ...and seen in its log
        self.logpos = 0
        # ONE xdotool at a time on a display: a row that wiggles the mouse
        # in a thread while it clicks would otherwise have two calls both
        # see `regrab` and toggle the capture twice - OFF
        import threading
        self.lock = threading.Lock()

    def xd(self, *a):
        with self.lock:
            pre = ()
            if self.regrab:
                self.regrab = False
                pre = GRAB
            # ...ENDING IN A ROUND TRIP (XQueryPointer): requests on one
            # connection are done in order, so when xdotool has its reply
            # the X server has PROCESSED every fake event before it and
            # written them to Bochs's connection - which is what `taken`
            # below then watches drain. (Its x:/y: line is the reply.)
            # EXCEPT AFTER `type`, which takes EVERY argument left as text:
            # the round trip was typed into the guest as letters - `kbtrip`
            # arrived as `kbtripgetmouselocation`, a Bad command in the DOS
            # window, and megasheet's macro name as COUNTgetmouselocation.
            # There xdotool's exit is the round trip: XCloseDisplay XSyncs
            #
            # ...AND ENDING IN THE SENTINEL where the Bochs has a log: a key
            # it does not map, which it does not hand the guest and DOES log
            # (see SENTINEL). X delivers a client's events in order and
            # handle_events takes them in order, so its line in bochs.log
            # is proof that every event of the chain has been PROCESSED, not
            # merely read off the socket - which is what `taken` waits on.
            # `type` takes every argument left as text, so after one it is
            # a second call (xdotool's exit XSyncs: its events come first)
            if self.log:
                tail = [] if "type" in a else ["key", SENTINEL]
            else:
                tail = [] if "type" in a else ["getmouselocation"]
            subprocess.run(["xdotool"] + [str(x) for x in pre + a] + tail,
                           env=self.env, check=True,
                           stdout=subprocess.DEVNULL)
            if self.log and "type" in a:
                subprocess.run(["xdotool", "key", SENTINEL], env=self.env,
                               check=True, stdout=subprocess.DEVNULL)
            if self.log:
                self.marks += 1
            self.last = time.time()
            self.sent = True

    def taken(self, stopped=None, timeout=10.0):
        """Return once Bochs has READ every input sent to it - and only then
        may the debugger take the machine, because A STOP RELEASES THE GRAB
        and an event Bochs takes after it goes to an uncaptured window, which
        drops it. This was INPUT_GAP alone, a host-time guess that the GUI
        had got round to the X queue: on a loaded box Bochs is not scheduled
        for longer than that, the stop came first, and the edge was lost -
        mega-httpinstall's "guest never saw the button release", a press
        confirmed and its release never arriving because nothing after it
        re-took the grab. Now it is a CONDITION: the receive queue of Bochs's
        X connection, read through sock_diag (`unread`), is empty. INPUT_GAP
        still follows it, for the events Xlib has read and not yet handed
        over. `stopped()` true means Bochs is at its prompt and reads no X
        until it resumes, so there is nothing to wait for; `timeout` is a
        backstop for a box with no sock_diag, where this is INPUT_GAP alone
        as before."""
        if self.sent and self.pid and self.log and TAKEN == "socket":
            # THE A/B: the socket read dry and INPUT_GAP, as before the
            # sentinel - and the sentinel still COUNTED, so what that left
            # in flight at a stop is said (os88bochs: ... in flight)
            self._taken_socket(stopped, timeout)
            self._scan()
            if self.seen < self.marks:
                sys.stderr.write("os88bochs: %s: a stop with %d input chain(s)"
                                 " in flight\n" % (self.display,
                                                    self.marks - self.seen))
                self.seen = self.marks
            return
        if self.sent and self.pid and self.log:
            end = time.time() + timeout
            nap = 0.001
            while self.seen < self.marks:
                if stopped is not None and stopped():
                    return              # still pending: the next stop asks
                self._scan()
                if self.seen >= self.marks:
                    break
                if time.time() > end:
                    # NOT TAKEN AT ALL: the chain went to a window that is
                    # not Bochs's. Said, and forgiven, so the next input is
                    # not charged this one's timeout as well
                    sys.stderr.write(
                        "os88bochs: %s: Bochs logged %d of %d input sentinels"
                        " in %.0fs - input went to another window, or Bochs"
                        " is wedged\n" % (self.display, self.seen,
                                           self.marks, timeout))
                    self.seen = self.marks
                    break
                time.sleep(nap)
                nap = min(nap * 2, 0.01)
            self.sent = False
            return                      # PROCESSED: nothing is in flight
        self._taken_socket(stopped, timeout)

    def _taken_socket(self, stopped, timeout):
        if self.sent and self.pid:
            end = time.time() + timeout
            nap = 0.001
            while True:
                if stopped is not None and stopped():
                    return              # still unread: the next stop asks
                n = unread(self.pid)
                if not n or time.time() > end:  # 0, or no answer
                    break
                time.sleep(nap)
                nap = min(nap * 2, 0.01)
            self.sent = False
        gap = self.last + INPUT_GAP - time.time()
        if gap > 0:
            time.sleep(gap)

    def _scan(self):
        """Count the SENTINEL's log lines past `logpos` into `seen` (a press
        and a release each log one: two lines a sentinel)."""
        try:
            with open(self.log, "rb") as f:
                f.seek(self.logpos)
                t = f.read()
        except OSError:
            return
        cut = t.rfind(b"\n") + 1
        self._half = getattr(self, "_half", 0) + t[:cut].count(SENTINEL_LOG)
        self.logpos += cut
        self.seen = self._half // 2

    def attach(self, pid, log):
        """A NEW Bochs on this display: what the last one owed is void."""
        self.pid, self.log = pid, log
        self.marks = self.seen = self.logpos = self._half = 0
        self.sent = False

    def shot(self, path):
        subprocess.run("xwd -root -silent | convert xwd:- -crop 650x500+5+40 "
                       "+repage '%s'" % path, shell=True, env=self.env,
                       check=True)


def _sockets(pid):
    """The inodes of PID's sockets - for Bochs, its X connection."""
    out = []
    try:
        for fd in os.listdir("/proc/%d/fd" % pid):
            try:
                ln = os.readlink("/proc/%d/fd/%s" % (pid, fd))
            except OSError:
                continue
            if ln.startswith("socket:["):
                out.append(int(ln[8:-1]))
    except OSError:
        pass
    return out


def unread(pid):
    """Bytes waiting UNREAD in the receive queues of PID's unix sockets,
    asked of the kernel through NETLINK_SOCK_DIAG (UNIX_DIAG_RQLEN) one
    socket at a time; None where the box will not answer. For Bochs that is
    the X events its GUI has not yet taken (see _Input.taken)."""
    import socket as so
    total, seen = 0, False
    try:
        s = so.socket(so.AF_NETLINK, so.SOCK_RAW, 4)    # NETLINK_SOCK_DIAG
    except OSError:
        return None
    try:
        s.settimeout(1.0)
        for ino in _sockets(pid):
            # unix_diag_req: family, protocol, pad, states, ino, show
            # (UDIAG_SHOW_RQLEN), cookie = INET_DIAG_NOCOOKIE: an EXACT get
            req = struct.pack("=BBHIIIII", so.AF_UNIX, 0, 0, 0xFFFFFFFF, ino,
                              0x10, 0xFFFFFFFF, 0xFFFFFFFF)
            s.send(struct.pack("=IHHII", 16 + len(req), 20, 1, ino, 0) + req)
            data = s.recv(8192)
            o = 0
            while o + 16 <= len(data):
                ln, typ = struct.unpack_from("=IH", data, o)
                if typ == 20 and ln >= 32:      # SOCK_DIAG_BY_FAMILY
                    a = o + 32
                    while a + 4 <= o + ln:
                        al, at = struct.unpack_from("=HH", data, a)
                        if al < 4:
                            break
                        if at == 4 and al >= 12:        # UNIX_DIAG_RQLEN
                            total += struct.unpack_from("=I", data, a + 4)[0]
                            seen = True
                        a += (al + 3) & ~3
                if ln < 16:
                    break
                o += (ln + 3) & ~3
    except OSError:
        return None
    finally:
        s.close()
    return total if seen else None


_IN = {}


def _input(display=None):
    display = display or DISPLAY
    if display not in _IN:
        _IN[display] = _Input(display)
    return _IN[display]


def xd(*a):
    _input().xd(*a)


_HELD = []


def take_display(lo=150, hi=199):
    """A free X display for ONE MORE machine in this process (`Box(...,
    display=)`), held until the process exits by an flock on
    /tmp/.os88bochs-N.lock - the same lock tools/os88test.py takes for a
    row's own display (:110 up), so neither hands out the other's. This is
    how a row runs its independent legs at once."""
    import fcntl
    for n in range(lo, hi + 1):
        fd = os.open("/tmp/.os88bochs-%d.lock" % n, os.O_RDWR | os.O_CREAT,
                     0o666)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            os.close(fd)
            continue
        if running(":%d" % n):      # a hand-started machine holds no lock
            os.close(fd)
            continue
        _HELD.append(fd)
        return ":%d" % n
    raise RuntimeError("os88bochs: no free display in :%d-:%d" % (lo, hi))


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
    _input().shot(path)


def autorepeat_off(display=None):
    """Turn the X server's KEY AUTOREPEAT off on DISPLAY.

    X autorepeats a held key as RELEASE/PRESS pairs, and Bochs passes each
    pair to the guest as a break and a make - a real keyboard's typematic is
    makes alone. So an xdotool `keydown` held for half a second was a dozen
    fresh presses: a held Alt+Enter toggled ArtfulType full screen 8-13 times
    (tests/altenter.py's hold, which the kernel's own typematic rule - a make
    with the key already down is a repeat - was written to absorb, and could
    not, because every make was preceded by a break). A new server is started
    with `-r`; one already running (another agent's, a hand-started one) is
    told here. It is server state, so it holds while a client - Bochs - is
    connected: called once Bochs is up. libX11 by ctypes, since there is no
    `xset` in the container."""
    import ctypes
    import ctypes.util
    try:
        x = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
        x.XOpenDisplay.restype = ctypes.c_void_p
        x.XOpenDisplay.argtypes = [ctypes.c_char_p]
        for f in ("XAutoRepeatOff", "XCloseDisplay"):
            getattr(x, f).argtypes = [ctypes.c_void_p]
        x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
        d = x.XOpenDisplay((display or DISPLAY).encode())
        if d:
            x.XAutoRepeatOff(d)
            x.XSync(d, 0)
            x.XCloseDisplay(d)
    except OSError:
        pass


def xvfb(display=None):
    display = display or DISPLAY
    n = display.lstrip(":").split(".")[0]
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
        subprocess.Popen(["Xvfb", display, "-screen", "0", "1024x768x24",
                          "-r"],            # no key autorepeat: see above
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):             # until it is listening, not a guess
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        time.sleep(0.5)


def _ours(pid, display=None):
    """Is PID's environment on this instance's display?"""
    try:
        with open("/proc/%d/environ" % pid, "rb") as f:
            env = f.read().split(b"\0")
    except OSError:
        return False
    return ("DISPLAY=" + (display or DISPLAY)).encode() in env


def _pids(*pgrep, display=None):
    r = subprocess.run(["pgrep"] + list(pgrep), capture_output=True,
                       text=True)
    return [p for p in map(int, r.stdout.split()) if _ours(p, display)]


def running(display=None):
    """PIDs of every Bochs on the box. ONE AT A TIME: the mouse, the keys and
    the screenshots all go to the X display, and with two machines on it they
    land on whichever window is on top - which reads exactly like a guest that
    froze. `bochs` execs `bochs-bin`, so that is the name to look for."""
    return _pids("-x", "bochs-bin", display=display)


def dbg(out, cmds):
    """Break into the running Bochs, run CMDS, print what they said, and
    continue it. On condition now (`session`): ~20 ms, where it was a fixed
    3.5 s + 0.3 s a command. A stop RELEASES the mouse - re-`capture` after
    one, or drive through a Box, whose `dbg` does."""
    sys.stdout.write(session(out, cmds))


_PROMPT = re.compile(r"<bochs:(\d+)> ")


def _tail(path, n):
    with open(path, "rb") as f:
        f.seek(n)
        return f.read().decode("latin-1")


def session(out, cmds, timeout=20.0, pids=None, inp=None, interrupt=True,
            at_prompt=None):
    """Break in, run the debugger commands CMDS, continue - and answer the
    transcript of the commands, read off OUT/e9.out (the debugger writes to
    the same stdout the E9 hack does).

    ON CONDITION, NOT ON A CLOCK: the stop is the debugger's prompt
    appearing, and each command is done when the NEXT prompt does, so a
    stop costs ~15-20 ms where `dbg`'s fixed 1.5 + 2 + 0.3n s used to. The
    guest is stopped for exactly that long, which under `sync=slowdown` it
    never sees."""
    e9 = os.path.join(out, "e9.out")
    # INPUT FIRST: a stop RELEASES Bochs's mouse capture, so a click still
    # in the X queue when the debugger takes over is dropped on the floor.
    # Give the GUI the moment it needs to have taken the last xdotool input.
    # (taken: until Bochs has READ it, then INPUT_GAP - a machine already
    # at its prompt reads no X, so there is nothing to wait for there)
    (inp or _input()).taken(stopped=None if interrupt else (lambda: True))
    n = os.path.getsize(e9)
    end = time.time() + timeout
    if not interrupt:
        # ALREADY STOPPED (a breakpoint hit): the prompt it printed is the
        # one before n, so start the transcript there
        with open(e9, "rb") as f:
            t = f.read().decode("latin-1")
        k = t.rfind("<bochs:")
        n = k if k >= 0 else n
    for p in (pids if pids is not None else running()) if interrupt else ():
        os.kill(p, 2)               # SIGINT: Bochs stops at its prompt
    text, nap, kick = "", 0.002, time.time() + 1.0
    while True:
        text = _tail(e9, n)
        if _PROMPT.search(text) and text.rstrip(" ").endswith(">"):
            break
        if interrupt and time.time() > kick:    # a signal lost at a prompt
            for p in (pids if pids is not None else running()):
                os.kill(p, 2)
            kick = time.time() + 1.0
        if time.time() > end:
            raise RuntimeError("os88bochs: no debugger prompt in %.0fs - "
                               "is Bochs on %s running?" % (timeout, DISPLAY))
        time.sleep(nap)
        nap = min(nap * 2, 0.02)
    stop_at = len(text)
    want = len(_PROMPT.findall(text)) + len(cmds)
    if cmds:
        with open(os.path.join(out, "dbg.fifo"), "w") as fh:
            fh.write("\n".join(cmds) + "\n")
        nap = 0.002
        while True:
            text = _tail(e9, n)
            if (len(_PROMPT.findall(text)) >= want
                    and text.rstrip(" ").endswith(">")):
                break
            if time.time() > end:
                break               # say what it did say, and carry on
            time.sleep(nap)
            nap = min(nap * 2, 0.02)
    if at_prompt is not None:       # ...anything the caller does while
        at_prompt()                 # the guest is stopped (Box.dblclick)
    with open(os.path.join(out, "dbg.fifo"), "w") as fh:
        fh.write("c\n")
    # ...and until Bochs has READ it (it echoes each command after its
    # prompt): a SIGINT landing while it still sits at the prompt is lost,
    # and the next stop then waits for a prompt that never comes
    k = len(_tail(e9, n))
    while time.time() < end:
        t = _tail(e9, n + k - 4) if k >= 4 else _tail(e9, n)
        if "c\n" in t:
            break
        time.sleep(0.002)
    return text[stop_at:]


def xp_bytes(text):
    """The bytes every `xp /Nbx ADDR` in a transcript printed, keyed by
    ADDR, in order."""
    got, cur = {}, None
    for line in text.splitlines():
        m = re.search(r"xp /\d+bx (0x[0-9a-f]+)\s*$", line)
        if m:
            cur = int(m.group(1), 16)
            got[cur] = b""
            continue
        m = re.match(r"0x[0-9a-f]+ <bogus\+\s*\d+>:\s+(.*)", line)
        if m and cur is not None:
            got[cur] += bytes(int(w, 16) for w in m.group(1).split())
        elif _PROMPT.match(line):
            cur = None
    return got


def kmap(build=None):
    """{name: LINEAR address} for every kernel label the tree's kern_mega
    map places (megak/kernel.map): .text and .bss at KERNEL_SEG, .lowbss at
    LOW_SEG, .cold at COLD_SEG, by the map's own equates. Take it from the
    tree that BUILT the disk in A: (`--build`), or every address is a
    plausible wrong one (docs/BOCHS.md)."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88sym
    path = os.path.join(build or os.path.join(ROOT, "build"), "megak",
                        "kernel.map")
    off, sect, equ = os88sym._parse_map(path)
    seg = {".text": equ["KERNEL_SEG"], ".bss": equ["KERNEL_SEG"],
           ".lowbss": equ.get("LOW_SEG"), ".cold": equ.get("COLD_SEG")}
    return {k: seg[sect[k]] * 16 + v for k, v in off.items()
            if seg.get(sect[k]) is not None}


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
                 build=None, csys=False, put=(), cd=None, aimg=None,
                 display=None, megs=None, noc=False, vgahz=None,
                 flog=False, mouse=None, noserial=False, imps2=False,
                 cimg=None, cgeom=None, ne2k_io=None, rcextra="",
                 ips=None):
        # DISPLAY: the X display this machine owns - the module's
        # ($OS88_BOCHS_DISPLAY, :99) by default, or its own, so ONE process
        # can run several machines at once (one display each)
        self.display = display or DISPLAY
        self.inp = _input(self.display)
        self.env = self.inp.env
        if running(self.display):
            sys.exit("os88bochs: a Bochs is already running on %s (pids %s) "
                     "- every click would land on whichever window is on "
                     "top. `os88bochs.py stop` ends it" %
                     (self.display,
                      " ".join(map(str, running(self.display)))))
        bld = build or os.path.join(ROOT, "build")
        self.build = bld
        # THIS machine's instructions a guest second (guest_sleep)
        self.ips = (ips if ips and not os.environ.get("OS88_BOCHS_IPS")
                    else IPS_N)
        self._map = None
        self.captured = False
        self.inp.regrab = False
        self.out = out
        self.e9 = os.path.join(out, "e9.out")
        a, b = os.path.join(out, "a.img"), os.path.join(out, "b.img")
        c = os.path.join(out, "c.img")
        # `cimg`: THE CALLER'S OWN FIXED DISK, mounted IN PLACE (written
        # through, so the host reads back what the guest wrote, and a second
        # Box on the same file boots what the first left) at `cgeom` (C, H,
        # S) - a row's VHD, os88megabochs's `--mount hd:0:` (docs/BOCHS.md)
        if cimg:
            c, noc = os.path.abspath(cimg), False
        geom = tuple(cgeom) if cgeom else GEOM
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
        # MEGARUN rides the C: disk only, so a machine with none (`noc`,
        # os88megabochs) does not need it built - a knob tree a ported row
        # builds for itself (`targets=("mega",)`) has none
        args = [] if noc or cimg or (cmode == "keep" and os.path.exists(c)) else [
            "RUN:" + self._stage(stage, os.path.join(bld, "megarun.o88"),
                                 "0run.o88")]
        for p in pkgs:
            args.append("RUN:" + self._stage(stage, p,
                                             os.path.basename(p).lower()))
        args += list(put)          # --put: FOLDER:path, as os88disk.py takes it
        if csys:
            args += self._sysfiles(a, stage)
        if noc or cimg:
            pass                    # no fixed disk at all (os88megabochs),
                                    # or the caller's own, as it stands
        elif cmode == "keep" and os.path.exists(c):
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
            # `imps2`: an IntelliMouse, so the WHEEL reaches the guest
            f.write(BOCHSRC.format(
                # `ips`: THIS machine's guest speed, where the row's subject
                # is a real-time load the default 4M CPU correctly declines
                # (tests/x3mega.py's live play); $OS88_BOCHS_IPS still wins
                megs=megs or MEGS, a=a, b=b,
                cpu=("cpu: ips=%d\n" % ips
                     if ips and not os.environ.get("OS88_BOCHS_IPS") else IPS),
                time0=TIME0, sync=SYNC,
                # `rcextra`: bochsrc lines a row adds as they stand - an
                # option ROM (`optromimage1:`, tests/megapmmouse.py)
                rcextra=rcextra,
                mouse="imps2" if imps2 else (mouse or MOUSE),
                com1="com1: enabled=1, mode=mouse\n"
                if (mouse or MOUSE) == "serial" else "",
                # NO UART AT ALL (`noserial`): the PS/2 mouse the only
                # pointing device, QEMU's `-serial none` (tests/ps2mouse.py)
                com="com1: enabled=0\n" if noserial and (mouse or MOUSE) != "serial" else "",
                flogdbg=", floppy=report" if flog else "",
                ata0="" if noc else (
                    "ata0: enabled=1, ioaddr1=0x1f0, ioaddr2=0x3f0, irq=14\n"
                    "ata0-master: type=disk, path=%s, mode=flat, "
                    "cylinders=%d, heads=%d, spt=%d\n" % ((c,) + geom)),
                vgahz=", update_freq=%d" % vgahz if vgahz else "",
                # A C: OF THE CALLER'S tries A: first and falls to C: - a
                # blank or empty A: is how a MartyPC row offers the BIOS C:
                boot="disk" if bootc else (
                    "floppy, disk" if cimg else "floppy"),
                astat="ejected" if noa else "inserted",
                btype=btype, bmedia={368640: "360k", 737280: "720k",
                                     1228800: "1_2", 1474560: "1_44"}
                [os.path.getsize(b)],
                log=os.path.join(out, "bochs.log"),
                cd=("ata1: enabled=1, ioaddr1=0x170, ioaddr2=0x370, irq=15\n"
                    "ata1-master: type=cdrom, path=%s, status=inserted\n" %
                    os.path.abspath(cd)) if cd else "",
                # waveindrv=dummy: the default input driver is the host's
                # (ALSA, absent in a container), and a guest that opened the
                # DSP for INPUT (Recorder, SPEC.md 34) wedged Bochs with no
                # debugger prompt ever again - the dummy hands it silence
                sb=("sound: waveoutdrv=file, waveout=%s, waveindrv=dummy\n"
                    "sb16: enabled=1, midimode=0, wavemode=1, "
                    "dmatimer=%s\n" % (os.path.join(out, "sb.wav"), os.environ.get("OS88_BOCHS_DMATIMER", "600000")))
                if sb else "") + (
                "ne2k: ioaddr=0x%x, irq=10, mac=52:54:00:12:34:56, "
                "ethmod=slirp%s\n" % (ne2k_io or 0x300,
                                       ", script=" + os.path.abspath(SLIRP)
                                       if SLIRP else "") if ne2k else ""))
        for n in os.listdir(out):
            if n.endswith(".lock"):
                os.remove(os.path.join(out, n))
        # ...and the caller's own C:'s, which lives OUTSIDE out: Bochs locks
        # a disk image with PATH.lock and a stopped Bochs leaves it, so the
        # second launch on the same disk (install, then boot it) was refused
        # with "could not open hard drive image file"
        if cimg and os.path.exists(c + ".lock"):
            os.remove(c + ".lock")
        xvfb(self.display)
        self.f = open(self.e9, "wb")
        # The debugger reads a FIFO, held open by a `sleep` of its own so the
        # machine outlives this process (`boot`) and `dbg` can still reach it
        fifo = os.path.join(out, "dbg.fifo")
        if os.path.exists(fifo):
            os.remove(fifo)
        os.mkfifo(fifo)
        # The clock preload on BOCHS ALONE (the wrapper script execs
        # bochs-bin and the variable rides through), never on the display's
        # shared env, which xdotool and xwd also run under
        benv = self.env
        if BXCLOCK and SYNC == "slowdown":
            pre = os.environ.get("LD_PRELOAD")
            benv = dict(self.env, LD_PRELOAD=bxclock() +
                        (" " + pre if pre else ""))
        self.p = subprocess.Popen(["bochs", "-q", "-f", rc], cwd=out,
                                  stdin=os.open(fifo, os.O_RDWR),
                                  stdout=self.f, stderr=subprocess.STDOUT,
                                  env=benv, preexec_fn=_fsize_cap)
        self.inp.attach(self.p.pid,     # (the wrapper execs bochs-bin)
                        os.path.join(out, "bochs.log"))
        self.hold = subprocess.Popen("exec sleep 86400 > '%s'" % fifo,
                                     shell=True, env=self.env)
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
        """E9's lines: a test package's own, Bochs's debugger transcript, and
        on a MEGADIAG=1 or MEGATRAP=1 kernel the kernel's (MEGA_E9) - a
        SHIPPED kern_mega writes none, so read `faults()` and `wait_pm()`
        for what it used to say there."""
        with open(self.e9, "rb") as f:
            t = f.read().decode("latin-1")
        return [l for l in t.splitlines() if l.strip()]

    def said(self):
        """`lines()` and then `faults()`: everything a row that scans for
        `kern_mega FAULT` / `kern_mega CLOSED` lines needs, whichever kernel
        it booted. One stop of the guest; call it before `stop()`."""
        return self.lines() + self.faults()

    def wait_for(self, text, secs):
        end = time.time() + secs
        while time.time() < end:
            if any(text in l for l in self.lines()):
                return True
            time.sleep(0.2)
        return False

    def xd(self, *a):
        """xdotool on THIS machine's display (re-capturing first after a
        debugger stop, as the module's `xd` does on the default display)."""
        self.inp.xd(*a)

    def key(self, *keys, delay=80):
        self.xd("key", "--delay", delay, *keys)

    def type(self, text, delay=80):
        self.xd("type", "--delay", delay, text)

    def capture(self):
        w = subprocess.run(["xdotool", "search", "--name", "Bochs"],
                           env=self.env,
                           capture_output=True, text=True).stdout.split()[0]
        self.inp.regrab = False
        autorepeat_off(self.display)
        self.xd("windowfocus", "--sync", w)
        self.xd("mousemove", 330, 300)
        self.xd("keydown", "ctrl")
        self.xd("click", 2)
        self.xd("keyup", "ctrl")
        self.captured = True
        time.sleep(1)

    # ---- the guest's own state, through the debugger (docs/BOCHS.md,
    # "Waiting on the guest"). Every wait below ends on a CONDITION read out
    # of kernel memory, so none of them is a host sleep standing in for one.

    def sym(self, name):
        """The LINEAR address of kernel label NAME, off this boot's tree."""
        if self._map is None:
            self._map = kmap(self.build)
        return self._map[name]

    def ptime(self):
        """Bochs's instruction clock, read in one stop of the guest."""
        m = re.findall(r"ptime: (\d+)", self.dbg(["ptime"]))
        if not m:
            raise RuntimeError("os88bochs: no answer to `ptime`")
        return int(m[-1])

    def guest_sleep(self, secs, poll=0.05):
        """Let the guest run SECS of ITS OWN seconds (`ptime` over this
        machine's ips), however long the host takes to give them. A
        time.sleep() between two inputs is a HOST interval, and how much of
        the guest it holds is how much CPU this Bochs got meanwhile: with
        four Bochs to a core megavidv2's "Space, then Space again a second
        in, while the blocks are coming" paused before the first frame had
        drawn, and both tails were wrong. Read every POLL host seconds, one
        stop each, which costs the guest nothing (bxclock), so it overshoots
        by at most a poll's worth of running. A backstop at 10x real time
        plus 30 s names a machine that stopped running."""
        want = int(secs * self.ips)
        t0, end = self.ptime(), time.time() + 30 + 10 * secs
        while True:
            time.sleep(poll)
            if self.ptime() - t0 >= want:
                return
            if time.time() > end:
                raise RuntimeError("os88bochs: %.2f guest seconds not run in "
                                   "%.0f host seconds" % (secs, 30 + 10 * secs))

    def at_prompt(self, cmds, timeout=20.0):
        """`dbg` for a machine that is ALREADY at the debugger's prompt -
        stopped on a breakpoint (`lb`): run CMDS, continue, answer the
        transcript. No signal is sent."""
        t = session(self.out, cmds, timeout, pids=[self.p.pid],
                    inp=self.inp, interrupt=False)
        if self.captured:
            self.inp.regrab = True
        return t

    def dbg(self, cmds, timeout=20.0):
        """`session` on this machine: the commands' transcript.

        A STOP RELEASES THE MOUSE: Bochs drops its capture whenever the
        debugger takes over, so after one the next move and click reach
        nobody. Once `capture` has been called this re-captures after every
        stop, so a read can sit between two moves."""
        t = session(self.out, cmds, timeout, pids=[self.p.pid],
                    inp=self.inp)
        if self.captured:
            self.inp.regrab = True  # the next input re-captures first
        return t

    def reads(self, spans):
        """[bytes] for each (address or label, length) in SPANS, all in ONE
        stop of the guest (~20 ms)."""
        ads = [self.sym(a) if isinstance(a, str) else a for a, _ in spans]
        got = xp_bytes(self.dbg(["xp /%dbx 0x%x" % (n, a)
                                 for a, (_, n) in zip(ads, spans)]))
        out = []
        for a, (_, n) in zip(ads, spans):
            b = got.get(a, b"")
            if len(b) < n:
                raise RuntimeError("os88bochs: xp 0x%x came back %d of %d "
                                   "bytes" % (a, len(b), n))
            out.append(b[:n])
        return out

    def read(self, addr, n):
        return self.reads([(addr, n)])[0]

    def until(self, cond, what, secs=60.0, poll=0.1):
        """Poll COND() (which reads the guest) until true; RuntimeError
        naming WHAT once SECS GUEST seconds have run without it.

        IT WAS SECS HOST SECONDS, and that is the failure the soak keeps
        reporting as contention: the guest runs at real time AT MOST
        (`clock: sync=slowdown`), so with four Bochs to a core a 60-second
        wait handed the machine a fraction of 60 guest seconds and a row
        that passes alone timed out under load (megamod's `wide` leg). The
        guest clock is read (`ptime`, one stop, free to the guest) only once
        the host deadline has passed, so a wait that succeeds in time costs
        nothing extra; past it, the wait goes on while the guest is still
        short of SECS of its own. guest_sleep's backstop - 10x real time plus
        30 s - names a machine that stopped running."""
        t0 = time.time()
        end = t0 + secs
        back = t0 + 30 + 10 * secs
        try:
            g0 = self.ptime()
        except (RuntimeError, OSError):
            g0 = None               # (no clock to read: host seconds alone)
        while True:
            v = cond()
            if v:
                return v
            f = self.fault()
            if f:                   # a FATAL fault ends every wait: the
                raise RuntimeError(  # machine is stopped on its screen
                    "os88bochs: %s while waiting for %s" % (f, what))
            now = time.time()
            if now > end:
                ran = None
                if g0 is not None:
                    try:
                        ran = (self.ptime() - g0) / float(self.ips)
                    except (RuntimeError, OSError):
                        pass
                if ran is None or ran >= secs or now > back:
                    raise RuntimeError(
                        "os88bochs: no %s in %.0fs%s" % (
                            what, secs, "" if ran is None else
                            " (%.1f guest seconds in %.0f host)"
                            % (ran, now - t0)))
                end = now + min(poll * 20, 2.0)     # look again shortly
            time.sleep(poll)

    # ---- WHAT kern_mega USED TO SAY ON E9, read out of its STATE instead.
    # A shipped kern_mega writes nothing to the debug port (kernel.asm's
    # MEGA_E9: only MEGADIAG=1 and MEGATRAP=1 do, and `lines()` is still what
    # they wrote). The switch is [mega_on]; a FATAL fault is [mega_infault],
    # set by mega_report before it draws mega_fbuf - the glass's own line,
    # " kern_mega FAULT vv err eeee at cccc:iiii sp ssss" - and halts; a
    # package CLOSED for a fault is [mega_fkn] (faults in the incident, 0 =
    # none ever) with mega_fkmsg its toast, "NAME vv at cccc:iiii". Each is
    # true only while the KERNEL is in conventional memory: in a DOS trip
    # that RAM is kern_dos's, so every read checks a constant of the image
    # first (`home`).
    _HOME = ("mega_tmem", b"real mode: no room for kern_mega's tables")
    # ...a toast's text in .text (mega.inc), never written: at its address
    # while the kernel is resident, kern_dos's bytes while it is parked

    def _home_span(self):
        return (self._HOME[0], len(self._HOME[1]))

    def _state(self):
        """(home, on, infault, fkn) in one stop of the guest"""
        h, on, inf, fkn = self.reads([self._home_span(), ("mega_on", 1),
                                      ("mega_infault", 1), ("mega_fkn", 1)])
        home = h == self._HOME[1]
        return (home, home and on[0] != 0, home and inf[0] != 0,
                fkn[0] if home else 0)

    def home(self):
        """Is the kernel in conventional memory - not parked for a DOS trip,
        not between a reset and its reload?"""
        return self._state()[0]

    def pm(self):
        """Has kern_mega switched to protected mode (and is it home)?"""
        return self._state()[1]

    def wait_pm(self, secs):
        """`wait_for("kern_mega: protected mode", SECS)`'s replacement, off
        [mega_on]: True once the switch is made, False after SECS host
        seconds (or on a fault first)."""
        end = time.time() + min(secs, 60)
        while not re.search(r"<bochs:1> c", _tail(self.e9, 0)):
            if time.time() > end or self.p.poll() is not None:
                return False        # (Bochs never took its first `c`: a
            time.sleep(0.05)        # stop before it is lost)
        try:
            self.until(self.pm, "the switch to protected mode", secs,
                       poll=0.5)
            return True
        except RuntimeError:
            if getattr(self, "_flt", None):
                raise               # a FATAL fault: say it, not a timeout
            return False

    def _fbuf_line(self):
        b = self.read("mega_fbuf", 59).decode("latin-1")
        return b.split(" - ")[0].strip()

    def fault(self):
        """The FATAL fault's line, "kern_mega FAULT vv err eeee at cccc:iiii
        sp ssss" off mega_fbuf, or None. Sticky: the machine is halted once
        one is had. Unreadable state (Bochs gone) answers None - the caller's
        own read fails louder. At most one stop a second (`until` asks every
        poll, and a poll can be 50 ms)."""
        if getattr(self, "_flt", None) is None:
            if time.time() - getattr(self, "_flt_at", 0) < 1.0:
                return None
            self._flt_at = time.time()
            try:
                if self._state()[2]:
                    self._flt = self._fbuf_line()
            except (RuntimeError, OSError):
                return None
        return getattr(self, "_flt", None)

    def faults(self):
        """[line] for a FATAL fault and for a package CLOSED by one -
        "kern_mega FAULT ..." and "kern_mega CLOSED NAME vv at cccc:iiii" -
        which is what the shipped kernel put on E9 before MEGA_E9. CLOSED
        carries the LAST incident (mega_fkmsg holds one), so a row counting
        incidents polls (tests/megaflt.py). Once a Box has been asked, its
        `stop()` reads it one last time, and after the stop this answers
        that reading; a read that fails is a line too, so it cannot pass for
        a clean run."""
        if getattr(self, "_stopped_faults", None) is not None:
            return list(self._stopped_faults)
        self._asked = True
        try:
            home, _, inf, fkn = self._state()
            out = []
            if inf:
                out.append(self._fbuf_line())
            if fkn:
                msg = self.read("mega_fkmsg", 25).split(b"\0")[0]
                out.append("kern_mega CLOSED " + msg.decode("latin-1"))
            return out
        except (RuntimeError, OSError) as e:
            return ["kern_mega state unreadable: %s" % e]

    # A DOS TRIP: kern_mega parks the session above 1MB and kern_dos takes
    # conventional memory (KERN-MEGA-DOS-PLAN), so while it runs NOTHING of
    # the kernel can be read - `home()` is False. [mega_dwoke] is 1 once a
    # trip is back (mega_dwake sets it AFTER the restore, so it survives the
    # copy) and 0 in the snapshot, so a session past its first trip reads 1
    # between trips: `arm_trip` writes it 0 first. The kernel writes 0 there
    # itself at the next trip's start and reads it only at that trip's two
    # returns (mega_dosparkf), so the write changes nothing the machine does.
    def arm_trip(self):
        """Ready `trip_back` for the NEXT trip: call it before starting it"""
        self.dbg(["setpmem 0x%x 1 0" % self.sym("mega_dwoke")])

    def trip_back(self):
        """Has the trip since `arm_trip` come home (kernel resident, in
        protected mode, [mega_dwoke] 1)?"""
        h, on, wk = self.reads([self._home_span(), ("mega_on", 1),
                                ("mega_dwoke", 1)])
        return h == self._HOME[1] and on[0] != 0 and wk[0] == 1

    def wait_trip(self, secs):
        """`wait_for("kern_mega: unparked", SECS)`'s replacement: True once
        the armed trip is home, False after SECS host seconds."""
        try:
            self.until(self.trip_back, "the DOS trip's return", secs,
                       poll=0.25)
            return True
        except RuntimeError:
            if getattr(self, "_flt", None):
                raise               # a FATAL fault: say it, not a timeout
            return False

    def park(self):
        """(KB parked, its linear base) as the session holds them - what
        `kern_mega: parked kkkkK at pppppp` said, read once it is back"""
        kb, b = self.reads([("mega_dikb", 2), ("mega_dpbase", 4)])
        return (struct.unpack("<H", kb)[0], struct.unpack("<I", b)[0])

    def wait_resumed(self, secs, since=0):
        """`wait_for("kern_mega: resumed", SECS)`'s replacement: True once
        [hb_resumes] (hbm_wake's count) passes SINCE with the kernel home and
        in protected mode. A boot zeroes it, so a fresh Box's SINCE is 0."""
        def woke():
            h, on, n = self.reads([self._home_span(), ("mega_on", 1),
                                   ("hb_resumes", 2)])
            return (h == self._HOME[1] and on[0] != 0 and
                    struct.unpack("<H", n)[0] > since)
        try:
            self.until(woke, "the hibernated session's resume", secs,
                       poll=0.5)
            return True
        except RuntimeError:
            if getattr(self, "_flt", None):
                raise               # a FATAL fault: say it, not a timeout
            return False

    def _ui_spans(self):
        import os88marty as om
        return [("desk_rows", 2), ("menu_nbar", 2), ("spl_live", 1),
                ("ticks", 2), (self.sym("sch_tasks") + om.UI_T_STATE, 1),
                ("evq_count", 1), ("sch_uiwake", 1), ("gfx_lock_flag", 1),
                (om.BIOS_KBUF, 4)]

    def _ui_state(self, lock=True):
        """(the desktop is up, the UI task is idle, [ticks]) in one stop:
        os88marty's kernel_up and ui_idle tests, the same five bytes."""
        import os88marty as om
        d, mb, sl, tk, st, eq, wk, gl, kb = self.reads(self._ui_spans())
        up = any(d) and any(mb) and not sl[0]
        idle = (st[0] == om.UI_ASLEEP and not eq[0] and not wk[0]
                and not (lock and gl[0]) and kb[0:2] == kb[2:4])
        return up, idle, tk

    def ui_done(self, what="the UI to finish with that input", secs=60.0,
                lock=True, hold=0.25):
        """Until the UI task is idle at two reads HOLD apart with the
        kernel's tick moving between them: os88marty.ui_done's shape.
        Answers the host seconds it took."""
        t0, first = time.time(), [None]

        def done():
            up, idle, tk = self._ui_state(lock)
            if not idle:
                first[0] = None
                return False
            if first[0] is None or time.time() - first[0][0] < hold:
                if first[0] is None:
                    first[0] = (time.time(), tk)
                return False
            return tk != first[0][1]
        self.until(done, what, secs, poll=0.05)
        return time.time() - t0

    def up(self, secs=240.0):
        """THE BOOT GATE: `desk_rows` and `menu_nbar` written, [spl_live]
        0 and [ticks] moving across it, then `ui_done` - os88marty's
        kernel_up, where the rows used to wait for the first E9 line and then
        sleep 15 s. A desktop is ~6 s after reset here."""
        end = time.time() + secs
        if not self.wait_pm(secs):
            raise RuntimeError("os88bochs: kern_mega did not switch to "
                               "protected mode in %.0fs" % secs)
        seen = [None]

        def ready():
            up, _, tk = self._ui_state()
            if not up:
                seen[0] = None
                return False
            if seen[0] is None:
                seen[0] = tk
                return False
            return tk != seen[0]
        self.until(ready, "desktop", max(1.0, end - time.time()), poll=0.1)
        return self.ui_done("the desktop's first paint",
                            max(1.0, end - time.time()))

    def instances(self):
        """[(slot, name, I_SPTR, I_FLAGS)] for every LIVE package instance,
        off inst_tab (kernel/instance.inc's record, tools/os88geom.py's
        I_* offsets)."""
        import os88geom as g
        raw = self.read("inst_tab", g.INST_MAX["big"] * g.I_RECSZ
                        if isinstance(g.INST_MAX, dict) else
                        g.INST_MAX * g.I_RECSZ)
        out = []
        for i in range(len(raw) // g.I_RECSZ):
            r = raw[i * g.I_RECSZ:(i + 1) * g.I_RECSZ]
            if r[g.I_STATE] != 1 or not r[g.I_KIND] & g.KIND_PKG:
                continue
            nm = r[g.I_NAME:g.I_CYC].split(b"\0")[0].decode("latin-1")
            out.append((i, nm, int.from_bytes(r[g.I_SPTR:g.I_SPTR + 2],
                                               "little"), r[g.I_FLAGS]))
        return out

    def ldt(self):
        """{selector: (base, limit)} for every descriptor `info ldt` lists:
        where an OS88_HIGH package's LDT selector points on THIS boot."""
        out = {}
        for line in self.dbg(["info ldt"]).splitlines():
            m = re.match(r"LDT\[(0x[0-9a-f]+)\]=\w+ segment, base="
                         r"(0x[0-9a-f]+), limit=(0x[0-9a-f]+)", line)
            if m:
                out[(int(m.group(1), 16) << 3) | 4] = (int(m.group(2), 16),
                                                        int(m.group(3), 16))
        return out

    def ldt_base(self):
        """The LDT's linear base, off LDTR (`sreg`), asked once a boot: the
        kernel builds the table once and never moves it."""
        if getattr(self, "_ldtb", None) is None:
            m = re.search(r"ldtr:0x[0-9a-f]+, dh=(0x[0-9a-f]+), "
                          r"dl=(0x[0-9a-f]+)", self.dbg(["sreg"]))
            if not m:
                raise RuntimeError("os88bochs: no LDTR in `sreg`")
            dh, dl = int(m.group(1), 16), int(m.group(2), 16)
            self._ldtb = ((dl >> 16) | ((dh & 0xFF) << 16)
                          | (dh & 0xFF000000))
        return self._ldtb

    def sel_base(self, sel):
        """An LDT selector's base, out of its descriptor in memory (one stop
        - `info ldt` prints all 2,048 and costs ten times that)."""
        d = self.read(self.ldt_base() + (sel & ~7), 8)
        return d[2] | d[3] << 8 | d[4] << 16 | d[7] << 24

    def mod_base(self, name):
        """The LINEAR base on-demand module NAME ("CTRL.DRV") is loaded at,
        or None while it is not: its mod_tab row's MODR_SEG - a selector
        once it went above 1MB (mod_hi), where a MEGA_E9 kernel also says
        `kern_mega: above 1MB: NAME base` on E9 - or a paragraph."""
        a, e = self.sym("mod_tab"), self.sym("mod_tab_end")
        rows = self.read(a, e - a)
        base = a - self._text_off("mod_tab")    # KERNEL_SEG * 16
        ents = [struct.unpack_from("<HH", rows, i)  # MODR_SEG, MODR_FILE
                for i in range(0, len(rows) - 3, 4)]  # (MODR_SIZE 4)
        names = self.reads([(base + fn, 13) for _, fn in ents])
        for (seg, _), f in zip(ents, names):
            if f.split(b"\0")[0].decode("latin-1").upper() == name.upper():
                if not seg:
                    return None
                return self.sel_base(seg) if seg & 4 else seg * 16
        return None

    def _text_off(self, label):
        """LABEL's offset in its KERNEL_SEG segment (.text and .bss)."""
        if getattr(self, "_kseg", None) is None:
            sys.path.insert(0, os.path.join(ROOT, "tools"))
            import os88sym
            off, _, equ = os88sym._parse_map(os.path.join(
                self.build or os.path.join(ROOT, "build"), "megak",
                "kernel.map"))
            self._kseg, self._koff = equ["KERNEL_SEG"], off
        return self._koff[label]

    def pkg_base(self, name):
        """The LINEAR address of package NAME's image (its DS:0), NAME being
        the header's name or a prefix of it, case ignored: a conventional
        segment * 16, or an LDT selector's base above 1MB (OS88_HIGH). None
        while no live instance has that name. Asked afresh each call, since
        a region MOVES (SPEC.md 66): two stops of the guest."""
        for _, nm, sp, _ in self.instances():
            if nm.lower().startswith(name.lower()):
                return self.sel_base(sp) if sp & 4 else sp * 16
        return None

    def pkg_read(self, name, label, n, pkgmap=None):
        """N bytes at LABEL (a name in PKGMAP, nasm's `[map all]` of the
        package - build/<pkg>.map - or a plain offset) of package NAME's
        image."""
        base = self.pkg_base(name)
        if base is None:
            raise RuntimeError("os88bochs: no live instance of %r" % name)
        if isinstance(label, str):
            sys.path.insert(0, os.path.join(ROOT, "tools"))
            import os88sym
            label = os88sym._parse_map(pkgmap)[0][label]
        return self.read(base + label, n)

    def in_fsx(self):
        """True while an exclusive fullscreen bracket runs ([fsx_task] is
        the exclusive task's slot, FFh off - SPEC.md 53.2). ui_task is
        frozen inside one, so `ui_done` cannot end there: wait on this."""
        return self.read("fsx_task", 1)[0] != 0xFF

    def shot(self, path):
        """A screenshot of the GLASS, which lags the guest's framebuffer by
        up to one VGA update (5 a second, Bochs's default): after a
        `ui_done` the picture is drawn, so wait out one update and take it."""
        time.sleep(GLASS)
        self.inp.shot(path)

    def mouse(self):
        """(mouse_x, mouse_y): where the KERNEL has the pointer."""
        b = self.read("mouse_x", 4)
        return (int.from_bytes(b[0:2], "little"),
                int.from_bytes(b[2:4], "little"))

    def goto(self, x, y, tries=12):
        """CLOSED-LOOP pointer: read where the kernel has it, send the
        difference (two X pixels a guest pixel, in steps Bochs passes on
        unclamped), read it back, until it is exactly (X, Y). No homing
        walk, no fixed pause: ~0.1-0.3 s where module `goto` is ~3.5 s.
        RuntimeError if the pointer will not move - the mouse is not
        captured (`capture`), or the guest is not taking packets."""
        pos, stuck = self.mouse(), 0
        for _ in range(tries):
            if stuck >= 2:          # two sends that moved NOTHING: dead -
                break               # said in ~4 s, not tries x 2
            dx, dy = x - pos[0], y - pos[1]
            if not dx and not dy:
                return pos
            x2, y2 = 2 * dx, 2 * dy
            while x2 or y2:
                sx, sy = max(-60, min(60, x2)), max(-60, min(60, y2))
                self.xd("mousemove_relative", "--", sx, sy)
                x2 -= sx
                y2 -= sy
            was, end = pos, time.time() + 2.0
            while True:
                time.sleep(0.03)
                pos = self.mouse()
                if pos != was:
                    # until it STOPS: one more read the same
                    time.sleep(0.03)
                    p2 = self.mouse()
                    if p2 == pos:
                        break
                    pos = p2
                    continue
                if time.time() > end:
                    break
            stuck = stuck + 1 if pos == was else 0
        raise RuntimeError("os88bochs: the pointer is at %s, not (%d, %d) - "
                           "is the mouse captured?" % (pos, x, y))

    def click(self, x=None, y=None, what="the UI to take the click",
              lock=True, button=1):
        if x is not None:
            self.goto(x, y)
        self.xd("click", button)
        return self.ui_done(what, lock=lock)

    def dblclick(self):
        """A double-click where the pointer is, QUEUED ON A STOPPED GUEST:
        Bochs's GUI drains the X queue in one pass and each PS/2 button
        change is a packet of its own, so the four edges are a few GUEST ms
        apart whatever the host does (os88megabochs.queued_input is the
        shim's twin; docs/BOCHS.md *The clock*). `click --repeat 2 --delay
        120` spaced them in HOST time, and a guest catching up after a long
        stop - megahma's few hundred `setpmem`s - ran that spacing past the
        kernel's 9-tick window: two first clicks, KBTRIP never started.
        The stop drops the capture, so the queue starts with GRAB."""
        def queue():
            if self.captured:
                self.inp.regrab = True
            self.inp.xd("mousedown", 1, "mouseup", 1, "mousedown", 1,
                        "mouseup", 1)
        session(self.out, [], pids=[self.p.pid], inp=self.inp,
                at_prompt=queue)
        self.inp.taken()            # read by Bochs before any next stop

    def dbl(self, x=None, y=None, what="the UI to take the double-click",
            lock=True):
        """A double-click, then `ui_done` - `lock=False` when what it opens
        is a program whose worker holds the gfx lock (a game)."""
        if x is not None:
            self.goto(x, y)
        self.dblclick()
        return self.ui_done(what, lock=lock)

    def megarun(self, row=0):
        """Capture the mouse, open C:, its RUN folder and 0RUN.O88 (ROW
        rows down when --put made folders in RUN/): MEGARUN starts the
        rest. Each double-click waits for the UI to be done with it rather
        than the 10 s and 8 s the recipe slept."""
        self.capture()
        self.dbl(591, 160, "C:'s Disk window")
        self.dbl(150, 123, "RUN's listing")
        self.dbl(160, 139 + 16 * row, "MEGARUN's start")

    def stop(self):
        if getattr(self, "_asked", False) and self.p.poll() is None and \
                getattr(self, "_stopped_faults", None) is None:
            self._stopped_faults = self.faults()    # (see faults())
        self.hold.kill()
        self.p.terminate()
        try:
            self.p.wait(10)
        except subprocess.TimeoutExpired:
            self.p.kill()
        self.f.close()


def _sweep_boot(d, todo, quiet_secs, sb, cfg, build, cap_secs, linger, put,
                cd, display=None):
    """One boot of `sweep`: TODO's packages on C:, MEGARUN started, its E9
    followed. -> (report lines, the names MEGARUN started, booted)"""
    os.makedirs(d, exist_ok=True)
    report, started = [], []
    box = Box(d, todo, sb, cfg, build=build, put=put, cd=cd, display=display)
    shot = box.inp.shot             # (this machine's display)
    try:
        try:
            box.up()                # the desktop, off kernel state
        except RuntimeError as e:
            report.append(("(boot)", str(e)))
            return report, started, False
        # 0RUN.O88, under '..' - and under any folder --put made in RUN/,
        # a listing showing folders first (16 pixels a row)
        subs = {q.split(":", 1)[0].split("/")[1] for q in put
                if q.split(":", 1)[0].startswith("RUN/")}
        box.megarun(len(subs))
        seen, last, quiet = 0, None, time.time()
        began, closed = quiet, []
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
                    box.guest_sleep(4)  # GUEST seconds to draw, then look
                    shot(os.path.join(d, last.replace(".", "_") + ".png"))
                    report.append((last, "ok"))
                elif l.startswith("MEGARUN: no "):
                    report.append((last, "refused " + l[12:]))
                elif l.startswith("kern_mega FAULT"):
                    fault = l + " | " + " ".join(
                        ls[ls.index(l) + 1:ls.index(l) + 2])
                elif l == "MEGARUN: done":
                    fault = "done"
            # ...and what a SHIPPED kern_mega says nowhere but in its state
            # (Box.faults): a fatal fault, or a package CLOSED - the LAST
            # incident's toast, so it is polled here, MEGARUN starting one
            # package every few seconds, and charged to the one last named
            for f in (box.faults() if not fault else ()):
                if f.startswith("kern_mega FAULT"):
                    fault = f
                elif not f.startswith("kern_mega CLOSED") or f in closed:
                    continue
                closed.append(f)    # (kept beside e9.out: tests/megasweep)
                with open(os.path.join(d, "faults.out"), "a") as fh:
                    fh.write("%s\t%s\n" % (last or "(before MEGARUN)", f))
            if new:
                quiet = time.time()
            if fault == "done":
                if linger:
                    # MEGATRAP=1: let what was started run on, so its
                    # loops land in the census, then a picture of it -
                    # LINGER GUEST seconds, however long the host takes
                    box.guest_sleep(linger, poll=0.5)
                    shot(os.path.join(d, "LINGER.png"))
                break
            if fault:
                shot(os.path.join(d, "FAULT.png"))
                report.append((last or "(before MEGARUN)", "FAULT " + fault))
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
            time.sleep(0.5)
        return report, started, True
    finally:
        box.stop()


def sweep(pkgs, batch, out, quiet_secs=60, sb=False, cfg=None, build=None,
          cap_secs=900, linger=0, put=(), cd=None, parallel=False):
    """PARALLEL: the batches' boots at ONCE, one X display each
    (take_display) - they share nothing but this report. What a boot never
    reached (a FAULT ends it) goes round again one boot at a time, as it
    always did."""
    os.makedirs(out, exist_ok=True)
    left = list(pkgs)
    report = []
    boot = 0
    args = (quiet_secs, sb, cfg, build, cap_secs, linger, put, cd)

    def unreached(todo, started):
        return [p for p in todo if os.path.basename(p).upper() not in started]
    if parallel and len(left) > batch:
        import threading
        batches = [left[i:i + batch] for i in range(0, len(left), batch)]
        res = [None] * len(batches)

        def run(k, disp):
            try:
                res[k] = _sweep_boot(os.path.join(out, "boot%02d" % (k + 1)),
                                     batches[k], *args, display=disp)
            except Exception as e:          # (reported, never swallowed)
                res[k] = ([("(boot %d)" % (k + 1), repr(e))], [], False)
        ths = [threading.Thread(target=run, args=(k, None if k == 0 else
                                                  take_display()))
               for k in range(len(batches))]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        boot, left = len(batches), []
        for todo, (rep, started, _) in zip(batches, res):
            report += rep
            left += unreached(todo, started)
    while left:
        boot += 1
        todo, left = left[:batch], left[batch:]
        rep, started, booted = _sweep_boot(
            os.path.join(out, "boot%02d" % boot), todo, *args)
        report += rep
        # what MEGARUN never reached goes round again
        left = unreached(todo, started) + left
        if not booted:
            break
        if not started:
            break                   # nothing moved: do not loop for ever
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
        box.wait_pm(240)
        print("running: DISPLAY=%s, E9 in %s" % (DISPLAY, box.e9))


if __name__ == "__main__":
    main()
