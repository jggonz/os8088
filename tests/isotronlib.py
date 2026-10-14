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
EQU_SRCS = ("isotron.asm", "ittest.inc", "itcanvas.inc", "itwin.inc", "itfs.inc",
            "itsweep.inc", "itinput.inc", "itsnd.inc")
MARKS = ("it_step0", "it_step1", "it_wake0", "it_wake1", "it_frame_end",
         "it_hs0", "it_hs1", "it_worker", "it_iterate", "it_move", "it_round_end",
         "it_span", "it_present", "it_rb0", "it_rb1", "it_fdone", "it_ai1", "it_span0",
         "it_fsw0", "it_fsw1", "it_fs_frame", "it_fsx0", "it_fsback", "it_fsf0",
         "it_fr_cga", "it_fr_vga", "it_fr_herc", "it_sw0", "it_sw1", "it_rdy1",
         "it_swp0", "it_swp1", "it_bld0", "it_bld1", "it_paint_rect", "it_paint_rect.ov",
         "it_tonecall", "it_tonecall1", "it_ph_crash", "it_ph_ready", "it_ph_tower",
         "it_ph_over", "it_fssnd1", "it_snd_frame", "it_onkey")

# it_tflags (ittest.inc)
TF_FAST, TF_BOT, TF_NOFLOW, TF_HOLD, TF_SEED = 1, 2, 4, 8, 16
# it_state (isotron.asm)
ST_TITLE, ST_PLAY, ST_CRASH, ST_WAIT, ST_OVER, ST_NEXT, ST_SWEEP, ST_READY = range(8)


def _equs():
    """The package's own numeric `equ`s, read off its source - a row reads the
    heads and the mailbox by these, and a second copy here would drift."""
    out = {}
    for fn in EQU_SRCS:
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
H_BX0, H_BX1, H_BMV = _E["H_BX0"], _E["H_BX1"], _E["H_BMV"]
CSIZE = _E["IT_CCOL"] * _E["IT_CCOLS"]             # the canvas claim's bytes
TLINES = _E["IT_TLINES"]                            # the title's own lines
OV_SIZE, OV_N = _E["OV_SIZE"], 3 + TLINES           # the overlay records
OVF_SIZE, OVF_N = _E["OVF_SIZE"], 3 + TLINES        # ...and full screen's
FSK = {"cga": _E["FSK_CGA"], "vga": _E["FSK_VGA"], "herc": _E["FSK_HERC"],
       "ega": _E["FSK_EGA"]}                        # it_fsk: the bracket's surface
IT_MLUT = _E["IT_MLUT"]
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


    # --- wave 2: the picture ------------------------------------------------
    def canvas(self):
        """The guest's 64,000-byte canvas claim, as isotronsim's pack()."""
        return self.m.read(self.w("it_cseg") << 4, CSIZE)

    def geom(self):
        """The picture's place: origin, rows and bytes across."""
        return dict(ox=self.w("it_ox"), oy=self.w("it_oy"), hc=self.w("it_hc"),
                    pwb=self.w("it_pwb"), cx=self.w("it_cx"), cy=self.w("it_cy"),
                    cw=self.w("it_cw"), ch=self.w("it_ch"), surf=self.b("it_surf"),
                    bpp=self.b("it_bpp"), layok=self.b("it_layok"))

    def overlays(self):
        """The overlay rects that show, as (x0, y0, x1, y1) device px in the
        picture (x1, y1 exclusive), with their text."""
        raw = self.m.read(self.a("it_ov"), OV_N * OV_SIZE)
        out = []
        for k in range(OV_N):
            r0, j0, j1, sp = struct.unpack_from("<HBBH", raw, k * OV_SIZE)
            if sp:
                txt = self.m.read(self.base + sp, 64).split(b"\0")[0].decode("latin-1")
                out.append((8 * j0, r0, 8 * j1 + 8, r0 + 8, txt))
        return out

    # --- wave 3: full screen ------------------------------------------------
    def fs_overlays(self):
        """The full-screen overlay records that show, as (row, c0, cells,
        text) in 8 x 8 cells of the logical picture (itfs.inc it_ovf)."""
        raw = self.m.read(self.a("it_ovf"), OVF_N * OVF_SIZE)
        out = []
        for k in range(OVF_N):
            row, c0, n, _, sp = struct.unpack_from("<BBBBH", raw, k * OVF_SIZE)
            if sp and n:
                txt = self.m.read(self.base + sp, 64).split(b"\0")[0].decode("latin-1")
                out.append((row, c0, n, txt[:n]))
        return out

    def fs_owed(self):
        """Is any full-screen present still owed (a whole one, an overlay, a
        sweep's items)?"""
        return bool(self.b("it_full") or self.b("it_hudd") or self.b("it_msgd")
                    or self.b("it_state") == ST_SWEEP)

    def owed(self):
        """Is any present still owed (a box, an overlay, a whole present, a
        sweep's items, a canvas build the worker has yet to make or a round
        waiting on one)?"""
        if self.b("it_full") or self.b("it_hudd") or self.b("it_msgd"):
            return True
        if self.b("it_bldq") or self.b("it_qround"):
            return True
        if self.b("it_state") == ST_SWEEP:
            return True
        heads = self.m.read(self.a("it_heads"), (MAXHEADS + 1) * H_SIZE)
        return any(heads[k * H_SIZE + H_BMV] for k in range(MAXHEADS + 1))


def fresh_disk(img):
    """THE DISK MUST BE THE BUILD. `make` rebuilds build/isotron.o88 and not
    the on-demand isoTron floppies, so a row booting a disk older than the
    package drives the LAST wave's game through THIS wave's offsets - which
    wave 2 did, and read a debug view as a garbled 3D one."""
    o88 = os88build.at("build/isotron.o88")
    if os.path.getmtime(img) < os.path.getmtime(o88):
        sys.exit("isotronlib: %s is older than build/isotron.o88 - run `make isotrondisk`"
                 % img)
    return img


def open_game(ui, drive="B"):
    """Open ISOTRON.O88 off a drive's root and hand back its Game."""
    ui.open_drive(drive)
    ui.open(FILE)
    return Game(ui)
