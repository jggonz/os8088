#!/usr/bin/env python3
"""PIXELSTEIN 3D's guest reader (SPEC.md 96.12): what every emulator row of the
package imports rather than rewrites.

The package's variables are read out of its own bss - never inferred from the
glass - by re-assembling apps/pixelstein/pxgame.asm with `[map symbols]`
(tests/cycweb.py's pkg_syms, the way tests/pxsbench.py finds pb_res) and
locating the program's segment through the window it opened: the window's
W_SEG names PART 0 after the loader's re-home (SPEC.md 20.12.10), which is
the segment every equate below is relative to. The loader's part table is
gone by then, so the scratch part and the level part are found through the
handoff words the loader left at the head of the bss (96.9's PXH_*).

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
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# tools/ LAST, so it WINS: tests/pxssim.py (the row) shadows tools/pxssim.py
# (the reference renderer) by name, and `import pxssim` below must find the
# renderer. The first cut inserted tools/ first and tests/ second - tests/
# at sys.path[0] - and worked only because the os88marty import line
# re-inserted tools/ at 0 as a side effect
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty, os88mouse, os88sym, os88geom, os88build, dispcp   # noqa: E402
import os88parts                                                 # noqa: E402
from cycweb import pkg_syms, u16                                # noqa: E402
import pxssim                                                    # noqa: E402
assert hasattr(pxssim, "render"), "pxslib: `pxssim` resolved to tests/, not tools/"

TITLE = "Pixelstein 3D"
FILE = "PXSTEIN.O88"
ROWS, STRIDE, X0, BAND = 80, 80, 8, 64
PXB = {0: "none", 1: "cga4", 2: "cga16", 3: "herc", 4: "modex", 5: "win1"}
PXR = {"wire": 0, "flat": 1}
PXD = {"auto": 0, "wire": 1, "flat": 2}
PXH = dict(magic=0, lev=2, gen=4, cold=6, nlev=8, levlen=10)   # pxstein.asm's PXH_*
# the two pinned scenes (96.10): A is the spawn, B the doorway turn - pinned
# in tools/pxssim.py's scenes() and read from there, one source
COLMAX, HORIZON = 80, 40                # PX_COLMAX, PX_HORIZON (pxgame.asm)
PX_TURN, PX_SPEED = 64, 24              # a step's turn and walk (96.8)
# px_force_all's SEVEN arrays and their fills (pxcomp.inc): the memory of
# "the whole view was drawn", so the next frame writes and presents all of
# it. Not px_lside: the Flat skip's lmat compare fires first, and Wire has
# no tone to compare
FORCE_ALL = (("px_ltop", 0), ("px_lbot", 79), ("px_lmat", 0xFF),
             ("px_lu0", 0), ("px_lu1", HORIZON - 1), ("px_ll0", HORIZON),
             ("px_ll1", 79))

_SYMS = None


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
                             os.path.join(ROOT, "apps", "pixelstein") + os.sep))
        try:
            raw = open(os88build.at("build/pxstein.o88"), "rb").read()
        except OSError:
            sys.exit("pxslib: no build/pxstein.o88 - run `make`")
        built = os88parts.part_bytes(raw, 0)
        if built != image:
            sys.exit("pxslib: part 0 of build/pxstein.o88 is %d bytes and the "
                     "source assembles to %d - the package is BEHIND THE TREE. "
                     "Run `make`." % (len(built), len(image)))
        _SYMS = s
    return _SYMS


def find(m, S=None, limit=120.0):
    """(window index, part 0's segment) of the game's window, or None."""
    S = S or os88sym.linear
    t0 = time.time()
    while time.time() - t0 < limit:
        for w in os88geom.windows(m, S):
            if w.title.startswith(TITLE):
                seg = u16(m.read(os88geom.winptr(m, w.i, S) + os88geom.W_SEG, 2))
                if seg:
                    return w.i, seg
        time.sleep(0.3)
    return None


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
        h = self.bytes_("px_hand", 12)                  # PXH_SIZE
        return {k: u16(h, v) for k, v in PXH.items()}

    def eye(self):
        return self.word("px_px"), self.word("px_py"), self.word("px_head")

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

    def force_all_poke(self):
        """px_force_all, done by the gate (SPEC.md 96.5): the seven history
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
        repaint, force_all_poke). Paused around the pokes."""
        px, py, head = scene_at(which)
        self.m.pause()
        self._mark()
        self.eye_poke(px, py, head)
        # ...and NOTHING LATCHED that would move it: a typematic arrow still
        # in the BIOS buffer from a leg that turned the player becomes a tap
        # (SPEC.md 96.8) spent on the next step, and the scene measured is
        # 64 units off the one poked. One run read scene A 4 ms faster than
        # three others that way.
        for k in ("px_tap", "px_kturn", "px_kfwd", "px_kstr"):
            self.poke_byte(k, 0)
        if force:
            self.force_all_poke()
        self.m.run()
        return px, py, head

    def pin(self, rung="flat", lowres=True):
        """A Detail and Resolution pick, applied by the package between frames
        (px_pend, SPEC.md 96.8) exactly as the menu's is."""
        self.m.pause()
        self._mark()
        self.poke_byte("px_detail", PXD[rung])
        self.poke_byte("px_res", 1 if lowres else 0)
        self.poke_byte("px_pend", 1)
        self.m.run()

    def turn(self, px, py, head):
        """One TURN frame the way frame_times' "turn" mode makes one: the
        heading poked with px_dirty set and NOTHING forced, so the compose
        writes what moved against its memory of the last frame - the skip,
        the two-ends arm and px_wrun's paths (SPEC.md 96.5), which a forced
        frame never takes. Paused around the pokes; the caller waits."""
        self.m.pause()
        self._mark()
        self.eye_poke(px, py, head)
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

    def kticks(self):
        """The KERNEL's tick word - what OSAPI_GET_TICKS answers, and so the
        clock px_ahold (96.8's hold-down) is written in. It runs ~200 ticks
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
                        self.byte("px_back") != 5, "the bracket", poll=0.3,
                        limit=limit)

    def leave_fsx(self, limit=60.0):
        self.m.key("Escape")
        os88marty.until(self.m, lambda mm: self.byte("px_inbr") == 0,
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
        page (SPEC.md 96.3) - the page SHOWN after the flip is px_page ^ 1,
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
                    gen=self.byte("px_gen"), tier=self.byte("px_tier"))

    def stage_times(self, n, mode="full"):
        """n consecutive DRAWN frames in the BRACKET, split by stage: cycles
        from px_frame_begin to px_cast (the prologue: pit_now, a pending
        apply), px_cast to px_compose (THE CAST), px_compose to px_present
        (THE COMPOSE), px_present to px_frame_end (THE PRESENT) and
        px_frame_end round to the next px_frame_begin (the loop: the frame's
        epilogue, the selector, int 16h, px_steps, the keys). Five exec
        breakpoints, the stops taken in that order because a forced frame
        never returns clean; the same two modes as frame_times. The first
        frame is dropped. This is what turns 'the units-derived frame is 30%
        light' into a stage the residual belongs to."""
        m = self.m
        names = ("px_frame_begin", "px_cast", "px_compose", "px_present",
                 "px_frame_end")
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
            c = [m.status()["cycles"]]
            for _ in range(5):              # cast, compose, present, end, begin
                m.run()
                m.wait_stop(30)
                c.append(m.status()["cycles"])
            d = [c[k + 1] - c[k] for k in range(5)]
            if i:
                out.append(dict(prologue=d[0], cast=d[1], compose=d[2],
                                present=d[3], loop=d[4], frame=c[5] - c[0]))
        m.bp_exec()
        m.run()
        return out

    def frame_times(self, n, mode="full"):
        """n consecutive DRAWN frames, each as (cycles between two entries to
        px_frame_begin, the package's own px_ftime in 838ns units). The
        breakpoint is on px_frame_begin, and at every stop the frame is made
        to draw (tests/tankperf.py's method; the delta of MartyPC's cycle
        counter between stops is the whole loop - input, the owed steps,
        cast, compose, present). The first delta is dropped: it spans the
        poke. Two modes (SPEC.md 96.10):

          "full"  a FULL REPAINT: force_all_poke at every stop, so every
                  column writes every row and the present sends all 80 -
                  the dearest frame, the one the promise is asserted on;
          "turn"  the heading stepped by PX_TURN at every stop with px_dirty
                  set and nothing forced: the delta-fill writes what a turn
                  changes and the present sends the rows it touched - the
                  frame a player sees (and its rows are asserted against
                  present_rows: 80 on both pinned scenes, SPEC.md 96.5).

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
            else:
                raise ValueError(mode)
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
    """(px, py, head) of pinned scene 'a' or 'b' (tools/pxssim.py's scenes)."""
    return pxssim.scenes(level())[which]


def turn_stores(which, cols=32):
    """What the Flat delta-fill WRITES when scene `which` turns by PX_TURN,
    on the host (SPEC.md 96.10): (stores, columns changed). The wall rows of
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
    row any column wrote, as pxcomp.inc computes px_r0..px_r1 (SPEC.md 96.5)
    - a Flat column that changed dirties min(t, lt)..max(b, lb); a Wire
    column whose runs moved dirties what px_wrun writes. 0 if nothing was
    written.

    This is the host's model of the range the gate reads back; it is what
    makes `rows presented` an assertion rather than a number. The rule it
    encodes is why the range is the whole band on every frame a player sees:
    one min/max over ALL columns, and a column nearer than 2.5 tiles spans
    rows 0..79."""
    r0, r1 = 0xFF, 0
    if rung == "flat":
        for x, y in zip(prev, new):
            if (x["top"], x["bot"], x["mat"], x["side"]) == \
               (y["top"], y["bot"], y["mat"], y["side"]):
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
    new = pxssim.cast_view(lv.cells, px, py, (head + k * PX_TURN) & 0xFFF, cols)
    prev = pxssim.cast_view(lv.cells, px, py, (head + (k - pages) * PX_TURN) & 0xFFF,
                            cols)
    return present_rows(prev, new, rung)


def open_game(m, apps_root=True, S=None):
    """Boot to the desktop, open B:, launch PXSTEIN.O88 and answer a Game.

    `apps_root`: the file is at the ROOT of games360.img (SPEC.md 24.6) and in
    GAMES/ on the general apps disks."""
    S = S or os88sym.linear
    os88marty.settle(m)
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    if not apps_root:
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "GAMES")
        wx, wy = dispcp.win_rect(m, S, disk)[:2]
    rows = [r[0] for r in dispcp.listing(m, S)]
    if FILE not in rows:
        sys.exit("pxslib: %s is not on the apps disk (%s)" % (FILE, rows))
    row = dispcp.scroll_to(m, mo, S, os88marty.settle, wx, wy, rows.index(FILE))
    x, y = dispcp.row_xy(wx, wy, row)
    mo.dblclick(x, y)                       # NOT open_named: a running game
    got = find(m, S)                        # never settles again
    if got is None:
        sys.exit("pxslib: %s did not open a '%s' window" % (FILE, TITLE))
    win, seg = got
    g = Game(m, win, seg)
    os88marty.until(m, lambda mm: g.word("px_frames") >= 1, "the first frame",
                    poll=0.3, limit=120.0)
    mo.to(4, 4)                             # the pointer parked off the window
    return g


def ms(cycles, hz=4772727.0):
    return cycles / hz * 1000.0


def median(v):
    v = sorted(v)
    return v[len(v) // 2] if v else 0
