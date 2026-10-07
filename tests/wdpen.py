#!/usr/bin/env python3
"""THE DISABLED PEN BELONGS TO THE HOLD (SPEC.md 68.2.5).

    make worddisk && python3 tests/wdpen.py

`[gfx_dis]` is ONE KERNEL BYTE. `font_ink` is the only thing that reads it, so
it reaches GLYPHS and not fills or lines, and `gfx_unlock` clears it - which
makes its lifetime one gfx-lock hold. SPEC.md 12.8.3 takes that lock around
the WHOLE event handler, so a greyed control drawn anywhere earlier in the
same hold is still armed when Word's callback starts lettering.

The field photograph is unmistakable and is what this row is made of: Word's
nine menu titles drawn as a 50% checkerboard with their mnemonic UNDERLINES
solid beside them - which is precisely a pen that reaches glyphs and not
lines - and it does not clear up, because nothing redraws the chrome.

Both legs arm the pen BEHIND WORD'S BACK at the instant a callback is
ENTERED, which is where the guard is. Poking later would only prove the poke
works; poking before the gesture is cleared by whatever the window manager
draws on the way in, which is how the first version of this row passed on a
build with no guard at all.

  leg A  a View toggle, which is the one ordinary gesture that redraws the
         menu bar (SPEC.md 68.2.5's own measurement: a 205-event sweep ran
         `wd_mbar` zero times) - and the bar must come out solid
  leg B  ...and a click in the text, which is glyphs too
"""
import os, sys, subprocess, tempfile, argparse, functools
print = functools.partial(print, flush=True)
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools"); sys.path.insert(0, "tests")
import os88marty as M
from os88mouse import Mouse
import dispcp

u16 = lambda b, i=0: b[i] | (b[i+1] << 8)
FAIL = []


def check(name, ok, detail=""):
    print("   %-54s %s%s" % (name, "ok" if ok else "FAIL", "" if ok else "  " + detail))
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


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_cga_gla")
a = ap.parse_args()
syms, image = pkg_syms()
DISK = "build/wdpengate.img"
M.scratch_disk(DISK, "build/word.o88", "build/WELCOME.DOC")

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m); mo = Mouse(marty=m); S = lambda n: m.sym(n)
    print("== Word: the disabled pen belongs to the hold (SPEC.md 68.2.5) ==")
    dispcp.open_drive(m, mo, S, M.settle, "B")
    w = dispcp.win_list(m, S)[-1]; dx, dy = dispcp.win_rect(m, S, w)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")
    # The window is up; what is left is the document read, which ends when
    # the floppy goes quiet.
    M.quiesce(m, lambda: m.disk().get("reads"), guest=1.0,
              what="Word to finish reading WELCOME.DOC")

    def find_seg():
        raw = m.read(S("inst_tab"), 32*12)
        for i in range(12):
            b = i*32
            if raw[b] == 1 and (raw[b+2] & 0x80):
                c = u16(raw, b+6)
                if m.read(c*16+syms["wd_mact"], 48) == image[syms["wd_mact"]:syms["wd_mact"]+48]:
                    return c
        return None
    try:
        M.until(m, lambda _: find_seg() is not None, "Word's instance record",
                poll=0.1, limit=10)
    except M.MartyError:
        pass
    seg = find_seg()
    if seg is None:
        sys.exit("could not locate the running package (stale build/word.o88?)")
    base = seg*16; P = lambda n: base + syms[n]
    rw = lambda n: u16(m.read(P(n), 2)); rb = lambda n: m.read(P(n), 1)[0]
    for _ in range(400):
        if rb("wd_hdirty") == 0:
            break
        m.advance(cycles=2_000_000)
    M.settle(m)

    GD = S("gfx_dis")
    cl, ct = rw("wd_cl"), rw("wd_ct")
    tx = rw("wd_tx")
    ryb = lambda r: u16(m.read(P("wd_ryb") + 2*r, 2))
    print("   gfx_dis=%d at idle; menu bar at (%d,%d)" % (m.read(GD, 1)[0], cl+8, ct+2))

    def shot():
        if m.cards()[0]["type"] in ("cga", "mda"):
            w2, h, rr = m.vram(); return w2, bytes(b for r in rr for b in r)
        w2, h, px = m.fbuf()
        return w2, bytes(1 if px[i] or px[i+1] or px[i+2] else 0
                         for i in range(0, len(px), 3))

    def ink(y0, y1, x0, x1):
        w2, px = shot()
        return sum(1 for y in range(y0, y1+1) for x in range(x0, x1+1)
                   if not px[y*w2 + x])

    MB = (ct+2, ct+11, cl+8, min(cl + 8 + 56*8 - 1, rw("wd_sbr")))
    TXT = (ryb(3), ryb(3) + rw("wd_gh1"), tx, rw("wd_rgt"))

    hits = [0]                  # entries to the armed symbol, for `drawn`

    def drawn(before, what):
        """Until the armed symbol has run since BEFORE and the gfx lock is
        free again - SPEC.md 12.8.3 holds it round the whole handler, so that
        is the hold the poke landed in having ended."""
        M.until(m, lambda mm: hits[0] > before
                and mm.read(S("gfx_lock_flag"), 1)[0] == 0,
                what, poll=0.05, limit=20)

    def poked(sym, act, what):
        """Run ACT with [gfx_dis] armed at every entry to SYM.

        `bp_trace` PUMPS from a daemon, so the pointer verbs inside `act`
        drive a machine that keeps moving - which a bare `bp_exec` does not,
        and os88mouse says so by name when it is the cause.
        """
        hits[0] = 0
        def on_hit(mm, rec):
            mm.write(GD, bytes([1])); hits[0] += 1
            return 1
        with M.bp_trace(m, base + syms[sym], on_hit=on_hit, cap=4000):
            act()
        M.settle(m)
        print("      armed the pen at %s %d time(s) (%s)" % (sym, hits[0], what))
        return hits[0]

    base_mb = ink(*MB)
    base_txt = ink(*TXT)
    print("   pen live: menu bar ink %d, text row ink %d" % (base_mb, base_txt))

    # --- leg A: the chrome, under a KERNEL repaint --------------------------
    # It has to be a kernel repaint and not a View toggle, which is the
    # obvious spelling and measures nothing: wd_vtoggle redraws the four
    # strips ITSELF after wd_redrawall ("cheap insurance either way"), so a
    # pen armed against that pass's wd_chrome is overwritten by a live second
    # draw a moment later. A RESIZE goes through W_PAINT and nothing draws
    # the bar again after it.
    #
    # The poke is at wd_chrome and not at the callback, because wd_paint
    # reaches the chrome THROUGH wd_sbar, whose os88ui scroll bar puts the
    # pen back live on its way past - so the callback's guard is not what
    # defends the strips and a leg that poked there would pass on a build
    # with no chrome guard at all.
    wdw = dispcp.win_list(m, S)[-1]         # WORD'S slot, not the Disk
    wx, wy, ww, wh = dispcp.win_rect(m, S, wdw)     # window `w` above still
    gx, gy = wx + ww - 5, wy + wh - 5               # names
    print("   Word is slot %d at (%d,%d) %dx%d; grow box (%d,%d)"
          % (wdw, wx, wy, ww, wh, gx, gy))

    def resize(dy):
        # The grow box where it IS now: the second call puts back what the
        # first took, so it has to press the box the first one moved.
        x, y, w2, h = dispcp.win_rect(m, S, wdw)
        gx, gy = x + w2 - 5, y + h - 5
        mo.to(gx, gy); mo._sep()
        mo._edge(True); M.guest_sleep(m, 0.15)
        mo.to(gx, gy + dy, l=True); M.guest_sleep(m, 0.15)
        before = hits[0]
        mo._edge(False)
        drawn(before, "the resize to repaint Word's chrome")

    n = poked("wd_chrome", lambda: (resize(-24), resize(24)),
              "a window resize")
    check("the chrome really redrew (case, not assertion)", n > 0,
          "wd_chrome never ran, so nothing was tested")
    got_mb = ink(*MB)
    check("A: the menu bar is SOLID with the pen armed against it",
          abs(got_mb - base_mb) <= max(4, base_mb // 20),
          "%d ink against %d with the pen live - the titles came out as a "
          "checkerboard" % (got_mb, base_mb))
    print("      menu bar ink %d (was %d)" % (got_mb, base_mb))

    def click_text():
        before = hits[0]
        mo.click(tx + 60, ryb(3) + 2, settle=0)
        drawn(before, "Word's click handler to run and return")

    n2 = poked("wd_onclick", click_text, "a click in the text")
    check("the click really ran (case, not assertion)", n2 > 0,
          "wd_onclick never ran")
    got_txt = ink(*TXT)
    check("B: the text is SOLID with the pen armed against it",
          abs(got_txt - base_txt) <= max(4, base_txt // 20),
          "%d ink against %d with the pen live" % (got_txt, base_txt))
    print("      text row ink %d (was %d)" % (got_txt, base_txt))

print()
if FAIL:
    print("FAILED: " + ", ".join(FAIL)); sys.exit(1)
print("ok"); sys.exit(0)
