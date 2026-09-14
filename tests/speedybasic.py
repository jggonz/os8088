#!/usr/bin/env python3
"""Drive the native Speedy Basic window through its basic UI lifecycle.

    make speedybasicdisk && python3 tests/speedybasic.py

The retained BASIC screen is inspected in package memory and the repaint is
checked against rendered pixels.  This keeps the test independent of font
antialiasing or a golden screenshot while still failing if W_PAINT loses the
program output.
"""
import argparse
import hashlib
import os
import struct
import sys
import time

sys.path.insert(0, "tools")
sys.path.insert(0, "tests")
import dispcp                                                # noqa: E402
import os88fixture                                           # noqa: E402
import os88geom                                              # noqa: E402
import os88marty                                             # noqa: E402
import os88mouse                                             # noqa: E402
import os88sym                                               # noqa: E402
from os88map import Syms                                     # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYSTEM = "build/os8088-360.img"
APPS = "build/speedybasic.img"
MACHINE = "os8088_5150_herc_gla_144"
SB_DONE = 4
W_SEG = 22                         # SPEC.md 11's package segment field
FAIL = []


def u16(data, off=0):
    return struct.unpack_from("<H", data, off)[0]


def check(ok, message):
    if not ok:
        FAIL.append(message)


def byte(m, base, off):
    return m.read(base + off, 1)[0]


def word(m, base, off):
    return u16(m.read(base + off, 2))


def wait_byte(m, address, wanted, frames=180):
    for _ in range(frames):
        if m.read(address, 1)[0] == wanted:
            return True
        m.advance(frames=1)
    return False


def wait_word(m, address, wanted, frames=900):
    for _ in range(frames):
        if u16(m.read(address, 2)) == wanted:
            return True
        m.advance(frames=1)
    return False


def crop_digest(m, rect):
    """Hash one rectangle of the rendered card, excluding changing chrome."""
    width, height, pixels = m.fbuf()
    x, y, w, h = rect
    x = max(0, x); y = max(0, y)
    w = max(0, min(w, width - x)); h = max(0, min(h, height - y))
    cpp = len(pixels) // (width * height)
    out = bytearray()
    for row in range(y, y + h):
        start = (row * width + x) * cpp
        out.extend(pixels[start:start + w * cpp])
    return hashlib.sha256(out).hexdigest(), len(set(out))


def capture(m, directory, name):
    if not directory:
        return
    os.makedirs(directory, exist_ok=True)
    width, height, pixels = m.fbuf()
    path = os.path.join(directory, name)
    os88marty.write_png_rgb(path, width, height, pixels)
    print("  screenshot -> " + path)


def package_segment(m, S, slot):
    rec = m.read(S("wm_wins") + slot * os88geom.WIN_SIZE,
                 os88geom.WIN_SIZE)
    seg = u16(rec, W_SEG)
    if not seg or m.read(seg << 4, 2) != b"O8":
        raise RuntimeError("Speedy Basic window has no live package segment")
    return seg


def open_named(m, mouse, S, wx, wy, name):
    """Retry only the emulator's documented lost-double-click condition."""
    last = None
    for attempt in (1, 2, 3):
        try:
            dispcp.open_named(m, mouse, S, os88marty.settle,
                              wx, wy, name)
            return
        except (os88marty.MartyError, RuntimeError) as error:
            # A late package launch can cross os88ui's timeout boundary: its
            # diagnostic snapshot then contains the successfully opened app.
            if name == "SPEEDYBA.O88" and any(
                    w.visible and w.title == "Speedy Basic"
                    for w in os88geom.windows(m, S)):
                print("      open %s -> 'Speedy Basic' (late)" % name)
                return
            if "two FIRST clicks" not in str(error):
                raise
            last = error
            print("  navigation retry %d for %s: %s" %
                  (attempt, name, str(error)[:120]))
    raise last


def open_drive(m, mouse, S):
    last = None
    for attempt in (1, 2, 3):
        try:
            return dispcp.open_drive(m, mouse, S, os88marty.settle, "B")
        except os88marty.MartyError as error:
            last = error
            print("  navigation retry %d for drive B: %s" %
                  (attempt, str(error)[:120]))
    raise last


def open_app_file(m, mouse, S, app_win, path):
    """Choose a volume-root-relative path through the shipping Open dialog."""
    ui = dispcp._ui(m, mouse, None, S)
    ui.raise_window(app_win)
    m.run()
    ui.menu_pick("File", "Open")
    os88marty.settle(m, limit=120)
    check(any(w.visible and w.title == "Open"
              for w in os88geom.windows(m, S)),
          "File > Open did not create its dialog")

    def rows():
        return [row[0] for row in dispcp.snapshot(m, S)]

    def pick(name):
        listing = rows()
        check(name in listing, "Open dialog did not list " + name)
        if name not in listing:
            return False
        for _ in range(listing.index(name) + 1):
            m.run(); m.key("ArrowDown"); time.sleep(0.08)
        check(word(m, 0, S("fdlg_sel")) == listing.index(name),
              "Open dialog selected the wrong row for " + name)
        m.run(); m.key("Enter")
        os88marty.settle(m, limit=180)
        return True

    while rows()[:1] == [".."]:
        if not pick(".."):
            return
    for part in path.split("/"):
        if not pick(part):
            return


def leave_output(m, mouse, S, SB, base, app_win):
    ui = dispcp._ui(m, mouse, None, S)
    ui.raise_window(app_win)
    # Leave the expensive live graphics view before File > Open or Close. F6
    # lets the queued key take effect as soon as the retained repaint ends;
    # the shipping callbacks stop the old program before replacing/closing it.
    if byte(m, base, SB.sym("_sbu_view")) == 1:
        m.run(); m.key("F6")
        check(wait_byte(m, base + SB.sym("_sbu_view"), 0, frames=3600),
              "F6 did not leave graphical Output before the next action")
        m.run(); os88marty.settle(m, limit=180)


def run(capture_dir=None, machine=MACHINE):
    os88fixture.need(APPS)
    SB = Syms("apps/speedybasic/speedybasic.asm",
              "build/speedybasic.bin", ["apps", "build"])
    S = os88sym.linear

    with os88marty.launch(SYSTEM, apps=APPS, machine=machine) as m:
        mouse = os88mouse.Mouse(marty=m)
        os88marty.no_saver(m)

        open_drive(m, mouse, S)
        slot = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, slot)[:2]
        open_named(m, mouse, S, wx, wy, "SPEEDY")
        slot = dispcp.win_list(m, S)[-1]
        wx, wy = dispcp.win_rect(m, S, slot)[:2]
        open_named(m, mouse, S, wx, wy, "SPEEDYBA.O88")
        m.run(); os88marty.settle(m)

        wins = dispcp.win_list(m, S)
        app_slot = wins[-1]
        # Package creation does not itself promise z-order.  Raise the new
        # window explicitly before sending shortcuts; keys follow wm_top.
        app_win = next(w for w in os88geom.windows(m, S) if w.i == app_slot)
        dispcp._ui(m, mouse, None, S).raise_window(app_win)
        seg = package_segment(m, S, app_slot)
        base = seg << 4
        check(m.read(S("ld_status"), 1)[0] == 0,
              "the package loader reported an error")
        check(byte(m, base, SB.sym("_sbu_view")) == 0,
              "the launched app did not start in Editor view")
        capture(m, capture_dir, "speedybasic-editor.png")

        # F6 is both directions through the two retained views.
        # settle/advance leave Marty paused; resume before injecting each key.
        m.run()
        m.key("F6")
        check(wait_byte(m, base + SB.sym("_sbu_view"), 1),
              "F6 did not switch Editor to Output")
        m.run(); m.key("F6")
        check(wait_byte(m, base + SB.sym("_sbu_view"), 0),
              "the second F6 did not switch Output back to Editor")

        # Run the built-in program. Its exact retained rows prove parser,
        # scheduler and text-host delivery rather than merely a view change.
        m.run(); m.key("F5")
        status_addr = base + SB.sym("_sbc_status")
        check(wait_byte(m, status_addr, SB_DONE),
              "the welcome program did not reach DONE")
        check(byte(m, base, SB.sym("_sbu_view")) == 1,
              "Run did not select Output view")
        chars = bytes(m.read(base + SB.sym("_sbs_ch"), 160))
        check(chars[:22] == b"Speedy Basic is ready.",
              "the first welcome PRINT did not reach the retained screen")
        check(chars[80:96] == b"Press F5 to run.",
              "the second welcome PRINT did not reach the retained screen")
        m.run(); os88marty.settle(m)
        capture(m, capture_dir, "speedybasic-welcome.png")

        # Ctrl+F must take and release the kernel fullscreen latch. A second
        # toggle is deliberate: Escape belongs to a running BASIC program.
        m.run(); m.ctrl("KeyF")
        check(wait_byte(m, base + SB.sym("_sbu_full"), 1),
              "Ctrl+F did not enter app fullscreen")
        check(u16(m.read(S("wm_fs"), 2)) != 0,
              "the app flag changed but the kernel fullscreen latch did not")
        m.run(); m.ctrl("KeyF")
        check(wait_byte(m, base + SB.sym("_sbu_full"), 0),
              "the second Ctrl+F did not leave app fullscreen")
        check(u16(m.read(S("wm_fs"), 2)) == 0,
              "the kernel fullscreen latch survived exit")
        m.run()                         # advance() deliberately leaves paused

        # Cover the output with Control Panel, close it, then compare the
        # repaired application body with its pre-cover rendering.
        wx, wy, ww, wh = dispcp.win_rect(m, S, app_slot)
        mouse.to(4, 4)
        m.run(); os88marty.settle(m)
        body = (wx + 8, wy + os88geom.TITLE_H + 16,
                max(1, ww - 16), max(1, wh - os88geom.TITLE_H - 30))
        before, colors = crop_digest(m, body)
        check(colors > 1, "the Output view rendered as a flat empty region")
        dispcp.open_panel(m, mouse, S, os88marty.settle, page=None)
        dispcp.close_panel(m, mouse, S, os88marty.settle)
        mouse.to(4, 4)
        os88marty.settle(m)
        after, _ = crop_digest(m, body)
        check(after == before,
              "cover/expose repaint did not restore the Output view")

        # Load a shipped graphical program through the app's own File menu,
        # then record both of the presentation modes used by the PR.
        app_win = next(w for w in os88geom.windows(m, S) if w.i == app_slot)
        open_app_file(m, mouse, S, app_win,
                      "SPEEDY/DEMOS/PATTERN.BAS")
        check(byte(m, base, SB.sym("_sbu_view")) == 0,
              "opening PATTERN.BAS did not return to Editor view")
        m.run(); m.key("F5")
        check(wait_byte(m, status_addr, SB_DONE),
              "PATTERN.BAS did not reach DONE")
        screen = word(m, base, SB.sym("_sbs_screen"))
        mode = word(m, base, SB.sym("_sbs_mode"))
        logical_w = word(m, base, SB.sym("_sbs_lw"))
        logical_h = word(m, base, SB.sym("_sbs_lh"))
        print("  graphics state: screen=%d mode=%d %dx%d" %
              (screen, mode, logical_w, logical_h))
        line_args = tuple(word(m, base, SB.sym("_sbs_arg") + i * 2)
                          for i in range(5))
        check(line_args == (20, 180, 300, 180, 13),
              "PATTERN's last LINE did not reach the screen callback")
        check(screen == 7,
              "PATTERN.BAS did not select SCREEN 7")
        check(mode == 1 and logical_w == 320 and logical_h == 200,
              "SCREEN 7 did not configure its logical surface")
        m.run(); os88marty.settle(m)
        gseg = word(m, base, SB.sym("_sbs_gseg"))
        check(gseg != 0, "SCREEN 7 has no retained presentation surface")
        if gseg:
            def retained_pixel(x, y):
                packed = m.read((gseg << 4) + y * 160 + x // 2, 1)[0]
                return packed >> 4 if not (x & 1) else packed & 15
            psets = tuple(retained_pixel(x, 30)
                          for x in (40, 80, 120, 160, 200, 240, 280))
            print("  retained PSET row: %r" % (psets,))
            check(psets == (9, 9, 9, 9, 9, 9, 9),
                  "PATTERN's seven PSET pixels are not all retained")
            line_a = retained_pixel(20, 60)
            line_b = retained_pixel(160, 150)
            print("  retained pixels: pset=%d line11=%d line10=%d" %
                  (retained_pixel(40, 30), line_a, line_b))
            check(line_a == 11 and line_b == 10,
                  "PATTERN's colored LINE pixels are absent")
        m.run(); m.ctrl("KeyF")
        check(wait_byte(m, base + SB.sym("_sbu_full"), 1),
              "graphical output did not enter fullscreen")
        m.run(); os88marty.settle(m)
        capture(m, capture_dir, "speedybasic-pattern-fullscreen.png")
        m.run(); m.ctrl("KeyF")
        check(wait_byte(m, base + SB.sym("_sbu_full"), 0),
              "graphical output did not leave fullscreen")
        m.run(); os88marty.settle(m)
        wx, wy, ww, wh = dispcp.win_rect(m, S, app_slot)
        _, graphic_colors = crop_digest(
            m, (wx + 8, wy + os88geom.TITLE_H + 16,
                max(1, ww - 16), max(1, wh - os88geom.TITLE_H - 30)))
        check(graphic_colors > 1,
              "PATTERN rendered as a flat black output region")
        capture(m, capture_dir, "speedybasic-pattern-windowed.png")

        # LANDERHD combines SCREEN 9's four 28K retained planes with several
        # numeric arrays.  Reaching the mode after its DIM/READ setup proves
        # the lazy graphics and runtime claims coexist on a 640K machine.
        app_win = next(w for w in os88geom.windows(m, S) if w.i == app_slot)
        open_app_file(m, mouse, S, app_win,
                      "SPEEDY/DEMOS/LANDERHD.BAS")
        m.run(); m.key("F5")
        check(wait_word(m, base + SB.sym("_sbs_screen"), 9, frames=1800),
              "LANDERHD did not reach SCREEN 9")
        screen9 = word(m, base, SB.sym("_sbs_screen")) == 9
        print("  LANDERHD screen=%d status=%d" %
              (word(m, base, SB.sym("_sbs_screen")),
               byte(m, base, SB.sym("_sbc_status"))))
        if not screen9:
            print("  LANDERHD line=%d pc=%d graphics=%04x planes=%r toast=%r" %
                  (word(m, base, SB.sym("_sbc_lineno")),
                   word(m, base, SB.sym("_sbc_pc")),
                   word(m, base, SB.sym("_sbs_gseg")),
                   tuple(word(m, base, SB.sym("_sbs_pseg") + i * 2)
                         for i in range(4)),
                   bytes(m.read(S("toast_buf"), 32)).split(b"\0")[0]))
        if byte(m, base, SB.sym("_sbc_status")) == 5:
            err = bytes(m.read(base + SB.sym("_sbc_err"), 48)).split(b"\0")[0]
            print("  LANDERHD error line %d: %r pc=%d/%d" %
                  (word(m, base, SB.sym("_sbc_lineno")), err,
                   word(m, base, SB.sym("_sbc_pc")),
                   word(m, base, SB.sym("_sbc_source_len"))))
        if screen9:
            planes = tuple(word(m, base, SB.sym("_sbs_pseg") + i * 2)
                           for i in range(4))
            check(all(planes), "SCREEN 9 did not retain all four EGA planes")
            check(word(m, base, SB.sym("sbm_lo_seg")) != 0 and
                  word(m, base, SB.sym("sbm_used")) != 0,
                  "LANDERHD's numeric array store was not allocated")
            toast = bytes(m.read(S("toast_buf"), 32)).split(b"\0")[0]
            check(b"memory unavailable" not in toast.lower(),
                  "LANDERHD reported unavailable graphics memory")

        # STARS3D proves its array setup can coexist with SCREEN 13 natively.
        # The bounded host gate runs through its OUT and A000 POKE callbacks.
        app_win = next(w for w in os88geom.windows(m, S) if w.i == app_slot)
        leave_output(m, mouse, S, SB, base, app_win)
        open_app_file(m, mouse, S, app_win,
                      "SPEEDY/DEMOS/STARS3D.BAS")
        m.run(); m.key("F5")
        check(wait_word(m, base + SB.sym("_sbs_screen"), 13, frames=1200),
              "STARS3D did not reach SCREEN 13")
        m.advance(frames=120)
        stars_status = byte(m, base, SB.sym("_sbc_status"))
        stars_hi = word(m, base, SB.sym("sbm_hi_seg"))
        stars_pokes = word(m, base, SB.sym("_sbs_vpoke_count"))
        print("  STARS3D screen=%d status=%d high=%04x pokes=%d line=%d pc=%d" %
              (word(m, base, SB.sym("_sbs_screen")), stars_status, stars_hi,
               stars_pokes,
               word(m, base, SB.sym("_sbc_lineno")),
               word(m, base, SB.sym("_sbc_pc"))))
        check(stars_status != 5, "STARS3D entered runtime error")

        # BOING allocates three 4,001-cell arrays after SCREEN 9.  This catches
        # native capacity across their boundaries; the host gate runs through
        # all three GETs and both PUTs.
        app_win = next(w for w in os88geom.windows(m, S) if w.i == app_slot)
        leave_output(m, mouse, S, SB, base, app_win)
        open_app_file(m, mouse, S, app_win,
                      "SPEEDY/DEMOS/BOING.BAS")
        m.run(); m.key("F5")
        check(wait_word(m, base + SB.sym("_sbs_screen"), 9, frames=1200),
              "BOING did not reach SCREEN 9")
        for _ in range(3600):
            if (word(m, base, SB.sym("sbm_used")) >= 12003 or
                    byte(m, base, SB.sym("_sbc_status")) == 5):
                break
            m.advance(frames=1)
        boing_status = byte(m, base, SB.sym("_sbc_status"))
        print("  BOING screen=%d status=%d used=%d get=%d put=%d" %
              (word(m, base, SB.sym("_sbs_screen")), boing_status,
               word(m, base, SB.sym("sbm_used")),
               word(m, base, SB.sym("_sbs_gget_count")),
               word(m, base, SB.sym("_sbs_gput_count"))))
        if boing_status == 5:
            err = bytes(m.read(base + SB.sym("_sbc_err"), 48)).split(b"\0")[0]
            print("  BOING error line %d: %r pc=%d/%d" %
                  (word(m, base, SB.sym("_sbc_lineno")), err,
                   word(m, base, SB.sym("_sbc_pc")),
                   word(m, base, SB.sym("_sbc_source_len"))))
        check(boing_status != 5, "BOING entered runtime error")
        check(word(m, base, SB.sym("sbm_used")) >= 12003,
              "BOING did not allocate its three 4,001-cell arrays")

        # The ordinary close box must remove the window and release the
        # package rather than leave an invisible scheduled instance.
        app_win = next(w for w in os88geom.windows(m, S) if w.i == app_slot)
        leave_output(m, mouse, S, SB, base, app_win)
        m.run()
        wx, wy = dispcp.win_rect(m, S, app_slot)[:2]
        mouse.click(*os88geom.close_xy(wx, wy))
        m.advance(frames=8)
        check(app_slot not in dispcp.win_list(m, S),
              "the Speedy Basic window survived its close box")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-dir",
                        help="write editor, welcome and graphics PNGs here")
    parser.add_argument("--machine", default=MACHINE,
                        help="MartyPC machine configuration")
    args = parser.parse_args()
    run(args.capture_dir, args.machine)
    if FAIL:
        for failure in FAIL:
            print("speedybasic: FAIL: " + failure)
        return 1
    print("speedybasic: launch, run, fullscreen, repaint and close - PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
