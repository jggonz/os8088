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


def lit(m):
    """(whole screen, the menu bar's rows) lit-pixel counts."""
    w, h, rows = m.vram()
    return sum(sum(r) for r in rows), sum(sum(r) for r in rows[:MBAR_H])


def wait(m, addr, want, limit=60.0):
    t = time.time()
    while time.time() - t < limit:
        if m.read(addr, 1)[0] == want:
            return True
        time.sleep(0.3)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args()

    bad = 0
    with os88marty.launch(a.image, apps=a.apps, machine=a.machine) as m:
        sv = m.sym("blk_sv")
        on = m.sym("blk_on")
        seg = m.sym("ss_row") + 2                   # DRVR_SEG
        desk, deskbar = lit(m)
        print("desktop: %d lit, %d of them in the menu bar" % (desk, deskbar))
        if desk < 1000:
            print("  ...that is not a desktop; the boot gate let something through")
            return 1

        for name, bit in MODES:
            m.write(m.sym("ss_modes"), bytes([bit]))
            m.write(m.sym("ss_secs"), b"\xc8")      # one long turn: no re-pick
            m.write(m.sym("ss_idle"), IDLE_SOON)
            m.key("Space")                          # the idle clock from HERE
            time.sleep(0.4)
            if not wait(m, sv, 1):
                print("  %-10s NEVER STARTED" % name)
                bad += 1
                continue
            image = int.from_bytes(m.read(seg, 2), "little")
            time.sleep(2.5)
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
            time.sleep(2.5)
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
            setup()
            m.write(m.sym("ss_idle"), IDLE_SOON)
            m.key("Space")
            time.sleep(0.4)
            started = wait(m, on, 1)
            time.sleep(1.0)
            total, _ = lit(m)
            ok = started and m.read(sv, 1)[0] == 0 and abs(total - desk) < 200
            print("  %-14s blanker: blk_on=%d blk_sv=%d framebuffer lit=%d %s"
                  % (name, m.read(on, 1)[0], m.read(sv, 1)[0], total,
                     "" if ok else "<-- WRONG"))
            bad += not ok
            m.write(m.sym("ss_idle"), IDLE_NEVER)
            m.key("Space")
            time.sleep(1.5)

        m.write(m.sym("ss_idle"), b"\x00\x00")      # zero minutes = OFF
        m.key("Space")
        time.sleep(8.0)
        ok = m.read(on, 1)[0] == 0 and m.read(sv, 1)[0] == 0
        print("  %-14s off: blk_on=%d blk_sv=%d after 8s of idle %s"
              % ("zero minutes", m.read(on, 1)[0], m.read(sv, 1)[0],
                 "" if ok else "<-- WRONG"))
        bad += not ok

    print("saver: %d finding(s)" % bad)
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
