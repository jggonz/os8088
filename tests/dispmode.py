#!/usr/bin/env python3
"""Single or Extend, where the second display sits, and does it survive a
reboot? (SPEC.md 39.19, 31.10.2, 39.11.3)

    make && python3 tests/dispmode.py
    python3 tests/dispmode.py --primary herc      # ...with make VIDEO=herc

`dispcheck.py` proves the extended desktop WORKS; this proves it is a
CHOICE - which is the whole of step 7, and the half a user can see.

Six things, in the order they can fail:

  1. THE DEFAULT IS SINGLE on a machine with two cards. SPEC.md 39.19.1: the
     kernel can detect a second card and nothing it can do detects a second
     monitor, so extending by default puts half the desktop - and the pointer
     that would reach the control to undo it - on glass that may not exist.
     The second card is DARK, which is the same measurement SPEC.md 39.18.1
     needs and has the same trap in it (see `dark` below).
  2. THE PAGE OFFERS THE CHOICE. Opening the Control Panel on its Display
     page and clicking `Right` has to reach [vid_dmode].
  3. RIGHT AND BELOW PLACE display 1 where they say (SPEC.md 39.19.2), and
     the desktop becomes the union while the CHROME stays the primary's -
     the pair SPEC.md 39.16 is about.
  4. SINGLE PUTS IT BACK: the second card dark again, the desktop back to one
     display, and - the part with no second opinion - every window clamped
     back onto a screen that exists (SPEC.md 39.19's vid_disp_relayout).
  5. IT SURVIVES A REBOOT, which is the `VM` key widening (SPEC.md 39.11.3).
     The panel must be CLOSED for that: closing is what writes SYSTEM.CFG
     (SPEC.md 31.8), so a test that quits with the panel open measures the
     defaults again and passes for the wrong reason.
  6. AND THE RECORDED BYTES ARE OBEYED, not merely stored - the reboot comes
     up with two displays without anybody clicking anything.

DARK IS TWO DIFFERENT MEASUREMENTS, one per adapter, and SPEC.md 39.18.1 has
the numbers: a Hercules with 3B8h bit 3 clear STOPS SCANNING (frames go to 0
and `fbuf` then returns the last frame it rasterised for ever), while a CGA
with 3D8h bit 3 clear keeps its CRTC running and gates the output (frames
hold, lit pixels go to 0). Either one alone passes a kernel that never wrote
the other card's port.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402

# ...the kind/card maps and the two pairings live in dispcp, which is where
# every dual-display test already shares its one copy of a fact.
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402

KERNEL_SEG = 0x0060
DBG_TAG_VIDEO = 0x4456
MBAR_H = 20
S = os88sym.linear


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


class VD(object):
    """SPEC.md 57.4's block, read whole."""

    def __init__(self, m):
        base = KERNEL_SEG << 4
        reg = u16(m.read(base + 0x0E, 2))
        if not reg:
            raise RuntimeError("this kernel publishes no debug registry")
        blk = None
        while True:
            tag = u16(m.read(base + reg, 2))
            if not tag:
                break
            if tag == DBG_TAG_VIDEO:
                blk = u16(m.read(base + reg + 2, 2))
                break
            reg += 4
        if blk is None:
            raise RuntimeError("this kernel publishes no 'VD' block "
                               "(built before SPEC.md 57.4)")
        p = [u16(m.read(base + blk, 18), i) for i in range(0, 18, 2)]
        if p[0] != DBG_TAG_VIDEO:
            raise RuntimeError("the 'VD' offset does not lead with its tag")
        self.kind = m.read(base + p[1], 1)[0]
        self.avail = m.read(base + p[2], 1)[0]
        nd = m.read(base + p[3], 8)
        self.ndisp, self.cur = nd[0], nd[1]
        self.dmode, self.dlay = nd[6], nd[7]
        self.ptr = m.read(base + p[4], 1)[0]
        d = m.read(base + p[7], 4)
        self.desk = (u16(d), u16(d, 2))
        c = m.read(base + p[8], 4)
        self.chrome = (u16(c), u16(c, 2))
        self.disp = []
        for i in range(self.ndisp):
            r = m.read(base + p[5] + i * p[6], 42)
            self.disp.append({"kind": r[40], "vx": u16(r, 36),
                              "vy": u16(r, 38), "cw": u16(r, 14),
                              "ch": u16(r, 16), "seg": u16(r, 0)})

    def __str__(self):
        s = ("kind %d avail %#x ndisp %d dmode %d dlay %d ptr %d desk %dx%d "
             "chrome %dx%d" % (self.kind, self.avail, self.ndisp, self.dmode,
                               self.dlay, self.ptr, self.desk[0], self.desk[1],
                               self.chrome[0], self.chrome[1]))
        for i, d in enumerate(self.disp):
            s += "\n      d%d kind %d at (%d,%d) %dx%d seg %04x" % (
                i, d["kind"], d["vx"], d["vy"], d["cw"], d["ch"], d["seg"])
        return s


def lit(px):
    return sum(1 for i in range(0, len(px), 3) if px[i])


def dark(m, card):
    """Is this monitor showing nothing? Two adapters, two answers, and the
    disjunction is what covers both (SPEC.md 39.18.1). Returns (bool, why).

    ...EXCEPT THAT ONE OF THE TWO CANNOT BE ASKED ON THIS EMULATOR, and saying
    so is better than a red result nobody can act on. MartyPC's MDA does not
    model 3B8h's VIDEO ENABLE bit. Measured by driving the port from the host
    on a Hercules that os8088 has PROGRAMMED (an unprogrammed one reads 0
    frames and 0 lit whatever you write to it, which is what the first attempt
    at this measured), the three values are:

        3B8h = 0x0A   graphics + video ON     75 frames/0.7s, 367,488 lit
        3B8h = 0x02   graphics, video OFF     76 frames/0.7s, 367,488 lit
        3B8h = 0x00   text, video off          0 frames/0.7s

    `0x02` is what vid_blank_kind writes to blank the card, and it is
    INDISTINGUISHABLE from `0x0A` here - same raster, same pixels. So no
    observation of this card can tell a blanked Hercules from a live one, and
    what stops its raster in MartyPC is leaving GRAPHICS mode (bit 1), not
    disabling VIDEO (bit 3).

    The kernel is right and the model is short: bit 3 is the video enable on
    every MDA and HGC, and gating it is what SPEC.md 39.18.1 specifies and what
    keeps the CRTC running so the monitor never re-acquires. Writing 0x00
    instead would "pass" here and is the defect vid_blank_kind's own header
    records being fixed - it does not blank the card, it puts it into MDA TEXT
    mode with a 6845 still carrying 720x348 timings. (Note the 0x00 row above
    has no lit count: `fbuf` on a stopped card hands back the last frame it
    rasterised, so it reads as a perfectly ordinary desktop.)

    So a MONO secondary is reported and not judged. A CGA one still is: 3D8h
    bit 3 clear takes its lit count to 0 with the CRTC running, which the model
    does show. The 5150 is where the mono half lands (docs/FIELD-MACHINES.md) -
    it is one of the three things only a real monitor can answer.

    tests/fsxdisp.py's header carries a "3B8h bit 3 clear -> 163 frames/s
    becomes 0" from before that fix landed (a32be1d predates e3e0fb2), so it
    is a measurement of 0x00 wearing bit 3's name. Its Hercules leg is
    measuring the model rather than the kernel too, and has not been re-run.
    """
    idx = card["idx"]
    a = [c for c in m.cards() if c["idx"] == idx][0]["frames"]
    time.sleep(1.0)
    f = [c for c in m.cards() if c["idx"] == idx][0]["frames"] - a
    n = lit(m.fbuf(card=idx)[2])
    if f == 0:
        return True, "stopped scanning (0 frames/s)"
    if n == 0:
        return True, "scanning nothing (%d frames/s, 0 lit)" % f
    if card["type"] == "mda":
        return None, ("%d frames/s with %d lit - NOT JUDGED: MartyPC's MDA "
                      "does not model 3B8h bit 3 (see dark())" % (f, n))
    return False, "%d frames/s with %d pixels lit" % (f, n)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_both_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--primary", choices=("auto", "herc", "cga", "vga"),
                    default="auto",
                    help="which card the KERNEL drives; anything but auto "
                         "means a VIDEO= build, and picks the boot gate's card")
    a = ap.parse_args(argv)

    fail = []
    say = lambda s: print("  " + s)

    # THE REBOOT IS A RESET AND NOT A SECOND LAUNCH, which is the one thing to
    # know before changing this. MartyPC mounts a floppy by reading it into
    # RAM and os88marty.launch copies the image into its run directory first,
    # so a guest's SYSTEM.CFG write reaches NEITHER build/os8088-360.img nor
    # anything a second launch would mount - and the machine then comes up at
    # the defaults, which is what "it did not persist" looks like when the
    # test is what did not persist. A reset re-runs the POST against the same
    # in-RAM media, which is what a reboot is.
    m = os88marty.launch(a.image, apps=a.apps, machine=a.machine, boot=False)
    cards = m.cards()
    if len(cards) != 2:
        m.close()
        sys.exit("dispmode: %s has %d video card(s), not 2" % (a.machine,
                                                               len(cards)))
    gate = None
    if a.primary != "auto":
        gate = [c for c in cards
                if c["type"] == dispcp.PRIMARY_CARD[a.primary]][0]["idx"]
    m.run()
    os88marty.settle(m, gate=os88marty.desktop_up, card=gate)

    try:
        v = VD(m)
        say("at boot: %s" % v)
        pri = [c for c in cards if c["type"] == dispcp.KIND_CARD[v.kind]][0]
        sec = [c for c in cards if c is not pri][0]
        if gate is None:
            gate = pri["idx"]
        say("primary = card %d (%s), secondary = card %d (%s)"
            % (pri["idx"], pri["type"], sec["idx"], sec["type"]))

        # --- 1: the default is Single, and the other monitor is dark --------
        if v.avail not in (dispcp.AVAIL_HERC_CGA, dispcp.AVAIL_VGA_HERC):
            fail.append("[vid_avail] is %#x, and this machine is neither of "
                        "the two-card pairings everything below is about: "
                        "%#x is a Hercules and a CGA, %#x a VGA and a Hercules"
                        % (v.avail, dispcp.AVAIL_HERC_CGA, dispcp.AVAIL_VGA_HERC))
        if v.ndisp != 1 or v.dmode != 0:
            fail.append("a machine that has never been asked came up with "
                        "ndisp=%d dmode=%d - SPEC.md 39.19.1 says Single"
                        % (v.ndisp, v.dmode))
        if v.desk != v.chrome:
            fail.append("Single, and the desktop %s is not the chrome %s"
                        % (v.desk, v.chrome))
        ok, why = dark(m, sec)
        say("the second card at boot: %s" % why)
        if ok is False:
            fail.append("the second card is live on a Single machine - it is "
                        "showing whatever was on it before os8088 booted")
        # ...and this one cannot tell "blanked" from "never programmed", which
        # on a Hercules look identical (a card nobody has programmed produces
        # no frames for ever). That is fine here - both ARE dark, which is the
        # claim - and step 4 below is the measurement with teeth, because by
        # then the card has been extended onto and is demonstrably scanning.

        # --- 2 and 3: the page offers the choice, and places display 1 ------
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_panel(m, mo, S, os88marty.settle, card=gate)
        say("the Control Panel is open on its Display page")

        # Right places display 1 at (primary cw, MBAR_H) and not at
        # (primary cw, 0): the second card has no menu bar, so its top row is
        # aligned with the primary's DESKTOP BAND rather than its screen
        # (SPEC.md 39.19.3). Below is unchanged - its seam is horizontal.
        for which, wx, wy in (("right", "cw", MBAR_H), ("below", 0, "ch")):
            dispcp.set_mode(m, mo, S, os88marty.settle, which, card=gate)
            v = VD(m)
            say("after '%s': %s" % (which, v))
            if v.ndisp != 2 or v.dmode != 1:
                fail.append("clicking '%s' left ndisp=%d dmode=%d"
                            % (which, v.ndisp, v.dmode))
                continue
            d0, d1 = v.disp[0], v.disp[1]
            want = (d0["cw"] if wx == "cw" else 0,
                    d0["ch"] if wy == "ch" else wy)
            if (d1["vx"], d1["vy"]) != want:
                fail.append("'%s' put display 1 at (%d,%d), wanted %s"
                            % (which, d1["vx"], d1["vy"], want))
            union = (max(d0["cw"], d1["vx"] + d1["cw"]),
                     max(d0["ch"], d1["vy"] + d1["ch"]))
            if v.desk != union:
                fail.append("'%s' left the desktop %s, wanted the union %s"
                            % (which, v.desk, union))
            if v.chrome != (d0["cw"], d0["ch"]):
                fail.append("'%s' moved the CHROME to %s - SPEC.md 39.16 says "
                            "it stays the primary's %s"
                            % (which, v.chrome, (d0["cw"], d0["ch"])))
            ok, why = dark(m, sec)
            if ok is True:
                fail.append("the second card is dark while extended: %s" % why)

        # --- 3b: swapping the PRIMARY is the other two arrangements --------
        # SPEC.md 39.19.2 offers two placements and claims all four physical
        # arrangements, on the grounds that the primary is always at the
        # virtual origin so which monitor is on the left is answered by which
        # CARD is primary. That claim is load-bearing - it is the whole reason
        # there is no Left and no Above - so it is tested rather than asserted.
        dispcp.set_mode(m, mo, S, os88marty.settle, "right", card=gate)
        was = VD(m)
        other = 0 if was.kind == 2 else 1        # slot 0 is Herc, slot 1 Cga
        dispcp.set_primary(m, mo, S, os88marty.settle, other, card=gate)
        v = VD(m)
        say("primary swapped: %s" % v)
        if v.kind == was.kind:
            fail.append("Activate did not move the primary off adapter %d"
                        % was.kind)
        elif v.ndisp != 2:
            fail.append("swapping the primary lost the extended desktop "
                        "(ndisp=%d)" % v.ndisp)
        else:
            if v.disp[0]["kind"] != v.kind or (v.disp[0]["vx"],
                                               v.disp[0]["vy"]) != (0, 0):
                fail.append("after the swap display 0 is kind %d at (%d,%d) - "
                            "the primary must be the adapter at the origin"
                            % (v.disp[0]["kind"], v.disp[0]["vx"],
                               v.disp[0]["vy"]))
            if v.disp[1]["kind"] != was.kind:
                fail.append("after the swap display 1 is kind %d, not the "
                            "adapter that used to be primary (%d)"
                            % (v.disp[1]["kind"], was.kind))
            if v.disp[1]["vx"] != v.disp[0]["cw"]:
                fail.append("'right' survived the swap as x=%d, wanted %d"
                            % (v.disp[1]["vx"], v.disp[0]["cw"]))
            if v.chrome != (v.disp[0]["cw"], v.disp[0]["ch"]):
                fail.append("the chrome is %s after the swap, not the NEW "
                            "primary's %s" % (v.chrome,
                                              (v.disp[0]["cw"],
                                               v.disp[0]["ch"])))
        # ...and back, so the rows below are about the machine that booted
        dispcp.set_primary(m, mo, S, os88marty.settle,
                           1 - other, card=gate)
        v = VD(m)
        if v.kind != was.kind:
            fail.append("could not swap the primary back (kind %d)" % v.kind)
        say("primary swapped back: kind %d, display 1 at (%d,%d)"
            % (v.kind, v.disp[1]["vx"], v.disp[1]["vy"]) if v.ndisp == 2
            else "primary swapped back: kind %d, ndisp %d" % (v.kind, v.ndisp))

        # --- 4: and Single puts it back ------------------------------------
        dispcp.set_mode(m, mo, S, os88marty.settle, "single", card=gate)
        v = VD(m)
        say("back to 'single': %s" % v)
        if v.ndisp != 1 or v.dmode != 0:
            fail.append("'single' left ndisp=%d dmode=%d" % (v.ndisp, v.dmode))
        if v.desk != v.chrome:
            fail.append("'single' left the desktop %s against a chrome of %s"
                        % (v.desk, v.chrome))
        ok, why = dark(m, sec)
        say("the second card after 'single': %s" % why)
        if ok is False:
            fail.append("'single' left the second card live: %s" % why)
        # ...and every window has to be back on a screen that exists. The
        # Control Panel is the one on screen, and it is the one that would be
        # stranded, having been re-fitted onto a desktop twice as wide.
        w = dispcp._cp_win(m, S)
        if w is None:
            fail.append("the Control Panel vanished across the mode changes")
        elif w[0] + 320 > v.desk[0] or w[1] > v.desk[1]:
            fail.append("the Control Panel is at %s on a %s desktop - "
                        "vid_disp_relayout did not re-fit it" % (w, v.desk))
        else:
            say("the Control Panel is at %s, inside a %s desktop" % (w, v.desk))

        # --- 5 and 6: it survives a reboot ---------------------------------
        dispcp.set_mode(m, mo, S, os88marty.settle, "below", card=gate)
        dispcp.close_panel(m, mo, S, os88marty.settle, card=gate)
        v = VD(m)
        say("set to 'below' and the panel CLOSED (which is what writes "
            "SYSTEM.CFG): dmode %d dlay %d" % (v.dmode, v.dlay))

        # --- 5 and 6, continued: and now REBOOT ----------------------------
        m.reset()
        m.run()
        os88marty.settle(m, gate=os88marty.desktop_up, card=gate)
        v = VD(m)
        say("after a reboot: %s" % v)
        if v.ndisp != 2 or v.dmode != 1 or v.dlay != 1:
            fail.append("the reboot came up ndisp=%d dmode=%d dlay=%d, not "
                        "the 2/1/1 that was saved - SPEC.md 39.11.3's 'VM' "
                        "key" % (v.ndisp, v.dmode, v.dlay))
        elif v.disp[1]["vy"] != v.disp[0]["ch"] or v.disp[1]["vx"]:
            fail.append("the reboot STORED 'below' and placed display 1 at "
                        "(%d,%d) anyway" % (v.disp[1]["vx"], v.disp[1]["vy"]))
    finally:
        m.close()

    print()
    for f in fail:
        print("dispmode: FAIL: %s" % f)
    if fail:
        return 1
    print("dispmode: Single/Right/Below is a choice, it places display 1, and "
          "it survives a reboot - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
