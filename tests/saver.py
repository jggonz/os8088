#!/usr/bin/env python3
"""The animated screen saver, end to end (SPEC.md 79).

    make && python3 tests/saver.py [--machine os8088_5150_cga_gla]

Five things, and each of them has already been the failure it checks for:

  1. **Every mode draws.** Only one mode is ticked at a time, so a picker
     that quietly falls back to mode 0 is caught - which SPEC.md 79.5's own
     note records happening for a distance of 3 or 4 with one mode ticked.
     The test is `lit` pixels on a screen the session has just blacked, so it
     cannot pass on a stale desktop.

  2. **The overlay is loaded and FREED** (SPEC.md 79.8): `ss_row + DRVR_SEG`
     is non-zero while a session runs and zero after the wake. A reap that
     stops working is a 7KB leak per idle period and nothing on screen.

  3. **The wake puts the WHOLE desktop back** (SPEC.md 79.6), inside a few
     pixels of what was there before - which is the check that caught
     `wm_paint_all` forcing neither the menu bar nor the dock, and left both
     of them BLACK with everything between them correct.

  4. **No BLOCK is left in the menu bar.** SPEC.md 79.3: the image is
     read inside a gfx-lock hold, and `fpg_finish` paints §12.8's widget span
     back at the END of that hold - so a clear inside the same hold leaves an
     80-pixel white block in the bar for the whole session. The top MBAR_H
     rows are counted directly.

  5. **All three fallbacks reach the blanker** (SPEC.md 79.1): no mode
     ticked, a file that is not there, and `[ss_mins]` = 0 meaning off. The
     first two must leave the FRAMEBUFFER untouched, because that is what
     "the video signal is gated" means (SPEC.md 64.3) and a saver that
     blacked the screen instead would look identical on a photograph.

The idle period is written directly rather than waited out: five minutes of
guest time is fifteen minutes of anybody's afternoon, and what is under test
is the machinery and not the arithmetic - `ss_mins2idle` is checked from the
settings window's side by tests that drive the UI.
"""
import sys, os, time, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))
import os88marty                                            # noqa: E402

MBAR_H = 20
MODES = [("cube", 1), ("starfield", 2), ("shapes", 4), ("sea life", 8)]
IDLE_SOON = b"\x1c\x00"          # 28 ticks, about a second and a half
IDLE_NEVER = b"\x00\x40"         # ...and a quarter of an hour


PLANAR = False                   # set once in main(), off the card's own type
VGA_BASE = 0xA0000
_NZ = bytes([0] + [1] * 255)     # bytes.translate: any non-zero byte -> 1


def _rgb_lit(d):
    """Pixels of an rgb24 frame with ANY channel non-zero, at C speed.

    A pixel is three bytes, so "lit" is the OR of three channels - indexing
    one byte per pixel reads a third of the frame at a third of the stride
    (tools/chromedown.py's warning), and taking the red byte alone calls a
    blue pixel black. Each channel's 0/1 plane becomes one big integer and
    the OR of the three is counted: 307,200 pixels without a Python loop.
    """
    t = d.translate(_NZ)
    a = (int.from_bytes(t[0::3], "big") | int.from_bytes(t[1::3], "big")
         | int.from_bytes(t[2::3], "big"))
    return bin(a).count("1")


def lit(m):
    """(whole screen, the menu bar's rows) lit-pixel counts, ON THE GLASS.

    TWO INSTRUMENTS, ONE QUESTION. On the 1bpp cards `vram` decodes SPEC.md
    39.3's banked layout out of memory and is exact; outside a gated signal
    memory and glass are the same picture, which is what every assertion here
    was written against. On a VGA there is no flat framebuffer to decode -
    mode 12h is four planes behind the Graphics Controller - so the CARD is
    asked what it rasterised (`fbuf`) and "lit" is any pixel that is not
    black. That is the 1bpp meaning exactly: os8088's VGA desktop is black
    and white (measured: 166,110 white and 141,090 black of 307,200, nothing
    else), and lit there is white.

    THIS USED TO CALL `m.vram()` ON EVERY CARD, and `vram` sent anything that
    was not a CGA down its Hercules arm - so on os8088_xt_vga it read the
    unmapped 0xB0000, which answers zeroes rather than erroring, and the
    boot gate reported "desktop: 0 lit" on a machine showing a desktop.
    `vram` now refuses a planar card instead (tools/os88marty.py).
    """
    if not PLANAR:
        w, h, rows = m.vram()
        return sum(sum(r) for r in rows), sum(sum(r) for r in rows[:MBAR_H])
    w, h, d = m.fbuf()
    return _rgb_lit(d), _rgb_lit(d[:w * MBAR_H * 3])


def memlit(m):
    """Lit pixels in the card's own MEMORY - what the blanker must not touch.

    On the 1bpp cards this is `lit` itself: `vram` already reads memory, so
    the fallbacks' "framebuffer untouched" assertion is the one it always was.

    ON A VGA THE GLASS CANNOT ANSWER IT. The blanker gates the signal through
    the Attribute Controller's Color Plane Enable (SPEC.md 64.3), MartyPC
    models that register, and so the rendered frame of a CORRECT blanker is
    black - which is exactly what a saver that blacked the framebuffer would
    show too. So memory is read directly: the debugger's `read` is a side-
    effect-free PEEK (no latch load, no Read Map Select written, nothing the
    guest can see), and MartyPC's peek answers PLANE 0. One plane is enough
    for the one thing asked - the desktop is black and white, so white has
    every plane set, and a blacking fill clears plane 0 with the rest - and
    the same plane is read on both sides of the comparison, so nothing here
    depends on which plane the peek picks.
    """
    if not PLANAR:
        return lit(m)[0]
    w = int.from_bytes(m.read(m.sym("vid_stride"), 2), "little")
    h = int.from_bytes(m.read(m.sym("vid_h"), 2), "little")
    b = m.read(VGA_BASE, w * h)
    return bin(int.from_bytes(b, "big")).count("1")


def wait(m, addr, want, limit=20.0):
    """Wait for a kernel byte, ON THE GUEST'S CLOCK.

    Every wait in this file used to be `time.time()` and `time.sleep`, and
    what the saver is waiting FOR is guest time: `ss_idle` is a count of
    TICKS, so "28 ticks after the last keypress" is 1.5 seconds of the 8088
    and whatever the box makes of it. In a three-wide emulator lane that is a
    different amount of host wall clock, which is
    the contention mechanism exactly - and this row
    took 110.7s there against 52.3s alone.
    """
    try:
        os88marty.until(m, lambda mm: mm.read(addr, 1)[0] == want,
                        "kernel byte %#07x to read %d" % (addr, want),
                        poll=0.05, guest=limit)
        return True
    except os88marty.MartyError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()

    global PLANAR
    bad = 0
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        vt = m.video()["type"]
        PLANAR = vt not in ("cga", "mda", "hercules")
        sv = m.sym("blk_sv")
        on = m.sym("blk_on")
        seg = m.sym("ss_row") + 2                   # DRVR_SEG
        desk, deskbar = lit(m)
        deskmem = memlit(m)
        print("desktop (%s, %s): %d lit, %d of them in the menu bar; "
              "%d lit in memory"
              % (vt, "fbuf + plane 0" if PLANAR else "vram", desk, deskbar,
                 deskmem))
        if desk < 1000 or deskmem < 1000:
            print("  ...that is not a desktop; the boot gate let something through")
            return 1

        for name, bit in MODES:
            m.write(m.sym("ss_modes"), bytes([bit]))
            m.write(m.sym("ss_secs"), b"\xc8")      # one long turn: no re-pick
            m.write(m.sym("ss_idle"), IDLE_SOON)
            m.key("Space")                          # the idle clock from HERE
            os88marty.guest_sleep(m, 0.4)
            if not wait(m, sv, 1):
                print("  %-10s NEVER STARTED" % name)
                bad += 1
                continue
            image = int.from_bytes(m.read(seg, 2), "little")
            os88marty.guest_sleep(m, 2.5)
            total, bar = lit(m)
            # A MODE MAY LEGITIMATELY DRAW IN THE BAR'S ROWS - a figure is
            # placed anywhere and a fish swims at any height - so the bar
            # test is for a BLOCK and not for silence: fpg_finish's span is
            # 80 x 19 = 1,520 solid pixels, and the busiest mode measured
            # here puts 73 up there. The floor on `total` is the starfield's:
            # 28 mostly-single-pixel stars, of which about 20 are on screen.
            ok = total > 10 and image and bar < 600
            print("  %-10s image=%04X  lit=%-5d bar=%-4d %s"
                  % (name, image, total, bar, "" if ok else "<-- WRONG"))
            bad += not ok

            m.write(m.sym("ss_idle"), IDLE_NEVER)
            m.key("Space")
            os88marty.guest_sleep(m, 2.5)
            back, backbar = lit(m)
            image = int.from_bytes(m.read(seg, 2), "little")
            ok = (m.read(on, 1)[0] == 0 and m.read(sv, 1)[0] == 0
                  and image == 0 and abs(back - desk) < 200)
            print("  %-10s woke: lit=%-5d bar=%-4d image=%04X %s"
                  % ("", back, backbar, image, "" if ok else "<-- WRONG"))
            bad += not ok

        # --- the three fallbacks (SPEC.md 79.1) -----------------------------
        for name, setup in (
                ("no mode ticked", lambda: m.write(m.sym("ss_modes"), b"\x00")),
                ("no SAVER.DRV", lambda: (m.write(m.sym("ss_modes"), b"\x0f"),
                                          m.write(m.sym("ss_fname"), b"NOSUCH"))),
        ):
            # **THE BLANKER HAS TO BE OFF BEFORE IT CAN BE SEEN TO COME ON**,
            # and confirming that is the whole of what was wrong here. The
            # wait below is for `blk_on == 1`, and the previous iteration
            # leaves it 1 - so it could return on a session that was already
            # running, and whether it did depended on how fast the box got
            # round to the trailing keypress. Measured: in a three-wide lane
            # "no mode ticked" failed and "no SAVER.DRV" passed; alone, the
            # same run failed the OTHER one. Exactly one of the two, either
            # way round, which is a race and not a saver.
            #
            # So: idle never, wake it, and prove it is off. Then arm.
            m.write(m.sym("ss_idle"), IDLE_NEVER)
            m.key("Space")
            if not wait(m, on, 0):
                print("  %-14s could not be woken before the test - blk_on is "
                      "still 1, so nothing below would be measuring this "
                      "fallback" % name)
                bad += 1
                continue
            setup()
            m.write(m.sym("ss_idle"), IDLE_SOON)
            m.key("Space")
            os88marty.guest_sleep(m, 0.4)
            started = wait(m, on, 1)
            os88marty.guest_sleep(m, 1.0)
            total = memlit(m)
            ok = (started and m.read(sv, 1)[0] == 0
                  and abs(total - deskmem) < 200)
            glass = ""
            if PLANAR:
                # ...AND ON A VGA THE GATE ITSELF IS VISIBLE, so it is asked
                # too. MartyPC models AC 12h, so a blanker that declined to
                # blank this card - SPEC.md 64.3's "silently declining on one
                # adapter in three", which is how the VGA arm came to be
                # written - leaves the desktop on the glass and is caught
                # here, where memory alone would read it as a pass. The
                # 1bpp legs cannot ask it the same way (MartyPC models the
                # CGA's video-enable bit and not the mono card's; see
                # dispsaver), so they keep the memory half alone.
                g, _ = lit(m)
                ok = ok and g < 50
                glass = " glass lit=%d" % g
            print("  %-14s blanker: blk_on=%d blk_sv=%d framebuffer lit=%d%s %s"
                  % (name, m.read(on, 1)[0], m.read(sv, 1)[0], total, glass,
                     "" if ok else "<-- WRONG"))
            bad += not ok
            m.write(m.sym("ss_idle"), IDLE_NEVER)
            m.key("Space")
            os88marty.guest_sleep(m, 1.5)

        m.write(m.sym("ss_idle"), b"\x00\x00")      # zero minutes = OFF
        m.key("Space")
        # TIME: a negative. IDLE_SOON starts a session in 28 ticks, so 4
        # guest seconds (~73 ticks) is well past any start a zero could cause
        os88marty.guest_sleep(m, 4.0)
        ok = m.read(on, 1)[0] == 0 and m.read(sv, 1)[0] == 0
        print("  %-14s off: blk_on=%d blk_sv=%d after 4s of idle %s"
              % ("zero minutes", m.read(on, 1)[0], m.read(sv, 1)[0],
                 "" if ok else "<-- WRONG"))
        bad += not ok

    print("saver: %d finding(s)" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
