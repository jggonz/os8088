#!/usr/bin/env python3
"""A `.O88` TYPED AT THE DOS PROMPT OPENS THE PACKAGE (SPEC.md 96.33.17).

    make && python3 tests/dospkg.py

§96.33.15 refuses an extension that is not `.COM` or `.EXE`, and `.O88` was
refused with the rest - rightly, because entering 30KB of package image as a
`.COM` wedges the machine.  What was missing was not a fourth extension to
allow but something else to do with one, and `OSAPI_PKG_START` (§21.5) is it:
the box hands the name to the kernel and the package opens in its own window,
*not inside the box*, which is the distinction that matters.

FOUR STEPS, and the third and fourth are the ones that break silently:

  1  `CALC.O88` at the prompt opens a window titled `Calculator`, and the DOS
     box is STILL THERE - a launch that replaced the box would be the `.COM`
     path back again.
  1b THE BARE NAME REACHES IT TOO (SPEC.md 96.33.7).  `PIANO` with no
     extension opens `Piano`, because a bare name is a SEARCH and `.O88` is
     the third thing it tries - after DOS's own `.COM` and `.EXE`, which is
     the order contract.  This shipped as a split: the DOTTED door answered a
     package and the bare one did not, so `CALC.O88` opened Calculator and
     `CALC` beside it in the same folder said `Bad command or file name`.
     ITS NEGATIVE CONTROL IS THE SHARP HALF - a bare `NOSUCHPG` that matches
     none of the three must still say `Bad command or file name` and NOT
     `Cannot open`: the `[dos_ispkg]` store sits on the arm where the probe
     HIT, and a store made before the probe would send every unresolved word
     on the machine to the package launcher.
  2  `NOSUCH.O88` says `Cannot open NOSUCH.O88` and opens nothing.  The
     refusal has to name the file: at that point the user has no window to
     look at and AL is the only thing that knows why.
  3  IT IS POSTED, NOT CALLED.  The slot wants the gfx lock FREE and the
     console runs under W_ONKEY, which holds it - so the launch goes through
     `dos_wake`.  Step 3 is what proves the post survives: the box is asked
     for a SECOND package and gets it, which a one-shot flag or a name left
     in the shell's scratch would not deliver.
  4  FROM THE FULL SCREEN the console's bracket comes down first (§96.33.16's
     rule reaching a second kind of launch).  A package's window cannot appear
     over a framebuffer that is character cells, and the wake cannot be
     dispatched while the UI task is inside the bracket either - so this is
     the step that would leave a machine showing 80x25 with a window nobody
     can see.

WHY IT IS NOT ASSERTED ON PIXELS: a window's TITLE is what the kernel's own
window table holds, so the reads are of guest state and not of the glass -
`wm_wins` for the titles, the box's own `con_scr` for the console text.
"""
import os
import struct
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import dosmap                                                  # noqa: E402
import os88geom                                                # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/apps360.img"
BOX = "A:/APPS/DOS.O88"
fails = []


def fail(msg):
    print("dospkg: FAIL: %s" % msg)
    fails.append(msg)


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds it" % p)
    if fails:
        return 1

    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if not ui.path(BOX):
            fail("could not launch the DOS box")
            return 1
        dm = dosmap.package()

        def titles():
            """Every live window's title, out of the kernel's own table."""
            out = []
            for sl in range(8):
                wp = os88geom.winptr(m, sl, m.sym)
                seg = struct.unpack("<H", m.read(wp + os88geom.W_SEG, 2))[0]
                toff = struct.unpack("<H", m.read(wp + os88geom.W_TITLE, 2))[0]
                if seg and toff:
                    out.append(m.read(seg * 16 + toff, 16).split(b"\0")[0]
                               .decode("latin-1"))
            return out

        def boxseg():
            for sl in range(8):
                wp = os88geom.winptr(m, sl, m.sym)
                ws = struct.unpack("<H", m.read(wp + os88geom.W_SEG, 2))[0]
                t = struct.unpack("<H", m.read(wp + os88geom.W_TITLE, 2))[0]
                if ws and t and m.read(ws * 16 + t, 4).startswith(b"DOS\0"):
                    return ws
            return 0

        def console():
            seg = boxseg()
            if not seg:
                return []
            scr = m.read((seg << 4) + dm["con_scr"], 80 * 25 * 2)
            out = []
            for r in range(25):
                row = scr[r * 160:(r + 1) * 160]
                out.append("".join(chr(row[i]) if 32 <= row[i] < 127 else " "
                                   for i in range(0, 160, 2)).rstrip())
            return [r for r in out if r.strip()]

        def to_box():
            """The last launch took the focus; the console only takes keys
            while the box is the active window."""
            w = ui.window("DOS")
            if w:
                ui.raise_window(w)

        def typ(s):
            m.type_text(s)
            os88marty.settle(m)

        def launch(line, want, limit=16.0):
            """Type it, then wait for `want` to appear among the titles."""
            to_box()
            typ(line + "\n")
            end = time.time() + limit
            while time.time() < end:
                if want in titles():
                    return True
                if any("Cannot open" in r for r in console()[-3:]):
                    return False
                time.sleep(1.0)
                os88marty.settle(m)
            return want in titles()

        to_box()
        typ("B:\n")
        typ("CD APPS\n")
        if not console()[-1].endswith("APPS>"):
            fail("the prompt is %r and not B:\\APPS> - every launch below is "
                 "typed standing in the folder the packages are in"
                 % console()[-1])
            return 1

        # --- 1: it opens, and the box survives -------------------------------
        if not launch("CALC.O88", "Calculator"):
            fail("CALC.O88 at the prompt opened no window titled "
                 "'Calculator'. The console hands a .O88 to OSAPI_PKG_START "
                 "(SPEC.md 96.33.17, 21.6); the last console lines were %r"
                 % console()[-3:])
        else:
            print("dospkg: CALC.O88 -> %r" % titles())
        if "DOS" not in titles():
            fail("the DOS box is gone after the launch - a package opens "
                 "BESIDE the box, not inside it (SPEC.md 96.33.17)")

        # --- 1b: ...and the BARE name finds it (SPEC.md 96.33.7) -------------
        # A bare name is a search and `.O88` is its third probe. A DIFFERENT
        # package from step 1 on purpose: re-typing `CALC` would open a second
        # Calculator and `want in titles()` was already true, so the assertion
        # would pass without the search ever running.
        if not launch("PIANO", "Piano"):
            fail("a BARE `PIANO` opened no window titled 'Piano'. The .COM/"
                 ".EXE search takes .O88 third (SPEC.md 96.33.7) - this is "
                 "the dotted door's third answer reaching the bare one. "
                 "Console: %r" % console()[-3:])
        else:
            print("dospkg: PIANO (bare) -> %r" % titles())

        # ...and the negative control, which is the half that breaks silently:
        # the [dos_ispkg] store is on the arm where the probe HIT. Made one
        # instruction earlier it would be unconditional, and every unresolved
        # word typed on this machine would go to the package launcher and come
        # back `Cannot open` instead of `Bad command or file name`.
        to_box()
        before = len(titles())
        typ("NOSUCHPG\n")
        os88marty.settle(m)
        end = time.time() + 12.0
        while time.time() < end and not any(("Bad command" in r or
                                             "Cannot open" in r)
                                            for r in console()[-3:]):
            time.sleep(1.0)
            os88marty.settle(m)
        tail = console()[-3:]
        if any("Cannot open" in r for r in tail):
            fail("a bare name matching NO extension answered %r - it reached "
                 "the PACKAGE launcher, so [dos_ispkg] is being set before "
                 "the .O88 probe answers rather than on its hit arm "
                 "(SPEC.md 96.33.7)" % tail)
        elif not any("Bad command" in r for r in tail):
            fail("a bare `NOSUCHPG` said %r, want 'Bad command or file name' "
                 "(SPEC.md 96.33.7)" % tail)
        else:
            print("dospkg: NOSUCHPG (bare, no match) -> %r"
                  % next(r for r in tail if "Bad command" in r))
        if len(titles()) != before:
            fail("a bare name that matched nothing changed the window list: "
                 "%r" % titles())

        # --- 2: a name that is not there -------------------------------------
        to_box()
        before = len(titles())
        typ("NOSUCH.O88\n")
        os88marty.settle(m)
        end = time.time() + 12.0
        while time.time() < end and not any("Cannot open" in r
                                            for r in console()[-3:]):
            time.sleep(1.0)
            os88marty.settle(m)
        tail = console()[-3:]
        if not any("Cannot open NOSUCH.O88" in r for r in tail):
            fail("a .O88 that is not there said %r, want 'Cannot open "
                 "NOSUCH.O88' - the refusal has to name the file, because "
                 "there is no window to look at (SPEC.md 96.33.17)" % tail)
        else:
            print("dospkg: NOSUCH.O88 -> %r"
                  % next(r for r in tail if "Cannot open" in r))
        if len(titles()) != before:
            fail("a refused .O88 changed the window list: %r" % titles())

        # --- 3: the post is not a one-shot -----------------------------------
        if not launch("NOTEPAD.O88", "Note Pad"):
            fail("a SECOND package would not launch from the prompt - the "
                 "post, the banked name or the flag survives one use only "
                 "(SPEC.md 96.33.17). Console: %r" % console()[-3:])
        else:
            print("dospkg: NOTEPAD.O88 -> %r" % titles())

        # --- 4: ...and from the FULL SCREEN ----------------------------------
        # 96.33.16's rule reaching a second kind of launch. The bracket has to
        # come down before the window can appear AND before the wake can even
        # be dispatched, so a machine that got this wrong shows 80x25 text with
        # a package running behind it.
        to_box()
        ui.menu_pick("Program", "Full Screen")
        os88marty.settle(m)
        if not m.read((boxseg() << 4) + dm["dos_fsxup"], 1)[0]:
            fail("Program > Full Screen did not raise [dos_fsxup], so step 4 "
                 "is not testing the full screen at all")
        else:
            typ("PAINT.O88\n")
            end = time.time() + 25.0
            while time.time() < end:
                if not m.read((boxseg() << 4) + dm["dos_fsxup"], 1)[0]:
                    break
                time.sleep(1.0)
                os88marty.settle(m)
            up = m.read((boxseg() << 4) + dm["dos_fsxup"], 1)[0]
            if up:
                fail("the console's bracket is STILL up after a .O88 was "
                     "typed into the full screen - [dos_fsxgo] is what brings "
                     "it down, and without it the wake is never dispatched "
                     "(SPEC.md 96.33.16, 96.33.17)")
            end = time.time() + 20.0
            while time.time() < end and "Paint" not in titles():
                time.sleep(1.0)
                os88marty.settle(m)
            if "Paint" not in titles():
                fail("the bracket came down and no window titled 'Paint' "
                     "opened: %r" % titles())
            else:
                print("dospkg: full screen -> PAINT.O88 -> %r" % titles())

    print("dospkg: %s" % ("ok - a package opens beside the box, by name, "
                          "from the window and from the full screen"
                          if not fails else "%d failed" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
