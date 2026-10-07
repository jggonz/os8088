#!/usr/bin/env python3
"""A CLICK IN WORD IS A CARET MOVE (SPEC.md 27.4.7).

    make worddisk && python3 tests/wdclick.py

SPEC.md 27.4.4 found Left and Right reaching wd_redraw with no BOUND. A click
reached it with no KIND: [wd_fast] was never set on the mouse path at all, so
[wd_ekind] was 0 and every cheap thing 27.4.1, 27.4.4 and 27.4.6 built was off
at once - no seed, so the pair could not bound the walk, and not kind 4, so
wd_1pok refused and the single pass did not apply either. The view was laid
out from its top row TWICE to move one caret bar: measured on a cycle-accurate
5150 with WELCOME.DOC in a 41-row window, 1,030 ms for a click on row 1.

  leg A  the ROWS the walk finishes, which is the quantity that changed: a
         caret move dirties TWO rows and now walks exactly those two, whatever
         it crossed - where an unarmed redraw walks the separation plus one,
         and an unseeded one the whole view. Rows and not wd_walk CALLS,
         because the fast path deliberately makes two walks of one row each
         (SPEC.md 27.4.9) and a call count cannot tell that from the two
         PASSES it replaced
  leg B  the pixels, for a click FORWARDS, a click BACKWARDS and a click at
         each end of the view - each against a full repaint, which is a page
         down and back (a formatted document always repaints in full, 68.6)
  leg C  the A/B inside one boot: wd_clickcm is the whole arming, so a bare
         `ret` over it in the guest puts the click back on the unseeded
         two-pass redraw and the same clicks must draw the same screen
  leg D  the REFUSAL, which is the load-bearing one. wd_onclick clears
         [wd_ckok] after erasing a selection, whose rows the pair says nothing
         about - so clicking away from a selection must still full-repaint,
         and must leave the selection's rows clean
  leg E  A DRAG STEP IS A CARET MOVE TOO (SPEC.md 27.8.2.1): the end that
         moves is the caret's and the anchor stands still, so wd_dragsel arms
         the same pair. Two rows per step, and the arming has to be
         SELF-SUSTAINING across the steps of one gesture - the walk re-banks
         the checkpoint on its way past the caret, which is the whole reason
         [wd_ckok] may stay set. The step after the first is the one that
         proves it
  leg F  ...and the selection a drag leaves is right to the pixel
  leg H  ONE ROW LETTERED, NOT TWO (SPEC.md 27.19). The caret is out of the
         row signature, so the row it LEFT no longer changes and is not
         drawn - the overlay erases the bar there instead. The DIRTY RANGE
         is the whole of it: [wd_dr0] must equal [wd_dr1]. Not a count of
         wd_rflush calls - it is entered once per row WALKED and decides
         inside, which is leg A's mistake in a second costume - and legs
         B/D/F are what say the screen is still right
  leg I  ONE ROW IS THE WHOLE ANSWER (SPEC.md 27.19.9), for the two callers
         wd_clickcm cannot pair. A KEYSTROKE has no pair at all and walked
         the caret's row and the one above it; and a click whose DEPARTING
         row is outside the view armed a pair with a negative row in it -
         [wd_ckpr] is signed - which wd_redraw's bound then refused, so the
         walk ran to the bottom of the view. 24 rows for a click that moved
         one caret. Both must be ONE row now
  leg G  WHAT IS UNDER THE BAR IS BANKED, and banked RIGHT (SPEC.md 27.17).
         wd_curbank copies the caret's byte column out of the 1bpp band Word
         composed, before the bar goes down - so the assertion is the glass
         against the bank: the eight pixels the bar stands in must equal the
         banked byte with the caret's own bit cleared, every row. Ink is a
         CLEAR bit and paper is 0xFF, so a bank that is inverted, off by a
         column, off by a row or reading a stale band all fail differently and
         all fail. The bank is PIXELS (OSAPI_GFX_SAVE of the byte column the
         bar stands in), so it is asserted on BOTH faces and on a STYLED cell
         - an earlier build banked the CHARACTER and re-lettered it to erase,
         which made a bold cell a refusal, and a style is not a reason to
         cache less
"""
import os, sys, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty as M
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
# the ribbon's Font combo, so leg G can put the document on a CHOSEN FACE -
# the band path only exists there, and WELCOME.DOC opens on the kernel's 8x8
# cell. Same numbers as tests/wdcombo.py, from the same place in word.asm.
WD_MENU_H = 14
WD_RB_FBX, WD_RB_FBW = 56, 96
OS88UI_DRIH = 10
DR_N, DR_OPEN, DR_HOT, DR_TOP = 10, 16, 17, 22
GUEST_HZ = 4772728.0
ms = lambda c: c / GUEST_HZ * 1000.0
FAIL = []


def check(name, ok, detail=""):
    print("   %-52s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
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
ap.add_argument("--machine", default="os8088_5150_cga_gla")
a = ap.parse_args()
syms, image = pkg_syms()
DISK = "build/wdclickgate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    mo = Mouse(marty=m)                 # launch() has already settled the boot
    print("== Word: a click is a caret move (SPEC.md 27.4.7) ==")
    dispcp.open_drive(m, mo, S, M.settle, "B")
    w = dispcp.win_list(m, S)[-1]; dx, dy = dispcp.win_rect(m, S, w)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")

    def find_seg():
        raw = m.read(S("inst_tab"), 32*12)
        for i in range(12):
            b = i*32
            if raw[b] == 1 and (raw[b+2] & 0x80):
                c = u16(raw, b+6)
                if m.read(c*16+syms["wd_mact"], 48) == image[syms["wd_mact"]:syms["wd_mact"]+48]:
                    return c
        return None
    try:                                # the package up, on guest state
        M.until(m, lambda _m: find_seg() is not None, "Word to be running",
                poll=0.3, limit=60)
    except M.MartyError:
        pass
    seg = find_seg()
    if seg is None:
        sys.exit("could not locate the running package (stale build/word.o88?)")
    base = seg*16; P = lambda n: base + syms[n]
    rw = lambda n: u16(m.read(P(n), 2)); rb = lambda n: m.read(P(n), 1)[0]

    # ---- waiting on the guest, not on a clock ------------------------------
    # Every gesture below waits for the UI to be FINISHED with it rather than
    # for a fixed pause: the input is out of both queues (the BIOS keyboard
    # ring and the kernel's event ring) and nobody holds the gfx lock, which
    # ui_task takes around every handler it dispatches.
    KBUF = 0x41A                        # 0040:001A/001C - the BIOS ring's head, tail
    kbhead = lambda: m.read(KBUF, 2)
    evtail = lambda: m.read(S("evq_tail"), 1)[0]
    cyc = lambda: int(m.status()["cycles"])

    def ui_idle():
        kb = m.read(KBUF, 4)
        return (kb[0:2] == kb[2:4] and m.read(S("evq_count"), 1)[0] == 0
                and m.read(S("gfx_lock_flag"), 1)[0] == 0)

    def done(arrived=None, what="the UI to finish with the gesture", tr=None,
             idle=True):
        """`arrived()` true (the gesture reached the guest) and the UI idle -
        twice, a twentieth of a guest second apart, because ui_task pops an
        event a few instructions before it takes the lock for it."""
        wait = ((lambda c, w: tr.until(c, w, 30)) if tr is not None else
                (lambda c, w: M.until(m, lambda _m: c(), w, poll=0.05, limit=30)))
        if tr is None:
            m.run()
        if not idle:                    # ...only that it was DISPATCHED: a text
            wait(lambda: arrived() and     # click's handler holds the lock
                 m.read(S("evq_count"), 1)[0] == 0, what)   # until the release
            return
        wait(lambda: (arrived is None or arrived()) and ui_idle(), what)
        c0 = cyc()
        wait(lambda: cyc() - c0 >= M.GUEST_HZ / 20 and ui_idle(), what)

    def key(k):
        m.run(); h = kbhead(); m.key(k)
        done(lambda: kbhead() != h, "the %s key to be handled" % k)

    def press():
        """the button down, and dispatched: its handler is running"""
        t = evtail(); m.mouse(l=True)
        done(lambda: evtail() != t, "the press to be dispatched", idle=False)

    def release():
        """the button up, and the handler it ends finished"""
        t = evtail(); m.mouse(l=False)
        done(lambda: evtail() != t, "the button release to be handled")

    def sb_click(x, y):
        """a scroll-bar click. The press is HANDLED before the release is
        sent, so wd_onclick's OSAPI_EVQ_PENDING sees no click behind it and
        draws the page itself (SPEC.md 27.7.8)."""
        m.run(); mo.to(x, y); done()
        t = evtail(); m.mouse(l=True)
        done(lambda: evtail() != t, "the scroll-bar press to be handled")
        release()

    def still():
        """before a capture: the UI idle, Word's height count finished (it
        redraws the scroll bar), then the screen still"""
        m.run()
        M.until(m, lambda _m: rb("wd_hdirty") == 0 and ui_idle(),
                "Word to finish its height count", poll=0.1, limit=60)
        M.settle(m, quiet=0.3)

    def tracked(x, y):
        """wd_dragsel has taken the pointer at (x, y) - [wd_lmx]/[wd_lmy] are
        its last sample - and finished the pass that sample drew: it is back
        at the top of its loop"""
        m.bp_exec(P("wd_dragsel.pass"))
        for _ in range(60):
            m.run()
            if not m.wait_stop(30):
                break
            if (rw("wd_lmx"), rw("wd_lmy")) == (x, y):
                break
        m.bp_exec(); m.run()

    # the document: wd_onwake clears [wd_argp] and then reads and draws it
    # under the lock, so the lock coming free after that is the load done
    done(lambda: rb("wd_argp") == 0 and rw("wd_len") > 0, "WELCOME.DOC to open")

    def settle_height():
        for _ in range(400):
            if rb("wd_hdirty") == 0:
                return True
            m.advance(cycles=2_000_000)
        return False

    settle_height()
    tx, ty = rw("wd_tx"), rw("wd_ty")
    cl, sbr, bot = rw("wd_cl"), rw("wd_sbr"), rw("wd_bot")
    sbb, vrows = rw("wd_sbb"), rw("wd_vrows")
    box = (cl, ty, sbr, bot)
    print("   vrows=%d len=%d hasfmt=%d hastab=%d pxon=%d"
          % (vrows, rw("wd_len"), rb("wd_hasfmt"), rb("wd_hastab"), rb("wd_pxon")))
    LAST = min(vrows, 5) - 1
    sbx = sbr - 7
    ydn = (ty+sbb)//2 + (sbb-ty)//4
    yup = ty + (sbb-ty)//4

    def ryb(r):
        """the banked GLYPH y of visible row r - the truth under formats, and
        a formatted document is the only kind this gate opens"""
        return u16(m.read(P("wd_ryb") + 2*r, 2))

    def click_at(r, col=20):
        m.run(); mo.to(tx + col*8, ryb(r) + 2); done()
        press(); release()

    def click_walks(r, col=20):
        """ROWS the wd_redraw of ONE click walks, and what it costs."""
        settle_height()
        m.run(); mo.to(tx + col*8, ryb(r) + 2); done()
        m.bp_exec(P("wd_redraw")); m.mouse(l=True)
        if not m.wait_stop(30):
            m.bp_exec(); m.mouse(l=False); m.run(); return None, None, None
        c0 = m.status()["cycles"]; rg = m.regs()
        ret = base + u16(m.read(rg["ss"]*16 + rg["sp"], 2))
        m.bp_exec(P("wd_nextrow"), ret)
        n = 0
        for _ in range(400):
            m.run()
            if not m.wait_stop(30):
                break
            pc = (m.regs()["cs"] << 4) + m.regs()["ip"]
            if pc == ret:
                break
            n += 1
        c = m.status()["cycles"] - c0
        kind = rb("wd_ekind")
        t = evtail(); m.bp_exec(); m.mouse(l=False); m.run()
        done(lambda: evtail() != t, "the click's release to be handled")
        return n, c, kind

    def page_round_trip(top0):
        sb_click(sbx, ydn)
        for _ in range(12):
            if rw("wd_top") <= top0:
                break
            sb_click(sbx, yup)
        mo.to(4, 4); still()
        return rw("wd_top") == top0

    # ---- leg A: one walk, not two -----------------------------------------
    click_at(0, col=8)
    n1, c1, k1 = click_walks(LAST, col=12)
    check("A: a click across the view walks TWO rows", n1 is not None and n1 <= 2,
          "%s rows - it is still walking the span between them" % n1)
    check("A: ...and it is kind 4, a caret move", k1 == 4,
          "[wd_ekind] = %s" % k1)
    print("      one click across %d rows: %s rows walked, %.1f ms"
          % (LAST, n1, ms(c1 or 0)))

    # ---- leg B: pixels, against a repaint the fast path never touched ------
    CASES = ((" forwards", 0, LAST), ("backwards", LAST, 0),
             ("     far", 0, LAST), ("  in place", 2, 2))

    def pixels_match(name, r0, r1):
        click_at(r0, col=8)
        click_at(r1, col=24)
        mo.to(4, 4); still()
        moved = shot(m)
        top0 = rw("wd_top")
        if not page_round_trip(top0):
            check("B/%s: the view came back to the same top" % name, False, "top moved")
            return None
        rp = shot(m)
        a3, b3 = band(moved, box), band(rp, box)
        w3 = box[2] - box[0] + 1
        bad3 = [i for i in range(len(a3)) if a3[i] != b3[i]]
        if bad3:
            xs = [box[0] + i % w3 for i in bad3]
            ys = [box[1] + i // w3 for i in bad3]
            print("      %s diff bbox x %d..%d y %d..%d (%d px); lit in "
                  "MOVED %d, in REPAINT %d; curseen=%d curshown=%d cbok=%d "
                  "cherr=%d curx=%d cury=%d curseen=%d"
                  % (name, min(xs), max(xs), min(ys), max(ys), len(bad3),
                     sum(1 for i in bad3 if a3[i]),
                     sum(1 for i in bad3 if b3[i]),
                     rb("wd_curseen"), rb("wd_curshown"), rb("wd_cbok"),
                     rb("wd_cherr"), rw("wd_curx"), rw("wd_cury"),
                     rb("wd_curseen")))
        return len(bad3)

    for name, r0, r1 in CASES:
        d = pixels_match(name, r0, r1)
        if d is not None:
            check("B: a click %s equals a full repaint" % name, d == 0,
                  "%d differing pixels" % d)

    # ---- leg C: the A/B inside one boot ------------------------------------
    off = pixels_match("armed", 0, LAST)
    save = m.read(P("wd_clickcm"), 1)
    m.write(P("wd_clickcm"), b"\xC3")          # bare ret: the arming is gone
    n2, c2, k2 = click_walks(LAST, col=12)
    check("C: disarmed, the click walks the whole view again",
          n2 is not None and n2 > n1,
          "%s rows against the armed %s - wd_clickcm is not the whole "
          "arming" % (n2, n1))
    dis = pixels_match("disarmed", 0, LAST)
    m.write(P("wd_clickcm"), save)
    if off is not None and dis is not None:
        check("C: armed and disarmed draw the same screen", off == 0 and dis == 0,
              "armed %s, disarmed %s differing pixels" % (off, dis))
    print("      disarmed: %s rows walked, %.1f ms  (armed %s rows, %.1f)"
          % (n2, ms(c2 or 0), n1, ms(c1 or 0)))

    # ---- leg D: the refusal - a selection is not a caret move --------------
    click_at(0, col=4)
    m.run(); mo.to(tx + 4*8, ryb(0) + 2); done()
    press(); tracked(tx + 4*8, ryb(0) + 2)
    mo.to(tx + 28*8, ryb(LAST) + 2, l=True); tracked(tx + 28*8, ryb(LAST) + 2)
    release()
    check("D: the drag made a selection", rb("wd_selon") == 1,
          "[wd_selon] = %d" % rb("wd_selon"))
    n3, c3, k3 = click_walks(1, col=16)
    check("D: clicking away from a selection is NOT kind 4", k3 != 4,
          "[wd_ekind] = %s - the pair says nothing about the "
          "selection's rows" % k3)
    mo.to(4, 4); still()
    moved = shot(m)
    top0 = rw("wd_top")
    if page_round_trip(top0):
        d = sum(1 for p, q in zip(band(moved, box), band(shot(m), box)) if p != q)
        check("D: ...and the selection is gone to the pixel", d == 0,
              "%d differing pixels" % d)
    else:
        check("D: the view came back to the same top", False, "top moved")

    # ---- leg E: a DRAG step is a caret move too (SPEC.md 27.8.2.1) --------
    def drag_step_walks(dy):
        """One raw packet of dy inside a live drag: rows its redraw walks."""
        m.bp_exec(P("wd_redraw"))
        m.mouse(dy=dy, l=True)
        if not m.wait_stop(40):
            m.bp_exec(); m.run(); return None, None, "no redraw"
        rg = m.regs()
        ret = base + u16(m.read(rg["ss"]*16 + rg["sp"], 2))
        m.bp_exec(P("wd_nextrow"), ret)
        n = 0
        for _ in range(400):
            m.run()
            if not m.wait_stop(40): break
            pc = (m.regs()["cs"] << 4) + m.regs()["ip"]
            if pc == ret: break
            n += 1
        kind = rb("wd_ekind")
        m.bp_exec(); m.run()            # wd_redraw has returned: the loop is
        return n, kind                  # back at .pass with nothing owed

    click_at(0, col=4)
    settle_height()
    m.run(); mo.to(tx + 4*8, ryb(1) + 2); done()
    press(); tracked(tx + 4*8, ryb(1) + 2)
    mo.to(tx + 4*8 + 40, ryb(1) + 2, l=True); tracked(tx + 4*8 + 40, ryb(1) + 2)
    nA, kA = drag_step_walks(8)
    nB, kB = drag_step_walks(8)
    nC, kC = drag_step_walks(-8)
    release()
    check("E: a drag step walks at most TWO rows",
          nA is not None and nA <= 2, "%s rows on the first step" % nA)
    check("E: ...and the arming survives the NEXT step",
          nB is not None and nB <= 2 and nC is not None and nC <= 2,
          "%s then %s rows - the walk is not re-banking the "
          "checkpoint" % (nB, nC))
    check("E: ...as kind 4 throughout", (kA, kB, kC) == (4, 4, 4),
          "kinds %s" % (list((kA, kB, kC)),))

    # ---- leg F: and the selection it left is right to the pixel -----------
    check("F: the drag left a selection", rb("wd_selon") == 1,
          "[wd_selon] = %d" % rb("wd_selon"))
    mo.to(4, 4); still()
    moved = shot(m)
    top0 = rw("wd_top")
    if page_round_trip(top0):
        rp2 = shot(m)
        a2, b2 = band(moved, box), band(rp2, box)
        w2 = box[2] - box[0] + 1
        bad2 = [i for i in range(len(a2)) if a2[i] != b2[i]]
        d = len(bad2)
        if bad2:
            xs = [box[0] + i % w2 for i in bad2]
            ys = [box[1] + i // w2 for i in bad2]
            print("      F diff bbox x %d..%d y %d..%d ; lit-in-moved %d of %d"
                  % (min(xs), max(xs), min(ys), max(ys),
                     sum(1 for i in bad2 if a2[i]), d))
        check("F: ...and it equals a full repaint", d == 0,
              "%d differing pixels" % d)
    else:
        check("F: the view came back to the same top", False, "top moved")

    # ---- leg G: the bank is what is under the bar (SPEC.md 27.17) ---------
    # THE BAND PATH ONLY EXISTS IN A CHOSEN FACE. WELCOME.DOC opens on the
    # kernel's 8x8 cell, where [wd_pxon] is 0 and nothing is composed - so the
    # first run of this leg read a void bank and was RIGHT to. Pick item 1 of
    # the ribbon's Font list, which is whatever face this disk carries.
    # THE CELL FACE IS THE REFUSAL, and it is where this starts: WELCOME.DOC
    # opens on the kernel's 8x8, which composes no band at all.
    click_at(1, col=6)
    mo.to(4, 4)
    key("ArrowRight")
    check("G: a CELL face banks too", rb("wd_cbok") == 1,
          "[wd_cbok]=0 with [wd_pxon]=%d - the bank is PIXELS now, so the "
          "face it was drawn in cannot matter (SPEC.md 27.17)"
          % rb("wd_pxon"))
    key("ArrowLeft")

    Rf = base + syms["wd_dfont"]
    cl, ct = rw("wd_cl"), rw("wd_ct")
    fbx = cl + WD_RB_FBX + WD_RB_FBW // 2
    fboxtop = ct + WD_MENU_H + 2
    fopen = lambda: m.read(Rf + DR_OPEN, 1)[0]
    fhot = lambda: m.read(Rf + DR_HOT, 1)[0]
    # [wd_nfont] IS 0 UNTIL THE LIST IS FIRST OPENED - wd_fontscan walks
    # SYSTEM/FONTS on that gesture and not before - so the count is read AFTER
    # the open and not used to decide whether to make it.
    m.run(); mo.to(fbx, fboxtop + 6); done()
    mo._edge(True)
    M.until(m, lambda _m: fopen() == 1,
            "the Font list to come down", guest=40.0)       # a FONTS walk
    n = u16(m.read(Rf + DR_N, 2))
    top = u16(m.read(Rf + DR_TOP, 2))
    if n >= 2:
        mo.to(fbx, top + OS88UI_DRIH + 5, l=True)           # item 1: a face
        M.until(m, lambda _m: fhot() == 1, "the pointer to reach item 1",
                guest=30.0)
    else:
        mo.to(fbx, top + 5, l=True)                         # item 0: Pica
    mo._edge(False)
    M.until(m, lambda _m: fopen() == 0, "the face pick to complete",
            guest=40.0)
    done()                              # the pick's handler, face and reflow
    M.quiesce(m, lambda: (m.disk().get("reads"), rb("wd_pxon")), guest=0.5,
              what="the face load to finish")
    settle_height()
    check("G: the document is on a chosen face", rb("wd_pxon") == 1,
          "[wd_pxon] = %d, Font list had %d item(s) and [wd_nfont] is %d - "
          "the band path is not running, so leg G would pass vacuously"
          % (rb("wd_pxon"), n, rb("wd_nfont")))

    def screen_col(sh, x, y, rows):
        """the 8 pixels at x..x+7 for `rows` rows, as band bytes (1 = lit)."""
        w, _, px = sh
        out = []
        for r in range(rows):
            b = 0
            for i in range(8):
                if px[(y + r) * w + x + i]:
                    b |= 0x80 >> i
            out.append(b)
        return out

    # A CARET MOVE is the trigger, not a click: it redraws the caret's row and
    # nothing else, so .caret runs on a row whose band was just composed. A
    # click reaches the same place, but where it PUTS the caret under a
    # proportional face is a second question this leg has no business asking.
    click_at(1, col=6)
    mo.to(4, 4)
    key("ArrowRight")
    still(); settle_height()
    print("      caret at (%d,%d) row %d; bank ok=%d at (%d,%d) y2=%d"
          % (rw("wd_curx"), rw("wd_cury"), rw("wd_ckpr"), rb("wd_cbok"),
             rw("wd_cbx"), rw("wd_cby"), rw("wd_cbh")))
    cbok = rb("wd_cbok")
    check("G: the caret's column is banked", cbok == 1,
          "[wd_cbok] = 0 - pxon=%d curx=%d cury=%d cherr=%d"
          % (rb("wd_pxon"), rw("wd_curx"), rw("wd_cury"), rb("wd_cherr")))
    if cbok == 1:
        cbx, cby = rw("wd_cbx"), rw("wd_cby")
        cbh = rw("wd_cbh") - cby + 1        # [wd_cbh] is y2, not a count
        bank = list(m.read(P("wd_cbank"), cbh))
        # [wd_rcx] is reset per ROW, so after the redraw it names whatever row
        # was drawn LAST. The caret's own x is [wd_curx], which wd_ask banks.
        rcx = rw("wd_curx")
        check("G: ...and the bank is where the caret IS", (rcx & ~7) == cbx
              and rw("wd_cury") == cby,
              "bank at (%d,%d), caret at (%d,%d)"
              % (cbx, cby, rcx & ~7, rw("wd_cury")))
        bit = 0x80 >> (rcx & 7)
        got = screen_col(shot(m), cbx, cby, cbh)
        want = [b & ~bit & 0xFF for b in bank]
        bad = [(r, want[r], got[r]) for r in range(cbh) if want[r] != got[r]]
        check("G: ...and it IS what the bar stands on", not bad,
              "%d of %d rows differ, first %s (want/got %02X/%02X) at "
              "x=%d y=%d rcx=%d" % (len(bad), cbh, bad[0][0] if bad else -1,
                                    bad[0][1] if bad else 0,
                                    bad[0][2] if bad else 0, cbx, cby, rcx))
        check("G: ...and the bar is really drawn in that column",
              any((b & bit) for b in bank),
              "the bank says the caret's own pixel was already ink, so the "
              "assertion above is vacuous there")
        print("      bank: x=%d y=%d rows=%d, caret bit %02X" % (cbx, cby, cbh, bit))

    # A STYLE IS NOT A REFUSAL (SPEC.md 27.17.1). Row 0 of WELCOME.DOC is the
    # BOLD centred heading, and an earlier build - which banked the character
    # and re-lettered it - could not erase a caret there at all.
    click_at(0, col=4)
    mo.to(4, 4)
    key("ArrowRight")
    check("G: ...and a STYLED cell banks like any other",
          rb("wd_cbok") == 1 and rb("wd_cherr") == 0,
          "[wd_cbok]=%d [wd_cherr]=%d on the bold heading - the bank is "
          "PIXELS, so a style cannot matter"
          % (rb("wd_cbok"), rb("wd_cherr")))

    # the refusals, so an always-void bank cannot pass leg G vacuously
    key("ArrowRight")
    check("G: ...and it re-banks as the bar moves", rb("wd_cbok") == 1,
          "[wd_cbok] = 0 after a second move - the bank is armed once and "
          "not maintained")

    # ---- leg H: the overlay is the ONLY drawer (SPEC.md 27.19) ------------
    # wd_rflush no longer draws the bar at all, so there is no knob to A/B
    # against any more - the question is now the one the wave is FOR: a caret
    # move dirties ONE row, the one it arrives on, because the row it left is
    # erased by the overlay rather than lettered again.
    check("H: the overlay put a bar up", rb("wd_curshown") == 1,
          "[wd_curshown] = 0 - wd_curshow never ran or refused")
    settle_height()
    key("ArrowDown")
    dr0, dr1 = rw("wd_dr0"), rw("wd_dr1")
    check("H: a caret move dirties ONE row, not two",
          dr0 != 0xFFFF and dr0 == dr1,
          "dirty range %d..%d - the row the caret LEFT is in it, so the "
          "caret is still in its signature" % (dr0, dr1))
    print("      caret move dirtied rows %d..%d" % (dr0, dr1))
    mo.to(4, 4); done()

    # ---- leg I: one row, for a key and for a caret off the view ----------
    def key_walks(key):
        """ROWS the wd_redraw of ONE keystroke walks."""
        settle_height(); m.run()
        m.bp_exec(P("wd_redraw")); m.key(key)
        if not m.wait_stop(30):
            m.bp_exec(); m.run(); return None, None, "no redraw"
        c0 = m.status()["cycles"]; rg = m.regs()
        ret = base + u16(m.read(rg["ss"]*16 + rg["sp"], 2))
        m.bp_exec(P("wd_nextrow"), ret)
        n = 0
        for _ in range(400):
            m.run()
            if not m.wait_stop(30):
                break
            if (m.regs()["cs"] << 4) + m.regs()["ip"] == ret:
                break
            n += 1
        c = m.status()["cycles"] - c0
        why = ("kind=%d cherr=%d cbok=%d shown=%d selon=%d ckok=%d cur=%d "
               "ckpi=%d ckpr=%d mvbot=%d rowsok=%d rowsn=%d" % (
            rb("wd_ekind"), rb("wd_cherr"), rb("wd_cbok"), rb("wd_curshown"),
            rb("wd_selon"), rb("wd_ckok"), rw("wd_cur"), rw("wd_ckpi"),
            rw("wd_ckpr"), rw("wd_mvbot"), rb("wd_rowsok"), rw("wd_rowsn")))
        m.bp_exec(); m.run(); done()
        return n, c, why

    click_at(1, col=12)
    nk, ck, wk = key_walks("ArrowDown")
    # AT MOST two, not exactly one: a caret that lands exactly ON a row
    # boundary belongs to either row and wd_solorow refuses it, which is
    # wd_seedck's pair and is what the key cost before (SPEC.md 27.19.9).
    check("I: a keystroke walks at most TWO rows", nk is not None and nk <= 2,
          "%s rows (%s)" % (nk, wk))
    nk2, ck2, wk2 = key_walks("ArrowRight")
    print("      ArrowDown: %s rows, %.1f ms; ArrowRight: %s rows, %.1f ms"
          % (nk, ms(ck or 0), nk2, ms(ck2 or 0)))

    # ...and the click whose departing row is OFF the view. Page down until
    # the caret is above it, which is what makes [wd_ckpr] negative.
    top0 = rw("wd_top")
    for _ in range(6):
        sb_click(sbx, ydn)
    mo.to(4, 4); done()
    scrolled = rw("wd_top") > top0
    check("I: the view really scrolled past the caret (case, not assertion)",
          scrolled, "top %d -> %d" % (top0, rw("wd_top")))
    if scrolled:
        no, co, ko = click_walks(2, col=12)
        check("I: a click with the caret OFF the view walks ONE row", no == 1,
              "%s rows - [wd_ckpr] is signed and the pair it armed had a "
              "negative bound in it" % no)
        print("      off-view click: %s rows, %.1f ms" % (no, ms(co or 0)))



print()
if FAIL:
    print("FAILED: " + ", ".join(FAIL)); sys.exit(1)
print("ok"); sys.exit(0)
