#!/usr/bin/env python3
"""A DESELECT TAKES THE HIGHLIGHT OFF WITH THE XORS THAT PUT IT ON (SPEC.md 27.8.2.6).

    make worddisk && python3 tests/wdunsel.py

Every selected row reaches the glass as upright glyphs and ONE XOR fill over
its selected span (wd_selxor, and the selection-only path's delta). So a click
that clears a selection needs no walk to put the rows back: the same fills,
again, restore them exactly. wd_sxrec banks each visible row's inverted span
as wd_rflush finishes it, wd_shiftrows carries the bank with the tables on a
scroll, and wd_sxdesel replays it. Before it a click below a 15-row selection
re-lettered the view twice over: 2.2 s on a 5150.

THE ASSERTION IS PIXELS against a repaint the fast path never touched - the
view scrolled away and back with the scroll bar, which moves no caret - and
the trap is everything that puts pixels on a row WITHOUT wd_rflush:

  leg A  a selection with ragged ends (mid-row to mid-row), cleared by a
         click below it: the fast path runs, the glass equals a repaint, and
         the click costs a PLAIN click at the same spot plus its fills
  leg B  a selection made by a drag that AUTO-SCROLLED, so every row it
         inverted after the first step reached its place by a BLIT - the bank
         rode wd_shiftrows or the fills land a row off
  leg C  three Down arrows after a fast deselect: the rows it cleared keep a
         signature with the selection folded in, and the walks that pass them
         must letter them upright rather than trust the glass
  leg D  the same as A in a chosen face, where a cell's x is wd_px[] and not
         8k - the bank is taken while the row's own wd_px[] is in place
  leg E  the A/B inside one boot: wd_sxdesel poked to `stc; ret` is the old
         path, the same selection and click must give the same pixels, and
         the fast one must be at least three times cheaper
  legs F-H  a click INSIDE the selection, which is wd_dragmove's and not
         wd_onclick's: a press there may be the start of a drag-and-drop, so
         it is resolved on the release - and that path redrew the view twice
         (2.5 s, 52 rows) until it took the bank as well. F: most of the page,
         clicked in the middle. G: part of one line, clicked inside it. H: the
         field's report - a drag that scrolled, past WELCOME.DOC's flush-right
         line, then a click in the gap under that line, which names the next
         row and so lands inside the selection. Each takes the fast path,
         redraws at most four rows and equals a repaint
"""
import os, sys, time, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty as M
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
GUEST_HZ = 4772728.0
ms = lambda c: c / GUEST_HZ * 1000.0
FAIL = []

# the ribbon's Font combo and the dropdown record, as tests/wdcombo.py reads them
WD_MENU_H = 14
WD_RB_FBX, WD_RB_FBW = 56, 96
DR_OPEN, DR_HOT, DR_TOP = 16, 17, 22
OS88UI_DRIH = 10


def check(name, ok, detail=""):
    print("   %-58s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src="apps/word/word.asm", incs=("apps/", "apps/word/")):
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"] + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for L in open(mp):
            f = L.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[1], 16)   # VIRTUAL: part 1 is assembled at WD_P1ORG
        return out, open(os.path.join(d, "p.bin"), "rb").read()


def shot(m):
    if m.cards()[0]["type"] in ("cga", "mda"):
        w, h, rows = m.vram()
        return w, h, bytes(b for r in rows for b in r)
    w, h, px = m.fbuf()
    return w, h, bytes(1 if px[i] or px[i+1] or px[i+2] else 0
                       for i in range(0, len(px), 3))


def band(sh, box):
    w, _, px = sh
    x0, y0, x1, y1 = box
    return bytes(px[y*w + x] for y in range(y0, y1+1) for x in range(x0, x1+1))


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_herc_gla")
a = ap.parse_args()
syms, image = pkg_syms()
DISK = "build/wdunselgate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m)
    print("== Word: a deselect is the XORs taken back (SPEC.md 27.8.2.6) ==")
    dispcp.open_drive(m, mo, S, M.settle, "B")
    w = dispcp.win_list(m, S)[-1]; dx, dy = dispcp.win_rect(m, S, w)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")
    time.sleep(2.5); M.settle(m)

    raw = m.read(S("inst_tab"), 32*12); seg = None
    for i in range(12):
        b = i*32
        if raw[b] == 1 and (raw[b+2] & 0x80):
            c = u16(raw, b+6)
            if m.read(c*16+syms["wd_mact"], 48) == image[syms["wd_mact"]:syms["wd_mact"]+48]:
                seg = c; break
    if seg is None:
        sys.exit("could not locate the running package (stale build/word.o88?)")
    base = seg*16; P = lambda n: base + syms[n]
    rw = lambda n: u16(m.read(P(n), 2)); rb = lambda n: m.read(P(n), 1)[0]

    def settle_height():
        for _ in range(600):
            if rb("wd_hdirty") == 0:
                return
            m.advance(cycles=2_000_000)

    def geom():
        g = dict(tx=rw("wd_tx"), ty=rw("wd_ty"), cl=rw("wd_cl"), sbr=rw("wd_sbr"),
                 bot=rw("wd_bot"), sbb=rw("wd_sbb"), vrows=rw("wd_vrows"))
        g["box"] = (g["cl"], g["ty"], g["sbr"], g["bot"])
        g["sbx"] = g["sbr"] - 7
        g["ydn"] = (g["ty"] + g["sbb"])//2 + (g["sbb"] - g["ty"])//4
        g["yup"] = g["ty"] + (g["sbb"] - g["ty"])//4
        return g

    ryb = lambda r: u16(m.read(P("wd_ryb") + 2*r, 2))

    def page_round_trip(g, top0):
        mo.to(g["sbx"], g["ydn"]); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
        for _ in range(12):
            if rw("wd_top") <= top0:
                break
            mo.to(g["sbx"], g["yup"]); time.sleep(0.25)
            m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
        mo.to(4, 4); time.sleep(1.0); M.settle(m)
        return rw("wd_top") == top0

    def to_top(g):
        for _ in range(12):
            if rw("wd_top") == 0:
                return
            mo.to(g["sbx"], g["yup"]); time.sleep(0.25)
            m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
        M.settle(m)

    def select(g, r0, x0, r1, x1, hold_below=0):
        mo.to(g["tx"] + x0, ryb(r0) + 2); time.sleep(0.4)
        mo._edge(True); time.sleep(0.4)
        if hold_below:
            mo.to(g["tx"] + x1, g["bot"] + 3, l=True)
            deadline = time.time() + 60
            t0 = rw("wd_top")
            while rw("wd_top") < t0 + hold_below and time.time() < deadline:
                time.sleep(0.3)
            mo.to(g["tx"] + x1, ryb(r1) + 2, l=True); time.sleep(1.2)
        else:
            mo.to(g["tx"] + x1, ryb(r1) + 2, l=True); time.sleep(1.2)
        mo._edge(False); time.sleep(1.0); M.settle(m)
        return rb("wd_selon"), rw("wd_sel0"), rw("wd_sel1")

    def nvis(g):
        n = 0
        while n < g["vrows"] and ryb(n) + rw("wd_gh") <= g["bot"]:
            n += 1
        return n

    def deselect(g, row):
        """Click at the start of `row`; (fast path ran, guest ms up to
        wd_dragsel - the pointer-following loop after it waits on the HOST's
        release, so it is timed out rather than in)."""
        mo.to(g["tx"] + 4, ryb(row) + 2); time.sleep(0.4); M.settle(m)
        T, hit = {}, []
        NM = {}
        for nm in ("wd_onclick", "wd_dragsel", "wd_sxdesel.ret"):
            a2 = P(nm)
            for kk in (a2, "%X" % a2, str(a2)):
                NM[kk] = nm
        def on(mm, rec):
            nm = NM.get(rec.get("name")); cyc = int(mm.status()["cycles"])
            if nm == "wd_onclick":
                T.setdefault("in", cyc)
            elif nm == "wd_dragsel":
                T.setdefault("out", cyc)
            elif nm == "wd_sxdesel.ret":
                hit.append(not (mm.regs()["flags"] & 1))
            return None
        with M.bp_trace(m, P("wd_onclick"), P("wd_dragsel"), P("wd_sxdesel.ret"),
                        on_hit=on, cap=64):
            mo._edge(True); time.sleep(0.15); mo._edge(False)
            M.quiesce(m, lambda: (rb("wd_selon"), rw("wd_cur")))
            time.sleep(0.5)
        mo.to(4, 4); time.sleep(0.8); M.settle(m)
        return (bool(hit) and hit[0]), ms(T.get("out", 0) - T.get("in", 0))

    def inside(g, x, y):
        """A click that lands INSIDE the selection: (fast path, rows flushed)."""
        mo.to(x, y); time.sleep(0.4); M.settle(m)
        st = {"fast": None, "rf": 0}
        NM = {}
        for nm in ("wd_sxdesel.ret", "wd_rflush"):
            a2 = P(nm)
            for kk in (a2, "%X" % a2, str(a2)):
                NM[kk] = nm
        def on(mm, rec):
            nm = NM.get(rec.get("name"))
            if nm == "wd_rflush":
                st["rf"] += 1
            elif st["fast"] is None:
                st["fast"] = not (mm.regs()["flags"] & 1)
            return None
        with M.bp_trace(m, P("wd_sxdesel.ret"), P("wd_rflush"), on_hit=on, cap=400):
            mo._edge(True); time.sleep(0.15); mo._edge(False)
            M.quiesce(m, lambda: (rb("wd_selon"), rw("wd_cur")))
            time.sleep(0.6)
        mo.to(4, 4); time.sleep(0.8); M.settle(m)
        return bool(st["fast"]), st["rf"]

    def against_repaint(g):
        now = shot(m)
        top0 = rw("wd_top")
        if not page_round_trip(g, top0):
            return None
        rp = shot(m)
        x0, y0, x1, y1 = g["box"]
        w = x1 - x0 + 1
        diff = [i for i, (p, q) in enumerate(zip(band(now, g["box"]), band(rp, g["box"])))
                if p != q]
        if diff:
            xs = [x0 + i % w for i in diff]
            ys = [y0 + i // w for i in diff]
            print("      differing pixels in x %d..%d, y %d..%d; caret at [wd_cur]=%d, "
                  "row %d" % (min(xs), max(xs), min(ys), max(ys), rw("wd_cur"),
                              rw("wd_currow")))
        return len(diff)

    settle_height()
    g = geom()
    print("   vrows=%d len=%d hasfmt=%d" % (g["vrows"], rw("wd_len"), rb("wd_hasfmt")))

    # ---- leg A: ragged ends, cleared by a click below ------------------------
    # The reference is a PLAIN click at the same spot with nothing selected:
    # the hit walk and the caret row are every click's, and what a deselect
    # may add over it is its fills - measured at ~6 ms a row on a Hercules.
    NV = nvis(g)
    RA = NV - 4
    _, tP = deselect(g, RA + 2)
    on, s0, s1 = select(g, 1, 40, RA, 200)
    check("A: the drag selected across %d rows (case, not assertion)" % RA,
          on == 1 and s1 - s0 > 200, "selon=%d %d..%d" % (on, s0, s1))
    fast, tA = deselect(g, RA + 2)
    dA = against_repaint(g)
    print("      deselect: %.1f ms against a plain click's %.1f, fast path %s"
          % (tA, tP, fast))
    check("A: the click took the fast path", fast, "wd_sxdesel refused")
    check("A: ...and the glass equals a full repaint", dA == 0,
          "%s differing pixels" % dA)
    check("A: ...and costs a plain click plus under 10 ms a row",
          0 < tA < tP + 10 * RA, "%.1f ms against %.1f + %d rows" % (tA, tP, RA))

    # ---- leg C: keystrokes over the rows it cleared -------------------------
    mo.to(g["tx"] + 16, ryb(2) + 2); time.sleep(0.3)
    m.mouse(l=True); time.sleep(0.1); m.mouse(l=False); time.sleep(1.0)
    select(g, 1, 40, RA - 3, 120)
    deselect(g, RA - 1)
    mo.to(g["tx"] + 16, ryb(3) + 2); time.sleep(0.3)
    m.mouse(l=True); time.sleep(0.1); m.mouse(l=False); time.sleep(1.0)
    for _ in range(3):
        m.key("ArrowDown"); time.sleep(0.9)
    mo.to(4, 4); time.sleep(0.8); M.settle(m)
    dC = against_repaint(g)
    check("C: Down across the cleared rows still equals a repaint", dC == 0,
          "%s differing pixels" % dC)

    # ---- leg B: a drag that auto-scrolled -----------------------------------
    to_top(g)
    on, s0, s1 = select(g, 2, 24, NV // 2, 160, hold_below=3)
    topB = rw("wd_top")
    check("B: the drag scrolled the view (case, not assertion)", topB >= 3,
          "top %d" % topB)
    fast, tB = deselect(g, NV // 2 + 2)
    dB = against_repaint(g)
    print("      deselect after a scrolling drag: %.1f ms, fast path %s" % (tB, fast))
    check("B: the fast path after a scroll", fast, "wd_sxdesel refused")
    check("B: ...and the glass equals a full repaint", dB == 0,
          "%s differing pixels" % dB)

    # ---- leg E: the A/B inside one boot --------------------------------------
    to_top(g)
    select(g, 1, 40, RA, 200)
    fast1, tF = deselect(g, RA + 2)
    one = shot(m)
    keep = m.read(P("wd_sxdesel"), 2)
    m.write(P("wd_sxdesel"), bytes([0xF9, 0xC3]))       # stc; ret - refuse
    select(g, 1, 40, RA, 200)
    fast2, tS = deselect(g, RA + 2)
    two = shot(m)
    m.write(P("wd_sxdesel"), keep)
    dE = sum(1 for p, q in zip(band(one, g["box"]), band(two, g["box"])) if p != q)
    print("      the same deselect: %.1f ms fast, %.1f ms without the bank" % (tF, tS))
    check("E: the refused arm really is the old path (case, not assertion)",
          fast1 and not fast2, "fast %s / refused %s" % (fast1, fast2))
    check("E: both arms draw the SAME screen", dE == 0, "%d differing pixels" % dE)
    check("E: ...and the bank is at least 3x cheaper", tF > 0 and tS > 3 * tF,
          "%.1f ms against %.1f" % (tF, tS))

    # ---- legs F-H: a click INSIDE the selection (wd_dragmove) ----------------
    to_top(g)
    on, s0, s1 = select(g, 1, 40, NV - 3, 200)
    fF, rF = inside(g, g["tx"] + 100, ryb(NV // 2) + 2)
    dF = against_repaint(g)
    check("F: most of the page, clicked inside: the fast path",
          on == 1 and fF and rF <= 4, "selon %d, fast %s, %d rows flushed" % (on, fF, rF))
    check("F: ...and the glass equals a full repaint", dF == 0,
          "%s differing pixels" % dF)
    on, s0, s1 = select(g, 3, 16, 3, 150)
    fG, rG = inside(g, g["tx"] + 80, ryb(3) + 2)
    dG = against_repaint(g)
    check("G: part of a line, clicked inside it: the fast path",
          on == 1 and fG and rG <= 4, "selon %d, fast %s, %d rows flushed" % (on, fG, rG))
    check("G: ...and the glass equals a full repaint", dG == 0,
          "%s differing pixels" % dG)
    to_top(g)
    select(g, 2, 24, NV - 2, 160, hold_below=3)
    note = m.read(u16(m.read(P("wd_dseg"), 2)) * 16, rw("wd_len"))
    mark = note.find(b"flush right")
    rowsA = lambda r: u16(m.read(P("wd_rows") + 2*r, 2))
    FR = None
    for r in range(min(rw("wd_rowsn"), g["vrows"]) - 1):
        if rowsA(r) <= mark < rowsA(r + 1):
            FR = r
            break
    inH = FR is not None and rb("wd_selon") and rw("wd_sel0") <= mark < rw("wd_sel1")
    check("H: a scrolled selection runs past the flush-right line (case, "
          "not assertion)", inH, "top %d, row %s" % (rw("wd_top"), FR))
    if inH:
        fH, rH = inside(g, g["tx"] + 60, (ryb(FR) + rw("wd_gh") + ryb(FR + 1)) // 2)
        dH = against_repaint(g)
        print("      the gap under the flush-right line: fast %s, %d rows" % (fH, rH))
        check("H: the gap under it, clicked: the fast path", fH and rH <= 4,
              "fast %s, %d rows flushed" % (fH, rH))
        check("H: ...and the glass equals a full repaint", dH == 0,
              "%s differing pixels" % dH)

    # ---- leg D: a chosen face ------------------------------------------------
    Rf = base + syms["wd_dfont"]
    fopen = lambda: m.read(Rf + DR_OPEN, 1)[0]
    bx, boxtop = rw("wd_cl") + WD_RB_FBX + WD_RB_FBW // 2, rw("wd_ct") + WD_MENU_H + 2
    mo.to(bx, boxtop + 6); mo._edge(True)
    M.until(m, lambda mm: fopen() == 1, "the Font list to come down", poll=0.15, limit=40.0)
    if rb("wd_nfont") < 1:
        mo._edge(False); time.sleep(1.0)
        check("D: the disk carries a face (case, not assertion)", False, "no faces")
    else:
        top = u16(m.read(Rf + DR_TOP, 2))
        mo.to(bx, top + OS88UI_DRIH + 5, l=True)
        M.until(m, lambda mm: m.read(Rf + DR_HOT, 1)[0] == 1, "the pointer on face 1",
                poll=0.15, limit=40.0)
        mo._edge(False)
        M.until(m, lambda mm: fopen() == 0 and rb("wd_prop") != 0, "the face to take",
                poll=0.15, limit=40.0)
        mo.to(4, 4); M.settle(m); settle_height(); M.settle(m)
        g = geom(); to_top(g)
        check("D: a chosen face is in (case, not assertion)", rb("wd_pxon") != 0,
              "[wd_pxon]=0")
        ND = nvis(g)
        onD, _, _ = select(g, 1, 40, ND - 3, 150)
        check("D: the drag selected (case, not assertion)", onD == 1, "selon=0")
        fast, tD = deselect(g, ND - 1)
        dD = against_repaint(g)
        print("      deselect in a chosen face: %.1f ms, fast path %s" % (tD, fast))
        check("D: the fast path in a chosen face", fast, "wd_sxdesel refused")
        check("D: ...and the glass equals a full repaint", dD == 0,
              "%s differing pixels" % dD)

print()
print("wdunsel: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
sys.exit(1 if FAIL else 0)
