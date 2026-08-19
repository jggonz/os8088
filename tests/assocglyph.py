#!/usr/bin/env python3
"""A DECLARED extension's icon is right from a COLD mount (SPEC.md 54.7.3).

    make && python3 tests/assocglyph.py [machine] [system-image]

`ASSOC.DAT` carries, per package, its stem, its size, the folder it lives in
AND its 64-byte icon body (SPEC.md 54.7). `asc_seed` took the first three and
left the icon, so an extension a package DECLARES (54.6) resolved to an app
slot whose `assoc_glyph` was still the unresolved sentinel - and
`assoc_compose` draws that as the BARE PAGE. On the shipped apps disk the
programs are in `APPS/` and their documents are in `MEDIA/` (19.2), so
`DEMO.HTM`, `GUIDE.TEX` and `PAPER.TEX` drew blank pages until `APPS/` had
been browsed and the harvest had read `BROWSER.O88` and `TEXPAD.O88`.

`MOD` never showed it: Tracker is one of the four whose glyph os88mini.py
bakes into the kernel, so it has one before any disk is read.

The second argument is a system image, so this can be pointed at a reference
build - both assertions fail against a kernel from before 54.7.3, which is
what says the gate is testing something.

Two assertions, and the second is the one that matters:

  1. After ONE root mount of B:, the declared slots' glyphs are RESOLVED.
  2. The Disk window showing `MEDIA/` is BYTE-IDENTICAL drawn cold and
     drawn after `APPS/` has been browsed - the icon a user sees first is
     the icon they keep. That is checked inside ONE build, so it needs no
     reference kernel: what it compares is the seeded glyph against the
     harvested one, which is the whole claim.

The diff is the WINDOW's rect and not the screen, because the menu bar
carries a clock (SPEC.md 12.9) and the two captures are minutes apart - a
whole-screen compare measures the time of day.

It reads the 1bpp framebuffer, so the pixel half runs on CGA and Hercules
and not on VGA - mode 12h is four planes and `fbuf`'s raster is not in
kernel coordinates. Nothing is lost by that: the glyph is a table of bytes
and the path that fills it has no adapter branch in it at all, so
assertion 1 and the seeded-equals-harvested compare answer for all three.
"""
import sys

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import os88marty
import os88mouse
import os88sym
import dispcp

MACHINE = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga_gla"
SYS_IMG = sys.argv[2] if len(sys.argv) > 2 else "build/os8088-360.img"
APPS_IMG = "build/apps360.img"
S = os88sym.linear
NAPP = 12                       # ASSOC_NAPP
DECLARED = ("BROWSER", "TEXPAD")
PARK = (320, 190)               # the pointer is DRAWN, over the window like
                                # anything else - it only has to be in the
                                # SAME place for both captures, not clear of
                                # them, and os88mouse.to lands pixel-exact
fails = []
PIX1BPP = ("cga", "mda")        # the cards vram() can read as flat memory


def say(s):
    print("  " + s)


def slots(m):
    """{stem: 8-byte glyph} for every live app slot."""
    stem = m.read(S("assoc_stem"), NAPP * 8)
    glyph = m.read(S("assoc_glyph"), NAPP * 8)
    out = {}
    for i in range(NAPP):
        s = bytes(stem[i * 8:(i + 1) * 8])
        if s[0]:
            out[s.decode("latin1").rstrip()] = bytes(glyph[i * 8:(i + 1) * 8])
    return out


def shot(m, mo, rect):
    """The window's own pixels, with the pointer parked at one fixed spot."""
    mo.to(*PARK)
    os88marty.settle(m)
    w, h, rows = m.vram()       # CGA or Hercules; see the note above
    x, y, rw, rh = rect
    return rw, b"".join(bytes(rows[yy][x:x + rw])
                        for yy in range(y, min(y + rh, h)))


def media(m, mo, wx, wy):
    """Stand in B:\\MEDIA, from wherever in the volume we are."""
    while ".." in [n for n, _ in dispcp.listing(m, S)]:
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "..")
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "MEDIA")
    return [n for n, _ in dispcp.listing(m, S) if n != ".."]


with os88marty.launch(SYS_IMG, apps=APPS_IMG, machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    wx, wy = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])[:2]

    # --- 1. one root mount of B:, and nothing else --------------------------
    seeded = slots(m)
    say("after the root mount, slots: " + ", ".join(
        "%s=%s" % (k, "resolved" if any(v) else "UNRESOLVED")
        for k, v in seeded.items()))
    for name in DECLARED:
        if name not in seeded:
            fails.append("%s has no app slot at all - asc_merge_ext did not "
                         "take this volume's association rows" % name)
        elif not any(seeded[name]):
            fails.append("%s's glyph is unresolved after a root mount: its "
                         "documents draw the bare page until APPS/ is "
                         "browsed (SPEC.md 54.7.3)" % name)

    card = m.cmd(cmd="video")["type"]

    # --- 2. MEDIA/ cold ------------------------------------------------------
    names = media(m, mo, wx, wy)
    say("MEDIA/ = %r" % names)
    if not [n for n in names if n.endswith((".HTM", ".TEX"))]:
        sys.exit("assocglyph: no .HTM or .TEX in B:\\MEDIA - this disk cannot "
                 "answer the question (check APPS_DATA in the Makefile)")
    rect = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
    if card in PIX1BPP:
        w, cold = shot(m, mo, rect)
        say("cold capture: the window at %r, %d lit" % (rect, sum(cold)))
    else:
        # ...and SAY so. vram() answers about B0000 on anything it does not
        # know, which on a VGA in mode 12h is memory nobody is driving - so
        # both captures would come back blank and AGREE, and the gate would
        # report a pass it had not earned
        cold = None
        say("card is %r: the pixel compare needs a 1bpp framebuffer, so this "
            "run asserts the glyph bytes alone" % card)

    # --- 3. ...and again, with APPS/ harvested in between --------------------
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "..")
    dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, "APPS")
    say("APPS/ browsed: the harvest has now read every package's own sector")
    harvested = slots(m)
    media(m, mo, wx, wy)
    if cold is not None:
        r2 = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
        if r2 != rect:
            sys.exit("assocglyph: the window moved (%r -> %r) - the two "
                     "captures are not of the same rect" % (rect, r2))
        _, warm = shot(m, mo, rect)
        say("warm capture: %d lit" % sum(warm))

    for name in DECLARED:
        # only where the seed produced one: an EMPTY seeded glyph is
        # assertion 1's report, and saying it twice in different words sends
        # the reader looking for a second bug
        if any(seeded.get(name, b"")) and name in harvested \
                and seeded[name] != harvested[name]:
            fails.append("%s's SEEDED glyph differs from its HARVESTED one: "
                         "asc_seed's assoc_reduce and the harvest's disagree "
                         "about the same package's icon" % name)

    d = 0
    if cold is not None:
        d = sum(1 for p, q in zip(cold, warm) if p != q)
        say("%d px of %d differ between the two" % (d, len(cold)))
    if d:
        bad = [i for i, (p, q) in enumerate(zip(cold, warm)) if p != q]
        say("bbox x %d..%d y %d..%d"
            % (min(i % w for i in bad), max(i % w for i in bad),
               min(i // w for i in bad), max(i // w for i in bad)))
        fails.append("MEDIA/ does not draw the same cold as it does after "
                     "APPS/ has been browsed: %d px differ - a document icon "
                     "that changes once its program has been SEEN is 54.7.3"
                     % d)

if fails:
    print("\nassocglyph: FAIL")
    for f in fails:
        print("  " + f)
    sys.exit(1)
print("\nassocglyph: a declared extension's document icon is right from the "
      "first mount - PASS on %s" % MACHINE)
