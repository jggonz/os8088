#!/usr/bin/env python3
"""PRESSING ENTER PUSHES THE NOTE DOWN INSTEAD OF DRAWING IT AGAIN (SPEC.md 27.4.5).

    make worddisk && python3 tests/wdenter.py

An Enter was the one edit at the caret with no fast path at all: [wd_fast]
stayed 0, so wd_redraw got no seed, no bound and no early-out, and it laid the
whole view out twice - once to find what changed and once to draw it. Measured
on a cycle-accurate 5150 with WELCOME.DOC, caret on row 1: 448.2 ms, of which
165 was a pass that draws nothing (PERFORMANCE.md Set 117.3).

wd_eoutck already knew how to say "the note below reconverged". A split
reconverges ONE ROW DOWN, so the same compare against the entry ABOVE answers
it - and then the rows below are what they were, one row lower, so their
pixels are a gfx_scroll and not a repaint.

THE ASSERTION IS PIXELS, because the failure mode is silence. The first build
scrolled the band to [wd_bot] and left four scanlines of the last row's glyphs
standing below it - a picture that still reads as text.

  leg A  the push FIRES at all - otherwise every leg below is vacuous and
         would pass on a build with the feature turned off
  leg B  Enter mid-paragraph, then page down and back. A formatted document
         ALWAYS full-repaints on a page up (68.6's documented degrade), so the
         screen that comes back is drawn from the model with no push anywhere
         in it - and it must equal the screen the push drew, to the pixel.
         THE BAND INCLUDES THE SLIVER below the last whole row, which is where
         the first build was wrong
  leg C  backspace the ¶ away and require the document BYTES back, both claims
  leg D  three Enters in a row, same pixel identity: the second and third find
         a table the first one shifted
  leg E  an Enter on the LAST visible row, which makes the caret-follow scroll
         the view. The push has repaired the tables for a layout the glass has
         not been given, so wd_redraw must refuse the blit and repaint - the
         one place this change can corrupt a screen rather than slow it down
  leg F  the A/B: the same keystroke with wd_nlband patched to refuse, which
         is the whole feature turned off inside one boot. Same pixels, and the
         cycles say the push is doing something
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


def check(name, ok, detail=""):
    print("   %-52s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
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
DISK = "build/wdentergate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m)
    print("== Word: an Enter pushes the note down (SPEC.md 27.4.5) ==")
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

    for _ in range(400):                       # the height count first, so
        if rb("wd_hdirty") == 0:               # [wd_drows] stops moving under
            break                              # the captures
        m.advance(cycles=2_000_000)

    tx, ty = rw("wd_tx"), rw("wd_ty")
    cl, sbr, bot = rw("wd_cl"), rw("wd_sbr"), rw("wd_bot")
    sbb, vrows = rw("wd_sbb"), rw("wd_vrows")
    box = (cl, ty, sbr, bot)                   # ...INCLUDING the sliver below
    ln0 = rw("wd_len")                         # the last whole row
    dseg, cseg = rw("wd_dseg"), rw("wd_cseg")
    txt0 = m.read(dseg*16, ln0); chp0 = m.read(cseg*16, ln0)
    print("   vrows=%d len=%d hasfmt=%d hastab=%d pxon=%d ty=%d bot=%d"
          % (vrows, ln0, rb("wd_hasfmt"), rb("wd_hastab"), rb("wd_pxon"), ty, bot))

    sbx = sbr - 7
    ydn = (ty+sbb)//2 + (sbb-ty)//4
    yup = ty + (sbb-ty)//4

    def click_row(r, col=20):
        m.run(); mo.to(tx + col*8, ty + r*8 + 3); time.sleep(0.35)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.0)

    def page_round_trip(top0):
        """Page down and back, which a formatted note repaints in full."""
        mo.to(sbx, ydn); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
        for _ in range(12):
            if rw("wd_top") <= top0:
                break
            mo.to(sbx, yup); time.sleep(0.25)
            m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
        mo.to(4, 4); time.sleep(1.0); M.settle(m)
        return rw("wd_top") == top0

    def settle_height():
        """Wait out the background row count before timing anything.

        [wd_hdirty] is set by every edit and cleared only by a walk that
        reaches the note's end (SPEC.md 27.6), so after any editing the
        chunked counter is running on its own wake. Its cycles are the
        MACHINE's, and a bracket between two breakpoints counts every cycle
        the guest spends inside it - including another task's. Timing a
        keystroke over a busy counter reads 412 ms for a 133 ms keystroke and
        reads it the same in both arms of an A/B, which is exactly how a real
        difference disappears.
        """
        for _ in range(400):
            if rb("wd_hdirty") == 0:
                return True
            m.advance(cycles=2_000_000)
        return False

    def enter_cost(key="Enter"):
        """One keystroke's whole wd_onkey, in cycles, on a QUIET machine."""
        settle_height()
        m.bp_exec(P("wd_onkey")); m.key(key)
        if not m.wait_stop(30):
            m.bp_exec(); m.run(); return None
        c0 = m.status()["cycles"]; rg = m.regs()
        ret = base + u16(m.read(rg["ss"]*16 + rg["sp"], 2))
        m.bp_exec(ret); m.run()
        if not m.wait_stop(30):
            m.bp_exec(); m.run(); return None
        c = m.status()["cycles"] - c0
        m.bp_exec(); m.run(); time.sleep(0.8)
        return c

    # ---- leg A: the push fires at all -------------------------------------
    click_row(1, col=20)
    m.bp_exec(base + syms["wd_nlpush.d1"])     # reached only past the scroll
    m.key("Enter")
    fired = m.wait_stop(25)
    m.bp_exec()
    if fired: m.run()
    time.sleep(1.2)
    check("A: the Enter push FIRES on a mid-line Enter", bool(fired),
          "wd_nlpush.d1 never reached - the note below is still redrawn")
    m.key("Backspace"); time.sleep(1.2)

    # ---- leg B: pixels, against a repaint the push never touched ----------
    click_row(1, col=20)
    m.key("Enter"); time.sleep(1.6)
    nl = rw("wd_nlrow")
    cur_split = rw("wd_cur")
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    pushed = shot(m)
    top0 = rw("wd_top")
    check("B: the push took this Enter (case, not assertion)", nl != 0xFFFF,
          "wd_nlrow = 0x%04X" % nl)
    check("the view came back to the same top (case arranged)",
          page_round_trip(top0), "top moved")
    repainted = shot(m)
    d = sum(1 for p, q in zip(band(pushed, box), band(repainted, box)) if p != q)
    check("B: the pushed screen equals a full repaint", d == 0,
          "%d differing pixels" % d)

    # ---- leg C: the bytes come back ---------------------------------------
    m.write(P("wd_cur"), bytes([cur_split & 0xFF, (cur_split >> 8) & 0xFF]))
    time.sleep(0.3)
    m.key("Backspace"); time.sleep(1.2)
    ln2 = rw("wd_len")
    t2 = m.read(rw("wd_dseg")*16, ln2); c2 = m.read(rw("wd_cseg")*16, ln2)
    bad = next((i for i in range(min(len(t2), len(txt0))) if t2[i] != txt0[i]), None)
    check("C: the document BYTES are back, both claims",
          ln2 == ln0 and t2 == txt0 and c2 == chp0,
          "len %d (want %d), first bad byte %s" % (ln2, ln0, bad))

    # ---- leg D: three in a row --------------------------------------------
    # The second and third find a wd_rows/wd_sig/wd_ryb the first one shifted,
    # which is the repair reading its own output.
    click_row(1, col=20)
    fired3 = 0
    for _ in range(3):
        m.key("Enter"); time.sleep(1.5)
        if rw("wd_nlrow") != 0xFFFF:
            fired3 += 1
    cur3 = rw("wd_cur")
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    three = shot(m)
    top3 = rw("wd_top")
    check("D: all three Enters were pushed (case, not assertion)", fired3 == 3,
          "%d of 3" % fired3)
    check("the view came back to the same top (case arranged)",
          page_round_trip(top3), "top moved")
    rp3 = shot(m)
    d3 = sum(1 for p, q in zip(band(three, box), band(rp3, box)) if p != q)
    check("D: three pushed Enters equal a full repaint", d3 == 0,
          "%d differing pixels" % d3)
    m.write(P("wd_cur"), bytes([cur3 & 0xFF, (cur3 >> 8) & 0xFF]))
    time.sleep(0.3)
    for _ in range(3):
        m.key("Backspace"); time.sleep(1.0)

    # ---- leg E: an Enter that makes the view SCROLL ------------------------
    # The push repairs the tables for a layout the glass has not been given,
    # so a caret-follow scroll after it must refuse the blit and repaint in
    # full. Without that guard wd_scrollpaint shifts a lie and the screen is
    # wrong rather than slow.
    click_row(vrows - 1, col=10)
    top_before = rw("wd_top")
    m.key("Enter"); time.sleep(2.0)
    scrolled = rw("wd_top") != top_before
    cur_e = rw("wd_cur")
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    lastrow = shot(m)
    topE = rw("wd_top")
    print("   last-row Enter: top %d -> %d, nlrow=0x%04X"
          % (top_before, topE, rw("wd_nlrow")))
    check("the view came back to the same top (case arranged)",
          page_round_trip(topE), "top moved")
    rpE = shot(m)
    dE = sum(1 for p, q in zip(band(lastrow, box), band(rpE, box)) if p != q)
    check("E: an Enter on the last visible row equals a full repaint", dE == 0,
          "%d differing pixels%s" % (dE, " (the view scrolled)" if scrolled else ""))
    m.write(P("wd_cur"), bytes([cur_e & 0xFF, (cur_e >> 8) & 0xFF]))
    time.sleep(0.3)
    m.key("Backspace"); time.sleep(1.2)

    # ---- leg F: the A/B, inside one boot ----------------------------------
    # wd_nlband is the whole arming: refusing there leaves [wd_eorow] 0, so
    # the split test never fires and the keystroke is exactly what it used to
    # be. Pixels first, because a fast wrong answer is the failure that matters.
    click_row(1, col=20)
    con = enter_cost()
    con_nl = rw("wd_nlrow")
    check("F: the timed Enter was pushed (case, not assertion)",
          con_nl != 0xFFFF, "wd_nlrow = 0x%04X" % con_nl)
    m.key("Backspace"); time.sleep(1.2)
    click_row(1, col=20)
    m.key("Enter"); time.sleep(1.6)
    curF = rw("wd_cur")
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    with_push = shot(m)
    m.write(P("wd_cur"), bytes([curF & 0xFF, (curF >> 8) & 0xFF]))
    time.sleep(0.3)
    m.key("Backspace"); time.sleep(1.4)

    keep = m.read(P("wd_nlband"), 2)
    m.write(P("wd_nlband"), bytes([0xF9, 0xC3]))       # stc; ret - refuse
    click_row(1, col=20)
    coff = enter_cost()
    check("F: the timed A/B Enter was NOT pushed (case, not assertion)",
          rw("wd_nlrow") == 0xFFFF, "wd_nlrow = 0x%04X" % rw("wd_nlrow"))
    m.key("Backspace"); time.sleep(1.2)
    click_row(1, col=20)
    m.key("Enter"); time.sleep(1.6)
    off_nl = rw("wd_nlrow")
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    without = shot(m)
    m.write(P("wd_nlband"), keep)

    check("F: the A/B arm really has the push off (case, not assertion)",
          off_nl == 0xFFFF, "wd_nlrow = 0x%04X with wd_nlband refusing" % off_nl)
    dF = sum(1 for p, q in zip(band(with_push, box), band(without, box)) if p != q)
    check("F: pushed and unpushed draw the SAME screen", dF == 0,
          "%d differing pixels" % dF)
    if con and coff:
        print("   Enter: %.1f ms pushed against %.1f ms unpushed (%.2fx)"
              % (ms(con), ms(coff), coff / float(con)))
        check("F: the push is cheaper than the reflow it replaces",
              con < coff * 0.75,
              "%d cy against %d - the push is not paying for itself" % (con, coff))
    else:
        check("F: both arms were timed", False, "a bracket did not return")

print()
print("wdenter: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
sys.exit(1 if FAIL else 0)
