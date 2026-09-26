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
  a CGA keeps scanning nothing.    3D8h bit 3 clear gates the video output and
                                   the counter holds at ~215 frames/s. That is
                                   the card too, not the model: the CRTC still
                                   drives sync, the video output is gated off.

**BUT `nothing lit` IS NOT WHAT A DARK CGA READS HERE, AND THAT LINE COST THIS
ROW A RED FOR NOTHING.** It used to say `3D8h bit 3 clear -> 64,000 lit
becomes 0`, and on this tree the same write leaves **~840 of 128,000** - one
horizontal run the renderer keeps through the gate. The desktop is gone (42,612
pixels of 43,436 differ) and the card is as dark as the host can make it: with
the machine inside the bracket, `fbuf` there and `fbuf` after a HOST-driven
`3D8h <- [vid_cgamode] & ~8` on a bare desktop came back **pixel for pixel
identical**. So the figure is MEASURED IN THE RUN now (`dark_lit` below) rather
than remembered, and the gate is still 50x wide: dark is ~840 and a desktop is
~43,400.

**AND A CGA CAPTURE IS NOT REPEATABLE TO THE PIXEL, WHICH IS THE OTHER HALF.**
Four `fbuf` captures of ONE STILL DESKTOP, one second apart, with no bracket,
no key and nothing running, read `43404, 43404, 43412, 43412` - and the two
pairs differ from each other by **1,376 pixels**, in the same band (x 216..359)
every time. The picture sits in one of two rasterisations and moves between
them on its own. So any pixel-exact comparison of two CGA captures is a coin
flip, and the same-mode leg below - which asserted `lit == lit` across the
bracket - was that coin flip with a kernel's name on it.

What replaces it is the thing SPEC.md 39.18.3 is actually about, read out of
the guest: **`[vid_ndisp]`**. A same-mode bracket must leave it at 2 and a mode
bracket must collapse it to 1, and that is exact at any emulator speed and on
any renderer. The pixels then only have to answer the coarse question they can
answer - is the other monitor carrying a desktop, or is it dark?

The CGA half owes one thing more: SPEC.md 39.18.1 says blanking gates the
SIGNAL and not memory, so while that card is dark its VRAM must still hold the
desktop it was blanked on, which is read straight out of the guest (`vram`)
rather than off the card.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dispcp                                                # noqa: E402
import os88build
from os88fixture import need                                 # noqa: E402

# desk.inc's zone layout and files.inc's row layout both come from os88geom
# now, and the zone's PITCH and HEIGHT are not constants at all - they were
# written down here as 60 and 44, and SPEC.md 26.4's square CGA icon made them
# 34 and 26, so this double-clicked bare desktop and then reported that
# FSXTEST had not launched.


def lit(px):
    return sum(1 for i in range(0, len(px), 3) if px[i])


def fps(m, idx, secs=1.0):
    """Frames the card produced in what `secs` of an idle box's wall clock
    gives the guest (`pace`). Zero is a card that has stopped scanning, which
    is one of the two ways of being dark."""
    a = [c for c in m.cards() if c["idx"] == idx][0]["frames"]
    os88marty.pace(m, secs)
    b = [c for c in m.cards() if c["idx"] == idx][0]["frames"]
    return b - a


def whole_frame(m, idx):
    """Until the card has FINISHED a frame begun after now - two completed
    frames on its own counter - so the buffer read next shows a port write
    just made rather than the frame it landed in."""
    def frames():
        return [c for c in m.cards() if c["idx"] == idx][0]["frames"]
    f0 = frames()
    os88marty.until(m, lambda _: frames() >= f0 + 2,
                    "card %d to finish a frame" % idx, poll=0.05, limit=10)


def in_bracket(m, S, card):
    """Until an fsx bracket is up ([fsx_task] armed) and its app has drawn
    what it draws and gone to wait for the key that ends it."""
    os88marty.until(m, lambda mm: mm.read(S("fsx_task"), 1)[0] != 0xFF,
                    "the fsx bracket to open", poll=0.1, limit=15)
    os88marty.settle(m, card=card)


def vlit(m, kind):
    """Lit pixels in the card's own MEMORY, which blanking must not touch."""
    w, h, rows = m.vram(kind)
    return sum(sum(r) for r in rows), w * h


def dark_lit(m, S, sec, say):
    """What THIS card reads when it is dark - MEASURED, in this run.

    The host writes the very byte `vid_blank_kind` writes (SPEC.md 39.18.1:
    the card's own mode shadow with the video-enable bit clear), reads the
    card, and puts the byte back. It is one port write and it is undone before
    anything else happens; the kernel's `[vid_cgamode]` shadow is never
    touched, so the next blank or unblank it does is unaffected.

    **IT IS MEASURED BECAUSE THE REMEMBERED FIGURE WAS WRONG.** The header's
    `becomes 0` was taken on an older tree, and a dark CGA here reads ~840 of
    128,000 - a renderer artifact rather than a kernel one, since the kernel's
    own blank and this one come back PIXEL FOR PIXEL IDENTICAL. A row that
    re-takes the number cannot go stale that way again.

    Returns None where the dark is unobservable - a mono secondary, whose
    3B8h bit 3 MartyPC does not model (header). The caller then asserts
    nothing about it, which is what this row already did.
    """
    if sec["type"] != "cga":
        return None
    cm = m.read(S("vid_cgamode"), 1)[0]
    m.outb(0x3D8, cm & ~8)
    whole_frame(m, sec["idx"])
    n = lit(m.fbuf(card=sec["idx"])[2])
    m.outb(0x3D8, cm)
    whole_frame(m, sec["idx"])
    back = lit(m.fbuf(card=sec["idx"])[2])
    say("a DARK %s reads %d lit (measured here: 3D8h <- %02X and back); "
        "the desktop is %d" % (sec["type"], n, cm & ~8, back))
    return n, back


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla_mono")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default=os88build.at("build/fsxtest360.img"))
    ap.add_argument("--primary", choices=("auto", "herc", "cga"),
                    default="auto",
                    help="which card the KERNEL drives; anything but auto "
                         "means a VIDEO= build, and picks the boot gate's card")
    ap.add_argument("--dock", action="store_true",
                    help="restore an auto-hidden side dock with CTRL.DRV unloaded")
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
        if a.dock:
            import dockpos
            wx, wy = dispcp._cp_win(m, S)
            row = m.read(S("cp_nst"), 1)[0] - 1
            mo0.click(wx + 37, wy + 19 + 6 + row * 14 + 7, settle=0)
            os88marty.settle(m, card=gate_card)
            dockpos.click_row(m, mo0, dockpos.CPK_R0Y + 2 * dockpos.CPK_ROWH)
            dockpos.click_row(m, mo0, dockpos.CPK_AY)
            if m.read(S("dock_cfg"), 1)[0] != 6:
                raise RuntimeError("fsxdisp: could not enable right auto-hide")
            # The LIVE rect rather than the whole-strip one: the strip's
            # own rect is DOCK.DRV's private geometry and lives in the module
            # image (SPEC.md 30.5), while the live rect is what every painter
            # in the kernel reads - so this is the stronger of the two to
            # find unchanged after the bracket.
            dock_bounds = {n: m.read(S(n), 2) for n in
                           ("vid_band_x0", "vid_band_xe", "vid_dock_y0",
                            "vid_desk_zx", "dock_lx1", "dock_ly1",
                            "dock_lx2", "dock_ly2", "dock_thk")}
        dispcp.close_panel(m, mo0, S, os88marty.settle, card=gate_card)

        if a.dock and m.read(S("mod_tab"), 2) != b"\x00\x00":
            raise RuntimeError("fsxdisp: CTRL.DRV is still loaded before fullscreen")

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

        # THE DARK FIGURE FOR THIS CARD, TAKEN NOW, so the two legs below are
        # measured against this run rather than against a number in a comment.
        dark = dark_lit(m, S, sec, say)
        if dark is not None:
            n_dark, n_back = dark
            if n_back < 0.30 * w * h:
                sys.exit("fsxdisp: the host's own blank did not come back "
                         "(%d lit) - the machine has been perturbed and "
                         "nothing below would mean anything" % n_back)
            # The slack is a fraction of THE DESKTOP and not of the dark
            # figure, because what this ceiling has to separate is those two:
            # ~840 against ~43,400, so 2% of the desktop puts it an order of
            # magnitude clear of both.
            dark_ceiling = n_dark + 0.02 * n_before
        else:
            dark_ceiling = None

        # --- §39.18.3's guard: a SAME-MODE bracket changes NOTHING about
        # displays. This used to be the leg the dark was asserted on, and it
        # was right until §39.18.3 moved the collapse into fsx_mode on two
        # field reports. Asserting it the other way round is what keeps those
        # reports fixed.
        #
        # **THE ASSERTION IS `[vid_ndisp]` AND NOT THE PIXELS**, which is a
        # correction: this compared `lit` across the bracket for equality, and
        # a CGA capture is not repeatable to the pixel - four captures of one
        # STILL desktop, a second apart with nothing running, sit in two
        # rasterisations 1,376 pixels apart (header). So the leg failed or
        # passed on which one the emulator happened to be in, for a kernel
        # that had done nothing either way. §39.18.3's subject is exactly the
        # byte read here: `vid_fsx_enter` used to set it to 1 for every
        # bracket, and a bracket that sets no mode must leave the machine two
        # displays wide. The pixels still answer the coarse question, which is
        # the one they can answer: the other monitor is still lit and still
        # scanning, rather than dark.
        m.key("KeyX")
        in_bracket(m, S, pri["idx"])
        nd_same = m.read(S("vid_ndisp"), 1)[0]
        f_same = fps(m, sec["idx"])
        n_same = lit(m.fbuf(card=sec["idx"])[2])
        say("secondary in a SAME-MODE bracket: %d frames/s, %d lit, "
            "[vid_ndisp] = %d" % (f_same, n_same, nd_same))
        if nd_same != 2:
            fail.append("a SAME-MODE bracket collapsed the machine to "
                        "[vid_ndisp] = %d - §39.18.3 moved the collapse into "
                        "fsx_mode, so a bracket that sets no mode must leave "
                        "every virtual coordinate on the machine working"
                        % nd_same)
        if not f_same:
            fail.append("a SAME-MODE bracket stopped the secondary scanning "
                        "- it should not have been touched at all (§39.18.3)")
        if n_same < 0.30 * w * h:
            fail.append("a SAME-MODE bracket took the secondary from %d lit "
                        "to %d - it darked a monitor the app did not ask for "
                        "(§39.18.3: the app took the machine, not the "
                        "monitors)" % (n_before, n_same))
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
        in_bracket(m, S, pri["idx"])
        nd_mode = m.read(S("vid_ndisp"), 1)[0]
        f_during = fps(m, sec["idx"])
        n_during = lit(m.fbuf(card=sec["idx"])[2])
        say("secondary inside the bracket:  %d frames/s, %d lit, "
            "[vid_ndisp] = %d" % (f_during, n_during, nd_mode))
        # ...and the COLLAPSE, which is the same-mode leg's assertion the
        # other way up and is what makes that one mean something: a mode set
        # is the one thing that makes the other displays' geometry
        # meaningless, so it is the one thing that may take them (§39.18.3).
        if nd_mode != 1:
            fail.append("a MODE bracket left [vid_ndisp] at %d - fsx_mode is "
                        "where the collapse lives now, and without it the "
                        "app is drawing into a framebuffer of one display "
                        "with the machine still claiming two (§39.18.3)"
                        % nd_mode)
        # **DARK IS `dark_ceiling` AND NOT ZERO** (header): a blanked CGA
        # here reads ~840 of 128,000 rather than 0, the kernel's blank and a
        # host-driven one being pixel for pixel identical, so `nothing lit`
        # failed this row against a card that really was as dark as the port
        # can make it.
        if dark_ceiling is not None and 0 < n_during <= dark_ceiling:
            n_during = 0        # dark, by this run's own measurement of dark
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
                        "(SPEC.md 39.18). A card this run measured DARK at "
                        "%s lit, so this is a picture and not the renderer's "
                        "residue" % (f_during, n_during,
                                     dark[0] if dark else "?"))
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
        if a.dock:
            if m.read(S("mod_tab"), 2) != b"\x00\x00":
                fail.append("fullscreen return loaded CTRL.DRV")
            for name, expected in dock_bounds.items():
                if m.read(S(name), 2) != expected:
                    fail.append("fullscreen return changed " + name)
            say("side-dock geometry restored with CTRL.DRV unloaded")

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
