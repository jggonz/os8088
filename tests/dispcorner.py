#!/usr/bin/env python3
"""REPORTED ARTIFACTS, LOCALISED (a corner pixel, and two drags across a seam)

    make && python3 tests/dispcorner.py
    python3 tests/dispcorner.py --only d --machine os8088_5150_both_gla

Reported by another agent, the first two as "incremental differs from a full
repaint":

  A. a window's (W_X, W_Y+W_H) - the drop shadow's bottom-LEFT corner - reads
     differently after an incremental draw than after a full repaint. One
     pixel, reproducible with `hello`.
  B. dragging across an extended desktop's seam leaves pixels a repaint
     disagrees with - hundreds of them.
  D. reported off the FIELD MACHINE, and in the field's own words: drag an
     UNRESIZABLE window into the corner where the Hercules and the CGA join,
     and that area is never repainted after the window is moved. It is a
     two-card leg like B and it needs its own subject and its own assertion -
     see the block that runs it.

Both are asked the same way and it is the way this tree asks every question of
this shape: do the thing, capture, force a full repaint, diff. What this adds
is that it prints WHERE and shows WHAT - a count says a defect exists, a
bounding box says which rect owns it, and a picture says which of the things
in that rect it is.

EVERY VARIABLE IS A FLAG, and that is the correction this file has already
needed once. B ran after A, so it inherited A's open windows without saying
so - and it FAILED with them and PASSED without them, which reads as a flaky
test rather than as the discriminator it is. What is under the dragged window
(--under) and where it is dragged to (--dest) are named now, so a run states
its own conditions.

THE FORCED REPAINT IS THE WHOLE CHECK, so it is poked and then VERIFIED. This
opened and closed the Control Panel, on the belief that closing a window ends
in wm_paint_all - it ends in wm_paint_dmg (SPEC.md 11.91), the incremental
path, so both captures came off incremental draws and every run was comparing
the machine with itself. It writes [cp_dirty], whose only consumer is
ui_task's wm_paint_all (SPEC.md 31.2), and reads the flag back to see that the
consumer ran. Each operation also has to MOVE some pixels, or its 0 is
vacuous: tools/notepad/pixcheck.py and tools/sucheck.py are the two places
this tree has already paid for a gate that could only pass.

The mouse is parked at the same place for every capture so the arrow cancels
out of every comparison.

C came later and is a third question of the same shape asked about a VERTICAL
drag, where a drop's dy shifts a screen-phased dither's phase inside the drag
cache (SPEC.md 11.96.13.1). That residue is ACCEPTED now, so C does not ask
whether the two captures agree - it asks which KIND of difference it is, and
`dither_split` is what makes that a classification rather than a tolerance.
`--selftest` checks that rule against synthetic captures with no emulator at
all, including the two cases that would make it a way of not looking.
"""
import argparse
import sys
import time

sys.path.insert(0, "/home/user/os8088/tools")
sys.path.insert(0, "/home/user/os8088/tests")

from os88geom import (VID_CTX_SZ, VID_CTX_VX,          # noqa: E402
                      VID_CTX_VY, VID_CTX_KIND, VID_CTX_CH)
# SPEC.md 39.14's per-display record: DERIVED from VID_CTX_W and never
# written down here. This file spelled it `42 + 36`, which is the
# VID_CTX_W = 18 layout - two bytes early, and what sits there is
# display 1's vid_chm8, so the seam read 192 instead of 720.
import os88geom                                             # noqa: E402
import os88marty, os88mouse, os88sym, dispcp                # noqa: E402

def S(name, _d=[()]):
    """Kernel symbol -> linear address, THROUGH THE KNOB THE BUILD WAS MADE
    WITH. os88sym asserts its map against build/kernel.bin, so a reference
    build (`make DRAGCACHE=0`) fails outright rather than handing back a
    plausible wrong address - which is the right behaviour and means every
    A/B against a knob has to say which knob. --define is that."""
    return os88sym.linear(name, defines=_d[0])


TITLE_H, MBAR_H = 18, 20
PARK = (4, MBAR_H + 4)


snapw = os88geom.snapw          # SPEC.md 11.94.5, mirrored once

# `hello` IS THE SUBJECT BECAUSE IT DOES NOTHING - no worker, no animation, a
# 240x90 window that draws one string. That is what makes "capture, force a
# repaint, diff" mean anything at all: a screen that never settles cannot be
# compared with itself. This file reached row numbers for it (`APPS_ROW = 1`,
# `HELLO_ROW = 3`) and they named GAMES and MISSILE.O88 - Missile Command, a
# live arcade game with a worker drawing every tick - so it reported the
# game's own motion as a kernel artifact, twice, with different counts.
# dispcp.open_named asks the guest which row a NAME is on (SPEC.md 19.4).
HELLO_DIR, HELLO_FILE = "APPS", "HELLO.O88"


def u16(b, i=0): return b[i] | (b[i + 1] << 8)


def shot(m, cards):
    m.pause()
    out = {c: m.fbuf(card=c) for c in cards}
    m.run()
    return out


def diff(a, b, w):
    """[(x, y)] where two captures of the same card differ."""
    return [((i // 3) % w, (i // 3) // w)
            for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3]]


def bbox(d):
    """The differing pixels' bounding box - the SHAPE, which is what says
    which rect owns them. A count cannot, and the first six cannot either."""
    if not d:
        return None
    xs = [p[0] for p in d]
    ys = [p[1] for p in d]
    return (min(xs), min(ys), max(xs), max(ys))


def px(cap, w, x, y):
    o = (y * w + x) * 3
    return cap[o:o + 3]


DITHER_MIN = 16                 # pixels in a component before "these are a
                                # dither" is a statement about anything - see
                                # the floor paragraph in dither_split


def dither_phase(inc, full, comp, w):
    """Is this component `gfx_fill_gray`'s dither, one row out of phase?

    THE TEST IS THE PRIMITIVE'S OWN DEFINITION, NOT THE SHAPE OF THE
    DIFFERENCE. gfx_fill_gray is `(x + y) even = one colour, odd = the other`,
    phased on ABSOLUTE screen coordinates - deliberately, so that wm_dmg_gray
    can draw the desktop as fragments and get the pixels drawing it whole
    would give (SPEC.md 11.91.1). So residue is exactly this: the full repaint
    follows that parity, and the incremental capture is its complement at
    every pixel. Nothing about the region's outline comes into it.

    The two colours are taken FROM THE DATA rather than assumed to be black
    and white: this has to hold for a dithered control in any palette, and
    the constraint is not weakened by deriving them, because a component of
    two or more pixels always contains both parities - 4-connected
    neighbours flip (x + y) - so both are pinned by the first pixel and then
    CHECKED against every other.

    THE ONLY FLOOR LEFT IS AN AREA, and dropping the other two is the point of
    this routine rather than an accident of it. It used to require the
    component to FILL its bounding rectangle and to be at least 3x3, which is
    a fact about scroll-bar tracks with nothing in front of them - and a real
    track has a THUMB in it. The differing pixels are then one C-shaped
    component wrapping a solid block, 746 px in a 12x118 box it fills 53% of,
    and all three shape rules fail at once on the thumb's interior. That is
    what this file reported as `746 real` for two branches (see 30.3.3's merge)
    on a residue that is textbook 11.96.13.1: 373 white->black and 373
    black->white, every repaint pixel on parity, not one exception.

    A 1px-wide strip is legal now and that is not a hole: it is what the track
    beside a thumb IS, and a stale LINE - a drop shadow, a frame edge - is
    solid, so consecutive pixels along it are the same colour and parity
    refuses it on the second pixel. The area floor is what keeps a single
    stray pixel out, which matters here more than anywhere: leg A exists to
    watch ONE pixel at (W_X, W_Y+W_H), and two colours differing at one pixel
    satisfy any parity rule you like by coin flip.
    """
    if len(comp) < DITHER_MIN:
        return False
    x, y = comp[0]
    even, odd = px(full, w, x, y), px(inc, w, x, y)
    if (x + y) % 2:                 # comp[0] sits at ODD parity, so the
        even, odd = odd, even       # repaint's colour there is the odd one
    if even == odd:                 # one colour is not a dither - which is
        return False                # what a region the repaint drew SOLID
                                    # looks like from here
    for x, y in comp:
        if (x + y) % 2:
            if px(full, w, x, y) != odd or px(inc, w, x, y) != even:
                return False
        elif px(full, w, x, y) != even or px(inc, w, x, y) != odd:
            return False
    return True


def dither_split(inc, full, pts, w, h):
    """Split a differing-pixel set into DITHER-PHASE RESIDUE and a REAL
    disagreement (SPEC.md 11.96.13.1).

    A drop's dy is not snapped, so the drag cache can replay a window's own
    dithered controls an odd number of rows from where they were banked and
    every pixel of them comes back inverted. That is accepted - an inverted
    50% dither is the same 50% grey, inside the solid chrome of a scroll-bar
    track, with no adjacent dither to seam against - so this file has to be
    able to say "that, and only that" about a difference.

    IT IS A CLASSIFIER AND NOT A TOLERANCE, and the difference is the whole
    reason it is allowed to exist. Subtracting 1,416 from a count, or
    comparing against a threshold, would pass a kernel that had lost the
    scroll-bar track altogether. What is required instead is that the pixels
    BE the dither in the other phase, which `dither_phase` above asks of every
    one of them.

    The split is per 4-connected COMPONENT and all-or-nothing inside one, so a
    region that is part residue and part defect is reported whole as real -
    the safe direction, and it keeps a scattering of accidentally-on-parity
    pixels from being excused one at a time.
    """
    seen = set(pts)
    residue, real = [], []
    while seen:
        # the component this pixel belongs to, 4-connected
        start = next(iter(seen))
        comp, stack = [], [start]
        seen.discard(start)
        while stack:
            x, y = stack.pop()
            comp.append((x, y))
            for n in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if n in seen:
                    seen.discard(n)
                    stack.append(n)
        (residue if dither_phase(inc, full, comp, w) else real).extend(comp)
    return residue, real


def selftest():
    """dither_split, CHECKED WITHOUT A GUEST - `python3 tests/dispcorner.py
    --selftest`, no emulator, no floppy, no build.

    A classifier that excuses a class of difference is a way of not looking
    unless it can be shown to excuse ONLY that class, and the two cases that
    matter are both here: a track that VANISHED has the same pixel count as
    one whose phase slipped, and a checkerboard drawn in the wrong colour has
    the same shape. Neither may pass. This is the tautology guard
    tools/notepad/pixcheck.py exists to state, applied to the rule rather
    than to the run.
    """
    W, H = 64, 64
    BLK, WHT, RED = bytes([0, 0, 0]), bytes(b"\xff" * 3), bytes([255, 0, 0])

    def blank():
        return bytearray(bytes([128, 128, 128]) * W * H)

    def put(cap, x, y, v):
        o = (y * W + x) * 3
        cap[o:o + 3] = v

    def dset(a, b):
        return [((i // 3) % W, (i // 3) // W)
                for i in range(0, len(a), 3) if a[i:i + 3] != b[i:i + 3]]

    def track(cap, phase):          # 12 x 31, a scroll-bar track's shape
        for y in range(10, 41):
            for x in range(10, 22):
                put(cap, x, y, WHT if (x + y) % 2 == phase else BLK)

    cases, bad = [], []
    inc, full = blank(), blank()                    # 1: the phase slipped
    track(full, 0); track(inc, 1)
    cases.append(("phase slipped", inc, full, 372, 0))

    inc, full = blank(), blank()                    # 2: stale solid content
    for y in range(30, 40):
        for x in range(30, 45):
            put(inc, x, y, RED)
    cases.append(("stale block", inc, full, 0, 150))

    inc, full = blank(), blank()                    # 3: both, in one capture
    track(full, 0); track(inc, 1)
    for y in range(50, 58):
        for x in range(30, 45):
            put(inc, x, y, RED)
    cases.append(("slip + block", inc, full, 372, 120))

    inc, full = blank(), blank()                    # 4: right shape, wrong ink
    track(full, 0)
    for y in range(10, 41):
        for x in range(10, 22):
            put(inc, x, y, RED if (x + y) % 2 else BLK)
    cases.append(("wrong colour", inc, full, 0, 372))

    inc, full = blank(), blank()                    # 5: THE TOLERANCE TRAP -
    track(full, 0)                                  # the track is simply GONE,
    cases.append(("track vanished", inc, full, 0, 372))   # at the same count

    inc, full = blank(), blank()                    # 6: too small to mean it
    for y in (20, 21):
        for x in (20, 21):
            put(full, x, y, WHT if (x + y) % 2 == 0 else BLK)
            put(inc, x, y, WHT if (x + y) % 2 else BLK)
    cases.append(("2x2 speck", inc, full, 0, 4))

    # 7: THE SHAPE THIS FILE ACTUALLY MEETS, and the one it used to fail. A
    # real track has a THUMB in it: the differing pixels wrap around a solid
    # block as one C-shaped component that fills barely half its bounding box,
    # which is what the old "must fill its rectangle" rule refused. 12x31 less
    # a 10x15 thumb = 372 - 150.
    inc, full = blank(), blank()
    track(full, 0); track(inc, 1)
    for y in range(18, 33):
        for x in range(11, 21):
            put(full, x, y, WHT)
            put(inc, x, y, WHT)
    cases.append(("track around a thumb", inc, full, 222, 0))

    # 8: ...and the guard for having dropped that rule. A RAGGED two-colour
    # smear, in the track's own ink, big enough to clear the floor - the exact
    # thing "fills a rectangle" used to catch. Parity is what catches it now:
    # a solid run has two neighbours the same colour, so the second pixel of
    # it is already off phase.
    inc, full = blank(), blank()
    for i, y in enumerate(range(10, 30)):
        for x in range(10, 14 + (i % 3)):
            put(full, x, y, BLK)
            put(inc, x, y, WHT)
    cases.append(("ragged smear", inc, full, 0,
                  sum(4 + (i % 3) for i in range(20))))

    for name, inc, full, want_r, want_x in cases:
        r, x = dither_split(inc, full, dset(inc, full), W, H)
        ok = (len(r), len(x)) == (want_r, want_x)
        print("  %-15s residue %3d real %3d   %s"
              % (name, len(r), len(x), "ok" if ok else
                 "FAIL (wanted %d/%d)" % (want_r, want_x)))
        if not ok:
            bad.append(name)
    print("selftest: " + ("FAIL: " + ", ".join(bad) if bad else
                          "dither_split excuses a phase slip and nothing else"))
    return 1 if bad else 0


def classify(inc, full, d, w, h, tag, label, bad, allow_dither):
    """Report a differing set by CLASS and rule on it. `allow_dither` is the
    only knob, and it is set from the drag the leg actually took - an even dy
    cannot produce residue, so it is never allowed to excuse one."""
    if not d:
        print("%s: %-22s 0 differing pixel(s)" % (tag, label))
        return [], []
    res, real = dither_split(inc, full, d, w, h)
    print("%s: %-22s %d differing pixel(s): %d dither-phase, %d real  "
          "bbox %r" % (tag, label, len(d), len(res), len(real), bbox(d)))
    if real:
        bad.append("%s/%s: %d pixels differ and are NOT a dither's phase "
                   "(bbox %r)" % (tag, label, len(real), bbox(real)))
    if res and not allow_dither:
        bad.append("%s/%s: %d pixels of dither-phase residue where the drag "
                   "cannot make any (bbox %r)" % (tag, label, len(res),
                                                  bbox(res)))
    return res, real


def crop_png(path, cap, w, h, box, margin=24):
    """Write the differing region plus `margin` of context, so the picture can
    be read against what is around it."""
    x0 = max(0, box[0] - margin)
    y0 = max(0, box[1] - margin)
    x1 = min(w - 1, box[2] + margin)
    y1 = min(h - 1, box[3] + margin)
    rows = bytearray()
    for y in range(y0, y1 + 1):
        o = (y * w + x0) * 3
        rows += cap[o:o + (x1 - x0 + 1) * 3]
    os88marty.write_png_rgb(path, x1 - x0 + 1, y1 - y0 + 1, bytes(rows))
    return (x0, y0, x1, y1)


def repaint(m, mo, card):
    """FORCE A FULL REPAINT - and this is the whole check, so it may not be
    something that merely looks like one.

    It used to open and close the Control Panel, described here as "it ends in
    wm_paint_all". IT DOES NOT: closing a window is wm_destroy, which passes
    the vacated rect to wm_paint_dmg (SPEC.md 11.91) - the INCREMENTAL path.
    So both captures came off incremental draws and the run compared the
    machine with itself, which is tools/notepad/pixcheck.py's tautology and
    tools/sucheck.py's vacuous pass in a third place. It reports 0 either way.

    [cp_dirty] is the one flag in the machine whose only consumer IS
    wm_paint_all (ui.inc's .chk_cp, SPEC.md 31.2). Poking it needs no window,
    no click and no floppy write, and there is nothing between the flag and
    the call for it to mean instead.
    """
    # ...AND wm_paint_all IS NOT ENOUGH ON ITS OWN. It draws every window
    # through wm_draw_win, which puts a valid raise cache back INSTEAD of
    # running W_PAINT (SPEC.md 11.96) - so inside a window the "full repaint"
    # can be a byte copy of the very capture it is being compared against.
    # The desktop dither is drawn directly and is honest either way, which is
    # enough for (A); (B)'s pixels are window content and it is not.
    # tools/notepad/pixcheck.py's fix, verbatim: clear WF_SAVEU, which
    # wm_su_ck tests, so an already-banked cache is invalidated too.
    saved = []
    for w in dispcp.win_list(m, S):
        a = S("wm_wins") + w * dispcp.WIN_SIZE + dispcp.W_FLAGS
        f = m.read(a, 2)
        saved.append((a, f))
        m.write(a, bytes([f[0] & ~dispcp.WF_SAVEU & 0xFF, f[1]]))
    m.write(S("cp_dirty"), b"\x01")
    mo.to(*PARK)
    os88marty.settle(m, card=card)
    # ...AND CHECK IT WAS SPENT. The flag is cleared by the branch that calls
    # wm_paint_all and by nothing else, so reading 0 back is the observation
    # that the repaint ran - not an inference from the poke having happened.
    spent = m.read(S("cp_dirty"), 1)[0]
    for a, f in saved:              # ...PUT THE FLAG BACK, or every operation
        m.write(a, f)               # after this one is testing a kernel with
                                    # the drag cache and the raise cache
                                    # switched off. Writing W_FLAGS directly
                                    # skips wm_saveu_set, so the claim itself
                                    # survives and is live again the moment
                                    # the flag is - which is only SAFE because
                                    # the subject is inert (above): a cache
                                    # banked before this repaint describes the
                                    # same pixels after it. A window whose
                                    # content moved would need the claim
                                    # dropped, not the flag masked.
    if spent != 0:
        raise RuntimeError("[cp_dirty] is still set: ui_task never reached "
                           ".chk_cp, so NO full repaint happened and every "
                           "figure below would be a comparison with itself")


def probe(m, mo, pri, pw, ph, label, bad, prev, corner=None, tag="A",
          allow_dither=False):
    """Do the capture / force / capture / diff, below the menu bar.

    `prev` is a one-element list holding the previous probe's post-repaint
    capture: the operation just performed must have MOVED some pixels, or the
    0 this is about to print is vacuous and says so.

    `allow_dither` says whether THIS leg's drag could have shifted a
    screen-phased dither's phase (SPEC.md 11.96.13.1). It is decided from the
    dy the record actually took, never from the one asked for, and it excuses
    only pixels dither_split can prove are that.
    """
    mo.to(*PARK)
    os88marty.settle(m, card=pri)
    inc = shot(m, (pri,))[pri][2]
    base = MBAR_H * pw * 3
    if prev[0] is not None:
        n = len(diff(prev[0][base:], inc[base:], pw))
        if not n:
            bad.append("%s/%s: the operation changed NO pixel, so the 0 below "
                       "is vacuous" % (tag, label))
        else:
            print("%s: %-22s (moved %d px)" % (tag, label, n))
    repaint(m, mo, pri)
    full = shot(m, (pri,))[pri][2]
    prev[0] = full
    d = [(x, y + MBAR_H) for x, y in diff(inc[base:], full[base:], pw)]
    if corner is not None and corner in d:
        bad.append("%s/%s: the shadow corner %r differs"
                   % (tag, label, corner))
    classify(inc, full, d, pw, ph, tag, label, bad, allow_dither)
    if d:
        crop_png("/tmp/corner%s-%s-inc.png" % (tag, label), inc, pw, ph,
                 bbox(d))
        crop_png("/tmp/corner%s-%s-full.png" % (tag, label), full, pw, ph,
                 bbox(d))
    return d


def wins(m, tag=""):
    """Every visible window and its rect - printed, because 'which window did
    this drag actually take hold of' has already been guessed wrong once."""
    out = [(w, dispcp.win_rect(m, S, w)) for w in dispcp.win_list(m, S)]
    if tag:
        print("  %s: %s" % (tag, ", ".join(
            "%d=(%d,%d %dx%d)" % (w, r[0], r[1], r[2], r[3]) for w, r in out)))
    return out


def launch_hello(m, mo, pri):
    """A Disk window on B:, stepped into APPS, and HELLO launched out of it.
    Returns (disk slot, hello slot)."""
    dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
    disk = dispcp.win_list(m, S)[-1]
    bx, by = dispcp.win_rect(m, S, disk)[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, bx, by, HELLO_DIR, card=pri)
    bx, by = dispcp.win_rect(m, S, disk)[:2]
    dispcp.open_named(m, mo, S, os88marty.settle, bx, by, HELLO_FILE, card=pri)
    time.sleep(4)
    other = [x for x in dispcp.win_list(m, S) if x != disk]
    if not other:
        sys.exit("%s did not launch out of %s" % (HELLO_FILE, HELLO_DIR))
    return disk, other[-1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_xt_vga_herc")
    ap.add_argument("--only", choices=("a", "b", "c", "d"), default=None)
    ap.add_argument("--under", choices=("none", "hello"), default="none",
                    help="B: what is UNDER the dragged window")
    ap.add_argument("--dest", choices=("seam", "near", "far"), default="seam",
                    help="B: drag ONTO the seam, stay on the primary, or take "
                         "the window's ORIGIN across onto the second display "
                         "- which is a different case, because wm_dc_take "
                         "refuses a drag whose two ends differ in DEPTH "
                         "(SPEC.md 11.96.12) and the origin is what picks it")
    ap.add_argument("--single", action="store_true",
                    help="B: leave the desktop single-display")
    ap.add_argument("--mode", choices=("right", "below"), default="right",
                    help="B: which of SPEC.md 39.19.2's arrangements")
    ap.add_argument("--primary", choices=("vga", "herc", "cga"), default=None,
                    help="B: make this adapter the primary first. The primary "
                         "sits at the virtual origin (SPEC.md 39.19.2), so "
                         "this is what puts the OTHER monitor on the left - "
                         "and it changes the DEPTH either side of the seam, "
                         "which is what wm_dc_take branches on (11.96.12)")
    ap.add_argument("--define", action="append", default=[],
                    help="a NASM define this build was made with, e.g. "
                         "--define NODRAGCACHE for `make DRAGCACHE=0`. "
                         "Symbol resolution needs it or it refuses.")
    ap.add_argument("--selftest", action="store_true",
                    help="check dither_split against synthetic captures and "
                         "exit - no emulator, no build (SPEC.md 11.96.13.1)")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    S.__defaults__[0][0] = tuple(a.define)
    # THE CONTROL BUILD EXCUSES NOTHING. With the drag cache compiled out
    # there is no replay to carry a stale dither's phase - the window redraws
    # in full at the new place - so SPEC.md 11.96.13.1's residue must read 0
    # on every leg, whatever dy the drop took.
    nodc = "NODRAGCACHE" in a.define
    bad = []

    with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                          machine=a.machine, boot=False) as m:
        cards = {c["idx"]: c for c in m.cards()}
        m.run(); os88marty.settle(m, gate=os88marty.desktop_up)
        mo = os88mouse.Mouse(marty=m)
        pri = [i for i, c in cards.items()
               if c["type"] == dispcp.KIND_CARD[m.read(S("vid_kind"), 1)[0]]][0]
        other = [i for i in cards if i != pri]
        sec = other[0] if other else pri     # ONE-CARD MACHINES RUN LEGS A AND
                                             # C: neither needs a seam, and the
                                             # dither is (x+y)-phased on every
                                             # adapter - so 11.96.13.1 has to
                                             # be checked on the two MONO ones
                                             # as well, where a "grey" really
                                             # is a checkerboard
        pw, ph = m.fbuf(card=pri)[:2]

        # --- A: the shadow's bottom-left corner ----------------------------
        #
        # SWEPT OVER OPERATIONS, because the report does not say which one it
        # was and a raise alone answers 0. Every one of these takes a
        # DIFFERENT path to the glass - wm_show, wm_raise, ui_drag +
        # wm_paint_dmg, wm_hide + wm_paint_dmg - and only the last two dither
        # any desktop at all, which is what would have to disagree for a
        # corner nothing writes to come out differently.
        if a.only in (None, "a"):       # ...and NOT on --only c, which was
                                        # running A as well and saying nothing
            disk, h = launch_hello(m, mo, pri)
            hx, hy, hw, hh = dispcp.win_rect(m, S, h)
            print("%s at (%d,%d) %dx%d - its shadow corner is (%d,%d)"
                  % (HELLO_FILE, hx, hy, hw, hh, hx, hy + hh))
            want = (snapw(240), 90)         # hl_tpl, THROUGH SPEC.md 11.94.5's
            if (hw, hh) != want:            # size snap - the kernel rounds a
                sys.exit("that is not %s: hl_tpl is %dx%d after 11.94.5 and "
                         "this is %dx%d"    # frame down until its CONTENT is
                         % (HELLO_FILE, want[0], want[1], hw, hh))
                                            # whole bytes, so 240 lands at 242
                                            # and a literal here reads as
                                            # "another PROGRAM", which is what
                                            # this check exists to say

            prev = [None]           # the last check's post-repaint capture
            # SPEC.md 11.96.13.1's residue is possible on this leg from the
            # moment the two windows are PARTED, and saying otherwise was the
            # second half of what this file got wrong. The reasoning written
            # here was "hello draws one string and has no dithered control in
            # it" - true of hello, and this leg has a DISK WINDOW on screen as
            # well, with a scroll-bar track in it that gfx_fill_gray draws.
            # The parting drag below moves that window by whatever dy it takes,
            # and an odd one inverts the track for the rest of the session
            # (wm_dc_done keeps the cache as an ordinary raise cache), so the
            # raise after it replays an inverted track and this file called it
            # a defect. The flag is decided from the dy the RECORD took, which
            # is the rule the rest of the file already follows.
            dither_ok = [False]

            def check(label):
                mo.to(*PARK)
                os88marty.settle(m, card=pri)
                inc = shot(m, (pri,))[pri][2]
                # ...AND THE OPERATION DID SOMETHING. A run of zeroes is worth
                # nothing until the same comparison has been shown to report
                # something else (tools/notepad/pixcheck.py's rule), and the
                # cheapest proof is already in hand: the screen before this
                # operation against the screen after it.
                if prev[0] is not None:
                    n = len(diff(prev[0][MBAR_H * pw * 3:],
                                 inc[MBAR_H * pw * 3:], pw))
                    if not n:
                        bad.append("A/%s: the operation changed NO pixel, so "
                                   "the 0 below is vacuous" % label)
                    else:
                        print("A: %-10s (moved %d px)" % (label, n))
                repaint(m, mo, pri)
                full = shot(m, (pri,))[pri][2]
                prev[0] = full
                # BELOW THE MENU BAR. The clock lives at the right of it and
                # ticks between two captures seconds apart, which the first
                # run of this reported as 28 differing pixels at (624..629,
                # 6..9) - the harness's own, and exactly the kind of thing a
                # COUNT would have let through as "the artifact reproduced".
                base = MBAR_H * pw * 3
                d = [(x, y + MBAR_H)
                     for x, y in diff(inc[base:], full[base:], pw)]
                r = dispcp.win_rect(m, S, h)          # RE-READ: a drag moved it
                corner = (r[0], r[1] + r[3])
                for p in d[:8]:
                    print("     %r%s" % (p, "  <-- (W_X, W_Y+W_H)"
                                         if p == corner else ""))
                if corner in d:
                    bad.append("A/%s: the shadow corner %r differs"
                               % (label, corner))
                # ...and whether a dither's phase may be excused is
                # dither_ok, set from the parting drag's ACTUAL dy below.
                # Before that drag it is False and a rect that looks like
                # residue would be a finding about some other window.
                classify(inc, full, d, pw, ph, "A", label, bad,
                         dither_ok[0])
                if d:
                    crop_png("/tmp/cornerA-%s-inc.png" % label,
                             inc, pw, ph, bbox(d))
                    crop_png("/tmp/cornerA-%s-full.png" % label,
                             full, pw, ph, bbox(d))

            check("launch")                 # wm_show: reveals nothing (11.90)

            # ...AND NOW SEPARATE THE TWO WINDOWS, because hello opens INSIDE
            # the Disk window's rect (hl_tpl (200,150) against fm_tpl
            # (103,80) 320x200) and a click aimed at hello's title bar lands
            # on whatever is on top of it. That is tools/sucheck.py's
            # hard-coded (300,40) exactly: the raise raised the wrong window,
            # the drag pressed on a Disk row instead of a title bar, and the
            # window never moved - which the "did this operation move any
            # pixels" guard caught and a bare 0 would not have.
            dr = dispcp.win_rect(m, S, disk)
            disk_y0 = dr[1]                 # ...banked: the parity that
                                            # matters is the one the RECORD
                                            # took, not the one asked for
            mo.drag(dr[0] + dr[2] // 2, dr[1] + TITLE_H // 2,
                    dr[0] + dr[2] // 2, hy + hh + 20 + TITLE_H // 2)
            os88marty.settle(m, card=pri)
            dr = dispcp.win_rect(m, S, disk)
            hx, hy, hw, hh = dispcp.win_rect(m, S, h)
            if not (dr[1] > hy + hh or hy > dr[1] + dr[3]):
                sys.exit("the two windows still overlap: hello (%d,%d %dx%d) "
                         "and Disk (%d,%d %dx%d) - every click below would be "
                         "aimed at whichever is on top"
                         % (hx, hy, hw, hh, dr[0], dr[1], dr[2], dr[3]))
            parted_dy = dr[1] - disk_y0
            dither_ok[0] = bool(parted_dy % 2)
            print("   parted: hello y %d..%d, Disk y %d..%d - the Disk window "
                  "moved %+d, so its dither %s slip"
                  % (hy, hy + hh, dr[1], dr[1] + dr[3], parted_dy,
                     "CAN" if dither_ok[0] else "cannot"))

            mo.click(dr[0] + 60, dr[1] + TITLE_H // 2)
            os88marty.settle(m, card=pri)
            mo.click(hx + hw // 2, hy + TITLE_H // 2)
            os88marty.settle(m, card=pri)
            check("raise")                  # wm_raise: one window, no desktop

            def hdrag(dx, dy):
                """...AND SETTLE AFTERWARDS. Without it the capture can be
                taken while ui_drag is still tracking - the release not yet
                decoded off the 1200-baud UART - and what is on the glass is
                then an XOR outline over an unmoved window. The self-check
                above is what caught that: two legs reported 0 differing
                pixels because the operation had changed NO pixel."""
                r = dispcp.win_rect(m, S, h)
                mo.drag(r[0] + r[2] // 2, r[1] + TITLE_H // 2,
                        r[0] + r[2] // 2 + dx, r[1] + TITLE_H // 2 + dy)
                os88marty.settle(m, card=pri)
                n = dispcp.win_rect(m, S, h)
                print("   drag %+d%+d: (%d,%d) -> (%d,%d)"
                      % (dx, dy, r[0], r[1], n[0], n[1]))

            hdrag(+56, +40)                 # ui_drag: the union of both rects,
            check("drag")                   # and the first path that dithers

            hdrag(-56, -40)
            check("dragback")               # ...and back over its own shadow

            dr = dispcp.win_rect(m, S, disk)
            mo.click(dr[0] + 10, dr[1] + TITLE_H // 2)  # the close box, LEFT
            os88marty.settle(m, card=pri)
            check("closedisk")              # wm_hide: the vacated rect

        # --- C: THE DITHER'S PHASE ACROSS A VERTICAL DRAG ------------------
        #
        # SPEC.md 11.96.12's drag cache replays a window's banked content at
        # its new position. wm_dc_take refuses when the x BYTE PHASE would
        # change (`test al, 7`) and when the DEPTH would, and says in as many
        # words that "Any dy is free - rows are whole".
        #
        # That is true of the framebuffer's layout and false of a DITHER.
        # gfx_fill_gray is "(x+y) even = white, odd = black" - phased on
        # ABSOLUTE screen coordinates, deliberately, so that drawing it as
        # fragments is pixel-identical to drawing it whole (wm_dmg_gray) - and
        # files.inc draws the Disk window's scroll-bar track with it. Replay
        # those pixels an ODD number of rows away and every one of them is
        # inverted. dx cannot do it: it is already forced to a multiple of 8,
        # and 8 is even.
        #
        # WHAT CHANGED IS THE VERDICT AND NOT THE EXPERIMENT (SPEC.md
        # 11.96.13.1). The kernel snapped dy for a while to make the odd legs
        # read 0; that cost 7px of vertical drop precision and a sub-8px nudge
        # that moved nothing, to buy a 50% checkerboard in the other phase
        # inside a scroll-bar track. The snap is gone and the residue is
        # ACCEPTED - so this leg no longer asks "is it 0", it asks WHICH KIND
        # of difference it is:
        #
        #   odd dy  -> dither-phase residue is allowed, anything else fails;
        #   even dy -> neither is allowed. This is what stops the classifier
        #              from becoming a way of not looking, and it is the same
        #              pairing the experiment always had;
        #   NODRAGCACHE -> neither is allowed on ANY leg. With the cache gone
        #              the window redraws in full at the new place, so a
        #              residue here would mean the fill itself had moved.
        #
        # dither_split is what makes that a classification rather than a
        # tolerance: the pixels have to BE a phase-shifted copy of what the
        # repaint drew, and a defect that merely happens to be 1,416 px is not.
        if a.only == "c":
            dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
            d3 = dispcp.win_list(m, S)[-1]
            prev = [None]
            # HOW FAR THERE IS TO DRAG IS THE ADAPTER'S ANSWER, not a constant.
            # CGA's desktop band is ~157 rows and wm_fit clamps a Disk window
            # to 155 of them, so there is no vertical room here at all - a
            # fixed +40 walked the pointer off a 200-row screen and the mouse
            # driver refused, correctly, several steps from the reason. The
            # legs are k and k+1 so that one of each pair is odd, which is the
            # whole experiment.
            wr = dispcp.win_rect(m, S, d3)
            dock = u16(m.read(S("vid_dock_y0"), 2))
            room = min(dock - 1 - (wr[1] + wr[3]), wr[1] - MBAR_H)
            k = (min(40, room) // 8) * 8
            if k < 8:
                print("C: no vertical room on this adapter - a %dx%d window at "
                      "y=%d in a band ending %d leaves %d rows. SKIPPED."
                      % (wr[2], wr[3], wr[1], dock, max(room, 0)))
                k = 0
            for want in ([] if not k else (+k, +k + 1, -k - 1, -k)):
                r0 = dispcp.win_rect(m, S, d3)
                mo.drag(r0[0] + r0[2] // 2, r0[1] + TITLE_H // 2,
                        r0[0] + r0[2] // 2, r0[1] + TITLE_H // 2 + want)
                os88marty.settle(m, card=pri)
                r1 = dispcp.win_rect(m, S, d3)
                got = r1[1] - r0[1]
                # THE PARITY THAT MATTERS IS THE ONE THE RECORD TOOK, not the
                # one asked for: wm_dock_snap and ui_drag's clamp both move a
                # dropped window, and in the run that found this they turned a
                # requested +180 into an actual +175. It is what the residue is
                # allowed FROM as well, for the same reason - a leg that asked
                # for an odd dy and was clamped to an even one has no phase
                # shift in it and must not be handed an excuse for one.
                #
                # `got & 7` rather than `got & 1`: gfx_fill_gray's phase is
                # 2-periodic but gfx_fill_pat's is 8, and a dropped window can
                # land on any delta a clamp leaves it - so what is allowed is
                # "the cache replayed at a delta that shifts SOME dither".
                shifts = bool(got & 7) and not nodc
                probe(m, mo, pri, pw, ph,
                      "dy%+d got%+d %s" % (want, got,
                                           "ODD" if got & 1 else "even"),
                      bad, prev, tag="C", allow_dither=shifts)

        # --- D: AN UNRESIZABLE WINDOW IN THE CORNER (SPEC.md 39.16.3.1) ----
        #
        # Reported off the field machine: drag an unresizable window into the
        # corner where the Hercules and the CGA join, and that area is never
        # repainted after the window is moved.
        #
        # THE SUBJECT HAS TO DRAW DOWN ITS WHOLE HEIGHT, which is why it is
        # the Control Panel and not `hello`. What goes wrong is that
        # wm_strad_fit shortens the FRAME and the gfx primitives clip to the
        # SCREEN (SPEC.md 11.3), so a fixed layout puts the same pixels on the
        # glass either way - and hello's one string is near its top, inside
        # the shortened frame, so it spills nothing and the leg would read 0
        # on a broken kernel. The panel's list and page run to its last row.
        #
        # TWO ASSERTIONS, AND THE FIRST IS THE ONE THAT CANNOT BE VACUOUS.
        # The record's W_H after the drop is a number, and the rule is that a
        # window which will not lay itself out again keeps it. The pixels are
        # the report, and they are only visible in the rect the window LEFT: a
        # forced full repaint agrees with the incremental draw about the rows
        # spilled at the window's NEW place, because the application draws
        # them both times.
        if a.only == "d":
            if len(cards) < 2:
                sys.exit("D needs a two-card machine (os8088_5150_both_gla)")
            dispcp.open_panel(m, mo, S, os88marty.settle, card=pri)
            if a.primary:
                kind = {"vga": 0, "herc": 1, "cga": 2}[a.primary]
                av = m.read(S("vid_avail"), 1)[0]
                dispcp.set_primary(m, mo, S, os88marty.settle,
                                   dispcp.adapter_row(av, kind), card=pri)
                pri = [i for i, c in cards.items()
                       if c["type"] ==
                       dispcp.KIND_CARD[m.read(S("vid_kind"), 1)[0]]][0]
                sec = [i for i in cards if i != pri][0]
                pw, ph = m.fbuf(card=pri)[:2]
                dispcp.open_panel(m, mo, S, os88marty.settle, card=pri)
            dispcp.set_mode(m, mo, S, os88marty.settle, a.mode, card=pri)
            os88marty.settle(m, card=pri)
            if m.read(S("vid_ndisp"), 1)[0] != 2:
                sys.exit("D: the Control Panel did not turn Extend on")
            ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
            seam, vy1 = (u16(ctx, VID_CTX_SZ + VID_CTX_VX),
                         u16(ctx, VID_CTX_SZ + VID_CTX_VY))
            bot1 = vy1 + u16(ctx, 42 + 16)
            bot0 = u16(ctx, VID_CTX_VY) + u16(ctx, VID_CTX_CH)
            print("D: the second display is at (%d,%d), its last row %d "
                  "against display 0's %d" % (seam, vy1, bot1 - 1, bot0 - 1))
            if bot1 >= bot0:
                sys.exit("D: display 1 is not the SHORTER one, so there is no "
                         "corner here to drag into")

            # The panel is the window still on screen - and it is the one the
            # field report was made about.
            title = S("cp_ttl") - (dispcp.KERNEL_SEG << 4)
            tab = m.read(S("wm_wins"), dispcp.MAX_WIN * dispcp.WIN_SIZE)
            cp = [i for i in dispcp.win_list(m, S)
                  if u16(tab, i * dispcp.WIN_SIZE + dispcp.W_TITLE) == title]
            if not cp:
                sys.exit("D: the Control Panel is not on screen")
            cp = cp[0]
            wx, wy, ww, wh = dispcp.win_rect(m, S, cp)
            f = u16(m.read(S("wm_wins") + cp * dispcp.WIN_SIZE, 2))
            if f & (dispcp.WF_SIZABLE | dispcp.WF_KEEPH):
                sys.exit("D: the panel is not the unresizable, non-KEEPH "
                         "window this leg needs (W_FLAGS %#06x)" % f)
            print("D: the panel is %d=(%d,%d %dx%d)" % (cp, wx, wy, ww, wh))

            # ...into the corner: two thirds past the seam, and straddling the
            # row display 1 stops at.
            tx, ty = seam - ww // 3, bot1 - wh // 2
            mo.drag(wx + ww // 2, wy + TITLE_H // 2,
                    tx + ww // 2, ty + TITLE_H // 2)
            os88marty.settle(m, card=pri)
            nx, ny, nw, nh = dispcp.win_rect(m, S, cp)
            print("D: dropped at (%d,%d) %dx%d, its last row %d"
                  % (nx, ny, nw, nh, ny + nh))
            if not nx < seam < nx + nw - 1:
                sys.exit("D: it does not straddle the seam, so nothing here "
                         "is under test")
            if not ny < bot1 < ny + wh:
                sys.exit("D: it does not reach past display 1's last row, so "
                         "wm_strad_fit had nothing to take")
            if nh != wh:
                bad.append("D: the panel came back %d rows tall instead of "
                           "%d - a window with no grow box and no SPEC.md "
                           "11.98 handler cannot lay itself out again, so the "
                           "rows below its frame are still drawn and nothing "
                           "will ever repaint them (SPEC.md 39.16.3.1)"
                           % (nh, wh))

            # ...and away again. Everything it left has to come back.
            look = (pri, sec)
            mo.drag(nx + nw // 2, ny + TITLE_H // 2, 40 + nw // 2,
                    MBAR_H + 2 + TITLE_H // 2)
            os88marty.settle(m, card=pri)
            mo.to(*PARK)
            os88marty.settle(m, card=pri)
            gone = dispcp.win_rect(m, S, cp)
            print("D: moved away to %r" % (gone,))
            dshift = bool((gone[1] - ny) & 7) and not nodc
            inc = shot(m, look)
            repaint(m, mo, pri)
            full = shot(m, look)
            for c in look:
                nm = "primary" if c == pri else "second"
                w0, h0 = inc[c][0], inc[c][1]
                off = MBAR_H if c == pri else 0
                base = off * w0 * 3
                dd = [(x, y + off)
                      for x, y in diff(inc[c][2][base:], full[c][2][base:], w0)]
                classify(inc[c][2], full[c][2], dd, w0, h0, "D", nm, bad,
                         dshift)
                if dd:
                    for tg, cap in (("inc", inc[c][2]), ("full", full[c][2])):
                        box = crop_png("/tmp/cornerD-%s-%s.png" % (nm, tg),
                                       cap, w0, h0, bbox(dd))
                    print("     crops in /tmp/cornerD-%s-{inc,full}.png  %r"
                          % (nm, box))

        # --- B: a drag that uncovers ---------------------------------------
        if a.only not in ("a", "c", "d"):
            seam = None
            if not a.single and len(cards) > 1:
                dispcp.open_panel(m, mo, S, os88marty.settle, card=pri)
                if a.primary:
                    kind = {"vga": 0, "herc": 1, "cga": 2}[a.primary]
                    av = m.read(S("vid_avail"), 1)[0]
                    dispcp.set_primary(m, mo, S, os88marty.settle,
                                       dispcp.adapter_row(av, kind), card=pri)
                    # THE CARDS SWAP ROLES, so every index derived from
                    # [vid_kind] above is now the other one - and a settle
                    # gated on the wrong card watches a screen nothing is
                    # drawing to.
                    pri = [i for i, c in cards.items()
                           if c["type"] ==
                           dispcp.KIND_CARD[m.read(S("vid_kind"), 1)[0]]][0]
                    sec = [i for i in cards if i != pri][0]
                    pw, ph = m.fbuf(card=pri)[:2]
                    print("primary is now %s (card %d)" % (a.primary, pri))
                    dispcp.open_panel(m, mo, S, os88marty.settle, card=pri)
                dispcp.set_mode(m, mo, S, os88marty.settle, a.mode, card=pri)
                dispcp.close_panel(m, mo, S, os88marty.settle, card=pri)
                ctx = m.read(S("vid_ctx"), 2 * VID_CTX_SZ)
                seam = (u16(ctx, VID_CTX_SZ + VID_CTX_VX),
                        u16(ctx, VID_CTX_SZ + VID_CTX_VY))
                print("extended; the second display is at %r" % (seam,))
            if a.under == "hello" and a.only == "b":
                launch_hello(m, mo, pri)

            before = {w for w, _ in wins(m, "before")}
            dispcp.open_drive(m, mo, S, os88marty.settle, "B", card=pri)
            after = wins(m, "after ")
            fresh = [w for w, _ in after if w not in before]
            d2 = fresh[-1] if fresh else after[-1][0]
            wx, wy, ww, wh = dispcp.win_rect(m, S, d2)
            print("dragging window %d from (%d,%d) %dx%d - occupied to (%d,%d)"
                  % (d2, wx, wy, ww, wh, wx + ww, wy + wh))

            # THE SEAM IS AN AXIS, not an x. In "below" the displays are
            # stacked, so a drag along x crosses nothing at all and the run
            # silently measures the same thing "near" does.
            tx, ty = wx + ww // 2, wy + TITLE_H // 2
            if not seam:
                tx = pw - 130
            elif a.mode == "right":
                tx = seam[0] + (ww // 2 + 40 if a.dest == "far" else 40)
                if a.dest == "near":
                    tx = pw - 130
            else:
                ty = seam[1] + (TITLE_H + 8 if a.dest == "far" else 8)
                if a.dest == "near":
                    ty = seam[1] - 100
            mo.drag(wx + ww // 2, wy + TITLE_H // 2, tx, ty)
            os88marty.settle(m, card=pri)
            mo.to(*PARK)
            os88marty.settle(m, card=pri)
            landed = dispcp.win_rect(m, S, d2)
            print("dragged to %r" % (landed,))
            # ...AND IN "below" THAT WAS A VERTICAL DRAG. The subject here is a
            # Disk window, which has the dithered scroll-bar track SPEC.md
            # 11.96.13.1 is about, so B has to make the same distinction C
            # does - off the dy the RECORD took, and never in the control
            # build. In "right" this is 0 and nothing is excused.
            bshifts = bool((landed[1] - wy) & 7) and not nodc
            look = (pri, sec) if (seam and not a.single) else (pri,)
            inc = shot(m, look)

            repaint(m, mo, pri)
            full = shot(m, look)

            for c in look:
                nm = "primary" if c == pri else "second"
                w0, h0 = inc[c][0], inc[c][1]
                off = MBAR_H if c == pri else 0
                base = off * w0 * 3
                dd = [(x, y + off)
                      for x, y in diff(inc[c][2][base:], full[c][2][base:], w0)]
                classify(inc[c][2], full[c][2], dd, w0, h0, "B", nm, bad,
                         bshifts)
                if not dd:
                    continue
                # ...AND WHAT THE PIXELS ARE. A bbox says which rect owns them
                # and a picture says what they show, which is the difference
                # between "the drop shadow" and "three characters of a file
                # name". Both captures are cropped identically so they can be
                # looked at side by side.
                for tag, cap in (("inc", inc[c][2]), ("full", full[c][2])):
                    box = crop_png("/tmp/corner-%s-%s.png" % (nm, tag),
                                   cap, w0, h0, bbox(dd))
                print("     crops in /tmp/corner-%s-{inc,full}.png  %r"
                      % (nm, box))

        print()
        print(("FAIL: " + "; ".join(bad)) if bad else
              "PASS: incremental agrees with a full repaint")
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
