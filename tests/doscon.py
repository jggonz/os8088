#!/usr/bin/env python3
"""THE DOS BOX'S CONSOLE, AND THE PROMPT IN IT (SPEC.md 96.33).

The band below the top bar carried three lines of status text until this wave
and now carries an 80x25 screen. What that is worth is a list of things a
person can DO, so the row drives them in the order a person would - and every
one of them is a separate thing that can be missing:

  1  a box launched with NO document opens on a PROMPT, not on an error, and
     the prompt names the drive it was launched from (96.33.2). It read `A:\\>`
     on a machine standing on C: until OSAPI_FILE_HERE was asked.
  2  typing ECHOES, and costs ONE ROW of redraw and not twenty-five - the
     dirty-row bitmap is the whole reason the console is not a glyph call per
     cell (70.8.1), and a renderer that lost it would look identical.
  3  a BUILT-IN runs and its output lands in the band: VER is dosh.inc's and
     was reachable only from `AH=4Bh` before there was a prompt.
  4  DIR - the one verb the table was missing (96.33.4) - lists the folder
     with `<DIR>` for a folder and a size for a file.
  5  CD moves, and THE PROMPT FOLLOWS IT. `$P$G` is recomposed rather than
     held, so this is the assertion that says so.
  6  a file that is NOT a program is refused by its EXTENSION (96.33.15) -
     `SOUND.DRV`, at the volume root. It was `DOS.O88` until 96.33.17 made a
     `.O88` at the prompt OPEN THE PACKAGE, which is a window rather than a
     refusal; tests/dospkg.py owns that. This is the wedge's
     guard: without the rule the box runs 30KB of package image as a `.COM`
     and the machine never comes back, so the row fails by HANGING.
  7  ...and a name that is neither says `Bad command or file name`, which is
     DOS's sentence and not "It could not be read." about a file the user
     never had.
  8  FULL SCREEN puts the same buffer on real text VRAM and ESC comes back
     (96.33.5). The assertion is VRAM's OWN BYTES at the segment the bracket
     was handed, CELL FOR CELL over all 2,000 - because that is the whole
     claim the design makes: con_scr's cell IS the cell in VRAM, so the
     renderer is a move and not a translation (70.8.7), and a screenshot
     cannot tell those apart.
  8b ...and a program TYPED AT THE FULL SCREEN runs and gives it back
     (96.33.16), asserted as the three-state sequence [dos_fsxup]/[dos_inbr]
     goes 1/0 -> 0/1 -> 1/0. It never ran at all before: the wake a launch
     posts cannot be dispatched while the UI task is inside the bracket.

...and two more between them, both reported from the console and both the same
shape - a thing COMMAND.COM answers BEFORE its table and this box did not.
5b: a bare `B:` is a DRIVE CHANGE and not a verb (96.33.6), `Z:` is refused AND
does not move. 5c: a bare name with no extension is a SEARCH, `.COM` then
`.EXE` (96.33.7) - typed `PRINCE`, the box refused a folder holding PRINCE.EXE.

**IT READS THE BUFFER AND NOT THE GLASS**, with one exception. con_scr is
2,000 cells of (character, attribute) and every assertion above is about
CHARACTERS, so reading pixels would be reading a font. The exception is
assertion 2's cost, which is counted in the guest's own marks - and the one
PICTURE check, that the band is actually BLACK, because a console whose
buffer is perfect and whose glyph table is empty draws a black rectangle and
reads green from every other row here. That is not hypothetical: it is what
the first build did, con_open having not called con_font.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dosmap                                                  # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402

SYS = "build/os8088-360.img"
# **B: IS THE doscom DISK AND NOT THE APPS ONE**, for one reason: step 5c needs
# a `.COM` at a root to type the bare name of, and no apps disk has one - every
# package on those is a `.O88`. It costs nothing else: the only other thing B:
# is for here is being a drive to switch to.
APPS = "build/doscom360.img"
BOX = "A:/APPS/DOS.O88"
BARE = "DOSHELLO"                    # ...and DOSHELLO.COM is what it finds


def fail(msg):
    print("doscon: FAIL: %s" % msg)
    sys.exit(1)


class Box(object):
    """The live instance, and the two reads every assertion is made of.

    **THE SEGMENT IS RESOLVED PER ACCESS AND NOT CACHED** (SPEC.md 66.6.1.2).
    It used to be taken once in `__init__`, which was safe for exactly as long
    as the DOS box's region could not move - and since 66.6.1.2 unpinned the
    re-homed carve it moves like any other region, at any compaction, which is
    every time the box claims the arena. A banked base then names the bytes the
    package USED to occupy: `doslnk` read `[dos_path]` and got nine bytes of
    machine code, and `doscon` and `dosdirsw` read a console that had moved out
    from under them. A stale base decodes as plausible rubbish rather than as
    an error, which is `tests/dosarena.py`'s own note one row along.

    The re-read is a window-record lookup in the guest - cheap beside the
    debug round trip every access already costs.
    """

    def __init__(self, m):
        self.m = m
        self.dm = dosmap.package()

    @property
    def seg(self):
        return dosmap.instance(self.m)

    @property
    def base(self):
        return self.seg << 4

    def w(self, name):
        return int.from_bytes(self.m.read(self.base + self.dm[name], 2),
                              "little")

    def b(self, name):
        return self.m.read(self.base + self.dm[name], 1)[0]

    def text(self, name, n):
        raw = self.m.read(self.base + self.dm[name], n)
        return raw.split(b"\0")[0].decode("latin-1")

    def rows(self):
        """The console buffer as 25 rstripped lines of text."""
        scr = self.m.read(self.base + self.dm["con_scr"], 80 * 25 * 2)
        out = []
        for r in range(25):
            row = scr[r * 160:(r + 1) * 160]
            out.append("".join(chr(row[i]) if 32 <= row[i] < 127 else " "
                               for i in range(0, 160, 2)).rstrip())
        return out

    def live(self):
        """...and the ones with anything on them, in order."""
        return [r for r in self.rows() if r.strip()]

    def type(self, s):
        self.m.type_text(s)
        os88marty.settle(self.m)


def band_ink(m, bx):
    """(lit, dark) pixels INSIDE the console band, found by its own shape.

    **NOT AT AN ADDRESS.** The rendered frame is not in guest coordinates -
    MartyPC's Hercules aperture starts sixteen columns in - so a rectangle read
    at [dos_conx] lands partly on the window's WHITE MARGIN either side of the
    band, and 40 pixels of that margin look exactly like 40 pixels of text.
    Measured: with con_font deliberately removed, an address-read band still
    reported 3,951 lit pixels and the check stayed green.

    So the band is found instead: the widest run of DARK pixels on any row of
    it is a blank text row and gives the x extent, and a row counts as the
    band's when that extent is mostly dark. Everything outside is ignored.
    """
    w, h, px = m.fbuf()
    y0 = bx.w("dos_cony")
    rws = bx.w("dos_conrows") * 8
    lo = max(0, y0 - 6)
    hi = min(h, y0 + rws + 6)

    def runs(y):
        row = px[y * w * 3:(y + 1) * w * 3]
        out, st = [], None
        for x in range(w):
            if row[x * 3] <= 128:
                if st is None:
                    st = x
            elif st is not None:
                out.append((st, x - 1))
                st = None
        if st is not None:
            out.append((st, w - 1))
        return out

    # **THE MODAL EXTENT AND NOT THE WIDEST.** The window's own bottom border is
    # one full-width dark row, so "widest" picks 0..719 - and then the band's
    # white MARGINS are inside the rectangle and read as text. Measured: with
    # con_font removed the widest-run version reported 12,855 lit pixels and
    # stayed green. The band is twenty-five rows and the border is one, so the
    # extent that occurs most often is the band's.
    tally = {}
    for y in range(lo, hi):
        r = runs(y)
        if not r:
            continue
        a, b = max(r, key=lambda ab: ab[1] - ab[0])
        if b - a >= 200:
            tally[(a, b)] = tally.get((a, b), 0) + 1
    if not tally:
        fail("no dark band found under the top bar in rows %d..%d, and an "
             "80-column console is 640 pixels of one (SPEC.md 96.33)"
             % (lo, hi))
    x1, x2 = max(tally, key=lambda k: (tally[k], k[1] - k[0]))
    span = x2 - x1 + 1
    lit = dark = 0
    for y in range(lo, hi):
        row = px[y * w * 3:(y + 1) * w * 3]
        d = sum(1 for x in range(x1, x2 + 1) if row[x * 3] <= 128)
        if d * 10 < span * 6:
            continue                        # not one of the band's rows
        lit += span - d
        dark += d
    return lit, dark


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - a plain `make` builds it" % p)

    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if not ui.path(BOX):
            fail("could not launch %s" % BOX)
        bx = Box(m)

        # --- 1: a prompt, and it names the drive we came from ----------------
        rows = bx.live()
        print("doscon: the box opens on:")
        for r in rows[:6]:
            print("   | %s" % r)
        if not rows or not rows[-1].endswith(">"):
            fail("a box launched with no document should open on a PROMPT and "
                 "the last live row is %r" % (rows[-1] if rows else None))
        want = "%s:\\>" % chr(ord("A") + bx.b("dos_vol"))
        if rows[-1] != want:
            fail("the prompt is %r and the box is standing on volume %d, so "
                 "$P$G is %r (SPEC.md 96.33.2)"
                 % (rows[-1], bx.b("dos_vol"), want))

        # --- 2: typing echoes, one row of redraw at a time ------------------
        before = bx.w("con_cx")
        bx.type("VER")
        if bx.w("dos_cmdn") != 3:
            fail("three keys reached the console and it holds %d of them"
                 % bx.w("dos_cmdn"))
        if bx.w("con_cx") != before + 3:
            fail("three glyphs were echoed and the cursor moved %d columns"
                 % (bx.w("con_cx") - before))
        if not bx.live()[-1].endswith(">VER"):
            fail("the typed text is not on the prompt's row: %r"
                 % bx.live()[-1])

        # --- 3: a built-in runs, into the band ------------------------------
        bx.type("\n")
        rows = bx.live()
        if not any("os8088 DOS Version" in r for r in rows[-3:]):
            fail("VER ran and its line is not in the band - the last rows are "
                 "%r" % rows[-3:])

        # --- ...and the BAND IS BLACK, which the buffer cannot say ----------
        # A console with a perfect buffer and an empty glyph table draws a
        # black rectangle and passes every other check in this file. It is
        # what the first build did (con_open did not call con_font), so this
        # counts LIT pixels in the band and wants some.
        # 500, and the gap it sits in is two orders of magnitude: this screen
        # carries ~100 characters at this point and measures ~6,300 lit
        # pixels, and with con_font deliberately removed it measures 47 - the
        # cursor's underline, which con_compose draws from no glyph at all.
        lit, dark = band_ink(m, bx)
        if lit < 500:
            fail("the console band has %d lit pixels in %d dark ones and there "
                 "is text in its buffer - a zeroed [con_glyf] draws a BLACK "
                 "RECTANGLE and every character check above still passes "
                 "(with con_font removed this reads 47, the cursor alone)"
                 % (lit, dark))
        print("doscon: the band is %d lit of %d pixels - black with text on it"
              % (lit, lit + dark))

        # --- 4: DIR, the verb the table was missing -------------------------
        bx.type("DIR\n")
        rows = bx.live()
        body = [r for r in rows if "<DIR>" in r]
        if not body:
            fail("DIR listed no folder and this volume's root has four: %r"
                 % rows[-8:])
        # **THE FOOTER IS DOS 3.30's OWN FORMAT** since SPEC.md 96.33.9, and
        # it was `N file(s)` before: COMMAND.COM's string table has
        # `%9d File(s) %9ld bytes free` at offsets 20095 and 20111, capital F
        # and a byte count, both measured rather than remembered.
        foot = [r for r in rows if "File(s)" in r]
        if not foot:
            fail("DIR printed no `N File(s)` footer: %r" % rows[-4:])
        if "bytes free" not in foot[-1]:
            fail("the footer is %r and DOS's carries the free space too "
                 "(SPEC.md 96.33.9)" % foot[-1])
        print("doscon: DIR lists %d folder(s) and DOS's own footer, %r"
              % (len(body), foot[-1].strip()))

        # --- 5: CD moves, and the prompt follows ----------------------------
        bx.type("CD APPS\n")
        rows = bx.live()
        if rows[-1] != "%s:\\APPS>" % chr(ord("A") + bx.b("dos_vol")):
            fail("CD APPS left the prompt at %r - $P$G is recomposed at every "
                 "prompt so that it cannot go stale (SPEC.md 96.33.2)"
                 % rows[-1])
        print("doscon: ...and the prompt follows CD: %r" % rows[-1])

        # --- 5b: a bare X: changes DRIVE, which is not a verb ---------------
        # DOS answers it before the table, and the box did not answer it at
        # all: `B:` came back `Bad command or file name` (SPEC.md 96.33.6).
        here = bx.b("dos_vol")
        other = "B" if here == 0 else "A"
        bx.type("%s:\n" % other)
        if bx.b("dos_vol") != (1 if here == 0 else 0):
            fail("%s: left the box on volume %d - a bare drive letter is a "
                 "DRIVE CHANGE and not a verb (SPEC.md 96.33.6)"
                 % (other, bx.b("dos_vol")))
        if not bx.live()[-1].startswith("%s:" % other):
            fail("the prompt did not follow the drive change: %r"
                 % bx.live()[-1])
        bx.type("Z:\n")
        rows = bx.live()
        if not any("Invalid drive" in r for r in rows[-3:]):
            fail("a drive that is not there should answer DOS's own "
                 "`Invalid drive specification`, and the band says %r"
                 % rows[-3:])
        if bx.b("dos_vol") != (1 if here == 0 else 0):
            fail("a REFUSED drive change moved the box anyway, to volume %d"
                 % bx.b("dos_vol"))
        print("doscon: ...and %s: changes drive while Z: is refused without "
              "moving" % other)
        # --- 5c: a BARE NAME is a search, .COM then .EXE (96.33.7) ---------
        # Typed `PRINCE`, the box answered `Bad command or file name` for a
        # folder holding PRINCE.EXE: it took the word as a whole file name,
        # where COMMAND.COM treats a name with no extension as a SEARCH.
        bx.type("%s\n" % BARE)
        # **AND THE BOX IS FULLY QUALIFIED** since SPEC.md 96.33.10: it held
        # the name as typed, which is only true while the box is still
        # standing where it was typed - one CD and it names a different file.
        got = bx.text("dos_path", 32).upper()
        if not got.endswith(BARE + ".COM"):
            fail("typing %r should find %s.COM in this folder and the path box "
                 "holds %r - a name with no extension is a SEARCH, .COM then "
                 ".EXE (SPEC.md 96.33.7)" % (BARE, BARE, got))
        if got[1:3] != ":\\":
            fail("the path box holds %r and should be FULLY QUALIFIED - drive, "
                 "folder and name (SPEC.md 96.33.10)" % got)
        rows = bx.live()
        if any("Bad command" in r for r in rows[-3:]):
            fail("%r was refused as a bad command and %s.COM is right there: "
                 "%r" % (BARE, BARE, rows[-3:]))
        print("doscon: ...and a bare %r finds %s" % (BARE, got))

        # --- 5d: ...AND IT RAN, so its LAST SCREEN is the band (96.34) ------
        # This step used to type the next command straight after the launch,
        # which is a RACE with a program that waits for a key: the keystrokes
        # went to DOSHELLO, not to the console, and everything below asserted
        # against whichever side won. So the program is driven to its end here
        # - and what it leaves behind is the wave's own assertion.
        #
        # `dos_snap` copies the program's text screen into con_scr at teardown
        # (SPEC.md 96.34). With it removed the band still holds the prompt's
        # own history and not one of these lines, which is what makes this a
        # test rather than a description.
        for _ in range(120):
            if any("READY" in r for r in (m.screen() or [])):
                break
            time.sleep(0.25)
        else:
            fail("%s.COM never reached its READY prompt inside the bracket"
                 % BARE)
        m.type_text("x")                     # ...which is how it exits, 042
        for _ in range(120):
            time.sleep(0.4)
            if not bx.b("dos_inbr"):
                break
        else:
            fail("the bracket never came down after %s.COM took a key" % BARE)
        os88marty.settle(m)
        rows = bx.live()
        for want in ("os8088 DOS gate", "Memory to top of block",
                     "READY - press a key"):
            if not any(want in r for r in rows):
                fail("the program printed %r and the console does not hold it "
                     "after the bracket came down - dos_snap is what copies "
                     "its last screen in (SPEC.md 96.34). The band says %r"
                     % (want, rows[-8:]))
        if not any("exit code 042" in r for r in rows):
            fail("the box's own exit line is not in the band under the "
                 "captured screen: %r" % rows[-4:])
        # ...and the CURSOR came across, so the box's line is BELOW the
        # program's output rather than on top of its first row - which is
        # exactly what a clobbered DX looked like (96.34.3).
        prog = [i for i, r in enumerate(bx.rows()) if "os8088 DOS gate" in r]
        ended = [i for i, r in enumerate(bx.rows()) if "exit code 042" in r]
        if prog and ended and ended[0] <= prog[0]:
            fail("the exit line is at row %d and the program's first row at "
                 "%d - the cursor did not come across (SPEC.md 96.34.3)"
                 % (ended[0], prog[0]))
        print("doscon: ...and its last screen is in the band, %d rows of it, "
              "with the box's exit line under it" % len(rows))

        bx.type("%s:\n" % chr(ord("A") + here))

        # --- 7: a name that is neither a verb nor a file --------------------
        bx.type("NOSUCH\n")
        rows = bx.live()
        if not any("Bad command" in r for r in rows[-3:]):
            fail("a name that is neither a built-in nor a program should "
                 "answer DOS's own `Bad command or file name`, and the band "
                 "says %r (SPEC.md 96.33.3)" % rows[-3:])
        print("doscon: ...and an unknown name is a bad command, not a read "
              "error")

        # --- 6: ...AND A FILE THAT IS NOT A PROGRAM IS REFUSED BY EXTENSION -
        # An extension that is not .COM or .EXE is not a DOS program, and
        # COMMAND.COM answers `Bad command or file name` for it WITHOUT
        # OPENING IT (SPEC.md 96.33.15).
        #
        # **THIS STEP IS THE WEDGE'S GUARD AND IT FAILS BY HANGING.** Before
        # 96.33.15 the box ran what it was given - a package image's OP_ header
        # and org-0 code entered at PSP:0100 - and the machine did not come
        # back: the bracket up, the gfx lock held, no pointer and no menu bar.
        # So [dos_inbr] is asserted as well as the sentence.
        #
        # **IT USED TO TYPE `DOS.O88` AND IT CANNOT ANY MORE** (SPEC.md
        # 96.33.17): a `.O88` at the prompt OPENS THE PACKAGE now, which is a
        # window appearing rather than a refusal - and typing the box's own
        # name here would put a second DOS window in front of every step
        # below. tests/dospkg.py owns that behaviour and asserts it four ways.
        # What is left for this step is the rule itself, and `SOUND.DRV` at
        # the volume root is the sharper subject anyway: a REAL FILE that
        # really is not a program, where DOS.O88 was a real file that has
        # since become one this box can start.
        #
        # The path box is NOT rewritten, which is the other half of the rule:
        # nothing was resolved, so there is nothing to qualify. 5c above is
        # what asserts a real program's name landing there fully qualified.
        was = bx.text("dos_path", 32)
        bx.type("CD \\\n")
        bx.type("SOUND.DRV\n")
        rows = bx.live()
        if not any("Bad command" in r and "SOUND.DRV" in r for r in rows[-3:]):
            fail("a file whose extension is not .COM or .EXE is not a program "
                 "and DOS answers `Bad command or file name` without opening "
                 "it (SPEC.md 96.33.15); the band says %r" % rows[-3:])
        if bx.b("dos_inbr"):
            fail("the box ENTERED a bracket for SOUND.DRV - a driver image "
                 "run as a .COM, which is a machine that does not come back "
                 "(SPEC.md 96.33.15)")
        if bx.text("dos_path", 32) != was:
            fail("a refused name rewrote the path box to %r, and nothing was "
                 "resolved to put in it (SPEC.md 96.33.15)"
                 % bx.text("dos_path", 32))
        bx.type("CD APPS\n")
        print("doscon: ...and SOUND.DRV is refused by its extension, with the "
              "box and the machine untouched")

        # --- 8: FULL SCREEN, and Esc back out of it (SPEC.md 96.33.5) -------
        # The assertion is TEXT VRAM's own bytes, read out of the guest at the
        # segment the bracket was handed - B000 on this Hercules, B800 on the
        # rest - because that is the whole claim the design makes: con_scr's
        # cell IS the cell in VRAM, so the renderer is a move and not a
        # translation (70.8.7). A screenshot could not tell that from a
        # translation that happened to work.
        ui.menu_pick("Program", "Full Screen")
        time.sleep(2.0)
        if not bx.b("dos_fsxup"):
            fail("Program > Full Screen did not take the screen: [dos_fsxup] "
                 "is 0 (SPEC.md 96.33.5)")
        seg = bx.w("con_tseg")
        if seg not in (0xB000, 0xB800):
            fail("the bracket's framebuffer segment is %04X, and FSXM_TEXT80 "
                 "is B000 on Hercules and B800 on the rest" % seg)
        # **CELL FOR CELL, all 2,000 of them** - which is a far stronger claim
        # than "every line is present somewhere", and the one 70.8.7 actually
        # makes. The ATTRIBUTE byte is deliberately not compared: on a mono
        # adapter con_tx_mattr maps it (70.8.9) and on a colour one it does
        # not, so the CHARACTER is the half that is a pure move on every
        # adapter.
        buf = m.read(bx.base + bx.dm["con_scr"], 80 * 25 * 2)
        vram = m.read(seg << 4, 80 * 25 * 2)
        hint = " Esc to leave"
        pairs = []
        for r in range(25):
            pairs.append(("".join(chr(buf[r * 160 + c * 2]) for c in range(80)),
                          "".join(chr(vram[r * 160 + c * 2]) for c in range(80))))
        for r, (b, v) in enumerate(pairs):
            if r == 24:
                b, v = b[:80 - len(hint)], v[:80 - len(hint)]   # the hint has
                                                               # the row's TAIL
                                                               # and may (70.8.7)
            if b != v:
                col = next(i for i in range(len(b)) if b[i] != v[i])
                fail("the full screen is not the buffer: row %d column %d is "
                     "%r in con_scr and %r in text VRAM at %04X. The cell IS "
                     "the cell (SPEC.md 70.8.7), so this is a MOVE and cannot "
                     "differ" % (r, col, b[col], v[col], seg))
        got = [v for _, v in pairs if v.strip()]
        if hint not in pairs[24][1]:
            fail("the bottom row does not name the key that leaves: %r"
                 % pairs[24][1])
        print("doscon: full screen is the buffer cell for cell over %d live "
              "rows, and row 24 says how to get out" % len(got))

        # --- 8b: ...AND A PROGRAM TYPED IN IT RUNS, AND GIVES IT BACK -------
        # Reported from the field on CGA: launching a program from the full
        # screen "switched the gfx mode into weird flashing coloured glyphs
        # and never showed prince". Two halves of one defect (SPEC.md
        # 96.33.16), and this asserts both:
        #
        #   the program NEVER RAN, because dos_con_prog posts a wake and the
        #   UI task cannot dispatch one while it is inside dos_fsx_con's own
        #   poll loop. Measured before the fix: [dos_state] = DST_READY and
        #   [dos_inbr] = 0, unchanged twelve seconds after Enter;
        #
        #   ...and the GLYPHS, which are the repaint that followed it - a
        #   kernel drawing slot laying 1bpp or 4bpp pixel rows into a
        #   framebuffer that is character/attribute pairs now (SPEC.md 53.1).
        #
        # The assertion is the three-state sequence and not a screenshot,
        # because a screenshot of a text screen cannot say which renderer
        # wrote it: fullscreen -> the program's OWN bracket -> fullscreen
        # again, which is [dos_fsxup] 1, 0, 1 with [dos_inbr] 0, 1, 0.
        m.type_text("%s:\n" % other)                   # DOSHELLO.COM is there
        time.sleep(1.0)
        m.type_text("%s\n" % BARE)
        for _ in range(40):
            time.sleep(0.5)
            if bx.b("dos_inbr"):
                break
        else:
            fail("%s never started from the FULL SCREEN console: state=%d, "
                 "[dos_inbr]=0. The wake dos_con_prog posts cannot be "
                 "dispatched while the UI task is inside the bracket, so the "
                 "bracket has to come down first (SPEC.md 96.33.16)"
                 % (BARE, bx.b("dos_state")))
        if bx.b("dos_fsxup"):
            fail("the console's bracket is STILL up with the program's own "
                 "bracket inside it - they are two brackets and the first "
                 "ends before the second starts (SPEC.md 96.33.16)")
        for _ in range(120):
            if any("READY" in r for r in (m.screen() or [])):
                break
            time.sleep(0.25)
        else:
            fail("%s.COM never reached READY inside its own bracket, launched "
                 "from the full screen" % BARE)
        m.type_text("x")
        # **ONE LOOP FOR BOTH, and the exit line is POLLED rather than read
        # once.** The flags flip when the bracket is back up; the box's exit
        # line is written after that, so a single read the instant they flip
        # is a race - and it is a race that only loses UNDER LOAD, which is
        # the worst shape a gate can have: docs/plans/SOAK-PARALLEL.md §1
        # measures a contended guest doing up to 37% less work per host
        # second, so this failed at --marty-jobs 3 and passed alone, looking
        # exactly like the feature being broken. The two diagnoses stay
        # separate because they are different defects.
        back = seen = False
        for _ in range(40):
            time.sleep(0.5)
            if not back and not bx.b("dos_inbr") and bx.b("dos_fsxup"):
                back = True
            if back and any("exit code 042" in r for r in (m.screen() or [])):
                seen = True
                break
        if not back:
            fail("the full-screen console did not come back after %s.COM: "
                 "[dos_fsxup]=%d [dos_inbr]=%d. dos_run's exit re-enters it "
                 "(SPEC.md 96.33.16)"
                 % (BARE, bx.b("dos_fsxup"), bx.b("dos_inbr")))
        rows = [r for r in (m.screen() or []) if r.strip()]
        if not seen:
            fail("the box's exit line is not on the full screen it came back "
                 "to: %r" % rows[-4:])
        print("doscon: ...and a program typed at the full screen runs in its "
              "OWN bracket and hands the screen back")

        m.key("Escape")
        time.sleep(2.0)
        if bx.b("dos_fsxup"):
            fail("Esc did not leave the full screen: [dos_fsxup] is still set. "
                 "It is OURS only while the console has the screen - a running "
                 "program's Esc is the program's (SPEC.md 96.33.5)")
        rows = bx.live()
        if not rows[-1].startswith("%s:" % other) or not rows[-1].endswith(">"):
            fail("the window came back showing %r, and the console was left "
                 "standing at a %s: prompt" % (rows[-1], other))
        print("doscon: ...and Esc comes back to the window on the prompt the "
              "console was left at, %r" % rows[-1])

    print("doscon: ok")


if __name__ == "__main__":
    main()
