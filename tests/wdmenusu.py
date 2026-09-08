#!/usr/bin/env python3
"""WORD'S DROPDOWN SAVE-UNDER PUTS BACK EXACTLY WHAT IT COVERED (SPEC.md 68.2.1).

    make worddisk && python3 tests/wdmenusu.py

Word draws its own nine-title bar inside its window (SPEC.md 68.2), so it owns
the dismissal too.  It used to spell that as `wd_mrepair`, a piecewise repaint
of everything the panel covered - measured at 521 ms on a 4.77MHz 8088, because
the covered text rows are erased FULL WIDTH and re-lettered at ~900us a glyph
cell.  SPEC.md 5.3 published `gfx_save`/`gfx_restore`, so the drop banks its
pixels and the close writes them back: 19.7 ms.

THE ASSERTION IS PIXEL EQUALITY, and it is the only one worth making.  A
save-under that is fast and wrong is worse than a repaint that is slow and
right, and every way of getting it wrong shows up here: banking the panel
WITHOUT its drop shadow leaves a grey L on the glass, clamping the rect
differently from `wd_mrepair` leaves a column, and taking the plane count from
the wrong display leaves colour noise.  So: photograph the content, open a
menu, close it, photograph again, and require ZERO differing pixels.

It is also the A/B for the fallback.  `--repaint` pokes [wd_suseg] = 0 while
the menu is down, which is exactly what a refused claim leaves behind, and the
close must then take `wd_mrepair` and land on the SAME pixels.  One run
therefore checks both paths against one reference.
"""
import os, sys, time, subprocess, tempfile, argparse

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")

import os88marty as M
from os88mouse import Mouse
import dispcp

WD_MENU_H = 14
FAIL = []


def check(name, ok, detail=""):
    print("   %-46s %s%s" % (name, "ok" if ok else "FAIL",
                             "" if ok else "  " + detail))
    if not ok:
        FAIL.append(name)


def pkg_syms(src="apps/word/word.asm", incs=("apps/", "apps/word/")):
    with tempfile.TemporaryDirectory() as d:
        cp, mp = os.path.join(d, "p.asm"), os.path.join(d, "p.map")
        open(cp, "w").write(open(src).read() + "\n[map symbols %s]\n" % mp)
        subprocess.run(["nasm", "-f", "bin", "-w+error"]
                       + sum([["-I", i] for i in incs], [])
                       + ["-o", os.path.join(d, "p.bin"), cp], check=True)
        out = {}
        for line in open(mp):
            f = line.split()
            if len(f) == 3 and all(c in "0123456789ABCDEF" for c in f[0]):
                out[f[2]] = int(f[0], 16)
        return out, open(os.path.join(d, "p.bin"), "rb").read()


u16 = lambda b, i=0: b[i] | (b[i + 1] << 8)


def shot(m):
    if m.cards()[0]["type"] in ("cga", "mda"):
        w, h, rows = m.vram()
        return w, h, bytes(b for r in rows for b in r)
    w, h, px = m.fbuf()
    return w, h, bytes(1 if px[i] or px[i + 1] or px[i + 2] else 0
                       for i in range(0, len(px), 3))


def diff(a, b, box):
    w, _, ap = a
    _, _, bp = b
    x0, y0, x1, y1 = box
    return [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1)
            if ap[y * w + x] != bp[y * w + x]]


ap = argparse.ArgumentParser()
ap.add_argument("--machine", default="os8088_5150_both_gla")
ap.add_argument("--menu", type=int, default=5, help="wd_mtab row (5=Utilities)")
a = ap.parse_args()

syms, image = pkg_syms()
DISK = "build/wdmenusu.img"
M.scratch_disk(DISK, "build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")
S = lambda n: m.sym(n)

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    print("== Word's dropdown save-under (SPEC.md 68.2.1) on %s ==" % a.machine)

    dispcp.open_drive(m, mo, S, M.settle, "B")
    d = dispcp.win_list(m, S)[-1]
    dx, dy = dispcp.win_rect(m, S, d)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")
    time.sleep(2.5)
    M.settle(m)

    # the package's base out of the instance table (I_SPTR,
    # kernel/instance.inc:31), and its identity checked against CODE at a
    # named symbol. Not against the head of the image: os88pkg restamps the
    # size at +8 and the loader patches the header area, so those bytes are
    # legitimately not the ones nasm emitted - while a stale build, which is
    # what this check is for, moves wd_mact's body.
    I_RECSZ, I_STATE, I_SPTR, I_KIND = 32, 0, 6, 2
    raw = m.read(S("inst_tab"), I_RECSZ * 12)
    seg = None
    for i in range(12):
        b = i * I_RECSZ
        if raw[b + I_STATE] == 1 and (raw[b + I_KIND] & 0x80):
            cand = u16(raw, b + I_SPTR)
            if m.read(cand * 16 + syms["wd_mact"], 48) == image[syms["wd_mact"]:syms["wd_mact"] + 48]:
                seg = cand
                break
    if seg is None:
        sys.exit("could not locate the running package image in inst_tab")
    base = seg * 16
    P = lambda n: base + syms[n]
    rw = lambda n: u16(m.read(P(n), 2))
    rb = lambda n: m.read(P(n), 1)[0]

    cl, ct, cw, ch = rw("wd_cl"), rw("wd_ct"), rw("wd_cw"), rw("wd_ch")
    box = (cl, ct, cl + cw - 1, ct + ch - 1)
    tab = m.read(P("wd_mtab"), 8 * 12)
    cell = tab[a.menu * 8]
    tx, ty = cl + cell * 8 + 16, ct + WD_MENU_H // 2

    mo.to(4, 4)                                  # pointer off the content
    time.sleep(0.8)
    before = shot(m)

    def cycle(poke_refuse):
        mo.to(tx, ty); time.sleep(0.4)
        m.mouse(l=True); time.sleep(0.10); m.mouse(l=False)
        time.sleep(1.2)
        opened = rb("wd_mopen")
        banked = rw("wd_suseg")
        if poke_refuse:                          # what a REFUSED claim leaves
            m.write(P("wd_suseg"), b"\x00\x00")
        mo.to(tx, ty); time.sleep(0.3)
        m.mouse(l=True); time.sleep(0.10); m.mouse(l=False)
        time.sleep(1.5)
        mo.to(4, 4); time.sleep(0.8)
        return opened, banked, shot(m)

    op, bk, after = cycle(False)
    check("the menu opened", op == a.menu, "wd_mopen=%d" % op)
    check("the drop BANKED its pixels", bk != 0, "wd_suseg=0 (claim refused?)")
    d1 = diff(before, after, box)
    check("save-under restores the content EXACTLY", not d1,
          "%d differing px, first %s" % (len(d1), d1[:3]))

    op2, bk2, after2 = cycle(True)
    check("the menu opened again", op2 == a.menu, "wd_mopen=%d" % op2)
    d2 = diff(before, after2, box)
    check("the REPAINT fallback lands on the same pixels", not d2,
          "%d differing px, first %s" % (len(d2), d2[:3]))

    # --- EVERY menu's panel rect, against the TABLE that decides it ---------
    # The geometry moved out to os88ui_mngeo (SPEC.md 13.16), and it is the
    # one routine here whose answer is four words rather than pixels - so it
    # is checked as four words, on all nine, derived from wd_mtab rather than
    # remembered. A golden rect would be a window size written down; this is
    # the arithmetic wd_mtab already carries.
    bad = []
    for i in range(9):
        c = tab[i * 8]
        wid = tab[i * 8 + 6] | (tab[i * 8 + 7] << 8)
        mo.to(cl + c * 8 + 16, ct + WD_MENU_H // 2); time.sleep(0.35)
        m.mouse(l=True); time.sleep(0.10); m.mouse(l=False); time.sleep(1.0)
        got = rb("wd_mopen")
        r = [rw("wd_mrx1"), rw("wd_mry1"), rw("wd_mrx2"), rw("wd_mry2")]
        why = []
        if got != i:
            why.append("did not open (wd_mopen=%d)" % got)
        else:
            if r[1] != ct + WD_MENU_H:
                why.append("y1 %d, not the row under the bar (%d)" % (r[1], ct + WD_MENU_H))
            want = cl + c * 8 + 4
            ride = box[2] - 1 - wid + 1        # ...unless it rides the right edge
            if r[0] not in (want, ride) and r[0] != cl + 1:
                why.append("x1 %d, not under its title (%d) nor riding the edge (%d)"
                           % (r[0], want, ride))
            if r[2] - r[0] + 1 != wid and r[2] != box[2] - 1:
                why.append("width %d, not the table's %d" % (r[2] - r[0] + 1, wid))
            if r[3] > box[3] - 1:
                why.append("y2 %d past the content (%d)" % (r[3], box[3] - 1))
        if why:
            bad.append("menu %d: %s" % (i, "; ".join(why)))
        mo.to(cl + 4, ct + 4); time.sleep(0.2)
        m.mouse(l=True); time.sleep(0.08); m.mouse(l=False); time.sleep(0.7)
    check("every menu's panel rect agrees with wd_mtab", not bad,
          " | ".join(bad[:3]))

print()
print("wdmenusu: %s" % ("FAILED: " + ", ".join(FAIL) if FAIL else "ok"))
sys.exit(1 if FAIL else 0)
