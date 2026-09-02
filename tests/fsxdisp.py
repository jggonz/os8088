#!/usr/bin/env python3
"""Does an fsx bracket take ONE display and dark the others? (SPEC.md 39.18)

    python3 tests/fsxdisp.py
    python3 tests/fsxdisp.py --primary herc          # ...with make VIDEO=herc

`tests/dispcheck.py`'s twin, and separate because it needs a different apps
floppy: SPEC.md 53.9's `fsxtest` is the only package in the tree with a
fullscreen-exclusive item that can be reached in two double-clicks, and it
ships on a scratch image of its own.

THE DISK IS THE 360KB TWIN, and that is the whole of what this gate needs to
be told. Every MartyPC machine in this tree has 360KB drives, so the 1.44MB
`build/fsxtest.img` - which is the QEMU route's, `make test TESTAPPS=` -
cannot be read in one at all: B: never mounts, no Disk window opens, and the
failure surfaces as "the zone arithmetic above missed" about arithmetic that
is correct.

Its `x` key is the SAME-MODE bracket (SPEC.md 53.7) - no `fsx_mode` call at
all - which is what a Hercules or a CGA can take, and is also the case
39.18's own trap is about: entering may change the GEOMETRY even when it does
not change the mode, because the machine became a different shape.

Three things, in the order they can fail:

  1. THE OTHER MONITOR GOES DARK while the bracket runs. Nothing can maintain
     it - the kernel does not run inside a bracket (53.1) - so a live card
     would sit showing a frozen desktop.
  2. IT COMES BACK, and comes back with a DESKTOP rather than the frame it
     was blanked on. Blanking gates the video signal and not memory, so the
     stale frame is still in the card when the unblank happens; what says the
     repaint went first is that the pixels differ from nothing and match a
     desktop dither.
  3. THE PRIMARY IS RESTORED - fsxtest draws a black rect inside its bracket
     and SPEC.md 53.9 says the restore repaint must erase it.

THE TWO ADAPTERS GO DARK DIFFERENTLY, and one measurement cannot see both -
which is the one thing to know before changing this, because each instrument
on its own passes a kernel that never wrote the other card's port. Measured
directly, by writing the ports from the host on this machine:

  a HERCULES stops scanning.       3B8h <- 0x00 -> 163 frames/s becomes 0,
                                   and `fbuf` then hands back the last frame it
                                   rasterised, so through it a dark Hercules
                                   reads as a perfectly ordinary desktop.
                                   BUT THAT IS NOT THE BLANK, and this line
                                   used to say "bit 3 clear" and be wrong:
                                   0x00 clears the GRAPHICS bit as well, which
                                   is MDA text mode. vid_blank_kind writes
                                   0x02 - graphics kept, enable clear (SPEC.md
                                   39.6, 64) - and measured, MartyPC IGNORES
                                   bit 3: 174 frames/s, all 153,120 lit. The
                                   port is write-only, so nothing can be read
                                   back. A MONO SECONDARY THEREFORE CANNOT
                                   SHOW THIS, which is why the default machine
                                   is os8088_5150_both_gla_mono - the mono DIP
                                   makes the CGA the secondary.
  a CGA keeps scanning nothing.    3D8h bit 3 clear -> 64,000 lit becomes 0 and
                                   the counter holds at ~187 frames/s. That is
                                   the card too, not the model: the CRTC still
                                   drives sync, the video output is gated off.

So a dark monitor here is `frames == 0 OR nothing lit`, and the disjunction is
still a real gate in both directions - an unblanked card is scanning AND lit,
whichever it is. The CGA half then owes one thing more: SPEC.md 39.18.1 says
blanking gates the SIGNAL and not memory, so while that card is dark its VRAM
must still hold the desktop it was blanked on, which is read straight out of
the guest (`vram`) rather than off the card.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dispcp                                                # noqa: E402
from os88fixture import need                                 # noqa: E402

# desk.inc's zone layout and files.inc's row layout both come from os88geom
# now, and the zone's PITCH and HEIGHT are not constants at all - they were
# written down here as 60 and 44, and SPEC.md 26.4's square CGA icon made them
# 34 and 26, so this double-clicked bare desktop and then reported that
# FSXTEST had not launched.


def lit(px):
    return sum(1 for i in range(0, len(px), 3) if px[i])


def fps(m, idx, secs=1.0):
    """Frames the card produced in `secs` of wall clock. Zero is a card that
    has stopped scanning, which is one of the two ways of being dark."""
    a = [c for c in m.cards() if c["idx"] == idx][0]["frames"]
    time.sleep(secs)
    b = [c for c in m.cards() if c["idx"] == idx][0]["frames"]
    return b - a


def vlit(m, kind):
    """Lit pixels in the card's own MEMORY, which blanking must not touch."""
    w, h, rows = m.vram(kind)
    return sum(sum(r) for r in rows), w * h


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla_mono")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/fsxtest360.img")
    ap.add_argument("--primary", choices=("auto", "herc", "cga"),
                    default="auto",
                    help="which card the KERNEL drives; anything but auto "
                         "means a VIDEO= build, and picks the boot gate's card")
    a = ap.parse_args(argv)

    if a.apps == ap.get_default("apps"):
        need(a.apps)               # `all` builds nothing under tests/
    if not os.path.exists(a.apps):
        sys.exit("fsxdisp: no %s - `make %s` builds it" % (a.apps, a.apps))

    fail = []
    # The boot gate watches ONE card, and on a `VIDEO=` build that is not
    # MartyPC's primary - so enumerate before running rather than after
    # (dispcheck.py's pattern). Watching the wrong one here does not report a
    # wrong card: it reports a machine that never booted, because what the
    # secondary carries is the extended desktop's dither with no menu bar in it.
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine,
                          boot=False) as m:
        S = os88sym.linear
        cards = m.cards()
        if len(cards) != 2:
            sys.exit("fsxdisp: %s has %d video card(s), not 2" % (a.machine,
                                                                  len(cards)))
        gate_card = None
        if a.primary != "auto":
            gate_card = [c for c in cards
                         if c["type"] == ("mda" if a.primary == "herc"
                                          else "cga")][0]["idx"]
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up, card=gate_card)
        cards = m.cards()

        # SPEC.md 39.19.1's default is Single, so ask for the extended desktop
        # the way a user has to - dispcheck.py's block, and its reasoning.
        mo0 = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo0, S, os88marty.settle, card=gate_card)
        dispcp.set_mode(m, mo0, S, os88marty.settle, "right", card=gate_card)
        dispcp.close_panel(m, mo0, S, os88marty.settle, card=gate_card)

        kind = m.read(S("vid_kind"), 1)[0]
        ndisp = m.read(S("vid_ndisp"), 1)[0]
        if ndisp != 2:
            sys.exit("fsxdisp: the kernel reports %d display(s) - nothing "
                     "here applies" % ndisp)
        ptype = "mda" if kind == 1 else "cga"
        pri = [c for c in cards if c["type"] == ptype][0]
        sec = [c for c in cards if c is not pri][0]
        say = lambda s: print("  " + s)
        say("primary = card %d (%s), secondary = card %d (%s)"
            % (pri["idx"], pri["type"], sec["idx"], sec["type"]))

        mo = os88mouse.Mouse(marty=m)
        # drive B: is zone ordinal 1 - A:, B:, then any driver volumes.
        mo.dblclick(*dispcp.drive_xy(m, S, 1))
        os88marty.settle(m, card=pri["idx"])

        # ...and FSXTEST.O88 is the only file on that image, so it is row 0 of
        # the Disk window that just opened. Its rect is read out of wm_wins
        # rather than assumed: SPEC.md 39.7 clamps a template onto the live
        # screen, so where a window lands is the adapter's business.
        dsk = dispcp.win_list(m, S)
        if not dsk:
            sys.exit("fsxdisp: no Disk window after double-clicking B: - the "
                     "zone arithmetic above missed")
        wx, wy = dispcp.win_rect(m, S, dsk[-1])[:2]
        row0 = dispcp.row_xy(wx, wy, 0)
        say("Disk window at (%d,%d); row 0 at %s" % (wx, wy, row0))
        mo.dblclick(*row0)
        os88marty.settle(m, card=pri["idx"])
        if len(dispcp.win_list(m, S)) < 2:
            sys.exit("fsxdisp: FSXTEST did not launch - row 0 was not where "
                     "this thought it was")

        w, h, before = m.fbuf(card=sec["idx"])
        n_before, f_before = lit(before), fps(m, sec["idx"])
        pw, ph, pbefore = m.fbuf(card=pri["idx"])
        say("secondary before the bracket: %d frames/s, %d lit (%.1f%%)"
            % (f_before, n_before, 100.0 * n_before / (w * h)))
        if f_before == 0:
            fail.append("the secondary is not scanning before the bracket - "
                        "nothing below can mean anything")
        if not 0.30 * w * h < n_before:
            fail.append("the secondary is not carrying a desktop before the "
                        "bracket")

        # --- §39.18.3's guard: a SAME-MODE bracket changes NOTHING about
        # displays. This used to be the leg the dark was asserted on, and it
        # was right until §39.18.3 moved the collapse into fsx_mode on two
        # field reports. Asserting it the other way round is what keeps those
        # reports fixed.
        m.key("KeyX")
        time.sleep(2.5)
        f_same = fps(m, sec["idx"])
        n_same = lit(m.fbuf(card=sec["idx"])[2])
        say("secondary in a SAME-MODE bracket: %d frames/s, %d lit" % (f_same,
                                                                       n_same))
        if not f_same or n_same != n_before:
            fail.append("a SAME-MODE bracket changed the secondary (%d fps, "
                        "%d lit against %d) - §39.18.3 moved the collapse into "
                        "fsx_mode, so a bracket that sets no mode must change "
                        "nothing about displays" % (f_same, n_same, n_before))
        m.key("Enter")
        os88marty.settle(m, card=pri["idx"])

        # --- ...and the collapse itself, which needs a MODE to be set. The
        # key is the primary's, because fsx_mode refuses a mode outside
        # fsx_caps (VGA 0x1EF, HERC 0x011, CGA 0x00F) and fsxtest then reports
        # 'R' rather than bracketing at all.
        mode_key = {"mda": "Digit4",      # FSXM_HERC   720x348 mono
                    "cga": "Digit3",      # FSXM_CGA640 640x200x2
                    "vga": "Digit7"}[pri["type"]]
        say("mode bracket via %s (primary is %s)" % (mode_key, pri["type"]))
        m.key(mode_key)
        time.sleep(2.5)
        f_during = fps(m, sec["idx"])
        n_during = lit(m.fbuf(card=sec["idx"])[2])
        say("secondary inside the bracket:  %d frames/s, %d lit" % (f_during,
                                                                    n_during))
        if sec["type"] == "mda" and f_during and n_during:
            # NOT a failure, and not a kernel question: MartyPC does not model
            # 3B8h bit 3. Measured from the host on this machine with the
            # desktop extended - 0x02 (graphics on, ENABLE CLEAR, which is byte
            # for byte what vid_blank_kind writes) leaves the card scanning at
            # 174 frames/s with all 153,120 pixels still lit, while 0x00 stops
            # it - and 0x00 clears the GRAPHICS bit too, which is MDA text mode
            # rather than a blank. 3B8h is write-only (reads FF), so there is no
            # readback either. This is why the default machine is the mono-DIP
            # one: it makes the CGA the secondary, and the CGA's gate IS
            # modelled (3D8h bit 3 clear takes 81,980 lit to 0).
            say("...secondary is an MDA: the dark is UNOBSERVABLE here "
                "(MartyPC does not model 3B8h bit 3), so not asserted")
        elif f_during and n_during:
            fail.append("the secondary is still scanning at %d frames/s with "
                        "%d pixels lit inside an fsx bracket - nothing "
                        "maintains it, so what it is showing is frozen "
                        "(SPEC.md 39.18)" % (f_during, n_during))
        elif f_during:
            # A card gated off rather than stopped - the CGA's way. Its memory
            # is then still readable and must be untouched: 39.18.1 blanks the
            # SIGNAL, and a repaint that cleared VRAM would look identical
            # through the card.
            v, vtot = vlit(m, sec["type"] if sec["type"] == "cga" else "herc")
            say("...gated off, not stopped; its VRAM still holds %d lit "
                "(%.1f%%)" % (v, 100.0 * v / vtot))
            if v < 0.30 * vtot:
                fail.append("the secondary went dark by LOSING its picture "
                            "(%d lit in VRAM) - 39.18.1 blanks the signal, "
                            "not memory" % v)

        m.key("Enter")                      # ...any key leaves it
        os88marty.settle(m, card=pri["idx"])
        w, h, after = m.fbuf(card=sec["idx"])
        n_after, f_after = lit(after), fps(m, sec["idx"])
        say("secondary after it:            %d frames/s, %d lit (%.1f%%)"
            % (f_after, n_after, 100.0 * n_after / (w * h)))
        if f_after == 0:
            fail.append("the secondary never came back - it was blanked and "
                        "the unblank did not happen (SPEC.md 39.18.1)")
        if n_after < 0.30 * w * h:
            fail.append("the secondary came back at %d lit rather than a "
                        "desktop" % n_after)

        pw, ph, pafter = m.fbuf(card=pri["idx"])
        d = sum(1 for i in range(0, len(pafter), 3)
                if pafter[i] != pbefore[i])
        say("primary before vs after:       %d differing px of %d"
            % (d, pw * ph))
        if d > 0.02 * pw * ph:
            fail.append("the primary differs by %d px after the bracket - "
                        "SPEC.md 53.9 says the restore repaint erases what "
                        "the bracket drew" % d)

    print()
    for f in fail:
        print("fsxdisp: FAIL: %s" % f)
    if fail:
        return 1
    print("fsxdisp: an fsx bracket took one display and darked the other "
          "- PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
