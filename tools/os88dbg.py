#!/usr/bin/env python3
"""os88dbg: drive DEBUG.DRV's serial monitor (SPEC.md 58).

    make test DBG=1
    python3 tools/os88dbg.py build/dbg.sock m 60 0 20      # one command
    python3 tools/os88dbg.py build/dbg.sock                # a REPL
    python3 tools/os88dbg.py /tmp/os88dbg -            # 86Box's FIFO pair
    python3 tools/os88dbg.py /dev/ttyUSB0 --baud 9600  # a real cable

Three transports, because the three places this has to work reach the guest
differently and none of them is a superset of the others:

  QEMU      a UNIX socket chardev.  `make test DBG=1` opens it.
  86Box     the `pipe` chardev, which on Unix appends .in and .out to the
            configured path and mkfifo's them (src/char/char_pipe.c).  Pass
            the PREFIX; this opens both.  86Box may also be pointed straight
            at a pty, in which case pass the pty and it is opened like a
            serial device.
  real iron a serial port.  --baud matters here and ONLY here: the emulator
            transports are FIFOs with no line rate at all, which is what
            makes SPEC.md 58.4's divisor switch free on them.

The importable half is the point as much as the CLI - a scripted check wants
`Dbg(...).mem(0x60, 0, 16)` and an assert, not a transcript to eyeball:

    from os88dbg import Dbg
    with Dbg("build/dbg.sock") as d:
        assert d.mem(0x60, 0x0C, 2) != b"\\xff\\xff"     # boot_ticks stamped

WHAT IT WILL NOT DO. There is no symbol lookup here, deliberately, and it is
not an oversight: docs/FIELD-MACHINES.md's second dump rule is "re-derive
every offset from a listing of the exact commit", and a table of offsets
baked into a tool is exactly the thing that rule exists to forbid. Use
--listing to point at a `nasm -l` listing of the build you are actually
running and names resolve from that, or pass numbers.
"""
import argparse
import os
import re
import select
import socket
import stat
import sys
import time

DEFAULT_TIMEOUT = 5.0


class DbgError(Exception):
    pass


class _Transport:
    """Read/write bytes, however this guest is reached."""

    def close(self):
        pass


class _Socket(_Transport):
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.s.setblocking(False)

    def write(self, data):
        self.s.setblocking(True)
        try:
            self.s.sendall(data)
        finally:
            self.s.setblocking(False)

    def read(self, timeout):
        r, _, _ = select.select([self.s], [], [], timeout)
        if not r:
            return b""
        try:
            return self.s.recv(4096)
        except BlockingIOError:
            return b""

    def close(self):
        self.s.close()


class _Fds(_Transport):
    """A read fd and a write fd: 86Box's FIFO pair, or one pty opened twice.

    The FIFOs are opened NONBLOCK because a FIFO's open() blocks until the
    other end is there, and 86Box is the other end - so a blocking open
    against a machine that has not attached the driver yet hangs the tool
    rather than reporting it.
    """

    def __init__(self, rpath, wpath):
        self.rfd = os.open(rpath, os.O_RDONLY | os.O_NONBLOCK)
        try:
            self.wfd = os.open(wpath, os.O_WRONLY | os.O_NONBLOCK)
        except OSError as e:
            os.close(self.rfd)
            raise DbgError(f"{wpath}: {e}. Is 86Box running with the pipe "
                           f"chardev on this path?") from e

    def write(self, data):
        while data:
            _, w, _ = select.select([], [self.wfd], [], DEFAULT_TIMEOUT)
            if not w:
                raise DbgError("write timed out: nothing is reading the pipe")
            data = data[os.write(self.wfd, data):]

    def read(self, timeout):
        r, _, _ = select.select([self.rfd], [], [], timeout)
        if not r:
            return b""
        return os.read(self.rfd, 4096)

    def close(self):
        os.close(self.rfd)
        os.close(self.wfd)


class _Serial(_Transport):
    def __init__(self, path, baud):
        import termios
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.termios = termios
        speed = getattr(termios, f"B{baud}", None)
        if speed is None:
            raise DbgError(f"unsupported baud {baud}")
        a = termios.tcgetattr(self.fd)
        a[0] = a[1] = a[3] = 0                     # iflag, oflag, lflag: raw
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        a[4] = a[5] = speed                        # ispeed, ospeed
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.baud = baud

    def set_baud(self, baud):
        speed = getattr(self.termios, f"B{baud}", None)
        if speed is None:
            raise DbgError(f"unsupported baud {baud}")
        a = self.termios.tcgetattr(self.fd)
        a[4] = a[5] = speed
        self.termios.tcsetattr(self.fd, self.termios.TCSADRAIN, a)
        self.baud = baud

    def write(self, data):
        while data:
            data = data[os.write(self.fd, data):]

    def read(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return b""
        return os.read(self.fd, 4096)

    def close(self):
        os.close(self.fd)


def _open(path, baud):
    """Pick a transport from what `path` actually is."""
    try:
        mode = os.stat(path).st_mode
    except FileNotFoundError:
        # 86Box's pipe chardev: the prefix exists only as <path>.in/.out.
        if os.path.exists(path + ".out"):
            return _Fds(path + ".out", path + ".in")
        raise DbgError(f"{path}: no such socket, pipe or device")
    if stat.S_ISSOCK(mode):
        return _Socket(path)
    if stat.S_ISCHR(mode):
        return _Serial(path, baud)
    if stat.S_ISFIFO(mode):
        raise DbgError(f"{path} is one FIFO; pass the 86Box pipe PREFIX "
                       f"(without .in/.out) instead")
    # A plain path that also has .in/.out beside it is the 86Box prefix again.
    if os.path.exists(path + ".out"):
        return _Fds(path + ".out", path + ".in")
    raise DbgError(f"{path}: not a socket, pipe pair or serial device")


class Dbg:
    """One conversation with a machine running DEBUG.DRV."""

    def __init__(self, path, baud=9600, timeout=DEFAULT_TIMEOUT, listing=None):
        self.t = _open(path, baud)
        self.timeout = timeout
        self.buf = b""
        self.syms = _load_listing(listing) if listing else {}

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        self.t.close()

    # --- the wire ------------------------------------------------------------

    def command(self, line):
        """Send one command, return its single reply line.

        The monitor answers exactly one line per command, refusals included
        ('?'), which is what lets this read until a newline rather than
        guessing at a quiet period. A command that produces nothing at all is
        a driver that is not attached, and it times out saying so.
        """
        self.buf = b""                     # drop anything unsolicited (the
                                           # attach banner, a previous
                                           # timeout's late reply)
        self.t.write(line.encode("ascii") + b"\r")
        return self._readline()

    def _readline(self):
        deadline = time.time() + self.timeout
        while b"\n" not in self.buf:
            chunk = self.t.read(max(0.0, deadline - time.time()))
            if chunk:
                self.buf += chunk
            elif time.time() >= deadline:
                raise DbgError(
                    "timed out waiting for a reply. Is DEBUG.DRV ticked in "
                    "the Control Panel's Drivers page? It is not loaded by "
                    "default (SPEC.md 51.3).")
        line, _, self.buf = self.buf.partition(b"\n")
        return line.rstrip(b"\r").decode("ascii", "replace")

    # --- the commands --------------------------------------------------------

    def version(self):
        return self.command("v")

    def mem(self, seg, off, length):
        """Read `length` bytes of SEG:OFF, as bytes. 1..256 per call."""
        if not 1 <= length <= 256:
            raise ValueError("length must be 1..256; loop for more")
        seg = self.resolve(seg)
        off = self.resolve(off)
        reply = self.command(f"m {seg:x} {off:x} {length & 0xFF:x}")
        head, _, rest = reply.partition(" ")
        want = f"{seg:04x}:{off:04x}"
        if head != want:
            raise DbgError(f"expected {want}, got {reply!r}")
        data = bytes(int(b, 16) for b in rest.split())
        if len(data) != length:
            raise DbgError(f"asked for {length} bytes, got {len(data)}")
        return data

    def read(self, seg, off, length):
        """mem() without the 256-byte limit."""
        out = b""
        while length:
            n = min(256, length)
            out += self.mem(seg, off + len(out), n)
            length -= n
        return out

    def poke(self, seg, off, data):
        """Write bytes at SEG:OFF. Returns how many the guest took."""
        seg = self.resolve(seg)
        off = self.resolve(off)
        hexed = " ".join(f"{b:02x}" for b in data)
        reply = self.command(f"M {seg:x} {off:x} {hexed}")
        try:
            n = int(reply, 16)
        except ValueError:
            raise DbgError(f"unexpected reply {reply!r}") from None
        if n != len(data):
            raise DbgError(f"wrote {n} of {len(data)} bytes")
        return n

    def inb(self, port):
        reply = self.command(f"i {port:x}")
        try:
            return int(reply, 16)
        except ValueError:
            raise DbgError(f"unexpected reply {reply!r}") from None

    def outb(self, port, value):
        reply = self.command(f"o {port:x} {value:02x}")
        if reply != "ok":
            raise DbgError(f"unexpected reply {reply!r}")

    def divisor(self, div):
        """Change the line rate (SPEC.md 58.4). 1 = 115200, 12 = 9600.

        The reply comes back at the OLD rate and the guest drains its shift
        register before switching, so the only end that has to do anything is
        a real serial port - the emulator transports are FIFOs.
        """
        reply = self.command(f"s {div:x}")
        if reply != "ok":
            raise DbgError(f"unexpected reply {reply!r}")
        if isinstance(self.t, _Serial):
            self.t.set_baud(115200 // div)

    # --- symbols -------------------------------------------------------------

    def resolve(self, v):
        if isinstance(v, int):
            return v
        if v in self.syms:
            return self.syms[v]
        try:
            return int(v, 16)
        except ValueError:
            raise DbgError(f"{v!r} is neither hex nor a symbol in the "
                           f"listing") from None


# A nasm -f bin listing line is
#     <lineno> <ADDR> <hexbytes> [<depth>] <source>
# and the ADDR and hexbytes columns are BLANK on a line that emits nothing -
# which is exactly what a label alone on its line does. So there are two
# shapes to collect and the second is the one that catches people out:
#
#     577                                  boot_ticks:            <- no address
#     119 00000692 1000                <1> vid_popmax  dw 16      <- has one
#
# A bare label takes the address of the next byte emitted, so it is held
# PENDING and resolved by the next line that has an address. Guessing instead
# - taking the last address seen - puts every such label one item too early,
# silently, which is the failure docs/FIELD-MACHINES.md's second dump rule is
# about.
_LST = re.compile(r"^\s*\d+\s+([0-9A-F]{8})?\s")
_LABEL = re.compile(r"^\s*(?:<\d+>)?\s*([A-Za-z_.][A-Za-z0-9_.]*)\s*:?"
                    r"(?=\s|$)")
_DIRECTIVE = frozenset(("db", "dw", "dd", "dq", "resb", "resw", "resd",
                        "times", "equ", "incbin"))


def _load_listing(path):
    """Offsets out of a `nasm -l` listing, by name.

    Local labels (.foo) are skipped: they are not unique, and a debugger
    resolving one to whichever came last is worse than not resolving it.
    """
    syms = {}
    pending = []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = _LST.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16) if m.group(1) else None
        body = line[m.end():]
        # Step past the hex-bytes column, if this line emitted any.
        body = re.sub(r"^(?:[0-9A-F]{2,}|<[a-z-]+>)\s+", "", body)
        lm = _LABEL.match(body)
        name = lm.group(1) if lm else None
        if name and name.startswith("."):
            name = None                    # local: not unique, so not useful
        if addr is None:
            if name and ":" in body[:lm.end() + 1]:
                pending.append(name)       # resolved by the next emitted byte
            continue
        for p in pending:
            syms.setdefault(p, addr)
        pending = []
        if name:
            rest = body[lm.end():].strip().split()
            # A label with an address counts when it is followed by a data
            # directive or a colon; anything else on that line is an
            # instruction mnemonic sitting in the label column, not a label.
            if ":" in body[:lm.end() + 1] or (rest and rest[0].lower() in _DIRECTIVE):
                syms.setdefault(name, addr)
    return syms


def main():
    ap = argparse.ArgumentParser(
        description="Drive os8088's serial debug monitor (SPEC.md 58).")
    ap.add_argument("path", help="UNIX socket (QEMU), 86Box pipe prefix, "
                                 "or a serial device")
    ap.add_argument("args", nargs="*",
                    help="one command, e.g. `m 60 0 20`; omit for a REPL")
    ap.add_argument("--baud", type=int, default=9600,
                    help="real serial ports only (default 9600)")
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    ap.add_argument("--listing", help="a `nasm -l` listing of the running "
                                      "build, for symbol names")
    a = ap.parse_args()

    try:
        d = Dbg(a.path, baud=a.baud, timeout=a.timeout, listing=a.listing)
    except DbgError as e:
        print(f"os88dbg: {e}", file=sys.stderr)
        return 1

    try:
        if a.args:
            print(d.command(" ".join(a.args)))
            return 0
        print(f"os88dbg: {a.path} - {d.version()}")
        print("commands: v, m SEG OFF LEN, M SEG OFF BYTES.., i PORT, "
              "o PORT VAL, s DIV.  ^D to quit.")
        while True:
            try:
                line = input("os88> ").strip()
            except EOFError:
                print()
                return 0
            if not line:
                continue
            if line in ("q", "quit", "exit"):
                return 0
            print(d.command(line))
    except DbgError as e:
        print(f"os88dbg: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    finally:
        d.close()


if __name__ == "__main__":
    sys.exit(main())
