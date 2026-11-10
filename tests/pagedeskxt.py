#!/usr/bin/env python3
"""PD-29's proof on the target: PageDesk on a 640 KiB 8088 at 4.77 MHz.

    make pagedesk pagedeskdisk && python3 tests/pagedeskxt.py [--only vga|cga|herc] [--serial]

PAGEDESK_PROMPT.md's PD-29 asks the owner to be able to tell verified XT
behaviour from a fast-emulator demonstration. QEMU cannot say it: its CPU is
a 386 and its clock is the host's. MartyPC is cycle-exact against the field
5150, so this row is the instrument - one machine per adapter MartyPC has,
each with EXACTLY 640 KiB (conventional.size = 0xA0000), no EMS, no XMS, no
FPU, an 80-cylinder 720 KB B: and the shipped kern_big system disk with no
SYSTEM.CFG (so no driver is wanted - the minimal ordinary set):

    vga    os8088_xt_vga_720b        an IBM 5160 (8088, 4.77 MHz) with a VGA;
                                     A: 360 KB, B: 720 KB
    cga    os8088_5150_cga_720b_gla  a 5150 with a CGA; A: 360 KB, B: 720 KB
    herc   os8088_5150_herc_720_gla  a 5150 with a REAL Hercules card at
                                     B000h; both drives 720 KB, so A: is the
                                     720 KB system disk - the tree has no
                                     Hercules 5150 with a 360 KB A: and an
                                     80-cylinder B:. QEMU's HERCSEG harness
                                     cannot host PageDesk, whose heap overruns
                                     the fake framebuffer
                                     (docs/HERCULES-TESTING.md)

B: is `make pagedeskdisk`'s 720 KB disk, `pagedesk720.img`: the program and
every sample on one floppy (DECISIONS 07:22). The 360 KB set is a PAIR whose
first disk holds the program and its font licence and nothing else
(PAGEDESK-SPEC 20.22.2.8), so neither document this row opens is on it, and
a 720 KB image in a 40-cylinder drive reads only its first half. Each machine
is its 360 KB profile with the B: drive changed and 640 KiB as before. On each
machine the row:

  1. fills task 0's stack below its sleeping SP with 0xCC (tests/stk0water.py's
     method, which owns the explanation of every step of it);
  2. opens HELLO.PDF by double-click (SPEC.md 54's association launches
     PageDesk on it), then XTGUIDE.PDF through File > Open's chooser;
  3. turns a page (PgDn), zooms (+), Finds `Index` - which is on page 12
     only, so the search reads every page after the second - adds an area
     mark (Annotate, the Mark tool, a drag on the canvas) and Saves As
     XTMARK.PDF;
  4. reads XTMARK.PDF back OFF THE FLOPPY on the host (tools/os88flush.py,
     no os8088 code in the reader), and qpdf --check's it and counts its
     annotations by /Subtype when qpdf is on PATH.

WHAT IT MEASURES, and prints per adapter:

  * the claim heap, SAMPLED through every operation (heapmap.Map off mem_tab):
    unclaimed and the largest free run after it, and the lowest unclaimed
    total seen during it with the room that has once the purgeable caches
    are shed;
  * guest CYCLES from the input to the state that ends it, converted at
    4.772727 MHz: launch to window, to the document open, to the first page
    drawn; the second open and its first page; a page turn; a zoom; Find
    across XTGUIDE; a Save As (to `Saved ...`, the re-open and the redraw).
    The end of each is polled off PageDesk's own statics (pdu_doc, pdu_job,
    pdu_rwant, pdu_page, pdu_msg, read through the window's W_SEG with the
    package's map re-assembled here and checked byte-identical against
    build/pagedesk.bin), so a figure is good to the poll's resolution - about
    0.1 guest second, which the row prints;
  * task 0's stack high-water (the UI stack, 512 bytes), and the deepest use
    of PageDesk's private module stack (PAGEDESK-SPEC 20.15, PD_DSTK_*: a
    0xCC fill and a canary at each carve) for each document.

WHAT IT ASSERTS, and only this (PD-29: timings are a measurement, not a gate):

  * >= 32 KiB of the heap unclaimed AFTER every step (PAGEDESK_PROMPT 3's
    reserve, "after normal baseline operations"), and >= 32 KiB reachable at
    every step's trough once the purgeable caches are shed - Save As loads
    PDWRT beside the document, so its trough is the session's lowest, and
    what a claim made then would get is the free room plus what the kernel
    purges for it (SPEC.md 50.6). The trough's unclaimed figure is printed
    and NOT asserted;
  * task 0's high-water <= 384 bytes (DECISIONS D22 item 1: 128 left for the
    interrupt floor) and its canary intact;
  * the deep stack's use <= 1792 of 2048 (D23: 256 for the ISR floor, int 09h's
    ROM and sch_isr's frame) and its canary intact;
  * that every step finished - a wait that times out raises, naming what the
    guest showed - and that the saved file is on the disk.

Break it: drop `pdu_dckb`'s DSTK row to 1 KiB and the deep-stack assertion
goes red on XTGUIDE; claim 600 KiB in pdumod.c and the heap one does.

PAGEDESK_SHOTS=<dir> keeps a picture after every timed step, decoded from
VRAM on CGA and Hercules - a pull request's screenshots of the head
(CLAUDE.md, "Pull requests show the change"). tests/pagedesk.py takes the
same variable for its QEMU legs.
"""
import argparse
import collections
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "tools"), os.path.join(ROOT, "tests")]
os.chdir(ROOT)

import heapmap                                              # noqa: E402
import os88build                                            # noqa: E402
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
from os88fixture import need                                # noqa: E402

HZ = M.GUEST_HZ
APPIMG = "build/pagedesk720.img"     # every sample; DECISIONS 07:22
MACHINES = collections.OrderedDict((   # name: (machine, its A: system disk)
    ("vga", ("os8088_xt_vga_720b", "build/os8088-360.img")),
    ("cga", ("os8088_5150_cga_720b_gla", "build/os8088-360.img")),
    ("herc", ("os8088_5150_herc_720_gla", "build/os8088-720.img")),
))
RESERVE = 32 * 1024                 # PAGEDESK_PROMPT 3: >= 32 KiB unclaimed
UI_STACK_MAX = 384                  # D22 item 1
DEEP_MAX = 2048 - 256               # D23 item 5
SAVE_AS = "XTMARK.PDF"
FIND = "Index"                      # XTGUIDE.PDF: on page 12 and nowhere else

# PageDesk's constants (apps/pagedesk/pdu.h, pdabi.h) - pinned there, and
# re-read from the headers below rather than typed twice where they matter
D_OPEN = 2
J_NONE, J_OPEN, J_RENDER, J_SAVE = 0, 1, 2, 3
J_FIND, J_TEXT, J_THUMB = 4, 5, 6
P_NONE, P_MORE, P_FIND = 0, 2, 3
MODE_ANNOT = 1
L_COMPACT = 2
C_T_MARK = 21
POP_ROW = 12
LAY_CANVAS = 42                     # struct pdu_lay: kind, ox..h, 4 rects of 8
PD_DSTK_BYTES, PD_DSTK_UISP = 2048, 2040
PD_DSTK_CAN0, PD_DSTK_CAN1, PD_DSTK_FILL = 0x5350, 0x4B44, 0xCC


def _hdr_consts():
    """The few PageDesk constants this row depends on, read out of the
    headers so a renumbering fails HERE with a sentence, not as a wait that
    never ends."""
    want = {"D_OPEN": D_OPEN, "J_NONE": J_NONE, "J_OPEN": J_OPEN,
            "J_RENDER": J_RENDER, "J_SAVE": J_SAVE, "J_FIND": J_FIND,
            "J_TEXT": J_TEXT, "J_THUMB": J_THUMB,
            "P_MORE": P_MORE, "P_FIND": P_FIND, "PDU_MODE_ANNOT": MODE_ANNOT,
            "PDU_L_COMPACT": L_COMPACT, "C_T_MARK": C_T_MARK,
            "PD_DSTK_BYTES": PD_DSTK_BYTES, "PD_DSTK_UISP": PD_DSTK_UISP,
            "PD_DSTK_CAN0": PD_DSTK_CAN0, "PD_DSTK_CAN1": PD_DSTK_CAN1,
            "PD_DSTK_FILL": PD_DSTK_FILL}
    import re
    got = {}
    for f in ("apps/pagedesk/pdu.h", "apps/pagedesk/pdabi.h"):
        for mm in re.finditer(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)",
                              open(os.path.join(ROOT, f)).read()):
            got.setdefault(mm.group(1), int(mm.group(2), 0))
    bad = ["%s is %s in the headers, %s here" % (k, got.get(k), v)
           for k, v in want.items() if got.get(k) != v]
    if bad:
        sys.exit("pagedeskxt: the row's PageDesk constants moved: "
                 + "; ".join(bad))


# =============================================================================
# PageDesk's own map: its statics are labels in build/pagedesk.gen.asm
# =============================================================================
def pkg_syms():
    """PAGEDESK's primary symbols, by re-assembling its shim with a map.

    A C package's statics are labels in a GENERATED file, so the only answer
    that cannot go stale is nasm's own (tests/cmemmove.py's argument). The
    result is checked byte for byte against build/pagedesk.bin: a map of a
    different build would read every word in the wrong place and decode as a
    plausible document state."""
    bdir = os88build.at("build")
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        src = os.path.join(ROOT, "apps", "pagedesk", "pagedesk.asm")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        out = os.path.join(d, "p.bin")
        subprocess.run(["nasm", "-f", "bin", "-w+error",
                        "-I", os.path.join(ROOT, "apps") + "/",
                        "-I", os.path.join(ROOT, bdir) + "/", "-o", out, cp],
                       check=True, cwd=ROOT)
        if open(out, "rb").read() != open(os.path.join(
                ROOT, bdir, "pagedesk.bin"), "rb").read():
            sys.exit("pagedeskxt: re-assembling apps/pagedesk/pagedesk.asm "
                     "does not give build/pagedesk.bin - run `make pagedesk "
                     "pagedeskdisk` so the map describes the package on the disk")
        syms = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                syms[f[2]] = int(f[0], 16)
    return syms


def annots(path):
    """{/Subtype: count} over every page's /Annots, and the per-page counts,
    by qpdf's JSON - a reader that is not PageDesk's. None without qpdf."""
    if not shutil.which("qpdf"):
        return None
    r = subprocess.run(["qpdf", "--json=2", "--json-key=qpdf",
                        "--json-key=pages", path], capture_output=True)
    if r.returncode not in (0, 3):
        return {"error": r.stderr.decode("latin-1")[-200:]}
    j = json.loads(r.stdout)
    objs = j["qpdf"][1]

    def val(ref):
        o = objs.get("obj:" + ref, {})
        return o.get("value", o.get("stream", {}).get("dict"))
    count, per, where = collections.Counter(), [], {}
    for k, pg in enumerate(j["pages"]):
        a = (val(pg["object"]) or {}).get("/Annots", [])
        if isinstance(a, str):
            a = val(a) or []
        n = 0
        for ref in a:
            ad = val(ref) if isinstance(ref, str) else ref
            if ad:
                st = ad.get("/Subtype", "?")
                count[st] += 1
                where.setdefault(st, []).append(k)
                n += 1
        per.append(n)
    return {"subtypes": dict(count), "per_page": per, "where": where,
            "pages": len(j["pages"])}


def qpdf_check(path):
    """qpdf --check's verdict, or None (a SKIP, never a pass) without qpdf."""
    if not shutil.which("qpdf"):
        return None
    r = subprocess.run(["qpdf", "--check", path], capture_output=True)
    return r.returncode, (r.stdout + r.stderr).decode("latin-1").strip() \
        .splitlines()[-2:]


# =============================================================================
# one machine
# =============================================================================
class Leg(object):
    """PageDesk on one adapter; every number lands in self.rows."""

    def __init__(self, name, machine, P):
        (self.machine, self.sysimg), self.name, self.P = machine, name, P
        self.rows = []              # (what, guest seconds, heap min, note)
        self.fails = []
        self.notes = []
        self.deep = {}              # document -> deepest private-stack use
        self.heap_lo = None
        self.res = 0.0              # the worst poll resolution, guest s

    # --- reading the guest --------------------------------------------------
    def say(self, s):
        self.notes.append(s)

    def cyc(self):
        return int(self.m.status()["cycles"])

    def heap(self):
        """(unclaimed, largest run, unclaimed with the caches shed), bytes."""
        S = self.m.sym
        mp = heapmap.Map(self.m, {n: S(n) for n in
                                  ("mem_base", "mem_top", "spl_live",
                                   "mem_tab")})
        runs = mp.runs()
        shed = mp.runs(drop_purgeable=True)
        return (16 * sum(r[1] for r in runs),
                16 * max([r[1] for r in runs] or [0]),
                16 * sum(r[1] for r in shed), mp)

    def st(self):
        """PageDesk's state, ONE snapshot of its statics through W_SEG."""
        P = self.P
        names = ("_pdu_doc", "_pdu_job", "_pdu_page", "_pdu_npages",
                 "_pdu_rwant", "_pdu_zoom", "_pdu_fit", "_pdu_pop",
                 "_pdu_mode", "_pdu_tool", "_pdu_dstkseg")
        lo = min(P[n] for n in names)
        hi = max(P[n] for n in names) + 2
        need_msg = (P["_pdu_msg"], 48)
        spans = [(lo, hi - lo), need_msg, (P["_pdu_ed_n"], 2),
                 (P["_pdu_pgwant"], 2), (P["_pdu_L"], 64),
                 (P["_pdu_poprect"], 8)]
        raw = self.p.reads(spans)
        blk = raw[0]

        def w(n):
            return struct.unpack_from("<H", blk, P[n] - lo)[0]
        s = dict(doc=w("_pdu_doc"), job=w("_pdu_job"), page=w("_pdu_page"),
                 np=w("_pdu_npages"), rw=w("_pdu_rwant"), zoom=w("_pdu_zoom"),
                 fit=w("_pdu_fit"), pop=w("_pdu_pop"), mode=w("_pdu_mode"),
                 tool=w("_pdu_tool"), dstk=w("_pdu_dstkseg"))
        s["msg"] = raw[1].split(b"\0")[0].decode("latin-1")
        s["edn"] = struct.unpack("<H", raw[2])[0]
        s["pgw"] = struct.unpack("<H", raw[3])[0]
        lay = raw[4]
        s["kind"] = struct.unpack_from("<H", lay, 0)[0]
        s["canvas"] = struct.unpack_from("<4h", lay, LAY_CANVAS)
        s["poprect"] = struct.unpack("<4h", raw[5])
        return s

    def idle(self, s):
        """The PAGE IS DRAWN: a document open, no open/render/save/find/text
        job, no render or page load owed - and NO WAKE IN PROGRESS. The last
        half is what makes it true: inside one wake the open's last slice
        sets J_NONE and the render it owes is begun a few hundred instructions
        later, so the statics alone read "idle" for a moment in the middle of
        a pass. The statics are read FIRST and the kernel after them (st()
        then this): a pass that begins a job kicks the wake, so ui_task
        cannot be asleep after one. A thumbnail (J_THUMB) is nav's idle
        ladder, begun only when nothing else is owed, so it is not the page."""
        if not (s["doc"] == D_OPEN and s["job"] not in
                (J_OPEN, J_RENDER, J_SAVE, J_FIND, J_TEXT)
                and not s["rw"] and not s["pgw"]):
            return False
        return s["job"] == J_THUMB or M.ui_idle(self.m, lock=False)

    def quiet(self, s):
        """...and NOTHING is running, a thumbnail included: the state an input
        has to find. A PDREND thumbnail is ONE slice on the target - measured
        at ~107 guest seconds for HELLO's - and the UI task takes no input
        until it returns, so a menu pressed during one waits that long."""
        return (s["doc"] == D_OPEN and s["job"] == J_NONE and not s["rw"]
                and not s["pgw"] and M.ui_idle(self.m, lock=False))

    def settle_pd(self, what, guest=1800.0):
        """Wait for quiet() before the next input, and RECORD the wait when
        there was one - it is time the user could not use the program."""
        s = self.st()
        if self.quiet(s):
            return s
        s, _ = self.track(what, self.quiet, self.cyc(), guest=guest)
        return s

    def track(self, what, done, c0, guest=900.0):
        """Poll PageDesk until done(state) holds; sample the heap all the way.
        Answers (state, guest seconds since c0)."""
        box = {}
        last = [None]

        def f(_):
            c = self.cyc()
            s = self.st()
            free, big, shed, _mp = self.heap()
            if box.get("lo") is None or free < box["lo"][0]:
                box["lo"] = (free, big, shed)
            box["at"] = (free, big, shed)
            if last[0] is not None:
                self.res = max(self.res, (c - last[0]) / HZ)
            last[0] = c
            if done(s):
                box["c"], box["s"] = c, s
                return True
            box["s"] = s
            return False
        try:
            M.until(self.m, f, "%s (%s)" % (what, self.name), poll=0.02,
                    guest=guest)
        except Exception as e:
            raise RuntimeError("%s: %s; PageDesk showed %r"
                               % (what, e, box.get("s")))
        lo, lo_big, lo_shed = box["lo"]
        free, big, _shed = box["at"]
        if self.heap_lo is None or lo < self.heap_lo[0]:
            self.heap_lo = (lo, lo_big, lo_shed, what)
        # the reserve is held AFTER the step (PAGEDESK_PROMPT 3: "after
        # normal baseline operations") - and DURING it, the trough must still
        # reach it once the purgeable caches are shed, which the kernel does
        # for any claim that needs the room (SPEC.md 50.6)
        if free < RESERVE:
            self.fails.append("%s: %d bytes unclaimed after it, under the %d "
                              "reserve" % (what, free, RESERVE))
        if lo_shed < RESERVE:
            self.fails.append("%s: %d bytes reachable at its trough even with "
                              "the caches shed, under the %d reserve"
                              % (what, lo_shed, RESERVE))
        secs = (box["c"] - c0) / HZ
        self.rows.append((what, secs, free, big, lo, lo_shed))
        self.stepshot(what)
        return box["s"], secs

    def stepshot(self, what):
        """PAGEDESK_SHOTS=<dir>: the screen after every timed step, as the
        guest drew it - a pull request's screenshots of the head (CLAUDE.md,
        "Pull requests show the change"). Off unless asked: a picture is
        host time, never guest time, so no step's figure moves."""
        d = os.environ.get("PAGEDESK_SHOTS")
        if not d:
            return
        os.makedirs(d, exist_ok=True)
        self.nshot = getattr(self, "nshot", 0) + 1
        slug = "".join(c if c.isalnum() else "-" for c in what.lower())
        slug = "-".join(t for t in slug.split("-") if t)[:48]
        tool = os.path.splitext(os.path.basename(sys.argv[0]))[0]
        try:
            self.picture(os.path.join(d, "%s-%s-%02d-%s.png"
                                      % (tool, self.name, self.nshot, slug)))
        except Exception as e:                      # noqa: BLE001
            print("   (no step picture for %r: %s)" % (what, e))

    def deep_water(self, doc):
        """The private module stack's deepest use for this document - read
        PAUSED, through a W_SEG and a carve that the compactor may move."""
        P = self.P

        def rd(mm):
            seg = struct.unpack("<H", self.p.read(P["_pdu_dstkseg"], 2))[0]
            if not seg:
                return None
            return mm.read(seg << 4, PD_DSTK_BYTES)
        blob = M.heap_read(self.m, rd, "the private module stack")
        if blob is None:
            return
        can = struct.unpack_from("<HH", blob, 0)
        if can != (PD_DSTK_CAN0, PD_DSTK_CAN1):
            self.fails.append("%s: the private stack's canary is %04X %04X"
                              % (doc, can[0], can[1]))
        low = next((i for i in range(4, PD_DSTK_UISP)
                    if blob[i] != PD_DSTK_FILL), PD_DSTK_UISP)
        used = PD_DSTK_UISP - low
        self.deep[doc] = max(self.deep.get(doc, 0), used)

    # --- the stk0water fill -------------------------------------------------
    def stack_fill(self):
        """tests/stk0water.py's method, step for step: task 0 asleep with a
        shallow frame, the CPU elsewhere, then 0xCC from above the canary to
        64 bytes under the saved SP."""
        m, S = self.m, self.m.sym
        eq = self.eq
        low = eq["LOW_SEG"] * 16
        self.s_bot = low + eq["KLOW_SIZE"]
        self.s_top = low + eq["STK0_TOP"] + 2
        T_STATE, T_SP = 0, 2
        for _ in range(40):
            cur = m.read(S("sch_cur"), 1)[0]
            st = m.read(S("sch_tasks") + T_STATE, 1)[0]
            sp = struct.unpack("<H", m.read(S("sch_tasks") + T_SP, 2))[0]
            if cur != 0 and st == 2 and eq["STK0_TOP"] - 64 < sp <= eq["STK0_TOP"]:
                break
            M.ui_done(m)
        else:
            raise RuntimeError("task 0 never settled asleep with a shallow "
                               "frame (sch_cur=%d, T_STATE=%d, SP=%04X)"
                               % (cur, st, sp))
        self.s_lo, self.s_hi = self.s_bot + 2, self.s_top - 64
        m.write(self.s_lo, bytes([0xCC]) * (self.s_hi - self.s_lo))

    def stack_read(self):
        blob = self.m.read(self.s_lo, self.s_hi - self.s_lo)
        deep = next((i for i, b in enumerate(blob) if b != 0xCC), None)
        used = 64 if deep is None else len(blob) - deep + 64
        can = self.m.read(self.s_bot, 2)
        return used, can

    def picture(self, path):
        """The screen as the GUEST drew it: a 1bpp card's VRAM decoded (the
        rendered Hercules frame sits 16 pixels left and 2 down of the guest's
        coordinates - docs/HERCULES-TESTING.md), VGA's rendered frame."""
        if self.name in ("cga", "herc"):
            w, h, rows = self.m.vram(self.name)
            M.write_png(path, w, h, rows)
        else:
            M.write_png_rgb(path, *self.m.fbuf())

    # --- the session --------------------------------------------------------
    def run(self):
        import os88sym
        with os88ui.boot(self.sysimg, apps=APPIMG, machine=self.machine,
                         quick=True, verbose=False) as ui:
            self.ui, self.m = ui, ui.m
            self.eq = os88sym.equates(())
            try:
                self.go(ui)
            except Exception:
                # the picture at the moment it stopped, for whoever reads it
                p = os.path.join(tempfile.gettempdir(),
                                 "pagedeskxt-%s-fail.png" % self.name)
                try:
                    self.picture(p)
                    self.say("the screen when it stopped: %s" % p)
                except Exception:                    # noqa: BLE001
                    pass
                raise

    def go(self, ui):
        m = self.m
        self.stack_fill()
        free, big, shed, _ = self.heap()
        self.rows.append(("desktop, before PageDesk", None, free, big, None,
                          shed))
        ui.open_drive("B")

        # --- 1. HELLO.PDF by association -------------------------------
        c0 = self.cyc()
        w = ui.open("HELLO.PDF")
        self.w = w
        self.p = M.Pkg(m, w)
        cw = self.cyc()
        self.rows.append(("HELLO: double-click to PageDesk's window",
                          (cw - c0) / HZ, None, None, None, None))
        s, _ = self.track("HELLO: double-click to document open",
                          lambda s: s["doc"] == D_OPEN, c0)
        s, _ = self.track("HELLO: double-click to first page drawn",
                          self.idle, c0, guest=1800.0)
        self.layout = s["kind"]
        s = self.settle_pd("HELLO: page drawn to quiet (thumbnail)")
        self.deep_water("HELLO.PDF")

        # The Pages panel's thumbnails are ONE PDREND slice each on the
        # target (above), and a menu pressed while one runs waits for it: 12
        # of them for XTGUIDE would hold every input below for the better
        # part of half an hour. The cost is measured once, on HELLO; the rest
        # of the session runs with the panel hidden, which View offers.
        view = [c for c in ui.menus() if c[0] == "View"]
        if view and any(t.startswith("Hide Pages panel") for t, _ in view[0][3]):
            ui.menu_pick("View", "Hide Pages panel")
            s = self.settle_pd("hide the Pages panel to quiet")
            self.say("the Pages panel was hidden after HELLO (thumbnails)")

        # --- 2. XTGUIDE.PDF through File > Open --------------------------
        ui.menu_pick("File", "Open...")
        ui.chooser()
        c0 = self.cyc()
        ui.chooser_open("XTGUIDE.PDF")
        s, _ = self.track("XTGUIDE: chooser to document open",
                          lambda s: s["doc"] == D_OPEN and s["np"] == 12, c0)
        s, _ = self.track("XTGUIDE: chooser to first page drawn",
                          lambda s: s["np"] == 12 and self.idle(s), c0,
                          guest=1800.0)
        s = self.settle_pd("XTGUIDE: page drawn to quiet")
        self.deep_water("XTGUIDE.PDF")

        # --- 3. a page turn ---------------------------------------------
        pg = s["page"]
        c0 = self.cyc()
        m.key("PageDown")
        s, _ = self.track("page turn (PgDn) to page drawn",
                          lambda s: s["page"] == pg + 1 and self.idle(s), c0,
                          guest=1800.0)
        s = self.settle_pd("page turn: drawn to quiet")

        # --- 4. a zoom --------------------------------------------------
        was = (s["zoom"], s["fit"])
        c0 = self.cyc()
        m.type_text("+")
        s, _ = self.track("zoom in (+) to page drawn",
                          lambda s: (s["zoom"], s["fit"]) != was
                          and self.idle(s), c0, guest=1800.0)
        s = self.settle_pd("zoom: drawn to quiet")
        self.deep_water("XTGUIDE.PDF")

        # --- 5. Find over XTGUIDE ---------------------------------------
        ui.menu_pick("Edit", "Find...")
        self.track("Find's popover", lambda s: s["pop"] == P_FIND,
                   self.cyc(), guest=60.0)
        m.type_text(FIND)
        c0 = self.cyc()
        m.key("Enter")
        s, _ = self.track("Find %r across XTGUIDE to its answer" % FIND,
                          lambda s: s["job"] != J_FIND and (
                              s["msg"].startswith("Found on page")
                              or s["msg"].startswith("No match")), c0,
                          guest=3600.0)
        if not s["msg"].startswith("Found on page 12"):
            self.fails.append("Find %r answered %r, want `Found on page 12`"
                              % (FIND, s["msg"]))
        c0 = self.cyc()
        m.key("Escape")
        s, _ = self.track("Find: Esc to page 12 drawn",
                          lambda s: s["pop"] == P_NONE and s["page"] == 11
                          and self.idle(s), c0, guest=1800.0)
        s = self.settle_pd("Find: drawn to quiet")
        self.deep_water("XTGUIDE.PDF")

        # --- 6. an area mark --------------------------------------------
        m.key("F6")
        s, _ = self.track("Annotate mode (F6)", lambda s: s["mode"] == MODE_ANNOT
                          and self.quiet(s), self.cyc(), guest=600.0)
        m.type_text("5")                # FULL/MID: Mark; COMPACT: More
        if s["kind"] == L_COMPACT:
            s, _ = self.track("More's popover", lambda s: s["pop"] == P_MORE,
                              self.cyc(), guest=60.0)
            x1, y1, x2, y2 = s["poprect"]
            # a popover row: pducmd.c's pdu_pop_paint lays row k at
            # y1 + 4 + 2 + k * PDU_POP_ROW; Mark is COMPACT's row 0
            ui.mo.click(x1 + 16, y1 + 4 + 2 + 4, settle=0)
        s, _ = self.track("the Mark tool", lambda s: s["tool"] == C_T_MARK
                          and s["pop"] == P_NONE, self.cyc(), guest=600.0)
        s = self.settle_pd("the Mark tool to quiet")
        cx1, cy1, cx2, cy2 = s["canvas"]
        n0 = s["edn"]
        ax, ay = cx1 + (cx2 - cx1) // 4, cy1 + (cy2 - cy1) // 4
        bx, by = cx1 + (cx2 - cx1) // 2, cy1 + (cy2 - cy1) // 2
        c0 = self.cyc()
        ui.mo.drag(ax, ay, bx, by, settle=0)
        s, _ = self.track("area mark: drag to the mark drawn",
                          lambda s: s["edn"] == n0 + 1 and self.idle(s), c0,
                          guest=1800.0)
        s = self.settle_pd("area mark: drawn to quiet")
        self.deep_water("XTGUIDE.PDF")

        # --- 7. Save As ---------------------------------------------------
        ui.menu_pick("File", "Save As...")
        ui.chooser()
        c0 = self.cyc()
        ui.chooser_save(SAVE_AS)
        s, _ = self.track("Save As %s to `Saved`" % SAVE_AS,
                          lambda s: s["msg"].startswith("Saved")
                          or s["msg"].startswith("Could not"), c0,
                          guest=1800.0)
        if not s["msg"].startswith("Saved"):
            self.fails.append("Save As answered %r" % s["msg"])
        s, _ = self.track("Save As: to the re-opened page drawn",
                          self.idle, c0, guest=1800.0)
        s = self.settle_pd("Save As: drawn to quiet")
        self.deep_water(SAVE_AS)

        # --- 8. what is left -------------------------------------------------
        used, can = self.stack_read()
        self.ui_water = used
        if struct.unpack("<H", can)[0] != self.eq["SCH_MAGIC"]:
            self.fails.append("task 0's canary at STK0_BOT reads %s" % can.hex())
        if used > UI_STACK_MAX:
            self.fails.append("task 0's stack reached %d bytes, over D22's %d"
                              % (used, UI_STACK_MAX))
        for doc, d in self.deep.items():
            if d > DEEP_MAX:
                self.fails.append("%s: the private module stack reached %d "
                                  "bytes, over D23's %d" % (doc, d, DEEP_MAX))
        free, big, shed, mp = self.heap()
        self.claims = [c for c in mp.claims]
        self.rows.append(("after the session", None, free, big, None, shed))

        # the file, off the floppy, by a reader that is not os8088's
        import os88flush
        f = os88flush.Flush(marty=m)
        try:
            data = f.volume(1).read(SAVE_AS)
        except Exception as e:                       # noqa: BLE001
            data = None
            self.fails.append("%s is not on B: after the save (%s)"
                              % (SAVE_AS, e))
        self.saved = None
        if data:
            fd, path = tempfile.mkstemp(suffix=".pdf")
            os.write(fd, data)
            os.close(fd)
            self.saved = (len(data), qpdf_check(path), annots(path))
            chk = self.saved[1]
            if chk is not None and chk[0] != 0:     # 3 is warnings: a fail
                self.fails.append("qpdf --check %s: %s" % (SAVE_AS, chk[1]))
            an = self.saved[2]
            if an is not None and an.get("subtypes", {}).get("/Square", 0) < 1:
                self.fails.append("%s carries no /Square (the area mark): %r"
                                  % (SAVE_AS, an))
            os.unlink(path)
        shot = os.path.join(tempfile.gettempdir(), "pagedeskxt-%s.png"
                            % self.name)
        try:
            self.picture(shot)
            self.shot = shot
        except Exception:                            # noqa: BLE001
            self.shot = None


def report(leg):
    kinds = {0: "FULL", 1: "MID", 2: "COMPACT"}
    print("== %s : %s, 640 KiB, PageDesk %s layout ==" % (
        leg.name.upper(), leg.machine, kinds.get(getattr(leg, "layout", -1),
                                                  "?")))
    print("   %-44s %9s %9s %9s %9s %9s" % ("step", "guest s", "free KiB",
                                           "largest", "trough", "trough+"))
    k = lambda v: "-" if v is None else "%.1f" % (v / 1024.0)   # noqa: E731
    for what, secs, free, big, lo, lo_shed in leg.rows:
        print("   %-44s %9s %9s %9s %9s %9s" % (
            what[:44], "-" if secs is None else "%.2f" % secs,
            k(free), k(big), k(lo), k(lo_shed)))
    print("   (free/largest: after the step; trough: the lowest unclaimed "
          "during it; trough+: that with the purgeable caches shed)")
    if leg.heap_lo:
        print("   lowest unclaimed: %.1f KiB (largest run %.1f, %.1f with the "
              "caches shed), during %s" % (leg.heap_lo[0] / 1024.0,
                                           leg.heap_lo[1] / 1024.0,
                                           leg.heap_lo[2] / 1024.0,
                                           leg.heap_lo[3]))
    if hasattr(leg, "ui_water"):
        print("   task 0 stack: %d of 512 bytes (D22 bound %d)"
              % (leg.ui_water, UI_STACK_MAX))
    for doc, d in leg.deep.items():
        print("   private module stack, %s: %d of 2048 bytes (D23 bound %d)"
              % (doc, d, DEEP_MAX))
    if getattr(leg, "saved", None):
        n, chk, an = leg.saved
        print("   %s: %d bytes off the floppy; qpdf --check %s; annotations %s"
              % (getattr(leg, "save_as", SAVE_AS), n, "SKIP (no qpdf)" if chk is None
                 else "rc %d %s" % (chk[0], " ".join(chk[1])),
                 "SKIP" if an is None else an))
    if getattr(leg, "claims", None):
        print("   live claims after the session:")
        for c in leg.claims:
            print("      %r" % (c,))
    print("   poll resolution: worst gap between two samples %.3f guest s"
          % leg.res)
    if getattr(leg, "shot", None):
        print("   screen: %s" % leg.shot)
    for n in leg.notes:
        print("   " + n)
    for f in leg.fails:
        print("   FAIL " + f)
    print()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", action="append", choices=list(MACHINES))
    ap.add_argument("--serial", action="store_true",
                    help="one machine at a time (default: one thread each)")
    a = ap.parse_args()
    _hdr_consts()
    # ONCE, before any thread: `need` runs `make` when an image is stale, and
    # three makes of one target at once in one tree fail two of them
    for n in (a.only or MACHINES):
        need(MACHINES[n][1])
    need(APPIMG)
    P = pkg_syms()
    legs = [Leg(n, MACHINES[n], P) for n in (a.only or MACHINES)]
    errs = {}

    def one(leg):
        try:
            leg.run()
        # BaseException: a SystemExit inside a thread ends the thread SILENTLY
        # (threading drops it), and a leg that did nothing then reported ok
        except BaseException as e:                   # noqa: BLE001
            import traceback
            errs[leg.name] = "%s\n%s" % (e, traceback.format_exc()[-1500:])
    if a.serial or len(legs) == 1:
        for leg in legs:
            one(leg)
    else:
        th = [threading.Thread(target=one, args=(leg,)) for leg in legs]
        for t in th:
            t.start()
        for t in th:
            t.join()
    bad = 0
    for leg in legs:
        report(leg)
        if leg.name not in errs and not leg.rows:
            print("   FAIL %s measured nothing" % leg.name)
            bad += 1
        if leg.name in errs:
            print("   FAIL %s did not finish: %s" % (leg.name, errs[leg.name]))
            bad += 1
        bad += len(leg.fails)
    print("pagedeskxt: %s" % ("ok" if not bad else "%d failure(s)" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
