#!/usr/bin/env python3
"""Does the TWO-COLOUR declaration reach the record, and clear of the shape?

    make && python3 tests/win1bpp.py [--machine os8088_xt_vga]

SPEC.md 11.96.17 carries "every pixel of my content is colour 0 or 15" on
OSAPI_WM_SAVEU's AL bit 1 and stores it as WF_1BPP - bit 13, in the HIGH BYTE
of W_FLAGS beside WF_NOANIM and WF_STALE. That byte is also SPEC.md 7.2.1's
cursor shape, and the failure mode has shipped once already: WF_STALE's own
banner records the day it sat at bit 8, where it WAS CUR_CROSSSH, so a window
handed a new listing wore the aiming crosshair and a package asking for one
refused itself a cache.

Two questions, in a launch each because the subjects live in different
folders and a window opened over the Disk window swallows the next click:

  DECLARED  Note Pad's record carries WF_SAVEU *and* WF_1BPP, and nothing
            else does. Note Pad is the only window in the tree entitled to
            the second - its content is CBLACK on CWHITE and the only other
            thing inside it is os88ui_sbar's track, which is gfx_fill_gray, a
            50% dither of 15 and 0 rather than a grey.

  SHAPE     Missile Command still gets its crosshair. It asks for
            OSAPI_CUR_CROSS and claims no depth, so it is the half Note Pad
            cannot cover: that defining a third kernel bit in that byte did
            not cost a shape its value.

**What this does NOT prove, said out loud.** The sharp version of the
coupling - wm_cursor KEEPING the kernel's bits while mou_apply DROPS them,
and one mask gaining WF_1BPP without the other - needs a window carrying a
shape AND a depth claim at once, and no shipped window does: Note Pad has the
flag and no shape, Missile Command has a shape and no flag. Neither control
below can fail on a one-sided mask, so this gate would not catch one.

That is deliberate rather than a gap left open. Both masks derive from
WF_HIBITS in kernel/wm.inc, and an assembly-time `%error` checks it does not
overlap the shape field, so a one-sided mask cannot be written. The
expression is the protection; this gate covers what an expression cannot -
that the VALUE in the record is the one the application asked for. The
control that DOES fire is Note Pad passing OSAPI_SAVEU_ON alone, which fails
DECLARED.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88geom, os88sym, dispcp

MACHINE = "os8088_5150_cga_gla"
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

S = os88sym.linear
CUR_NSHAPE = 2
fails = []


def open_under(folder, pkg):
    """Boot, walk B: -> `folder` -> `pkg`, and hand back the window records."""
    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=MACHINE) as m:
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, "B")
        for name in (folder, pkg):
            w = dispcp.win_list(m, S)
            wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, name)
            os88marty.settle(m)
        return os88geom.windows(m)


def sweep(wins, label):
    """Nothing anywhere may read back a shape the kernel does not ship."""
    for x in wins:
        ok = x.shape < CUR_NSHAPE
        print("  %-14s flags=%04X 1bpp=%-5s shape=%d %s"
              % (x.title, x.flags, x.mono, x.shape,
                 "ok" if ok else "*** NOT A SHAPE"))
        if not ok:
            fails.append("%s: %s reads shape %d, past CUR_NSHAPE - a kernel "
                         "flag leaked into SPEC.md 7.2.1's field"
                         % (label, x.title, x.shape))


print("--- DECLARED")
wins = open_under("APPS", "NOTEPAD.O88")
np = [x for x in wins if "ote" in x.title]
if not np:
    sys.exit("Note Pad did not open - windows: %r" % wins)
np = np[-1]
if not np.promises:
    fails.append("DECLARED: Note Pad does not carry WF_SAVEU")
if not np.mono:
    fails.append("DECLARED: Note Pad does not carry WF_1BPP")
for x in wins:
    if x is not np and x.mono:
        fails.append("DECLARED: %s claims WF_1BPP and never asked" % x.title)
sweep(wins, "DECLARED")

print("--- SHAPE")
wins = open_under("GAMES", "MISSILE.O88")
mc = [x for x in wins if "issile" in x.title or "Comm" in x.title]
if not mc:
    sys.exit("Missile Command did not open - windows: %r" % wins)
mc = mc[-1]
if mc.shape != 1:
    fails.append("SHAPE: Missile Command reads shape %d, wanted 1 - the two "
                 "masks disagree about WF_HIBITS" % mc.shape)
if mc.mono:
    fails.append("SHAPE: Missile Command claims WF_1BPP and never asked")
sweep(wins, "SHAPE")

print()
if fails:
    for f in fails:
        print("FAIL  " + f)
    sys.exit(1)
print("PASS  declared where it was asked for, and the shape field is clean")
