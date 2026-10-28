#!/usr/bin/env python3
"""A loaded TYPEFACE moves, and the text it draws does not change (NO-PINNING A7).

    make && python3 tests/tymove.py [machine]

apps/os88type.inc is the typeface library Word, Scribe, CWORD and the Font
Viewer carry. It holds two kinds of claim: each open face (an .F88 read into a
claim, `TF_CLAIM`, which is also its glyph segment `TF_SEG`, with
`[ty_curseg]` a copy of the current one's) and the pre-shifted phase table
(`[ty_psseg]`). Both were PINNED for as long as the program ran - a face a
user opens mid-session is a wall in the arena. `ty_reloc` puts all of those
words right, and both are declared movable once they are filled.

THE SCENARIO: the Font Viewer opens CHARTER.F88 by association (tests/
fontview.py's walk to it), the row posts a compaction from OUTSIDE at the
TRIVIAL purge rank (`[mem_cpq]` + `[mem_cpq_lvl]`, the door OSAPI_MEM_COMPACT
posts through, SPEC.md 66.4.3) - which drops the raise caches and icon store
under the face and so opens a hole beneath it - and the face slides down.

FOUR ASSERTIONS:
  1. every open face's claim and the table's are declared movable;
  2. at least one of them MOVED (else the run proved nothing);
  3. every word names a live claim: TF_CLAIM, TF_SEG = TF_CLAIM, [ty_curseg] =
     the current face's, [ty_psseg];
  4. the specimen REDRAWS identically: the viewer's content, forced to repaint
     by a key that changes nothing visible, is the same picture as before.

VERIFIED TO FAIL: with ty_reloc reduced to a bare `ret`, assertion 3 goes red
(TF_CLAIM names the old base).
"""
import sys

sys.path[:0] = ["tools", "tests"]
import dispapps                                         # noqa: E402
import dispcp                                           # noqa: E402
import os88geom                                         # noqa: E402
import os88marty                                        # noqa: E402
import os88mouse                                        # noqa: E402
import os88sym                                          # noqa: E402

S = os88sym.linear
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
MAP = dispapps._map("fontview")
TF_SIZE, TF_CLAIM, TF_SEG, TF_REF = 16, 0, 2, 12
TY_MAXFACE = 4
MC_SIZE, MEM_MAX = os88geom.MC_SIZE, os88geom.MEM_MAX


def u16(b, i=0):
    return b[i] | b[i + 1] << 8


def claims(m):
    raw = m.read(S("mem_tab"), MEM_MAX * MC_SIZE)
    return {u16(raw, i * MC_SIZE): (u16(raw, i * MC_SIZE + 2),
                                    u16(raw, i * MC_SIZE + 8))
            for i in range(MEM_MAX) if u16(raw, i * MC_SIZE)}


def words(m, seg):
    tab = m.read(seg * 16 + MAP["ty_tab"], TY_MAXFACE * TF_SIZE)
    faces = [(u16(tab, i * TF_SIZE + TF_CLAIM), u16(tab, i * TF_SIZE + TF_SEG))
             for i in range(TY_MAXFACE)
             if tab[i * TF_SIZE + TF_REF] and u16(tab, i * TF_SIZE + TF_CLAIM)]
    cur = u16(m.read(seg * 16 + MAP["ty_curseg"], 2))
    ps = u16(m.read(seg * 16 + MAP["ty_psseg"], 2))
    return faces, cur, ps


fails = []
done = lambda mm, **k: os88marty.ui_done(mm)
with os88marty.launch("build/os8088-360.img", apps="build/office360.img",
                      machine=MACHINE, boot=False) as m:
    m.run()
    os88marty.kernel_up(m)
    os88marty.no_saver(m)
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, done, "B")
    for name in ("SYSTEM", "FONTS"):
        slot = dispcp.win_list(m, S)[-1]
        wx, wy, _, _ = dispcp.win_rect(m, S, slot)
        dispcp.open_named(m, mo, S, done, wx, wy, name)
    slot = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, slot)
    before = dispcp.win_list(m, S)
    dispcp.open_named(m, mo, S, done, wx, wy, "CHARTER.F88")
    os88marty.until(m, lambda _: len(dispcp.win_list(m, S)) > len(before),
                    "the viewer's window", poll=0.2, limit=15)
    fvslot = dispcp.win_list(m, S)[-1]
    seg = u16(m.read(S("wm_wins") + fvslot * dispcp.WIN_SIZE, 22), 22) \
        if False else None
    rec = m.read(S("wm_wins") + fvslot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    seg = u16(rec, 22)
    os88marty.quiesce(m, lambda: m.read(seg * 16 + MAP["fv_pending"], 1),
                      guest=1.0, what="the face load")
    os88marty.ui_done(m)

    faces0, cur0, ps0 = words(m, seg)
    held = claims(m)
    print("faces %s  curseg %04x  psseg %04x"
          % (["%04x" % c for c, _ in faces0], cur0, ps0))
    mine = [c for c, _ in faces0] + ([ps0] if ps0 else [])
    decl = [c for c in mine if c in held and held[c][1]]
    ok = mine and len(decl) == len(mine)
    print("  1 declared movable   %d of %d" % (len(decl), len(mine)))
    if not ok:
        fails.append("not every typeface claim is declared movable")
    x, y, w, h = dispcp.win_rect(m, S, fvslot)
    shot0 = m.fbuf()

    # post a compaction at the TRIVIAL rank, the door OSAPI_MEM_COMPACT uses
    wptr = os88geom.winptr(m, fvslot, S)
    m.write(S("mem_cpq_lvl"), bytes([0xFC]))
    m.write(S("mem_cpq"), bytes([wptr & 0xFF, wptr >> 8]))
    os88marty.until(m, lambda _: u16(m.read(S("mem_cpq"), 2)) == 0,
                    "the posted compaction to run", poll=0.2, limit=30)
    os88marty.ui_done(m)

    rec = m.read(S("wm_wins") + fvslot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    seg = u16(rec, 22)                  # the region may have moved too
    faces1, cur1, ps1 = words(m, seg)
    held = claims(m)
    print("faces %s  curseg %04x  psseg %04x"
          % (["%04x" % c for c, _ in faces1], cur1, ps1))
    moved = (sorted(c for c, _ in faces0) != sorted(c for c, _ in faces1)
             or ps0 != ps1)
    print("  2 something moved    %s" % ("yes" if moved else "NO"))
    if not moved:
        fails.append("nothing moved: the run proves nothing")
    live = all(c in held and s == c for c, s in faces1) \
        and (ps1 == 0 or ps1 in held) \
        and (cur1 in [s for _, s in faces1] or cur1 == cur0 and cur0 not in
             [c for c, _ in faces0])
    print("  3 every word live    %s" % ("yes" if live else "NO"))
    if not live:
        fails.append("a typeface word names no claim after the move")

    # force a repaint of the viewer and compare its content
    m.key("ArrowDown")
    os88marty.ui_done(m)
    m.key("ArrowUp")
    os88marty.quiesce(m, lambda: m.read(seg * 16 + MAP["fv_pending"], 1),
                      guest=1.0, what="the face reload")
    os88marty.ui_done(m)
    shot1 = m.fbuf()
    W = shot0[0]

    def band(f):
        _, _, d = f
        return b"".join(d[(yy * W + x) * 3:(yy * W + x + w) * 3]
                        for yy in range(y, y + h))
    same = band(shot0) == band(shot1)
    print("  4 redraws the same   %s" % ("yes" if same else "NO"))
    if not same:
        fails.append("the specimen does not redraw identically")

if fails:
    print("\ntymove: FAIL")
    for f in fails:
        print("  " + f)
    raise SystemExit(1)
print("\ntymove: 4 checks passed on " + MACHINE)
