#!/usr/bin/env python3
"""os88marty: drive the MartyPC debug server (docs/MARTYPC-DEBUG.md).

    tools/martypc/build.sh
    cd build/martypc/run && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 \\
        ./martypc_headless --mount fd:0:media/floppies/os8088-360.img &

    python3 tools/os88marty.py 127.0.0.1:9001 status
    python3 tools/os88marty.py 127.0.0.1:9001 run
    python3 tools/os88marty.py 127.0.0.1:9001 dump 0060:0000 71624 -o /tmp/k.dump
    python3 tools/os88marty.py 127.0.0.1:9001            # a REPL

This talks to the EMULATOR, and that is the whole of what makes it usable:
it costs the guest not one cycle, needs nothing installed, answers on a
machine that has hard-frozen, and does the things a stub running INSIDE the
guest structurally cannot - single step, breakpoints, cycle counts,
registers.

There used to be a sibling that took the other side - DEBUG.DRV and
tools/os88dbg.py, a monitor in the guest answering over a UART, which needed
an IRQ, interrupts enabled and a machine healthy enough to service them. It
is gone (SPEC.md 58). ON REAL IRON THAT LEAVES A GAP AND IT IS WORTH KNOWING
WHERE THE FLOOR IS: SPEC.md 57's registry read out of a photograph by
tools/kfzread.py, and a MartyPC dump taken by whoever has the machine
(docs/FIELD-MACHINES.md). Neither single-steps anything.

MARTYPC IS CYCLE-ACCURATE AND IT IS NOT DISK-ACCURATE. It models the 8088's
instruction timing, prefetch queue and bus contention; it models no platter,
no seek and no interleave. PERFORMANCE.md Set 11 measured a 16KB read at
0.27s against the 5150's 8.07 - 30x fast - and a boot 17x fast. So any figure
with a disk in its path is wrong here, including plenty that is not obviously
about disks: a boot time, a package launch, a module load, a SYSTEM.CFG
write. And it will not catch a disk CORRECTNESS bug either - SPEC.md 18.91's
AL bug moved 148 sectors in 34 int 13h calls on the 5150 and 34 sectors in 6
calls under QEMU, correct and silent. For anything with a disk in it the
instrument is docs/FIELD-MACHINES.md's machine and there is no substitute.

THE DUMP IS SELF-VALIDATING, and that is the point of `verify`:
docs/FIELD-MACHINES.md's rule is that linear 0x600 onward is build/kernel.bin
byte for byte apart from writable state, so a diff proves you are running the
build you think you are AND hands you every live variable at its listing
offset with no instrumentation added. `verify` is that check as one command.
"""
import argparse
import json
import socket
import sys

DEFAULT_TIMEOUT = 60.0
KERNEL_SEG = 0x0060


class MartyError(Exception):
    pass


class Marty:
    """One conversation with a headless MartyPC."""

    def __init__(self, addr, timeout=DEFAULT_TIMEOUT):
        host, _, port = addr.rpartition(":")
        if not host:
            host, port = "127.0.0.1", addr
        try:
            self.s = socket.create_connection((host, int(port)), timeout=timeout)
        except OSError as e:
            raise MartyError(
                f"{addr}: {e}. Is martypc_headless running with "
                f"MARTYPC_DEBUG_ADDR set?") from None
        self.f = self.s.makefile("rwb")
        self._log = []          # every input, stamped with its guest cycle

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        try:
            self.f.close()
            self.s.close()
        except OSError:
            pass
        proc = getattr(self, "_proc", None)      # launch() gave us the process
        if proc is not None:                     # to own: end it, or the next
            self._proc = None                    # session inherits a survivor
            try:                                 # holding the port
                proc.kill()
                proc.wait(timeout=5)
            except Exception:
                pass

    def cmd(self, **kw):
        self.f.write((json.dumps(kw) + "\n").encode())
        self.f.flush()
        line = self.f.readline()
        if not line:
            raise MartyError("server closed the connection")
        r = json.loads(line)
        if not r.get("ok", False):
            raise MartyError(r.get("err", "refused"))
        return r

    # --- state ---------------------------------------------------------------

    def status(self):
        return self.cmd(cmd="status")

    def disk(self, reset=False):
        """What the FLOPPY CONTROLLER has been asked to do, from OUTSIDE.

        SPEC.md 18.94's DISKCNT block without the guest's help: no counters in
        the kernel, no knob-built binary, no test package on the floppy, so a
        SHIPPED image can be watched. That is what makes SPEC.md 18.91's class
        cheap to find - a kernel re-reading sectors it was already handed looks
        entirely correct from inside itself and shows up here as traffic
        nobody asked for. `reset=True` zeroes the counters, so a region of a
        session can be bracketed:

            m.disk(reset=True)          # ...do the thing...
            print(m.disk())             # {'reads': 6, 'read_sectors': 34, ...}

        `longest_run` near the track length is a kernel batching properly;
        `read_sectors` far above the file's own size is the 18.91 shape.
        """
        return self.cmd(cmd="disk", reset=bool(reset))

    def regs(self):
        return self.cmd(cmd="regs")

    def setreg(self, reg, value):
        return self.cmd(cmd="setreg", reg=reg, value=value)

    def cards(self):
        """Every video card this machine has, in `[[machine.video]]` order.

        The answer to "did my two-card config actually produce two cards",
        which nothing else here can ask: `video` on a machine whose second
        entry was dropped looks exactly like `video` on a machine that never
        had one. Each entry carries `idx` (what to pass as `card=`), `type`,
        `primary`, `mode`, `field_w`/`field_h` and `frames`.

        `frames` is the one to watch: a card nothing has PROGRAMMED sits at 0
        for ever, which is not the same as a card that is not there.
        """
        return self.cmd(cmd="cards")["cards"]

    def screen(self, card=None):
        """The video card's text rows, in text modes."""
        return self.cmd(cmd="screen", card=card)["rows"]

    def video(self, card=None):
        """Which card, its raster geometry, and its display apertures.

        `graphics` IS NOT TO BE TRUSTED ON VGA: the card's `mode_graphics`
        field is initialised to false and never assigned, so it answers false
        in mode 12h exactly as it does in mode 3. `field_w`/`field_h` are the
        honest question - 800x524 is mode 12h's raster and a text mode's is
        not.
        """
        return self.cmd(cmd="video", card=card)

    def flicker(self, frames=60, aperture=0, card=None):
        """Sample the glass once per DISPLAYED FRAME and price what a person
        would have seen (PERFORMANCE.md Part 3.1).

        Call it with the machine PAUSED and the action already injected, so
        the action lands inside the capture window instead of racing it:

            m.pause(); m.key("KeyA")
            r = m.flicker(frames=40)

        Returns the per-frame `changed` and `transient` counts, the bounding
        box of the transient pixels, and `settled` - which must be true, or
        every count was measured against a moving target.
        """
        return self.cmd(cmd="flicker", frames=frames, aperture=aperture,
                        card=card)

    def pace(self, frames=300, aperture=0, ignore=None, card=None):
        """Per-frame changed-pixel counts, for FRAME PACING (PERFORMANCE.md
        Part 3.2). Keeps two frames server-side, so `frames` can be large.

        `ignore` is an inclusive [x0,y0,x1,y1] excluded from every comparison —
        for a blinking cursor, which changes pixels on a clock of its own.
        """
        kw = {"cmd": "pace", "frames": frames, "aperture": aperture,
              "card": card}
        if ignore:
            kw["ignore"] = list(ignore)
        return self.cmd(**kw)

    def fbuf(self, aperture=0, card=None):
        """The card's RENDERED framebuffer as (width, height, rgb24 bytes).

        The complement of `vram`, and the only route that works on VGA: mode
        12h is four planes behind the Graphics Controller, so there is no flat
        framebuffer in guest memory to read. This asks the CARD what it
        rasterised, which is a different assertion - `vram` says the kernel
        wrote the right bytes, this says the machine put them on a screen -
        and it works on every adapter and in every mode.
        """
        r = self.cmd(cmd="fbuf", aperture=aperture, card=card)
        return r["w"], r["h"], bytes.fromhex(r["data"])

    def vram(self, kind=None):
        """The 1bpp framebuffer as (width, height, rows-of-bits).

        `read` resolves MMIO, so video RAM comes back like any other memory -
        no screendump, no HERCSEG relocation, and no reason to start QEMU just
        to look at the screen. `kind` is 'cga' or 'herc'; None asks the
        machine which card it has.

        VGA is deliberately absent, and that is about the LAYOUT rather than
        about MartyPC: mode 12h is four PLANES behind the Graphics
        Controller's Read Map Select, so it is not readable as flat memory at
        all. `fbuf` is the route there - it asks the card what it rasterised
        instead of asking memory what is in it.
        """
        if kind is None:
            # ASK THE CARD. Sniffing memory does not work: an unmapped
            # 0xB0000 reads as zeroes rather than erroring, so "is there
            # something at the MDA aperture" answers yes on a CGA-only
            # machine - which is exactly the wrong answer, silently.
            vt = self.cmd(cmd="video")["type"]
            kind = "cga" if vt == "cga" else "herc"   # MDA and Hercules share
                                                      # a layout and an aperture
        if kind == "cga":
            base, w, h, stride, banks = 0xB8000, 640, 200, 80, 2
        elif kind == "herc":
            base, w, h, stride, banks = 0xB0000, 720, 348, 90, 4
        else:
            raise MartyError("kind must be 'cga' or 'herc'")
        fb = self.read(base, banks * 0x2000)
        rows = []
        for y in range(h):
            # SPEC.md 39.3's banked layout, byte for byte the arithmetic in
            # tools/hercshot.py - so a picture from either route is the same
            # picture, and a shear means the KERNEL's bank arithmetic moved.
            off = (y % banks) * 0x2000 + (y // banks) * stride
            rows.append(bytearray((fb[off + (x >> 3)] >> (7 - (x & 7))) & 1
                                  for x in range(w)))
        return w, h, rows

    # --- memory --------------------------------------------------------------

    def read(self, addr, length):
        """Read `length` bytes from a flat address, in one call per 64KB."""
        out = bytearray()
        while length:
            n = min(1 << 16, length)
            r = self.cmd(cmd="read", addr=addr + len(out), len=n)
            out += bytes.fromhex(r["data"])
            length -= n
        return bytes(out)

    def readseg(self, seg, off, length):
        return self.read((seg << 4) + off, length)

    def sym(self, name, defines=()):
        """The flat address of a KERNEL symbol, from nasm's own map.

            m.read(m.sym("fpg_on"), 1)

        **The answer is FLAT** - `KERNEL_SEG*16 + offset`, which is what
        `read()` and an `exec` breakpoint's `addr` both take, and which is
        0x600 more than what `readseg(KERNEL_SEG, ...)` and an `execseg`
        breakpoint's `off` take. Both forms accept it without complaint and
        land 0x600 away, in real code, on an address that is simply never
        reached; `bp_exec()` exists so a breakpoint cannot get this wrong.

        Never take one out of `nasm -l`'s listing: for anything in `.bss` the
        listing prints a SECTION-RELATIVE address that is fixed up afterwards,
        so it is a plausible small number pointing into `.text` - and reading
        a byte from it succeeds and means nothing. os88sym.py has the story.
        """
        import os88sym
        return os88sym.linear(name, defines)

    def write(self, addr, data):
        return self.cmd(cmd="write", addr=addr, data=data.hex())["written"]

    def inb(self, port):
        return self.cmd(cmd="inb", port=port)["value"]

    def outb(self, port, value):
        return self.cmd(cmd="outb", port=port, value=value)

    # --- execution -----------------------------------------------------------

    def run(self):
        return self.cmd(cmd="run")

    def pause(self):
        return self.cmd(cmd="pause")

    def step(self, n=1, over=False):
        return self.cmd(cmd="step", n=n, over=over)

    def reset(self):
        return self.cmd(cmd="reset")

    def breakpoints(self, bps):
        """Replace the whole breakpoint set. `bps` is a list of dicts:

            {"type": "exec",    "addr": 0x600}      # flat CS<<4+IP
            {"type": "execseg", "seg": 0x60, "off": 0x1234}
            {"type": "mem",     "addr": 0x60C}      # any access
            {"type": "int",     "addr": 0x13}       # interrupt number
            {"type": "io",      "addr": 0x3F8}

        TWO WAYS TO ARM ONE AND SEE NOTHING, both of which cost a session
        here and neither of which errors:

        - **A HIT IS NOT `"paused"`.** `status()["state"]` reads
          `"breakpoint"`. Poll `stopped()` - or anything that tests
          `!= "running"`, which is what `until()` does - and never
          `== "paused"`, which is true only of an explicit `pause()` and so
          is false forever at a breakpoint that is firing perfectly.
        - **`sym()` is FLAT and `off` is an OFFSET.** `sym("wm_show")` on a
          kernel symbol answers `KERNEL_SEG*16 + offset`, so handing it
          straight to `execseg`'s `off` arms an address 0x600 past the one
          you meant - a real address, in real code, that simply is not
          reached. Use `bp_exec()` below, or subtract `KERNEL_SEG << 4`.
        """
        return self.cmd(cmd="bp", list=bps)["count"]

    def bp_exec(self, *targets):
        """Arm exec breakpoints on kernel SYMBOLS or flat addresses.

            m.bp_exec("wm_show", "wm_destroy")
            m.bp_exec(0x8133)

        The flat form is used throughout, so `sym()`'s answer goes in
        unmodified and the offset confusion above cannot happen. Replaces the
        whole set, like `breakpoints()`; call with no arguments to clear.
        """
        bps = []
        for t in targets:
            addr = self.sym(t) if isinstance(t, str) else int(t)
            bps.append({"type": "exec", "addr": addr})
        return self.breakpoints(bps)

    def stopped(self):
        """Is the guest NOT executing? True at a breakpoint and when paused.

        The test every wait on a breakpoint wants, and the one nobody writes
        the first time: `"breakpoint"` and `"paused"` are different states and
        only this covers both.
        """
        return self.status().get("state") != "running"

    def wait_stop(self, limit=20.0, poll=0.02):
        """Run until the guest stops, and answer WHY - or None on a timeout.

            if m.wait_stop(10): ...          # a breakpoint hit (or a pause)

        Returns the state string, so a caller can tell a breakpoint from a
        machine somebody else paused.
        """
        import time
        t0 = time.time()
        while time.time() - t0 < limit:
            st = self.status().get("state")
            if st != "running":
                return st
            time.sleep(poll)
        return None

    # --- input, through the REAL devices -------------------------------------
    #
    # No guest code is involved in either of these, which is the point: `key`
    # enters the emulator's keyboard buffer so the guest sees it through the
    # 8255 and int 09h, and `mouse` builds a real Microsoft 3-byte packet and
    # clocks it into the serial controller so the guest's own ISR decodes it.
    # A debug module poking [mouse_x] would skip the UART, the packet decoder
    # and SPEC.md 9.5's port contest - the code most likely to be wrong.

    # --- reproducibility: guest-time positioning (docs/SNAPSHOT-PLAN.md) -----

    def advance(self, frames=None, cycles=None, card=None):
        """Run a bounded amount of GUEST time and stop.

        USE THIS INSTEAD OF time.sleep FOR ANYTHING THAT MUST REPEAT. The
        emulator is bit-exact deterministic in guest time and runs at whatever
        multiple of real time the host manages, so a wall-clock wait lands at a
        different guest position every run - two instances given the same
        sleep(22) were measured 21.7 million cycles apart.

        `frames` is a question about ONE card and the two disagree on a
        two-card machine - 50Hz Hercules against 60Hz CGA - so `card` picks
        which one is counting. Absent, it is the primary, which is what it
        always was.
        """
        kw = {"cmd": "advance", "card": card}
        if frames is not None:
            kw["frames"] = frames
        if cycles is not None:
            kw["cycles"] = cycles
        return self.cmd(**kw)

    def input_log(self):
        """Every input this session delivered, with the guest cycle it landed
        at. Replaying these positions on a fresh machine reproduces the state
        exactly; replaying them by wall clock does not."""
        return list(self._log)

    def replay(self, log, settle=0):
        """Re-drive an input log on a fresh machine, positioning by CYCLES.

        The machine must start from the same image and be at a cycle count at
        or below the first entry's. `settle` frames are advanced at the end.
        """
        for e in log:
            here = self.status()["cycles"]
            if e["cycles"] > here:
                self.advance(cycles=e["cycles"] - here)
            if e["kind"] == "key":
                self.cmd(cmd="key", **e["args"])
            elif e["kind"] == "mouse":
                self.cmd(cmd="mouse", **e["args"])
        if settle:
            self.advance(frames=settle)
        return self.status()

    # --- fork snapshots (docs/SNAPSHOT-PLAN.md B) ----------------------------

    def snapshot(self):
        """Fork a holder process that freezes the machine exactly as it is.

        Returns {'id', 'pid', 'cycles'}. Nothing is serialized, so nothing can
        be left out - the holder is a copy-on-write image of the whole process.
        """
        return self.cmd(cmd="snapshot")

    def restore(self, snap_id, port):
        """Wake a holder onto `port` and return a Marty connected to it.

        THIS CONNECTION STAYS THE SNAPSHOT REGISTRY. The restored machine is a
        different process; keep this one to restore again. The holder re-forks
        itself first, so one snapshot can be restored any number of times.
        """
        r = self.cmd(cmd="restore", id=snap_id, port=port)
        for _ in range(100):
            try:
                return Marty("127.0.0.1:%d" % r["port"])
            except MartyError:
                import time as _t
                _t.sleep(0.1)
        raise MartyError("snapshot %d never came up on port %d" % (snap_id, port))

    def key(self, name, down=True, up=True):
        """One MartyKey by name: 'KeyA', 'Enter', 'Digit1', 'ArrowUp'."""
        return self.cmd(cmd="key", key=name, down=down, up=up)

    # The US layout's shifted characters, as (MartyKey name, needs shift).
    # MartyKey's vocabulary is the W3C KeyboardEvent.code set, so these are
    # the emulator's own names rather than a second table that has to agree
    # with an enum (debug_server.rs says why it takes names at all).
    _PUNCT = {
        ".": ("Period", 0), ",": ("Comma", 0), "/": ("Slash", 0),
        ";": ("Semicolon", 0), "'": ("Quote", 0), "[": ("BracketLeft", 0),
        "]": ("BracketRight", 0), "\\": ("Backslash", 0), "-": ("Minus", 0),
        "=": ("Equal", 0), "`": ("Backquote", 0),
        ":": ("Semicolon", 1), "?": ("Slash", 1), '"': ("Quote", 1),
        "<": ("Comma", 1), ">": ("Period", 1), "_": ("Minus", 1),
        "+": ("Equal", 1), "~": ("Backquote", 1), "{": ("BracketLeft", 1),
        "}": ("BracketRight", 1), "|": ("Backslash", 1),
        "!": ("Digit1", 1), "@": ("Digit2", 1), "#": ("Digit3", 1),
        "$": ("Digit4", 1), "%": ("Digit5", 1), "^": ("Digit6", 1),
        "&": ("Digit7", 1), "*": ("Digit8", 1), "(": ("Digit9", 1),
        ")": ("Digit0", 1),
    }

    def type_text(self, s):
        """ASCII through the keyboard, INCLUDING shifted characters.

        It was letters, digits, space and Enter, and the first thing that
        needed more was a URL: `no key mapping for ':'` ended a whole
        browser-fetch run before it had typed four characters. `:` and `/`
        are not exotic - they are what an address is made of - and an
        uppercase letter was quietly wrong rather than refused, since
        `Key` + ch.upper() with no shift held reaches the guest as lower case.

        SHIFT IS HELD ACROSS THE KEY and released after, because the debug
        server presses with `KeyboardModifiers::default()` - the modifier is a
        key that is down, exactly as it is on the real 8255, and not a flag on
        the press.
        """
        for ch in s:
            if ch == "\n":
                self.key("Enter")
            elif ch == "\t":
                self.key("Tab")
            elif ch == "\b":
                self.key("Backspace")   # ...and it was missing, which is not
                                        # exotic either: the first test to
                                        # DELETE something rather than type it
                                        # had nowhere to go
            elif ch == " ":
                self.key("Space")
            elif ch.isalpha() and ch.isascii():
                self._shifted("Key" + ch.upper(), ch.isupper())
            elif ch.isdigit():
                self.key("Digit" + ch)
            elif ch in self._PUNCT:
                name, sh = self._PUNCT[ch]
                self._shifted(name, sh)
            else:
                raise MartyError("no key mapping for %r" % ch)

    def ctrl(self, name):
        """One key with ControlLeft HELD across it - a control character.

        _shifted's shape and for its reason: the debug server presses with
        `KeyboardModifiers::default()`, so a modifier is a key that is DOWN
        exactly as it is on the real 8255, and not a flag on the press. There
        was no way to send one at all, and the first thing that wanted one was
        a terminal's escape key - which is Ctrl+] on every telnet client there
        has ever been.
        """
        self.key("ControlLeft", down=True, up=False)
        try:
            self.key(name)
        finally:
            self.key("ControlLeft", down=False, up=True)     # ...ALWAYS: a
                                                             # stuck modifier
                                                             # is a guest
                                                             # nobody can get
                                                             # back
    def _shifted(self, name, shift):
        """One key, optionally with ShiftLeft held down across it."""
        if not shift:
            return self.key(name)
        self.key("ShiftLeft", down=True, up=False)
        try:
            self.key(name)
        finally:
            self.key("ShiftLeft", down=False, up=True)  # ...ALWAYS: a stuck
                                                        # modifier is a guest
                                                        # nobody can get back

    def mouse(self, dx=0, dy=0, l=False, r=False):
        """One packet. dx/dy are RELATIVE and clamped to a signed byte."""
        rr = self.cmd(cmd="mouse", dx=dx, dy=dy, l=l, r=r)
        self._log.append({"cycles": rr.get("cycles", 0), "kind": "mouse",
                          "args": {"dx": dx, "dy": dy, "l": l, "r": r}})
        return rr

    def mouse_move(self, dx, dy, l=False, r=False, step=100):
        """A long move as several packets - a packet carries a signed byte."""
        while dx or dy:
            sx = max(-step, min(step, dx))
            sy = max(-step, min(step, dy))
            self.mouse(sx, sy, l, r)
            dx -= sx
            dy -= sy

    def click(self, l=True):
        """Press and release in place."""
        self.mouse(0, 0, l=l)
        self.mouse(0, 0)

    def history(self):
        return self.cmd(cmd="history")["history"]

    # --- floppies ------------------------------------------------------------

    def disks(self):
        """Every floppy drive: what is in it, and where it was mounted from.

        Each row: drive, present, writes, path, and cylinders/heads/format
        when a disk is in.

        `writes` IS NOT A DIRTY FLAG, however much it looks like one. It is
        fluxfox's `write_ct`, which is set to 1 at mount (so it never reads 0)
        and is never advanced for a raw sector image - the one call that would
        do it is commented out upstream. Measured: it read 1 through a Control
        Panel close that wrote three sectors. To ask whether the guest has
        written, flush and compare the bytes: `os88flush.Flush(marty=m).dirty()`.
        """
        return self.cmd(cmd="disks")["drives"]

    def flush(self, drive=0, path=None, fmt="raw"):
        """Write a drive's live image out to a file on the host.

        This is the eframe frontend's Save Floppy As, reached from the socket
        - and it is the only way the bytes ever leave: nothing in
        martypc_headless or marty_core writes an image back, so every sector
        os8088 writes lives and dies in RAM. Without it a scripted session can
        make the OS save a document and then has to ask the OS itself whether
        it worked, which cannot catch the bug where the writer and the reader
        agree on the same wrong thing.

        `path` defaults to the file the drive was mounted from, which under
        `launch()` is the session's private copy in the run tree - so a bare
        `m.flush(0)` persists the session's disk where the next tool will look
        for it. `fmt` is raw / imd / psi / pri / mfm / hfe / 86f; raw is the
        flat sector image every other tool here reads.

        PAUSE FIRST, and `os88flush.flush()` does. A flush is a read of the
        image at the instant it is asked for, so one taken in the middle of a
        multi-sector commit captures a volume that really is inconsistent -
        which afterwards reads as a corrupt disk rather than a mistimed grab.
        """
        return self.cmd(cmd="flush", drive=drive, path=path, format=fmt)

    def quit(self):
        return self.cmd(cmd="quit")


def _png(path, w, h, raw, colour_type):
    import struct, zlib

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, colour_type, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


# =============================================================================
# LAUNCHING ONE. Every scripted session needs a fresh emulator, and until this
# existed every one of them hand-rolled the same twenty lines - which is where
# essentially all of this harness's lost time has gone. The failures do not
# announce themselves:
#
#   * A SURVIVOR FROM A PREVIOUS RUN KEEPS PORT 9001. The new emulator cannot
#     bind, says so only in its log, and the client then drives the STALE
#     machine - a different image, at a different point in its boot. It reads
#     as the guest hanging, or as a change that did nothing.
#   * `pkill -f martypc_headless` IS NOT RELIABLE HERE. The pattern matches
#     the calling shell's own command line, so it can kill the caller instead
#     of (or as well as) the target; the `[m]artypc` bracket dodge fixes that
#     one and not the rest. This kills by PID, read out of /proc.
#   * `pgrep -f <pattern>` SELF-MATCHES for the same reason, so the obvious
#     `until ! pgrep -f foo; do sleep; done` wait never finishes.
#   * THE SERVER TAKES ONE CLIENT. A second connection does not error, it
#     HANGS until the read times out - so a script that wants both the mouse
#     driver and the framebuffer must share one Marty, never build two.
#   * TWO CONCURRENT SESSIONS ON ONE BOX DESTROY EACH OTHER, BY CONSTRUCTION.
#     The sweep below kills EVERY martypc_headless before starting its own -
#     which is right for the first hazard above and fatal for two people, two
#     terminals or two agents working in one checkout: each new launch kills
#     the other's running machine. The victim sees BrokenPipeError or
#     ConnectionResetError from a socket to a process that no longer exists,
#     and every symptom points at the emulator or the guest while the cause is
#     somewhere else entirely. `serial=True` in tests/suite.py does NOT cover
#     it: that orders rows within ONE runner process (tools/os88test.py), and
#     nothing coordinates two runners.
#
#     Two things make it hard to see. Because the sweep kills BEFORE it
#     starts, the two emulators never coexist, so a `ps` that only ever shows
#     one process looks like proof that nothing is competing - it is not.
#     And a MartyPC that loses the race does NOT exit: it reports
#     "Could not bind debug server to 127.0.0.1:9001: Address already in use"
#     to its LOG ONLY and runs on headless and unreachable forever, which is
#     where survivors come from in the first place.
#     The way to be sure is the exit code: hold an idle emulator, and if it
#     dies with -9 while your own close() has not run, another launch() killed
#     it. THE FIX IS NOT TO RETRY - a retry races the same way. It is to not
#     run two.
#
# All of that is handled once, here, and loudly: every failure raises with a
# sentence saying which of the above it was.
# =============================================================================

def _keep_pid(cmd):
    """Does this command line belong to the EMULATOR, not to a shell or a
    client that merely names it? The one rule both platforms below share."""
    if "martypc_headless" not in cmd:
        return False
    if "pgrep" in cmd or "pkill" in cmd:
        return False
    parts = cmd.split()
    return bool(parts) and parts[0].endswith("martypc_headless")


def _marty_pids():
    """Every running martypc_headless, by PID, without pgrep's self-match.

    TWO READERS, because /proc IS NOT PORTABLE AND DARWIN HAS NONE. This
    harness ran on Linux only until a macOS box tried it and every marty row
    died in launch() with FileNotFoundError('/proc') before starting anything
    - which reads as the emulator being broken rather than as the process
    lister being Linux-only, and macOS is a supported dev platform here
    (tools/setup-macos.sh). The Darwin branch reads the same two fields from
    `ps` and applies the same _keep_pid rule, so BOTH still kill by PID rather
    than by pattern - which is the discipline the banner above insists on and
    the whole reason this function exists instead of a pkill.
    """
    import os
    import subprocess
    out = []
    if os.path.isdir("/proc"):
        for ent in os.listdir("/proc"):
            if not ent.isdigit():
                continue
            try:
                with open("/proc/%s/cmdline" % ent, "rb") as f:
                    cmd = f.read().replace(b"\0", b" ").decode("utf-8",
                                                               "replace")
            except OSError:
                continue
            if _keep_pid(cmd):
                out.append(int(ent))
        return out

    # Darwin (and any other /proc-less unix): `ps` is the only process table.
    # -A every process, -o with trailing `=` to suppress the headers, so each
    # line is "<pid> <argv joined>" and nothing has to be parsed around a
    # column title that localises.
    try:
        ps = subprocess.run(["ps", "-Ao", "pid=,args="],
                            capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError):
        return out
    for line in ps.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        pid, _, cmd = line.partition(" ")
        if not pid.isdigit():
            continue
        if _keep_pid(cmd.strip()):
            out.append(int(pid))
    return out


def _port_free(host, port, tries=60, gap=0.5):
    """True once nothing answers on the port. A survivor holding it is the
    single commonest reason a scripted session drives the wrong machine."""
    import time
    for _ in range(tries):
        s = socket.socket()
        s.settimeout(0.3)
        try:
            s.connect((host, port))
            s.close()
            time.sleep(gap)
        except OSError:
            s.close()
            return True
    return False


MBAR_H = 20                     # SPEC.md 12: the same on every adapter


DOCK_H = 24                     # SPEC.md 30: the strip, on every adapter


class _Screen(object):
    """One reading of the screen: comparable bytes, and the three bands the
    boot gate asks about, all taken from THE SAME FRAME.

    That last part is the point of the class. The gate and the stillness test
    used to read the screen independently, a round trip apart - and this
    emulator runs the guest several times faster than real time, so "a round
    trip apart" is tens of milliseconds of GUEST time, which is most of a
    desktop paint on a 4.77MHz machine. Two reads can therefore report a state
    that never existed: measured, a probe built that way reported the menu bar
    up while the same screen was 26% lit, on a machine whose desktop is 56%
    and whose bar goes up last. One read, two questions - and one `video` call
    rather than three, which is the same economy.

    `card` picks one on a two-card machine, and it is not optional there:
    os8088 need not be driving MartyPC's PRIMARY. A `make VIDEO=herc` kernel
    on `os8088_5150_both_gla` draws on the Hercules while the config's first
    `[[machine.video]]` is the CGA, so a gate that asks the primary watches a
    card nothing is drawing on and times out - which reads exactly like a
    machine that never booted.

    `vram` on the 1bpp cards and `fbuf` on VGA, which is where the two
    functions already split: mode 12h is four planes behind the Graphics
    Controller and not readable as flat memory, and everything else is.

    ON THE 1bpp CARDS IT IS A CHOICE RATHER THAN A NECESSITY, and the reason
    given here used to be wrong. `fbuf` is NOT dead on Hercules - measured, a
    live desktop is 54.2% lit through it against 55.7% through `vram`
    (docs/MARTYPC-DEBUG.md). `vram` is still the right one for a boot gate
    because it is EXACT: the card's rendered frame is not in the GUEST's
    coordinate system - 720x350 over a 720x348 framebuffer, at dx = -16,
    dy = +2 - and a gate that counts pixels in a named band wants the band it
    named.
    """

    def __init__(self, m, card=None):
        vt = m.video(card=card)["type"]
        if vt in ("cga", "mda", "hercules"):
            w, h, rows = m.vram("cga" if vt == "cga" else "herc")
            lit = [sum(r) for r in rows]
            self.bytes = b"".join(bytes(r) for r in rows)
        else:
            w, h, d = m.fbuf(card=card)
            self.bytes = d
            lit = [sum(1 for x in range(w) if d[(y * w + x) * 3])
                   for y in range(h)]
        self.w, self.h = w, h
        self.field = sum(lit[0:MBAR_H - 1]) / float(w * (MBAR_H - 1))
        self.rule = lit[MBAR_H - 1] / float(w)
        self.dock = sum(lit[h - DOCK_H:]) / float(w * DOCK_H)

    def __repr__(self):
        return ("menu field %.0f%% lit, its rule %.0f%%, dock strip %.0f%%"
                % (100 * self.field, 100 * self.rule, 100 * self.dock))


def saver_running(m):
    """Is SPEC.md 79's idle screen saver on the glass right now?

    One byte, and it is the difference between "this gate found a bug" and
    "this gate waited out its limit for five minutes of guest idle". The
    saver DRAWS - a starfield, shapes, fish - so a screen with it up never
    settles and never will.
    """
    try:
        return bool(m.read(_syms().linear("blk_on"), 1)[0])
    except Exception:
        return False            # kern_small, a knob kernel, a stale map: the
                                # diagnostic is worth nothing if it can raise


def no_saver(m):
    """Turn the screen saver OFF for the rest of this session, and say so.

    [ss_idle] == 0 is the setting's own OFF (blank.inc's `zero minutes: OFF,
    and that is a setting and not a degenerate period`), so this is what the
    Control Panel writes for Never rather than a poke around the feature.
    Both halves, because ss_mins2idle would put the period back the moment
    anything touched a setting.

    CALL IT IN ANY GATE THAT DRIVES FOR MORE THAN ~5 GUEST MINUTES, which on
    a host running the guest at 4x is a bit over a minute of navigation with
    no input. tests/blitplane.py is the worked example, and the reason this
    exists: the saver came up in the middle of its run, `settle` could never
    return because the saver animates, and the gfx_blit4 breakpoint it had
    armed caught a SAVER shape instead of Paint's canvas. Neither symptom
    named the saver.

    Not done in `launch`: tests/saver.py is a gate ABOUT the saver, and a
    harness that silently disabled the feature under test would be worse than
    the failure this prevents.
    """
    S = _syms().linear
    m.write(S("ss_idle"), b"\0\0")
    m.write(S("ss_mins"), b"\0")


def _syms():
    import os88sym
    return os88sym


def desktop_up(s):
    """Is the os8088 DESKTOP on screen? - the boot gate, and the only one that
    works on all three adapters.

    Not "has the card left text mode": MartyPC's MDA answers text forever, in
    graphics mode as in any other, so that gate hangs the whole 120 s on the
    Hercules machine. Not stillness on its own either: the BIOS POST sits
    PERFECTLY still for seconds before the floppy is touched, and a settle
    that accepted it returned an 8.3s "boot" showing a quarter of the
    desktop's lit pixels.

    THREE FACTS, because one band is a thing another screen can imitate and
    the whole desktop is not. It used to be the menu bar's white field alone,
    which is *nearly* enough - `wm_paint_all` draws the dither, the drive
    zones and the dock BEFORE the bar, so the bar going up means the rest is
    already there - but "nearly" is carrying the argument, and the failure
    mode of a boot gate is that everything after it is measuring a machine
    that was not ready. So the bar is now tested as SPEC.md 12 defines it,
    white field AND the 1px black rule under it, and the dock strip is tested
    too: the first thing on the screen and the last.

    Measured from reset at 8 Hz, field / rule / dock:

        POST text     0.26  0.25  0.25      (the rule is what rejects this)
        splash        0.00  0.00  0.00
        CGA desktop   0.93  0.00  0.96
        Herc desktop  0.94  0.00  0.96
        VGA desktop   1.00  0.00  0.96
    """
    return s.field > 0.8 and s.rule < 0.1 and s.dock > 0.8


# `desktop_up` IS the public name - it was `bar_up`, and it is a predicate
# over a _Screen now rather than over a Marty, because the gate and the
# stillness test have to be two questions about ONE reading (see _Screen).
# A caller running `settle` itself - on a two-card machine, anything that must
# ask `cards()` before it knows which card to watch, and so cannot use
# `launch(card=...)` - passes `gate=desktop_up, card=<idx>`; the card belongs
# to `settle` now, so there is no lambda to close over it.


# Wait for a machine that is going to CHANGE THE SCREEN and then stop: a boot,
# a click, a repaint. Its twin below, `until`, is for work that changes no
# pixels while it runs - pick by whether the screen is the evidence.
def settle(m, quiet=1.0, stable=2, gate=None, limit=120.0, card=None):
    """Run until the SCREEN STOPS CHANGING, and answer how long that took.

    A fixed sleep is either waste or a lie. A 360KB CGA boot is ~25 s on this
    container and a driver-loading or Hercules one is longer, so the number
    that works today is the number that silently truncates the next machine's
    boot - and a truncated boot does not look like a truncated boot, it looks
    like the thing under test not working.

    `stable` consecutive rendered frames `quiet` apart being identical is the
    signal, because an os8088 screen is genuinely static between events: the
    splash bar moves while the kernel loads, and after that nothing does until
    something is clicked. The clock is the one exception and it changes once a
    MINUTE, so at worst it costs one more round.

    `gate` is a predicate over a `_Screen` that must answer True BEFORE
    stillness is believed - `desktop_up` for a boot, nothing for an action.
    Without one this returns during the BIOS's own POST, which sits perfectly
    still for seconds before the floppy is touched: measured, and it looked
    exactly like a booted machine (an 8.3s "boot" showing a quarter of the
    desktop's lit pixels).

    ONE READ SERVES BOTH QUESTIONS. The gate and the stillness test used to
    read the screen separately, which on an emulator running the guest faster
    than real time means they can see different frames and answer about a
    state that never existed - see `_Screen`.

    It is equally the right wait AFTER an action: `settle(m)` in place of the
    `time.sleep(4)` every script in this tree has after a click, which is the
    same guess in the same two directions.

    **KEEP `limit` UNDER 90 SECONDS unless there is a very good reason, and
    write the reason down.** MartyPC runs the guest FASTER than real time -
    **measured 4.87x and 4.81x** on this container, two 10-second samples of
    the cycle counter against the 8088's own 4.772727 MHz (14.31818/3), which
    is the only honest way to ask:

        c0 = m.status()["cycles"]; time.sleep(10)
        (m.status()["cycles"] - c0) / 10 / 4772727.0

    So guest seconds arrive sooner than wall seconds, and a generous limit is
    not insurance - it is how long you wait to be told something went wrong.
    The whole of `tests/sysbench`, which is a minute and a half of guest time
    including its floppy rows, is up in well under 90 s here. The default 120
    already covers a 360KB Hercules boot (16.1 s) several times over.

    The ratio is a property of THIS HOST and will move with its load, so
    re-measure rather than quoting 4.8x; what does not move is that it is
    above 1, and no `limit` should be sized as though it were below.

    The failure this guards against is not a truncated wait, it is a
    MISDIAGNOSIS. A session that sets `limit=1800` because "the emulator might
    be slow" and then sees the run take half an hour concludes the GUEST is
    slow and goes looking for a performance bug in os8088; it was the harness
    waiting. Sitting on a 700-second blind `time.sleep` before a `settle` is
    the same mistake with the evidence thrown away - if a run really needs
    that long, something is wrong and the way to find out is to sample the
    screen every few seconds, not to sleep through it.
    """
    import time
    # A STILLNESS WINDOW WIDER THAN THE CLOCK'S OWN PERIOD CAN NEVER CLOSE,
    # and that is the paragraph above happening rather than being warned
    # about. `stable` identical samples `quiet` apart is (stable * quiet) HOST
    # seconds of unchanged screen; the menu bar's clock changes once a GUEST
    # minute, and the guest runs FASTER than real time - so at 3.3x a
    # 60-host-second window spans three clock ticks and every pair of samples
    # differs, for ever. `settle(m, quiet=30, limit=2400)` after a hard-disk
    # install sat out its full 40 minutes with the install long finished, and
    # was duly reported as the GUEST being slow: the install was 64 guest
    # seconds. Measured rather than assumed, because the ratio is a property
    # of this host, and only when the window is wide enough to be at risk.
    if quiet * stable > 4.0:
        c0 = m.status()["cycles"]
        time.sleep(0.5)
        # 4.772727 MHz is every machine in this tree bar --turbo, which only
        # makes this over-estimate and so err towards raising.
        rate = (m.status()["cycles"] - c0) / 0.5 / 4772727.0
        if rate > 0.0 and quiet * stable * rate >= 55.0:
            raise MartyError(
                "settle(quiet=%g, stable=%d) asks for %.0f GUEST seconds of "
                "unchanged screen (this host runs the guest at %.1fx), and the "
                "menu bar's clock changes every 60 - so this can never return "
                "and would wait out the whole %.0fs limit. Use a smaller "
                "`quiet`; and if you are waiting on something that holds the "
                "gfx lock while it works - a hard-disk install, a big copy - "
                "`settle` is the wrong tool anyway, because the screen is "
                "MORE still while it is busy. Use `until(m, cond)`."
                % (quiet, stable, quiet * stable * rate, rate, limit))
    t0 = time.time()
    last, run, seen = None, 0, None
    while time.time() - t0 < limit:
        try:
            cur = _Screen(m, card)
        except MartyError:
            cur = None
        if cur is not None and gate is not None and not gate(cur):
            seen, last, run = cur, None, 0       # not up yet: stillness before
            time.sleep(quiet)                    # the gate means nothing
            continue
        run = run + 1 if (cur is not None and last is not None
                          and cur.bytes == last.bytes) else 0
        if run >= stable:
            return time.time() - t0
        last = cur
        time.sleep(quiet)
    if gate is not None and seen is not None and not gate(seen):
        raise MartyError(
            "the os8088 desktop never appeared in %.0fs - the last screen was "
            "%s, against a desktop's 93+/0/96. This machine never finished "
            "booting, so nothing below would mean what it says. See the "
            "MartyPC log." % (limit, seen))
    if saver_running(m):
        raise MartyError(
            "the screen was still changing after %.0fs because SPEC.md 79's "
            "SCREEN SAVER IS RUNNING - [blk_on] is set. It draws, so this can "
            "never settle, and anything else this gate armed is now watching "
            "the saver rather than the machine. Call os88marty.no_saver(m) "
            "once after the desktop is up." % limit)
    raise MartyError(
        "the screen was still changing after %.0fs: the machine never "
        "finished booting (or something on it is animating, in which case "
        "pass boot=<seconds> instead)." % limit)


# Wait for a machine that is BUSY WITHOUT DRAWING: anything holding the gfx
# lock for its whole run. `settle` cannot see that work at all - it watches
# pixels, and there are none - so ask about the thing being waited for.
def until(m, cond, what="that condition", poll=1.0, limit=600.0):
    """Run until CONDITION IS TRUE, and answer how long that took.

    The twin of `settle`, for the case it silently gets wrong. An operation
    that holds the gfx lock while it works freezes the UI for its whole run,
    so the screen is *more* still while it is busy than when it is finished:
    `settle` sees stillness about five seconds into a hard-disk install and
    returns, and a `with launch(...)` block then kills the emulator mid-copy.
    Nothing about that failure looks like a wait ending early - it looks like
    the install stopping halfway, which is a bug in the installer.

    `cond` is called with the Marty each round and may look wherever the
    answer actually is. Guest memory is one place; the HOST side of a mounted
    image is often the better one, because a commit is usually a single write
    you can watch for - SPEC.md 52.10 writes the partition table LAST, so our
    boot stub appearing in sector 0 of the VHD *is* the install finishing.

        os88marty.until(m, lambda _: open(vhd, "rb").read(12) == STUB,
                        "the installer to commit the MBR")

    It separates the two ways this wait fails, because they want different
    fixes. A guest that has STOPPED EXECUTING can never satisfy any condition,
    and "the machine is paused at 0060:3C19" is worth more than a timeout that
    blames the condition - that is the shape a still-armed breakpoint takes,
    and it cost a session an afternoon (docs/MARTYPC-DEBUG.md). Everything
    else is an honest timeout.
    """
    import time
    t0 = time.time()
    while True:
        if cond(m):
            return time.time() - t0
        st = m.status()
        if st["state"] != "running":
            raise MartyError(
                "waited %.0fs for %s, but the guest is %r at %04X:%04X and is "
                "not executing - a stopped machine can never make a condition "
                "true. A breakpoint still armed, or `run` never called?"
                % (time.time() - t0, what, st["state"], st["cs"], st["ip"]))
        if time.time() - t0 > limit:
            raise MartyError(
                "waited %.0fs for %s and it never happened. The guest is still "
                "running, so either it is slower than the limit or the "
                "condition is asking about the wrong thing." % (limit, what))
        time.sleep(poll)


def launch(image, apps=None, machine="os8088_5150_cga", addr="127.0.0.1:9001",
           run_dir=None, boot=True, timeout=DEFAULT_TIMEOUT, extra=(),
           card=None):
    """Start a FRESH martypc_headless on `image` and return a booted Marty.

    `image` and `apps` are paths to floppy images (fd:0 and fd:1). `boot` is
    True to run until the screen settles (`settle`, the right answer on any
    machine), a NUMBER of seconds to run blind for that long, or 0/False to
    return the machine PAUSED. The returned Marty owns the process: close() -
    or the `with` block - kills it, so a session cannot leak a survivor onto
    the next one.

        with os88marty.launch("build/os8088-360.img",
                              apps="build/apps360.img") as m:
            m.vram("cga")

    `card` is which video card the BOOT GATE watches, and it is needed only
    where os8088 is not driving MartyPC's primary - a two-card machine plus a
    `VIDEO=` kernel. Otherwise leave it: the primary is what os8088 picked.

    Raises rather than limping: a port that will not go quiet, an emulator
    that never answers, and a machine that is already part-way through its
    boot are all errors with a sentence each.
    """
    import os
    import shutil
    import subprocess
    import time

    host, _, port = addr.rpartition(":")         # NOT parse_addr, which is for
    if not host:                                 # seg:off memory addresses
        host, port = "127.0.0.1", addr
    port = int(port)
    if run_dir is None:
        here = os.path.dirname(os.path.abspath(__file__))
        run_dir = os.path.join(os.path.dirname(here), "build", "martypc", "run")
    if not os.path.isdir(run_dir):
        raise MartyError("no MartyPC run directory at %s - `make marty` first"
                         % run_dir)

    for pid in _marty_pids():                   # ...by PID, never by pattern
        try:
            os.kill(pid, 9)
        except OSError:
            pass
    if not _port_free(host, port):
        raise MartyError(
            "%s never went quiet: something is still holding the port, so a "
            "new emulator cannot bind it and this would silently drive the "
            "OLD one." % addr)

    media = os.path.join(run_dir, "media", "floppies")
    os.makedirs(media, exist_ok=True)
    mounts = []
    for i, src in enumerate((image, apps)):
        if src is None:
            continue
        dst = os.path.join(media, "run%d.img" % i)   # a copy per DRIVE, so two
        shutil.copyfile(src, dst)                    # sessions cannot collide
        mounts += ["--mount", "fd:%d:media/floppies/run%d.img" % (i, i)]

    cmd = ["./martypc_headless", "--machine-config-name", machine] + mounts \
        + list(extra)
    env = dict(os.environ, MARTYPC_DEBUG_ADDR=addr)
    log = open(os.path.join(run_dir, "martypc.log"), "wb")
    proc = subprocess.Popen(cmd, cwd=run_dir, env=env, stdout=log, stderr=log)

    m = None
    for _ in range(80):                          # wait for it to ANSWER
        if proc.poll() is not None:
            raise MartyError("martypc_headless exited at once (rc=%s); see %s"
                             % (proc.returncode,
                                os.path.join(run_dir, "martypc.log")))
        try:
            m = Marty(addr, timeout=timeout)
            break
        except MartyError:
            time.sleep(0.5)
    if m is None:
        proc.kill()
        raise MartyError("martypc_headless never answered on %s; see %s"
                         % (addr, os.path.join(run_dir, "martypc.log")))

    if m.status()["cycles"] != 0:
        m.close()
        raise MartyError(
            "attached to a machine that is already running: this is a STALE "
            "emulator, not the one just started. Nothing below would mean "
            "what it says.")
    m._proc = proc                               # so close() can end it
    if boot:
        try:
            m.run()
            if boot is True:
                settle(m, gate=desktop_up, card=card)
            else:
                time.sleep(boot)
        except BaseException:                    # ...or the caller never gets
            m.close()                            # the object that owns the
            raise                                # process, and it survives
    return m


def write_png(path, w, h, rows):
    """1bpp rows out as a greyscale PNG, no dependencies (hercshot.py's)."""
    raw = b"".join(b"\x00" + bytes(255 if b else 0 for b in row) for row in rows)
    _png(path, w, h, raw, 0)


def write_png_rgb(path, w, h, data):
    """Packed rgb24 out as a truecolour PNG."""
    raw = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))
    _png(path, w, h, raw, 2)


def crop_rgb(m, x, y, w, h):
    """A rectangle of the card's RENDERED framebuffer, packed rgb24.

    THE CROP IS WHY, and it is the same reason in three tests: a claim like
    "only the number of draws changed" cannot be checked by a capture of ONE
    build, so the window's pixels are captured and diffed against the other
    build's.  Cropping to the WINDOW rather than to a band around the thing
    under test is deliberate - MartyPC's framebuffer is the CARD's, and on
    Hercules that is 720x350 against a 720x348 screen, so a band computed from
    guest coordinates lands beside its subject there.

    Clamped at the bottom edge and not at the right: a row is sliced, so a
    width past the edge silently wraps onto the next row rather than raising,
    which is a diff that fails for the wrong reason.
    """
    fw, fh, data = m.fbuf()
    x = max(0, min(x, fw))
    w = max(0, min(w, fw - x))
    out = bytearray()
    for row in range(max(0, y), min(y + h, fh)):
        o = (row * fw + x) * 3
        out += data[o:o + w * 3]
    return bytes(out)


# SPEC.md 7.1's arrow: 8 wide, 12 high. A drawn mouse cursor is the one thing
# guaranteed to be changing pixels in a graphics-mode capture that has nothing
# to do with what is being measured.
CUR_GW, CUR_GH = 8, 12


def strip_cursor(r):
    """Drop the update events that are ENTIRELY the mouse arrow.

    `gfx_lock` erases the arrow and `gfx_unlock` puts it back, so every locked
    draw can produce a second changed-frame a frame or two later, at the
    pointer and nowhere else. Those are not updates of the thing being
    measured, and they do not merely add noise - they SUBDIVIDE genuine stalls,
    so the interval histogram and the max-gap both come out flattering.

    Detected from the data rather than from the kernel: the arrow is a small
    fixed cell, so the most frequent bbox no bigger than CUR_GW x CUR_GH is it.
    That needs no listing offsets and survives a rebuild. Returns
    (changed, bbox, cell) with the cursor-only events zeroed.
    """
    ch = list(r["changed"])                 # two statements, not one tuple
    bb = r.get("bbox") or [None] * len(ch)  # assignment: the len(ch) in the
                                            # fallback reads ch BEFORE the
                                            # tuple binds it, so a server
                                            # older than bbox - which is any
                                            # martypc_headless built before
                                            # the pin moved - crashed here
                                            # with an UnboundLocalError
                                            # instead of degrading
    small = {}
    for c, b in zip(ch, bb):
        if c and b and (b[2] - b[0]) < CUR_GW and (b[3] - b[1]) < CUR_GH:
            small[tuple(b)] = small.get(tuple(b), 0) + 1
    if not small:
        return ch, bb, None
    cell = max(small, key=small.get)
    for i, (c, b) in enumerate(zip(ch, bb)):
        if c and b and tuple(b) == cell:
            ch[i] = 0
    return ch, bb, cell


def report_pace(r, minpx=1, nocursor=False):
    """Turn a per-frame changed series into a pacing verdict.

    SMOOTH IS LOW VARIANCE, not a high rate. The gaps between frames that
    actually changed are the update intervals; their spread is the jitter, and
    the jitter is what the eye calls judder. A 3,3,3,3 series and a 2,7,1,5
    series can have the identical mean and look completely different.
    """
    ch, cyc = r["changed"], r["cycles"]
    if nocursor:
        ch, _, cell = strip_cursor(r)
        if cell:
            print("  (excluding %d update(s) that were entirely the mouse arrow at %s)"
                  % (sum(1 for c, c2 in zip(r["changed"], ch) if c and not c2), list(cell)))
        else:
            print("  (--no-cursor: no arrow-sized recurring bbox found; nothing excluded)")
    per = (cyc[-1] - cyc[0]) / max(1, len(cyc) - 1) / (r["cpu_mhz"] * 1000.0)
    hits = [i for i, c in enumerate(ch) if c >= minpx]
    print("%dx%d, %d frames, %.2f ms/frame (%.1f Hz)"
          % (r["w"], r["h"], r["frames"], per, 1000.0 / per))
    if len(hits) < 2:
        print("  %d frames changed - nothing is animating." % len(hits))
        return
    gaps = [hits[i + 1] - hits[i] for i in range(len(hits) - 1)]
    mean = sum(gaps) / len(gaps)
    var = sum((g - mean) ** 2 for g in gaps) / len(gaps)
    sd = var ** 0.5
    hist = {}
    for g in gaps:
        hist[g] = hist.get(g, 0) + 1
    moved = [c for c in ch if c >= minpx]
    print("  updates      : %d in %d frames (%.1f%% of frames move)"
          % (len(hits), len(ch), 100.0 * len(hits) / len(ch)))
    print("  interval     : mean %.2f fr (%.1f ms) -> %.1f updates/s"
          % (mean, mean * per, 1000.0 / (mean * per)))
    print("  JITTER       : sd %.2f fr (%.1f ms), min %d, max %d fr (%.0f ms)"
          % (sd, sd * per, min(gaps), max(gaps), max(gaps) * per))
    print("  evenness     : %.2f  (sd/mean; 0.00 is perfect, >0.5 is visible judder)"
          % (sd / mean if mean else 0))
    print("  pixels/update: mean %d, max %d" % (sum(moved) // len(moved), max(moved)))
    print("  intervals    : " + "  ".join(
        "%dfr x%d" % (g, hist[g]) for g in sorted(hist)[:10]))

    # SPLIT BY SIZE, because a bimodal interval histogram almost always means
    # TWO THINGS are updating on two different rhythms and the summary above is
    # their interleaving rather than either one. Tracker's fullscreen is the
    # worked example: a small element ticking every frame and a big grid blit
    # every music row read together as "mean 3.8 frames", which is a number
    # describing nothing that is actually on screen.
    big = max(moved) // 4
    if len(set(moved)) > 1 and min(moved) < big:
        for name, keep in (("BIG   >=%d px" % big, lambda c: c >= big),
                           ("small < %d px" % big, lambda c: c < big)):
            idx = [i for i in hits if keep(ch[i])]
            if len(idx) < 3:
                continue
            g = [idx[k + 1] - idx[k] for k in range(len(idx) - 1)]
            mu = sum(g) / len(g)
            s = (sum((x - mu) ** 2 for x in g) / len(g)) ** 0.5
            hh = {}
            for x in g:
                hh[x] = hh.get(x, 0) + 1
            print("  %-14s n=%3d  mean %5.2f fr (%6.1f ms)  sd %5.2f  evenness %.2f  %s"
                  % (name, len(idx), mu, mu * per, s, s / mu if mu else 0,
                     "  ".join("%dfr x%d" % (k, hh[k]) for k in sorted(hh)[:6])))


def video_is_text(v):
    """Is the card in a TEXT mode? Ask the field that is LIVE on that card.

    `video` reports both `graphics` and `mode`/`text`, and on any given card
    one of them is a dead field left at its initial value:

      - on the **VGA**, `graphics` is dead - it answers false in mode 12h
        exactly as it does in mode 3 - so `mode`/`text` is the discriminator;
      - on the **MDA/Hercules**, `mode`/`text` is dead. os8088 puts the card
        into HGC graphics through 3BF/3B8 rather than through int 10h, so
        `display_mode()` still says `Mode0TextBw40` while `graphics` correctly
        says true. Trusting `text` there sends every Hercules capture down the
        rendered route and REFUSES `--kind herc` - and the VRAM route is the
        one whose output is byte-comparable with tools/hercshot.py, so every
        "0 differing pixels" check in this tree goes through it.

    The tell that one of them is dead is that they contradict each other:
    `graphics: true` with `text: true` cannot both be so.
    """
    if v.get("type") == "vga":
        return bool(v.get("text"))
    return not v.get("graphics", True)


def parse_addr(text):
    """`0060:0000`, `0x600` or `600` -> a flat address."""
    text = text.strip()
    if ":" in text:
        seg, _, off = text.partition(":")
        return (int(seg, 16) << 4) + int(off, 16)
    return int(text, 0)


def cmd_verify(m, args):
    """Dump the kernel and diff it against the build it should be."""
    img = open(args.image, "rb").read()
    ram = m.read(KERNEL_SEG << 4, len(img))
    diff = [i for i in range(len(img)) if ram[i] != img[i]]
    print(f"{args.image}: {len(img)} bytes")
    print(f"differing:  {len(diff)} ({100.0 * len(diff) / len(img):.2f}%)")
    bt = int.from_bytes(ram[0x0C:0x0E], "little")
    print(f"boot_ticks: {bt} live, 0x{int.from_bytes(img[0x0C:0x0E], 'little'):04x} in the file")
    if bt == 0xFFFF:
        print("  ...unstamped: this machine has not finished booting.")
    # Runs, not individual bytes: live state is contiguous variables, and a
    # list of 1,350 offsets tells you nothing a list of 60 runs does not.
    runs, start = [], None
    for i in range(len(img) + 1):
        d = i < len(img) and ram[i] != img[i]
        if d and start is None:
            start = i
        if not d and start is not None:
            runs.append((start, i - start))
            start = None
    print(f"in {len(runs)} run(s); the largest:")
    for off, ln in sorted(runs, key=lambda r: -r[1])[:8]:
        print(f"  +0x{off:04x} {ln:4d} bytes")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Drive a headless MartyPC debug server.")
    ap.add_argument("addr", help="host:port of the debug server (e.g. 127.0.0.1:9001)")
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    sub = ap.add_subparsers(dest="op")

    for name in ("status", "regs", "run", "pause", "reset", "screen", "history", "quit"):
        sub.add_parser(name)

    p = sub.add_parser("key"); p.add_argument("name")
    p = sub.add_parser("type"); p.add_argument("text")
    p = sub.add_parser("mouse")
    p.add_argument("dx", type=int); p.add_argument("dy", type=int)
    p.add_argument("--click", action="store_true")

    p = sub.add_parser("shot", help="the framebuffer as a PNG - no QEMU needed")
    p.add_argument("out")
    p.add_argument("--kind", choices=("cga", "herc"), default=None)
    p.add_argument("--rendered", action="store_true",
                   help="ask the CARD what it rasterised (rgb24) instead of "
                        "decoding guest VRAM. Automatic on VGA, where there "
                        "is no flat framebuffer to decode.")
    p.add_argument("--aperture", type=int, default=0)

    p = sub.add_parser("flicker", help="price a visible drawing defect in frames")
    p.add_argument("-n", "--frames", type=int, default=60)
    p.add_argument("--aperture", type=int, default=0)
    p.add_argument("--key", help="inject this MartyKey before capturing")
    p.add_argument("--click", action="store_true", help="inject a click before capturing")

    p = sub.add_parser("pace", help="frame pacing / smoothness over time")
    p.add_argument("-n", "--frames", type=int, default=300)
    p.add_argument("--aperture", type=int, default=0)
    p.add_argument("--min", type=int, default=1,
                   help="pixels that count as an update (default 1)")
    p.add_argument("--no-cursor", action="store_true", dest="nocursor",
                   help="drop update events that are entirely the mouse arrow")
    p.add_argument("--ignore", help="exclude a rect: x0,y0,x1,y1 (inclusive)")

    p = sub.add_parser("read"); p.add_argument("where"); p.add_argument("len", type=lambda x: int(x, 0))
    p = sub.add_parser("dump"); p.add_argument("where"); p.add_argument("len", type=lambda x: int(x, 0))
    p.add_argument("-o", "--out", required=True)
    p = sub.add_parser("write"); p.add_argument("where"); p.add_argument("hex")
    p = sub.add_parser("step"); p.add_argument("n", nargs="?", type=int, default=1)
    p = sub.add_parser("verify"); p.add_argument("--image", default="build/kernel.bin")

    a = ap.parse_args()

    try:
        with Marty(a.addr, timeout=a.timeout) as m:
            if a.op in (None, ""):
                print("os88marty:", json.dumps(m.status()))
                print("commands: status regs run pause reset step screen history "
                      "read dump write verify quit.  ^D to leave.")
                while True:
                    try:
                        line = input("marty> ").strip()
                    except EOFError:
                        print()
                        return 0
                    if not line:
                        continue
                    if line in ("q", "quit", "exit"):
                        return 0
                    parts = line.split()
                    try:
                        if parts[0] == "read":
                            print(m.read(parse_addr(parts[1]), int(parts[2], 0)).hex(" "))
                        elif parts[0] == "screen":
                            for row in m.screen():
                                print(" |", row.rstrip())
                        elif parts[0] == "step":
                            print(json.dumps(m.step(int(parts[1]) if len(parts) > 1 else 1)))
                        else:
                            print(json.dumps(m.cmd(cmd=parts[0])))
                    except (MartyError, IndexError, ValueError) as e:
                        print("error:", e)
            elif a.op == "read":
                print(m.read(parse_addr(a.where), a.len).hex(" "))
            elif a.op == "dump":
                data = m.read(parse_addr(a.where), a.len)
                open(a.out, "wb").write(data)
                print(f"{len(data)} bytes -> {a.out}")
            elif a.op == "write":
                print(m.write(parse_addr(a.where), bytes.fromhex(a.hex)), "bytes written")
            elif a.op == "step":
                print(json.dumps(m.step(a.n)))
            elif a.op == "key":
                m.key(a.name); print("ok")
            elif a.op == "type":
                m.type_text(a.text); print("ok")
            elif a.op == "mouse":
                m.mouse_move(a.dx, a.dy)
                if a.click:
                    m.click()
                print("ok")
            elif a.op == "shot":
                # VGA has no flat framebuffer to decode - mode 12h is four
                # planes behind the Graphics Controller - so it takes the
                # rendered route whether or not it was asked for. The 1bpp
                # adapters keep the VRAM route by default, because that is the
                # one whose output is byte-comparable with tools/hercshot.py
                # and so with every "0 differing pixels" check in this tree.
                # THE CAPTURE ROUTE IS CHOSEN FROM THE CARD'S MODE, never
                # guessed. `vram` decodes SPEC.md 39.3's banked GRAPHICS
                # layout; point it at a text screen and it reads
                # character/attribute pairs as pixel bits and produces a
                # plausible picture of nothing, with no error - which is
                # exactly what happened to a fullscreen text-mode app
                # (SPEC.md 53.4's FSXM_TEXT80). In text mode the rendered
                # route is the only one that means anything, and `screen`
                # is usually what you actually wanted.
                v = m.video()
                is_text = video_is_text(v)
                if is_text and not a.rendered and a.kind is None:
                    print("%s: card is in %s (a TEXT mode) - capturing the "
                          "RENDERED framebuffer.\n"
                          "  The VRAM route would decode character cells as a "
                          "bitmap and show you nothing real.\n"
                          "  For the characters themselves: os88marty.py <addr> screen"
                          % (a.out, v.get("mode")), file=sys.stderr)
                if a.kind is not None and is_text:
                    raise MartyError(
                        "--kind %s decodes a GRAPHICS framebuffer and the card is in "
                        "%s, a text mode. Drop --kind (the rendered route works in "
                        "every mode), or use `screen` for the characters."
                        % (a.kind, v.get("mode")))
                rendered = a.rendered or is_text or (a.kind is None and v["type"] == "vga")
                if rendered:
                    w, h, data = m.fbuf(a.aperture)
                    write_png_rgb(a.out, w, h, data)
                    lit = sum(1 for i in range(0, len(data), 3)
                              if data[i:i + 3] != b"\x00\x00\x00")
                    print("%s: %dx%d rendered, %d non-black of %d (%.1f%%)"
                          % (a.out, w, h, lit, w * h, 100.0 * lit / (w * h)))
                else:
                    w, h, rows = m.vram(a.kind)
                    write_png(a.out, w, h, rows)
                    lit = sum(sum(r) for r in rows)
                    print("%s: %dx%d, %d lit of %d (%.1f%%)"
                          % (a.out, w, h, lit, w * h, 100.0 * lit / (w * h)))
            elif a.op == "flicker":
                m.pause()
                if a.key:
                    m.key(a.key)
                if a.click:
                    m.mouse(0, 0, l=True)
                    m.mouse(0, 0)
                r = m.flicker(a.frames, a.aperture)
                per = r["cycles"] / r["frames"] / (r["cpu_mhz"] * 1000.0)
                print("%dx%d, %d frames, %.1f ms/frame, settled=%s"
                      % (r["w"], r["h"], r["frames"], per, r["settled"]))
                print("  VISIBLE REDRAW : %2d frames changed = %6.0f ms"
                      % (r["moved_frames"], r["moved_frames"] * per))
                print("  FLASH          : %2d frames with transient pixels = %6.0f ms, worst %d px"
                      % (r["flash_frames"], r["flash_frames"] * per, r["worst_transient"]))
                if not r["settled"]:
                    print("  ...NOT SETTLED: ask for more frames; every count above is "
                          "measured against a moving target.")
                for f in r["per_frame"]:
                    if f["changed"] or f["transient"]:
                        print("   frame %3d  changed %6d  transient %6d  %s"
                              % (f["frame"], f["changed"], f["transient"], f["bbox"] or ""))
            elif a.op == "pace":
                ig = [int(v) for v in a.ignore.split(",")] if a.ignore else None
                r = m.pace(a.frames, a.aperture, ig)
                report_pace(r, a.min, a.nocursor)
            elif a.op == "screen":
                for row in m.screen():
                    print(row.rstrip())
            elif a.op == "history":
                print(m.history())
            elif a.op == "verify":
                return cmd_verify(m, a)
            else:
                print(json.dumps(m.cmd(cmd=a.op)))
        return 0
    except MartyError as e:
        print(f"os88marty: {e}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
