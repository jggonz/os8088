#!/usr/bin/env python3
"""CLEAR SKIES IN SIXTEEN COLOURS ON A CGA (SPEC.md 88.15), on MartyPC.

    python3 tests/skies160.py [--machine os8088_5150_cga_gla]

The 160x100 text hack is the one backend whose mode the kernel has never
heard of: the app takes SPEC.md 53.4's `FSXM_TEXT80`, writes the 6845 itself
and fills every cell with the CP437 right half block, so an attribute byte
becomes two pixels in sixteen colours. Six things say it worked, and every
one of them is about something no other CGA mode can do:

  1. THE PLAYER CAN REACH IT. Flight -> Settings, the Mode row's second item,
     Done - through the drop-down and not through a poke, because
     [cs_modepref] is "which of the two this display offers" (88.15.7) and a
     row whose names cs_adapter forgot to set is a row that picks the wrong
     mode in silence;
  2. the bracket adopts CSB_C160 and lays out a 160x87 view in a 160x100
     box with a thirteen-row strip under it (88.15.5);
  3. MORE THAN FOUR COLOURS ARE ON THE GLASS. That is the whole claim, and
     it is read off the rendered frame: a CGA in 320x200 has four, and this
     picture has the sky, the grass, the asphalt, the white marks and the
     strip's green at once;
  4. every EVEN byte of the 16,000 the screen now covers is still 0xDE. The
     mode set writes the character cells once and the blit lays attributes
     at the ODD addresses only, so a single character byte that moved means
     the blit is writing pairs (which is exactly what it did when it read
     [cs_back] after DS had already moved to the shadow);
  5. THE CRTC WAS RETIMED. The strip is the bottom rows of the picture,
     which is only true at a hundred two-scan-line rows: leave R9 at 7 and
     the same VRAM draws the top quarter of the same scene across all 200
     lines with no strip in sight. --clobber-crtc is that arm;
  6. nothing stale (88.3.1): the world paused, the glass against a forced
     full redraw of the same scene. The dirty-row scheme is shared with the
     other two shadow backends and the row/byte arithmetic is not - a
     device row here is 160 bytes where the shadow row is 80.

...and the strip itself: the three readings letter, they CHANGE over a climb,
and a message takes cells 6..19 while leaving the speed standing (88.15.6).

Three red runs (docs/WRITING-TESTS.md 1). --clobber-crtc puts R9 back to 7, so
the card stays in 80x25 and check 5 must go red; --clobber-clear makes
cs_scrclear zero the screen on this backend as it does on the other two,
which takes every character cell out on a size change and leaves a picture
nothing can be seen in; and --clobber-fit lengthens the strip's own sentence
past the fourteen cells it gets and takes cs_d_msg's fit clamp out with it,
which is the whole message vanishing off the glass.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88ui                                               # noqa: E402
import dispapps                                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSB_C160 = 4
CS_ST_AIR = 1
HALF = 0xDE                             # the right half block (88.15.2.1)
POP = bytes(bin(i).count("1") for i in range(256))
bad = []


def check(cond, what):
    print("  [%s] %s" % ("PASS" if cond else "FAIL", what))
    if not cond:
        bad.append(what)


def rows160(m, y0, y1):
    """The glass over box rows y0..y1, as the ATTRIBUTE bytes: 80 a row, one
    per pixel pair. A device row is 160 bytes here and there is no bank."""
    fb = m.read(0xB8000, 0x4000)
    return [bytes(fb[y * 160 + 1:(y + 1) * 160:2]) for y in range(y0, y1)]


def chars160(m):
    """...and the CHARACTER bytes of the whole hundred rows."""
    fb = m.read(0xB8000, 0x4000)
    return fb[0:16000:2]


def colours(m):
    """The distinct RGB triples on the rendered frame, and their share."""
    w, h, data = m.fbuf(0)
    seen = {}
    for i in range(0, len(data), 3):
        c = data[i:i + 3]
        seen[c] = seen.get(c, 0) + 1
    return w, h, seen


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    ap.add_argument("--clobber-crtc", action="store_true",
                    help="leave the 6845's max scan line at 7, so the card"
                         " stays in 80x25: check 5 must go red")
    ap.add_argument("--clobber-fit", action="store_true",
                    help="the strip's sentence back to ' KNOTS' AND cs_d_msg's"
                         " does-it-fit clamp taken out: the prompt vanishes"
                         " off the glass entirely, which is the defect")
    ap.add_argument("--clobber-clear", action="store_true",
                    help="cs_scrclear zeroes the screen on this backend too,"
                         " so a size change takes the character cells out:"
                         " the size checks must go red")
    a = ap.parse_args(argv)
    os.chdir(ROOT)
    mp = dispapps._map("skies")

    def off(n):
        return dispapps.bss_off("skies", n)

    with os88ui.boot(a.image, apps=a.apps, machine=a.machine) as ui:
        m = ui.m
        win = ui.path("B:/GAMES/SKIES.O88")
        slot, seg = dispapps.pkg_seg(m, 0)
        lin = seg << 4
        base = int.from_bytes(m.readseg(seg, 8, 2), "little")

        def w(n):
            return int.from_bytes(m.readseg(seg, base + off(n), 2), "little")

        def byte(n):
            return m.readseg(seg, base + off(n), 1)[0]

        def rec(name, o):
            return int.from_bytes(m.readseg(seg, mp[name] + o, 2), "little")

        def rect(name):
            return [rec(name, 2 * i) for i in range(4)]

        def click(x, y, f=25):
            ui.mo.click(x, y)
            m.advance(frames=f)
            m.run()

        if a.clobber_crtc:
            # cs_c160_crtc is (register, value) pairs; R9 is the max scan
            # line and 1 is what makes a character row two scan lines
            at = mp["cs_c160_crtc"]
            tab = m.read(lin + at, 12)
            i = tab.find(b"\x09\x01")
            if i < 0:
                sys.exit("skies160: cs_c160_crtc does not set R9 to 1 where "
                         "this patch expects it - re-read it before trusting "
                         "the red run")
            m.pause()
            m.write(lin + at + i + 1, b"\x07")
            m.run()
            print("  (the 6845's max scan line left at 7: this run must fail)")

        if a.clobber_clear:
            # the `jne .z` that keeps 0x00DE out of the zero fill, made a
            # jump: black becomes 0 again and the half blocks go with it
            lo, hi = mp["cs_scrclear"], mp["cs_scrclear"] + 0x60
            code = m.read(lin + lo, hi - lo)
            i = code.find(b"\x04\x75")             # cmp ..., CSB_C160 / jne
            if i < 0:
                sys.exit("skies160: cs_scrclear does not hold 88.15.2's "
                         "character-cell test where this patch expects it")
            m.pause()
            m.write(lin + lo + i + 1, b"\xEB")
            m.run()
            print("  (cs_scrclear zeroes the screen: this run must fail)")

        if a.clobber_fit:
            # TWO patches, because the defect needs both halves: a message
            # one cell too long for the space it is given, and the unsigned
            # subtraction that used to turn that into a pen of 0x7FFC
            m.pause()
            m.write(lin + mp["cs_g_take"] + 6,
                    mp["cs_g_kt"].to_bytes(2, "little"))
            code = m.read(lin + mp["cs_d_msg"], 0x140)
            i = code.find(b"\x7F\x02\x31\xC9")     # jg .fits / xor cx, cx
            if i < 0:
                sys.exit("skies160: cs_d_msg does not hold 88.15.6.1's fit "
                         "clamp where this patch expects it")
            m.write(lin + mp["cs_d_msg"] + i, b"\xEB")
            m.run()
            print("  (the message's fit clamp taken out and its sentence "
                  "lengthened: this run must fail)")

        check(byte("cs_vidk") == 2,
              "the display is a real CGA, which is the one this mode is "
              "offered on (vid_kind %d)" % byte("cs_vidk"))

        # --- 1. the player can reach it, through the Settings page ----------
        # ITS OWN WINDOW FIRST, and CONFIRMED: menu_pick reads the FRONT
        # window's bar, and a launch is asynchronous - under load the Disk
        # window was still in front, so the bar read ['Apple', 'File', 'Edit',
        # 'Nav', 'Builtins', 'Clear Skies'] and the row raised on a menu that
        # was simply not on it (1 run in 4 at four-way concurrency)
        ui.raise_window(win)
        ui.menu_pick("Flight", "Settings")
        m.advance(frames=40)
        m.run()
        check(byte("cs_page") == 2, "Flight -> Settings turns to the page (%d)"
              % byte("cs_page"))
        names = int.from_bytes(m.readseg(seg, mp["cs_drmode"] + 8, 2), "little")
        check(names == mp["cs_i_mode160"],
              "the Mode row names a real CGA's two modes and not a VGA's "
              "(%04x, want %04x)" % (names, mp["cs_i_mode160"]))
        r = rect("cs_drmode")
        click((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
        check(m.readseg(seg, mp["cs_drmode"] + 16, 1)[0] == 1,
              "the press opens the Mode list")
        top = rec("cs_drmode", 22)
        click(r[0] + 20, top + 1 + 12 + 6)          # the SECOND item
        check(byte("cs_modepref") == 1,
              "picking the second item sets the mode preference (%d)"
              % byte("cs_modepref"))
        check(byte("cs_want") == CSB_C160,
              "...and cs_adapter would take the 16-colour raster (%d)"
              % byte("cs_want"))
        d = rect("cs_donerect")
        click((d[0] + d[2]) // 2, (d[1] + d[3]) // 2, f=40)
        check(byte("cs_page") == 0, "Done comes back to the title page (%d)"
              % byte("cs_page"))

        # --- 2. the bracket adopts it, at the geometry 88.15 names ----------
        m.type_text("f")
        m.advance(frames=120)
        m.run()
        back = byte("cs_back")
        check(back == CSB_C160, "the bracket adopted CSB_C160 (%d)" % back)
        if back != CSB_C160:
            print("  (nothing below can mean anything: stopping)")
            return 1
        geom = (w("cs_vw"), w("cs_vh"), w("cs_ww"), w("cs_viewh"),
                w("cs_panrows"))
        check(geom == (160, 100, 160, 87, 13),
              "a 160x87 view in a 160x100 box over a 13-row strip %s"
              % (geom,))

        # --- 4. the character cells are the mode set's, and only those -----
        ch = chars160(m)
        wrong = sum(1 for b in ch if b != HALF)
        check(wrong == 0,
              "all 8,000 character cells are still the half block (%d are "
              "not: the blit is writing pairs)" % wrong)

        # --- 3. MORE THAN FOUR COLOURS -------------------------------------
        vw, vh, seen = colours(m)
        big = {c: n for c, n in seen.items() if n >= vw * vh // 200}
        check(vw == 640 and vh == 200,
              "the card rasterises 640x200 (%dx%d)" % (vw, vh))
        check(len(big) >= 5,
              "more than a CGA's four colours are on the glass at once (%d "
              "with half a percent each: %s)"
              % (len(big), ", ".join("#%02X%02X%02X" % tuple(c)
                                     for c in sorted(big))))

        # --- 5. the CRTC was retimed: the strip is at the BOTTOM ------------
        # In plain 80x25 the same VRAM puts box row 23 on scan line 190, and
        # box row 23 is sky. A hundred two-scan-line rows put the strip
        # there, which is black under a light green rule.
        w2, h2, data = m.fbuf(0)
        def band(y0, y1):
            out = {}
            for y in range(y0, y1):
                for x in range(0, w2):
                    i = (y * w2 + x) * 3
                    c = data[i:i + 3]
                    out[c] = out.get(c, 0) + 1
            return out
        low = band(180, 198)
        nblack = low.get(b"\x00\x00\x00", 0)
        ngreen = low.get(b"\x55\xFF\x55", 0)
        check(nblack > 18 * w2 // 2 and ngreen > 0,
              "the instrument strip is the bottom of the picture - black "
              "under lit readings, which only a hundred two-scan-line rows "
              "put there (%d black and %d light green of %d; at R9 = 7 the "
              "same VRAM puts box row 23, which is sky, on scan line 190)"
              % (nblack, ngreen, 18 * w2))
        top_b = band(4, 30)
        sky = max(top_b.items(), key=lambda kv: kv[1])
        check(sky[0] != b"\x00\x00\x00" and sky[1] > 26 * w2 * 3 // 4,
              "...and the sky is the top of it (%s is %d of %d)"
              % ("#%02X%02X%02X" % tuple(sky[0]), sky[1], 26 * w2))

        # --- the strip: three readings, and they move ----------------------
        m.key("KeyW", down=True, up=False)           # full throttle...
        for _ in range(30):
            m.advance(frames=20)
            m.run()
        m.key("KeyW", down=False, up=True)
        # ...and the take-off prompt is up the whole roll, so the SPEED has
        # to be readable beside it (88.15.6)
        pany = w("cs_pany")
        strip = rows160(m, pany, pany + 13)
        spd = [row[:5 * 4] for row in strip]         # cells 0..4, 4 bytes each
        check(any(b for row in spd for b in row),
              "the speed is lettered beside the take-off prompt, in the five "
              "cells the message starts after")
        # ...AND THE PROMPT ITSELF IS ON THE GLASS. cs_d_msg centres a message
        # in what is left of the line, and that subtraction is UNSIGNED: one
        # cell too long made the pen 0x7FFC, cs_text's `and cl, 0xF8` left the
        # high bits and cs_glyph's own `js .out` then dropped every cell, so a
        # message that did not fit did not appear AT ALL. The strip is the
        # only panel narrow enough to reach it, and it reached it on the very
        # first sentence it was given (88.15.6.1)
        # THE GLYPH ROWS ONLY (3..10 of the strip): row 0 is the rule
        # cs_pface draws across the WHOLE width, so a field read from the
        # top of the strip is lit whatever the message did - which is how
        # this check passed its own red run the first time
        msgf = [row[6 * 4:] for row in strip[3:11]]  # cells 6..19
        check(any(b for row in msgf for b in row),
              "...and the take-off prompt is lettered beside it, in the "
              "fourteen the strip lends a message")
        prompt = m.readseg(seg, base + off("cs_promptc"), 16).split(b"\x00")[0]
        check(b"KT" in prompt and any(c in b"0123456789" for c in prompt),
              "...and it names THIS aeroplane's rotate speed in the strip's "
              "own units (%r)" % prompt)
        m.key("ArrowDown", down=True, up=False)      # the stick back
        for _ in range(60):
            m.advance(frames=20)
            m.run()
            if byte("cs_state") == CS_ST_AIR:
                break
        m.key("ArrowDown", down=False, up=True)
        for _ in range(15):
            m.advance(frames=20)
            m.run()
        check(byte("cs_state") == CS_ST_AIR,
              "it takes off (state %d)" % byte("cs_state"))
        check(byte("cs_msg") == 0,
              "...and the take-off prompt goes with it (%d)" % byte("cs_msg"))
        s0 = rows160(m, pany, pany + 13)
        lit0 = sum(POP[b] for row in s0 for b in row)
        check(lit0 > 0, "the strip has something on it (%d bits)" % lit0)
        for _ in range(12):
            m.advance(frames=20)
            m.run()
        s1 = rows160(m, pany, pany + 13)
        moved = sum(1 for x, y in zip(s0, s1) if x != y)
        check(moved > 0,
              "the readings change over a climb (%d of 13 strip rows moved)"
              % moved)

        # --- a SIZE change keeps the mode (88.13.4, 88.15.2) ---------------
        # cs_scrclear blacks the whole framebuffer when the view shrinks, and
        # BLACK IS NOT ZERO here: zeroing it takes the character cells out and
        # nothing the blit lays afterwards can be seen at all. It also puts
        # the strip back at the bottom of the box rather than under the
        # smaller view (88.15.5).
        m.key("F3")                                  # Size, one rung round
        m.advance(frames=60)
        m.run()
        wh2, pany2 = w("cs_viewh"), w("cs_pany")
        check(pany2 == w("cs_vh") - w("cs_panrows"),
              "the strip is still across the bottom of the box after a size "
              "change (row %d of %d, view %d)" % (pany2, w("cs_vh"), wh2))
        ch2 = chars160(m)
        wrong2 = sum(1 for b in ch2 if b != HALF)
        check(wrong2 == 0,
              "...and every character cell survived the screen clear (%d did "
              "not)" % wrong2)
        for _ in range(6):
            m.advance(frames=20)
            m.run()
        vw3, vh3, seen3 = colours(m)
        big3 = {c: n for c, n in seen3.items() if n >= vw3 * vh3 // 200}
        check(len(big3) >= 4,
              "...and the picture is still there in colour (%d colours)"
              % len(big3))
        m.key("F3")                                  # ...and back
        m.advance(frames=60)
        m.run()
        for _ in range(6):
            m.advance(frames=20)
            m.run()

        # --- 6. nothing stale (88.3.1) -------------------------------------
        m.pause()
        m.write(lin + base + off("cs_pause"), b"\x01")
        m.run()
        m.advance(frames=25)
        m.run()
        before = rows160(m, 0, w("cs_viewh"))
        m.pause()
        m.write(lin + base + off("cs_rowkind"), b"\x03" * w("cs_viewh"))
        m.run()
        m.advance(frames=25)
        m.run()
        after = rows160(m, 0, w("cs_viewh"))
        stale = sum(POP[x ^ y] for p, q in zip(before, after)
                    for x, y in zip(p, q))
        check(stale <= 8,
              "the glass matches a forced full redraw of the same scene (%d "
              "bits differ)" % stale)

        # --- and the DESKTOP comes back ------------------------------------
        # This backend leaves the 6845 retimed and the mode register poked,
        # and what undoes both is the kernel's own exit mode set (SPEC.md
        # 53.4: "nothing the app pokes outlives the exit mode set"). It is
        # the one thing here that could leave a machine unusable, so it is
        # asserted rather than assumed.
        m.pause()
        m.write(lin + base + off("cs_pause"), b"\x00")
        m.run()
        m.type_text("f")
        m.advance(frames=120)
        m.run()
        v = m.video()
        check(not v.get("text"),
              "F leaves the bracket and the desktop's graphics mode is back "
              "(%s)" % v.get("mode"))
        w4, h4, d4 = m.fbuf(0)
        seen4 = {}
        for i in range(0, len(d4), 3):
            seen4[d4[i:i + 3]] = seen4.get(d4[i:i + 3], 0) + 1
        check(len(seen4) <= 4,
              "...and it is a four-colour desktop again, not the sixteen the "
              "bracket had (%d colours)" % len(seen4))

    if bad:
        print("skies160: %d problem(s)" % len(bad))
        return 1
    print("  ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
