#!/usr/bin/env python3
"""The teardown every QEMU launcher in `tests/` owes, written ONCE.

WHY THIS FILE EXISTS.  A row that FAILS used to leave its emulator running.
`make test` daemonises QEMU and returns, so the process outlives the script
that started it, and the only thing that ever killed one was the NEXT run's
kill-stale — which reaches an instance in the same checkout with the same
pidfile, and nothing else.

Measured, and it is what put this here: two `qemu-system-i386` processes
**five hours old**, left by two `trkscrl` classification runs in two different
worktrees, still holding `build/os8088.img`.  The bill landed on `ps2mouse`,
which launches with a pidfile of its own and so had never had a chance to kill
them:

    qemu-system-i386: Failed to get "write" lock
    Is another process using the image [build/os8088.img]?

`ps2mouse` passed the moment they were killed.  **The cost of the leak is paid
by an unrelated row, hours later, wearing a message about the wrong subject** —
which is the expensive kind of failure, and a leaked emulator is
the account of it.  CLAUDE.md documents the same leak from the other end: a
previous session's instance still answering on `build/qmp.sock` and serving the
OLD kernel, which reads exactly like a change that did nothing.

`atexit` AND NOT A `finally` IN `main()`, which is what the two files that got
this right already use.  A `finally` covers what is inside its own `try`, and
these scripts raise `SystemExit` from helpers, run work at module level
(`tests/sbar.py` opens its machine inside an `if` at the top level), and die on
exceptions raised while a launcher is still being set up.  `atexit` covers all
of that, and it is one line at the launch site rather than a restructuring of
twelve `main()`s.  It does NOT cover `SIGKILL` or `os._exit`, and nothing can.
"""
import atexit
import os
import signal
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PIDFILE = os.path.join(ROOT, "build", "qemu.pid")
SOCK = os.path.join(ROOT, "build", "qmp.sock")


def kill(pidfile=PIDFILE, sock=SOCK, wait=1.0):
    """Stop the instance named by `pidfile`, and clear its files.

    **BY PIDFILE, NEVER BY `pkill -f`.**  `-f` matches the killing shell's own
    command line, so a command that names the emulator kills itself: exit 144,
    no error text, nothing dead, and every command after it silently skipped.
    That is why no file in this tree writes `qemu-system-i386` whole on a
    command line, and why this one does not either.

    Silent about everything: a pidfile naming a process that has already gone
    is the ordinary case on the success path, where the script has usually
    asked the guest to quit first.
    """
    if os.path.exists(pidfile):
        try:
            pid = int(open(pidfile).read().strip())
            os.kill(pid, signal.SIGTERM)
            gone(pid, wait)
        except Exception:                                       # noqa: BLE001
            pass
    for f in (sock, pidfile):
        if f and os.path.exists(f):
            try:
                os.unlink(f)
            except OSError:
                pass


def gone(pid, wait=1.0, most=10.0):
    """Wait for process `pid` to have EXITED - a HOST wait, on a host thing.

    It was `time.sleep(wait)` after the SIGTERM, which is a guess: the
    process that has not gone yet still holds the image's write lock, and
    the next launch then dies on `Failed to get "write" lock`. So this asks
    the process table instead, returning the moment it is gone and giving up
    at `max(wait, most)` seconds - still silently, as before.
    """
    end = time.monotonic() + max(wait, most)
    while time.monotonic() < end:
        try:
            os.kill(pid, 0)
        except OSError:
            return True
        time.sleep(0.05)
    return False


def own(pidfile=PIDFILE, sock=SOCK):
    """Kill at exit whatever this script has just LAUNCHED.

    **Call it at the launch site and nowhere else.**  A script that DRIVES an
    instance somebody else started must not kill it — `tests/xmcheck.py` takes
    a socket on its command line and does exactly that, and killing another
    session's machine is the failure this file exists to prevent rather than
    to move.

    Idempotent: registering twice kills once, because the second call finds no
    pidfile.  Safe to call before the guest is up.

    AND IT CATCHES SIGTERM, because atexit alone is not enough: Python's
    default disposition for SIGTERM is the kernel's - the process is gone at
    once and no atexit runs - so a row that tools/os88test.py stops on a
    timeout would leave its guest running exactly as a SIGKILL did.  The
    handler raises SystemExit, which is the one path that runs the teardown
    registered above.  Installed only when no handler is already there, so a
    script with its own SIGTERM policy keeps it.
    """
    atexit.register(kill, pidfile, sock)
    try:
        if signal.getsignal(signal.SIGTERM) in (signal.SIG_DFL, None):
            signal.signal(signal.SIGTERM,
                          lambda signo, frame: sys.exit(128 + signo))
    except (ValueError, OSError):
        pass                            # not the main thread: atexit alone


# -----------------------------------------------------------------------------
# THE GUEST'S CLOCK, for every QEMU row. `make test` runs QEMU WITHOUT
# `-icount`, so there is no cycle counter to budget a wait in, and a
# `time.sleep` is a different amount of the machine at every load on the box.
# What there is, is the BIOS tick count at 0040:006C: IRQ0 advances it at
# 18.2 Hz, and only by running GUEST CODE (the ROM's int 08h, which the kernel
# chains, SPEC.md 8.5) - so a tick that has moved is proof the guest ran, and
# a budget counted in ticks is one a loaded box cannot shorten. It needs no
# symbol, so it is the same address under the kernel, the DOS box and a bare
# boot.
#
# `m` is any object with `read(linear, n)` - tests/ethernet.py's `Qemu`,
# tests/telansi.py's, tests/trkscrl.py's `Qmp`: each opens QMP per command.
# -----------------------------------------------------------------------------
BIOS_TICKS = 0x46C
TICK_HZ = 18.2065
STALL = 30.0            # HOST seconds of a clock that does not move = stopped
JUMP = 256              # a step bigger than this was SET, not counted


class GuestStopped(RuntimeError):
    """The guest's tick count stopped moving: the machine is not running."""


def ticks(m):
    """The low word of the BIOS tick count."""
    b = m.read(BIOS_TICKS, 2)
    return b[0] | (b[1] << 8)


def to_ticks(secs):
    """At LEAST `secs` of guest time, whatever phase the count is in."""
    return int(secs * TICK_HZ + 0.999) + 1


class Clock(object):
    """Guest ticks elapsed since construction, and a host-side stall watch.

    The low word is summed a step at a time rather than subtracted from the
    start, because the count is not a clean clock: the BIOS SETS it from the
    RTC during POST and clears it at midnight. So a step bigger than JUMP
    counts as one tick, never as thousands - a poll that lands across POST
    must not spend a whole budget at once.
    """

    def __init__(self, m):
        self.m = m
        self.last = ticks(m)
        self.n = 0
        self.seen = time.monotonic()

    def step(self):
        t = ticks(self.m)
        d = (t - self.last) & 0xFFFF
        if d:
            self.n += 1 if d > JUMP else d
            self.last, self.seen = t, time.monotonic()
        return self.n

    def secs(self):
        """Guest seconds so far - a deadline for a loop that reads as it goes."""
        return self.step() / TICK_HZ

    def stalled(self):
        return time.monotonic() - self.seen > STALL


def acted(m, cond, secs=6.0, what=None, poll=0.05):
    """Wait for `cond()` - budgeted in `secs` of the GUEST's time.

    Answers True the moment it holds and False once the guest has run `secs`
    without it (one last look is taken at the edge). A guest whose clock does
    not move for STALL host seconds is not slow, it has STOPPED, and that
    raises GuestStopped naming `what` rather than reporting it as the
    condition failing.
    """
    c = Clock(m)
    lim = to_ticks(secs)
    while True:
        if cond():
            return True
        if c.step() >= lim:
            return bool(cond())
        if c.stalled():
            raise GuestStopped("the guest's clock stopped (%s at 0040:006C "
                               "for %ds) while waiting for %s"
                               % (hex(c.last), STALL, what or "a condition"))
        time.sleep(poll)


def pace(m, secs, poll=0.02):
    """Spend `secs` of GUEST time - the wait for a pause with nothing to wait
    on (a gesture's spacing, a program left to run, a NEGATIVE case's "and
    nothing happened"). QEMU's clock follows the host's, so on an idle box
    this is what `time.sleep(secs)` was, rounded UP to whole ticks; on a
    loaded one it is still that much of the machine. A clock that stops ends
    the pause, as a sleep would have."""
    c = Clock(m)
    lim = to_ticks(secs)
    while c.step() < lim and not c.stalled():
        time.sleep(poll)


def ui_idle(m, sym):
    """tools/os88marty.py's `ui_idle` on QEMU: ui_task ASLEEP (task 0's
    T_STATE is 2) with no event queued, no wake pending, the gfx lock free
    and the BIOS key ring empty - the UI has finished with everything it was
    given. `sym(name)` is the flat address of a kernel symbol
    (os88sym.linear). QEMU has no drive counter to watch as MartyPC does, and
    needs none: a load here runs inside ui_task's handler, which reads busy
    for as long as it lasts."""
    a = getattr(m, "_ui_idle_syms", None)
    if a is None:
        a = m._ui_idle_syms = (sym("sch_tasks"), sym("evq_count"),
                               sym("sch_uiwake"), sym("gfx_lock_flag"))
    if m.read(a[0], 1)[0] != 2:
        return False
    if m.read(a[1], 1)[0] or m.read(a[2], 1)[0] or m.read(a[3], 1)[0]:
        return False
    kb = m.read(0x41A, 4)
    return kb[0:2] == kb[2:4]


def ui_done(m, sym, cap=2.0, what=None):
    """Until `ui_idle` holds across two consecutive guest ticks, CAPPED at
    `cap` guest seconds - the fixed pause a caller used to spend, so no wait
    is ever longer than it was. Answers True if the UI went idle, False if
    the cap ran out first. A kernel this process cannot resolve takes the
    old pause."""
    try:
        ui_idle(m, sym)
    except (GuestStopped, OSError):
        raise
    except Exception:
        pace(m, cap)
        return False
    c = Clock(m)
    lim = to_ticks(cap)
    run, last = 0, None
    while True:
        n = c.step()
        if ui_idle(m, sym):
            if n != last:
                run += 1
                last = n
            if run >= 2:
                return True
        else:
            run, last = 0, None
        if n >= lim:
            return False
        if c.stalled():
            return False
        time.sleep(0.02)


def quiesce(m, read, secs=1.0, stable=2, limit=60.0, what=None, poll=0.1):
    """Wait until `read()` answers the same thing `stable` times running,
    `secs` of GUEST time apart - "until these bytes stop changing" (a block
    table, a listing, a counter). Answers the settled value; a value still
    moving after `limit` guest seconds answers the last one read, and the
    caller's own assertion then says what was wrong with it."""
    c = Clock(m)
    lim = to_ticks(limit)
    gap = max(1, int(secs * TICK_HZ + 0.5))
    last, same, mark = read(), 1, c.step()
    while same < stable:
        n = c.step()
        if n >= lim:
            break
        if c.stalled():
            raise GuestStopped("the guest's clock stopped while waiting for "
                               "%s to settle" % (what or "a value"))
        if n - mark >= gap:
            v = read()
            same = same + 1 if v == last else 1
            last, mark = v, n
        else:
            time.sleep(poll)
    return last


# =============================================================================
# THE ISOLATED INSTANCE: os88marty.launch's shape, on QEMU
# =============================================================================
#
# WHY THIS EXISTS. Every QEMU launcher above this line goes through `make
# test`, which pins ONE socket (build/qmp.sock), ONE pidfile (build/qemu.pid)
# and boots build/os8088.img itself - and QEMU takes a WRITE LOCK on every
# image it opens. So two QEMU rows cannot run at once: the second either
# drives the first one's machine through the shared socket or dies on
# `Failed to get "write" lock`. MartyPC rows stopped having that problem when
# os88marty.launch gave each instance its own port, directory and disks
# (docs/MARTYPC-DEBUG.md); this is the same move on QEMU:
#
#   * a private run directory under /tmp - SHORT on purpose, because an
#     AF_UNIX path is limited to ~104 bytes and a worktree's build/ or the
#     scratchpad can be longer than that by itself;
#   * its own QMP socket and pidfile in it;
#   * PRIVATE COPIES of the floppies, so there is no lock to collide on and
#     the guest never writes build/;
#   * QEMU run as a CHILD rather than -daemonize'd, so a row's CPU includes
#     the emulator's (tools/os88test.py charges the tier in CPU) and a
#     timed-out row's teardown is the child it owns - atexit plus a SIGTERM
#     handler, by pid, never by `pkill -f`.
#
# THE API IS os88marty's, so a port is mechanical. `Qemu` subclasses
# os88marty.Marty for the helpers that are pure Python over `key`/`read`
# (`type_text`, `ctrl`, `alt`, `readseg`, `sym`) and replaces every verb that
# was a debug-server request. `os88marty.settle`, `until`, `quiesce`,
# `ui_done`, `no_saver`, os88mouse.Mouse(marty=m), tools/os88ui.py and
# tests/dispcp.py all take it unchanged. A verb QEMU has no answer for -
# breakpoints, single-step, `flicker`, a snapshot - raises MartyError naming
# itself rather than answering something plausible.
#
# THE GUEST CLOCK. MartyPC counts 8088 cycles; QEMU without -icount has no
# counter at all. What it has is the BIOS tick at 0040:006C, which only GUEST
# code advances (the ROM's int 08h, chained by the kernel - SPEC.md 8.5). So
# `status()["cycles"]` is SYNTHESISED from ticks: QEMU_RATE harness-seconds a
# QEMU second, at os88marty.GUEST_HZ. QEMU_RATE is os88marty.GUEST_PACE - the
# 4.5 guest seconds a host second buys on an idle MartyPC - so every pause and
# budget in the harness costs the same HOST time here as on an idle MartyPC,
# while a QEMU second is far MORE machine than 4.5 seconds of an 8088. Waits
# keep their meaning in the safe direction. The resolution is one tick, 55 ms.
#
# MEMORY IS A FILE THIS PROCESS MAPS. Guest RAM is a memory-backend-file in the
# run directory, so `read` and `write` below 0xA0000 are slices of an mmap -
# no QMP round trip, no temp file - which matters because `settle`, `ui_idle`
# and the mouse driver poll guest bytes continuously. Video RAM and the ROMs
# are not RAM, and go through QMP's `pmemsave`.
#
# THE MACHINE IS `-m 1M` BY DEFAULT: 640KB conventional and NOTHING ABOVE 1MB,
# which is the memory shape of every MartyPC machine this replaces. QEMU's own
# default (128MB) makes kernel/xmem.inc's xm_sniff load XMEM.DRV at boot - a
# heap claim no 8088 makes - and turns every OP_XMS part and the RAM disk's
# Store radio onto a path no MartyPC row takes. `mem="128M"` gives it back.
# What `-m 1M` does NOT change is the CPU: OSAPI_CPU_INFO still answers
# CPU_386 under SeaBIOS, and a row has to be judged against that itself
# (docs/TESTING.md).
# -----------------------------------------------------------------------------
import json as _json
import mmap as _mmap
import shutil as _shutil
import socket as _socket
import subprocess as _subprocess
import tempfile as _tempfile
import threading as _threading

sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build as _build                                     # noqa: E402
import os88marty as _marty                                     # noqa: E402
from os88marty import MartyError                               # noqa: E402

QEMU_BIN = os.environ.get("OS88_QEMU", "qemu-system-i386")
QEMU_RATE = _marty.GUEST_PACE if _marty.GUEST_PACE > 0 else 4.5
RUN_ROOT = "/tmp"                       # SHORT: see the banner
RUN_PREFIX = "o88q-"
LOW_RAM = 0xA0000                       # where conventional RAM ends

# W3C KeyboardEvent.code (MartyKey's vocabulary, which every `m.key` caller
# in tests/ already speaks) -> QEMU's QKeyCode. Letters and digits are derived.
QCODE = {
    "Enter": "ret", "Space": "spc", "Tab": "tab", "Backspace": "backspace",
    "Escape": "esc", "ShiftLeft": "shift", "ShiftRight": "shift_r",
    "ControlLeft": "ctrl", "ControlRight": "ctrl_r", "AltLeft": "alt",
    "AltRight": "alt_r", "ArrowUp": "up", "ArrowDown": "down",
    "ArrowLeft": "left", "ArrowRight": "right", "Home": "home", "End": "end",
    "PageUp": "pgup", "PageDown": "pgdn", "Insert": "insert",
    "Delete": "delete", "CapsLock": "caps_lock", "NumLock": "num_lock",
    "ScrollLock": "scroll_lock", "Minus": "minus", "Equal": "equal",
    "BracketLeft": "bracket_left", "BracketRight": "bracket_right",
    "Backslash": "backslash", "Semicolon": "semicolon", "Quote": "apostrophe",
    "Backquote": "grave_accent", "Comma": "comma", "Period": "dot",
    "Slash": "slash",
}


def qcode(name):
    """The QKeyCode for one MartyKey name, or a MartyError naming it."""
    if name in QCODE:
        return QCODE[name]
    if name.startswith("Key") and len(name) == 4 and name[3].isalpha():
        return name[3].lower()
    if name.startswith("Digit") and len(name) == 6 and name[5].isdigit():
        return name[5]
    if name.startswith("F") and name[1:].isdigit() and 1 <= int(name[1:]) <= 12:
        return name.lower()
    raise MartyError("no QEMU key code for %r" % name)


def _alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def _is_qemu(pid):
    """Is `pid` a QEMU? Asked of `ps`, because a recycled pid is a stranger."""
    try:
        out = _subprocess.run(["ps", "-o", "comm=", "-p", str(pid)],
                              capture_output=True, text=True).stdout
    except OSError:
        return False
    return "qemu-system" in out


def reap(verbose=False):
    """Kill every isolated QEMU whose OWNER is gone, and drop its directory.

    os88marty.reap's rule: an orphan is an instance whose owning process has
    exited, and a live owner's machine is never touched - so two worktrees,
    two agents or two rows on this box cannot reap each other. The owner and
    the emulator are each a pid in the instance's directory; a pid that is
    alive but no longer a QEMU has been recycled and is left alone. This is
    what catches the one case atexit cannot: a row killed outright.
    """
    n = 0
    try:
        names = os.listdir(RUN_ROOT)
    except OSError:
        return 0
    for d in names:
        if not d.startswith(RUN_PREFIX):
            continue
        home = os.path.join(RUN_ROOT, d)
        try:
            owner = int(open(os.path.join(home, "owner")).read().strip())
        except (OSError, ValueError):
            continue
        if _alive(owner):
            continue
        try:
            pid = int(open(os.path.join(home, "qemu.pid")).read().strip())
        except (OSError, ValueError):
            pid = None
        if pid and _alive(pid) and _is_qemu(pid):
            try:
                os.kill(pid, signal.SIGTERM)
                gone(pid, 1.0, 5.0)
            except OSError:
                pass
            n += 1
            if verbose:
                print("os88qemu: reaped orphan pid %d (%s)" % (pid, home))
        _shutil.rmtree(home, ignore_errors=True)
    return n


class Qemu(_marty.Marty):
    """One isolated QEMU, driven through os88marty's surface. `launch` makes it."""

    def __init__(self, proc, home, sock, mem_path=None, label=None):
        # Marty.__init__ dials a debug server; there is none, so the fields
        # its pure-Python helpers read are set here instead.
        self._lock = _threading.RLock()
        self._log = []
        self._go = None
        self._pumping = 0
        self._proc = proc
        self._rec = None
        self.pid = proc.pid
        self.run_dir = home
        self.addr = sock                # what to name in a message
        self.port = None
        self.label = label
        self._state = "running"
        self._ticks = 0                 # BIOS ticks summed, see _cycles
        self._last = None
        self._btn = (False, False)
        self._pend = []                 # input sent while paused, see _input
        self._disk0 = (0, 0)
        self._troff = self._rd = self._wr = 0   # trace.log, see disk()
        self._closed = False
        self._mm = None
        self.s = self.f = None
        for _ in range(400):            # the socket appears once QEMU has
            if proc.poll() is not None:  # parsed its command line
                raise MartyError("QEMU exited at once (rc=%s); its log: %s"
                                 % (proc.returncode,
                                    os.path.join(home, "qemu.log")))
            try:
                s = _socket.socket(_socket.AF_UNIX, _socket.SOCK_STREAM)
                s.connect(sock)
                break
            except OSError:
                s.close()
                time.sleep(0.05)
        else:
            raise MartyError("no QMP socket at %s after 20s" % sock)
        s.settimeout(60.0)
        self.s, self.f = s, s.makefile("rw")
        _json.loads(self.f.readline())              # the greeting
        self.qmp("qmp_capabilities")
        if mem_path:
            # `launch` sized the file before QEMU started, and QEMU maps it
            # MAP_SHARED (share=on), so this mapping and the guest's are the
            # same pages
            fd = os.open(mem_path, os.O_RDWR)
            try:
                self._mm = _mmap.mmap(fd, 0)
            finally:
                os.close(fd)

    # --- transport ----------------------------------------------------------
    def qmp(self, execute, **args):
        """One QMP command on the instance's own socket. It is ours alone, so
        the connection is held for the life of the instance (QMP serves one
        client; nothing else on this box knows the path)."""
        msg = {"execute": execute}
        if args:
            msg["arguments"] = args
        try:
            with self._lock:
                self.f.write(_json.dumps(msg) + "\n")
                self.f.flush()
                while True:
                    line = self.f.readline()
                    if not line:
                        raise MartyError(self._gone("QMP closed"))
                    r = _json.loads(line)
                    if "event" not in r:
                        break
        except OSError as e:
            raise MartyError(self._gone("%s: %s" % (type(e).__name__, e)))
        if "error" in r:
            raise MartyError("QMP %s: %s" % (execute, r["error"].get("desc")))
        return r.get("return")

    def hmp(self, line):
        return self.qmp("human-monitor-command", **{"command-line": line})

    def _gone(self, how):
        rc = self._proc.poll() if self._proc is not None else None
        return ("the QEMU at %s is gone (%s)%s; its log: %s"
                % (self.addr, how,
                   "" if rc is None else ", exited rc=%s" % rc,
                   os.path.join(self.run_dir, "qemu.log")))

    def cmd(self, **kw):
        """Every MartyPC debug-server verb this class does not replace."""
        raise MartyError("QEMU has no `%s`: that is a MartyPC debug-server "
                         "verb (breakpoints, stepping, flicker, snapshots, "
                         "cycle-exact positioning). Keep a row that needs one "
                         "on MartyPC." % kw.get("cmd"))

    def close(self):
        if self._closed:
            return
        self._closed = True
        proc, self._proc = self._proc, None
        if proc is not None and proc.poll() is None:
            try:
                self.qmp("quit")
            except Exception:                               # noqa: BLE001
                pass
            try:
                proc.wait(timeout=5)
            except Exception:                               # noqa: BLE001
                try:
                    proc.terminate()
                    proc.wait(timeout=5)
                except Exception:                           # noqa: BLE001
                    proc.kill()
                    proc.wait()
        for f in (self.f, self.s, self._mm):
            try:
                if f is not None:
                    f.close()
            except (OSError, ValueError, BufferError):
                pass
        if not os.environ.get("OS88_QEMU_KEEP"):
            _shutil.rmtree(self.run_dir, ignore_errors=True)

    # --- the clock ----------------------------------------------------------
    def _cycles(self):
        """Synthetic guest cycles: BIOS ticks summed a step at a time (Clock's
        rule - a step over JUMP was SET by POST or midnight and counts one),
        at QEMU_RATE harness-seconds a tick-second."""
        b = self.read(BIOS_TICKS, 2)
        t = b[0] | (b[1] << 8)
        if self._last is not None:
            d = (t - self._last) & 0xFFFF
            self._ticks += 1 if d > JUMP else d
        self._last = t
        return int(self._ticks * _marty.GUEST_HZ * QEMU_RATE / TICK_HZ)

    def status(self):
        return {"state": self._state, "cycles": self._cycles(),
                "cs": 0, "ip": 0}

    def run(self):
        self.qmp("cont")
        self._state = "running"
        if self._pend:                  # what was sent while it was paused
            ev, self._pend = self._pend, []
            self.qmp("input-send-event", events=ev)
        return self.status()

    def go(self, limit=4.0):
        self.run()
        return None

    def pause(self):
        self.qmp("stop")
        self._state = "paused"
        return self.status()

    def advance(self, frames=None, cycles=None, card=None):
        """Run for `frames` (60 Hz) or `cycles` of harness time, then stop."""
        secs = (frames or 0) / 60.0 + (cycles or 0) / _marty.GUEST_HZ
        self.run()
        c0 = self._cycles()
        while (self._cycles() - c0) / _marty.GUEST_HZ < secs:
            time.sleep(0.005)
        return self.pause()

    # --- memory -------------------------------------------------------------
    def read(self, addr, length):
        out = bytearray()
        while length:
            if self._mm is not None and addr < LOW_RAM:
                n = min(length, LOW_RAM - addr)
                out += self._mm[addr:addr + n]
            else:
                n = min(length, 1 << 16)
                p = os.path.join(self.run_dir, "mem.bin")
                self.qmp("pmemsave", val=addr, size=n, filename=p)
                with open(p, "rb") as f:
                    out += f.read()
            addr += n
            length -= n
        return bytes(out)

    def write(self, addr, data):
        if self._mm is None or addr + len(data) > LOW_RAM:
            raise MartyError("QEMU writes only conventional RAM here "
                             "(0x%X+%d asked)" % (addr, len(data)))
        self._mm[addr:addr + len(data)] = bytes(data)
        return len(data)

    # --- the screen ---------------------------------------------------------
    def video(self, card=None):
        """Always a VGA: QEMU emulates no CGA and no Hercules (Makefile)."""
        return {"type": "vga", "idx": 0, "primary": True}

    def cards(self):
        return [self.video()]

    def fbuf(self, aperture=0, card=None):
        """The rendered display as (w, h, rgb24), through QMP's screendump."""
        p = os.path.join(self.run_dir, "screen.ppm")
        self.qmp("screendump", filename=p)
        data = open(p, "rb").read()
        fields, pos = [], 0
        while len(fields) < 4:
            while data[pos:pos + 1].isspace():
                pos += 1
            if data[pos:pos + 1] == b"#":
                while data[pos:pos + 1] not in (b"\n", b""):
                    pos += 1
                continue
            start = pos
            while pos < len(data) and not data[pos:pos + 1].isspace():
                pos += 1
            fields.append(data[start:pos])
        w, h = int(fields[1]), int(fields[2])
        return w, h, data[pos + 1:pos + 1 + w * h * 3]

    def vram(self, kind=None):
        raise MartyError("vram() reads a 1bpp CGA/Hercules framebuffer, and "
                         "QEMU has only a VGA - use fbuf()")

    def screen(self, card=None):
        raise MartyError("QEMU has no text-row reader here; read guest "
                         "memory or fbuf()")

    # --- input --------------------------------------------------------------
    def _input(self, ev):
        """Deliver input events - or HOLD them while the guest is paused.

        QEMU DROPS input sent to a stopped VM (ui/input.c returns early unless
        the run state is running), and os88mouse's `dblclick` pauses the guest
        on purpose and steps it with `advance`, sending each packet while it is
        stopped. Held here, they go in at the next `run` - the moment the
        guest can see them, which is when MartyPC's UART would clock them in.
        """
        if not ev:
            return
        if self._state != "running":
            self._pend += ev
        else:
            self.qmp("input-send-event", events=ev)

    def key(self, name, down=True, up=True):
        k = {"type": "qcode", "data": qcode(name)}
        ev = []
        if down:
            ev.append({"type": "key", "data": {"down": True, "key": k}})
        if up:
            ev.append({"type": "key", "data": {"down": False, "key": k}})
        self._input(ev)
        self._log.append({"cycles": 0, "kind": "key",
                          "args": {"key": name, "down": down, "up": up}})
        return {}

    def mouse(self, dx=0, dy=0, l=False, r=False):
        """One relative event into QEMU's msmouse - the serial mouse on COM1,
        so the guest's UART and mou_isr decode a real Microsoft packet."""
        dx = max(-127, min(127, int(dx)))
        dy = max(-127, min(127, int(dy)))
        ev = []
        if dx:
            ev.append({"type": "rel", "data": {"axis": "x", "value": dx}})
        if dy:
            ev.append({"type": "rel", "data": {"axis": "y", "value": dy}})
        for i, (btn, want) in enumerate((("left", bool(l)),
                                         ("right", bool(r)))):
            if want != self._btn[i]:
                ev.append({"type": "btn",
                           "data": {"down": want, "button": btn}})
        if not ev:
            # A RE-SEND: os88mouse repeats a packet whose edge it has not seen
            # decoded, and a Microsoft packet carries the button's LEVEL, so
            # restating the left button's level makes the same packet again.
            ev.append({"type": "btn",
                       "data": {"down": bool(l), "button": "left"}})
        self._btn = (bool(l), bool(r))
        self._input(ev)
        self._log.append({"cycles": 0, "kind": "mouse",
                          "args": {"dx": dx, "dy": dy, "l": l, "r": r}})
        return {}

    # --- floppies -----------------------------------------------------------
    def disk(self, reset=False):
        """Floppy traffic from OUTSIDE - the 'did the drive move' question
        os88marty's ui_done and settle ask of MartyPC's FDC counters.

        NOT query-blockstats: QEMU's floppy controller does no block
        accounting, so those read 0 after a whole boot. `launch` turns on the
        block layer's own `blk_co_preadv`/`blk_co_pwritev` trace events into
        the instance's trace.log instead, and this counts the lines - one a
        SECTOR, since the FDC moves 512 bytes a call, where MartyPC counts
        commands. Every caller compares two readings for equality or asks
        whether one moved, which both units answer the same."""
        p = os.path.join(self.run_dir, "trace.log")
        try:
            with open(p, "rb") as f:
                f.seek(self._troff)
                chunk = f.read()
        except OSError:
            chunk = b""
        cut = chunk.rfind(b"\n") + 1           # whole lines only
        self._troff += cut
        for line in chunk[:cut].splitlines():
            if line.startswith(b"blk_co_preadv "):
                self._rd += 1
            elif line.startswith(b"blk_co_pwritev "):
                self._wr += 1
        if reset:
            self._disk0 = (self._rd, self._wr)
        return {"reads": self._rd - self._disk0[0],
                "writes": self._wr - self._disk0[1]}

    def disks(self):
        return [{"drive": i, "present": os.path.exists(self._fd(i)),
                 "path": self._fd(i)} for i in (0, 1)]

    def _fd(self, drive):
        return os.path.join(self.run_dir, "fd%d.img" % drive)

    def flush(self, drive=0, path=None, fmt="raw"):
        """The drive's image as the guest left it, copied to `path`.

        QEMU writes a raw floppy through to its file, so the private copy IS
        the live volume; the guest is paused around the copy so it cannot be
        caught half way through a multi-sector commit (os88flush's rule)."""
        if fmt != "raw":
            raise MartyError("QEMU flushes raw images only")
        src = self._fd(drive)
        if path is None or os.path.abspath(path) == src:
            return {"path": src}
        was = self._state
        if was == "running":
            self.pause()
        try:
            _shutil.copyfile(src, path)
        finally:
            if was == "running":
                self.run()
        return {"path": path}

    def mount(self, drive, path):
        raise MartyError("QEMU media change is not wired here")

    def quit(self):
        self.close()


def launch(image, apps=None, mem=None, boot=True, extra=(), label=None,
           card=None):
    """Start a FRESH, PRIVATE QEMU on `image` (A:) and `apps` (B:).

        with os88qemu.launch("build/os8088.img",
                             apps="build/apps.img") as m:
            os88marty.settle(m)          # os88marty's verbs take it as is

    Mirrors os88marty.launch: the images go through os88build.at (a frozen
    soak tree), each is COPIED into the instance's own directory, and the
    returned object owns the process - the `with` block, atexit and SIGTERM
    all end it. `boot=True` settles on the desktop (os88marty.desktop_up),
    a number runs blind for that many harness seconds, and 0 returns at once.
    `mem` is QEMU's -m; see the banner for why 1M. $OS88_QEMU_MEM overrides
    the default and not an explicit `mem` - it is the A/B for a row's claim
    that nothing above 1MB is what keeps it on the 8088's path. `extra` is
    appended to the command line - a device a row needs and nothing else does.
    """
    if mem is None:
        mem = os.environ.get("OS88_QEMU_MEM", "1M")
    reap()
    if label is None:
        label = os.path.basename(getattr(sys.modules.get("__main__"),
                                         "__file__", "") or "os88qemu")
    home = _tempfile.mkdtemp(prefix=RUN_PREFIX, dir=RUN_ROOT)
    with open(os.path.join(home, "owner"), "w") as f:
        f.write("%d\n" % os.getpid())
    with open(os.path.join(home, "label"), "w") as f:
        f.write(label + "\n")
    cmd = [QEMU_BIN]
    for i, src in enumerate((image, apps)):
        if src is None:
            continue
        dst = os.path.join(home, "fd%d.img" % i)
        _shutil.copyfile(_build.at(src), dst)
        cmd += ["-drive", "file=%s,format=raw,if=floppy,index=%d" % (dst, i)]
    mem_path = None
    mach = "pc,vmport=off"
    if mem:
        mem_path = os.path.join(home, "ram")
        units = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}
        size = int(mem[:-1]) * units[mem[-1].upper()] \
            if mem[-1].upper() in units else int(mem) << 20
        with open(mem_path, "wb") as f:     # sparse, and exactly -m's size
            f.truncate(size)
        cmd += ["-m", mem, "-object",
                "memory-backend-file,id=ram0,size=%s,mem-path=%s,share=on"
                % (mem, mem_path)]
        mach += ",memory-backend=ram0"
    sock = os.path.join(home, "qmp.sock")
    pidfile = os.path.join(home, "qemu.pid")
    # The serial mouse on COM1 and vmport off: the Makefile's MOUSE and
    # QEMUMACH defaults, for the reason it gives - two pointers split the
    # buttons and stick [mouse_btn].
    cmd += ["-machine", mach, "-boot", "a",
            "-chardev", "msmouse,id=m0", "-serial", "chardev:m0",
            "-display", "none", "-audiodev", "none,id=snd",
            "-trace", "enable=blk_co_preadv", "-trace", "enable=blk_co_pwritev",
            "-D", os.path.join(home, "trace.log"),
            "-qmp", "unix:%s,server=on,wait=off" % sock,
            "-pidfile", pidfile] + list(extra)
    log = open(os.path.join(home, "qemu.log"), "wb")
    proc = _subprocess.Popen(cmd, cwd=home, stdout=log, stderr=log,
                             stdin=_subprocess.DEVNULL)
    log.close()
    try:
        m = Qemu(proc, home, sock, mem_path=mem_path, label=label)
    except BaseException:
        try:
            proc.kill()
            proc.wait(timeout=5)
        except Exception:                                   # noqa: BLE001
            pass
        _shutil.rmtree(home, ignore_errors=True)
        raise
    # THE TEARDOWN IS REGISTERED HERE, at the launch, for own()'s reasons -
    # and own()'s SIGTERM handler too, because the runner's timeout is a
    # SIGTERM and Python's default for it runs no atexit.
    atexit.register(m.close)
    try:
        if signal.getsignal(signal.SIGTERM) in (signal.SIG_DFL, None):
            signal.signal(signal.SIGTERM,
                          lambda signo, frame: sys.exit(128 + signo))
    except (ValueError, OSError):
        pass
    if boot:
        try:
            if boot is True:
                _marty.settle(m, gate=_marty.desktop_up)
            else:
                _marty.pace(m, boot)
        except BaseException:
            m.close()
            raise
    return m


def boot(image, apps=None, **kw):
    """tools/os88ui.py's `boot` on an isolated QEMU: launch, wait for the
    desktop, turn the saver off, hand back a UI - `with os88qemu.boot(...)
    as ui:`. Every keyword but `machine`/`why_ibm` (MartyPC's) is os88ui's or
    `launch`'s."""
    import os88ui
    kw.pop("machine", None)
    kw.pop("why_ibm", None)
    return os88ui.boot(image, apps=apps, launcher=launch, **kw)
