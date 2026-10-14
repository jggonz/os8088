#!/usr/bin/env python3
"""FONT VIEWER's association, catalogue, specimen and selection (SPEC.md 90).

Run after `make`: python3 tests/fontview.py [machine]
"""
import sys

sys.path[:0] = ["tools", "tests"]
import os88marty
import os88mouse
import os88sym
import dispcp
import dispapps

S = os88sym.linear
MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
FV_LISTY, FV_ROWH = 15, 11
# THE BSS OFFSETS COME FROM NASM'S OWN MAP, not from arithmetic here.  They
# used to be hand-summed - `FV_BSS_OWN + TY_BANDSZ + TY_NGLYPH + TY_MAXFACE *
# TF_SIZE + 9 * 2 + 9` - which is a copy of apps/os88type.inc's TY_BSS layout
# living in a second file with nothing to keep the two in step.  It went stale
# the moment that layout moved: this branch's ty_gofonts banks four more words
# for the SYSTEM/FONTS descent (SPEC.md 19.8), so the sum was **two bytes
# high** and the row read `ty_nfam + 2`, which is 0.  It reported "viewer lists
# 0 of 10 installed faces" about a viewer that was listing all ten - and the
# same run's own evidence said so, Down moving the selection 1 -> 2 and a
# click on row 4 selecting 4, neither of which an empty catalogue can do.
MAP = dispapps._map("fontview")
IMAGE_END = MAP["os88_image_end"]


def u16(data, at=0):
    return data[at] | data[at + 1] << 8


def package_segment(m, slot):
    rec = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    return u16(rec, 22)


STATE = ("selected", "loaded", "face", "pending", "error", "textlen")


def fv_state(m, seg):
    """Each byte at its OWN mapped offset from the region's base. They are not
    one block: [fv_loaded], [fv_pending], [fv_error] and [fv_textlen] start
    non-zero and so live in the IMAGE, and the rest in the bss after the
    specimen's tail (SPEC.md 90.10) - which is the reason to read the map and
    not a span off os88_image_end."""
    lo = min(MAP["fv_" + n] for n in STATE)
    hi = max(MAP["fv_" + n] for n in STATE)
    b = m.read(seg * 16 + lo, hi - lo + 1)
    return {n: b[MAP["fv_" + n] - lo] for n in STATE}


def fv_quiet(m, seg):
    """Until the viewer's state AND the drive both stop moving: a face load is
    disk reads between which the state bytes can sit still."""
    os88marty.quiesce(m, lambda: (fv_state(m, seg), m.disk().get("reads")),
                      guest=1.0, what="the face load to finish")


fails = []
with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "A")
    slot = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, slot)
    # SYSTEM/FONTS and not FONTS: SPEC.md 19.8.1 moved the folder INTO SYSTEM/
    # for the ROOT's sake - the boot floppy's own window is what a person opens
    # - and ty_gofonts walks to exactly one folder. tests/unit/t_fonts.py
    # asserts BOTH halves ("the root has no FONTS folder", "SYSTEM/FONTS holds
    # every face in faces/"), so the root is where this can never be.
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "SYSTEM")
    slot = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, slot)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "FONTS")
    names = [n for n, _ in dispcp.listing(m, S)]
    installed = [n for n in names if n.endswith(".F88")]
    print("installed:", installed)
    if len(installed) != 10:
        fails.append("SYSTEM/FONTS contains %d F88 faces, not all 10" % len(installed))

    slot = dispcp.win_list(m, S)[-1]
    wx, wy, _, _ = dispcp.win_rect(m, S, slot)
    before = dispcp.win_list(m, S)
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "CHARTER.F88")
    try:
        os88marty.until(m, lambda _: len(dispcp.win_list(m, S)) > len(before),
                        "the viewer's window", poll=0.2, limit=15)
    except os88marty.MartyError:
        pass                            # ...and the next line says so
    after = dispcp.win_list(m, S)
    if len(after) <= len(before):
        raise SystemExit("fontview: CHARTER.F88 opened no new window")

    fvslot = after[-1]
    seg = package_segment(m, fvslot)
    image_end = u16(m.read(seg * 16, 32), 8)
    if image_end != IMAGE_END:
        raise SystemExit("fontview: the running image is %d bytes and the "
                         "map's is %d - a different build" % (image_end,
                                                              IMAGE_END))
    fv_quiet(m, seg)
    state = fv_state(m, seg)
    print("associated launch:", state)

    # ty_nfam, off the map.  It must agree with the directory rather than
    # merely reaching TY_MAXFAM and silently hiding a family.
    ty_nfam = seg * 16 + MAP["ty_nfam"]
    found = m.read(ty_nfam, 1)[0]
    print("catalogue rows:", found)
    if found != len(installed):
        fails.append("viewer lists %d of %d installed faces" %
                     (found, len(installed)))

    # THE LAUNCH NAME PICKED ITS OWN ROW: which row Charter is comes off the
    # viewer's own catalogue (ty_fnames, TY_NAMSZ = 13 bytes a family), so a
    # launch that fell back to row 0 fails unless Charter really is row 0.
    names = m.read(seg * 16 + MAP["ty_fnames"], 13 * max(found, 1))
    rows = [names[i * 13:(i + 1) * 13].split(b"\0")[0].decode("ascii", "replace")
            for i in range(found)]
    want = rows.index("CHARTER.F88") if "CHARTER.F88" in rows else -1
    print("Charter is row", want, "of", rows)
    if not (want >= 0 and state["selected"] == want
            and state["selected"] == state["loaded"] and state["face"] > 0
            and not state["pending"] and not state["error"]):
        fails.append("CHARTER.F88 did not become the open selected face: %r"
                     % state)

    oldlen = state["textlen"]
    m.type_text("XYZ")
    os88marty.settle(m)
    state = fv_state(m, seg)
    print("after typing XYZ:", state)
    if state["textlen"] != oldlen + 3:
        fails.append("typing changed specimen length %d -> %d, wanted %d" %
                     (oldlen, state["textlen"], oldlen + 3))
    m.key("Backspace")
    os88marty.settle(m)
    state = fv_state(m, seg)
    if state["textlen"] != oldlen + 2:
        fails.append("Backspace did not edit the specimen")

    old = state["loaded"]
    m.key("ArrowDown")
    fv_quiet(m, seg)
    os88marty.settle(m)
    state = fv_state(m, seg)
    print("after Down:", state)
    if (state["loaded"] == old or state["selected"] != state["loaded"]
            or not state["face"] or state["pending"] or state["error"]):
        fails.append("Down did not finish loading the next face: %r" % state)

    # The mouse path is separate from the arrow path: click row 4 using the
    # content origin the package banked from WM_CONTENT.
    raw = m.read(seg * 16 + MAP["fv_x"], 4)
    cx, cy = u16(raw, 0), u16(raw, 2)
    target = 4
    mo.click(cx + 12, cy + FV_LISTY + target * FV_ROWH + 4)
    fv_quiet(m, seg)
    os88marty.settle(m)
    state = fv_state(m, seg)
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
