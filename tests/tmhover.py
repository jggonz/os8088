#!/usr/bin/env python3
"""The memory map's HOVER inverts the row it stands for (SPEC.md 28.4.16)

    make && python3 tests/tmhover.py [--machine os8088_xt_vga]

The Task Manager's memory map draws every claim at its address in a texture,
and the legend square beside a row is the only key from one to the other. With
the pointer over a band, the row of the group that band belongs to is
lettered INVERTED until the pointer leaves.

  PROGRAM  the pointer over the Calculator's region: the page names the
           Calculator's group ([tm_hgid]), marks a screen row inverted
           ([tm_hrow]) - the one the last walk put that group on ([tm_rgid]) -
           and the row's text band is mostly INK on the glass, where the same
           band was mostly ground before
  KERNEL   the pointer over the kernel's own span at the map's left edge:
           System's row, the same three ways
  PURGE    DETAIL on and Purgeable open: the pointer over one cache's band
           lights THAT CLAIM's own row ([tm_hcid], [tm_rcid]) rather than
           the Purgeable heading - every cache wears the one texture on the
           map, so the heading alone matched nothing
  OFF      the pointer on the page's own list, off the map: nothing named,
           nothing inverted, and both rows back to ground on the glass

READ OFF THE PAGE'S OWN STATE AND THEN CHECKED ON THE GLASS. The bss says
which row the page thinks is inverted; the framebuffer says whether it is.
Either alone passes a broken build: state with no pixels is a toggle that
never drew, pixels with no state is a highlight the next toggle cannot find.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88ui, os88marty, dispapps

MACHINE, SHOTS = "os8088_xt_vga", None
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]
    if a == "--shots":                  # a PNG per leg, to LOOK at
        SHOTS = sys.argv[i + 1]

INST_MAX, MEM_MAX, CLS_RECSZ = 12, 32, 6    # apps/os88api.inc
TMG_SYS = INST_MAX                          # apps/taskmgr/taskmgr.asm
TMG_PURGE = INST_MAX + 2
MEM_PG_MIN, MEM_PG_MAX = 0xFB, 0xFE         # a purge tier's owner high byte
TM_GW = 223 - 7                             # TM_RW - 7: the map's interior
TM_PEN, TM_RW, TM_ROW_H, TM_NCHUNK = 8, 223, 11, 5
TMM_M1_Y1, TMM_ROW_Y = 14, 78
TITLE_H = 18
fails = []

with os88ui.boot("build/os8088-360.img", apps="build/apps360.img",
                 machine=MACHINE, quick=True) as ui:
    ui.still(0)
    m = ui.m

    ui.path("B:/APPS/CALC.O88")
    calc = ui.window("Calculator").i
    ui.path("A:/SYSTEM/TASKMGR.O88")
    got, i = None, 0
    while dispapps.pkg_seg(m, i) is not None:
        got = dispapps.pkg_seg(m, i)
        i += 1
    if got is None:
        sys.exit("the Task Manager did not open - %r" % ui.titles())
    slot, _ = got
    img = dispapps.img_size("taskmgr")
    P = os88marty.Pkg(m, slot, image=img)

    def off(name):
        return dispapps.bss_off("taskmgr", name)

    def W(i):
        return [x for x in ui.windows() if x.i == i][0]

    ui.move_window(W(calc), 8, 26)       # left, clear of the page (CGA is 200 high)
    ui.raise_window(W(slot))
    w = W(slot)
    ui.mo.click(w.x + 20, w.y + TITLE_H + 40)       # onto the memory page
    ui._wait(lambda: P.byte(off("tm_view")) == 1, "the memory page", guest=10)
    ui._wait(lambda: any(g != 0xFF for g in P.read(off("tm_rgid"), 8)), "a walk", guest=10)

    cx, cy = P.word(off("tm_cx")), P.word(off("tm_cy"))
    xoff = P.word(off("tm_xoff"))

    def band(row):
        """The inverted row's text band on the glass: the fraction of its
        pixels that are INK (not the pane's white)"""
        y = cy + TMM_ROW_Y - xoff + row * TM_ROW_H
        x1, x2 = cx + TM_PEN, cx + TM_RW - 12      # clear of the scroll bar
        wd, _, fb = m.fbuf()
        ink = n = 0
        for yy in range(y, y + 8):
            for xx in range(x1, x2):
                o = (yy * wd + xx) * 3
                n += 1
                ink += fb[o:o + 3] != b"\xff\xff\xff"
        return ink / n

    def rows_of(gid):
        rg = P.read(off("tm_rgid"), 16)
        return [r for r, g in enumerate(rg) if g == gid]

    def hover(px, label, gid):
        mx, my = cx + 7 + px, cy + TMM_M1_Y1 + 1 + 7
        row = rows_of(gid)
        if not row:
            fails.append("%s: group %d has no row on the screen" % (label, gid))
            return None
        row = row[0]
        before = band(row)
        ui.mo.to(mx, my)
        try:
            ui._wait(lambda: P.byte(off("tm_hgid")) == gid + 1
                     and P.byte(off("tm_hrow")) == row + 1,
                     "%s highlighted" % label, guest=5,
                     snapshot=lambda: "hgid %d hrow %d" % (
                         P.byte(off("tm_hgid")), P.byte(off("tm_hrow"))))
        except os88ui.UIError as e:
            fails.append("%s: %s" % (label, e))
            return row
        ui.still(0.2)
        after = band(row)
        if SHOTS:
            wd, ht, fb = m.fbuf()
            os88marty.write_png_rgb(os.path.join(SHOTS, "tmhover-%s.png"
                                                 % label.lower()), wd, ht, fb)
        print("%-8s px %3d -> group %2d, row %2d: ink %.2f before, %.2f "
              "hovered" % (label, px, gid, row, before, after))
        if not (before < 0.5 < after):
            fails.append("%s: row %d's band is %.2f ink hovered against %.2f "
                         "before - not inverted on the glass"
                         % (label, row, after, before))
        return row

    # --- PROGRAM: the middle of the Calculator's region ----------------------
    tot = P.word(off("tm_totkb"))
    snap = P.read(off("tm_claims"), MEM_MAX * CLS_RECSZ)
    gids = P.read(off("tm_cgid"), MEM_MAX)
    px = None
    for r in range(MEM_MAX):
        seg = int.from_bytes(snap[r * CLS_RECSZ:r * CLS_RECSZ + 2], "little")
        para = int.from_bytes(snap[r * CLS_RECSZ + 2:r * CLS_RECSZ + 4],
                              "little")
        if seg and gids[r] == calc and para >= 0x40 * 4:
            a = seg >> 6
            b = a + (para >> 6) - 1
            x1, x2 = a * TM_GW // tot, b * TM_GW // tot
            px = (x1 + x2) // 2
    if px is None:
        fails.append("PROGRAM: no claim of the Calculator's 4KB or more in "
                     "the page's snapshot")
        prow = None
    else:
        prow = hover(px, "PROGRAM", calc)

    # --- WALK: a refresh that re-letters the hovered row keeps it inverted ---
    # The toggle inverted it by XOR; now the page's own walk draws it. Every
    # element key is forgotten (so the refresh is not skipped as quiet) and
    # so is the row's, as a figure changing in it would make it - the walk
    # must letter it INVERTED, from [tm_hgid], and leave [tm_hrow] naming it.
    if prow is not None:
        ck = off("tm_rowck") + prow * TM_NCHUNK * 2
        el = off("tm_elck")
        m.pause()
        P.write(el, bytes(off("tm_rowck") - el))
        P.write(ck, bytes(TM_NCHUNK * 2))
        m.run()
        try:
            ui._wait(lambda: all(P.read(ck, TM_NCHUNK * 2)),
                     "the walk re-lettering row %d" % prow, guest=6)
            ui.still(0.2)
            b = band(prow)
            print("WALK     row %2d re-lettered by the page: ink %.2f, "
                  "hrow %d" % (prow, b, P.byte(off("tm_hrow"))))
            if b < 0.5:
                fails.append("WALK: the walk lettered the hovered row %d "
                             "plain (%.2f ink)" % (prow, b))
            if P.byte(off("tm_hrow")) != prow + 1:
                fails.append("WALK: [tm_hrow] is %d after the walk, not %d"
                             % (P.byte(off("tm_hrow")), prow + 1))
        except os88ui.UIError as e:
            fails.append("WALK: %s" % e)

    # --- KERNEL: the map's second column is the kernel's span ----------------
    krow = hover(1, "KERNEL", TMG_SYS)
    if prow is not None and krow is not None and prow != krow:
        b = band(prow)
        if b > 0.5:
            fails.append("KERNEL: the Calculator's row %d stayed inverted "
                         "(%.2f ink) when the pointer moved on" % (prow, b))

    # --- PURGE: DETAIL on, Purgeable open, a cache's own row -----------------
    # The two switches are the page's own bytes, set and then a walk forced,
    # as WALK forces one: what is under test is the hover, not the clicks
    urow = None
    shown = off("tm_rsq") - off("tm_rgid")      # TM_SHOWN
    m.pause()
    P.write(off("tm_detail"), b"\x01")
    exp = int.from_bytes(P.read(off("tm_exp"), 4), "little") | (1 << TMG_PURGE)
    P.write(off("tm_exp"), exp.to_bytes(4, "little"))
    el = off("tm_elck")
    P.write(el, bytes(off("tm_rowck") - el))
    P.write(off("tm_rowck"), bytes(shown * TM_NCHUNK * 2))   # every row
    m.run()
    try:
        ui._wait(lambda: TMG_PURGE in P.read(off("tm_rgid"), shown),
                 "the Purgeable heading listed", guest=10)
        ui.still(0.3)
        snap = P.read(off("tm_claims"), MEM_MAX * CLS_RECSZ)
        best = None
        for r in range(MEM_MAX):
            o = r * CLS_RECSZ
            seg = int.from_bytes(snap[o:o + 2], "little")
            para = int.from_bytes(snap[o + 2:o + 4], "little")
            own = snap[o + 5]
            if seg and MEM_PG_MIN <= own <= MEM_PG_MAX and para >= 0x40 * 4:
                if best is None or para > best[2]:
                    best = (r, seg, para)
        rc = P.read(off("tm_rcid"), shown)
        if best is None:
            fails.append("PURGE: no cache of 4KB or more to hover")
        elif best[0] + 1 not in rc:
            fails.append("PURGE: claim %d has no row of its own (tm_rcid %r)"
                         % (best[0], list(rc)))
        else:
            r, seg, para = best
            urow = rc.index(r + 1)
            grow = list(P.read(off("tm_rgid"), shown)).index(TMG_PURGE)
            a = seg >> 6
            b = a + (para >> 6) - 1
            px = (a * TM_GW // tot + b * TM_GW // tot) // 2
            before = band(urow)
            ui.mo.to(cx + 7 + px, cy + TMM_M1_Y1 + 1 + 7)
            ui._wait(lambda: P.byte(off("tm_hgid")) == TMG_PURGE + 1
                     and P.byte(off("tm_hcid")) == r + 1
                     and P.byte(off("tm_hrow")) == urow + 1,
                     "PURGE claim %d's row highlighted" % r, guest=5,
                     snapshot=lambda: "hgid %d hcid %d hrow %d" % (
                         P.byte(off("tm_hgid")), P.byte(off("tm_hcid")),
                         P.byte(off("tm_hrow"))))
            ui.still(0.2)
            after, gb = band(urow), band(grow)
            if SHOTS:
                wd, ht, fb = m.fbuf()
                os88marty.write_png_rgb(os.path.join(SHOTS,
                                        "tmhover-purge.png"), wd, ht, fb)
            print("PURGE    px %3d -> claim %2d, row %2d: ink %.2f before, "
                  "%.2f hovered; heading row %2d %.2f"
                  % (px, r, urow, before, after, grow, gb))
            if not (before < 0.5 < after):
                fails.append("PURGE: row %d's band is %.2f ink hovered "
                             "against %.2f before" % (urow, after, before))
            if gb > 0.5:
                fails.append("PURGE: the Purgeable heading (row %d) is "
                             "inverted too (%.2f ink)" % (grow, gb))
    except os88ui.UIError as e:
        fails.append("PURGE: %s" % e)
        print("PURGE: detail %d exp %08x rgid %r" % (P.byte(off("tm_detail")),
              int.from_bytes(P.read(off("tm_exp"), 4), "little"),
              list(P.read(off("tm_rgid"), shown))))
        if SHOTS:
            wd, ht, fb = m.fbuf()
            os88marty.write_png_rgb(os.path.join(SHOTS, "tmhover-pfail.png"),
                                    wd, ht, fb)

    # --- OFF: the list, under the map ---------------------------------------
    ui.mo.to(cx + 120, cy + TMM_ROW_Y + 40)    # on the page, off the map
    try:
        ui._wait(lambda: P.byte(off("tm_hgid")) == 0
                 and P.byte(off("tm_hcid")) == 0
                 and P.byte(off("tm_hrow")) == 0, "no highlight", guest=5)
    except os88ui.UIError as e:
        fails.append("OFF: %s" % e)
    ui.still(0.2)
    for r, lab in ((prow, "PROGRAM"), (krow, "KERNEL"), (urow, "PURGE")):
        if r is not None:
            b = band(r)
            print("OFF      row %2d (%s): ink %.2f" % (r, lab, b))
            if b > 0.5:
                fails.append("OFF: row %d is still %.2f ink with the pointer "
                             "off the map" % (r, b))

print()
if fails:
    for f in fails:
        print("FAIL:", f)
    sys.exit(1)
print("tmhover: ok")
