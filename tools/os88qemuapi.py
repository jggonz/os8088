#!/usr/bin/env python3
"""QEMU answering the os88marty TEST API - `$OS88_EMU=qemu` (docs/TESTING.md,
docs/plans/soak-speed-2/qemubox.md).

    OS88_EMU=qemu python3 tests/<row>.py

"Marty" is `os88marty.Marty`, the class every MartyPC row is written against.
`ApiQemu` is a SERVER for the requests `Marty` is built on (`cmd(**kw)`),
answered out of a private QEMU over QMP - as tools/os88box86api.py answers
them out of an 86Box - so os88ui (`boot`, `open_drive`, `path`, `menu_pick`,
`raise_window`, ...), os88mouse, `until`/`ui_done`/`quiesce`/`key_taken` and
every row above them run on QEMU unchanged. That is the parity a QEMU row
wanted: the closed list in docs/TESTING.md (a 386's CPU, the PS/2 mouse, the
NE2000, the MC146818's write half, the VMware pointer) used to be written in
QMP by hand, a dialect of its own; written against os88ui it is the same row
as MartyPC's, with the emulator chosen by the environment.

THE CLOCK IS THE GUEST'S, as MartyPC's is - but COARSER. QEMU publishes no
cycle counter a client can read, so `status()["cycles"]` is the BIOS tick
count (0040:006C, summed a step at a time as tests/os88qemu.py's Clock does)
times GUEST_HZ / 18.2065: every guest budget in os88marty means the same
seconds of machine time it means there, to a tick (55 ms). `advance(cycles=)`
runs WHOLE ticks (at least one). QEMU's guest clock follows the host's (no
-icount here), so a loaded host slows the guest and every budget with it -
which is the point of a guest budget.

WHAT IT HAS NOT GOT, each a MartyError naming the verb, never a silent
stand-in: breakpoints and single steps (`bp`, `step`, `park`, `setreg`),
snapshots (`os88marty.Forks`), the FDC counters (`disk()` answers {} - a
`ui_done` hold is then held on the tick alone), an EGA, a Hercules, an MDA
or two cards (QEMU has only its VGA: docs/HERCULES-TESTING.md is the 1bpp
route; a CGA machine NAME is run on the VGA, and said), and a machine
smaller than 640KB. Registers are `info registers`, read only.

THE MACHINE a MartyPC name describes (`machine_args`): QEMU's `pc` with its
VGA, both floppies, 16 MB; `_ps2` - the pointer on the i8042 (no serial
mouse); otherwise QEMU's msmouse on COM1; `_ne2k` an NE2000 at 300h/IRQ 3 on
slirp; `_sb` an SB16 at 220h/IRQ 7/DMA 1 with no audio out; `_hdd` the row's
own fixed disk (`extra=("--mount", "hd:0:<path>")`, as os88marty's).

Every instance is PRIVATE (tests/os88qemu.py's rule, SOAK-PARALLEL 17): its
own directory under $OS88_QEMU_DIR (the runner's per-row sweep) or /tmp, its
own QMP and GDB sockets, COPIES of the floppies, killed by the pid it wrote
at close and at exit - never by name.
"""
import atexit
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
for _p in (HERE, os.path.join(ROOT, "tests")):
    if _p not in sys.path:
        sys.path.insert(0, _p)
import os88marty as om          # noqa: E402
import qmp as qmpc              # noqa: E402

GUEST_HZ = om.GUEST_HZ
TICK_HZ = 18.2065
TICKCYC = GUEST_HZ / TICK_HZ    # one BIOS tick, in GUEST_HZ cycles
TAG = "os88qemuapi"
BIN = os.environ.get("OS88_QEMU_BIN", "qemu-system-i386")

# W3C KeyboardEvent.code (MartyKey's vocabulary) -> QEMU qcode
_KEYS = {"Enter": "ret", "Escape": "esc", "Space": "spc",
         "Backspace": "backspace", "Tab": "tab", "ArrowUp": "up",
         "ArrowDown": "down", "ArrowLeft": "left", "ArrowRight": "right",
         "ShiftLeft": "shift", "ShiftRight": "shift_r",
         "ControlLeft": "ctrl", "ControlRight": "ctrl_r",
         "AltLeft": "alt", "AltRight": "alt_r", "Period": "dot",
         "Comma": "comma", "Slash": "slash", "Semicolon": "semicolon",
         "Quote": "apostrophe", "BracketLeft": "bracket_left",
         "BracketRight": "bracket_right", "Backslash": "backslash",
         "Minus": "minus", "Equal": "equal", "Backquote": "grave_accent",
         "Home": "home", "End": "end", "PageUp": "pgup", "PageDown": "pgdn",
         "Insert": "insert", "Delete": "delete", "CapsLock": "caps_lock",
         "NumLock": "num_lock", "ScrollLock": "scroll_lock",
         "NumpadAdd": "kp_add", "NumpadSubtract": "kp_subtract",
         "NumpadMultiply": "kp_multiply", "NumpadDivide": "kp_divide",
         "NumpadEnter": "kp_enter", "NumpadDecimal": "kp_decimal",
         "PrintScreen": "print", "Pause": "pause"}


def qcode(name):
    """A MartyKey name as QEMU's qcode."""
    if name in _KEYS:
        return _KEYS[name]
    m = re.match(r"^Key([A-Z])$", name)
    if m:
        return m.group(1).lower()
    m = re.match(r"^Digit([0-9])$", name)
    if m:
        return m.group(1)
    m = re.match(r"^Numpad([0-9])$", name)
    if m:
        return "kp_" + m.group(1)
    m = re.match(r"^F([0-9]{1,2})$", name)
    if m:
        return "f" + m.group(1)
    raise om.MartyError("%s: no QEMU qcode for the key %r" % (TAG, name))


def machine_args(machine):
    """QEMU arguments for a MartyPC machine NAME, or a MartyError for what
    QEMU cannot be (see the module note)."""
    n = (machine or "").lower()
    for card in ("herc", "mda", "both", "ega"):
        if re.search(r"(^|_)%s(_|$)" % card, n):
            raise om.MartyError(
                "%s: %r asks for a %s and QEMU has only its VGA - run this "
                "row on MartyPC, or name a _vga machine (docs/TESTING.md)"
                % (TAG, machine, card.upper()))
    if re.search(r"(^|_)cga(_|$)", n) and "vga" not in n:
        # os88ui.boot's DEFAULT machine is a CGA one, named by rows that do
        # not care which card it is - so this is said, not refused: the
        # kernel finds QEMU's VGA and drives it as one, every os88ui verb
        # reads the guest's own geometry, and only a CGA PIXEL assertion
        # cannot hold (`make test VIDEO=cga` is the CGA path on QEMU)
        sys.stderr.write("%s: %s - QEMU has a VGA, and the kernel will drive "
                         "it as one\n" % (TAG, machine))
    if re.search(r"_(\d+)k(_|$)", n) and not re.search(r"_640k(_|$)", n):
        raise om.MartyError("%s: %r is a small machine and QEMU's pc has "
                            "640KB of conventional memory" % (TAG, machine))
    args = ["-machine", "pc,vmport=off", "-m", "16", "-vga", "std"]
    if "_ps2" not in n:
        args += ["-chardev", "msmouse,id=m0", "-serial", "chardev:m0"]
    if "_ne2k" in n:
        args += ["-netdev", "user,id=n0",
                 "-device", "ne2k_isa,netdev=n0,iobase=0x300,irq=3"]
    if "_sb" in n:
        args += ["-audiodev", "none,id=snd0",
                 "-device", "sb16,audiodev=snd0,iobase=0x220,irq=7,dma=1"]
    return args


class _Gdb(object):
    """QEMU's GDB stub on a Unix socket: the one door QEMU has for a memory
    WRITE (tests/os88qemu.py `poke`'s reason). Attaching stops the guest;
    `D` detaches and resumes it."""

    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.settimeout(15)
        self.s.connect(path)
        self.buf = b""

    def packet(self, body):
        b = body.encode("ascii")
        self.s.sendall(b"$" + b + b"#%02x" % (sum(b) & 0xFF))
        while True:
            while b"#" not in self.buf or \
                    len(self.buf) < self.buf.index(b"#") + 3:
                got = self.s.recv(65536)
                if not got:
                    raise om.MartyError("%s: the GDB stub hung up" % TAG)
                self.buf += got
            self.buf = self.buf.lstrip(b"+")
            if self.buf.startswith(b"$"):
                end = self.buf.index(b"#")
                reply, self.buf = self.buf[1:end], self.buf[end + 3:]
                self.s.sendall(b"+")
                return reply.decode("ascii", "replace")
            self.buf = self.buf[1:]

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


class ApiQemu(om.Marty):
    """`os88marty.Marty` - the TEST API - answered by a private QEMU: see the
    module note. `m.qmp(cmd)` is one HMP command line, for a row that needs
    QEMU itself."""

    qemu = True
    mega = False

    def __init__(self, run_dir, a, b, machine, label=None, extra=(),
                 paused=False, hd=None):
        self.run_dir = run_dir
        self.label = label
        self.machine = machine
        self.addr = "qemu:%s" % os.path.basename(run_dir)
        self.port = None
        self._log = []
        self._go = None
        self._pumping = 0
        self._planes_verb = False
        self._pipeline = False
        self._proc = None
        self._rec = None
        self._lock = threading.RLock()
        self._closed = False
        self._stops = 0
        self._btn = 0
        self._live = {}
        self._src = {}
        self.sock = os.path.join(run_dir, "qmp.sock")
        self.gdbsock = os.path.join(run_dir, "gdb.sock")
        pidf = os.path.join(run_dir, "qemu.pid")
        cmd = [BIN] + machine_args(machine)
        for d, img in ((0, a), (1, b)):
            if not img:
                continue
            dst = os.path.join(run_dir, "%s.img" % "ab"[d])
            shutil.copyfile(img, dst)
            self._live[d], self._src[d] = dst, img
            cmd += ["-drive", "file=%s,format=raw,if=floppy,index=%d"
                    % (dst, d)]
        if hd:
            cmd += ["-drive", "file=%s,format=raw,if=ide,index=0,media=disk"
                    % hd]
        cmd += ["-boot", "c" if hd and not a else "a", "-display", "none",
                "-qmp", "unix:%s,server,nowait" % self.sock,
                "-gdb", "unix:%s,server=on,wait=off" % self.gdbsock,
                "-daemonize", "-pidfile", pidf] + list(extra)
        if paused:
            cmd.append("-S")
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=run_dir)
        if r.returncode:
            raise om.MartyError("%s: QEMU would not start: %s"
                                % (TAG, (r.stderr or r.stdout).strip()[-600:]))
        self.pid = int(open(pidf).read().strip())
        atexit.register(self.close)
        t0 = time.time()
        while not os.path.exists(self.sock):
            if time.time() - t0 > 30:
                raise om.MartyError("%s: no QMP socket after 30s" % TAG)
            time.sleep(0.02)            # (the socket appearing: a HOST event)
        self._c = qmpc.Client(self.sock)
        self._state = "paused" if paused else "running"
        self._last = self._ticks()
        self._n = 0

    # ---- QMP ------------------------------------------------------------------
    def qmp(self, line):
        """One HMP command line, on the held connection; its text."""
        with self._lock:
            r = self._c.send({"execute": "human-monitor-command",
                              "arguments": {"command-line": line}})
        if "error" in r:
            raise om.MartyError("%s: %s: %r" % (TAG, line, r["error"]))
        return r.get("return", "")

    def _send(self, obj):
        with self._lock:
            r = self._c.send(obj)
        if "error" in r:
            raise om.MartyError("%s: %s: %r" % (TAG, obj.get("execute"),
                                                r["error"]))
        return r.get("return")

    def _input(self, evs):
        """input-send-event - which QEMU REFUSES on a stopped VM ("VM not
        running"), and os88mouse steps a gesture on a paused guest. So a
        held machine is let go for the send alone: cont, send, stop - a
        millisecond or so of guest time, where MartyPC queues the packet
        with no time passing at all."""
        with self._lock:
            held = self._state != "running"
            if held:
                self.qmp("cont")
            try:
                self._send({"execute": "input-send-event",
                            "arguments": {"events": evs}})
            finally:
                if held:
                    self.qmp("stop")

    # ---- memory ---------------------------------------------------------------
    def _read(self, addr, n):
        p = os.path.join(self.run_dir, "m.bin")
        # (pmemsave: one command whatever the length, where `xp` is text a
        # byte; the filename QUOTED, or HMP parses /tmp/... as an expression)
        self.qmp('pmemsave 0x%X %d "%s"' % (addr, n, p))
        with open(p, "rb") as f:
            return f.read()

    def _write(self, addr, data):
        with self._lock:
            g = _Gdb(self.gdbsock)
            try:
                g.packet("?")                   # attached: the guest held
                for i in range(0, len(data), 1024):
                    r = g.packet("M%x,%x:%s" % (addr + i, len(data[i:i + 1024]),
                                               data[i:i + 1024].hex()))
                    if r != "OK":
                        raise om.MartyError("%s: a write at %#x answered %r"
                                            % (TAG, addr + i, r))
                g.packet("D")                   # detach - which RESUMES
            finally:
                g.close()
            if self._state != "running":        # ...so a held one is held
                self.qmp("stop")                # again
        return len(data)

    # ---- the clock --------------------------------------------------------------
    def _ticks(self):
        b = self._read(0x46C, 2)
        return b[0] | (b[1] << 8)

    def _cycles(self):
        """Guest ticks since launch, as GUEST_HZ cycles (the module note)."""
        with self._lock:
            t = self._ticks()
            d = (t - self._last) & 0xFFFF
            if d:
                self._n += 1 if d > 256 else d  # (SET by POST, not counted)
                self._last = t
            return int(self._n * TICKCYC)

    # ---- the server -------------------------------------------------------------
    def cmd(self, **kw):
        c = kw.pop("cmd")
        f = getattr(self, "_c_" + c, None)
        if f is None:
            raise om.MartyError(
                "`%s` is a MartyPC instrument the QEMU backend does not have "
                "(tools/os88qemuapi.py) - this row is not portable to QEMU as "
                "it stands" % c)
        r = dict(f(**kw) or {})
        r["ok"] = True
        return r

    def _status(self):
        return {"state": self._state, "cycles": self._cycles(),
                "stops": self._stops, "cs": 0, "ip": 0, "flat_ip": 0}

    def _c_ping(self, **_):
        return {"pipeline": False}

    def _c_status(self, **_):
        return self._status()

    def _c_read(self, addr, len, **_):          # noqa: A002
        return {"data": self._read(addr, len).hex()}

    def _c_write(self, addr, data, **_):
        return {"written": self._write(addr, bytes.fromhex(data))}

    def _c_run(self, **_):
        frm = self._stops
        if self._state != "running":
            self.qmp("cont")
            self._state = "running"
        st = self._status()
        st["resumed_from"] = frm
        return st

    def _c_pause(self, **_):
        if self._state == "running":
            self.qmp("stop")
            self._state = "paused"
            self._stops += 1
        return self._status()

    def _c_advance(self, frames=None, cycles=None, card=None, **_):
        """Run a bounded amount of guest time, then hold. A tick or more is
        WHOLE ticks, counted on the guest's clock; less than a tick (a
        stepped gesture's 4 ms - os88mouse's DBL_STEP) is that much HOST
        time running, which without -icount is the guest's time too, near
        enough - the clock this backend can read has no finer grain."""
        secs = (cycles / float(GUEST_HZ) if cycles is not None
                else (frames or 1) / 60.0)
        if self._state != "running":
            self.qmp("cont")
            self._state = "running"
        if secs < 1.0 / TICK_HZ:
            time.sleep(max(secs, 0.002))
        else:
            need = int(secs * TICK_HZ + 0.5)
            c0 = self._cycles()
            t0 = time.monotonic()
            while self._cycles() - c0 < need * TICKCYC - 1:
                if time.monotonic() - t0 > om.GUEST_STALL + secs * 4:
                    break                       # (a clock that stands: the
                time.sleep(0.002)               # caller's stall watch says)
        st = self._c_pause()
        st["advanced_cycles"] = int(secs * GUEST_HZ)
        return st

    def _c_key(self, key, down=True, up=True, **_):
        q = qcode(key)

        def ev(dn):
            self._input([{"type": "key", "data": {
                "down": dn, "key": {"type": "qcode", "data": q}}}])
        if down:
            t0 = self._read(0x41C, 2)
            ev(True)
            # THE RELEASE ONLY ONCE THE ROM HAS STORED THE PRESS, on a
            # running guest (tests/os88qemu.py `key`: a starved guest takes a
            # queued pair at one IRQ1 and the kernel's 8042 peek loses one) -
            # bounded at a few ticks: a shift key stores nothing
            if up and self._state == "running":
                c0 = self._cycles()
                while self._read(0x41C, 2) == t0 and \
                        self._cycles() - c0 < 4 * TICKCYC:
                    time.sleep(0.002)
        if up:
            ev(False)
        self._log.append({"cycles": self._cycles(), "kind": "key",
                          "args": {"key": key, "down": down, "up": up}})
        return {"cycles": self._cycles()}

    def _c_mouse(self, dx=0, dy=0, l=False, r=False, wz=0, **_):
        evs = []
        if dx:
            evs.append({"type": "rel", "data": {"axis": "x", "value": dx}})
        if dy:
            evs.append({"type": "rel", "data": {"axis": "y", "value": dy}})
        if wz:
            evs.append({"type": "btn", "data": {
                "down": True, "button": "wheel-up" if wz < 0 else "wheel-down"}})
        want = (1 if l else 0) | (2 if r else 0)
        for bit, name in ((1, "left"), (2, "right")):
            if (want ^ self._btn) & bit:
                evs.append({"type": "btn", "data": {"down": bool(want & bit),
                                                    "button": name}})
        self._btn = want
        if evs:
            self._input(evs)
        self._log.append({"cycles": self._cycles(), "kind": "mouse",
                          "args": {"dx": dx, "dy": dy, "l": l, "r": r}})
        return {"cycles": self._cycles()}

    def _c_regs(self, **_):
        t = self.qmp("info registers")
        out = {}
        for n in ("ax", "bx", "cx", "dx", "sp", "bp", "si", "di"):
            m = re.search(r"E%s=([0-9a-fA-F]+)" % n.upper(), t)
            out[n] = int(m.group(1), 16) & 0xFFFF if m else 0
        m = re.search(r"EIP=([0-9a-fA-F]+)", t)
        out["ip"] = int(m.group(1), 16) & 0xFFFF if m else 0
        m = re.search(r"EFL=([0-9a-fA-F]+)", t)
        out["flags"] = int(m.group(1), 16) & 0xFFFF if m else 0
        for s in ("cs", "ds", "es", "ss", "fs", "gs"):
            m = re.search(r"^%s =([0-9a-fA-F]+)" % s.upper(), t, re.M)
            out[s] = int(m.group(1), 16) if m else 0
        return out

    # ---- the glass ----------------------------------------------------------------
    def _video(self, card=None):
        if card not in (None, "pri", 0, "0"):
            raise om.MartyError("%s: one card (QEMU's VGA), not %r"
                                % (TAG, card))
        mode = self._read(0x449, 1)[0]
        text = mode in (0, 1, 2, 3, 7)
        return {"idx": 0, "type": "VGA", "primary": True,
                "mode": "Mode%02X" % mode, "graphics": not text,
                "field_w": 720 if text else 640, "field_h": 400 if text else 480,
                "frames": 1}

    def _c_video(self, card=None, **_):
        return self._video(card)

    def _c_cards(self, **_):
        return {"cards": [self._video()]}

    def video(self, card=None):
        return self._video(card)

    def _c_screen(self, card=None, **_):
        v = self._video(card)
        if v["graphics"]:
            return {"rows": []}
        mode = int(v["mode"][4:], 16)
        cols = 40 if mode in (0, 1) else 80
        raw = self._read(0xB0000 if mode == 7 else 0xB8000, cols * 25 * 2)
        return {"rows": [bytes(c if c >= 32 else 32 for c in raw[
            r * cols * 2:(r + 1) * cols * 2:2]).decode("cp437").rstrip()
            for r in range(25)]}

    def _c_fbuf(self, aperture=0, card=None, **_):
        self._video(card)
        p = os.path.join(self.run_dir, "fbuf.ppm")
        self.qmp('screendump "%s"' % p)
        with open(p, "rb") as f:
            data = f.read()
        m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
        if not m:
            raise om.MartyError("%s: screendump is not a P6 PPM" % TAG)
        w, h = int(m.group(1)), int(m.group(2))
        return {"w": w, "h": h, "data": data[m.end():m.end() + w * h * 3].hex()}

    def _c_disk(self, reset=False, **_):
        return {}                       # (no FDC counters: the module note)

    def _c_reset(self, **_):
        self.qmp("system_reset")
        return self._c_run()

    def _c_quit(self, **_):
        self.close()
        return {}

    def disks(self):
        return [{"drive": d, "path": self._live.get(d)} for d in (0, 1)]

    def flush(self, drive=0, path=None, fmt="raw"):
        """The drive's live copy (QEMU writes it in place), copied to PATH."""
        src = self._live[drive]
        if path:
            shutil.copy(src, path)
        return {"path": path or src}

    def close(self):
        if self._closed:
            return
        self._closed = True
        try:
            self._c.close()
        except Exception:                       # noqa: BLE001
            pass
        if self.pid:
            try:
                os.kill(self.pid, signal.SIGTERM)
                import os88qemu
                os88qemu.gone(self.pid)
            except OSError:
                pass
            self.pid = None
        if os.environ.get("OS88_QEMU_KEEP"):
            print("%s: kept %s" % (TAG, self.run_dir), file=sys.stderr)
        else:
            shutil.rmtree(self.run_dir, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def __repr__(self):
        return "<ApiQemu %s pid %s>" % (self.run_dir, self.pid)


def _run_dir():
    base = os.environ.get("OS88_QEMU_DIR")
    if base:
        os.makedirs(base, exist_ok=True)
    # /tmp BY NAME when no runner dir: a Unix socket path is 108 bytes at
    # most and a worktree's build/tmp/ spends most of them
    return tempfile.mkdtemp(prefix="oqa-", dir=base or "/tmp")


def launch(image, apps=None, machine="", boot=True, label=None, extra=(),
           run_dir=None, **_):
    """os88marty.launch's shape on QEMU: THE ROW'S OWN IMAGES (copied) and
    its own kernel's map (os88sym's normal path). `boot=True` returns once
    the kernel has reached its desktop (os88marty.kernel_up); `boot=False`
    launches PAUSED, for os88ui.boot(quick=True) to run and gate itself.
    `extra` takes os88marty's `--mount hd:0:<path>` for a fixed disk; any
    other entry is a QEMU argument."""
    extra = list(extra or ())
    hd = None
    if "--mount" in extra:
        i = extra.index("--mount")
        spec = extra[i + 1]
        del extra[i:i + 2]
        if not spec.startswith("hd:0:"):
            raise om.MartyError("%s: --mount %s - only hd:0: here" % (TAG, spec))
        hd = spec[5:]
    rd = _run_dir()
    m = ApiQemu(rd, image, apps, machine, label=label, extra=extra,
                paused=boot is False, hd=hd)
    if boot is not False:
        om.kernel_up(m)
    return m


def main(argv):
    """`os88qemuapi.py IMAGE [APPS] [MACHINE]` - boot one, print what the
    API reads back, quit: the smoke test of this module."""
    if not argv:
        print(__doc__)
        return 2
    with launch(argv[0], apps=argv[1] if len(argv) > 1 else None,
                machine=argv[2] if len(argv) > 2 else "os8088_xt_vga") as m:
        st = m.status()
        print("%s: up at %.1f guest s, %s" % (TAG, st["cycles"] / GUEST_HZ, m))
        print("  video %r" % (m.video(),))
        x, y = m.read(m.sym("mouse_x"), 4)[0:4:2]
        print("  mouse_x/mouse_y %d %d" % (x, y))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
