#!/usr/bin/env python3
"""os88marty: drive the MartyPC debug server (docs/MARTYPC-DEBUG.md).

    tools/martypc/build.sh
    cd build/martypc/run && MARTYPC_DEBUG_ADDR=127.0.0.1:9001 \\
        ./martypc_headless --mount fd:0:media/floppies/os8088-360.img &

    python3 tools/os88marty.py 127.0.0.1:9001 status
    python3 tools/os88marty.py 127.0.0.1:9001 run
    python3 tools/os88marty.py 127.0.0.1:9001 dump 0060:0000 71624 -o /tmp/k.dump
    python3 tools/os88marty.py 127.0.0.1:9001            # a REPL

The sibling of tools/os88dbg.py, and the difference is worth stating because
they answer the same question from opposite sides. os88dbg talks to DEBUG.DRV,
which is CODE RUNNING INSIDE THE GUEST: it needs a UART, an IRQ, interrupts
enabled and a machine healthy enough to service them - and it works on real
iron, which nothing else here does. This talks to the EMULATOR: it costs the
guest not one cycle, needs nothing installed, answers on a machine that has
hard-frozen, and can do the things a guest stub structurally cannot - single
step, breakpoints, cycle counts, registers.

Use this one for everything on an emulator. Use os88dbg when the machine is
on somebody's desk.

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

    def regs(self):
        return self.cmd(cmd="regs")

    def setreg(self, reg, value):
        return self.cmd(cmd="setreg", reg=reg, value=value)

    def screen(self):
        """The video card's text rows, in text modes."""
        return self.cmd(cmd="screen")["rows"]

    def video(self):
        """Which card, its raster geometry, and its display apertures.

        `graphics` IS NOT TO BE TRUSTED ON VGA: the card's `mode_graphics`
        field is initialised to false and never assigned, so it answers false
        in mode 12h exactly as it does in mode 3. `field_w`/`field_h` are the
        honest question - 800x524 is mode 12h's raster and a text mode's is
        not.
        """
        return self.cmd(cmd="video")

    def flicker(self, frames=60, aperture=0):
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
        return self.cmd(cmd="flicker", frames=frames, aperture=aperture)

    def pace(self, frames=300, aperture=0, ignore=None):
        """Per-frame changed-pixel counts, for FRAME PACING (PERFORMANCE.md
        Part 3.2). Keeps two frames server-side, so `frames` can be large.

        `ignore` is an inclusive [x0,y0,x1,y1] excluded from every comparison —
        for a blinking cursor, which changes pixels on a clock of its own.
        """
        kw = {"cmd": "pace", "frames": frames, "aperture": aperture}
        if ignore:
            kw["ignore"] = list(ignore)
        return self.cmd(**kw)

    def fbuf(self, aperture=0):
        """The card's RENDERED framebuffer as (width, height, rgb24 bytes).

        The complement of `vram`, and the only route that works on VGA: mode
        12h is four planes behind the Graphics Controller, so there is no flat
        framebuffer in guest memory to read. This asks the CARD what it
        rasterised, which is a different assertion - `vram` says the kernel
        wrote the right bytes, this says the machine put them on a screen -
        and it works on every adapter and in every mode.
        """
        r = self.cmd(cmd="fbuf", aperture=aperture)
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
        """
        return self.cmd(cmd="bp", list=bps)["count"]

    # --- input, through the REAL devices -------------------------------------
    #
    # No guest code is involved in either of these, which is the point: `key`
    # enters the emulator's keyboard buffer so the guest sees it through the
    # 8255 and int 09h, and `mouse` builds a real Microsoft 3-byte packet and
    # clocks it into the serial controller so the guest's own ISR decodes it.
    # A debug module poking [mouse_x] would skip the UART, the packet decoder
    # and SPEC.md 9.5's port contest - the code most likely to be wrong.

    # --- reproducibility: guest-time positioning (docs/SNAPSHOT-PLAN.md) -----

    def advance(self, frames=None, cycles=None):
        """Run a bounded amount of GUEST time and stop.

        USE THIS INSTEAD OF time.sleep FOR ANYTHING THAT MUST REPEAT. The
        emulator is bit-exact deterministic in guest time and runs at whatever
        multiple of real time the host manages, so a wall-clock wait lands at a
        different guest position every run - two instances given the same
        sleep(22) were measured 21.7 million cycles apart.
        """
        kw = {"cmd": "advance"}
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

    def type_text(self, s):
        """ASCII through the keyboard. Letters, digits, space and Enter."""
        for ch in s:
            if ch == "\n":
                self.key("Enter")
            elif ch == " ":
                self.key("Space")
            elif ch.isalpha():
                self.key("Key" + ch.upper())
            elif ch.isdigit():
                self.key("Digit" + ch)
            else:
                raise MartyError("no key mapping for %r" % ch)

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


def write_png(path, w, h, rows):
    """1bpp rows out as a greyscale PNG, no dependencies (hercshot.py's)."""
    raw = b"".join(b"\x00" + bytes(255 if b else 0 for b in row) for row in rows)
    _png(path, w, h, raw, 0)


def write_png_rgb(path, w, h, data):
    """Packed rgb24 out as a truecolour PNG."""
    raw = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))
    _png(path, w, h, raw, 2)


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
