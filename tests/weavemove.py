#!/usr/bin/env python3
"""The Weave family's claims MOVE, every copy follows, the programs run on.

    make build/weavemove360.img && python3 tests/weavemove.py --pkg weave
    make build/weavemove360.img && python3 tests/weavemove.py --pkg loom

WEAVE-SPEC 1.2.6 (docs/plans/NO-PINNING-PLAN.md item C3 for Weave/Loom).
tests/emumove.py's recipe: Note Pad first, so its region takes the CEILING and
its document claim the floor; then the package under test; then heapfrag,
LAST, so its pinned blocks land below and wall nothing. Note Pad is closed -
a hole at the ceiling and one on the floor - and a key in heapfrag posts its
region compaction (heapfrag.asm hf_key, SPEC.md 66.4.3), which runs both
passes: the data claims slide DOWN and the regions UP.

WHY THIS IS NOT tests/pkgmove.py. These packages bank a moved base in MORE
THAN ONE place, and most of them are not package statics at all: the WJS VM
keeps the VM and bundle claims and the REGION (wvm_ourds, the DS it hands a
native) in its own .bss, the FX VM keeps the grid's, the canvas core keeps
the canvas and bundle claims in ITS state block - inside part 0, another
claim - and part 0 itself is named by a parts-table row and a far vector.
pkgmove checks that a word names a live claim; this checks that every COPY
names the SAME claim as the word it copies, which is what goes wrong.

--pkg weave: TWO WEAVE instances, PONG.WAB (bundle, VM, canvas, canvas core)
and SHEET.WAB (bundle, VM, grid). Assertions:

  1. every claim DECLARED - MC_RLOC non-zero in mem_tab, read from OUTSIDE;
  2. something of each instance MOVED (else the run proves nothing);
  3. every word names a live claim, and every COPY agrees with its word:
     wvm_vmseg/wvm_bseg/wfx_gseg, wvm_ourds = the region, the core's
     WSS_CVSEG/WSS_BSEG, the row's zkb and wcv_vec = the canvas core;
  4. the bytes survived: the bundle claim IS the .WAB file, the canvas core
     IS WEAVE.WSM up to its state block, the VM/grid/canvas claims hash the
     same at both addresses;
  5. both windows REDRAW the same picture (a repaint through every moved
     word: the band composer over the grid, the canvas core's PAINT);
  6. PONG still PLAYS: Serve is clicked - a WJS handler runs in a slice and
     calls start(), a native, through wvm_ourds - the worker is hired, frames
     run, the computer's paddle moves (ontick), the staging ring drops
     nothing; and the canvas core is now PINNED (bucket C: the worker parks
     inside it) while the canvas and bundle claims stay movable;
  7. ...and a move UNDER THE RUNNING WORKER: SHEET closes, a pass is posted
     from outside, PONG's bundle - which the worker reads every frame through
     the core's WSS_BSEG - moves while the core stays, every copy follows,
     and frames, the paddle and the ring carry on.

--pkg loom: LOOM opens PONG.WML (the source claim), then View > Preview
(Preview's two pack claims and the preview module, part 0). Assertions 1-5
in LOOM's terms: lm_srcseg / lm_outseg / lm_workseg / lm_pvseg declared,
moved, live; the row's zkb and lpv_vec follow lm_pvseg; the source claim
holds PONG.WML, the output claim IS PONG.WAB (LOOM packs byte-identical to
weavesim, WEAVE-SPEC 11.1), the module IS LOOM.WPV's code; and the Preview
pane - a far call through the moved vector over the moved output claim -
redraws the same picture. Then 6: View > Edit gives Preview's claims back, a
typed character lands in the moved source claim, ^S saves it (pinned across
the write, lm_save) and the claim is movable again after. And 7: a Pack
from the Preview pane gives Preview's two claims back first - it reused the
same two words and leaked 112KB until LOOM closed. Red with lm_pack's
lm_prev_off() taken out: 7 fails naming the two leaked claims.

RED RUNS (each verified by hand, see the suite row): WEAVE's os88_onmove
emptied fails 3 (stale words); LOOM's os88_onmove emptied fails 3.
WEAVEMOVE_DUMP=1 prints mem_tab before and after.
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88fixture                                      # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88ui                                           # noqa: E402
from cmemmove import pkg_syms, u16, claims, pkg_seg, heapfrag_ran  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISK = "build/weavemove360.img"
OP_ROW0_ZKB = 10 + 6                    # op_table + OP_T_ROWS + OP_R_ZKB, row 0
WSS_RUN, WSS_CVSEG, WSS_BSEG = 0, 8, 12
WSS_FRAMES, WSS_OVF = 20, 22
WSC_SPR, WSR_SIZE, WSR_Y = 16, 24, 6


def rd(path):
    return open(os.path.join(ROOT, path), "rb").read()


class Run(object):
    def __init__(self, ui):
        self.ui, self.m, self.S = ui, ui.m, ui.sym
        self.bad = 0
        self.dump = os.environ.get("WEAVEMOVE_DUMP")

    def check(self, ok, what, detail=""):
        print("  %s %s%s" % ("ok  " if ok else "FAIL", what,
                             ("  (%s)" % detail) if detail else ""))
        self.bad += not ok
        return ok

    def held(self):
        return {c[0]: c for c in claims(self.m, self.S)}

    def show(self, tag):
        if self.dump:
            print("    -- mem_tab %s" % tag)
            for c in sorted(claims(self.m, self.S)):
                print("    claim %04x +%04x own %04x rloc %04x" % c)

    def key(self, v):
        """A segment word as the heap keys its block - itself on a real-mode
        kernel. On kern_mega (KERN-MEGA-MEMORY-PLAN MC7.4) every one of
        these is an LDT selector a move REBASES and never changes - the
        region, each claim, the far vectors' CODE halves and the part rows -
        so they are compared, moved and read as the PARAGRAPH under them,
        and a code half and its data half are the same paragraph"""
        if getattr(self.m, "bochs", False) and v and 0x100 <= v < 0xA000 \
                and v & 4:
            b = self.m.selector(v)
            return b[0] >> 4 if b else v
        return v

    def rseg(self, win):
        return self.key(u16(self.m.read(os88geom.winptr(self.m, win.i,
                                                          self.S)
                                        + os88geom.W_SEG, 2)))

    def md5(self, seg, held):
        c = held.get(seg)
        return hashlib.md5(self.m.read(seg * 16, c[1] * 16)).digest() \
            if c else None

    def shots(self, wins):
        """Each window RAISED and repainted, its content band read back.

        Raised first because the z-order is not the same before and after the
        pass (Note Pad closes, heapfrag is fronted for its key), and a band
        another window covers is that window's picture, not ours."""
        m, out = self.m, []
        for win in wins:
            self.ui.raise_window(win)
            o = [x for x in os88geom.windows(m, self.S) if x.i == win.i][0]
            x, y = o.x + 1, o.y + os88geom.TITLE_H
            w, h = o.w - 3, o.h - os88geom.TITLE_H - 2
            self.ui.mo.to(2, 2)         # the ARROW off every window: a raise
                                        # leaves it on the title bar, and its
                                        # tail hangs into the band below
            m.write(self.S("cp_dirty"), b"\x01")
            os88marty.ui_done(m)
            os88marty.settle(m)
            f = m.fbuf()
            W = f[0]
            out.append((w, b"".join(f[2][(yy * W + x) * 3:(yy * W + x + w) * 3]
                                    for yy in range(y, y + h))))
        return out

    def same(self, p0, p1):
        """Equal bands, or None and the differing pixels' box printed."""
        if p0 == p1:
            return True
        w, a = p0
        b = p1[1]
        px = [i // 3 for i in range(0, min(len(a), len(b)), 3)
              if a[i:i + 3] != b[i:i + 3]]
        if px:
            xs = [i % w for i in px]
            ys = [i // w for i in px]
            print("    %d pixels differ, box x %d-%d y %d-%d of the band"
                  % (len(px), min(xs), max(xs), min(ys), max(ys)))
        return False

    def pass_(self, np, hf):
        """Close Note Pad, post heapfrag's region pass, wait for its wake."""
        ui, m, S = self.ui, self.m, self.S

        def hf_rn():
            hseg, _ = pkg_seg(m, S, "Heap")
            hseg = self.key(hseg)
            img = u16(m.read(hseg * 16 + 8, 2))
            return u16(m.read(hseg * 16 + img + 152, 2))   # heapfrag's hf_rn
        ui.close(np)
        ui.raise_window(hf)
        m.key("KeyA")
        os88marty.until(m, lambda _: hf_rn() >= 4,
                        "heapfrag's posted region pass and its wake",
                        poll=0.5, guest=120)
        os88marty.quiesce(m, lambda: claims(m, S), guest=1.0,
                          what="the claims after the pass")
        os88marty.ui_done(m)


# =============================================================================
# WEAVE
# =============================================================================
def weave(r):
    ui, m, S = r.ui, r.m, r.S
    C = pkg_syms("apps/weave/weave.asm")
    wsm = rd("build/WEAVE.WSM")
    wsm_so = u16(wsm, 6)                 # WSM_H_STATE, from the FILE

    def w(seg, name, off=0):
        return u16(m.read(seg * 16 + C[name] + off, 2))

    def state(win):
        reg = r.rseg(win)
        st = dict(reg=reg)
        K = r.key
        for k in ("_w_seg", "_w_vmseg", "_w_gseg", "_w_cseg", "_w_cmseg",
                  "wvm_vmseg", "wvm_bseg", "wvm_ourds", "wfx_gseg"):
            st[k] = K(w(reg, k))
        st["_w_cso"] = w(reg, "_w_cso")
        st["vec"] = K(w(reg, "wcv_vec", 2))
        st["zkb"] = K(w(reg, "op_table", OP_ROW0_ZKB))
        if st["_w_cmseg"]:
            cm, so = st["_w_cmseg"], st["_w_cso"]
            st["cv"] = K(u16(m.readseg(cm, so + WSS_CVSEG, 2)))
            st["bs"] = K(u16(m.readseg(cm, so + WSS_BSEG, 2)))
        return st

    CLAIMS = ("_w_seg", "_w_vmseg", "_w_gseg", "_w_cseg", "_w_cmseg")

    ui.open_drive("B")
    np = ui.open("NOTEPAD.O88")
    insts = []
    for name in ("SHEET.WAB", "PONG.WAB"):      # SHEET first: its claims are
                                                # the hole leg 7 moves PONG's
                                                # bundle into
        win = ui.open(name)
        os88marty.until(m, lambda _: state(win)["_w_vmseg"] != 0,
                        "%s's VM claim (the open's last)" % name,
                        poll=0.5, guest=120)
        os88marty.quiesce(m, lambda: claims(m, S), guest=1.0,
                          what="%s's claims" % name)
        insts.append((name, win, rd("build/" + name)))
    hf = ui.open("HEAPFRAG.O88")
    os88marty.until(m, lambda _: heapfrag_ran(m, S),
                    "heapfrag's suite and its posted pass", poll=0.5,
                    guest=240)
    os88marty.ui_done(m)

    held = r.held()
    s0 = {n: state(win) for n, win, _ in insts}
    h0 = {n: {k: r.md5(s0[n][k], held) for k in CLAIMS if s0[n][k]}
          for n in s0}
    for n in s0:
        print("%s at %04x: %s" % (n, s0[n]["reg"], " ".join(
            "%s=%04x" % (k.strip("_"), s0[n][k]) for k in CLAIMS)))
    r.show("before")
    for n in s0:
        gone = [k for k in CLAIMS if s0[n][k] and s0[n][k] not in held]
        und = [k for k in CLAIMS if s0[n][k] in held
               and not held[s0[n][k]][3]]
        r.check(not gone and not und,
                "1 %s: every claim declared movable" % n,
                "; ".join(x for x in (
                    "NAMES NO CLAIM (heapfrag's own pass moved it and the "
                    "word did not follow): %s" % gone if gone else "",
                    "not declared: %s" % und if und else "") if x))
    shot0 = r.shots([win for _, win, _ in insts])

    r.pass_(np, hf)
    r.show("after")
    held = r.held()
    s1 = {n: state(win) for n, win, _ in insts}
    for n in s1:
        print("after the pass: %s at %04x: %s" % (n, s1[n]["reg"], " ".join(
            "%s=%04x" % (k.strip("_"), s1[n][k]) for k in CLAIMS)))
    for n, win, wab in insts:
        a, b = s0[n], s1[n]
        moved = [k for k in CLAIMS + ("reg",) if a[k] != b[k]]
        r.check(bool(moved), "2 %s: something moved" % n,
                ", ".join("%s %04x->%04x" % (k.strip("_"), a[k], b[k])
                          for k in moved) or "NOTHING")
        stale = [k for k in CLAIMS if b[k] and b[k] not in held]
        r.check(not stale, "3 %s: every claim word is live" % n,
                "stale: %s" % stale if stale else "")
        cop = dict(wvm_vmseg=b["_w_vmseg"], wvm_bseg=b["_w_seg"],
                   wvm_ourds=b["reg"])
        if b["_w_gseg"]:
            cop["wfx_gseg"] = b["_w_gseg"]
        if b["_w_cmseg"]:
            cop.update(zkb=b["_w_cmseg"], vec=b["_w_cmseg"],
                       cv=b["_w_cseg"], bs=b["_w_seg"])
        wrong = ["%s=%04x want %04x" % (k, b[k], v) for k, v in cop.items()
                 if b[k] != v]
        r.check(not wrong, "3 %s: every copy agrees with its word" % n,
                "; ".join(wrong))
        ok = m.read(b["_w_seg"] * 16, len(wab)) == wab
        r.check(ok, "4 %s: the bundle claim is still the .WAB" % n)
        if b["_w_cmseg"]:
            ok = m.read(b["_w_cmseg"] * 16, wsm_so) == wsm[:wsm_so]
            r.check(ok, "4 %s: the canvas core is still WEAVE.WSM" % n)
        diff = [k for k in ("_w_vmseg", "_w_gseg", "_w_cseg")
                if b[k] and a[k] != b[k] and r.md5(b[k], held) != h0[n][k]]
        r.check(not diff, "4 %s: the moved claims' bytes survived" % n,
                "changed: %s" % diff if diff else "")
    shot1 = r.shots([win for _, win, _ in insts])
    for (n, _, _), p0, p1 in zip(insts, shot0, shot1):
        r.check(r.same(p0, p1),
                "5 %s: the window redraws the same picture" % n)

    # --- 6: PONG plays through the moved words ------------------------------
    n, win, _ = insts[1]
    ui.raise_window(win)
    o = [x for x in os88geom.windows(m, S) if x.i == win.i][0]
    bx, by = o.x + 20, o.y + 19 + 16 * 8 + 4        # weavegame's Serve
    ui.mo.to(bx, by)
    ui.mo.click(bx, by, settle=0.0)
    m.advance(frames=6)
    st = state(win)
    cm, so = st["_w_cmseg"], st["_w_cso"]

    def sw(off):
        return u16(m.readseg(cm, so + off, 2))
    # STEPPED, the machine stopped between steps, from the serve to the
    # reading: the ball reaches the computer's end ~3 guest seconds after the
    # serve and comes back, its paddle drifting home to 48 - so a wait whose
    # length is the HOST's (a poll loop on a running guest) read y0 after
    # the chase on a slow box, and y 48 -> 48 (mega-weavemove)
    for _ in range(300):
        if sw(WSS_RUN) & 0xFF:
            break
        m.advance(frames=6)
    else:
        raise os88marty.MartyError("start() never hired the worker in 30 "
                                   "guest seconds")
    cv = st["_w_cseg"]

    def cpu_y():
        return u16(m.readseg(cv, WSC_SPR + 2 * WSR_SIZE + WSR_Y, 2))
    y0, f0 = cpu_y(), sw(WSS_FRAMES)
    m.advance(frames=60)
    m.run()
    y1, f1 = cpu_y(), sw(WSS_FRAMES)
    r.check(f1 > f0, "6 PONG: frames run after the move", "%d" % (f1 - f0))
    r.check(y1 != y0, "6 PONG: the computer's paddle moved (ontick, a slice)",
            "y %d -> %d" % (y0, y1))
    r.check(sw(WSS_OVF) == 0, "6 PONG: the staging ring dropped nothing",
            "ovf %d" % sw(WSS_OVF))
    held = r.held()
    c = held.get(cm)
    r.check(bool(c) and c[3] == 0,
            "6 PONG: the canvas core is PINNED once the worker stands in it",
            "MC_RLOC %04x" % (c[3] if c else 0xFFFF))
    keep = [k for k in ("_w_seg", "_w_cseg", "_w_vmseg")
            if not (held.get(st[k]) and held[st[k]][3])]
    r.check(not keep, "6 PONG: bundle, VM and canvas stay movable",
            "pinned: %s" % keep if keep else "")

    # --- 7: a move UNDER THE RUNNING WORKER ---------------------------------
    # SHEET closes, so its claims - below PONG's - are holes, and a pass is
    # posted from outside (pkgmove's door, [mem_cpq] at the TRIVIAL rank).
    # The compactor parks the worker at OSAPI_TASK_ALIVE (inst_park_req) and
    # moves PONG's bundle - which the worker READS every frame, through the
    # core's WSS_BSEG - while the canvas core stays where the worker stands.
    ui.close(insts[0][1])
    os88marty.quiesce(m, lambda: claims(m, S), guest=1.0,
                      what="the claims after SHEET closed")
    a = state(win)
    print("  (the worker's RUN at the post: %d)" % (sw(WSS_RUN) & 0xFF))
    wptr = os88geom.winptr(m, win.i, S)
    m.write(S("mem_cpq_lvl"), bytes([0xFC]))
    m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
    os88marty.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                    "the posted compaction", poll=0.2, guest=30)
    os88marty.ui_done(m)
    b = state(win)
    held = r.held()
    print("under the worker: w_seg %04x->%04x cv %04x->%04x vm %04x->%04x "
          "cm %04x->%04x" % (a["_w_seg"], b["_w_seg"], a["_w_cseg"],
                            b["_w_cseg"], a["_w_vmseg"], b["_w_vmseg"],
                            a["_w_cmseg"], b["_w_cmseg"]))
    r.check(a["_w_seg"] != b["_w_seg"],
            "7 PONG: the bundle moved while the worker ran",
            "%04x -> %04x" % (a["_w_seg"], b["_w_seg"]))
    r.check(b["_w_cmseg"] == a["_w_cmseg"],
            "7 PONG: the canvas core did not (the worker stands in it)")
    wrong = ["%s=%04x want %04x" % (k, b[k], v) for k, v in
             (("bs", b["_w_seg"]), ("cv", b["_w_cseg"]),
              ("wvm_bseg", b["_w_seg"]), ("wvm_vmseg", b["_w_vmseg"]))
             if b[k] != v]
    r.check(not wrong and all(b[k] in held for k in
                              ("_w_seg", "_w_cseg", "_w_vmseg")),
            "7 PONG: every word and copy names the claim it moved to",
            "; ".join(wrong))
    # SERVE AGAIN: the rally leg 6 started has likely ended in a goal by now
    # (onGoal stops the field), and a serve is a WJS handler running against
    # the bundle and VM where they are NOW, then start() through the worker
    # that was parked across the move.
    ui.raise_window(win)
    ui.mo.to(bx, by)
    ui.mo.click(bx, by, settle=0.0)
    m.advance(frames=6)
    m.run()
    os88marty.until(m, lambda _: sw(WSS_RUN) & 0xFF,
                    "the second serve's start()", poll=0.3, guest=30)
    f0, ovf0 = sw(WSS_FRAMES), sw(WSS_OVF)
    cv = b["_w_cseg"]

    def ball_x():                       # sprite 0, WSR_X at +4 of a record
        return u16(m.readseg(cv, WSC_SPR + 4, 2))
    x0 = ball_x()
    m.pause()
    m.advance(frames=30)
    m.run()
    r.check(sw(WSS_FRAMES) > f0, "7 PONG: frames still run",
            "%d" % (sw(WSS_FRAMES) - f0))
    r.check(ball_x() != x0, "7 PONG: the ball still moves",
            "x %d -> %d (this serve goes AWAY from the computer, so its "
            "paddle has nothing to chase - leg 6 asserted that one)"
            % (x0, ball_x()))
    r.check(sw(WSS_OVF) == ovf0 == 0, "7 PONG: nothing dropped")
    r.check(m.read(b["_w_seg"] * 16, len(insts[1][2])) == insts[1][2],
            "7 PONG: the bundle claim is still PONG.WAB")


# =============================================================================
# LOOM
# =============================================================================
def loom(r):
    ui, m, S = r.ui, r.m, r.S
    C = pkg_syms("apps/loom/loom.asm")
    wml = rd("apps/weave/demos/pong.wml")
    wab = rd("build/PONG.WAB")
    wpv = rd("build/LOOM.WPV")
    WORDS = ("_lm_srcseg", "_lm_outseg", "_lm_workseg", "_lm_pvseg")

    def w(seg, name, off=0):
        return u16(m.read(seg * 16 + C[name] + off, 2))

    def state(win):
        reg = r.rseg(win)
        st = dict(reg=reg, state=w(reg, "_lm_state"))
        for k in WORDS:
            st[k] = r.key(w(reg, k))
        st["vec"] = r.key(w(reg, "lpv_vec", 2))
        st["zkb"] = r.key(w(reg, "op_table", OP_ROW0_ZKB))
        return st

    ui.open_drive("B")
    np = ui.open("NOTEPAD.O88")
    win = ui.open("PONG.WML")
    os88marty.until(m, lambda _: state(win)["state"] == 1,
                    "LOOM's project open (lm_state EDIT)", poll=0.5,
                    guest=120)
    ui.menu_pick("View", "Preview")
    os88marty.until(m, lambda _: state(win)["state"] == 2,
                    "LOOM's Preview (lm_state PREVIEW)", poll=0.5, guest=240)
    os88marty.quiesce(m, lambda: claims(m, S), guest=1.0,
                      what="LOOM's claims")
    hf = ui.open("HEAPFRAG.O88")
    os88marty.until(m, lambda _: heapfrag_ran(m, S),
                    "heapfrag's suite and its posted pass", poll=0.5,
                    guest=240)
    os88marty.ui_done(m)

    held = r.held()
    s0 = state(win)
    h0 = {k: r.md5(s0[k], held) for k in WORDS}
    print("LOOM at %04x: %s" % (s0["reg"], " ".join(
        "%s=%04x" % (k.strip("_"), s0[k]) for k in WORDS)))
    r.show("before")
    gone = [k for k in WORDS if s0[k] not in held]
    und = [k for k in WORDS if s0[k] in held and not held[s0[k]][3]]
    r.check(not gone and not und, "1 LOOM: every claim declared movable",
            "; ".join(x for x in (
                "NAMES NO CLAIM (heapfrag's own pass moved it and the word "
                "did not follow): %s" % gone if gone else "",
                "not declared: %s" % und if und else "") if x))
    shot0 = r.shots([win])

    r.pass_(np, hf)
    r.show("after")
    held = r.held()
    s1 = state(win)
    print("after the pass: LOOM at %04x: %s" % (s1["reg"], " ".join(
        "%s=%04x" % (k.strip("_"), s1[k]) for k in WORDS)))
    moved = [k for k in WORDS + ("reg",) if s0[k] != s1[k]]
    r.check(bool(moved), "2 LOOM: something moved",
            ", ".join("%s %04x->%04x" % (k.strip("_"), s0[k], s1[k])
                      for k in moved) or "NOTHING")
    stale = [k for k in WORDS if s1[k] not in held]
    r.check(not stale, "3 LOOM: every claim word is live",
            "stale: %s" % stale if stale else "")
    wrong = ["%s=%04x" % (k, s1[k]) for k in ("zkb", "vec")
             if s1[k] != s1["_lm_pvseg"]]
    r.check(not wrong, "3 LOOM: the row's zkb and lpv_vec follow lm_pvseg",
            "; ".join(wrong) + " want %04x" % s1["_lm_pvseg"] if wrong else "")
    r.check(m.read(s1["_lm_srcseg"] * 16, len(wml)) == wml,
            "4 LOOM: the source claim still holds PONG.WML")
    r.check(m.read(s1["_lm_outseg"] * 16, len(wab)) == wab,
            "4 LOOM: the output claim is still PONG.WAB")
    r.check(m.read(s1["_lm_pvseg"] * 16, 4096) == wpv[:4096],
            "4 LOOM: the preview module is still LOOM.WPV")
    diff = [k for k in ("_lm_workseg",) if s0[k] != s1[k]
            and r.md5(s1[k], held) != h0[k]]
    r.check(not diff, "4 LOOM: the scratch claim's bytes survived",
            "changed" if diff else "")
    shot1 = r.shots([win])
    r.check(r.same(shot0[0], shot1[0]),
            "5 LOOM: the Preview pane redraws the same picture")

    # --- 6: edit and SAVE out of the moved source claim ---------------------
    # View > Edit gives Preview's two claims back; a typed character modifies
    # slot 0 IN the moved claim, and ^S writes it - pinned across the write
    # (lm_save) and declared again after, which is what is read here.
    ui.menu_pick("View", "Edit")
    os88marty.until(m, lambda _: state(win)["state"] == 1,
                    "LOOM back in the editor", poll=0.3, guest=60)
    st = state(win)
    r.check(st["_lm_outseg"] == 0 and st["_lm_workseg"] == 0,
            "6 LOOM: Preview's two claims were given back")
    src = st["_lm_srcseg"]
    m.key("KeyQ")
    os88marty.until(m, lambda _: m.read(src * 16, 1) == b"q",
                    "the typed character in the source claim", poll=0.3,
                    guest=30)
    smod = C["_lm_smod"]
    m.ctrl("KeyS")
    os88marty.until(m, lambda _: m.read(state(win)["reg"] * 16 + smod, 1)
                    == b"\0", "^S to save slot 0", poll=0.5, guest=120)
    os88marty.ui_done(m)
    held = r.held()
    st = state(win)
    c = held.get(st["_lm_srcseg"])
    r.check(bool(c) and c[3] != 0,
            "6 LOOM: saved, and the source claim is movable again after the "
            "write's pin", "MC_RLOC %04x" % (c[3] if c else 0xFFFF))
    r.check(m.read(st["_lm_srcseg"] * 16, len(wml) + 1) == b"q" + wml,
            "6 LOOM: the claim holds what was saved")

    # --- 7: Pack WHILE Preview is up gives Preview's claims back ------------
    # lm_pack_claim() reuses lm_outseg/lm_workseg, so a Pack from the Preview
    # pane overwrote both words and leaked 112KB until LOOM closed (found by
    # this work, fixed in lm_pack). LOOM's claims after the Pack must be what
    # they were in the editor before the Preview.
    # The "q" is a pack error, and a Preview that fails to pack does not go
    # up: take it back out and save, so the project packs again
    m.key("Backspace")
    os88marty.until(m, lambda _: m.read(src * 16, 1) != b"q",
                    "the typed character deleted", poll=0.3, guest=30)
    m.ctrl("KeyS")
    os88marty.until(m, lambda _: m.read(state(win)["reg"] * 16 + smod, 1)
                    == b"\0", "^S to save slot 0 again", poll=0.5, guest=120)
    os88marty.ui_done(m)

    def owned():
        reg = state(win)["reg"]
        return sorted((c[0], c[1]) for c in claims(m, S)
                      if r.key(c[2]) == reg)
    base = owned()
    ui.menu_pick("View", "Preview")
    os88marty.until(m, lambda _: state(win)["state"] == 2,
                    "LOOM's Preview again", poll=0.5, guest=240)
    m.ctrl("KeyP")
    os88marty.until(m, lambda _: (lambda st: st["_lm_outseg"] == 0 and st["_lm_workseg"] == 0
                    and w(st["reg"], "_lm_packran") == 1)(state(win)),
                    "^P to pack from the Preview pane", poll=0.5, guest=240)
    os88marty.quiesce(m, owned, guest=1.0, what="LOOM's claims")
    st = state(win)
    after = owned()
    r.check(after == base and st["state"] == 1,
            "7 LOOM: a Pack from Preview leaks neither of its claims",
            "before %s, after %s" % (base, after))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pkg", choices=("weave", "loom"), default="weave")
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    a = ap.parse_args()
    os88fixture.need(DISK)
    with os88ui.boot("build/os8088-360.img", apps=DISK, machine=a.machine,
                     quick=True) as ui:
        r = Run(ui)
        (weave if a.pkg == "weave" else loom)(r)
        print("VERDICT:", "OK" if not r.bad else "%d PROBLEM(S)" % r.bad)
        return 1 if r.bad else 0


if __name__ == "__main__":
    sys.exit(main())
