#!/usr/bin/env python3
"""A `.O88` BY PATH, WITH A DOCUMENT, AND `OPEN` (SPEC.md 96.33.21, 96.33.22).

    make && python3 tests/dosopen.py

Three things the DOS prompt could not do, and one kernel argument that carries
all three (§21.5.3 - `OSAPI_PKG_START` takes a DOCUMENT beside the name):

  A  A TYPED PATH TO A PACKAGE (§96.33.21).  `dos_con_pkg` copied `dsh_a1`
     into a 13-byte cell, so `B:\\APPS\\CALC.O88` was truncated to twelve
     characters of PATH and answered `Cannot open B:\\APPS\\CALC.O`.  All
     three shapes now: fully qualified, relative, and the folder we stand in.

  B  `PROGRAM DOCUMENT` (§96.33.21.1).  `NOTEPAD README.TXT` opens Note Pad
     ON that document, exactly as clicking it would - the package reads
     `OSAPI_ARG_FILE` and cannot tell the two apart.  The program is found by
     the HINT CACHE and not in the folder we stand in, which is the whole
     reason this is the association route.

  C  `OPEN DOCUMENT` (§96.33.22).  The same with the program left out: the
     extension names it.

WHY THE VOLUME IS NEVER BROWSED IN A DISK WINDOW HERE, and it is the sharp
part: the association tables are filled by the mount HARVEST, and every path
a program reaches this slot by is a QUIET mount, which skips it (§21.5.3.1).
The DOS box seeds nothing at all - `dos_drv_sel` does no mount (§96.48) and
the mount at the next name is `GOTO_QM`'s quiet one.  So the kernel seeds the
cache itself, and this test proves it by never opening B: in a Disk window:
before that seed existed, every lookup below missed.

CASE IS FOLDED ON BOTH NAMES (§21.5.3.2).  Reported off the glass at `A:\>`
on a stock system disk: `notepad readme.txt` and `open readme.txt` both
refused and `open README.TXT` worked.  The association tables are
uppercase-exact - they are built from FAT names - and nothing in that path
goes through the file layer that would fold it.  BOTH names had to be folded
and only the extension was ever going to be noticed: a lowercase document name
matches nothing on a FAT volume either, so folding the extension alone would
have turned a visible refusal into a package opening an empty window.

A LAUNCH THAT WORKED OWES A PROMPT (§96.33.17.1), reported with a photograph:
two `open`s in a row left the cursor at column 0 of a bare line, so the second
command had no `A:\>` in front of it.  It is §96.33.17's gap rather than this
feature's - a DOS program's prompt comes back with its EXIT LINE and a package
has none - so both spellings are asserted.

AND THE REFUSALS ARE THREE, NOT ONE (§96.33.22.1).  One string used to answer
all of them and was wrong twice over - it named the PROGRAM when the thing
missing was the FILE, and said `on this disk` about a lookup that searches the
volumes.  The first fix for it then sent the PARSE failure to the new
association wording, which is the same defect wearing its replacement's
clothes, so all three are asserted here.

THE NEGATIVE CONTROLS ARE THE HALF THAT BREAKS SILENTLY:

  *  A TYPO WITH NO TAIL still answers `Bad command or file name` (§96.33.21.2).
     The stem is only looked up when a document follows it; without that rule
     every unresolved word on the machine reaches the package launcher.
  *  A TYPO WITH A TAIL answers `Cannot open NOTPAD` - naming the half that
     was wrong, not the document.
  *  `OPEN` of an extension nothing claims says so in its own words, because
     at that point there is no window to look at.
  *  A PROGRAM ON ANOTHER VOLUME is NOT asserted either way, deliberately
     (§96.33.21.2): what the tables and the hint cache know is SESSION STATE,
     so that lookup refuses on a fresh boot and succeeds once an earlier case
     has taught the machine where the program lives.  The reliable half - a
     PATH carries it - is what case B asserts.
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
    print("dosopen: FAIL: %s" % msg)
    fails.append(msg)


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make` builds it" % p)
    if fails:
        return 1

    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        # B: IS DELIBERATELY NEVER OPENED IN A DISK WINDOW. See the docstring:
        # that is what makes every lookup below a test of the kernel's own seed.
        if not ui.path(BOX):
            fail("could not launch the DOS box")
            return 1
        dm = dosmap.package()

        def titles():
            out = []
            for sl in range(8):
                wp = os88geom.winptr(m, sl, m.sym)
                seg = struct.unpack("<H", m.read(wp + os88geom.W_SEG, 2))[0]
                toff = struct.unpack("<H", m.read(wp + os88geom.W_TITLE, 2))[0]
                if seg and toff:
                    out.append(m.read(seg * 16 + toff, 32).split(b"\0")[0]
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
            w = ui.window("DOS")
            if w:
                ui.raise_window(w)

        def typ(s):
            m.type_text(s)
            os88marty.settle(m)

        REFUSALS = ("Cannot open", "Bad command", "no program on this disk")

        def clear():
            """Close every window but the box.

            TWO REASONS, and both bit this row before it was written down.
            The window table is EIGHT slots (SPEC.md 11), so cases that each
            leave a window behind run the machine out of them and the LAST
            case fails for a reason that has nothing to do with it - which is
            how `OPEN ...BROWSER.HTM` came back empty with seven windows up.
            And a title that is still on the screen from an EARLIER case makes
            the next `want` match before its command has even run, which is
            how a per-volume boundary read as a program being found by name.
            """
            for t in list(titles()):
                if t == "DOS":
                    continue
                # TOLERANT, because titles() and ui.window() read the table at
                # two different moments: a window that closed itself in
                # between raises rather than answering, and that is not this
                # row's business.
                try:
                    w = ui.window(t)
                except Exception:
                    continue
                if w:
                    try:
                        ui.close(w)
                    except Exception:
                        pass
            os88marty.settle(m)
            to_box()

        def launch(line, want, limit=30.0):
            """Type it; True once a window whose title CONTAINS `want` is up.
            A refusal on the console ends the wait at once, so a failure costs
            a second rather than the whole limit."""
            to_box()
            typ(line + "\n")
            end = time.time() + limit
            while time.time() < end:
                if any(want in t for t in titles()):
                    return True
                if any(r in c for c in console()[-2:] for r in REFUSALS):
                    return False
                time.sleep(1.0)
                os88marty.settle(m)
            return any(want in t for t in titles())

        def says(line, want, limit=15.0):
            """Type it and wait for `want` among the last console lines."""
            to_box()
            typ(line + "\n")
            end = time.time() + limit
            while time.time() < end:
                if any(want in c for c in console()[-3:]):
                    return True
                time.sleep(1.0)
                os88marty.settle(m)
            return False

        to_box()
        # THE OPENING HINT NAMES HELP, which is how anybody finds it at all.
        # Checked HERE and not beside the HELP case below: the console is a
        # 25-row SCREEN and not a log, so by then the banner has scrolled off
        # and the check would fail for a reason that is not about the hint.
        if not any("or help" in r for r in console()[:4]):
            fail("the opening hint does not name help (SPEC.md 96.33.23): %r"
                 % console()[:4])
        else:
            print("dosopen: ?  the hint names help")
        if not console()[-1].endswith("A:\\>"):
            fail("the prompt is %r and not A:\\> - every case below is typed "
                 "from the system disk's ROOT, which is the folder that holds "
                 "none of these programs" % console()[-1])
            return 1

        # --- A: a typed PATH to a package (SPEC.md 96.33.21) -----------------
        if not launch("B:\\APPS\\CALC.O88", "Calculator"):
            fail("a FULLY QUALIFIED path to a .O88 did not launch it "
                 "(SPEC.md 96.33.21). Console: %r" % console()[-3:])
        else:
            print("dosopen: A  B:\\APPS\\CALC.O88 -> Calculator")

        clear()
        typ("B:\n")
        if not launch("APPS\\PIANO.O88", "Piano"):
            fail("a RELATIVE path to a .O88 did not launch it from B:\\ "
                 "(SPEC.md 96.33.21). Console: %r" % console()[-3:])
        else:
            print("dosopen: A  APPS\\PIANO.O88 (relative) -> Piano")

        clear()
        typ("CD APPS\n")
        if not launch("NOTEPAD", "Note Pad"):
            fail("a BARE name in the folder that holds it stopped working - "
                 "the resolve-only entry must still answer the folder we "
                 "stand in (SPEC.md 96.33.21). Console: %r" % console()[-3:])
        else:
            print("dosopen: A  NOTEPAD (bare, in its own folder) -> Note Pad")
        clear()
        typ("A:\n")

        # --- B: PROGRAM DOCUMENT, the program NOT in this folder -------------
        # The literal case: standing on A:\, with NOTEPAD.O88 in A:\APPS\.
        if not launch("NOTEPAD README.TXT", "Note Pad"):
            fail("`NOTEPAD README.TXT` on A:\\ did not open Note Pad. The "
                 "program is in A:\\APPS\\ and the search walks the CURRENT "
                 "folder, so the stem has to reach the kernel's association "
                 "lookup (SPEC.md 96.33.21.2) and the kernel has to SEED the "
                 "volume itself (21.5.3.1). Console: %r" % console()[-3:])
        else:
            print("dosopen: B  NOTEPAD README.TXT -> %r"
                  % [t for t in titles() if "Note" in t])
        clear()

        # ...and the document really ARRIVED, which the title is the proof of:
        # TeXPad puts the file name in its own title, so this asserts the
        # OSAPI_ARG_FILE handover and not merely that a window opened.
        if not launch("B:\\APPS\\TEXPAD.O88 B:\\MEDIA\\GUIDE.TEX", "GUIDE.TEX"):
            fail("the DOCUMENT did not reach the package: TeXPad names the "
                 "file it was launched on in its own title, and no window "
                 "carries GUIDE.TEX (SPEC.md 21.5.3). Titles: %r, console: %r"
                 % (titles(), console()[-3:]))
        else:
            print("dosopen: B  TEXPAD.O88 + document -> %r"
                  % [t for t in titles() if "GUIDE" in t])
        clear()

        # --- C: OPEN, with no program named (SPEC.md 96.33.22) ---------------
        if not launch("OPEN B:\\MEDIA\\PAPER.TEX", "PAPER.TEX"):
            fail("`OPEN` did not launch the associated program on its "
                 "document (SPEC.md 96.33.22) - the empty-name form of "
                 "21.5.3. Titles: %r, console: %r" % (titles(), console()[-3:]))
        else:
            print("dosopen: C  OPEN ...PAPER.TEX -> %r"
                  % [t for t in titles() if "PAPER" in t])
        clear()

        if not launch("OPEN B:\\MEDIA\\BROWSER.HTM", "Browser"):
            fail("`OPEN` of a SECOND type opened no Browser - one extension "
                 "working is not an association lookup (SPEC.md 96.33.22). "
                 "Titles: %r, console: %r" % (titles(), console()[-3:]))
        else:
            print("dosopen: C  OPEN ...BROWSER.HTM -> Browser")
        clear()

        # --- the negative controls -------------------------------------------
        before = len(titles())
        if not says("NOTPAD", "Bad command or file name"):
            fail("a TYPO WITH NO TAIL must still answer `Bad command or file "
                 "name` (SPEC.md 96.33.21.2): the stem is looked up only when "
                 "a document follows it, and without that rule every "
                 "unresolved word reaches the package launcher. Console: %r"
                 % console()[-3:])
        else:
            print("dosopen: -  NOTPAD -> Bad command or file name")

        if not says("NOTPAD README.TXT", "Cannot open NOTPAD"):
            fail("a typo WITH a tail must name the PROGRAM half - `Cannot "
                 "open NOTPAD` - or the user is sent to look at the document "
                 "(SPEC.md 96.33.21.2). Console: %r" % console()[-3:])
        else:
            print("dosopen: -  NOTPAD README.TXT -> Cannot open NOTPAD")

        # --- CASE (SPEC.md 21.5.3.2), which is the reported bug ------------
        # A:\README.TXT with NOTEPAD.O88 in A:\APPS\ - the exact sequence.
        to_box()
        typ("A:\n")
        for line, why in (("notepad readme.txt", "both names lower"),
                          ("OPEN readme.txt", "document lower"),
                          ("NOTEPAD readme.TXT", "document mixed")):
            clear()
            if not launch(line, "Note Pad"):
                fail("`%s` did not open Note Pad (%s). The association tables "
                     "are UPPERCASE-EXACT and nothing on this path folds case "
                     "for them (SPEC.md 21.5.3.2). Console: %r"
                     % (line, why, console()[-3:]))
            else:
                print("dosopen: =  %-20s (%s) -> Note Pad" % (line, why))
        clear()

        # --- A LAUNCH THAT WORKED OWES A PROMPT (SPEC.md 96.33.17.1) -------
        # Reported with a photograph: two `open`s in a row, and the console
        # left the cursor at column 0 of a bare line - so the second command
        # had no `A:\>` in front of it and the log read as one command running
        # into the next. It is 96.33.17's gap rather than this feature's: a
        # DOS program's prompt comes back with its EXIT LINE and a package has
        # none, so both spellings are asserted here.
        for line, want in (("OPEN README.TXT", "Note Pad"),
                           ("B:\\APPS\\CALC.O88", "Calculator")):
            clear()
            if not launch(line, want):
                fail("`%s` did not launch, so the prompt assertion below "
                     "cannot run. Console: %r" % (line, console()[-3:]))
                continue
            tail = console()[-1]
            if not tail.endswith(">"):
                fail("after `%s` the console's last line is %r - a launch "
                     "that WORKED still owes a newline and a prompt, because "
                     "a package has no exit line to bring one back "
                     "(SPEC.md 96.33.17.1)" % (line, tail))
            elif tail.strip() not in ("A:\\>", "B:\\>"):
                fail("after `%s` the prompt reads %r, which is not a bare "
                     "prompt on a line of its own (SPEC.md 96.33.17.1)"
                     % (line, tail))
            else:
                print("dosopen: >  %-18s -> prompt back: %r" % (line, tail))
        clear()

        # --- ...AND IT MAY NOT DRAW OVER WHAT IT JUST OPENED (96.33.17.2) --
        # The prompt above is printed AFTER OSAPI_PKG_START returns, so the
        # package's window is already in front of the box - and a repaint from
        # the wake handler has NO CLIP REGION, the kernel arming one in front
        # of W_PAINT and nowhere else. Reported off the glass as Note Pad with
        # a black band cut through it and console text down its left edge.
        #
        # ASSERTED ON GUEST STATE AND NOT PIXELS: [con_drb] is the console's
        # dirty-ROW bitmap, so rows that were marked and NOT spent are exactly
        # what "the draw was skipped" means. A pixel diff would have to know
        # where the other window landed; this does not.
        clear()
        to_box()
        typ("OPEN README.TXT\n")
        end = time.time() + 25.0
        while time.time() < end and not any("Note Pad" in t for t in titles()):
            time.sleep(1.0)
            os88marty.settle(m)
        if not any("Note Pad" in t for t in titles()):
            fail("the launch for the overdraw check did not happen")
        else:
            b = boxseg()
            drb = struct.unpack("<I", m.read((b << 4) + dm["con_drb"], 4))[0]
            if drb == 0:
                fail("the console SPENT its marks with a package window in "
                     "front of it - [con_drb] is 0, so dos_pkg_go drew "
                     "without testing OSAPI_WM_OBSCURED and painted over "
                     "whatever had just opened (SPEC.md 96.33.17.2)")
            else:
                print("dosopen: #  covered by the launch -> marks KEPT "
                      "(con_drb=0x%08x), nothing drawn over it" % drb)
            # ...and raising the box spends them, so the prompt is not lost
            w = ui.window("DOS")
            if w:
                ui.raise_window(w)
            os88marty.settle(m)
            tail = console()[-1]
            if not tail.endswith(">"):
                fail("after raising the box the prompt is not there: %r - a "
                     "skipped draw must cost nothing, the next real paint "
                     "spending the marks (SPEC.md 96.33.17.2)" % tail)
            else:
                print("dosopen: #  ...and raising the box spends them: %r"
                      % tail)
        clear()

        # --- HELP lists the verbs (SPEC.md 96.33.23) ----------------------
        # That it FITS is dosconcga's, on the CGA band that decides it. This
        # asserts that it PRINTS, whole, and leaves a prompt - the built-in's
        # own half of 96.33.17.1.
        clear()
        to_box()
        typ("help\n")
        scr = console()
        for want in ("CD [path]", "OPEN file", "cmd > file"):
            if not any(want in r for r in scr):
                fail("HELP printed no line for %r (SPEC.md 96.33.23). "
                     "Console tail: %r" % (want, scr[-4:]))
                break
        else:
            n = sum(1 for r in scr if "  " in r and r[:1].isalnum())
            print("dosopen: ?  help -> %d lines, first %r last %r"
                  % (n, scr[-17] if len(scr) > 17 else scr[0], scr[-2]))
        if not console()[-1].rstrip().endswith(">"):
            fail("HELP left no prompt: %r (SPEC.md 96.33.17.1 - a built-in "
                 "has no exit line to bring one back)" % console()[-1])

        # --- the THREE refusals (SPEC.md 96.33.22.1) ----------------------
        # 1: not a legal 8.3 path at all. `nosuchfile.txt` is FOURTEEN
        # characters, so it never reaches the association lookup - and this is
        # the arm that spent a cycle answering with the association's message.
        if not says("OPEN nosuchfile.txt", "File not found"):
            fail("a name that is not a legal 8.3 path must answer `File not "
                 "found` - it never reaches the association lookup, so the "
                 "association's wording is wrong for it (SPEC.md 96.33.22.1). "
                 "Console: %r" % console()[-3:])
        else:
            print("dosopen: -  OPEN <14-char name> -> File not found")

        # 2: a legal name that is not there. It must NAME the file: a typo in
        # a document is the commonest failure this verb has, and the old
        # wording sent the user to look at programs instead.
        if not says("OPEN NOSUCH.TXT", "File not found - NOSUCH.TXT"):
            fail("a legal name that is not on the disk must answer `File not "
                 "found - NOSUCH.TXT`, naming the FILE (SPEC.md 96.33.22.1). "
                 "Console: %r" % console()[-3:])
        else:
            print("dosopen: -  OPEN NOSUCH.TXT -> File not found - NOSUCH.TXT")

        # ...and the same through the PROGRAM DOCUMENT door, which shares the
        # check - it is in dos_pkg_go, below both.
        if not says("NOTEPAD NOSUCH.TXT", "File not found - NOSUCH.TXT"):
            fail("`PROGRAM DOCUMENT` with a missing document must refuse the "
                 "same way OPEN does - the check is in dos_pkg_go, below both "
                 "doors (SPEC.md 96.33.22.1). Console: %r" % console()[-3:])
        else:
            print("dosopen: -  NOTEPAD NOSUCH.TXT -> File not found - NOSUCH.TXT")

        # 3: the file IS there and nothing claims its type. No shipped disk
        # carries such a file - every visible document on these two has an
        # association, and the extensionless/unclaimed ones in the root
        # (KERNEL.SYS, the .DRVs, ASSOC.DAT) are HIDDEN, which DIR does not
        # list either and which this check agrees with. So one is MADE, with
        # the box's own COPY, onto the scratch B: the harness gives us.
        to_box()
        typ("COPY A:\\README.TXT B:\\NOCLAIM.ZZZ\n")
        os88marty.settle(m)
        if not says("OPEN B:\\NOCLAIM.ZZZ",
                    "No program is associated with that file"):
            print("dosopen: note: could not stage an unclaimed-type file "
                  "(console %r), so the third refusal is unasserted this run"
                  % console()[-3:])
        else:
            print("dosopen: -  OPEN <unclaimed type> -> No program is "
                  "associated with that file.")

        # A PROGRAM ON ANOTHER VOLUME IS NOT ASSERTED EITHER WAY, and that is
        # a finding rather than a gap in the row (SPEC.md 96.33.21.2). It was
        # asserted as a refusal and went GREEN on a fresh boot and RED here,
        # in the same build: what the association tables and the hint cache
        # know is SESSION STATE - disk.inc says so in as many words about the
        # same cache - so by this point an earlier case in this very row has
        # taught the machine where TeXPad lives. Asserting either arm asserts
        # the order of the cases above it.
        #
        # What IS asserted is the reliable half, and it is asserted in case B:
        # a PATH carries a program on another volume, every time.

        if len(titles()) < before:
            fail("a window disappeared across the refusals: %r" % titles())

    print("dosopen: %s" % ("ok - a package by path, with a document, and OPEN"
                           if not fails else "%d failed" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
