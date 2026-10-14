#!/usr/bin/env python3
"""isoTron's guest reader (SPEC.md 108.7.2): what every emulator row of the
package imports rather than rewrites. A library, not a test.

    import isotronlib as L
    with os88ui.boot(...) as ui:
        g = L.open_game(ui)              # ISOTRON.O88 off drive B:
        g.put("it_tseed", 2425, 2)
        st = g.state()                   # the outputs, a dict

THE OFFSETS come from re-assembling apps/isotron/isotron.asm with
`dw <name> - os88_image_end` appended for every bss variable and `dw <label>`
for every code mark (tests/n1942.py's recipe): nasm's map carries labels and
not the `equ`s the bss is made of. The bss base is the IMAGE size at +8 of
the plain assembly.

THE MARKS (it_step0 ...) exist only under -DIT_MARKS (isotron.asm's IT_MARK:
a symbol in nasm's map splits a routine for tools/stkdepth.py). They emit no
byte, so the -DIT_MARKS image IS the shipped one - which the check below
proves rather than assumes.

THE PACKAGE MUST BE THE TREE (tests/pxslib.py's rule): the offsets are taken
off the SOURCE and the guest runs what `make` wrote to the floppy, so an edit
after the last build would read the old binary's bss through the new offsets
- plausible rubbish, not an error. So the plain assembly is compared with the
image unpacked out of build/isotron.o88, and a mismatch stops the row.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                    # noqa: E402
import os88geom                                     # noqa: E402
import os88pkg                                      # noqa: E402

TITLE = "isoTron"
FILE = "ISOTRON.O88"
SRC = os.path.join(ROOT, "apps", "isotron", "isotron.asm")
INCS = (os.path.join(ROOT, "apps") + os.sep, os.path.join(ROOT, "apps", "isotron") + os.sep)
BSS_SRCS = ("isotron.asm", "ittest.inc")
MARKS = ("it_step0", "it_step1", "it_wake0", "it_wake1", "it_frame_end",
         "it_worker", "it_iterate", "it_move", "it_round_end")

# it_tflags (ittest.inc)
TF_FAST, TF_BOT, TF_NOFLOW, TF_HOLD = 1, 2, 4, 8
# it_state (isotron.asm)
ST_TITLE, ST_PLAY, ST_CRASH, ST_WAIT, ST_OVER, ST_NEXT = range(6)


def _equs():
    """The package's own numeric `equ`s, read off its source - a row reads the
    heads and the mailbox by these, and a second copy here would drift."""
    out = {}
    for fn in ("isotron.asm", "ittest.inc"):
        text = open(os.path.join(ROOT, "apps", "isotron", fn), encoding="utf-8").read()
        for name, val in re.findall(r"^(\w+)\s+equ\s+(0x[0-9A-Fa-f]+|\d+)\b", text, re.M):
            out[name] = int(val, 0)
    return out


_E = _equs()
TINMAX = _E["IT_TINMAX"]
MAXHEADS, H_SIZE = _E["MAXHEADS"], _E["H_SIZE"]     # the player is record MAXHEADS
H_U, H_D, H_HD, H_ALIVE = _E["H_U"], _E["H_D"], _E["H_HD"], _E["H_ALIVE"]
HD_N, HD_E, HD_S, HD_W = _E["HD_N"], _E["HD_E"], _E["HD_S"], _E["HD_W"]
K_LEFT, K_RIGHT = _E["K_LEFT"], _E["K_RIGHT"]
P_NORMAL, P_D0 = _E["P_NORMAL"], _E["P_D0"]
WIN = {0: None, 1: "P", 2: "C"}

_SYMS = None


def _bss_names():
    names = []
    for fn in BSS_SRCS:
        text = open(os.path.join(ROOT, "apps", "isotron", fn), encoding="utf-8").read()
        names += re.findall(r"^\s+(?:WORDV|BYTEV|BUFV)\s+(it_\w+)", text, re.M)
    return names


def _nasm(src_text, out):
    with tempfile.TemporaryDirectory() as td:
        asm = os.path.join(td, "s.asm")
        open(asm, "w", encoding="utf-8").write(src_text)
        cmd = ["nasm", "-f", "bin", "-w+error", "-DIT_MARKS"]
        for i in INCS:
            cmd += ["-I", i]
        subprocess.run(cmd + ["-o", out, asm], check=True, cwd=ROOT)


def syms():
    """(bss, code): every bss variable's offset in the package segment, and
    every code mark's. Refuses when build/isotron.o88 is behind the tree."""
    global _SYMS
    if _SYMS is not None:
        return _SYMS
    source = open(SRC, encoding="utf-8").read()
    # the includes are found through -I, so the copy can live anywhere
    with tempfile.TemporaryDirectory() as td:
        plain = os.path.join(td, "plain.bin")
        _nasm(source, plain)
        image = open(plain, "rb").read()
        try:
            built = os88pkg.image_unwrap(open(os88build.at("build/isotron.o88"), "rb").read())
        except OSError:
            sys.exit("isotronlib: no build/isotron.o88 - run `make`")
        if built != image:
            sys.exit("isotronlib: build/isotron.o88 is %d bytes of image and the source "
                     "assembles to %d - the package is BEHIND THE TREE. Run `make`."
                     % (len(built), len(image)))
        names = _bss_names()
        tail = "\n".join("dw %s - os88_image_end" % n for n in names)
        tail += "\n" + "\n".join("dw %s" % c for c in MARKS)
        k = source.rindex("OS88_IMAGE_END")
        probe = source[:k] + tail + "\n" + source[k:]
        out = os.path.join(td, "probe.bin")
        _nasm(probe, out)
        raw = open(out, "rb").read()
    n = len(names) + len(MARKS)
    vals = struct.unpack("<%dH" % n, raw[-2 * n:])
    base = struct.unpack_from("<H", image, 8)[0]
    bss = {nm: base + v for nm, v in zip(names, vals[:len(names)])}
    code = dict(zip(MARKS, vals[len(names):]))
    _SYMS = (bss, code)
    return _SYMS


class Game:
    """The running package's own variables, read and written by name."""

    def __init__(self, ui, title=TITLE):
        self.ui, self.m = ui, ui.m
        self.bss, self.code = syms()
        w = ui.wait_window(title)
        seg = struct.unpack("<H", self.m.read(os88geom.winptr(self.m, w.i, ui.sym)
                                              + os88geom.W_SEG, 2))[0]
        assert seg, "the isoTron window has no segment"
        self.seg = seg
        self.base = seg << 4

    def a(self, name):
        return self.base + (self.bss[name] if name in self.bss else self.code[name])

    def b(self, name):
        return self.m.read(self.a(name), 1)[0]

    def w(self, name):
        return struct.unpack("<H", self.m.read(self.a(name), 2))[0]

    def put(self, name, v, n=1):
        self.m.write(self.a(name), (v & ((1 << (8 * n)) - 1)).to_bytes(n, "little"))

    def state(self):
        """The round outputs the model is compared on (ittest.inc)."""
        return dict(h=self.b("it_hplayed"), winner=WIN.get(self.b("it_winner")),
                    iter=self.w("it_iter"), sp=self.w("it_sp"), sc=self.w("it_sc"),
                    hnext=self.b("it_h"), lossrow=self.b("it_lossrow"),
                    rng=self.w("it_rng"), ck=self.w("it_ck"),
                    nsteps=self.w("it_nsteps"))


def open_game(ui, drive="B"):
    """Open ISOTRON.O88 off a drive's root and hand back its Game."""
    ui.open_drive(drive)
    ui.open(FILE)
    return Game(ui)
