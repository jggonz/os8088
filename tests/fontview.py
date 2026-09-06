#!/usr/bin/env python3
"""FONT VIEWER's association, catalogue, specimen and selection (SPEC.md 90).

Run after `make`: python3 tests/fontview.py [machine]
"""
import sys
import time

sys.path[:0] = ["tools", "tests"]
import os88marty
import os88mouse
import os88sym
import dispcp

S = os88sym.linear
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
FV_LISTY, FV_ROWH = 15, 11
TY_MAXFACE, TY_BANDSZ, TY_NGLYPH, TF_SIZE = 4, 92 * 16, 95, 16
FV_BSS_OWN = 50


def u16(data, at=0):
    return data[at] | data[at + 1] << 8


def package_segment(m, slot):
    rec = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    return u16(rec, 22)


def fv_state(m, base):
    b = m.read(base, 13)
    return dict(selected=b[6], loaded=b[7], face=b[8], pending=b[9],
                error=b[10], arghave=b[11], textlen=b[12])


fails = []
with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "A")
    slot = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, slot)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "FONTS")
    names = [n for n, _ in dispcp.listing(m, S)]
    installed = [n for n in names if n.endswith(".F88")]
    print("installed:", installed)
    if len(installed) != 10:
        fails.append("FONTS contains %d F88 faces, not all 10" % len(installed))

    slot = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, slot)
    before = dispcp.win_list(m, S)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "CHARTER.F88")
    time.sleep(3)
    after = dispcp.win_list(m, S)
    if len(after) <= len(before):
        raise SystemExit("fontview: CHARTER.F88 opened no new window")

    fvslot = after[-1]
    seg = package_segment(m, fvslot)
    image_end = u16(m.read(seg * 16, 32), 8)
    base = seg * 16 + image_end
    state = fv_state(m, base)
    print("associated launch:", state)
    if not (state["selected"] == state["loaded"] and state["face"] > 0
            and not state["pending"] and not state["error"]
            and state["arghave"]):
        fails.append("CHARTER.F88 did not become the open selected face: %r"
                     % state)

    # ty_nfam is after the band, advances, four face slots, ten words and
    # eight scalar bytes in TY_BSS.  It must agree with the directory rather
    # than merely reaching TY_MAXFAM and silently hiding a family.
    ty_nfam = (base + FV_BSS_OWN + TY_BANDSZ + TY_NGLYPH
               + TY_MAXFACE * TF_SIZE + 9 * 2 + 9)
    found = m.read(ty_nfam, 1)[0]
    print("catalogue rows:", found)
    if found != len(installed):
        fails.append("viewer lists %d of %d installed faces" %
                     (found, len(installed)))

    oldlen = state["textlen"]
    m.type_text("XYZ")
    os88marty.settle(m)
    state = fv_state(m, base)
    print("after typing XYZ:", state)
    if state["textlen"] != oldlen + 3:
        fails.append("typing changed specimen length %d -> %d, wanted %d" %
                     (oldlen, state["textlen"], oldlen + 3))
    m.key("Backspace")
    os88marty.settle(m)
    state = fv_state(m, base)
    if state["textlen"] != oldlen + 2:
        fails.append("Backspace did not edit the specimen")

    old = state["loaded"]
    m.key("ArrowDown")
    time.sleep(3)
    os88marty.settle(m)
    state = fv_state(m, base)
    print("after Down:", state)
    if (state["loaded"] == old or state["selected"] != state["loaded"]
            or not state["face"] or state["pending"] or state["error"]):
        fails.append("Down did not finish loading the next face: %r" % state)

    # The mouse path is separate from the arrow path: click row 4 using the
    # content origin the package banked from WM_CONTENT.
    raw = m.read(base, 6)
    cx, cy = u16(raw, 2), u16(raw, 4)
    target = 4
    mo.click(cx + 12, cy + FV_LISTY + target * FV_ROWH + 4)
    time.sleep(3)
    os88marty.settle(m)
    state = fv_state(m, base)
    print("after clicking row 4:", state)
    if (state["selected"] != target or state["loaded"] != target
            or state["pending"] or state["error"]):
        fails.append("clicking family row 4 did not load it: %r" % state)

if fails:
    print("\nfontview: FAIL")
    for failure in fails:
        print("  " + failure)
    raise SystemExit(1)
print("\nfontview: association, all faces, typing, arrows and clicks - PASS on "
      + MACHINE)
