#!/usr/bin/env python3
"""MOVING THE CARET LAYS THE NOTE OUT ONCE, NOT TWICE (SPEC.md 27.4.6).

    make worddisk && python3 tests/wdcaret.py

wd_redraw is two walks: pass 1 works out which rows stopped matching their
signatures, and pass 2 draws them. Measured on a cycle-accurate 5150 with
WELCOME.DOC, a Right arrow was 68.6 ms of which pass 1 was 23.5 - and pass 1
draws not one pixel.

A caret move needs neither half of what the split buys: nothing reflowed, so
no row changes height and no band has to be erased before it is lettered. And
with no fill in the way a row can be drawn the moment its signature says it
changed - which is at the row's END, where wd_rflush already runs.

THE ASSERTION IS PIXELS, and the trap this gate exists for is one level under
them: [wd_clip] gates the GLYPH STORE as well as the drawing, deliberately and
by the same three tests. Clipping the one pass to the dirty range therefore
composed no cells at all for a row whose signature was not yet known, and
wd_rflush's delta then found every cell changed and re-lettered the whole row.
The screen still read as text - 419 differing bits on a Right arrow.

  leg A  ONE wd_walk in the keystroke, not two. This is the change, and it is
         what fails on a build with the feature off
  leg B  Right, Left, Home, End and a Down/Up pair, each checked against a
         full repaint - a page down and back, which a formatted document
         always repaints in full (68.6's documented degrade)
  leg C  the A/B inside one boot: wd_1pok is the whole arming, so stc/ret over
         it in the guest turns the one pass back into two and the same keys
         must draw the same screen
  leg D  a Down that SCROLLS the view, which is the one ordering the collapse
         changes - wd_seecaret now runs after the drawing rather than before it
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
DISK = "build/wdcaretgate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m)
    print("== Word: a caret move lays the note out ONCE (SPEC.md 27.4.6) ==")
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

    sbx = sbr - 7
    ydn = (ty+sbb)//2 + (sbb-ty)//4
    yup = ty + (sbb-ty)//4

    def click_row(r, col=20):
        m.run(); mo.to(tx + col*8, ty + r*8 + 3); time.sleep(0.35)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.0)

    def page_round_trip(top0):
        mo.to(sbx, ydn); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
        for _ in range(12):
            if rw("wd_top") <= top0:
                break
            mo.to(sbx, yup); time.sleep(0.25)
            m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
        mo.to(4, 4); time.sleep(1.0); M.settle(m)
        return rw("wd_top") == top0

    def key_walks(key):
        """How many wd_walk calls one keystroke makes, and what it costs."""
        settle_height()
        m.bp_exec(P("wd_onkey")); m.key(key)
        if not m.wait_stop(30):
            m.bp_exec(); m.run(); return None, None
        c0 = m.status()["cycles"]; rg = m.regs()
        ret = base + u16(m.read(rg["ss"]*16 + rg["sp"], 2))
        m.bp_exec(P("wd_walk"), ret)
        n = 0
        for _ in range(30):
            m.run()
            if not m.wait_stop(30):
                break
            pc = (m.regs()["cs"] << 4) + m.regs()["ip"]
            if pc == ret:
                break
            n += 1
        c = m.status()["cycles"] - c0
        m.bp_exec(); m.run(); time.sleep(0.8)
        return n, c

    # ---- leg A: one walk, not two -----------------------------------------
    click_row(1, col=20)
    n1, c1 = key_walks("ArrowRight")
    check("A: a Right arrow makes ONE wd_walk", n1 == 1,
          "%s walks - the two-pass redraw is still running" % n1)
    m.key("ArrowLeft"); time.sleep(1.0)

    # ---- leg B: pixels, against a repaint the one pass never touched ------
    for name, keys, row in (("Right",  ["ArrowRight"]*4,             1),
                            ("Left",   ["ArrowLeft"]*4,              2),
                            ("End",    ["End", "Home"],              3),
                            ("R then L", ["ArrowRight"]*5 + ["ArrowLeft"]*5, 2),
                            ("Down/Up",["ArrowDown", "ArrowUp"],     1)):
        click_row(row, col=20)
        for k in keys:
            m.key(k); time.sleep(0.9)
        mo.to(4, 4); time.sleep(1.0); M.settle(m)
        moved = shot(m)
        top0 = rw("wd_top")
        if not page_round_trip(top0):
            check("B/%s: the view came back to the same top" % name, False, "top moved")
            continue
        rp = shot(m)
        d = sum(1 for p, q in zip(band(moved, box), band(rp, box)) if p != q)
        check("B: %-8s equals a full repaint" % name, d == 0, "%d differing pixels" % d)

    # ---- leg D: a caret move that SCROLLS ---------------------------------
    # wd_seecaret runs AFTER the drawing now, so a scroll it decides on lands
    # on rows this pass has already drawn. wd_scrollpaint's precondition is
    # that the tables describe the glass, and after one pass they do.
    click_row(vrows - 1, col=10)
    top_before = rw("wd_top")
    for _ in range(3):
        m.key("ArrowDown"); time.sleep(1.2)
    scrolled = rw("wd_top") != top_before
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    afterscroll = shot(m)
    topD = rw("wd_top")
    check("D: the Down actually scrolled the view (case, not assertion)",
          scrolled, "top stayed at %d" % top_before)
    if page_round_trip(topD):
        rpD = shot(m)
        dD = sum(1 for p, q in zip(band(afterscroll, box), band(rpD, box)) if p != q)
        check("D: a caret move that scrolled equals a full repaint", dD == 0,
              "%d differing pixels" % dD)
    else:
        check("D: the view came back to the same top", False, "top moved")

    # ---- leg C: the A/B, inside one boot ----------------------------------
    click_row(1, col=20)
    for _ in range(4):
        m.key("ArrowRight"); time.sleep(0.9)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    one = shot(m)
    for _ in range(4):
        m.key("ArrowLeft"); time.sleep(0.9)
    time.sleep(0.6); M.settle(m)
    click_row(1, col=20)
    non, con = key_walks("ArrowRight")
    m.key("ArrowLeft"); time.sleep(1.0)

    keep = m.read(P("wd_1pok"), 2)
    m.write(P("wd_1pok"), bytes([0xF9, 0xC3]))       # stc; ret - refuse
    click_row(1, col=20)
    ntwo, ctwo = key_walks("ArrowRight")
    m.key("ArrowLeft"); time.sleep(1.0)
    click_row(1, col=20)
    for _ in range(4):
        m.key("ArrowRight"); time.sleep(0.9)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    two = shot(m)
    m.write(P("wd_1pok"), keep)

    check("C: the A/B arm really is two passes (case, not assertion)",
          ntwo == 2, "%s walks with wd_1pok refusing" % ntwo)

    # ...and while the two-pass form is available, put IT against the same
    # reference. This asserts nothing - a defect in the code being replaced is
    # not a requirement on the code replacing it - but it is the finding a
    # ten-scenario A/B turned up and could not attribute: End/Home and a
    # Right/Left pair came out 6 and 15 bits apart between the arms, and it is
    # the TWO-PASS arm that disagrees with a repaint.
    m.write(P("wd_1pok"), bytes([0xF9, 0xC3]))
    click_row(3, col=20)
    for k in ("End", "Home"):
        m.key(k); time.sleep(0.9)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    twoEH = shot(m)
    topEH = rw("wd_top")
    if page_round_trip(topEH):
        d2 = sum(1 for p, q in zip(band(twoEH, box), band(shot(m), box)) if p != q)
        print("   (End/Home in TWO passes against a full repaint: %d differing "
              "pixels - the one-pass leg above reads 0)" % d2)
    m.write(P("wd_1pok"), keep)
    dC = sum(1 for p, q in zip(band(one, box), band(two, box)) if p != q)
    check("C: one pass and two draw the SAME screen", dC == 0,
          "%d differing pixels" % dC)
    if con and ctwo:
        print("   Right: %.1f ms in one pass against %.1f ms in two (%.2fx)"
              % (ms(con), ms(ctwo), ctwo / float(con)))
        check("C: one pass is cheaper than two", con < ctwo * 0.85,
              "%d cy against %d" % (con, ctwo))
    else:
        check("C: both arms were timed", False, "a bracket did not return")

print()
print("wdcaret: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
sys.exit(1 if FAIL else 0)
