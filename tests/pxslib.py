#!/usr/bin/env python3
"""PIXELSTEIN 3D's guest reader (SPEC.md 97.12): what every emulator row of the
package imports rather than rewrites.

The package's variables are read out of its own bss - never inferred from the
glass - by re-assembling apps/pixelstein/pxgame.asm with `[map symbols]`
(tests/cycweb.py's pkg_syms, the way tests/pxsbench.py finds pb_res) and
locating the program's segment through the window it opened: the window's
W_SEG names PART 0 after the loader's re-home (SPEC.md 20.12.10), which is
the segment every equate below is relative to. The loader's part table is
gone by then, so the scratch part and the level part are found through the
handoff words the loader left at the head of the bss (97.9's PXH_*).

    import pxslib
    with os88marty.launch(...) as m:
        g = pxslib.open_game(m)         # PXSTEIN.O88 off the Disk window
        g.scene("b")                    # the doorway turn, px_force poked
        g.pin(rung="flat", lowres=True) # a Detail/Resolution pick
        cols = g.columns()              # the arrays, a dict of lists
        sh = g.shadow()                 # 80 x 80 bytes of the shadow claim
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# tools/ LAST, so it WINS: tests/pxssim.py (the row) shadows tools/pxssim.py
# (the reference renderer) by name, and `import pxssim` below must find the
# renderer. The first cut inserted tools/ first and tests/ second - tests/
# at sys.path[0] - and worked only because the os88marty import line
# re-inserted tools/ at 0 as a side effect
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88sym, os88geom, os88build, os88ui       # noqa: E402
import os88parts                                                 # noqa: E402
from cycweb import pkg_syms, u16                                # noqa: E402
# ...AND AGAIN, because cycweb inserts tests/ at sys.path[0] on import (main's
# #197 made it do so), which put this row's own tests/pxssim.py ahead of the
# renderer and failed every PIXELSTEIN emulator row at its first import - the
# reason the first cut's order "worked only through a side effect" above
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pxssim                                                    # noqa: E402
assert hasattr(pxssim, "render"), "pxslib: `pxssim` resolved to tests/, not tools/"

TITLE = "Pixelstein 3D"
FILE = "PXSTEIN.O88"
ROWS, STRIDE = 80, 80                   # the band's first byte is (80 - Size)
                                        # / 2 and lives in the package's
                                        # px_x0 - there is no constant (97.3)
PXB = {0: "none", 1: "cga4", 2: "cga16", 3: "herc", 4: "modex", 5: "win1",
       6: "win4"}                       # (win4: the 16-colour window, 97.14)
WINDOWED = (5, 6)                       # the two window backends' PXB_*
PXR = {"wire": 0, "flat": 1, "tex": 2}
PXD = {"auto": 0, "wire": 1, "flat": 2, "tex": 3}
PXH = dict(magic=0, lev=2, gen=4, cold=6, nlev=8, levlen=10, art=12, bt=14,
           genlen=16, spr=18)           # pxstein.asm's PXH_*, PXH_SIZE = 20
PXH_SIZE = 20
SIZES = (48, 56, 64, 72, 80)            # px_sizes: the Size row (97.3)
WIN_SIZES = (48, 56, 64)                # ...of which a window shows these
# part 2's layout - the scratch, in os88pkg's indices (pxgen.inc's PXG_*):
# the three literals here are held to the source by tests/unit/
# t_pxsscale.py; the rest is derived from labels in layout(), because
# nasm's map carries labels and not equates
PXG_DRVMAX, PXG_QN, PXG_QSZ = 96, 512, 8
# the two pinned scenes (97.10): A is the spawn, B the doorway turn - pinned
# in tools/pxssim.py's scenes() and read from there, one source
COLMAX, HORIZON = 80, 40                # PX_COLMAX, PX_HORIZON (pxgame.asm)
PX_TURN, PX_SPEED = 64, 24              # a step's turn and walk (97.8)
# px_force_all's SEVEN arrays and their fills (pxcomp.inc): the memory of
# "the whole view was drawn", so the next frame writes and presents all of
# it. Not px_lside: the Flat skip's lmat compare fires first, and Wire has
# no tone to compare
FORCE_ALL = (("px_ltop", 0), ("px_lbot", 79), ("px_lmat", 0xFF),
             ("px_lu0", 0), ("px_lu1", HORIZON - 1), ("px_ll0", HORIZON),
             ("px_ll1", 79))

_SYMS = None
DEFINES = ()                    # tests/pxsperf.py --probe: ("PXPROBE",) and
PKG = None                      # the .o88 it built, in place of build/'s


def syms():
    """Every equate of pxgame.asm, from nasm's own map on a temp copy.

    THE PACKAGE MUST BE THE TREE (tests/pxsbench.py's rule): the map is
    taken off the SOURCE and the guest runs what `make` wrote to the floppy,
    so an edit after the last build reads the old binary's bss through the
    new offsets - which does not fail, it answers plausible rubbish (a
    Hercules run once read `hasfoc = 101` and waited 180 guest seconds for
    a bracket the F key had entered). What is compared is PART 0 unpacked
    out of build/pxstein.o88 - the file os88disk.py writes to every disk
    the rows mount - and not build/pxgame.bin, an intermediate a hand-run
    nasm can leave newer than the package. The one thing this cannot see is
    an image handed in by `--apps` that was built elsewhere; that is the
    caller's, and the docstring says so rather than claiming otherwise."""
    global _SYMS
    if _SYMS is None:
        s, image = pkg_syms(os.path.join(ROOT, "apps", "pixelstein", "pxgame.asm"),
                            (os.path.join(ROOT, "apps") + os.sep,
                             os.path.join(ROOT, "apps", "pixelstein") + os.sep),
                            DEFINES)
        try:
            raw = open(PKG or os88build.at("build/pxstein.o88"), "rb").read()
        except OSError:
            sys.exit("pxslib: no build/pxstein.o88 - run `make`")
        built = os88parts.part_bytes(raw, 0)
        if built != image:
            sys.exit("pxslib: part 0 of build/pxstein.o88 is %d bytes and the "
                     "source assembles to %d - the package is BEHIND THE TREE. "
                     "Run `make`." % (len(built), len(image)))
        _SYMS = s
    return _SYMS


def layout():
    """pxgen.inc's layout of part 2 (the scratch) as a dict: BODIES, DRV,
    DRVSZ, QTEX, QEND, Q, SCAL - from the labels the map carries."""
    s = syms()
    ball = s["px_vadj"] - s["px_bodies"]                # PXB_ALL
    drvsz = s["px_drv_end"] - s["px_drv_tpl"]
    assert drvsz <= PXG_DRVMAX, "the driver outgrew PXG_DRVMAX"
    drv = ball
    qtex = drv + PXG_DRVMAX
    return dict(BODIES=0, BALL=ball, DRV=drv, DRVSZ=drvsz, QTEX=qtex, QSPR=qtex + 2,
                QEND=qtex + 4, Q=qtex + 6, SCAL=qtex + 6 + PXG_QN * PXG_QSZ)


def _window(m, S, title):
    """(window index, part 0's segment) of the first window whose caption
    starts `title` and whose W_SEG is written - or None. One look."""
    for w in os88geom.windows(m, S):
        if w.title.startswith(title):
            seg = u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))
            if seg:
                return w.i, seg
    return None


def find(m, S=None, guest=0.0, title=TITLE):
    """(window index, part 0's segment) of the game's window - or of the
    window whose caption starts `title` (tests/pxsbench.py's) - or None.

    `guest` is how long to WAIT for it, in the GUEST's own seconds; 0 is one
    look. It was a host deadline around `time.sleep(0.3)`, which allows a
    loaded box a third of the machine an idle one gets (docs/WRITING-TESTS.md
    7). MartyPC and QEMU both: tests/pxswin.py's --qemu arm finds its window
    through here, and QEMU's clock is the BIOS tick (tests/os88qemu.py)."""
    S = S or os88sym.linear
    got = _window(m, S, title)
    if got is not None or guest <= 0:
        return got
    what = "a %r window with its segment written" % title
    if isinstance(m, os88marty.Marty):
        try:
            os88marty.until(m, lambda mm: _window(mm, S, title) is not None,
                            what, poll=0.3, guest=guest)
        except os88marty.MartyError as e:
            print("   pxslib: %s" % str(e).split("\n")[0])  # budget or a stop,
            return None                                     # and it says which
    else:
        import os88qemu                                     # noqa: E402
        os88qemu.acted(m, lambda: _window(m, S, title) is not None,
                       secs=guest, what=what, poll=0.3)
    return _window(m, S, title)


class Game:
    def __init__(self, m, win, seg):
        self.m, self.win, self.seg = m, win, seg
        self.base = seg << 4
        self.s = syms()
        self.f0 = None                  # px_frames as last read PAUSED, before
                                        # a poke that owes a frame (wait_frames)

    # --- the bss ------------------------------------------------------------
    def addr(self, name):
        return self.base + self.s[name]

    def word(self, name):
        return u16(self.m.read(self.addr(name), 2))

    def byte(self, name):
        return self.m.read(self.addr(name), 1)[0]

    def dword(self, name):
        b = self.m.read(self.addr(name), 4)
        return int.from_bytes(b, "little")

    def bytes_(self, name, n):
        return self.m.read(self.addr(name), n)

    def poke(self, name, data):
        """Write while the guest is PAUSED (the caller pauses)."""
        self.m.write(self.addr(name), bytes(data))

    def poke_word(self, name, v):
        self.poke(name, [v & 255, (v >> 8) & 255])

    def poke_byte(self, name, v):
        self.poke(name, [v & 255])

    def handoff(self):
        h = self.bytes_("px_hand", PXH_SIZE)
        return {k: u16(h, v) for k, v in PXH.items()}

    def part_gen(self, n):
        """n bytes of the scratch part (97.9's part 2: the bodies, the
        driver, the queue and the generated sets) from its start."""
        seg = self.handoff()["gen"]
        if not seg:
            return None
        return self.m.read(seg << 4, n)

    def part_bt(self, n=30720):
        """The byte-texture set (97.9's part 3)."""
        seg = self.handoff()["bt"]
        if not seg:
            return None
        return self.m.read(seg << 4, n)

    def part_spr(self, n):
        """The sprite set (97.9's part 4)."""
        seg = self.handoff()["spr"]
        if not seg:
            return None
        return self.m.read(seg << 4, n)

    # --- the world (wave 3) --------------------------------------------------
    def sim(self, on):
        """The world's sim - the doors, the guards, the weapon - running
        (on=True) or frozen (px_simoff, 97.8): a row that diffs a pose
        against the host wants nothing moving; a row that measures the
        finished frame wants it all. Paused around the poke."""
        self.m.pause()
        self.poke_byte("px_simoff", 0 if on else 1)
        self.m.run()

    def god(self, on):
        """No damage to the player (px_god): a measurement leg cannot have
        the floor restart under it. Paused around the poke."""
        self.m.pause()
        self.poke_byte("px_god", 1 if on else 0)
        self.m.run()

    def actor(self, i):
        """Actor i's record as a dict (pxgame.asm's PXAC_*)."""
        b = self.bytes_("px_act", 16 * (i + 1))[16 * i:]
        return dict(x=u16(b, 0), y=u16(b, 2), kind=b[4], state=b[5], dir=b[6],
                    timer=b[7], hp=b[8], frame=b[9], flags=b[10], cell=u16(b, 12),
                    ang=u16(b, 14))

    def actor_poke(self, i, x=None, y=None, state=None, ang=None, dir=None,
                   hp=None, flags=None, kind=None, frame=None, timer=None):
        """Move or re-state actor i (paused): the cell and its PXC_ACTOR
        mark follow the position, as px_act_move keeps them."""
        base = self.s["px_act"] + 16 * i
        a = self.actor(i)
        mapb = self.s["px_map"]
        if x is not None or y is not None:
            old = a["cell"]
            v = self.m.read(self.base + mapb + old, 1)[0]
            if not v & 2:                       # never a mark on a DOOR cell
                self.m.write(self.base + mapb + old, bytes([v & ~0x20]))
            nx = a["x"] if x is None else x
            ny = a["y"] if y is None else y
            cell = ((ny >> 8) << 6) | (nx >> 8)
            self.m.write(self.base + base + 0, bytes([nx & 255, nx >> 8, ny & 255, ny >> 8]))
            self.m.write(self.base + base + 12, bytes([cell & 255, cell >> 8]))
            if (state if state is not None else a["state"]) not in (0, 7, 8):
                v = self.m.read(self.base + mapb + cell, 1)[0]
                if not v & 2:                   # (97.8: its nibble is a material)
                    self.m.write(self.base + mapb + cell, bytes([v | 0x20]))
        if state is not None:
            self.m.write(self.base + base + 5, bytes([state]))
        if dir is not None:
            self.m.write(self.base + base + 6, bytes([dir]))
        if hp is not None:
            self.m.write(self.base + base + 8, bytes([hp]))
        if flags is not None:
            self.m.write(self.base + base + 10, bytes([flags]))
        if ang is not None:
            self.m.write(self.base + base + 14, bytes([ang & 255, ang >> 8]))
        if kind is not None:                    # 0 a guard, 1 a dog (wave 6)
            self.m.write(self.base + base + 4, bytes([kind]))
        if frame is not None:
            self.m.write(self.base + base + 9, bytes([frame]))
        if timer is not None:
            self.m.write(self.base + base + 7, bytes([timer]))

    def candidates(self):
        """This frame's sprite list (px_sc, px_nsc): [(frame, flags, actor)]
        - PXS_C_FR, PXS_C_FL and PXS_C_ACT of each twelve-byte record."""
        n = self.byte("px_nsc")
        b = self.bytes_("px_sc", 12 * 8)
        return [(b[12 * k + 9], b[12 * k + 10], b[12 * k + 11]) for k in range(min(n, 8))]

    def door(self, i):
        """Door i's record (PXD_*)."""
        b = self.bytes_("px_doors", 8 * (i + 1))[8 * i:]
        return dict(cell=u16(b, 0), flags=b[2], lock=b[3], pos=u16(b, 4), state=b[6],
                    timer=b[7])

    def door_poke(self, i, pos=None, state=None, timer=None):
        base = self.s["px_doors"] + 8 * i
        if pos is not None:
            self.m.write(self.base + base + 4, bytes([pos & 255, pos >> 8]))
        if state is not None:
            self.m.write(self.base + base + 6, bytes([state]))
        if timer is not None:
            self.m.write(self.base + base + 7, bytes([timer]))

    def doors(self):
        return [self.door(i) for i in range(self.byte("px_ndoors"))]

    def actors(self):
        return [self.actor(i) for i in range(self.byte("px_nact"))]

    def player(self):
        return dict(health=self.byte("px_health"), ammo=self.byte("px_ammo"),
                    keys=self.byte("px_keys"), weapon=self.byte("px_weapon"),
                    lives=self.byte("px_lives"), state=self.byte("px_state"),
                    score=self.word("px_score"), floor=self.byte("px_floor"),
                    cell=self.word("px_pcell"), aim=self.byte("px_aim"))

    def eye(self):
        return self.word("px_px"), self.word("px_py"), self.word("px_head")

    # --- the states (wave 4, 97.13) ------------------------------------------
    def gstate(self):
        return self.byte("px_state")

    def wait_state(self, name, limit=60.0):
        """Poll FAST: READY stands 27 ticks, 1.5 s of the guest's and ~0.2 s
        of the host's at MartyPC's pace - the first cut polled every 0.2 s
        and missed it whole."""
        want = PXST[name]
        os88marty.until(self.m, lambda mm: self.byte("px_state") == want,
                        "the %s state" % name.upper(), poll=0.01, limit=limit)

    def start(self, limit=90.0):
        """Space on the attract page: a new game, READY, then PLAY on
        READY's own clock (27 ticks). No second Space: one arriving in PLAY
        is a held key px_keys_tick reads as Use."""
        if self.byte("px_state") == PXST["play"]:
            return
        self.m.key("Space")
        self.wait_state("play", limit=limit)
        release_held(self.m, ("Space",), "the start")   # (a lost break here
        # is a Space held into PLAY, px_keys_tick's Use, every step)

    # --- driving it ---------------------------------------------------------
    def _mark(self):
        """px_frames as it stands, read PAUSED before a poke that owes a
        frame - the baseline wait_frames counts from. Read after the run and
        the forced frame may already be in it, and a still scene then never
        produces another: the wait burned its whole limit and read as a
        package hang (latent in wave 1's first cut)."""
        self.f0 = self.word("px_frames")

    def eye_poke(self, px, py, head):
        """The eye and heading, with the heading's cos/sin - the package
        derives them when the heading CHANGES and nothing changed it. The
        guest must be PAUSED."""
        self.poke_word("px_px", px)
        self.poke_word("px_py", py)
        self.poke_word("px_head", head)
        self.poke_word("px_hcos", pxssim.cos_q14(head) & 0xFFFF)
        self.poke_word("px_hsin", pxssim.sin_q14(head) & 0xFFFF)

    def pcell_poke(self, px, py):
        """The player's cell and its PXC_PLAYER mark to (px, py): what
        px_pmark does when the step moves the eye."""
        mapb = self.s["px_map"]
        old = self.word("px_pcell")
        v = self.m.read(self.base + mapb + old, 1)[0]
        if not v & 2:                           # never a mark on a DOOR cell
            self.m.write(self.base + mapb + old, bytes([v & ~0x40]))
        cell = ((py >> 8) << 6) | (px >> 8)
        self.poke_word("px_pcell", cell)
        v = self.m.read(self.base + mapb + cell, 1)[0]
        if not v & 2:                           # (97.8: its nibble is a material)
            self.m.write(self.base + mapb + cell, bytes([v | 0x40]))

    def force_all_poke(self):
        """px_force_all, done by the gate (SPEC.md 97.5): the seven history
        arrays to 'the whole view was drawn' and px_force set, so the next
        frame WRITES and presents every row of every column. px_force alone
        only bypasses the idle predicate - the frame then composes against
        an unchanged memory and writes what moved, nothing on a still eye -
        which is what wave 1's first measurement poked and called a full
        repaint. The guest must be PAUSED."""
        for name, fill in FORCE_ALL:
            self.poke(name, [fill] * (2 * COLMAX))
        self.poke_byte("px_force", 1)

    def scene(self, which, force=True):
        """Put the eye at a pinned scene and owe a WHOLE frame (a full
        repaint, force_all_poke). Paused around the pokes. The player's
        cell mark follows the eye (px_pmark reads px_pcell), so the mark is
        moved here as the step would move it."""
        px, py, head = scene_at(which)
        self.m.pause()
        self._mark()
        self.eye_poke(px, py, head)
        self.pcell_poke(px, py)
        # ...and NOTHING LATCHED that would move it: a typematic arrow still
        # in the BIOS buffer from a leg that turned the player becomes a tap
        # (SPEC.md 97.8) spent on the next step, and the scene measured is
        # 64 units off the one poked. One run read scene A 4 ms faster than
        # three others that way.
        for k in ("px_tap", "px_kturn", "px_kfwd", "px_kstr"):
            self.poke_byte(k, 0)
        if force:
            self.force_all_poke()
        self.m.run()
        return px, py, head

    def pin(self, rung="flat", lowres=True, size=None):
        """A Detail and Resolution pick (and a Size, 48..80), applied by the
        package between frames (px_pend, SPEC.md 97.8) exactly as the menu's
        is."""
        self.m.pause()
        self._mark()
        self.poke_byte("px_detail", PXD[rung])
        self.poke_byte("px_res", 1 if lowres else 0)
        if size is not None:
            self.poke_byte("px_sizeix", SIZES.index(size))
        self.poke_byte("px_pend", 1)
        self.m.run()

    def margins(self):
        """The bytes of every device row of the view OUTSIDE the band, read
        off the framebuffer in the bracket (CGA 320x200x4's two banks from
        row 28, the Hercules box's four from row 134 at x = 40): what a Size
        picked narrower inside the bracket must have blacked (px_band_blank,
        SPEC.md 97.3). None on a backend this does not model."""
        back = PXB.get(self.byte("px_back"))
        size, x0 = self.byte("px_size"), self.byte("px_x0")
        out = bytearray()
        for r in range(ROWS):
            if back == "cga4":
                y = 28 + r
                off = 0xB8000 + (y & 1) * 0x2000 + (y >> 1) * 80
            elif back == "herc":
                y = 134 + r
                off = 0xB0000 + (y & 3) * 0x2000 + (y >> 2) * 90 + 5
            else:
                return None
            row = self.m.read(off, STRIDE)
            out += row[:x0] + row[x0 + size:]
        return bytes(out)

    def turn(self, px, py, head):
        """One TURN frame the way frame_times' "turn" mode makes one: the
        heading poked with px_dirty set and NOTHING forced, so the compose
        writes what moved against its memory of the last frame - the skip,
        the two-ends arm and px_wrun's paths (SPEC.md 97.5), which a forced
        frame never takes. Paused around the pokes; the caller waits. A
        STEP is the same poke with px/py moved and the heading kept - the
        motion that holds u still while h grows, which the Textured skip
        must see through px_lh (tests/pxssim.py's --steps)."""
        self.m.pause()
        self._mark()
        self.eye_poke(px, py, head)
        self.pcell_poke(px, py)
        self.poke_byte("px_dirty", 1)
        self.m.run()

    def force(self):
        """Owe a full repaint (px_force_all's memory), and run."""
        self.m.pause()
        self._mark()
        self.force_all_poke()
        self.m.run()

    def ticks(self):
        """The BIOS tick count (0040:006C), the clock the steps are owed by."""
        return int.from_bytes(self.m.read(0x46C, 4), "little")

    def key_edge(self, name, down, step=20000, most=400, resend=25):
        """Press (down=True) or release `name` on a PAUSED machine and step it
        in guest cycles until the kernel's key map (kbd_dnmap, what
        OSAPI_KEY_DOWN reads) agrees - and answer the BIOS tick at that
        moment. The machine is left PAUSED.

        THE WALK'S TWO ENDS, measured by the guest. They were read off a
        RUNNING machine around `m.key()`, so the host's round trips were in
        the bracket: at a lane of four the eye walked 21 ticks' worth while
        the row counted 17 (pixelstein-win, once `alone` came off it). A
        key held down is a level the package reads once a tick, so the
        kernel's map turning over IS the edge the step clock sees, and
        tools/os88mouse.py's double-click is stepped the same way."""
        # A LOST EDGE IS SENT AGAIN, every `resend` steps (~0.1 guest s):
        # release_held's finding, that a break code the guest never saw
        # leaves the kernel believing the key held - the final run of this
        # change lost the ArrowUp break on the CGA and C160 brackets and
        # waited out all 8M cycles. The tick answered is still the one at
        # which the kernel's map turned over, so a resend costs the
        # measurement nothing; os88ui's _edge_until is the same cure.
        m = self.m
        base = m.sym("kbd_dnmap")
        sc = SCAN[name]
        m.pause()
        sent = 0
        for i in range(most):
            if i % resend == 0:
                m.key(name, down=down, up=not down)
                sent += 1
            bit = m.read(base + (sc >> 3), 1)[0] & (1 << (sc & 7))
            if bool(bit) == down:
                if sent > 1:
                    print("   pxslib: %s %s reached the kernel on send %d of the "
                          "edge" % (name, "down" if down else "up", sent))
                return self.ticks()
            m.advance(cycles=step)
        raise os88marty.MartyError(
            "%s %s: the kernel's key map did not follow in %d guest cycles, "
            "sent %d times" % (name, "down" if down else "up", most * step, sent))

    def kticks(self):
        """The KERNEL's tick word - what OSAPI_GET_TICKS answers, and so the
        clock px_ahold (97.8's hold-down) is written in. It runs ~200 ticks
        behind the BIOS count (it starts at the kernel's boot, not the
        ROM's), which a first cut of tests/pxsauto.py read as a hold-down
        that had already passed."""
        return u16(self.m.read(self.m.sym("ticks"), 2))

    def wait_frames(self, n, limit=60.0, guest=None, f0=None):
        """Run until px_frames has climbed by n over the baseline the last
        scene()/pin()/force() took while paused (or over now, if none)."""
        if f0 is None:
            f0 = self.f0 if self.f0 is not None else self.word("px_frames")
        self.f0 = None
        os88marty.until(self.m, lambda mm: (self.word("px_frames") - f0) & 0xFFFF >= n,
                        "%d frame(s)" % n, poll=0.2, limit=limit, guest=guest)
        return self.word("px_frames")

    def enter_fsx(self, limit=60.0):
        self.m.type_text("f")
        os88marty.until(self.m, lambda mm: self.byte("px_inbr") == 1 and
                        self.byte("px_back") not in WINDOWED, "the bracket",
                        poll=0.3, limit=limit)

    def leave_fsx(self, limit=60.0):
        """Esc, then PAST the exit path: px_inbr clears at the top of it, and
        what follows - the window's set regenerated and re-transposed when
        the window's rung is Textured (97.3, ~0.9 s on the 5150; nothing
        when it is Flat), Auto re-seated, the window's frame composed -
        consumes a px_force poked meanwhile (a first cut poked and waited
        for a frame that the exit path had already spent; a second waited
        on px_back = WIN1, which flips at the TOP of that path). px_brn is
        incremented where OSAPI_FSX_RUN returns, after all of it."""
        n = self.word("px_brn")
        self.m.key("Escape")
        os88marty.until(self.m, lambda mm: self.word("px_brn") != n,
                        "the desktop", poll=0.3, limit=limit)

    # --- reading the frame back ---------------------------------------------
    def columns(self):
        n = self.byte("px_cols")
        out = dict(cols=n, top=list(self.bytes_("px_top", n)),
                   bot=list(self.bytes_("px_bot", n)),
                   mat=list(self.bytes_("px_mat", n)),
                   side=list(self.bytes_("px_side", n)),
                   u=list(self.bytes_("px_u", n)))
        wh = self.bytes_("px_wallh", n * 2)
        out["wallh"] = [u16(wh, i * 2) for i in range(n)]
        return out

    def shadow(self):
        """The 80 x 80 bytes the last frame composed: the shadow claim on
        every backend but Mode X, which composes straight into the hidden
        page (SPEC.md 97.3) - the page SHOWN after the flip is px_page ^ 1,
        its view rows at 48 * 80 into it, and with the map mask at 0Fh all
        four planes hold the byte, so plane 0 (what a linear read of A000
        answers) is the picture."""
        if self.byte("px_back") == 4:                     # PXB_MODEX
            seg = u16(self.bytes_("px_fsi", 2))
            page = self.byte("px_page") ^ 1
            return self.m.read((seg << 4) + page * 19200 + 48 * STRIDE,
                               STRIDE * ROWS)
        seg = self.word("px_shseg")
        return self.m.read(seg << 4, STRIDE * ROWS)

    def state(self):
        return dict(back=PXB.get(self.byte("px_back")), rung=self.byte("px_rung"),
                    lowres=self.byte("px_lowres"), cols=self.byte("px_cols"),
                    detail=self.byte("px_detail"), apos=self.byte("px_apos"),
                    frames=self.word("px_frames"), inbr=self.byte("px_inbr"),
                    gen=self.byte("px_gen"), tier=self.byte("px_tier"),
                    size=self.byte("px_size"), texok=self.byte("px_texok"))

    def stage_times(self, n, mode="full"):
        """n consecutive DRAWN frames in the BRACKET, split by stage: cycles
        from px_frame_begin to px_cast (the prologue: pit_now, a pending
        apply), px_cast to px_spr_gather (THE CAST), px_spr_gather to
        px_compose (THE GATHER: the sprite candidates, the match and the
        erase - its own bracket since review r1, which found 9.2 ms of it
        booked as cast), px_compose to px_present (THE COMPOSE, and the
        sprite posts and the weapon after it), px_present to px_frame_end
        (THE PRESENT) and px_frame_end round to the next px_frame_begin (the
        loop: the frame's epilogue, the selector, int 16h, px_steps, the
        keys). Six exec breakpoints, the stops taken in that order because a
        forced frame never returns clean; the same two modes as frame_times.
        The first frame is dropped. This is what turns 'the units-derived
        frame is 30% light' into a stage the residual belongs to."""
        m = self.m
        names = ("px_frame_begin", "px_cast", "px_spr_gather", "px_compose",
                 "px_present", "px_frame_end")
        m.bp_exec(*[self.addr(x) for x in names])
        m.run()
        # get to a px_frame_begin stop, whatever stop came first
        for _ in range(6):
            m.wait_stop(30)
            st = m.status()
            flat = ((st.get("cs", 0) << 4) + st.get("ip", 0)) & 0xFFFFF
            if flat == self.addr("px_frame_begin"):
                break
            m.run()
        else:
            sys.exit("pxslib: never stopped at px_frame_begin")
        out = []
        head = self.word("px_head")
        for i in range(n + 1):
            if mode == "full":
                self.force_all_poke()
            elif mode == "turn":
                head = (head + PX_TURN) & 0xFFF
                self.eye_poke(self.word("px_px"), self.word("px_py"), head)
                self.poke_byte("px_dirty", 1)
            elif mode == "sim":
                # the FINISHED frame (wave 3, 97.1's fork): a full repaint
                # with the world's sim running - every stop also forces the
                # doors' and guards' clocks on by not freezing them
                self.force_all_poke()
            c = [m.status()["cycles"]]
            tw = 0
            for _ in range(6):              # cast, gather, compose, present, end, begin
                m.run()
                m.wait_stop(30)
                c.append(m.status()["cycles"])
                if len(c) == 6:
                    # at px_frame_end: the waits px_wait_* bracketed OUT of
                    # px_ftime this frame (Mode X's OSAPI_FSX_PAGE retrace,
                    # another task's gfx lock), 838 ns units = 4 clk each.
                    # The present stage INCLUDES them; this splits it
                    # (review, wave 6's close)
                    tw = 4 * self.dword("px_twait")
            d = [c[k + 1] - c[k] for k in range(6)]
            if i:
                out.append(dict(prologue=d[0], cast=d[1], gather=d[2], compose=d[3],
                                present=d[4], loop=d[5], frame=c[6] - c[0], wait=tw))
        m.bp_exec()
        m.run()
        return out

    def frame_times(self, n, mode="full", pose=None):
        """n consecutive DRAWN frames, each as (cycles between two entries to
        px_frame_begin, the package's own px_ftime in 838ns units). The
        breakpoint is on px_frame_begin, and at every stop the frame is made
        to draw (tests/tankperf.py's method; the delta of MartyPC's cycle
        counter between stops is the whole loop - input, the owed steps,
        cast, compose, present). The first delta is dropped: it spans the
        poke. `pose`, if given, is called at every stop (the guest paused at
        the breakpoint) to put the world back - the "sim" mode's guards see
        the player and walk, and a measurement of "three sprites in view"
        that lets them leave the view measures something else (review r1:
        scene C's 64 x 80 finished frame was read with two). Two modes
        (SPEC.md 97.10):

          "full"  a FULL REPAINT: force_all_poke at every stop, so every
                  column writes every row and the present sends all 80 -
                  the dearest frame, the one the promise is asserted on;
          "turn"  the heading stepped by PX_TURN at every stop with px_dirty
                  set and nothing forced: the delta-fill writes what a turn
                  changes and the present sends the rows it touched - the
                  frame a player sees (and its rows are asserted against
                  present_rows: 80 on both pinned scenes, SPEC.md 97.5).

        px_force alone, which the first cut poked, is neither: on a still
        eye it composes against an unchanged memory and writes nothing."""
        m = self.m
        m.bp_exec(self.addr("px_frame_begin"))
        m.run()
        m.wait_stop(30)
        out = []
        c0 = m.status()["cycles"]
        head = self.word("px_head")
        for i in range(n + 1):
            if mode == "full":
                self.force_all_poke()
            elif mode == "turn":
                head = (head + PX_TURN) & 0xFFF
                self.eye_poke(self.word("px_px"), self.word("px_py"), head)
                self.poke_byte("px_dirty", 1)
            elif mode == "sim":
                self.force_all_poke()
            else:
                raise ValueError(mode)
            if pose:
                pose()
            m.run()
            m.wait_stop(30)
            c1 = m.status()["cycles"]
            ft = self.dword("px_ftime")
            if i:
                out.append((c1 - c0, ft))
            c0 = c1
        m.bp_exec()
        m.run()
        return out


def level():
    """E1M1 parsed, the level every pinned scene stands in."""
    return pxssim.pxslevel.parse(os.path.join(pxssim.pxslevel.DEFAULT_DIR, "e1m1.txt"))


def scene_at(which):
    """(px, py, head) of pinned scene 'a', 'b' or 'c' (tools/pxssim.py's
    scenes; C is wave 3's sprite scene)."""
    return pxssim.scenes(level())[which]


def _key(col, rung):
    """What a change is, per rung (pxcomp.inc's skips): Flat and Wire the
    four bytes, Textured those plus the quantised height and the texture
    column u >> 3 (the six bytes of 97.3's memory)."""
    k = (col["top"], col["bot"], col["mat"], col["side"])
    return k + (col["hq"], col["u"] >> 3) if rung == "tex" else k


def turn_stores(which, cols=32):
    """What the Flat delta-fill WRITES when scene `which` turns by PX_TURN,
    on the host (SPEC.md 97.10): (stores, columns changed). The wall rows of
    every column whose (top, bot, mat, side) moved, plus the ceiling or
    floor put back where it shrank - tools/pxssim.py's arithmetic, the
    package's rule (pxcomp.inc's Flat arm). Scene B is pinned to be the
    heavier of the two here, and tests/pixelstein.py asserts it before it
    boots anything."""
    lv = level()
    px, py, head = scene_at(which)
    a = pxssim.cast_view(lv.cells, px, py, head, cols)
    b = pxssim.cast_view(lv.cells, px, py, (head + PX_TURN) & 0xFFF, cols)
    st = chg = 0
    for x, y in zip(a, b):
        if (x["top"], x["bot"], x["mat"], x["side"]) == \
           (y["top"], y["bot"], y["mat"], y["side"]):
            continue
        chg += 1
        st += y["bot"] - y["top"] + 1
        if y["top"] > x["top"]:
            st += y["top"] - x["top"]
        if y["bot"] < x["bot"]:
            st += x["bot"] - y["bot"]
    return st, chg


def _wire_runs(view):
    """Per column the two Wire runs (u0, u1, l0, l1) pxcomp.inc's .wire arm
    derives: the edge row alone, widened to the neighbour's edge where the
    neighbour's (mat, side, top, bot) differs."""
    out = []
    prev = None
    for col in view:
        t, b = col["top"], col["bot"]
        u0 = u1 = t
        l0 = l1 = b
        key = (col["mat"], col["side"], t, b)
        if prev is not None and prev != key:
            pt, pb = prev[2], prev[3]
            u0, u1 = min(t, pt), max(t, pt)
            l0, l1 = min(b, pb), max(b, pb)
        out.append((u0, u1, l0, l1))
        prev = key
    return out


def _wrun_rows(new, old, clip):
    """The rows px_wrun dirties for one run: None (it stood), or (lo, hi)."""
    dl, dh = new
    al, ah = old
    if new == old:
        return None
    if dl == dh and al == ah and al >= clip and dl >= clip:
        return min(al, dl), max(al, dl)
    lo = max(min(dl, al), clip)
    hi = max(dh, ah)
    if lo > hi:
        return None
    return lo, hi


def present_rows(prev, new, rung="flat"):
    """How many rows the present sends after composing `new` over `prev`
    (two cast_view lists of the same column count): the LEAST and GREATEST
    row any column wrote, as pxcomp.inc computes px_r0..px_r1 (SPEC.md 97.5)
    - a Flat column that changed dirties min(t, lt)..max(b, lb); a Wire
    column whose runs moved dirties what px_wrun writes. 0 if nothing was
    written.

    This is the host's model of the range the gate reads back; it is what
    makes `rows presented` an assertion rather than a number. The rule it
    encodes is why the range is the whole band on every frame a player sees:
    one min/max over ALL columns, and a column nearer than 2.5 tiles spans
    rows 0..79."""
    r0, r1 = 0xFF, 0
    if rung in ("flat", "tex"):
        for x, y in zip(prev, new):
            if _key(x, rung) == _key(y, rung):
                continue
            r0 = min(r0, y["top"], x["top"])
            r1 = max(r1, y["bot"], x["bot"])
    else:
        for o, n in zip(_wire_runs(prev), _wire_runs(new)):
            if o == n:
                continue
            for got in (_wrun_rows(n[:2], o[:2], 0),
                        _wrun_rows(n[2:], o[2:], n[1] + 1)):
                if got is not None:
                    r0, r1 = min(r0, got[0]), max(r1, got[1])
    return (r1 - r0 + 1) if r1 >= r0 else 0


def turn_present_rows(which, cols, rung, k, pages=1):
    """The rows tests/pixelstein.py's TURN measurement presents on its last
    frame: scene `which` turned k times by PX_TURN, composed against the
    memory of `pages` frames before (2 on Mode X, whose history is per
    page)."""
    lv = level()
    px, py, head = scene_at(which)
    new = pxssim.cast_view(lv.cells, px, py, (head + k * PX_TURN) & 0xFFF, cols, rung)
    prev = pxssim.cast_view(lv.cells, px, py, (head + (k - pages) * PX_TURN) & 0xFFF,
                            cols, rung)
    return present_rows(prev, new, rung)


PXST = dict(play=0, dying=1, attract=2, ready=3, done=4, over=5, enter=6,
            demo=7)                     # pxgame.asm's PXST_* (97.13)


def open_file(m, name, title, S=None, folder=None):
    """B:, then `folder` if one is given, then the package `name` - each step
    an os88ui verb, so each one CONFIRMS on guest state - and answer the
    (window index, part 0's segment) of the window it opened.

    This replaced a hand-rolled launch that double-clicked up to THREE times,
    and a B: icon opened up to twice, because a double-click's two presses
    were then spaced by host round trips and a loaded box let them cross the
    kernel's 9-tick window (wave 4 measured 6 launches in ~20 lost). The
    presses are stepped in GUEST cycles now (tools/os88mouse.py DBL_STEP), and
    the retries went out of every other row with that fix (the Weave rows'
    among them): a retry that covers nothing could only hide a mouse path
    that broke. os88ui.open waits for the NEW window rather than a settle, so
    a running game - which never settles again - is not a problem for it."""
    S = S or os88sym.linear
    ui = os88ui.UI(m, verbose=False, sym=S)
    ui.open_drive("B")
    if folder:
        ui.open(folder)
    ui.open(name)
    got = find(m, S, guest=30.0, title=title)
    if got is None:
        sys.exit("pxslib: %s opened a window, and no '%s' window has its "
                 "segment written" % (name, title))
    return ui, got


# the kernel's key map (kbd_dnmap, the bitmap OSAPI_KEY_DOWN reads, SPEC.md
# 9.7) - scancodes by the names os88marty.key takes
SCAN = {"ArrowLeft": 0x4B, "ArrowRight": 0x4D, "ArrowUp": 0x48, "ArrowDown": 0x50,
        "Space": 0x39, "Enter": 0x1C}


def release_held(m, names, why=""):
    """Every key in `names` UP in the kernel's own key map before a row relies
    on it - and each one read down is released AGAIN and named.

    A break code the guest never saw leaves the kernel believing the key is
    held, and the package reads OSAPI_KEY_DOWN: tests/pixelstein.py's c160
    run spun the heading through every pose while it was POKED (review, wave
    6 r2), and tests/pxsact.py's leg (j) waited 180 guest seconds on a card's
    hold that re-arms every step while Space or Enter reads down (px_timers,
    97.13; wave 6's verification). One guard, here, for every row."""
    base = os88sym.linear("kbd_dnmap")
    held = []
    for name in names:
        sc = SCAN[name]
        m.pause()
        bit = m.read(base + (sc >> 3), 1)[0] & (1 << (sc & 7))
        m.run()
        if bit:
            held.append(name)
            m.key(name, down=False, up=True)
    if held:
        m.advance(frames=10)
        m.run()
        print("   A KEY READ DOWN after its release%s: %s - the guest lost the break "
              "code; released again" % (" (" + why + ")" if why else "", ", ".join(held)))
    return held


def open_game(m, apps_root=True, S=None, play=True):
    """Boot to the desktop, open B:, launch PXSTEIN.O88 and answer a Game.

    `apps_root`: the file is at the ROOT of games360.img (SPEC.md 24.6) and in
    GAMES/ on the general apps disks.

    `play`: SINCE WAVE 4 THE GAME OPENS ON THE ATTRACT PAGE (97.13), so every
    row that measures or drives the world presses Space here and waits past
    READY to PLAY - the first floor loaded afresh, which is what the rows
    were written against. tests/pxsstate.py passes play=False: the states
    are its subject."""
    S = S or os88sym.linear
    os88marty.settle(m)
    os88marty.no_saver(m)           # every PIXELSTEIN row drives for guest
                                    # MINUTES with no key: the idle saver
                                    # came up mid-row (px_hidden set by an
                                    # empty clip, the frame counter still)
                                    # once tests/pxssim.py grew a third scene
    ui, (win, seg) = open_file(m, FILE, TITLE, S,
                               folder=None if apps_root else "GAMES")
    g = Game(m, win, seg)
    os88marty.until(m, lambda mm: g.word("px_frames") >= 1, "the first frame",
                    poll=0.3, limit=120.0)
    ui.mo.to(4, 4)                          # the pointer parked off the window
    if play:
        g.start()
    return g


def ms(cycles, hz=4772727.0):
    return cycles / hz * 1000.0


def median(v):
    v = sorted(v)
    return v[len(v) // 2] if v else 0
