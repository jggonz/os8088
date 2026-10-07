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
  leg E  an Up that scrolls is a caret move too (SPEC.md 27.4.11): wd_redraw's
         first walk lays out the row the caret left, not the whole view, and
         the scrolled screen equals a full repaint
  leg F  a Down after a Down does not measure the caret again (SPEC.md
         27.4.13): the redraw that drew the first one stood on the caret, so
         the second is TWO walks and not three. The A/B is inside one boot -
         the same Downs from the same click, once with the bank and once with
         [wd_cxcur] poked stale before each key - and the carets must land on
         the same indices, keystroke for keystroke
"""
import os, sys, subprocess, tempfile, argparse, functools
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
            if len(f)==3 and all(c in "0123456789ABCDEF" for c in f[0]): out[f[2]]=int(f[1],16)
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
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    mo = Mouse(marty=m)                 # launch() has already settled the boot
    print("== Word: a caret move lays the note out ONCE (SPEC.md 27.4.6) ==")
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
                poll=0.5, limit=60)
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

    sbx = sbr - 7
    ydn = (ty+sbb)//2 + (sbb-ty)//4
    yup = ty + (sbb-ty)//4

    def click_row(r, col=20):
        m.run(); mo.to(tx + col*8, ty + r*8 + 3); done()
        press(); release()

    def page_round_trip(top0):
        sb_click(sbx, ydn)
        for _ in range(12):
            if rw("wd_top") <= top0:
                break
            sb_click(sbx, yup)
        mo.to(4, 4); still()
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
        m.bp_exec(); m.run(); done()
        return n, c

    # ---- leg A: one walk, not two -----------------------------------------
    click_row(1, col=20)
    n1, c1 = key_walks("ArrowRight")
    check("A: a Right arrow makes ONE wd_walk", n1 == 1,
          "%s walks - the two-pass redraw is still running" % n1)
    key("ArrowLeft")

    # ---- leg B: pixels, against a repaint the one pass never touched ------
    for name, keys, row in (("Right",  ["ArrowRight"]*4,             1),
                            ("Left",   ["ArrowLeft"]*4,              2),
                            ("End",    ["End", "Home"],              3),
                            ("R then L", ["ArrowRight"]*5 + ["ArrowLeft"]*5, 2),
                            ("Down/Up",["ArrowDown", "ArrowUp"],     1)):
        click_row(row, col=20)
        for k in keys:
            key(k)
        mo.to(4, 4); still()
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
        key("ArrowDown")
    scrolled = rw("wd_top") != top_before
    mo.to(4, 4); still()
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

    # ---- leg E: an Up that SCROLLS is a caret move too (SPEC.md 27.4.11) ---
    # wd_move left before arming kind 4 when the target row was ABOVE the
    # view, so pass 1 walked the whole view to learn nothing had moved - 450
    # ms of an 850 ms Up on a 5150. Count the rows wd_redraw's FIRST walk lays
    # out: the row the caret left is the bound now.
    sb_click(sbx, ydn)
    mo.to(4, 4); done()
    check("E: the view is paged down (case, not assertion)", rw("wd_top") > 0,
          "top %d" % rw("wd_top"))
    click_row(0, col=10); settle_height()
    top_e = rw("wd_top")
    NM = {}
    for nm in ("wd_redraw", "wd_walk", "wd_rstart", "wd_caretnet", "wd_seecaret"):
        a = P(nm)
        for kk in (a, "%X" % a, str(a)): NM[kk] = nm
    seqE = []
    with M.bp_trace(m, *[P(nm) for nm in ("wd_redraw", "wd_walk", "wd_rstart",
                                          "wd_caretnet", "wd_seecaret")],
                    on_hit=lambda mm, rec: seqE.append(NM.get(rec.get("name"))),
                    cap=400) as tr:
        h = kbhead(); m.key("ArrowUp")     # (the pump does the running)
        done(lambda: kbhead() != h, "the Up to be handled", tr=tr)
    names = [x for x in seqE if x]
    p1 = None
    if "wd_redraw" in names:
        i = names.index("wd_redraw")
        rest = names[i+1:]
        if "wd_walk" in rest:
            j = rest.index("wd_walk")
            k = j + 1
            while k < len(rest) and rest[k] == "wd_rstart":
                k += 1
            p1 = k - j - 1
    check("E: the Up actually scrolled the view (case, not assertion)",
          rw("wd_top") < top_e, "top stayed at %d" % top_e)
    check("E: an Up off the top row walks the row it left, not the view",
          p1 is not None and p1 <= 3,
          "wd_redraw's first walk laid out %s rows of a %d-row view" % (p1, vrows))
    print("      pass-1 rows on a scrolling Up: %s" % p1)
    mo.to(4, 4); still()
    afterE = shot(m); topE = rw("wd_top")
    if page_round_trip(topE):
        rpE = shot(m)
        dE = sum(1 for p, q in zip(band(afterE, box), band(rpE, box)) if p != q)
        check("E: an Up that scrolled equals a full repaint", dE == 0,
              "%d differing pixels" % dE)
    else:
        check("E: the view came back to the same top", False, "top moved")

    # ---- leg F: a run of Downs measures the caret once (SPEC.md 27.4.13) ---
    # Row 1 of the view, then five Downs: the first primes the bank (the
    # click's redraw is not guaranteed to have stood on the caret), and each
    # of the other four must walk exactly ONCE FEWER than the same Down with
    # the key poked stale - which is the reference, for the walk count and for
    # the indices the cache must match. A plain Down is 2 against 3.
    def downs(stale):
        page_round_trip(0) if rw("wd_top") else None
        click_row(1, col=20)
        key("ArrowDown")
        out = []
        for _ in range(4):
            if stale:
                m.write(P("wd_cxcur"), b"\xff\xff")
            t0 = rw("wd_top")
            n, c = key_walks("ArrowDown")
            out.append((n, c, rw("wd_cur"), rw("wd_currow"), rw("wd_top") != t0))
        return out
    hot, cold = downs(False), downs(True)
    print("      Down with the bank:   walks %s, cur %s"
          % ([x[0] for x in hot], [x[2] for x in hot]))
    print("      Down, key poked stale: walks %s, cur %s"
          % ([x[0] for x in cold], [x[2] for x in cold]))
    check("F: the stale arm really measures (case, not assertion)",
          all(x[0] and x[0] >= 3 for x in cold), "walks %s" % [x[0] for x in cold])
    # A Down that SCROLLS aims at the row past the glass, whose table entry
    # 27.7.12's stop may have left stale - so it measures, as it always did,
    # and walks the same count in both arms. Every other one saves a walk.
    check("F: a Down on the glass walks ONE fewer time; one that scrolls, "
          "the same", all(h[0] is not None and
                          h[0] == c[0] - (0 if c[4] else 1)
                          for h, c in zip(hot, cold))
          and any(not c[4] for c in cold),
          "walks %s against %s measured (scrolled %s)"
          % ([x[0] for x in hot], [x[0] for x in cold], [x[4] for x in cold]))
    check("F: ...and lands where the measured Down lands",
          [x[2:] for x in hot] == [x[2:] for x in cold],
          "cached (cur, row) %s, measured %s"
          % ([x[2:] for x in hot], [x[2:] for x in cold]))
    if all(x[1] for x in hot + cold):
        mh = sorted(ms(x[1]) for x in hot)[len(hot)//2]
        mc = sorted(ms(x[1]) for x in cold)[len(cold)//2]
        print("      Down: %.1f ms median with the bank, %.1f ms without" % (mh, mc))
        check("F: ...and is cheaper", mh < mc, "%.1f ms against %.1f" % (mh, mc))
    else:
        check("F: every Down was timed", False, "a bracket did not return")

    # ---- leg C: the A/B, inside one boot ----------------------------------
    click_row(1, col=20)
    for _ in range(4):
        key("ArrowRight")
    mo.to(4, 4); still()
    one = shot(m)
    for _ in range(4):
        key("ArrowLeft")
    click_row(1, col=20)
    non, con = key_walks("ArrowRight")
    key("ArrowLeft")

    keep = m.read(P("wd_1pok"), 2)
    m.write(P("wd_1pok"), bytes([0xF9, 0xC3]))       # stc; ret - refuse
    click_row(1, col=20)
    ntwo, ctwo = key_walks("ArrowRight")
    key("ArrowLeft")
    click_row(1, col=20)
    for _ in range(4):
        key("ArrowRight")
    mo.to(4, 4); still()
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
        key(k)
    mo.to(4, 4); still()
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
