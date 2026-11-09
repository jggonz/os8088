#!/usr/bin/env python3
"""PageDesk's FUNCTIONAL row on QEMU: every user-facing flow, on VGA and CGA.

    make pagedesk pagedeskdisk && python3 tests/pagedesk.py [--only vga|cga]

tests/pagedeskxt.py is PD-29's XT proof - memory, stacks and cycles on
MartyPC - and at 4.77 MHz one session there is an hour of guest time, so it
carries the few operations PD-29 names and no more. This row is the other
half: the flows a user takes, end to end, on a machine fast enough to take all
of them, with the result read back by readers that are not PageDesk's.

The machine is `make test`'s (tests/os88qemu.py make_test): the shipped 1.44MB
system disk in A: and a SCRATCH COPY of `make pagedeskdisk`'s 1.44MB disk in
B:, booted in place so its writes can be read back on the host, on this
process's own QMP socket and pidfile. Two legs:

    vga   build/os8088.img                  640x480, PageDesk's FULL layout
    cga   VIDEO=cga, built into a private   640x200, the COMPACT layout - Mark
          tree by tools/os88build.py        lives in More's popover there
          (build/ stays the shipped kernel)

THE SESSION, on each, with every step confirmed off PageDesk's own statics
(pdu_doc, pdu_job, pdu_page, pdu_npages, pdu_pop, pdu_tool, pdu_ed_n,
pdu_msg - read through the window's W_SEG with the package map that
tests/pagedeskxt.py re-assembles and checks byte-identical):

  1. the empty desk is clicked and the screen saver set to Never, then B:'s
     Disk window opens and XTGUIDE.PDF - the sample guide - is double-clicked
     (SPEC.md 54's association launches PageDesk on it);
  2. paged through (PgDn twice) and zoomed (+);
  3. Edit > Find... `Index`, which is on page 12 only: `Found on page 12`,
     and Esc leaves page 12 drawn;
  4. Annotate (F6): an area MARK dragged on the canvas, and a NOTE placed by
     a click, typed into and kept with Enter;
  5. File > Insert from PDF... MARKUP.PDF, page `1`: 13 pages;
  6. View > Preferences..., Up to `Flate cache`, Right to Off, Esc: the wake
     writes SYSTEM/APPDATA/PAGEDESK.CFG on the launch volume and says
     `Preferences saved.`;
  7. File > Save As... QMARK.PDF: `Saved`, and the re-open drawn;
  8. (VGA) back to page 12 of the RE-OPENED file, where the mark's light red
     must form a rectangle - every edge of its bounding box lit along at
     least 60% of its length. The same test passes on the in-session mark
     (94-100% on every edge) and the saved file draws four edges in mutool,
     so a miss is PageDesk drawing its own saved /Square. RED when written
     (8e00bef0d): left and bottom only, top and right 1%.

THEN, OFF THE FLOPPY on the host (tools/os88flush.py's FAT reader, no os8088
code), after QEMU has exited:

  * QMARK.PDF passes `qpdf --check` with exit 0 - qpdf's 3 is warnings and a
    fail - and its annotations, counted by /Subtype by qpdf's JSON, are
    XTGUIDE's own seven /Link plus exactly one /Square and one /Text, both on
    the twelfth page, in a document of 13 pages. Without qpdf on PATH this is
    a SKIP and the row says so; it never passes in qpdf's place;
  * PAGEDESK.CFG is 10 bytes, `PDCF` then the version, and its Flate byte
    (cfg[9]) is 1 - Off.

The heap is sampled through every step and the lowest unclaimed figure
printed (pagedeskxt's table shape); it is asserted there, on the 640 KiB XT,
not here - QEMU's 640 KiB is the same, but nothing this row adds needs it.
Timings are the guest's BIOS tick (55 ms) and are printed, not asserted: a
386 at the host's clock says nothing about an 8088.

QEMU and NOT Hercules: QEMU has no Hercules card and the HERCSEG harness's
fake framebuffer is ordinary RAM the heap does not reserve, so PageDesk's
claims land on it (docs/HERCULES-TESTING.md). Hercules is pagedeskxt's, on
MartyPC's real card.
"""
import argparse
import os
import struct
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.chdir(ROOT)

import os88build                                            # noqa: E402
import os88flush                                            # noqa: E402
import os88marty as M                                       # noqa: E402
import os88qemu                                             # noqa: E402
import os88ui                                               # noqa: E402
import pagedeskxt as X                                      # noqa: E402
from os88fixture import need                                # noqa: E402

SYSIMG = "build/os8088.img"
APPIMG = "build/pagedesk.img"
SAVE_AS = "QMARK.PDF"
NOTE_TEXT = "Checked on QEMU"
INSERT_FROM = "MARKUP.PDF"
P_NOTE, P_PREFS = 7, 8              # apps/pagedesk/pdu.h
C_T_NOTE = 19
CFG = "SYSTEM/APPDATA/PAGEDESK.CFG"
TICK_HZ = 1193182.0 / 65536.0
DBL_TICKS = 9                       # the kernel's double-click window
XT_LINKS = 7                        # XTGUIDE.PDF's own /Link annotations


# =============================================================================
# QEMU dressed as the machine os88ui and os88marty's readers expect
# =============================================================================
class QM(object):
    """`read`/`readseg`/`write`/`hmp`/`key`/`type_text`/`status` over one
    private QEMU. os88marty treats anything with `hmp` and no `advance` as
    QEMU (`_is_qemu`) and takes tests/os88qemu.py's twins for its snapshot
    reads; `status()` answers a cycle count made of the BIOS tick, so
    os88marty.until's GUEST budgets and stall check work unchanged."""

    KEYS = {"PageDown": "pgdn", "PageUp": "pgup", "Enter": "ret",
            "Escape": "esc", "Backspace": "backspace", "ArrowUp": "up",
            "ArrowDown": "down", "ArrowLeft": "left", "ArrowRight": "right",
            "F5": "f5", "F6": "f6", "F7": "f7", "F8": "f8"}

    def __init__(self, inst, sym):
        import xmcheck
        self.xm = xmcheck
        self.inst, self.sock, self.sym = inst, inst.sock, sym
        self.tmp = tempfile.mkdtemp(prefix="pdq-")
        self._clock = None

    def hmp(self, cmd):
        return self.xm.qmp(self.sock, cmd)

    def read(self, linear, n):
        p = os.path.join(self.tmp, "m.bin")
        # quoted: HMP parses a bare path as an EXPRESSION (tests/thewire.py)
        self.hmp('pmemsave 0x%X %d "%s"' % (linear, n, p))
        return open(p, "rb").read()

    def readseg(self, seg, off, n):
        return self.read(os88qemu.lin(seg, off), n)

    def lin(self, seg, off=0):
        return os88qemu.lin(seg, off)

    def write(self, linear, data):
        os88qemu.poke(self.hmp, linear, bytes(data))

    def status(self):
        if self._clock is None:
            self._clock = os88qemu.Clock(self)
        n = self._clock.step()
        return {"cycles": int(n * M.GUEST_HZ / TICK_HZ), "state": "running",
                "cs": 0, "ip": 0}

    def disk(self):
        # QEMU counts no int 13h reads: os88marty.ui_done compares this
        # before and after, so a constant leaves its other terms (ui_task
        # asleep, the queue empty, the lock free, the tick passed) to decide
        return {"reads": 0}

    def key(self, name):
        os88qemu.key(self, self.KEYS.get(name, name.lower()))

    def type_text(self, text):
        for ch in text:
            if ch == "+":               # not in os88qemu's table: shift-=
                os88qemu.sendkey(self.sock, "shift-equal")
            else:
                os88qemu.type_text(self.sock, ch)

    def frame(self):
        """(w, h, rgb24) of the VGA's screendump (P6, maxval 255)."""
        ppm = os.path.join(self.tmp, "s.ppm")
        self.hmp('screendump "%s"' % ppm)
        parts = open(ppm, "rb").read().split(b"\n", 3)
        w, h = (int(v) for v in parts[1].split())
        return w, h, parts[3][:w * h * 3]

    def shot(self, path):
        M.write_png_rgb(path, *self.frame())


class QMouse(object):
    """os88mouse.Mouse's verbs os88ui uses (to, where, click, dblclick,
    _edge, _sep) and drag, CLOSED-LOOP on the kernel's mouse_x/mouse_btn
    (tests/os88qemu.py mouse_to / mouse_button). The press-to-press
    separation is the GUEST's tick, as os88mouse's `_sep` - two ordinary
    clicks inside DBL_TICKS are a double-click to the kernel."""

    def __init__(self, q):
        self.q = q
        self._last = None

    def _h(self, cmd):
        return self.q.hmp(cmd)

    def where(self):
        return os88qemu.mouse_where(self.q, self.q.sym)

    def to(self, x, y, l=False, r=False, **_kw):
        os88qemu.mouse_to(self.q, self.q.sym, self._h, int(x), int(y))

    def _edge(self, down, btn=1, **_kw):
        os88qemu.mouse_button(self.q, self.q.sym, self._h, btn if down else 0)
        self._last = os88qemu.ticks(self.q)

    def _sep(self):
        if self._last is None:
            return
        c = os88qemu.Clock(self.q)
        while ((os88qemu.ticks(self.q) - self._last) & 0xFFFF) <= DBL_TICKS:
            if c.stalled():
                raise os88qemu.GuestStopped("the guest's clock stopped")
            time.sleep(0.01)

    def click(self, x, y, settle=None):
        self.to(x, y)
        if self.where()[2] & 1:
            self._edge(False)
        self._sep()
        self._edge(True)
        self._edge(False)

    def dblclick(self, x, y, settle=None):
        self._sep()
        os88qemu.mouse_dblclick(self.q, self.q.sym, self._h, int(x), int(y))
        self._last = os88qemu.ticks(self.q)

    def drag(self, x0, y0, x1, y1, settle=None):
        self.to(x0, y0)
        if self.where()[2] & 1:
            self._edge(False)
        self._sep()
        self._edge(True)
        self.to(x1, y1, l=True)
        self._edge(False)


class QUI(os88ui.UI):
    """os88ui on QEMU: `still` is os88qemu.ui_done (ui_idle across two guest
    ticks) - os88marty's steps a PAUSED Marty by cycles, which QEMU has not."""

    def still(self, frames=0.05):
        os88qemu.ui_done(self.m, self.sym, cap=2.0)
        if frames:
            M.guest_sleep(self.m, frames)
        return self


# =============================================================================
# one adapter
# =============================================================================
class QLeg(X.Leg):
    """pagedeskxt's Leg - its readers, its waits and its table - on QEMU,
    with the longer session above and no stack fill (that is the XT row's)."""

    def __init__(self, name, P):
        X.Leg.__init__(self, name, "qemu", P)
        self.checks = []
        self.save_as = SAVE_AS

    def check(self, ok, what):
        self.checks.append(("ok  " if ok else "FAIL") + " " + what)
        if not ok:
            self.fails.append(what)
        return ok

    def track(self, what, done, c0, guest=120.0):
        # every step here is seconds on a 386; the XT's half-hour budgets
        # would turn a lost input into a half-hour wait
        return X.Leg.track(self, what, done, c0, guest=min(guest, 300.0))

    def settle_pd(self, what, guest=300.0):
        return X.Leg.settle_pd(self, what, guest=min(guest, 300.0))

    def picture(self, path):
        # QEMU's screendump, whatever the adapter (PAGEDESK_SHOTS, stepshot)
        self.m.shot(path)

    def run(self):
        need(SYSIMG)
        need(APPIMG)
        import os88sym
        if self.name == "cga":
            tree = os88build.tree("VIDEO=cga", targets=("os8088.img",)).apply()
            sysimg = tree.img("os8088.img")
        else:
            sysimg = os88build.at(SYSIMG)
        bimg = os88qemu.scratch(APPIMG, "pd%s.img" % self.name)
        S = os88sym.linear
        inst = os88qemu.make_test("TESTIMG=%s" % sysimg, "TESTAPPS=%s" % bimg)
        self.bimg = inst.b
        q = QM(inst, S)
        try:
            os88qemu.kernel_up(q, S)
            self.m = q
            ui = QUI(q, mouse=QMouse(q), sym=S, verbose=False)
            self.ui = ui
            try:
                self.go(ui)
            except Exception:
                p = os.path.join(tempfile.gettempdir(),
                                 "pagedesk-%s-fail.png" % self.name)
                try:
                    q.shot(p)
                    self.say("the screen when it stopped: %s" % p)
                except Exception:                    # noqa: BLE001
                    pass
                raise
        finally:
            try:
                q.hmp("quit")
            except (Exception, SystemExit):          # noqa: BLE001
                pass
            inst.close()
        self.readback()

    def go(self, ui):
        m = self.m
        # the screen saver: Never (os88marty.no_saver's write), and the
        # empty desk clicked first, so the first input of the session is
        # one the desktop has had and not a wake from a blanked screen
        vw, vh = struct.unpack("<HH", m.read(m.sym("vid_w"), 2)
                               + m.read(m.sym("vid_h"), 2))
        ui.mo.click(vw // 2, vh // 2)           # bare desk at boot
        try:
            M.no_saver(m)
        except Exception as e:                       # noqa: BLE001
            self.say("no_saver: %s (the desk click stands)" % e)
        free, big, shed, _ = self.heap()
        self.rows.append(("desktop, before PageDesk", None, free, big, None,
                          shed))
        ui.open_drive("B")

        # --- 1. the guide, by association --------------------------------
        c0 = self.cyc()
        w = ui.open("XTGUIDE.PDF")
        self.w = w
        self.p = M.Pkg(m, w)
        s, _ = self.track("XTGUIDE: double-click to document open",
                          lambda s: s["doc"] == X.D_OPEN and s["np"] == 12, c0)
        s, _ = self.track("XTGUIDE: double-click to first page drawn",
                          lambda s: s["np"] == 12 and self.idle(s), c0)
        self.layout = s["kind"]
        s = self.settle_pd("XTGUIDE: page drawn to quiet")
        self.deep_water("XTGUIDE.PDF")

        # --- 2. paged through, zoomed ------------------------------------
        for k in range(2):
            pg = s["page"]
            c0 = self.cyc()
            m.key("PageDown")
            s, _ = self.track("PgDn %d to page drawn" % (k + 1),
                              lambda s: s["page"] == pg + 1 and self.idle(s),
                              c0)
            s = self.settle_pd("PgDn %d: drawn to quiet" % (k + 1))
        self.check(s["page"] == 2, "PgDn twice: page index %d, want 2"
                   % s["page"])
        was = (s["zoom"], s["fit"])
        c0 = self.cyc()
        m.type_text("+")
        s, _ = self.track("zoom in (+) to page drawn",
                          lambda s: (s["zoom"], s["fit"]) != was
                          and self.idle(s), c0)
        s = self.settle_pd("zoom: drawn to quiet")
        self.check((s["zoom"], s["fit"]) != was, "+ zoomed: %r -> %r"
                   % (was, (s["zoom"], s["fit"])))

        # --- 3. Find ------------------------------------------------------
        ui.menu_pick("Edit", "Find...")
        self.track("Find's popover", lambda s: s["pop"] == X.P_FIND,
                   self.cyc(), guest=30.0)
        m.type_text(X.FIND)
        c0 = self.cyc()
        m.key("Enter")
        s, _ = self.track("Find %r across XTGUIDE to its answer" % X.FIND,
                          lambda s: s["job"] != X.J_FIND and (
                              s["msg"].startswith("Found on page")
                              or s["msg"].startswith("No match")), c0)
        self.check(s["msg"].startswith("Found on page 12"),
                   "Find %r answered %r, want `Found on page 12`"
                   % (X.FIND, s["msg"]))
        c0 = self.cyc()
        m.key("Escape")
        s, _ = self.track("Find: Esc to page 12 drawn",
                          lambda s: s["pop"] == X.P_NONE and s["page"] == 11
                          and self.idle(s), c0)
        s = self.settle_pd("Find: drawn to quiet")

        # --- 4. a Mark and a Note ----------------------------------------
        m.key("F6")
        s, _ = self.track("Annotate mode (F6)",
                          lambda s: s["mode"] == X.MODE_ANNOT and self.quiet(s),
                          self.cyc(), guest=30.0)
        m.type_text("5")                # FULL/MID: Mark; COMPACT: More
        if s["kind"] == X.L_COMPACT:
            s, _ = self.track("More's popover", lambda s: s["pop"] == X.P_MORE,
                              self.cyc(), guest=30.0)
            x1, y1, x2, y2 = s["poprect"]
            ui.mo.click(x1 + 16, y1 + 4 + 2 + 4)     # row 0: Mark (pducmd.c)
        s, _ = self.track("the Mark tool", lambda s: s["tool"] == X.C_T_MARK
                          and s["pop"] == X.P_NONE, self.cyc(), guest=30.0)
        s = self.settle_pd("the Mark tool to quiet")
        cx1, cy1, cx2, cy2 = s["canvas"]
        n0 = s["edn"]
        c0 = self.cyc()
        ui.mo.drag(cx1 + (cx2 - cx1) // 4, cy1 + (cy2 - cy1) // 4,
                   cx1 + (cx2 - cx1) // 2, cy1 + (cy2 - cy1) // 2)
        s, _ = self.track("area mark: drag to the mark drawn",
                          lambda s: s["edn"] == n0 + 1 and self.idle(s), c0)
        s = self.settle_pd("area mark: drawn to quiet")

        m.type_text("3")                # the Note tool, in every layout
        s, _ = self.track("the Note tool", lambda s: s["tool"] == C_T_NOTE,
                          self.cyc(), guest=30.0)
        s = self.settle_pd("the Note tool to quiet")
        n0 = s["edn"]
        ui.mo.click(cx1 + 3 * (cx2 - cx1) // 4, cy1 + 3 * (cy2 - cy1) // 4)
        s, _ = self.track("the note's popover", lambda s: s["pop"] == P_NOTE,
                          self.cyc(), guest=30.0)
        m.type_text(NOTE_TEXT)
        c0 = self.cyc()
        m.key("Enter")                  # Done: kept (Esc would discard)
        s, _ = self.track("note: Enter to the note kept and drawn",
                          lambda s: s["edn"] == n0 + 1 and s["pop"] == X.P_NONE
                          and self.idle(s), c0)
        s = self.settle_pd("note: drawn to quiet")
        self.check(s["edn"] == n0 + 1, "the note was kept (pdu_ed_n %d -> %d)"
                   % (n0, s["edn"]))

        # --- 5. Insert from PDF ------------------------------------------
        np0 = s["np"]
        ui.menu_pick("File", "Insert from PDF...")
        ui.chooser()
        ui.chooser_open(INSERT_FROM)
        s, _ = self.track("Insert: the page prompt",
                          lambda s: s["msg"].startswith("Insert "),
                          self.cyc(), guest=60.0)
        m.type_text("1")
        c0 = self.cyc()
        m.key("Enter")
        s, _ = self.track("Insert %s page 1 to the page drawn" % INSERT_FROM,
                          lambda s: s["np"] == np0 + 1 and self.idle(s), c0)
        s = self.settle_pd("Insert: drawn to quiet")
        self.check(s["np"] == 13, "Insert from PDF: %d pages, want 13"
                   % s["np"])

        # --- 6. Preferences ----------------------------------------------
        ui.menu_pick("View", "Preferences...")
        self.track("the Preferences dialog", lambda s: s["pop"] == P_PREFS,
                   self.cyc(), guest=30.0)
        m.key("ArrowUp")                # row 0 -> 4, Flate cache (wraps)
        m.key("ArrowRight")             # Auto -> Off
        c0 = self.cyc()
        m.key("Escape")                 # close: the wake writes the file
        s, _ = self.track("Preferences: Esc to `Preferences saved.`",
                          lambda s: s["pop"] == X.P_NONE and (
                              s["msg"].startswith("Preferences saved")
                              or s["msg"].startswith("Not saved")), c0)
        self.check(s["msg"].startswith("Preferences saved"),
                   "Preferences answered %r" % s["msg"])
        s = self.settle_pd("Preferences: to quiet")

        # --- 7. Save As ---------------------------------------------------
        ui.menu_pick("File", "Save As...")
        ui.chooser()
        c0 = self.cyc()
        ui.chooser_save(SAVE_AS)
        s, _ = self.track("Save As %s to `Saved`" % SAVE_AS,
                          lambda s: s["msg"].startswith("Saved")
                          or s["msg"].startswith("Could not"), c0)
        self.check(s["msg"].startswith("Saved"), "Save As answered %r"
                   % s["msg"])
        s, _ = self.track("Save As: to the re-opened page drawn",
                          self.idle, c0)
        s = self.settle_pd("Save As: drawn to quiet")
        self.check(s["np"] == 13, "the re-opened %s has %d pages, want 13"
                   % (SAVE_AS, s["np"]))
        self.deep_water(SAVE_AS)

        # --- 8. the mark, as the re-opened file draws it -----------------
        # The saved /Square is right - qpdf and mutool draw its four edges
        # (readback) - so this is about PageDesk drawing its OWN file: back
        # to page 12, and the light red (/C 1 .333 .333) on the canvas must
        # be a rectangle, every edge of its bounding box lit along most of
        # its length. VGA only: the 1bpp faces draw a mark in black, among
        # the page's own black.
        if self.name == "vga":
            c0 = self.cyc()
            m.key("PageUp")
            s, _ = self.track("PgUp to the re-opened page 12",
                              lambda s: s["page"] == 11 and self.idle(s), c0)
            s = self.settle_pd("page 12: drawn to quiet")
            self.mark_edges(s["canvas"])
        free, big, shed, mp = self.heap()
        self.claims = list(mp.claims)
        self.rows.append(("after the session", None, free, big, None, shed))
        p = os.path.join(tempfile.gettempdir(), "pagedesk-%s.png" % self.name)
        try:
            m.shot(p)
            self.shot = p
        except Exception:                            # noqa: BLE001
            self.shot = None

    def mark_edges(self, canvas):
        w, h, rgb = self.m.frame()
        x1, y1, x2, y2 = canvas
        red = set()
        for y in range(max(0, y1), min(h, y2)):
            o = y * w * 3
            for x in range(max(0, x1), min(w, x2)):
                r, g, b = rgb[o + 3 * x:o + 3 * x + 3]
                if r >= 0xC0 and g < 0x80 and b < 0x80:
                    red.add((x, y))
        if not red:
            self.check(False, "the re-opened page 12 draws no mark at all")
            return
        bx0, bx1 = min(p[0] for p in red), max(p[0] for p in red)
        by0, by1 = min(p[1] for p in red), max(p[1] for p in red)
        bw, bh = bx1 - bx0 + 1, by1 - by0 + 1
        cov = {
            "top": sum((x, by0) in red for x in range(bx0, bx1 + 1)) / bw,
            "bottom": sum((x, by1) in red for x in range(bx0, bx1 + 1)) / bw,
            "left": sum((bx0, y) in red for y in range(by0, by1 + 1)) / bh,
            "right": sum((bx1, y) in red for y in range(by0, by1 + 1)) / bh}
        self.check(bw > 8 and bh > 8 and min(cov.values()) >= 0.6,
                   "the re-opened mark draws all four edges: box %dx%d at "
                   "(%d,%d), lit %s" % (bw, bh, bx0, by0, ", ".join(
                       "%s %d%%" % (k, 100 * v) for k, v in cov.items())))

    def readback(self):
        """The disk, after QEMU has exited, by os88flush's FAT reader."""
        vol = os88flush.Volume(open(self.bimg, "rb").read())
        try:
            cfg = vol.read(CFG)
        except Exception as e:                       # noqa: BLE001
            cfg = None
            self.check(False, "%s is not on B: (%s)" % (CFG, e))
        if cfg is not None:
            self.check(len(cfg) == 10 and cfg[:4] == b"PDCF" and cfg[9] == 1,
                       "%s: %d bytes %s, Flate byte %s (want 10, PDCF, 1 = Off)"
                       % (CFG, len(cfg), cfg[:5], cfg[9] if len(cfg) > 9
                          else None))
        try:
            data = vol.read(SAVE_AS)
        except Exception as e:                       # noqa: BLE001
            self.check(False, "%s is not on B: after the save (%s)"
                       % (SAVE_AS, e))
            return
        fd, path = tempfile.mkstemp(suffix=".pdf")
        os.write(fd, data)
        os.close(fd)
        try:
            chk, an = X.qpdf_check(path), X.annots(path)
        finally:
            os.unlink(path)
        self.saved = (len(data), chk, an)
        if chk is None:
            self.checks.append("SKIP qpdf is not on PATH: %s is not checked "
                               "and its annotations are not counted" % SAVE_AS)
            self.say("SKIP: no qpdf")
            return
        self.check(chk[0] == 0, "qpdf --check %s: exit %d %s"
                   % (SAVE_AS, chk[0], " ".join(chk[1])))
        sub = an.get("subtypes", {})
        where = an.get("where", {})
        self.check(an.get("pages") == 13, "%s has %s pages, want 13"
                   % (SAVE_AS, an.get("pages")))
        self.check(sub.get("/Square") == 1 and where.get("/Square") == [11],
                   "one /Square (the mark) on page 12: %r at %r"
                   % (sub.get("/Square"), where.get("/Square")))
        self.check(sub.get("/Text") == 1 and where.get("/Text") == [11],
                   "one /Text (the note) on page 12: %r at %r"
                   % (sub.get("/Text"), where.get("/Text")))
        self.check(sub.get("/Link") == XT_LINKS,
                   "XTGUIDE's %d /Link kept: %r" % (XT_LINKS, sub.get("/Link")))
        extra = set(sub) - {"/Square", "/Text", "/Link"}
        self.check(not extra, "no other annotation subtypes: %r"
                   % sorted(extra))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", action="append", choices=("vga", "cga"))
    a = ap.parse_args()
    X._hdr_consts()
    P = X.pkg_syms()
    bad = 0
    for name in (a.only or ("vga", "cga")):     # vga FIRST: cga's tree.apply()
        leg = QLeg(name, P)                     # repoints the symbol reader
        err = None
        try:
            leg.run()
        except BaseException as e:              # noqa: BLE001 - a need()'s
            import traceback                    # SystemExit is a fail too
            err = "%s\n%s" % (e, traceback.format_exc()[-1500:])
        leg.machine = "QEMU %s" % name.upper()
        X.report(leg)
        for c in leg.checks:            # the FAILs are report()'s
            if not c.startswith("FAIL"):
                print("   " + c)
        if err:
            print("   FAIL %s did not finish: %s" % (name, err))
            bad += 1
        bad += len(leg.fails)
        print()
    print("pagedesk: %s" % ("ok" if not bad else "%d failure(s)" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
