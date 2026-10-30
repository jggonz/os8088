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


# -----------------------------------------------------------------------------
# EVERY INSTANCE IS PRIVATE (docs/plans/SOAK-PARALLEL.md 17). The default
# socket and pidfile used to be build/qmp.sock and build/qemu.pid - ONE pair
# for the checkout - so two QEMU rows could not run at once: the second
# `make test` refused its pidfile, or worse, drove the first one's machine.
# That fixed pair is what kept every QEMU row in the one-at-a-time lane of a
# soak while MartyPC's rows ran four wide, and MartyPC had already solved the
# same problem (os88marty.launch: a port, a directory and disks per instance).
#
# So the defaults are THIS PROCESS'S now: a directory of its own, under
# $OS88_QEMU_DIR when tools/os88test.py hands the row one (so the runner can
# sweep exactly that row's emulators after a timeout, and nobody else's), or
# under /tmp standalone. It is short on purpose - a Unix socket path is
# capped at 108 bytes and a worktree's build/ is not short.
# $QMP_SOCK still wins, for a person driving an instance by hand.
# -----------------------------------------------------------------------------
def _rundir():
    import tempfile
    base = os.environ.get("OS88_QEMU_DIR")
    if base:
        d = os.path.join(base, str(os.getpid()))
        os.makedirs(d, exist_ok=True)
        return d
    # /tmp BY NAME and not $TMPDIR: tools/os88test.py points a row's TMPDIR
    # at build/tmp/, and a worktree's build/tmp/ already spends most of a
    # socket path's 108 bytes before the file name
    return tempfile.mkdtemp(prefix="os88q-", dir="/tmp")


RUNDIR = _rundir()
PIDFILE = os.path.join(RUNDIR, "qemu.pid")
SOCK = os.environ.get("QMP_SOCK", os.path.join(RUNDIR, "qmp.sock"))


def _tidy():
    """Remove RUNDIR at exit, once the instances in it are dead (atexit is
    LIFO and this is registered at import, before any launch's teardown)."""
    import shutil
    shutil.rmtree(RUNDIR, ignore_errors=True)


atexit.register(_tidy)
_SEQ = [0]


def paths(tag="qemu"):
    """A FRESH (socket, pidfile) pair inside this process's RUNDIR - for a
    script that launches more than one machine, or builds its own command
    line. Never the same pair twice, so a second launch cannot meet the
    first one's stale socket."""
    _SEQ[0] += 1
    stem = os.path.join(RUNDIR, "%s%d" % (tag, _SEQ[0]))
    return stem + ".sock", stem + ".pid"


def workdir(tag="w"):
    """A fresh directory inside RUNDIR for one instance's disk copies."""
    import tempfile
    return tempfile.mkdtemp(prefix=tag, dir=RUNDIR)


def kill(pidfile=None, sock=None, wait=1.0):
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
    if pidfile is None:
        pidfile, sock = PIDFILE, (SOCK if sock is None else sock)
    if SERVED:                          # a kern_mega Bochs or an 86Box:
        import os88qmpmega             # closed by its server, which puts
                                        # the disks back
        for srv in list(os88qmpmega._LIVE):
            if srv.pidfile == pidfile and not srv.closed:
                srv.close()
    if pidfile and os.path.exists(pidfile):
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
        # A ZOMBIE HAS EXITED: its files (the image's write lock) are closed,
        # and a daemonised QEMU's parent is PID 1, which here reaps ~1 s
        # later - measured 12 ms to exit, 1.0 s to vanish, every launch
        if _zombie(pid):
            return True
        time.sleep(0.01)
    return False


def _zombie(pid):
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().rsplit(")", 1)[1].split()[0] == "Z"
    except (OSError, IndexError):
        return False


class QemuRefused(RuntimeError):
    """QEMU exited before it ran - its own words, and who holds the file."""


def _holders(text):
    """Every OTHER live qemu-system process whose command line names a file
    QEMU's lock message names: `pid N: <its -drive words>`."""
    import re
    paths = set(re.findall(r"\[([^\]]+)\]", text))
    out = []
    for d in os.listdir("/proc"):
        if not d.isdigit() or int(d) == os.getpid():
            continue
        try:
            argv = open("/proc/%s/cmdline" % d, "rb").read().split(b"\0")
        except OSError:
            continue
        argv = [x.decode("latin-1") for x in argv if x]
        if not argv or "qemu-system" not in os.path.basename(argv[0]):
            continue
        hit = [x for x in argv for p in paths
               if p in x or os.path.basename(p) in x]
        if hit:                         # (its pidfile names the row's dir)
            pf = argv[argv.index("-pidfile") + 1] if "-pidfile" in argv[:-1] \
                else "?"
            out.append("pid %s (pidfile %s): %s"
                       % (d, pf, " ".join(sorted(set(hit)))))
    return out


def start(cmd, **kw):
    """Launch QEMU (`subprocess.run(cmd, **kw)`, a list or a shell string)
    and, when it exits non-zero, RAISE ITS OWN WORDS - QemuRefused with
    QEMU's stderr and, for an image lock, every other QEMU naming that file.

    A launch that died ~0.3 s in used to reach a row as `exit 1` and a
    CalledProcessError: `wheelps2` and `vmmouse` in a four-lane pass, booting
    build/os8088.img snapshot=on while another row's QEMU held it WRITABLE.
    QEMU refuses even a read-only open then - `Failed to get shared "write"
    lock` - and a row that cannot say whose lock it was is a row somebody
    re-runs alone, watches pass, and files as contention
    (docs/plans/SOAK-PARALLEL.md 17.10)."""
    import subprocess
    kw.setdefault("cwd", ROOT)
    kw.pop("check", None)
    kw["stderr"] = subprocess.PIPE
    r = subprocess.run(cmd, **kw)
    if r.returncode:
        err = r.stderr.decode("latin-1", "replace") if isinstance(
            r.stderr, bytes) else (r.stderr or "")
        msg = "QEMU exited %d before it ran: %s" % (
            r.returncode, err.strip() or "(nothing on stderr)")
        if "lock" in err:
            who = _holders(err)
            msg += ("\n  held by: " + "; ".join(who)) if who else \
                "\n  (no other QEMU names the file now - it has since exited)"
        sys.stderr.write(msg + "\n")
        raise QemuRefused(msg)
    if r.stderr:                        # (a warning: still the row's to see)
        sys.stderr.write(r.stderr.decode("latin-1", "replace")
                         if isinstance(r.stderr, bytes) else r.stderr)
    return r


def own(pidfile=None, sock=None):
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
    if pidfile is None:
        pidfile, sock = PIDFILE, (SOCK if sock is None else sock)
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
# chains, SPEC.md 8.5) - so a tick that has moved is proof the guest ran.
# It needs no symbol, so it is the same address under the kernel, DOS and a
# bare boot.
#
# **BUT A TICK IS NOT A FIXED AMOUNT OF WORK HERE, and that is the
# difference from MartyPC** (docs/plans/SOAK-PARALLEL.md 17.5). Without
# `-icount` QEMU's PIT follows the HOST's clock, so a guest the host starves
# runs fewer instructions per tick: a tick budget cannot be SHORTENED by load
# the way a `time.sleep` is, but it buys less of the machine. So on QEMU a
# budget is a CEILING on a wait for STATE (it ends the moment the state
# arrives, and wants generous room), never a pause that something else is
# assumed to have finished inside - that shape failed five rows at eight
# emulators on four cores. And a row whose ANSWER is the guest's speed
# against this clock was a host rate, and ran alone - until `icount()`
# below, which takes that one class of row off the host's clock.
#
# `m` is any object with `read(linear, n)` - tests/ethernet.py's `Qemu`,
# tests/telansi.py's, tests/trkscrl.py's `Qmp`: each opens QMP per command.
# -----------------------------------------------------------------------------
BIOS_TICKS = 0x46C
TICK_HZ = 18.2065
STALL = 30.0            # HOST seconds of a clock that does not move = stopped
JUMP = 256              # a step bigger than this was SET, not counted



# -----------------------------------------------------------------------------
# A FIXED MACHINE, for a row whose answer is the guest's SPEED against its own
# clock (docs/plans/SOAK-PARALLEL.md 17.8). `-icount shift=N` makes every
# guest instruction cost 2^N ns of VIRTUAL time, and QEMU's PIT, the BIOS
# tick and the wav audiodev all run on that virtual clock - so a bench timed
# on the 8253 counts INSTRUCTIONS, and a host that starves the emulator
# stretches the wall time and moves no answer. **The SB16's DMA does NOT, on
# a stock QEMU**: the 8237 moves its transfers from an idle bottom half on
# the main loop, in HOST time, while the vCPU runs on without the BQL - so a
# main loop starved 300 ms of host time (that thread alone) stopped the card,
# 'The sound card stopped' on 8.2 and 9.2 alike, which is what a loaded soak
# did to xtmp3rate. tools/qemu9/i8257-icount.patch puts the channels on a
# virtual-clock timer under -icount; a row whose card is the clock needs
# `qemuvt` (os88build.qemuvt()). The disk is the residual: IDE's aio
# completes on the main loop too, so a read's latency is still the host's. `align=off`
# lets it run as fast as the host can rather than pacing virtual time to
# real; `sleep=on` keeps an IDLE guest (hlt) on real time, which is what a
# disk transfer or a throttled drive waiting on the host needs - with
# `sleep=off` virtual time warps to the next timer while the host is still
# doing the I/O, and a BIOS timeout can fire on a transfer that is merely
# slow.
#
# THE SHIFT IS A ROW'S TRADE, and 4 (62.5 MIPS of guest) is the default
# because xtmp3eng's ladder needs it: XTMP3's stub calibrates a configured
# 1.00x at 0.98 and 4.00x at 3.72 there - the no-icount figures were 0.98
# and 3.91 - where shift 6 read 0.93 and 3.08 and took slowdown's 1.05x
# under 1.0, an underrun the leg forbids. Shift 4 costs xtmp3rate's seven
# legs 295 s against 198 without icount. A guest that is I/O-DENSE pays
# more: icount ends a TB at every I/O instruction, and vidbank's speaker
# play (a PIT write a sample) ran ~20 host seconds a guest second at shift
# 4, so that row asks for 6 (15.6 MIPS), where its verdicts still have
# margins of 26x and 630x. Pick the largest shift whose margins hold.
#
# What it CHANGES, and why it is only for these rows: the cost model. TCG
# with icount prices an instruction at 2^N ns whatever it does, so MMIO -
# a VGA write, an I/O port - costs what a register move does, where without
# it QEMU's device emulation is most of the host time (vidbank's key-1
# bench reads 3.9 ms without icount and 0.12 ms at shift 4: the decode
# writes A000). A row that is ABOUT host behaviour, or whose throttle
# (`throttling.bps-total`, real time) is the rate being compared, wants it
# off. OS88_QEMU_ICOUNT=0 is the A/B (no -icount), and =N forces a shift.
# -----------------------------------------------------------------------------
ICOUNT_SHIFT = 4


def icount(shift=ICOUNT_SHIFT):
    """QEMU arguments for a FIXED-SPEED guest (above), or none when
    $OS88_QEMU_ICOUNT is 0; a number there is the shift."""
    v = os.environ.get("OS88_QEMU_ICOUNT", "").strip()
    if v == "0":
        return []
    if v:
        shift = int(v)
    return ["-icount", "shift=%d,align=off,sleep=on" % shift]


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


def key(m, qcode, secs=10.0):
    """ONE KEY, as a keyboard sends it: the press, and the release only once
    the ROM has STORED the press - the BIOS buffer's tail (0040:001C) moved.

    HMP's `sendkey` queues the release a fixed 100 ms of HOST time after the
    press. A guest the host starves for longer than that - a loaded box -
    finds BOTH bytes waiting at its first IRQ1, and QEMU's 8042 loads the
    next queued byte the moment port 60h is read. So when SPEC.md 9.7's
    key-state map is armed, `kbm_isr`'s peek takes the press and the ROM
    reads the release: the keystroke is gone - two IRQ1s and nothing in the
    BIOS buffer (vidxiphdhold, the Video Player arming the map for
    Alt+Enter; 1 run in 4 at four lanes). A real keyboard cannot do that: it
    holds its next byte until the controller has been read and its clock
    released, about a millisecond. This is that pacing, on the guest's
    clock. QEMU's own IRQ count is NOT the signal - it counts the line being
    RAISED, which is the byte being queued, not the guest taking it.

    For a key the ROM stores nothing for (a shift key) the tail never moves,
    so that wait is bounded by `secs` of guest time and the release goes
    anyway; answers whether the tail moved."""
    import xmcheck
    ev = {"type": "key", "data": {"down": True,
                                  "key": {"type": "qcode", "data": qcode}}}
    sock = getattr(m, "sock", None) or m.path   # (ethernet.Qemu's is .path)
    t0 = m.read(0x41C, 2)
    xmcheck.qmp_exec(sock, "input-send-event", {"events": [ev]})
    took = acted(m, lambda: m.read(0x41C, 2) != t0, secs=secs,
                 what="the ROM to store %s's press" % qcode, poll=0.005)
    ev["data"]["down"] = False
    xmcheck.qmp_exec(sock, "input-send-event", {"events": [ev]})
    return took


# One character's keystroke as a QEMU qcode, and whether it is SHIFTED.
_QCHAR = {".": ("dot", 0), ":": ("semicolon", 1), "-": ("minus", 0),
          "/": ("slash", 0), "_": ("minus", 1), "~": ("grave_accent", 1),
          "?": ("slash", 1), "=": ("equal", 0), "&": ("7", 1),
          " ": ("spc", 0), ",": ("comma", 0), "!": ("1", 1),
          "@": ("2", 1), "\n": ("ret", 0)}


class _Sock(object):
    """`key`'s `m` for a caller holding only a socket path."""

    def __init__(self, sock):
        self.sock = sock

    def read(self, linear, n):
        import xmcheck
        return bytes(xmcheck.read_bytes(self.sock, linear, n))


def _mod(sock, name, down, secs=0.3):
    """A MODIFIER's edge, CONFIRMED like `key`'s press: sent, then the ROM's
    shift-state byte (0040:0017) changing is waited for, at most `secs` of
    guest time. A make code queued behind another at one IRQ1 is what the
    kernel's 8042 peek loses on QEMU (SPEC.md 9.7, `key` above), and a shift
    lost that way typed `http;;//` for `http://` into tests/brnav.py."""
    import xmcheck
    m = _Sock(sock)
    f0 = m.read(0x417, 1)
    xmcheck.qmp_exec(sock, "input-send-event", {"events": [
        {"type": "key", "data": {"down": down, "key": {
            "type": "qcode", "data": name}}}]})
    return acted(m, lambda: m.read(0x417, 1) != f0, secs=secs,
                 what="the ROM to see %s %s" % (name, "down" if down else "up"),
                 poll=0.005)


def _shift(sock, down):
    _mod(sock, "shift", down)


# The names the ROM keeps a FLAG for at 0040:0017 rather than a buffer
# entry: a press of one is confirmed by that byte (`_mod`), and every other
# key's by the buffer's tail (`key`). tools/qmp.py's FLAG_KEYS is the same set.
FLAG_KEYS = frozenset(("shift", "shift_r", "alt", "alt_r", "ctrl", "ctrl_r",
                       "caps_lock", "num_lock", "scroll_lock"))
STORE_SECS = 10.0       # guest seconds a press may take to be STORED: a
                        # ceiling the wait leaves the moment it holds


def sendkey(sock, spec, secs=0.3):
    """HMP's `sendkey a-b-c` CONFIRMED: the modifiers down, `key` on the last
    name, the modifiers up. HMP's own queues the release 100 ms of HOST time
    after the press, which a starved guest takes in the same IRQ1 as the
    press - a lost key (17.5). The names are QEMU's qcodes, which are HMP's
    key names.

    THE LAST KEY IS RELEASED ONLY ONCE THE ROM HAS STORED IT - up to
    STORE_SECS guest seconds, a ceiling and not a pause - unless it is a
    FLAG key, whose press is the shift-state byte changing within `secs`.
    `secs` used to bound every key, so a press the guest had not yet taken
    after seven ticks was released anyway: the queued pair SPEC.md 9.7 says
    QEMU's 8042 loses whenever kbm_isr peeks (docs/plans/SOAK-PARALLEL.md
    17.11). A stored key whose tail never moves is said on stderr."""
    names = spec.split("-")
    mods, last = names[:-1], names[-1]
    for n in mods:
        _mod(sock, n, True, secs)
    if last in FLAG_KEYS:
        _mod(sock, last, True, secs)
        _mod(sock, last, False, secs)
    elif not key(_Sock(sock), last, secs=STORE_SECS):
        sys.stderr.write("os88qemu: sendkey %s: the ROM had not stored the "
                         "press after %.0f guest seconds - released anyway\n"
                         % (spec, STORE_SECS))
    for n in reversed(mods):
        _mod(sock, n, False, secs)


def type_text(sock, text, gap=0.06):
    """TYPE `text`, every keystroke CONFIRMED (`key`): the release goes only
    once the ROM has stored the press, and `gap` guest seconds follow each.

    It replaces three copies of a loop of HMP `sendkey`s through
    tools/qmp.py (tests/ethernet.py's type_url, telansi's and wirechat's
    typetext). `sendkey` queues the release 100 ms of HOST time after the
    press, so a guest the host starved past that took both bytes at one IRQ1
    and the character was lost - measured at eight emulators on four cores,
    telansi's host box read '10..2.2:8094' and nothing after it connected
    (docs/plans/SOAK-PARALLEL.md 17.5). A shift is held across its key.
    """
    m = _Sock(sock)
    for ch in text:
        if ch.isalnum():
            q, sh = ch.lower(), ch.isupper()
        elif ch in _QCHAR:
            q, sh = _QCHAR[ch]
        else:
            raise ValueError("os88qemu.type_text: no qcode for %r" % ch)
        if sh:
            _shift(sock, True)
        key(m, q)
        if sh:
            _shift(sock, False)
        if gap:
            pace(m, gap, poll=0.01)


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


def poke(hmp, linear, data):
    """WRITE guest memory - which QEMU's monitor cannot do.

    HMP reads (`xp`, `pmemsave`) and has no write at all, so this borrows the
    one door QEMU has: its GDB stub. `gdbserver` starts one on a free local
    port at runtime (no relaunch, no `-s` in the Makefile), the remote
    protocol's `M` packet writes the bytes, and `D` detaches - which also
    RESUMES the guest the connection paused, so the write lands between two
    instructions with the machine stopped and every byte of `data` written at
    once. `gdbserver none` takes the stub down again. `hmp` is a callable
    taking one HMP command line (tests/thewire.py's `Qemu.hmp`). In real mode
    there is no paging, so the stub's address is the LINEAR one os88sym hands
    out. Raises on any answer but OK - a write that did not happen must not
    read as a test of what it would have done.
    """
    if SERVED:                          # the Bochs/86Box shim WRITES memory
        import os88qmpmega
        os88qmpmega.live().m.write(linear, bytes(data))
        return
    import socket
    s0 = socket.socket()
    s0.bind(("127.0.0.1", 0))
    port = s0.getsockname()[1]
    s0.close()
    hmp("gdbserver tcp:127.0.0.1:%d" % port)
    s = None
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=5)
            break
        except OSError:
            time.sleep(0.1)
    if s is None:
        raise RuntimeError("poke: no GDB stub on port %d" % port)
    buf = b""

    def packet(body):
        nonlocal buf
        b = body.encode("ascii")
        s.sendall(b"$" + b + b"#%02x" % (sum(b) & 0xFF))
        while True:                       # skip acks; read one $...#xx reply
            while b"#" not in buf or len(buf) < buf.index(b"#") + 3:
                got = s.recv(4096)
                if not got:
                    raise RuntimeError("poke: the GDB stub hung up")
                buf += got
            buf = buf.lstrip(b"+")
            if buf.startswith(b"$"):
                end = buf.index(b"#")
                reply, buf = buf[1:end], buf[end + 3:]
                s.sendall(b"+")
                return reply.decode("ascii", "replace")
            buf = buf[1:]                 # noise before a packet
    try:
        packet("?")                       # the stop reply: the guest is held
        r = packet("M%x,%x:%s" % (linear, len(data), bytes(data).hex()))
        if r != "OK":
            raise RuntimeError("poke: M at %#x answered %r" % (linear, r))
        packet("D")                       # detach: the guest runs on
    finally:
        s.close()
        hmp("gdbserver none")


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


def kernel_up(m, sym, secs=120.0, what="the kernel to reach the desktop"):
    """tools/os88marty.py's `kernel_up` on QEMU: `desk_rows` and `menu_nbar`
    written and `[spl_live]` = 0 (kmain's last act), the tick moving across
    it, then `ui_done`. THE GATE BEFORE A DRIVER'S SEGMENT IS READ: a driver
    row's segment is armed BEFORE its image is read, and mem_unblob packs the
    ceiling at the end of kmain, so a segment taken mid-boot can be one the
    image has left - and a starved guest is mid-boot for longer. Measured:
    tests/ethernet.py under load read eth_mode 211 and dhcp_st 194 off a stale
    segment for the whole row (docs/plans/SOAK-PARALLEL.md 17.5)."""
    a = (sym("desk_rows"), sym("menu_nbar"), sym("spl_live"))
    seen = [None]

    def up():
        if not (any(m.read(a[0], 2)) and any(m.read(a[1], 2))
                and not m.read(a[2], 1)[0]):
            seen[0] = None
            return False
        t = ticks(m)
        if seen[0] is None:
            seen[0] = t
            return False
        return t != seen[0]
    ok = acted(m, up, secs=secs, what=what, poll=0.1)
    if ok:
        ui_done(m, sym, what=what)
    return ok


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


# --- A HEAP BASE AND ITS BYTES IN ONE SNAPSHOT (tools/os88marty.py's twin) ---
#
# The compactor copies a block (mem_bcopy) BEFORE it rewrites the records that
# name it (mem_reloc_call), so a window's W_SEG or a drv_tab row's segment
# read as one HMP call and the bytes through it as another can straddle a move
# and hand back the old base's shifted copy (SPEC.md 66; os88marty's banner
# over seg_snap is the account). Same algorithm here: base, spans, then
# [mem_cp_busy] and the base again - unchanged and idle is consistent - and
# only otherwise a STOPPED guest (HMP `stop`) let run in short `cont` bursts
# until the pass is over. QEMU has no cycle clock to step by, so the bursts
# are host-timed; what they wait for is the guest's own byte.
#
# `q` is any of the tree's QEMU driver objects: an `hmp(cmd)` answering a
# string and a `read(linear, n)`. os88marty.seg_snap/heap_read/pkg_read hand
# a QEMU object here themselves, so a helper written once serves both.

HEAP_BURST = 0.005      # host seconds a `cont` burst while a pass runs
HEAP_LIMIT = 60.0       # ...and how long before that is a stuck compactor


def _busy(q):
    b = getattr(q, "_heap_busy_sym", False)
    if b is False:
        try:
            if hasattr(q, "sym"):
                b = q.sym("mem_cp_busy")
            else:
                sys.path.insert(0, os.path.join(ROOT, "tools"))
                import os88sym
                b = os88sym.linear("mem_cp_busy")
        except (Exception, SystemExit):                     # noqa: BLE001
            b = None
        try:
            q._heap_busy_sym = b
        except AttributeError:
            pass
    return b


def heap_read(q, read, what="a heap snapshot"):
    """`read(q)` answered off a STOPPED guest with [mem_cp_busy] clear; the
    guest is resumed only if it was running (os88marty.heap_read's twin)."""
    busy = _busy(q)
    was = "running" in q.hmp("info status")
    q.hmp("stop")
    try:
        t0 = time.time()
        while busy is not None and q.read(busy, 1)[0]:
            if time.time() - t0 > HEAP_LIMIT:
                raise RuntimeError("%s: the heap compactor was still running "
                                   "after %.0fs ([mem_cp_busy] never cleared)"
                                   % (what, HEAP_LIMIT))
            q.hmp("cont")
            time.sleep(HEAP_BURST)
            q.hmp("stop")
        return read(q)
    finally:
        if was:
            q.hmp("cont")


def seg_snap(q, word, spans, base=None, what="a read through a heap base",
             step=True):
    """(seg, [bytes, ...]) through the segment the kernel word at flat
    `word` names, in one consistent snapshot - os88marty.seg_snap's twin."""
    # (under $OS88_MEGA the word names an LDT SELECTOR: `lin` resolves it,
    # and os88geom.drv_base - a real-mode HMA sum - is lin's job there too)
    lin = (lambda s: globals()["lin"](s, 0)) if MEGA \
        else (base or (lambda s: s << 4))
    u16 = lambda b: b[0] | (b[1] << 8)          # noqa: E731
    busy = _busy(q)
    for _ in range(2):
        seg = u16(q.read(word, 2))
        if not seg:
            return 0, None
        out = [q.read(lin(seg) + o, n) for o, n in spans]
        if busy is None:
            return seg, out
        b = q.read(busy, 1)[0]
        if not b and u16(q.read(word, 2)) == seg:
            return seg, out
        if b:
            break
    if not step and "running" not in q.hmp("info status"):
        return seg, out                 # the caller holds it mid-pass

    def snap(qq):
        s = u16(qq.read(word, 2))
        return (s, [qq.read(lin(s) + o, n) for o, n in spans] if s else None)
    return heap_read(q, snap, what)


# --- a CLOSED-LOOP mouse (docs/plans/completed/SOAK-SPEED.md, harness changes) ---------
#
# tools/mouse.py cannot know where the pointer is, so it PINS against the
# bottom-right clamp and walks back, a host `sleep 0.06` after every packet,
# then holds each button edge 0.2 guest s: ~1.7 s a click, which was 88 of
# rdxms's 111 host seconds. A row that can resolve kernel symbols can do what
# tools/os88mouse.py does on MartyPC instead - read `mouse_x`/`mouse_y`/
# `mouse_btn` (kernel/mouse.inc, published by the ISR), send the exact delta,
# and wait for the word to MOVE rather than for a clock. The packet still goes
# through QEMU's msmouse, the UART and mou_isr: nothing is poked.

PKT_SECS = 0.5          # guest seconds a packet may take to land (it is ~25 ms
                        # at 1200 baud; the slack is a guest busy repainting)
STEP = 60               # msmouse truncates bigger deltas (tools/mouse.py)


class MouseError(RuntimeError):
    """The pointer did not get where it was sent, or a button edge was lost."""


def mouse_where(m, sym):
    """(x, y, buttons) as the kernel last published them."""
    a = getattr(m, "_mouse_at", None)
    if a is None:
        a = sym("mouse_x")
        if sym("mouse_y") != a + 2 or sym("mouse_btn") != a + 4:
            raise MouseError("mouse_x/mouse_y/mouse_btn are not contiguous "
                             "any more - mouse_where reads them as one block")
        m._mouse_at = a
    b = m.read(a, 5)
    return b[0] | (b[1] << 8), b[2] | (b[3] << 8), b[4]


def _changed(m, sym, was, what):
    c = Clock(m)
    lim = to_ticks(PKT_SECS)
    while True:
        if mouse_where(m, sym) != was:
            return True
        if c.step() >= lim or c.stalled():
            return False
        time.sleep(0.003)


def mouse_to(m, sym, hmp, x, y, btn=0):
    """Drive to (x, y) and PROVE it, carrying button level `btn` (an HMP
    move carries the current buttons). `hmp(*cmds)` sends human-monitor
    commands (xmcheck.qmp bound to the socket). Raises MouseError when the
    pointer stops short - a target off the kernel's clamp, or packets lost
    for longer than a busy guest explains."""
    cx, cy, _ = mouse_where(m, sym)
    tries = (max(abs(x - cx), abs(y - cy)) + STEP - 1) // STEP * 2 + 6
    for _ in range(tries):
        cx, cy, b = mouse_where(m, sym)
        dx, dy = x - cx, y - cy
        if not dx and not dy:
            return
        hmp("mouse_move %d %d" % (max(-STEP, min(STEP, dx)),
                                  max(-STEP, min(STEP, dy))))
        _changed(m, sym, (cx, cy, b), "the pointer to move")
    cx, cy, _ = mouse_where(m, sym)
    if (cx, cy) != (x, y):
        raise MouseError("could not reach (%d,%d): stuck at (%d,%d)"
                         % (x, y, cx, cy))


def mouse_button(m, sym, hmp, mask):
    """Set the buttons to `mask` (HMP's: 1 left, 2 right) and wait until the
    kernel has decoded that level - mouse_btn's bit 0 left, bit 1 right."""
    want = (1 if mask & 1 else 0) | (2 if mask & 2 else 0)
    for _ in range(3):
        hmp("mouse_button %d" % mask)
        c = Clock(m)
        lim = to_ticks(PKT_SECS)
        while c.step() < lim and not c.stalled():
            if mouse_where(m, sym)[2] & 3 == want:
                return
            time.sleep(0.003)
        if mouse_where(m, sym)[2] & 3 == want:
            return
    raise MouseError("the button level %d was never decoded (mouse_btn = %02x)"
                     % (want, mouse_where(m, sym)[2]))


def mouse_click(m, sym, hmp, x, y, mask=1):
    """to (x, y), press, release - each edge confirmed. No pause after: the
    caller's wait (ui_done, acted) is on the state the click is FOR."""
    mouse_to(m, sym, hmp, x, y)
    if mouse_where(m, sym)[2] & 3:
        mouse_button(m, sym, hmp, 0)
    mouse_button(m, sym, hmp, mask)
    mouse_button(m, sym, hmp, 0)


class Private(object):
    """A PRIVATE QEMU - what `make test TESTIMG=a TESTAPPS=b [TESTHD=hd]
    [ETHER=1]` launched, with its own socket, pidfile and COPIES of the
    floppies in `work`, so a row runs no `make` in the shared tree and never
    kills another row's emulator through build/qemu.pid. `ether` adds the
    NE2000 at 300h/IRQ3 on slirp, WITHOUT `make test`'s host forwards (a
    fixed host port is a collision between two rows). `.sock` is the QMP
    socket; `.close()` (also at exit) ends it by the pid it wrote."""

    def __init__(self, work, a, b, hd=None, ether=False, extra=()):
        import atexit
        import shutil
        import subprocess
        self.sock = os.path.join(work, "qmp.sock")
        pidf = os.path.join(work, "qemu.pid")
        self.a = os.path.join(work, "a.img")
        self.b = os.path.join(work, "b.img")
        shutil.copy(a, self.a)
        shutil.copy(b, self.b)
        cmd = ["qemu-system-i386", "-machine", "pc,vmport=off",
               "-drive", "file=%s,format=raw,if=floppy" % self.a,
               "-boot", "c" if hd else "a",
               "-chardev", "msmouse,id=m0", "-serial", "chardev:m0",
               "-drive", "file=%s,format=raw,if=floppy,index=1" % self.b]
        if hd:
            cmd += ["-drive", "file=%s,format=raw,if=ide,index=0,media=disk"
                    % hd]
        if ether:
            cmd += ["-netdev", "user,id=n0",
                    "-device", "ne2k_isa,netdev=n0,iobase=0x300,irq=3"]
        cmd += ["-display", "none", "-qmp", "unix:%s,server,nowait"
                % self.sock, "-daemonize", "-pidfile", pidf] + list(extra)
        start(cmd, check=True)
        self.pid = int(open(pidf).read().strip())
        atexit.register(self.close)
        for _ in range(150):
            if os.path.exists(self.sock):
                break
            time.sleep(0.2)             # the socket appearing is a HOST event

    def close(self):
        if self.pid:
            try:
                os.kill(self.pid, signal.SIGTERM)
                gone(self.pid)
            except OSError:
                pass
            self.pid = None


class Mouse(object):
    """tools/mouse.py's verbs as an OBJECT, closed-loop (mouse_to): `click`,
    `dblclick`, `run("to"|"down"|"up"|"click", ...)` - the shape
    tests/ethernet.py's Mouse and the dispcp verbs take. `after` is the
    guest pause that followed each gesture there, now a CEILING: ui_done
    ends it as soon as the UI is finished with the gesture."""

    def __init__(self, m, sym, hmp, after=0.4):
        self.m, self.sym, self.hmp, self.after = m, sym, hmp, after

    def _wait(self):
        if self.after:
            ui_done(self.m, self.sym, cap=self.after)

    def run(self, verb, *xy):
        xy = tuple(int(v) for v in xy)
        if verb == "click":
            mouse_click(self.m, self.sym, self.hmp, *xy)
            return
        if xy:
            mouse_to(self.m, self.sym, self.hmp, *xy)
        if verb in ("down", "up"):
            mouse_button(self.m, self.sym, self.hmp, 1 if verb == "down" else 0)
        elif verb != "to":
            raise ValueError("Mouse.run: %r" % verb)

    def to(self, x, y):
        mouse_to(self.m, self.sym, self.hmp, x, y)

    def click(self, x, y):
        mouse_click(self.m, self.sym, self.hmp, x, y)
        self._wait()

    def dblclick(self, x, y):
        """Both presses down ONE QMP connection, as xmcheck.dblclick: the
        detectors' 9-tick window is shorter than two confirmed clicks."""
        mouse_dblclick(self.m, self.sym, self.hmp, x, y)
        self._wait()


def mouse_dblclick(m, sym, hmp, x, y):
    """to (x, y), then two clicks, every edge confirmed off mouse_btn - four
    packets, well inside the kernel's 9-tick double-click window (SPEC.md
    22), where xmcheck.dblclick paced them with host sleeps after a
    tools/mouse.py walk (~1.7 s)."""
    mouse_to(m, sym, hmp, x, y)
    if mouse_where(m, sym)[2] & 3:
        mouse_button(m, sym, hmp, 0)
    # ALL FOUR EDGES IN ONE BURST, confirmed only at the end. They were each
    # confirmed off mouse_btn, a round trip apiece, and on QEMU the guest's
    # ticks are the host's: four round trips on a loaded box are more than
    # the 9-tick window, and the double-click arrived as two clicks
    # (vidclose: "the player did not open", docs/plans/SOAK-PARALLEL.md
    # 17.5). msmouse queues a packet per edge and the UART delivers them
    # back to back, so the presses are born a few milliseconds apart in the
    # guest whatever the host is doing.
    # (one command a call: some callers' `hmp` takes exactly one - trkscrl's
    # and minesrc's did, and a four-argument call was a TypeError there)
    for edge in (1, 0, 1, 0):
        hmp("mouse_button %d" % edge)
    c = Clock(m)
    lim = to_ticks(PKT_SECS)
    while (mouse_where(m, sym)[2] & 3 or c.step() < 2) and c.step() < lim:
        time.sleep(0.005)


def launch_test(tmp, sysimg="build/os8088.img", apps="build/apps.img",
                extra=(), card=True, copy=True):
    """`make test`'s machine WITHOUT `make`, in a directory of its own.

    `make test` is a launcher, but its parse rewrites build/buildnum.inc and
    its prerequisites are the shared images - so in a checkout whose build/
    is behind its sources (every commit moves the build number) it REBUILDS
    the kernel and the disks under every other row before it boots. This is
    the same command line (Makefile `test:` with the default MOUSE and
    DEVCARD - the OPL2 at 388h, `card=False` is NOCARD=1) against COPIES of
    the two images in `tmp` (the guest writes SYSTEM.CFG; `copy=False` boots
    the files given, for a row that reads its own scratch back), and its own
    socket and pidfile, so two of these can run at once. Paths resolve
    through os88build.at. Answers (sock, pidfile); torn down at exit (own)."""
    import shutil
    import subprocess
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88build
    imgs = []
    for name, src in (("sys.img", sysimg), ("apps.img", apps)):
        src = os88build.at(src) if not os.path.isabs(src) else src
        if copy:
            dst = os.path.join(tmp, name)
            shutil.copyfile(src, dst)
            src = dst
        imgs.append(os.path.abspath(src))
    sock = os.path.join(tmp, "qmp.sock")
    pid = os.path.join(tmp, "qemu.pid")
    cmd = ["qemu-system-i386", "-machine", "pc,vmport=off",
           "-drive", "file=%s,format=raw,if=floppy" % imgs[0], "-boot", "a",
           "-chardev", "msmouse,id=m0", "-serial", "chardev:m0",
           "-drive", "file=%s,format=raw,if=floppy,index=1" % imgs[1],
           "-display", "none", "-qmp", "unix:%s,server,nowait" % sock,
           "-daemonize", "-pidfile", pid]
    if card:
        cmd += ["-audiodev", "none,id=devsnd", "-device",
                "adlib,audiodev=devsnd"]
    own(pid, sock)
    start(cmd + list(extra), cwd=ROOT, check=True)
    # ...and BY THE PID IT WROTE as well (vidxms.Q's lesson): a caller that
    # deletes `tmp` on its way out takes the pidfile own() would read
    atexit.register(_killpid, int(open(pid).read().strip()))
    return sock, pid


def _killpid(pid):
    try:
        os.kill(pid, signal.SIGTERM)
        gone(pid)
    except OSError:
        pass


# -----------------------------------------------------------------------------
# `make test` WITHOUT make, AND WITHOUT ANYTHING SHARED (SOAK-PARALLEL.md 17).
#
# `make test KNOB=...` was the launcher of a dozen QEMU rows, and it shares
# FOUR things with every other row on the box: the QMP socket and pidfile it
# spells literally (build/qmp.sock, build/qemu.pid), the shared images it
# boots IN PLACE and so write-locks (`Failed to get "write" lock` for the next
# machine on build/os8088.img), and the parse - which rewrites
# build/buildnum.inc and checks the images' prerequisites in the shared tree
# while a frozen soak reads its own. Any one of them is enough to keep a row
# in the one-at-a-time lane.
#
# This is the same machine. The command line is the Makefile's `test:` recipe
# and the knobs are spelled exactly as on a `make` command line, so a call
# site changes from a `make test ETHER=1 ...` subprocess to
# `os88qemu.make_test("ETHER=1", ...)` and nothing else moves. What differs:
#
#   * the socket and pidfile are THIS PROCESS'S (RUNDIR), and module SOCK /
#     PIDFILE - and tests/ethernet.py's SOCK when it is loaded - are pointed
#     at them, so every helper that defaults to "the" socket drives this one;
#   * TESTIMG / TESTAPPS are COPIED into a private directory - unless the
#     image is already inside RUNDIR, which makes it the row's own scratch
#     (made here and read back afterwards), or `copy=False` is passed;
#   * paths resolve through os88build.at, so a frozen run boots its own tree.
#
# Knobs the recipe does not take here are refused by name rather than
# ignored: a VIDEO= or RTC= is a KERNEL build, and ignoring it would boot the
# wrong kernel and report on it.
# -----------------------------------------------------------------------------
_MT_KNOBS = {"TESTIMG", "TESTAPPS", "TESTHD", "ETHER", "ETHFWD", "ETHDUMP", "ADLIB",
             "SB16", "NOCARD", "VMPORT", "MOUSEPORT"}


# The Makefile's ETHFWD=1 forwards, kept equal to its FTPPASV by hand: the
# server's passive ports are fixed in apps/ftpd, so these are too.
ETHFWD_CTRL = 2121
ETHFWD_PASV = tuple(range(2048, 2056))


class Instance(object):
    """One launched machine: `.sock`, `.pidfile`, `.pid`, `.a`/`.b`/`.hd`
    (the paths QEMU actually opened), `.dir`. `close()` (also at exit, and
    on SIGTERM through own()) ends it by the pid it wrote."""

    def __init__(self, sock, pidfile, pid, a, b, hd, d):
        self.sock, self.pidfile, self.pid = sock, pidfile, pid
        self.a, self.b, self.hd, self.dir = a, b, hd, d

    def close(self):
        if self.pid:
            _killpid(self.pid)
            self.pid = None
        for f in (self.sock, self.pidfile):
            try:
                os.unlink(f)
            except OSError:
                pass


def _alive(pidfile):
    try:
        pid = int(open(pidfile).read().strip())
        os.kill(pid, 0)
    except (OSError, ValueError):
        return False
    return not _zombie(pid)


def scratch(path, name=None):
    """A COPY of a built image in this process's RUNDIR, for a row that boots
    it and then READS IT BACK on the host: make_test boots an image already
    inside RUNDIR in place. Answers the copy's path."""
    import shutil
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88build
    src = os88build.at(path) if not os.path.isabs(path) else path
    dst = os.path.join(RUNDIR, name or os.path.basename(path))
    shutil.copyfile(os.path.join(ROOT, src), dst)
    return dst


def _point(sock, pidfile):
    """Make this instance THE default for every helper in this process."""
    global SOCK, PIDFILE
    SOCK, PIDFILE = sock, pidfile
    eth = sys.modules.get("ethernet")
    if eth is not None:
        eth.SOCK = sock


def make_test(*knobs, copy=True, extra=()):
    """`make test <knobs>`'s machine, private. Answers an Instance."""
    import shutil
    import subprocess
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88build
    kv = {}
    for k in knobs:
        name, _, val = k.partition("=")
        if name not in _MT_KNOBS:
            raise ValueError("os88qemu.make_test: %s is not a launch knob "
                             "(a kernel or build knob needs a private tree - "
                             "tools/os88build.py)" % name)
        kv[name] = val

    def at(p):
        p = os88build.at(p) if not os.path.isabs(p) else p
        return os.path.abspath(os.path.join(ROOT, p))

    d = workdir("mt")
    a = at(kv.get("TESTIMG") or "build/os8088.img")
    b = at(kv.get("TESTAPPS") or "build/apps.img")
    # An image already inside RUNDIR is this row's OWN scratch - made here,
    # read back after - so it is booted in place; everything else is shared
    # and is copied, so the guest's writes never reach it.
    mine = os.path.join(RUNDIR, "")
    if copy and not a.startswith(mine):
        ca = os.path.join(d, "a.img")
        shutil.copyfile(a, ca)
        a = ca
    if copy and not b.startswith(mine):
        cb = os.path.join(d, "b.img")
        shutil.copyfile(b, cb)
        b = cb
    hd = at(kv["TESTHD"]) if kv.get("TESTHD") else None
    # THE DEFAULT PAIR when it is free, so a script that captured
    # os88qemu.SOCK at import (`SOCK = os88qemu.SOCK`) drives this machine
    # exactly as it drove `make test`'s - and a fresh pair only when an
    # instance of this process's is still alive on it
    sock, pidfile = SOCK, PIDFILE
    if _alive(pidfile):
        sock, pidfile = paths()
    else:
        for f in (sock, pidfile):
            try:
                os.unlink(f)
            except OSError:
                pass
    vmport = kv.get("VMPORT") or "off"
    cmd = ["qemu-system-i386", "-machine", "pc,vmport=%s" % vmport,
           "-drive", "file=%s,format=raw,if=floppy" % a,
           "-boot", "c" if hd else "a"]
    # MOUSE: the Makefile's default arm, its PS/2 arm and VMPORT=on's
    mp = kv.get("MOUSEPORT")
    if mp == "ps2" or vmport == "on":
        cmd += ["-serial", "none"]
    elif mp:
        raise ValueError("os88qemu.make_test: MOUSEPORT=%s is not carried "
                         "here yet - add its arm from the Makefile" % mp)
    else:
        cmd += ["-chardev", "msmouse,id=m0", "-serial", "chardev:m0"]
    cmd += ["-drive", "file=%s,format=raw,if=floppy,index=1" % b]
    if hd:
        cmd += ["-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % hd]
    cmd += ["-display", "none", "-qmp", "unix:%s,server,nowait" % sock,
            "-daemonize", "-pidfile", pidfile]
    if kv.get("ADLIB") or kv.get("SB16"):
        cmd += ["-audiodev", "none,id=snd"]
        if kv.get("ADLIB"):
            cmd += ["-device", "adlib,audiodev=snd"]
        if kv.get("SB16"):
            cmd += ["-device", "sb16,audiodev=snd"]
    elif not kv.get("NOCARD"):
        cmd += ["-audiodev", "none,id=devsnd", "-device",
                "adlib,audiodev=devsnd"]
    if kv.get("ETHER"):
        net = "user,id=n0"
        if kv.get("ETHFWD"):
            # The Makefile's ETHFWD=1: host 2121 -> guest 21 and the FTP
            # server's whole passive range 1:1 (a PASV reply names the GUEST's
            # port). Host ports cannot be private, so each is LEASED first and
            # a second FTP row in any runner waits instead of colliding.
            for port in (ETHFWD_CTRL,) + ETHFWD_PASV:
                lease_port(port)
            net += ",hostfwd=tcp::%d-:21" % ETHFWD_CTRL
            net += "".join(",hostfwd=tcp::%d-:%d" % (p, p) for p in ETHFWD_PASV)
        cmd += ["-netdev", net,
                "-device", "ne2k_isa,netdev=n0,iobase=0x300,irq=3"]
        if kv.get("ETHDUMP"):
            cmd += ["-object", "filter-dump,id=fdump,netdev=n0,file=%s"
                    % os.path.abspath(kv["ETHDUMP"])]
    if SERVED:
        # KERN_MEGA UNDER BOCHS, or ANY KERNEL UNDER 86Box ($OS88_EMU=86box):
        # the same socket answered by tests/os88qmpmega.py, an NE2000 on the
        # emulator's slirp for ETHER=, the PS/2 mouse for MOUSEPORT=ps2
        return _mega(sock, pidfile, a, b, hd, d, ether=bool(kv.get("ETHER")),
                     sb=bool(kv.get("SB16")), ps2=mp == "ps2")
    own(pidfile, sock)
    start(cmd + list(extra), cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    pid = int(open(pidfile).read().strip())
    atexit.register(_killpid, pid)
    _point(sock, pidfile)
    return Instance(sock, pidfile, pid, a, b, hd, d)


# -----------------------------------------------------------------------------
# KERN_MEGA (docs/BOCHS.md, "Rows ported from MartyPC"). Under $OS88_MEGA a
# launcher here starts tests/os88qmpmega.py's Server - a Bochs running
# kern_mega with an NE2000 on its slirp - behind the socket path QEMU would
# have had, so every QMP client a row uses drives it unchanged. Three helpers
# carry what the transport cannot: `lin` (a guest-read segment is an LDT
# SELECTOR there, so `seg << 4` is the wrong address), `drv_base` (os88geom's,
# through `lin`) and `mega_twin` (a system-disk fixture rebuilt on mega.img).
# Off kern_mega each is what the row computed before.
# -----------------------------------------------------------------------------
MEGA = bool(os.environ.get("OS88_MEGA"))
# ...and SERVED is MEGA or a kern_big/kern_small row handed to 86Box
# ($OS88_EMU=86box, tools/os88box86api.py): the launchers start the same
# QMP server over it. `lin`, `drv_base` and `mega_twin` stay MEGA's alone -
# a real-mode kernel's segment is `seg << 4` whatever runs it
SERVED = MEGA or (os.environ.get("OS88_EMU") or "").lower() == "86box"


def lin(seg, off=0):
    """SEG:OFF's linear address: `(seg << 4) + off`, or a selector's base."""
    if not MEGA:
        return (seg << 4) + off
    import os88qmpmega
    return os88qmpmega.lin(seg, off)


def drv_base(seg):
    """os88geom.drv_base for the machine this process launched."""
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import os88geom
    if not MEGA:
        return os88geom.drv_base(seg)
    return lin(seg, 0)


def mega_twin(img):
    """`img`, or under $OS88_MEGA its kern_mega twin (os88qmpmega)."""
    if not MEGA:
        return img
    import os88qmpmega
    return os88qmpmega.mega_twin(img)


def _mega(sock, pidfile, a, b, hd, d, ether=True, sb=False, ps2=False):
    import os88qmpmega
    srv = os88qmpmega.Server(sock, pidfile, a, b, hd=hd, ne2k=ether,
                              sb=sb, ps2=ps2)
    _point(sock, pidfile)
    inst = Instance(sock, pidfile, None, a, b, hd, d)
    inst.server = srv
    inst.close = srv.close
    return inst


# -----------------------------------------------------------------------------
# A HOST PORT IS THE ONE THING THAT CANNOT BE MADE PRIVATE (SOAK-PARALLEL
# 17.3). A slirp guest reaches the host at the gateway - 10.0.2.2:<port> is
# the host's 127.0.0.1:<port> - and that address is BAKED INTO FIXTURES
# (WIRE.CFG, HTTPDRV.CFG, install-gate.o88's IN_URL, the browser fixture's
# links). The remap that would make it private, `guestfwd`, is refused on the
# gateway by libslirp ("Conflicting/invalid host:port in guest forwarding
# rule"), so two rows serving one port cannot run at once - and neither can a
# row here and the same row in ANOTHER checkout's soak.
#
# So the port is LEASED: an flock on /tmp/.os88port-<n>.lock, held for the
# process's life. tools/os88test.py keeps two rows that declare the same port
# (`Row(locks=("port:8090",))`) out of each other's way without spending a
# lane on the wait; this lease is the cross-runner half, and it turns the
# collision into a WAIT rather than an `Address already in use` that reads as
# the feature under test.
# -----------------------------------------------------------------------------
_LEASES = {}


def lease_port(n, secs=1800.0):
    """Hold host port `n` for this process's life (blocking up to `secs`)."""
    import fcntl
    if n in _LEASES:
        return
    fd = os.open("/tmp/.os88port-%d.lock" % n, os.O_RDWR | os.O_CREAT, 0o666)
    end = time.monotonic() + secs
    said = False
    while True:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except OSError:
            if not said:
                print("os88qemu: host port %d is leased by another run - "
                      "waiting" % n, file=sys.stderr)
                said = True
            if time.monotonic() > end:
                os.close(fd)
                raise RuntimeError("host port %d stayed leased for %ds"
                                   % (n, secs))
            time.sleep(0.5)             # a HOST wait, on a host resource
    _LEASES[n] = fd
