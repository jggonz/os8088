#!/usr/bin/env python3
"""A DECLARED extension's icon is right from a COLD mount (SPEC.md 54.7.3).

    make && python3 tests/assocglyph.py [machine] [system-image]
                                        [--save PATH] [--ref PATH]

    # the reference, taken on BOTH 1bpp adapters BEFORE the change lands
    # (the system image defaults to build/os8088-360.img):
    python3 tests/assocglyph.py os8088_5150_cga_gla --save tests/assocref/cga.txt
    python3 tests/assocglyph.py os8088_5150_herc_gla --save tests/assocref/herc.txt
    # ...and afterwards, on the same two, asserting 0 differing pixels:
    python3 tests/assocglyph.py os8088_5150_cga_gla --ref tests/assocref/cga.txt
    python3 tests/assocglyph.py os8088_5150_herc_gla --ref tests/assocref/herc.txt

`ASSOC.DAT` carries, per package, its stem, its size, the folder it lives in
AND its 64-byte icon body (SPEC.md 54.7). `asc_seed` took the first three and
left the icon, so an extension a package DECLARES (54.6) resolved to an app
slot whose `assoc_glyph` was still the unresolved sentinel - and
`assoc_compose` draws that as the BARE PAGE. On the shipped apps disk the
programs are in `APPS/` and their documents are in `MEDIA/` (19.2), so
`BROWSER.HTM`, `GUIDE.TEX` and `PAPER.TEX` drew blank pages until `APPS/` had
been browsed and the harvest had read `BROWSER.O88` and `TEXPAD.O88`.

`MOD` never showed it: Tracker is one of the four whose glyph os88mini.py
bakes into the kernel, so it has one before any disk is read.

The second argument is a system image, so this can be pointed at a reference
build - both assertions fail against a kernel from before 54.7.3, which is
what says the gate is testing something. It is the image this BOOTS, though,
and that is worth saying in the file rather than leaving to be inferred:
handing it an older build runs the whole gate ON the old kernel and compares
nothing between the two. `--ref` is the cross-build comparison; the second
argument is not, and reading it as one produces a green result that means
nothing.

Three assertions, and the third is the only one that can see a bitmap that is
consistently WRONG:

  1. After ONE root mount of B:, the declared slots' glyphs are RESOLVED.
  2. The Disk window showing `MEDIA/` is BYTE-IDENTICAL drawn cold and
     drawn after `APPS/` has been browsed - the icon a user sees first is
     the icon they keep. That is checked inside ONE build, so it needs no
     reference kernel: what it compares is the seeded glyph against the
     harvested one, which is the whole claim.
  3. With `--ref PATH`, that cold capture is byte-identical to one `--save
     PATH` took from ANOTHER build. 1 and 2 are both self-consistency
     checks - each compares this kernel against itself - so a generator
     that composes the same wrong page every time satisfies both of them
     cleanly, and the icon it is wrong about is on every document in the
     system. A change that stops STORING a bitmap and starts COMPOSING one
     therefore needs a capture taken BEFORE it lands: there is no way to
     take one afterwards, which makes this a scheduling constraint and not
     only a test.

`--save` writes the cold capture as one line a pixel ROW, `#` lit, so a `git
diff` of two references shows the icon that moved instead of a hex blob. It
records the card and the rect, and `--ref` REFUSES rather than compares when
either has changed - a diff of two different rectangles is a number with no
meaning. It records the ROM's own banner too, read out of the ROM at 0xFE001
rather than inferred from the machine name, because two machine configs
differing only in `rom_set` are one edit apart; that one is provenance and
not a refusal, since the window's pixels are the kernel's - MEASURED, rather
than assumed: the two references in `tests/assocref/` were taken under
GLaBIOS and answer 0 differing pixels under the IBM 5150 part as well
(`os8088_5150_cga`, `os8088_5150_herc`, banner `501476 COPR. IBM`), so a run
may be diffed across ROMs even though a BENCH run may not.

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


def opt(name):
    """`--name PATH`, lifted OUT of argv so the positionals keep their places."""
    if name not in sys.argv:
        return None
    i = sys.argv.index(name)
    if i + 1 >= len(sys.argv):
        sys.exit("assocglyph: %s wants a path after it" % name)
    del sys.argv[i]
    return sys.argv.pop(i)


SAVE = opt("--save")
REF = opt("--ref")
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
REF_MAGIC = "assocglyph-ref 1"  # first line of a --save file, checked on --ref


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


def rom(m):
    """The ROM's own banner, READ OUT OF IT - never inferred from the config.

    0xFE001 is where a 5150-class BIOS puts its copyright string: the IBM
    part reads `501476 COPR. IBM` and GLaBIOS reads `GLaBIOS [`. A reference
    labelled from the machine NAME is a label that can be wrong with nothing
    to notice, so it is labelled from the bytes.
    """
    s = m.read(0xFE001, 16).decode("latin1")
    return "".join(c if " " <= c <= "~" else "." for c in s)


def ref_write(path, card, rect, w, pix, sig):
    """The capture as text, behind a header saying what it is a capture OF."""
    if len(pix) % w:
        sys.exit("assocglyph: the capture is %d px and the rect is %d wide - "
                 "the window hangs off the right edge and the rows are "
                 "ragged" % (len(pix), w))
    with open(path, "w") as f:
        f.write("%s\nmachine %s\ncard %s\nrect %d %d %d %d\nwidth %d\n"
                "rom %s\npixels\n"
                % ((REF_MAGIC, MACHINE, card) + tuple(rect) + (w, sig)))
        for i in range(0, len(pix), w):
            f.write("".join("#" if p else "." for p in pix[i:i + w]) + "\n")


def ref_read(path):
    """...and back, as {machine, card, rect, width, rom, pixels}."""
    lines = open(path).read().splitlines()
    if not lines or lines[0] != REF_MAGIC:
        sys.exit("assocglyph: %s does not begin %r, so it is not one of these"
                 % (path, REF_MAGIC))
    head, i = {}, 1
    while i < len(lines) and lines[i] != "pixels":
        k, _, v = lines[i].partition(" ")
        head[k] = v
        i += 1
    head["rect"] = tuple(int(n) for n in head["rect"].split())
    head["width"] = int(head["width"])
    head["pixels"] = bytes(1 if c == "#" else 0
                           for r in lines[i + 1:] for c in r)
    return head


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

    # --- 2a. ...and against a capture taken from ANOTHER BUILD ---------------
    if SAVE or REF:
        if cold is None:
            sys.exit("assocglyph: --save/--ref compare the 1bpp framebuffer, "
                     "and this machine's card is %r - run them on CGA or "
                     "Hercules, never on VGA" % card)
        sig = rom(m)
        say("ROM banner at 0xFE001: %r" % sig)
    if SAVE:
        ref_write(SAVE, card, rect, w, cold, sig)
        say("saved the %r window - %d px, %d lit - to %s"
            % (rect, len(cold), sum(cold), SAVE))
    if REF:
        r = ref_read(REF)
        if r["card"] != card or r["rect"] != tuple(rect):
            sys.exit("assocglyph: %s is card %r rect %r and this run is %r %r "
                     "- a diff of two different rectangles is a number with "
                     "no meaning"
                     % (REF, r["card"], r["rect"], card, tuple(rect)))
        if len(r["pixels"]) != len(cold):
            sys.exit("assocglyph: %s holds %d px and this capture is %d, with "
                     "the rects agreeing - the reference file is malformed"
                     % (REF, len(r["pixels"]), len(cold)))
        if r.get("rom") != sig:
            say("note: the reference was taken under ROM %r and this run is "
                "%r - the window's pixels are the KERNEL's, so that is "
                "provenance and not a refusal" % (r.get("rom"), sig))
        n = sum(1 for p, q in zip(r["pixels"], cold) if p != q)
        say("%d px of %d differ from %s" % (n, len(cold), REF))
        if n:
            bad = [i for i, (p, q) in enumerate(zip(r["pixels"], cold))
                   if p != q]
            say("bbox x %d..%d y %d..%d"
                % (min(i % w for i in bad), max(i % w for i in bad),
                   min(i // w for i in bad), max(i // w for i in bad)))
            fails.append("the MEDIA/ window is not the one %s recorded: %d px "
                         "differ. That is the CROSS-BUILD check - assertions "
                         "1 and 2 compare this kernel against itself and pass "
                         "whatever a generator composes, so this is the only "
                         "one that sees a page bitmap that is consistently "
                         "wrong" % (REF, n))

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
