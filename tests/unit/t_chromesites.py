#!/usr/bin/env python3
"""Every WF_FULL site in the kernel says what it means (SPEC.md 11.103.5).

    python3 tests/unit/t_chromesites.py

WF_FULL (SPEC.md 11.2) was the kernel's only "no title, no border, no shadow"
and most of its tests mean exactly that - which is what a DESK window (SPEC.md
11.103) is too. A few mean THE LATCH instead, the machine's one fullscreen
window, and must never see a widget: wm_refit's refit to the display,
wm_damage's forced "whole", wm_snap_want's opt-out of the 8-pixel snap a
widget's face depends on. The two look identical in a diff - `test byte
[bx+W_FLAGS], WF_FULL` - and a new one is written by copying an old one.

So every reference is REGISTERED here by the routine it sits in, as one of:

  CHROMELESS  "this window draws no chrome". It must be written
              `WM_CHROMELESS <reg>` - wm.inc's macro, which on kern_big is
              `test word [reg+W_FLAGS], WF_FULL | WF_DESK` (0x0408) and on
              kern_small the old byte test, so not one small byte moves.
  FULL-ONLY   "this is the fullscreen latch". It must test WF_FULL and must
              NOT name WF_DESK.
  DEF         the definition, or an assembly-time assertion about it.

and the reason goes with it. A site in neither list fails `make`, so the next
person to copy a WF_FULL test has to decide which one they meant; a listed
site that has gone fails too, so the list cannot rot into a stale comfort.

**BREAK IT ON PURPOSE**: turn wm_hit's `WM_CHROMELESS bx` back into `test byte
[bx+W_FLAGS], WF_FULL` and this fails (a CHROMELESS site that is not the
macro); add a fresh `test byte [di+W_FLAGS], WF_FULL` anywhere in wm.inc and
it fails (unregistered); widen wm_refit's and it fails (FULL-ONLY names
WF_DESK).
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from harness import check, done                           # noqa: E402

CHROMELESS, FULLONLY, DEF = "CHROMELESS", "FULL-ONLY", "DEF"

# (file, enclosing routine, ordinal within it) -> (class, reason)
REG = {
    ("kernel/wm.inc", None, 0): (DEF, "WF_FULL's own equ"),
    ("kernel/wm.inc", None, 1): (DEF, "WM_CHROMELESS's kern_big arm: the word test of WF_FULL | WF_DESK"),
    ("kernel/wm.inc", None, 2): (DEF, "WM_CHROMELESS's kern_small arm: the byte test, unchanged"),
    ("kernel/wm.inc", "wm_refit", 0): (FULLONLY, "the latch's refit to its display"),
    ("kernel/wm.inc", "wm_raise", 0): (FULLONLY, "the INCOMING window covers the screen; a desk window is never raised"),
    ("kernel/wm.inc", "wm_snap_want", 0): (FULLONLY, "a desk window KEEPS the 8-pixel snap: W_X = 7 mod 8 puts its face on the byte grid"),
    ("kernel/wm.inc", "wm_snap_want", 1): (DEF, "the one-byte-test assertion"),
    ("kernel/wm.inc", "wm_snap_want", 2): (DEF, "...and its message"),
    ("kernel/wm.inc", "wm_fullscreen", 0): (FULLONLY, "the latch's own setter"),
    ("kernel/wm.inc", "wm_hit", 0): (CHROMELESS, "every point is content: no title, no boxes"),
    ("kernel/wm.inc", "wm_win_rect", 0): (CHROMELESS, "no drop shadow: the occupied rect is the frame"),
    ("kernel/wm.inc", "wm_dmg_shadowed", 0): (CHROMELESS, "no shadow to owe"),
    ("kernel/wm.inc", "wm_content", 0): (CHROMELESS, "the frame IS the content"),
    ("kernel/wm.inc", "wm_geom", 0): (CHROMELESS, "...and so is its size"),
    ("kernel/wm.inc", "wm_clip_set", 0): (CHROMELESS, "the region is seeded with the frame"),
    ("kernel/wm.inc", "wm_seed_frame", 0): (CHROMELESS, "no shadow in the seed"),
    ("kernel/wm.inc", "wm_draw_title", 0): (CHROMELESS, "no title bar to draw"),
    ("kernel/wm.inc", "wm_chrome_relit_x", 0): (CHROMELESS, "no boxes to have lit"),
    ("kernel/wm.inc", "wm_title_set", 0): (CHROMELESS, "a retitle draws nothing"),
    # SHADOW GEOMETRY (SPEC.md 11.103.1): the two subtractions that walk every
    # visible window and assumed each one draws a shadow. Unregistered, a
    # widget's x+w column and y+h row were taken out of the dither and drawn
    # by nobody - the one class of site that names no WF_FULL to be found by
    ("kernel/wm.inc", "wm_clip_subl", 0): (CHROMELESS, "wm_occl_l's frame-and-L: no L to draw back, the frame alone"),
    ("kernel/wm.inc", "wm_clip_sub", 0): (CHROMELESS, "the occupied rect is the frame: no shadow under it"),
    ("kernel/wm.inc", "wm_damage", 0): (FULLONLY, "FULL forces 'whole' because its fill ignores WF_OWNBG; a desk window is OWNBG and gets a sub-rect"),
    ("kernel/wm.inc", "wm_cov_rect", 0): (FULLONLY, "a desk window reaches its frame through wm_su_rect's own desk arm"),
    ("kernel/wm.inc", "wm_su_take", 0): (FULLONLY, "a desk window has no WF_SAVEU and never banks"),
    ("kernel/wm.inc", "wm_draw_win", 0): (FULLONLY, "the FULL arm fills white; the desk arm is its own test ahead of it"),
    ("kernel/wm.inc", "wm_grow_erase", 0): (FULLONLY, "MAKE refuses a sizable window"),
    ("kernel/wm.inc", "wm_grow_paint", 0): (FULLONLY, "MAKE refuses a sizable window"),
    ("kernel/wm.inc", "osapi_wm_desk_x", 0): (FULLONLY, "MAKE refuses a fullscreen window (SPEC.md 11.103.9)"),
    ("kernel/instance.inc", "inst_minimize", 0): (FULLONLY, "IF_FSMIN; a DIA never minimizes its widget (SPEC.md 11.103.8)"),
    # WIN95.DRV (SPEC.md 76.14), main's Windows theme: its title door stands
    # IN FRONT of wm_draw_title_b's own test, so under that theme it is the
    # one that decides whether a window has a bar at all
    ("kernel/winmod.inc", "wn_title", 0): (CHROMELESS, "the Windows theme's title bar: none for a desk window either"),
}

LABEL = re.compile(r"^([A-Za-z_][\w]*):")
MACRO_USE = re.compile(r"^\s*WM_CHROMELESS\s+(\w+)\s*$")


def sites():
    """(file, routine, ordinal, code) for every WF_FULL / WM_CHROMELESS line.

    The routine is the last top-level label above the line; a site before any
    label (the equ, the macro body) has None. Ordinals count within it, in
    file order, so two tests in one routine are two registrations."""
    out = []
    files = sorted(glob.glob(os.path.join(ROOT, "kernel", "*.inc")))
    files.append(os.path.join(ROOT, "kernel", "kernel.asm"))
    for path in files:
        rel = os.path.relpath(path, ROOT)
        lab, seen, inmacro = None, {}, False
        for line in open(path, encoding="latin-1"):
            m = LABEL.match(line)
            if m:
                lab = m.group(1)
            code = line.split(";", 1)[0]
            if re.match(r"^\s*%macro\s+WM_CHROMELESS\b", code):
                inmacro = True
            if inmacro and re.match(r"^\s*%endmacro", code):
                inmacro = False
            if "WF_FULL" in code or MACRO_USE.match(code):
                where = None if inmacro else lab
                k = (rel, where)
                n = seen.get(k, 0)
                seen[k] = n + 1
                out.append((rel, where, n, code.strip()))
    return out


def main():
    found = sites()
    keys = set()
    for rel, lab, n, code in found:
        key = (rel, lab, n)
        keys.add(key)
        reg = REG.get(key)
        name = "%s %s#%d: %s" % (rel, lab or "(top)", n, code)
        if not check(reg is not None,
                     "registered: %s" % name,
                     why="an unclassified WF_FULL test - is it 'no chrome' "
                         "(write WM_CHROMELESS) or 'the fullscreen latch'? "
                         "SPEC.md 11.103.5; register it here with the reason"):
            continue
        cls, reason = reg
        if cls == CHROMELESS:
            check(bool(MACRO_USE.match(code)),
                  "CHROMELESS is the macro: %s" % name,
                  why="a desk window draws no chrome either: " + reason)
        elif cls == FULLONLY:
            check("WF_FULL" in code and "WF_DESK" not in code
                  and not MACRO_USE.match(code),
                  "FULL-ONLY names the latch alone: %s" % name,
                  why="the fullscreen latch must not see a widget: " + reason)
    for key in sorted(set(REG) - keys, key=str):
        check(False, "registered site still exists: %s %s#%d" % key,
              why="the registry names a WF_FULL site the kernel no longer has "
                  "- take it out, or the list rots")

    # the macro's kern_big arm IS the word test of 0x0408
    wm = open(os.path.join(ROOT, "kernel", "wm.inc"), encoding="latin-1").read()
    m = re.search(r"%macro WM_CHROMELESS 1\n%ifdef KERN_BIG\n\s*test word "
                  r"\[%1\+W_FLAGS\], WF_FULL \| WF_DESK\n%else\n\s*test byte "
                  r"\[%1\+W_FLAGS\], WF_FULL\n%endif\n%endmacro", wm)
    check(m is not None, "WM_CHROMELESS: a word test of WF_FULL|WF_DESK "
          "on kern_big, the byte test on kern_small",
          why="the CHROMELESS sites are only chromeless if the macro is")
    ev = {}
    for nm in ("WF_FULL", "WF_DESK"):
        mm = re.search(r"^%s\s+equ\s+(\S+)" % nm, wm, re.M)
        ev[nm] = int(mm.group(1), 0) if mm else None
    v = (ev["WF_FULL"] or 0) | (ev["WF_DESK"] or 0)
    check(v == 0x0408, "WF_FULL | WF_DESK = 0x0408",
          why="SPEC.md 11.103's two bits", got=hex(v), want="0x408")
    return done("t_chromesites: %d WF_FULL sites, each classified" % len(found))


if __name__ == "__main__":
    sys.exit(main())
