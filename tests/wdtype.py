#!/usr/bin/env python3
"""TYPING IN THE MIDDLE OF A LINE STOPS AT THE END OF THE LINE (SPEC.md 27.4.3).

    make worddisk && python3 tests/wdtype.py

wd_walk used to lay out every row from the caret to the bottom of the view on
every keystroke, to be told nothing below had changed: 149.2 ms of a 205.6 ms
keystroke on WELCOME.DOC in the shipped window (PERFORMANCE.md Set 117.2).

wd_eoutck stops it. SPEC.md 27.4 says the start of a row is (index, row)
alone, so a row that begins exactly [wd_eodel] characters later than it did
before the edit holds the same characters, at the same pen and the same
height - and so does every row below it. The walk stops there and the indices
below are bumped, which is wd_append's repair applied one case wider.

THE ASSERTION IS PIXELS AND BYTES, because the failure mode is silence. An
early-out that fires when it should not leaves rows standing at the wrong
index: the screen still looks like text, the caret still blinks, and the
damage only shows when something re-lays the note out. So:

  leg A  the early-out is REACHED at all - otherwise every leg below is
         vacuous and would pass on a build with the feature compiled out
  leg B  type a burst mid-paragraph, then scroll a page down and back. A
         formatted document ALWAYS full-repaints on a page up (68.6's
         documented degrade), so the screen that comes back is drawn from the
         model with no early-out anywhere in it - and it must equal the screen
         the incremental keystrokes drew, to the pixel
  leg C  backspace the burst away and require the document BYTES back, both
         claims - the text and its CHP twin - which is what catches an index
         repair that went the wrong way
  leg D  type until the line WRAPS, which is the case the early-out must NOT
         fire on, and check the same pixel identity
"""
import os, sys, time, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty as M
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
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
DISK = "build/wdtypegate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m)
    print("== Word: a keystroke stops at the end of its line (SPEC.md 27.4.3) ==")
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

    # the height count first, so [wd_drows] stops moving under the captures
    for _ in range(400):
        if rb("wd_hdirty") == 0:
            break
        m.advance(cycles=2_000_000)

    tx, ty = rw("wd_tx"), rw("wd_ty")
    cl, sbr, bot = rw("wd_cl"), rw("wd_sbr"), rw("wd_bot")
    sbb, vrows = rw("wd_sbb"), rw("wd_vrows")
    box = (cl, ty, sbr, bot)
    ln0 = rw("wd_len")
    dseg, cseg = rw("wd_dseg"), rw("wd_cseg")
    txt0 = m.read(dseg*16, ln0); chp0 = m.read(cseg*16, ln0)
    print("   vrows=%d len=%d hasfmt=%d hastab=%d pxon=%d"
          % (vrows, ln0, rb("wd_hasfmt"), rb("wd_hastab"), rb("wd_pxon")))

    def click_row(r, col=20):
        m.run(); mo.to(tx + col*8, ty + r*8 + 3); time.sleep(0.35)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.0)

    # ---- leg A: the early-out is reached at all ---------------------------
    click_row(1)
    m.bp_exec(P("wd_eoutck"))
    m.key("KeyQ")
    reached = m.wait_stop(15)
    m.bp_exec()
    if reached: m.run()
    time.sleep(1.0)
    check("A: wd_eoutck is reached on a keystroke", bool(reached),
          "the early-out is never asked - every leg below is vacuous")
    m.key("Backspace"); time.sleep(1.2)

    # ---- leg B: pixels, against a repaint the early-out never touched -----
    click_row(1, col=20)
    for ch in ("KeyA", "KeyB", "KeyC", "KeyD", "KeyE"):
        m.key(ch); time.sleep(0.9)
    cur_typed = rw("wd_cur")         # exactly past the five, banked for leg C:
                                     # a second CLICK cannot be used to find it
                                     # again, because the text under the pointer
                                     # has moved by those five characters
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    typed = shot(m)
    top0 = rw("wd_top")
    sbx = sbr - 7
    ydn = (ty+sbb)//2 + (sbb-ty)//4
    yup = ty + (sbb-ty)//4
    mo.to(sbx, ydn); time.sleep(0.25)
    m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
    for _ in range(12):
        if rw("wd_top") <= top0:
            break
        mo.to(sbx, yup); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    check("the view came back to the same top (case arranged)",
          rw("wd_top") == top0, "%d -> %d" % (top0, rw("wd_top")))
    repainted = shot(m)
    d = sum(1 for p, q in zip(band(typed, box), band(repainted, box)) if p != q)
    check("B: incrementally typed pixels equal a full repaint", d == 0,
          "%d differing pixels" % d)

    # ---- leg C: the bytes come back ---------------------------------------
    m.write(P("wd_cur"), bytes([cur_typed & 0xFF, (cur_typed >> 8) & 0xFF]))
    time.sleep(0.3)
    for _ in range(5):
        m.key("Backspace"); time.sleep(0.9)
    ln2 = rw("wd_len")
    t2 = m.read(rw("wd_dseg")*16, ln2); c2 = m.read(rw("wd_cseg")*16, ln2)
    bad = next((i for i in range(min(len(t2), len(txt0))) if t2[i] != txt0[i]), None)
    check("C: the document BYTES are back, both claims",
          ln2 == ln0 and t2 == txt0 and c2 == chp0,
          "len %d (want %d), first bad byte %s" % (ln2, ln0, bad))

    # ---- leg D: a line that REFLOWS must not take the early-out ----------
    # THIS IS THE LEG THAT CATCHES THE DANGEROUS FAILURE - an early-out that
    # fires WITHOUT its index proof. It stayed green the first time because it
    # never arranged a reflow at all, so it now PROVES one happened before it
    # asserts anything: a row below the caret whose start index moved by
    # something other than the number of characters typed is a row the wrap
    # rule moved, which is exactly the case the proof must refuse.
    rows_before = [u16(m.read(P("wd_rows") + 2*i, 2)) for i in range(vrows)]
    # row 1 col 20 is known to carry text (leg B typed there). A run of
    # non-space characters longer than the row is wide cannot fit whatever the
    # margin: wd_wordfit refuses it and the wrap rule moves it down.
    click_row(1, col=20)
    caret_row = rw("wd_ckpr")
    N = 60
    for _ in range(N):
        m.key("KeyW"); time.sleep(0.8)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    rows_after = [u16(m.read(P("wd_rows") + 2*i, 2)) for i in range(vrows)]
    moved = [(i, rows_before[i], rows_after[i]) for i in range(caret_row + 1, vrows)
             if rows_after[i] - rows_before[i] != N]
    print("   caret row %d  rows before %s" % (caret_row, rows_before))
    print("                 rows after  %s" % (rows_after,))
    print("   rows below shifted by != %d: %s" % (N, moved if moved else "none"))
    check("D: a REFLOW was actually arranged (case, not assertion)", bool(moved),
          "every row below moved by exactly %d - no wrap happened, so the "
          "assertion below proves nothing" % N)
    wrapped = shot(m)
    top1 = rw("wd_top")
    mo.to(sbx, ydn); time.sleep(0.25)
    m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
    for _ in range(12):
        if rw("wd_top") <= top1:
            break
        mo.to(sbx, yup); time.sleep(0.25)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(1.5)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    if rw("wd_top") != top1:
        check("D: the reflow case came back to the same top", False,
              "%d -> %d" % (top1, rw("wd_top")))
    else:
        rp2 = shot(m)
        d2 = sum(1 for p, q in zip(band(wrapped, box), band(rp2, box)) if p != q)
        check("D: a REFLOWING line still equals a full repaint", d2 == 0,
              "%d differing pixels" % d2)

    # ---- leg E: the early-out must actually FIRE --------------------------
    # Legs B..D are CORRECTNESS legs and the old code was correct, so they all
    # pass on a build with the early-out compiled out. This is the leg that
    # fails there, and it is a BREAKPOINT rather than a stopwatch: the first
    # version bounded wd_walk's cycles and passed at 344,824 with the feature
    # disabled, because the walk's cost swings with the caret's row and the
    # margin I assumed was not there. nasm's map carries local labels, so the
    # success path can be named outright - wd_eoutck.rok is reached only after
    # the index has matched.
    click_row(1, col=20)
    m.bp_exec(base + syms["wd_eoutck.rok"])
    m.key("KeyM")
    fired = m.wait_stop(20)
    m.bp_exec()
    if fired: m.run()
    time.sleep(1.0)
    check("E: the early-out FIRES on a mid-line keystroke", bool(fired),
          "wd_eoutck.rok never reached - the walk is not stopping early")
    m.key("Backspace"); time.sleep(1.0)

    # ---- leg F: a caret move is BOUNDED, and still correct ----------------
    # Left and Right used to park [wd_mvbot] at the 0x7FFF sentinel and lay out
    # the whole view to be told nothing moved: 142.8 ms of walk for Right and
    # 164.8 for Left, against Down's 4.3 (SPEC.md 27.4.4). The caret travels
    # ONE character, so the deeper of the two rows whose signatures can differ
    # is never past [wd_ckpr] + 1.
    click_row(1, col=20)
    m.key("ArrowRight"); time.sleep(1.0)
    mvb = rw("wd_mvbot")
    check("F: a Right arrow bounds the walk (not the sentinel)", mvb != 0x7FFF,
          "wd_mvbot = 0x%04X - Left/Right are unbounded again" % mvb)
    m.key("ArrowLeft"); time.sleep(1.0)
    mvb2 = rw("wd_mvbot")
    check("F: a Left arrow bounds it too", mvb2 != 0x7FFF,
          "wd_mvbot = 0x%04X" % mvb2)

    # ---- leg G: and the pixels survive a burst of them --------------------
    click_row(1, col=20)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    before_moves = shot(m)
    for _ in range(8):
        m.key("ArrowRight"); time.sleep(0.6)
    for _ in range(8):
        m.key("ArrowLeft"); time.sleep(0.6)
    mo.to(4, 4); time.sleep(1.0); M.settle(m)
    after_moves = shot(m)
    dm = sum(1 for p, q in zip(band(before_moves, box), band(after_moves, box))
             if p != q)
    check("G: a caret round trip leaves the screen identical", dm == 0,
          "%d differing pixels" % dm)

print()
print("wdtype: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
sys.exit(1 if FAIL else 0)
