#!/usr/bin/env python3
"""WORD'S SCROLL: the bar survives it, the pixels are right, and it BLITS.

    make worddisk && python3 tests/wdscroll.py

Three field reports, all about the scroll bar (SPEC.md 27.7.2, 68.6):

  A  the down arrow blanks part of the bar. wd_vshift cut its blit span from
     [wd_rgt], the last TEXT column, rounded UP to a byte column - and the
     bar's frame begins at [wd_rgt]+1, so on the shipped window it took SIX of
     the bar's fourteen columns. wd_scrollpaint then filled that strip white
     over the whole band height and wd_sbar redrew all sixteen calls of the
     bar at the end: Part 1's double-draw flash, once per click.

  B  a track click redraws the whole window, bar and grow box included, even
     though a REFUSED blit has drawn nothing and .scrolled is only reached
     when wd_sigsame agreed - so the bar on the glass was still right.

  C  a page is [wd_vfit] rows, which with formats is band/24 - 2 of the
     shipped window's 6. Two thirds is retained and could blit, but
     wd_scrollpaint lowered [wd_rowsn] to [wd_bd0] to bound its seed and
     nothing raised it once the walk had lettered the rest, so the FIRST page
     click blitted and every one after it refused on d > rowsn.

  G  a thumb DRAG clears the whole window and repaints it, toolbars and
     footer included. SB_RATE is 0 here, so the gesture commits once at the
     release and the view jumps further than [wd_vrows] - the blit refuses,
     and .fullpaint white-filled the whole content box and drew all four
     chrome strips again for a scroll that cannot have moved any of them
     (SPEC.md 68.2.4).

LEG B IS THE ONE WITH TEETH. Speed is worthless if the pixels are wrong, and
a stale banked y draws a row at the wrong height - which no timing assertion
would see. It pages DOWN through the document with the blit and then back UP,
which a formatted document always full-repaints (68.6's documented degrade),
and requires the screen to come back IDENTICAL. So the fast path is checked
against the slow one on the same document, in one run.
"""
import os, sys, time, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty as M
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
s16 = lambda v: v - 65536 if v >= 32768 else v
STEP_CYCLES = 25_000
SB_CELL = 10                    # apps/os88ui.inc: OS88UI_SBCELL, the arrow
                                # cell's height. The reference box is THAT and
                                # not the whole bar head: the rule sits at
                                # y1+10 and the track below it is where the
                                # THUMB travels, so a box any taller calls a
                                # legitimate thumb move a defect.
FAIL = []


def check(name, ok, detail=""):
    print("   %-50s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src="apps/word/word.asm", incs=("apps/", "apps/word/")):
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d,"p.asm"), os.path.join(d,"p.map")
        open(cp,"w").write(open(src).read()+"\n[map symbols %s]\n"%mp)
        subprocess.run(["nasm","-f","bin","-w+error"]+sum([["-I",i] for i in incs],[])
                       +["-o",os.path.join(d,"p.bin"),cp],check=True)
        out={}
        for L in open(mp):
            f=L.split()
            if len(f)==3 and all(c in "0123456789ABCDEF" for c in f[0]): out[f[2]]=int(f[0],16)
        return out, open(os.path.join(d,"p.bin"),"rb").read()


def shot(m):
    w, h, rows = m.vram()
    return w, h, bytes(b for r in rows for b in r)


def band(sh, box):
    w, _, px = sh
    x0, y0, x1, y1 = box
    return bytes(px[y*w + x] for y in range(y0, y1+1) for x in range(x0, x1+1))


def vram_cell(m, x1, x2, y1, y2):
    """The RAW framebuffer bits for columns x1..x2 of rows y1..y2, MASKED.

    CGA BY NAME, and read out of guest memory rather than through fbuf(): a
    rendered frame only changes once a video frame, so sampling it every
    STEP_CYCLES re-reads the SAME picture and a leg watching for a strip that
    is blanked and redrawn inside one frame sees nothing at all. That is a
    false green, and this gate had it - it passed with the fix backed out
    until this function replaced the frame read (docs/WRITING-TESTS.md 1).

    AND IT MASKS TO THE COLUMNS ASKED FOR, which the byte-granular version
    could not. Word's bar begins at [wd_rgt]+1 = 602 on the shipped window, so
    the byte holding its first columns ALSO holds text columns 600-601 - and
    a full repaint fills those legitimately. Rounding outward therefore
    reported one byte of honest drawing as a disturbed bar, while the bug it
    is looking for blanks 602-607 inside that same byte. Bits, not bytes.

    SPEC.md 39.3's two-bank layout, the arithmetic os88marty.vram uses.
    """
    fb = m.read(0xB8000, 0x4000)
    b1, b2 = x1 >> 3, x2 >> 3
    mask = bytearray(b2 - b1 + 1)
    for x in range(x1, x2 + 1):
        mask[(x >> 3) - b1] |= 0x80 >> (x & 7)
    out = bytearray()
    for y in range(y1, y2 + 1):
        off = (y % 2) * 0x2000 + (y // 2) * 80
        row = fb[off + b1:off + b2 + 1]
        out += bytes(v & k for v, k in zip(row, mask))
    return bytes(out)


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_cga_gla")
a = ap.parse_args()
syms, image = pkg_syms()
DISK = "build/wdscrollgate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m)
    print("== Word's scroll (SPEC.md 27.7.2) on %s ==" % a.machine)
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

    # THE HEIGHT COUNT MUST BE FINISHED FIRST. [wd_drows] is a lower bound
    # while the background walk is owed (SPEC.md 27.7.2.2/68.6), so the TOTAL
    # keeps moving between clicks - and wd_sbcheck answers a moved total with
    # the full sixteen-call draw, correctly, because only that can resize the
    # thumb. A leg that samples the bar while the count is running is watching
    # a legitimate redraw and calling it a defect.
    for _ in range(400):
        if rb("wd_hdirty") == 0:
            break
        m.advance(cycles=2_000_000)
    check("the height count is settled (so the total stops moving)",
          rb("wd_hdirty") == 0, "wd_hdirty still set after 800M cycles")
    print("   drows=%d after the count settled" % rw("wd_drows"))

    rgt, sbr = rw("wd_rgt"), rw("wd_sbr")
    ty, bot = rw("wd_ty"), rw("wd_bot")
    sbb = rw("wd_sbb")               # the bar's own bottom, CLEAR of the grow
                                     # box the kernel draws in the corner -
                                     # bot-4 is the grow box and clicking it
                                     # scrolls nothing at all
    vrows, vfit = rw("wd_vrows"), rw("wd_vfit")
    tx, rcols = rw("wd_tx"), rw("wd_rcols")
    barbox = (rgt+1, sbr, ty, ty+SB_CELL-1)   # the bar's UP-ARROW cell:
                                              # outside the thumb's travel, and
                                              # never drawn by a scroll at all,
                                              # so anything here is furniture
                                              # being erased
    sbx = sbr - 7
    print("   vrows=%d vfit=%d  tx=%d rcols=%d rgt=%d sbr=%d  hasfmt=%d"
          % (vrows, vfit, tx, rcols, rgt, sbr, rb("wd_hasfmt")))
    check("the document is formatted (the case under test)", rb("wd_hasfmt") == 1)
    check("a page retains rows (vfit < vrows)", vfit < vrows, "vfit=%d vrows=%d" % (vfit, vrows))

    # ---- leg A: the down arrow must never disturb the bar -----------------
    def watch(tag, y, steps=48):
        m.run(); mo.to(sbx, y); m.advance(frames=60)
        ref = vram_cell(m, *barbox)
        if vram_cell(m, *barbox) != ref:
            sys.exit("wdscroll: %s is not settled between two idle reads" % tag)
        before = rw("wd_top")
        m.pause(); m.mouse(0, 0, l=True); m.step(1)
        worst = seen = 0
        for _ in range(steps):
            m.advance(cycles=STEP_CYCLES)
            d = sum(1 for p, q in zip(ref, vram_cell(m, *barbox)) if p != q)
            worst = max(worst, d); seen += 1 if d else 0
        m.mouse(0, 0); m.step(1); m.run(); time.sleep(0.6)
        after = rw("wd_top")
        print("   %-22s %d samples, %d differ, worst %d byte(s); top %d -> %d"
              % (tag, steps, seen, worst, before, after))
        return seen, worst, before, after

    seen, worst, b4, af = watch("A down arrow", sbb-7)
    check("the down arrow scrolled (the case is arranged)", af != b4, "top %d->%d" % (b4, af))
    check("A: the bar is never blanked by the blit", seen == 0,
          "%d of 48 samples altered, worst %d byte(s)" % (seen, worst))

    seen2, worst2, b42, af2 = watch("B track below thumb", (ty+sbb)//2 + (sbb-ty)//4)
    check("the track click paged DOWN (the case is arranged)", af2 > b42,
          "top %d->%d" % (b42, af2))
    check("B: a track click leaves the bar on the screen", seen2 == 0,
          "%d of 48 samples altered, worst %d byte(s)" % (seen2, worst2))

    # ---- leg D: a page UP BLITS too, and still keeps the bar --------------
    # It did NOT, until SPEC.md 27.7.2.2: a formatted document gave up the
    # upward blit-scroll (68.6 - the entering rows are above the view and in
    # no bank), so every click above the thumb repainted the whole window,
    # menu bar and ruler included, at 622 ms against the down click's 251.
    # wd_upheight prices those rows instead, and the two halves of this leg
    # are what that claim means: no wd_paint, and the bar left alone.
    #
    # THE BAR HALF IS A BEHAVIOURAL ASSERTION, not a pixel one, and
    # deliberately. Either path redraws the bar's THUMB - os88ui_sbmove, three
    # calls - because the view really moved, and pinning a pixel box that
    # excludes the thumb's own travel while still covering the six columns the
    # bug blanked is a box this gate got wrong twice. What the fix claims is
    # exactly this: the whole-bar draw does not run, and the fill knows it.
    yup = ty + (sbb-ty)//4
    box = (rw("wd_cl"), ty, sbr, bot)

    def click(y, watch):
        m.bp_exec(P(watch))
        m.run(); mo.to(sbx, y); time.sleep(0.3)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False)
        hit = m.wait_stop(12)
        m.bp_exec()
        if hit: m.run()
        time.sleep(1.3)
        return bool(hit)

    def down_to(n):
        for _ in range(n):
            m.run(); mo.to(sbx, (ty+sbb)//2 + (sbb-ty)//4); time.sleep(0.25)
            m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.3)

    down_to(2)                       # get away from the top so UP can happen
    b43 = rw("wd_top")
    fullD = click(yup, "wd_paint")
    afD = rw("wd_top")
    check("the track click paged UP (the case is arranged)", afD < b43,
          "top %d->%d" % (b43, afD))
    check("D: a track click ABOVE the thumb blits", not fullD,
          "wd_paint ran: the whole window is being repainted again")
    down_to(2)
    b44 = rw("wd_top")
    barfull = click(yup, "wd_sbar")
    check("the second UP click paged too (case arranged)", rw("wd_top") < b44,
          "top %d->%d" % (b44, rw("wd_top")))
    check("D: an UP click does not redraw the bar WHOLE", not barfull,
          "wd_sbar ran, so the sixteen-call draw is back")

    # ---- leg E: the blit against the repaint it replaced, same view -------
    # wd_upheight is the whole arming, so stc/ret over it in the guest puts
    # the refusal back - which is both the A/B and the only thing that still
    # exercises [wd_sbkeep], leg D having been the refusal's old home.
    #
    # THE TARGET VIEW IS NOT THE TOP OF THE NOTE, and that is the whole leg.
    # Returning to top 0 passed this while the <8px SLIVER below the last
    # drawable row was being blitted into and never erased - because at top 0
    # the pixels the blit pushed into it happened to be white. Against a view
    # with text at the foot of the window it is 529 differing pixels
    # (SPEC.md 27.7.2.2).
    def up_to(target):
        for _ in range(14):
            if rw("wd_top") <= target: break
            m.run(); mo.to(sbx, yup); time.sleep(0.25)
            m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.3)
        mo.to(4, 4); time.sleep(1.2); M.settle(m)
        return rw("wd_top")

    up_to(0)
    down_to(2)
    topE = rw("wd_top")
    check("E: the target view is not the top of the note (case arranged)",
          topE > 0, "top is %d, so the sliver carries nothing" % topE)
    down_to(3)
    topE2 = up_to(topE)
    blitE = shot(m)

    keepU = m.read(P("wd_upheight"), 2)
    m.write(P("wd_upheight"), bytes([0xF9, 0xC3]))       # stc; ret - refuse
    down_to(3)
    barref = click(yup, "wd_sbar")
    topE3 = up_to(topE)
    repE = shot(m)
    m.write(P("wd_upheight"), keepU)

    check("E: both arms came back to the same view (case arranged)",
          topE2 == topE3, "%d against %d" % (topE2, topE3))
    dE = sum(1 for p, q in zip(band(blitE, box), band(repE, box)) if p != q)
    check("E: an UP-blitted view equals the repainted one", dE == 0,
          "%d differing pixels" % dE)
    check("E: the REFUSED blit still leaves the bar alone ([wd_sbkeep])",
          not barref, "wd_sbar ran on the refused path")

    # ---- leg C: consecutive page clicks must BLIT -------------------------
    def paged(y):
        m.bp_exec(P("wd_paint"))
        m.run(); mo.to(sbx, y); time.sleep(0.3)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False)
        hit = m.wait_stop(12)
        m.bp_exec()
        if hit: m.run()
        time.sleep(1.0)
        return bool(hit)

    ydn = (ty+sbb)//2 + (sbb-ty)//4
    for _ in range(6):               # back to the top, so page-down can page
        if rw("wd_top") <= 0: break
        m.run(); mo.to(sbx, yup); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.3)
    fulls = [paged(ydn) for _ in range(3)]
    print("   consecutive page-downs entering wd_paint (a FULL repaint): %s" % fulls)
    check("C: a repeated page click still blits", not any(fulls),
          "%d of 3 fell back to a full repaint" % sum(fulls))

    # ---- leg B: the pixels. Page down, page back up, require identity -----
    m.run(); mo.to(4, 4); time.sleep(1.2); M.settle(m)
    top0 = rw("wd_top")
    start = shot(m)
    for _ in range(3):
        mo.to(sbx, ydn); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.3)
    mid = rw("wd_top")
    # ...and back to the SAME view, driven by [wd_top] rather than by counting
    # clicks: the track auto-repeats while the button is held, so a click is
    # not reliably one page and a fixed count lands somewhere else entirely.
    for _ in range(12):
        if rw("wd_top") <= top0:
            break
        mo.to(sbx, yup); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.3)
    mo.to(4, 4); time.sleep(1.2); M.settle(m)
    backtop = rw("wd_top")
    end = shot(m)
    print("   round trip: top %d -> %d -> %d" % (top0, mid, backtop))
    check("the round trip moved and came back (case arranged)",
          mid > top0 and backtop == top0, "%d -> %d -> %d" % (top0, mid, backtop))
    # ...and WHICH of the two is wrong, since either could be: force a true
    # repaint of the same view and put both against it.
    keepB = m.read(P("wd_upheight"), 2)
    m.write(P("wd_upheight"), bytes([0xF9, 0xC3]))
    mo.to(sbx, ydn); time.sleep(0.25)
    m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.4)
    for _ in range(12):
        if rw("wd_top") <= backtop: break
        mo.to(sbx, yup); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.4)
    mo.to(4, 4); time.sleep(1.2); M.settle(m)
    ref = shot(m); reftop = rw("wd_top")
    m.write(P("wd_upheight"), keepB)
    ds = sum(1 for p, q in zip(band(start, box), band(ref, box)) if p != q)
    de = sum(1 for p, q in zip(band(end, box), band(ref, box)) if p != q)
    print("   against a FORCED repaint at top %d: start %d px, end %d px"
          % (reftop, ds, de))
    check("B: the round trip's END equals a forced repaint", de == 0,
          "%d differing pixels" % de)
    check("B: ...and so does its START", ds == 0,
          "%d differing pixels - what LED here is wrong, not the round trip" % ds)

    d = sum(1 for p, q in zip(band(start, box), band(end, box)) if p != q)
    if d:
        w0, _, _ = start
        x0, y0, x1, y1 = box
        for yy in range(y0, y1 + 1):
            n = sum(1 for xx in range(x0, x1 + 1)
                    if start[2][yy*w0+xx] != end[2][yy*w0+xx])
            if n:
                print("      y=%3d (row %d) %4d px" % (yy, (yy - ty) // 8, n))
    check("B: blit-scrolled pixels equal the repainted ones", d == 0,
          "%d differing pixels" % d)

    # ---- leg F: a page DOWN taken after an up-BLIT -----------------------
    # The up blit does not only have to draw the right screen, it has to leave
    # the tables describing it - and the walk that PRICES the entering rows
    # runs before wd_shiftrows, into exactly the wd_rows entries the shift
    # reads as its source. Suppressing wd_ryb was not enough and looked it:
    # the up-blit's own screen was perfect to the pixel, and the NEXT page
    # down drew three rows of the wrong text (SPEC.md 27.7.2.2).
    #
    # So this compares a page-down reached through an up BLIT against the same
    # one reached through an up REPAINT. It is the only leg that looks at what
    # a scroll leaves behind rather than at what it draws.
    def to_top_then_down(upblit):
        m.write(P("wd_upheight"), keepB if upblit else bytes([0xF9, 0xC3]))
        down_to(6)
        up_to(0)
        m.write(P("wd_upheight"), keepB)
        down_to(3)
        return shot(m), rw("wd_top")

    viaRep, tR = to_top_then_down(False)
    viaBlit, tB = to_top_then_down(True)
    check("F: both arms reached the same view (case arranged)", tR == tB,
          "%d against %d" % (tR, tB))
    dF = sum(1 for p, q in zip(band(viaRep, box), band(viaBlit, box)) if p != q)
    check("F: a page DOWN after an up-blit equals one after a repaint", dF == 0,
          "%d differing pixels: the up blit left the tables wrong" % dF)

    # ---- leg G: a thumb DRAG must not repaint the chrome ------------------
    # SB_RATE is 0 here (SPEC.md 13.10.5.4), so the whole gesture commits ONCE
    # at the release and the view jumps by however far the hand went - which
    # is nearly always more than [wd_vrows], so wd_scrollpaint retains nothing
    # and refuses.  A thumb drag IS the refused-blit repaint, every time, and
    # what that repaint used to do was white-fill the whole content box - menu
    # bar, ribbon, ruler and status strip included - and draw all four again.
    # None of them changed: a scroll moves the VIEW (SPEC.md 68.2.4).
    #
    # The reference is the SAME DRAG with wd_sigsame forced to refuse, which
    # is .full - the one path where the strips really may be in the wrong
    # place and [wd_chkeep] is cleared.  So the fast screen is checked against
    # a screen drawn the old way, in one boot, on the same view.
    winbox = (rw("wd_cl"), rw("wd_ct"),
              rw("wd_cl") + rw("wd_cw") - 1, rw("wd_ct") + rw("wd_ch") - 1)

    def thumb_drag(steps=6):
        mo.to(sbx, ty + 12); time.sleep(0.4)
        mo._edge(True); time.sleep(0.8)
        for k in range(1, steps + 1):
            mo.to(sbx, ty + 12 + k, l=True); time.sleep(0.7)
        mo._edge(False); time.sleep(1.4)

    up_to(0)
    nchrome = M.bp_count(m, P("wd_chrome"), thumb_drag,
                         quiet=4.0, first=25.0, limit=240.0)
    topG = rw("wd_top")
    check("G: the drag scrolled past a page (the case is arranged)",
          topG >= vrows, "top=%d, vrows=%d - the blit would not refuse" % (topG, vrows))
    check("G: a thumb drag redraws NO chrome", nchrome == 0,
          "wd_chrome ran %d time(s): the strips are being erased and drawn "
          "again for a scroll that cannot have moved them" % nchrome)
    mo.to(4, 4); time.sleep(1.2); M.settle(m)
    fastG = shot(m)

    keepS = m.read(P("wd_sigsame"), 2)
    up_to(0)
    m.write(P("wd_sigsame"), bytes([0xF9, 0xC3]))   # stc; ret - always .full
    thumb_drag()
    m.write(P("wd_sigsame"), keepS)
    topG2 = rw("wd_top")
    mo.to(4, 4); time.sleep(1.2); M.settle(m)
    slowG = shot(m)

    check("G: both arms reached the same view (case arranged)", topG == topG2,
          "%d against %d" % (topG, topG2))
    dG = sum(1 for p, q in zip(band(fastG, winbox), band(slowG, winbox)) if p != q)
    check("G: ...and the same pixels as the full repaint, chrome included",
          dG == 0, "%d differing pixels over the WHOLE window" % dG)

print()
print("wdscroll: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
sys.exit(1 if FAIL else 0)
