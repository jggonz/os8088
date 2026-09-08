#!/usr/bin/env python3
"""WORD'S COMBOS ARE DROP-DOWNS (SPEC.md 68.2.3), and the gesture works BOTH WAYS.

    make worddisk && python3 tests/wdcombo.py

The ribbon's Font and Pts and the ruler's Style used to be three rows of
`wd_mtab` - pseudo-menus on Word's own menu code, opened by a modal poll.  They
are `os88ui_drop` records now (SPEC.md 13.14), and ALL THREE are driven here:
they share `wd_drops` and every routine that walks it, but each sits in a
different strip with a different hit test in front of it, and the Font one's
list is built at runtime by `wd_fontscan`.  Being records now means the gesture
is three EVENTS rather than one loop: `os88ui_drpress` on the way down, `os88ui_drdrag`
while the button is held, `os88ui_drup` at the release.  That is the whole of
what this row is for, because each edge fails differently and silently:

  * press-drag-release  - needs W_ONDRAG to reach the record, or DR_HOT stays
    0FFh and the release picks NOTHING while leaving the list on screen
  * click-then-click    - needs the press ROUTED to the record before the strip
    hit tests.  The Style list lies on top of the ruler's second row, which
    owns the indent-marker drag, so without that routing the second click goes
    to the RULER and the list never comes down.  This one was real: it is why
    `wd_mroute` tests `wd_drany` in front of everything

and both of them end in PIXELS.  The list banks what it covers (SPEC.md
13.14.1) and the close writes it back, so a cycle that opens and picks must
leave the content bit-for-bit as it found it - and the second cycle pokes
`OS88UI_DR_SEG` = 0 while the list is down, which is what a refused claim
leaves behind, so `wd_drrep`'s piecewise repaint is measured against the same
reference in the same boot.  IT PUTS THE SEGMENT BACK before it lets go: a
real refusal never claims at all, and a poke that only clears the word
ORPHANS the claim.  That leak is not harmless - one of them, and Word's next
full re-layout reads its piece table through a stale segment and the document
blanks - so a test that forgets it fails on the thing after the thing it is
testing, which is exactly how this was found.

THE REFERENCE IS TAKEN INSIDE ONE BOOT and never across two: two boots of this
machine differ by a handful of bits at the desktop clock alone, so a
cross-boot comparison cannot answer a question about a save-under.
"""
import os, sys, time, subprocess, tempfile, argparse

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, "tools")
sys.path.insert(0, "tests")

import os88marty as M
from os88mouse import Mouse
import dispcp

WD_MENU_H, WD_RIBBON_H = 14, 16
WD_RL_SBX, WD_RL_SBW = 64, 96
WD_RB_FBX, WD_RB_FBW = 56, 96
WD_RB_PBX, WD_RB_PBW = 208, 56
OS88UI_DRIH = 10               # Word overrides the control's 12 (68.2.3)
DR_ITEMS, DR_N, DR_SEL, DR_OPEN, DR_HOT, DR_SEG, DR_TOP = 8, 10, 12, 16, 17, 18, 22
FAIL = []
u16 = lambda b, i=0: b[i] | (b[i + 1] << 8)


def check(name, ok, detail=""):
    print("   %-48s %s%s" % (name, "ok" if ok else "FAIL",
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
ap.add_argument("--machine", default="os8088_5150_cga_gla")
a = ap.parse_args()

syms, image = pkg_syms()
DISK = "build/wdcombo.img"
M.scratch_disk(DISK, "build/word.o88", "build/WORD.OVL", "build/WELCOME.DOC")

with M.launch("build/os8088-360.img", apps=DISK, machine=a.machine) as m:
    M.settle(m)
    mo = Mouse(marty=m)
    S = lambda n: m.sym(n)
    print("== Word's combos are drop-downs (SPEC.md 68.2.3) on %s ==" % a.machine)

    dispcp.open_drive(m, mo, S, M.settle, "B")
    d = dispcp.win_list(m, S)[-1]
    dx, dy = dispcp.win_rect(m, S, d)[:2]
    dispcp.open_named(m, mo, S, M.settle, dx, dy, "WELCOME.DOC")
    time.sleep(2.5)
    M.settle(m)

    # the package's base out of the instance table, its identity checked
    # against CODE at a named symbol (wdmenusu.py's probe, same reasoning)
    I_RECSZ, I_STATE, I_SPTR, I_KIND = 32, 0, 6, 2
    raw = m.read(S("inst_tab"), I_RECSZ * 12)
    seg = None
    for i in range(12):
        b = i * I_RECSZ
        if raw[b + I_STATE] == 1 and (raw[b + I_KIND] & 0x80):
            c = u16(raw, b + I_SPTR)
            if m.read(c * 16 + syms["wd_mact"], 48) == \
                    image[syms["wd_mact"]:syms["wd_mact"] + 48]:
                seg = c
                break
    if seg is None:
        sys.exit("could not locate the running package image in inst_tab")
    base = seg * 16
    R = base + syms["wd_dstyle"]
    rw = lambda n: u16(m.read(base + syms[n], 2))
    dw = lambda o: u16(m.read(R + o, 2))
    db = lambda o: m.read(R + o, 1)[0]

    cl, ct, cw, ch = rw("wd_cl"), rw("wd_ct"), rw("wd_cw"), rw("wd_ch")
    box = (cl, ct, cl + cw - 1, ct + ch - 1)
    bx = cl + WD_RL_SBX + WD_RL_SBW // 2
    by = ct + WD_MENU_H + WD_RIBBON_H + 8      # the Style box's own row
    boxtop = ct + WD_MENU_H + WD_RIBBON_H + 2

    mo.to(4, 4)                                # the pointer is part of the
    M.settle(m)                                # picture: park it identically
    before = shot(m)

    # WD_RB_FBX/FBW and WD_RB_PBX/PBW, and the ruler's WD_RL_SBX/SBW: the
    # three boxes, each in its own strip.  The rows come off the strip
    # geometry rather than out of the record, so a record whose RECT the
    # painter never filled is a MISS here rather than a coordinate this test
    # took from the thing it is testing.
    ribtop = ct + WD_MENU_H + 2
    rultop = ct + WD_MENU_H + WD_RIBBON_H + 2
    COMBOS = [
        ("Font",  "wd_dfont",  cl + WD_RB_FBX + WD_RB_FBW // 2, ribtop),
        ("Pts",   "wd_dpts",   cl + WD_RB_PBX + WD_RB_PBW // 2, ribtop),
        ("Style", "wd_dstyle", cl + WD_RL_SBX + WD_RL_SBW // 2, rultop),
    ]

    for label, sym, bx, boxtop in COMBOS:
        R = base + syms[sym]
        dw = lambda o: u16(m.read(R + o, 2))
        db = lambda o: m.read(R + o, 1)[0]
        by = boxtop + 6                        # the middle of the 12px box
        n = dw(DR_N)
        print("   -- %s (%d item%s) --" % (label, n, "" if n == 1 else "s"))

        # --- 1. press, drag onto an item, release ---------------------------
        mo.to(bx, by)
        time.sleep(0.4)
        mo._edge(True)
        time.sleep(1.4)
        check("%s: the press drops the list" % label, db(DR_OPEN) == 1,
              "DR_OPEN=%d" % db(DR_OPEN))
        check("%s: ...and BANKS the pixels it covers" % label, dw(DR_SEG) != 0,
              "DR_SEG=0")
        top = dw(DR_TOP)
        check("%s: ...under the box, or slid up into the window" % label,
              top == boxtop + 12 or top < boxtop + 12,
              "DR_TOP=%d, box top %d" % (top, boxtop))
        mo.to(bx, top + 5, l=True)             # THE BUTTON STAYS DOWN
        time.sleep(1.2)
        check("%s: the DRAG edge reaches the record" % label, db(DR_HOT) == 0,
              "DR_HOT=%d (0FFh = W_ONDRAG never arrived)" % db(DR_HOT))
        mo._edge(False)
        time.sleep(1.8)
        check("%s: the release picks item 0 and closes" % label,
              db(DR_OPEN) == 0 and db(DR_SEL) == 0,
              "OPEN=%d SEL=%d" % (db(DR_OPEN), db(DR_SEL)))
        mo.to(4, 4)
        M.settle(m)                            # A PICK CAN FIRE wd_redraw,
                                               # which is a whole re-layout of
                                               # the document on a 4.77MHz
                                               # 8088 - seconds, not the
                                               # fraction a fixed sleep buys
        d1 = diff(before, shot(m), box)
        check("%s: the close restores the content EXACTLY" % label, not d1,
              "%d differing px, first %s" % (len(d1), d1[:3]))

        # --- 2. the same gesture with the bank REFUSED ----------------------
        mo.to(bx, by)
        time.sleep(0.4)
        mo._edge(True)
        time.sleep(1.4)
        held = m.read(R + DR_SEG, 2)           # what a refused claim leaves -
        m.write(R + DR_SEG, b"\x00\x00")       # AND THE CLAIM GOES BACK below
        mo.to(bx, dw(DR_TOP) + 5, l=True)
        time.sleep(1.2)
        mo._edge(False)
        time.sleep(2.5)
        m.write(R + DR_SEG, held)              # ...here, so the next
                                               # os88ui_drbank frees it - which
                                               # is 13.14.1's own first line
        mo.to(4, 4)
        M.settle(m)                            # A PICK CAN FIRE wd_redraw,
                                               # which is a whole re-layout of
                                               # the document on a 4.77MHz
                                               # 8088 - seconds, not the
                                               # fraction a fixed sleep buys
        d2 = diff(before, shot(m), box)
        check("%s: wd_drrep lands on the same pixels" % label, not d2,
              "%d differing px, first %s" % (len(d2), d2[:3]))

        # --- 3. click-then-click: the press must be ROUTED to an open list --
        mo.to(bx, by)
        time.sleep(0.4)
        mo._edge(True)
        time.sleep(0.5)
        mo._edge(False)
        time.sleep(1.4)
        check("%s: press-and-release on the box leaves it OPEN" % label,
              db(DR_OPEN) == 1, "DR_OPEN=%d" % db(DR_OPEN))
        t2 = dw(DR_TOP)
        mo.to(bx, t2 + 5)
        time.sleep(0.4)
        mo._edge(True)
        time.sleep(0.5)
        mo._edge(False)
        time.sleep(1.8)
        check("%s: the second click picks and closes" % label, db(DR_OPEN) == 0,
              "DR_OPEN=%d (the strip under it took the press?)" % db(DR_OPEN))
        mo.to(4, 4)
        M.settle(m)                            # A PICK CAN FIRE wd_redraw,
                                               # which is a whole re-layout of
                                               # the document on a 4.77MHz
                                               # 8088 - seconds, not the
                                               # fraction a fixed sleep buys
        d3 = diff(before, shot(m), box)
        check("%s: ...and restores the content too" % label, not d3,
              "%d differing px, first %s" % (len(d3), d3[:3]))

        # --- 4. a KEY takes an open list down, as it takes a menu down ------
        mo.to(bx, by)
        time.sleep(0.4)
        mo._edge(True)
        time.sleep(0.5)
        mo._edge(False)
        time.sleep(1.4)
        check("%s: the list is open for the key test" % label, db(DR_OPEN) == 1,
              "DR_OPEN=%d" % db(DR_OPEN))
        m.key("Escape")
        time.sleep(1.8)
        check("%s: Esc takes it down" % label, db(DR_OPEN) == 0,
              "DR_OPEN=%d" % db(DR_OPEN))
        mo.to(4, 4)
        M.settle(m)                            # A PICK CAN FIRE wd_redraw,
                                               # which is a whole re-layout of
                                               # the document on a 4.77MHz
                                               # 8088 - seconds, not the
                                               # fraction a fixed sleep buys
        d4 = diff(before, shot(m), box)
        check("%s: ...and the key's close restores it too" % label, not d4,
              "%d differing px, first %s" % (len(d4), d4[:3]))

    # --- the Font combo's list is the machine's own, and picking ACTS -------
    # wd_fontscan walks SYSTEM/FONTS the first time the combo is opened, so by
    # now DR_N is 1 + however many faces this disk has.  A boot with no faces
    # is a legitimate 1, and the assertion is the one that holds either way:
    # the record's N agrees with [wd_nfont], and item 0 is Pica.
    Rf = base + syms["wd_dfont"]
    n = u16(m.read(Rf + DR_N, 2))
    nfont = m.read(base + syms["wd_nfont"], 1)[0]
    check("the Font list is 1 + [wd_nfont]", n == nfont + 1,
          "DR_N=%d, wd_nfont=%d" % (n, nfont))
    items = u16(m.read(Rf + DR_ITEMS, 2))
    check("...and item 0 is wd_s_pica", u16(m.read(base + items, 2))
          == syms["wd_s_pica"], "items[0]=%04x, wd_s_pica=%04x"
          % (u16(m.read(base + items, 2)), syms["wd_s_pica"]))
    cap = lambda: u16(m.read(base + items
                              + 2 * u16(m.read(Rf + DR_SEL, 2)), 2))
    fcap = lambda: u16(m.read(base + syms["wd_fcap"], 2))
    check("...and the box's caption follows [wd_fcap]", cap() == fcap(),
          "SEL=%d" % u16(m.read(Rf + DR_SEL, 2)))

    # --- and PICKING one ACTS, which no other combo does --------------------
    # Item 0 is Pica and picking it changes nothing, so every cycle above
    # proved the gesture and none of them proved wd_drtake.  Item 1 is a real
    # face: the pick has to reach wd_a_csel, open the family, and rename the
    # box - and if ty_openfam REFUSES, the box must still name the face that
    # reads (SPEC.md 68.13), which is what wd_dfsel puts back.  Either way the
    # invariant is the same one, so this is not two tests behind a condition.
    if nfont >= 1:
        bx, boxtop = cl + WD_RB_FBX + WD_RB_FBW // 2, ct + WD_MENU_H + 2
        mo.to(bx, boxtop + 6)
        time.sleep(0.4)
        mo._edge(True)
        time.sleep(1.4)
        top = u16(m.read(Rf + DR_TOP, 2))
        mo.to(bx, top + OS88UI_DRIH + 5, l=True)     # item 1
        time.sleep(1.2)
        hot = m.read(Rf + DR_HOT, 1)[0]
        mo._edge(False)
        time.sleep(2.0)
        M.settle(m)
        check("a face is item 1 of the list", hot == 1, "DR_HOT=%d" % hot)
        sel = u16(m.read(Rf + DR_SEL, 2))
        check("picking a face renames the box, or puts SEL back",
              cap() == fcap(), "SEL=%d cap=%04x fcap=%04x"
              % (sel, cap(), fcap()))
        check("...and it is a face, or the refusal kept Pica",
              (sel == 1 and fcap() != syms["wd_s_pica"])
              or (sel == 0 and fcap() == syms["wd_s_pica"]),
              "SEL=%d fcap=%04x" % (sel, fcap()))
        # ...and back to the built-in cell, which is as much a change
        mo.to(bx, boxtop + 6)
        time.sleep(0.4)
        mo._edge(True)
        time.sleep(1.4)
        mo.to(bx, u16(m.read(Rf + DR_TOP, 2)) + 5, l=True)
        time.sleep(1.2)
        mo._edge(False)
        time.sleep(2.0)
        M.settle(m)
        check("picking Pica back returns to the built-in cell",
              fcap() == syms["wd_s_pica"] and u16(m.read(Rf + DR_SEL, 2)) == 0,
              "fcap=%04x SEL=%d" % (fcap(), u16(m.read(Rf + DR_SEL, 2))))
        mo.to(4, 4)
        M.settle(m)
        d5 = diff(before, shot(m), box)
        check("...and the document is back as it was", not d5,
              "%d differing px, first %s" % (len(d5), d5[:3]))

print("\n%s" % ("all ok" if not FAIL else "FAILED: " + ", ".join(FAIL)))
sys.exit(1 if FAIL else 0)
