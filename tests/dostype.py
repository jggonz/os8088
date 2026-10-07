#!/usr/bin/env python3
"""`TYPE` AT THE DOS PROMPT, AND THE FLAG THAT MUTED THE BOX (SPEC.md 96.30.7).

    make && make build/dostype360.img && python3 tests/dostype.py

A tester ran the verbs for the first time and `TYPE` answered `File creation
error` on every file they pointed it at - a compressed one, an uncompressed
one, and a name that was not there. It was FOUR defects standing on each
other, and this row is one assertion per defect with the arithmetic that makes
each one fail for its own reason and not for its neighbour's.

1. IT COULD NOT READ A FILE AT ALL. `TYPE` read in 128-byte chunks and
   `OSAPI_FILE_READ_AT` refuses a capacity that is not a whole number of
   CLUSTERS (SPEC.md 18.4.4) - so the FIRST chunk was refused, on every file
   and every volume, and the smallest cluster this machine has is 512 bytes,
   so no geometry could have made 128 legal. `NOTES.TXT` is **9,200 bytes**
   against an 8 KB chunk on purpose: it takes two passes and finishes on a
   partial one, which is the case 18.4.4 makes its own exception for and the
   one a fixture that happened to be a cluster multiple would never reach.
   The assertion is the FILE'S LAST LINES, because 400 lines scroll - and the
   last line arriving is what says the second pass ran at the right offset.
2. A COMPRESSED FILE IS EXPANDED. `OSAPI_FILE_READ_AT` is raw (20.14.3) and
   `README.TXT` is a `'CZ'` container on every shipped floppy, so fixing 1
   alone would have typed a wrapper and 8 KB of LZ4 - at the file the tester
   actually tried. Read WHOLE through `OSAPI_FILE_READ`, which expands, the
   way `AH=3Dh` opens one `FHF_WHOLE`. Asserted on the README's own last line
   rather than on a byte count, because the wrapper is binary and would put
   nothing legible on the console at all.
3. ONE `>` MUTED THE BOX FOR GOOD. `[dsh_quiet]` is set by the first
   redirection and was never cleared - it is package bss, so every `dsh_say`
   in the instance went silent for the life of a window somebody leaves open.
   What that looks like is not an error, which is the point: `VER` prints
   nothing and `DIR` keeps printing NAMES while losing its `<DIR>` markers,
   its sizes and its footer. **Asserted in that order on one prompt**, and it
   is the sharpest row here: a box that has stopped speaking passes every
   other assertion in this file by printing nothing.
3b. AND IT WAS CASE-SENSITIVE, which is a FIFTH defect this row let through
   on its first build: `dos_fh_stat` compares byte-exact against an upper-case
   directory name, so `type readme.txt` answered `File not found` and
   `TYPE README.TXT` printed the file. Every assertion here typed upper case
   and every one was green. The lower-case line is asserted with a COPY beside
   it as a control, because COPY normalises through the kernel - if both go
   red the defect is below the shell.
4. THE REFUSAL WAS SILENCED BY THE FLAG IT SETS. `dsh_redir` set the flag at
   the `>` and only then asked whether the target was `NUL`, so `Cannot
   redirect to that file` never printed and `TYPE > FILE.TXT` read as a
   command that had worked - which is exactly the wrong KIND of answer
   96.30.3 refuses a non-`NUL` target to avoid giving.

...and a fifth nothing had typed: **the buffer comes out of the DOS ARENA, and
at the prompt there is no arena** (96.30.7.2). `[dos_arena]` is claimed inside
`dos_run` and freed back to 0, so `dos_mcb_alloc` walks from segment 0 through
the interrupt vector table and refuses. That is not `TYPE`'s - `COPY` at the
prompt had it first and nothing had typed one, because 96.30's verbs were
written for and tested through `AH=4Bh`. So assertion 6 is a COPY, at the
prompt, read back with the TYPE this file has just proved.

**IT READS THE CONSOLE BUFFER AND NOT THE GLASS**, for tests/doscon.py's
reason: every assertion here is about CHARACTERS, so reading pixels would be
reading a font.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88build                                               # noqa: E402
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402
from doscon import Box                                         # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/dostype360.img"
BOX = "A:/APPS/DOS.O88"

fails = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def no(msg):
    fails.append(msg)
    say("FAIL: %s" % msg)


def run(bx, cmd, settle=1.2):
    """One command, and the rows it left behind - once the PROMPT is back.

    The console buffer's last row ending in `>` again is the command having
    finished. `settle` is what the wait used to be, and bounds it in GUEST
    time now: a command that never gives the prompt back is read as it is.
    """
    bx.type(cmd + "\n")

    def back(_m):
        rows = bx.live()
        return bool(rows) and rows[-1].endswith(">")
    try:
        os88marty.until(bx.m, back, "the prompt after %r" % cmd, poll=0.2,
                        limit=settle * os88marty.GUEST_PACE
                        / os88marty.GUEST_BUDGET_RATIO)
    except os88marty.MartyError:
        pass                            # the checks below read what is there
    return bx.live()


def after(rows, cmd):
    """The rows below the line that echoed `cmd`, which is what it printed.

    The prompt echoes what was typed, so the command's own output is
    everything after the LAST row ending in it - last, because a previous run
    of the same command is still on the screen. The DRIVE is not spelled here:
    assertion 0 moves to B: and $P$G is recomposed rather than held (96.33.2),
    so an `A:\\>` in this helper would make every check below read an empty
    list and fail for the wrong reason - which it did.
    """
    tail = ">" + cmd
    hit = [i for i, r in enumerate(rows) if r.strip().endswith(tail)]
    if not hit:
        return []
    return [r for r in rows[hit[-1] + 1:] if r.strip()]


def main():
    import subprocess
    r = subprocess.run(["make", "-s", os88build.at(APPS)],
                       cwd=os.path.join(os.path.dirname(__file__), ".."),
                       capture_output=True, text=True)
    if r.returncode and not os.path.exists(os88build.at(APPS)):
        sys.exit("dostype: %s will not build:\n%s" % (APPS, r.stdout + r.stderr))

    notes = [b"line %03d of the notes" % i for i in range(1, 401)]
    want = "os8088 is free software, under the MIT licence."

    with os88ui.boot(os88build.at(SYS), apps=os88build.at(APPS)) as ui:
        m = ui.m
        ui.path(BOX)
        os88marty.settle(m)
        bx = Box(m)

        # --- 0: stand on B:, where the fixture is ---------------------------
        rows = run(bx, "B:")
        if not bx.live()[-1].startswith("B:\\>"):
            no("`B:` did not move the prompt: the last row is %r and the "
               "fixture disk is B: (SPEC.md 96.33.6)" % bx.live()[-1])
            return 1

        # --- 1: A PLAIN FILE, LONGER THAN ONE CHUNK -------------------------
        rows = run(bx, "TYPE NOTES.TXT", settle=6.0)
        out = [r.strip() for r in rows if r.strip()]
        say("TYPE NOTES.TXT left %d rows, last: %r" % (len(out), out[-1:]))
        tailwant = [notes[-3].decode(), notes[-2].decode(), notes[-1].decode()]
        got = [r for r in out if r.startswith("line ")]
        if not got:
            no("TYPE NOTES.TXT put no `line NNN` row on the console at all. "
               "That is defect 1 (SPEC.md 96.30.7): a 128-byte capacity is "
               "not a whole number of clusters and OSAPI_FILE_READ_AT refuses "
               "it on the FIRST chunk, which the console reports as `File "
               "creation error`. The rows it did leave are %r" % out[-4:])
        elif got[-3:] != tailwant:
            no("NOTES.TXT is 9,200 bytes - two 8KB chunks, the second partial "
               "- and its last three lines should be %r; the console's last "
               "three are %r. The FIRST chunk arriving and the last not is "
               "the offset the second pass asked for (SPEC.md 18.4.4's "
               "cluster rule applies to the OFFSET as well as the capacity)"
               % (tailwant, got[-3:]))
        else:
            say("  the 400th line arrived, so both passes read at the right "
                "offset and the partial tail was delivered whole")

        # --- 1b: ...AND THE SAME FILE IN LOWER CASE -------------------------
        # **THE SPELLING IS THE ASSERTION** (SPEC.md 96.30.7.1). dos_fh_stat
        # compares byte-exact and 8.3 names are upper case on the disk, so a
        # leaf copied verbatim made `type readme.txt` answer `File not found`
        # while `TYPE README.TXT` printed the file - one keystroke apart, on
        # the same file, in the same folder. Every assertion in this row typed
        # UPPER CASE and every one of them was green on that build, which is
        # docs/WRITING-TESTS.md §1 in one spelling: the row tested the command
        # and not the way anybody types it. The verb goes lower too, because
        # the verb and the argument are normalised by different code.
        rows = run(bx, "type short.txt")
        out = after(rows, "type short.txt")
        say("type short.txt (lower case) -> %r" % out[:2])
        if not out or out[0].strip() != "a short one":
            no("`type short.txt` printed %r where `TYPE SHORT.TXT` prints the "
               "file. dsh_stat hands dos_fh_stat the leaf as typed and that "
               "compare is byte-exact against an upper-case directory name "
               "(SPEC.md 96.30.7.1) - so this is the whole command broken for "
               "the way people actually type, with every upper-case assertion "
               "in this file still green" % out[:2])

        # ...and the control: every OTHER verb is case-blind already, because
        # it hands its name to the kernel, whose dskw_name83 upper-cases into
        # the 8.3 field. If these go red too, the defect is not dsh_stat's.
        rows = run(bx, "copy short.txt lower.txt", settle=3.0)
        out = after(rows, "copy short.txt lower.txt")
        say("copy short.txt lower.txt (control) -> %r" % out[:2])
        if not out or "copied" not in out[0]:
            no("the CONTROL failed: `copy short.txt lower.txt` printed %r. "
               "COPY normalises through the kernel, so if it cannot take a "
               "lower-case name either, the case defect is below the shell "
               "and not in dsh_stat" % out[:2])
        rows = run(bx, "del lower.txt", settle=2.0)

        # --- 1c: A RELATIVE PATH PREFIX (SPEC.md 96.30.8) -------------------
        # dsh_resolve kept the separator with the FOLDER part - correct for a
        # leading one, where cutting leaves an empty string where the caller
        # meant the root, and wrong for every other, where it left a TRAILING
        # one. `BIN\X` then handed dsh_cdto a string whose single component is
        # EMPTY, dos_cd_go read that as "where we already are" and answered
        # SUCCESS, and the leaf was looked for in the wrong folder. Every verb
        # said File not found about a file plainly there.
        # ASSERTED ON THREE VERBS, because the resolve is shared and a fix that
        # only reached one of them would be the wrong fix.
        rows = run(bx, "TYPE BIN\\NOTE.TXT", settle=3.0)
        out = after(rows, "TYPE BIN\\NOTE.TXT")
        say("TYPE BIN\\NOTE.TXT -> %r" % out[:2])
        if not out or out[0].strip() != "in the bin":
            no("`TYPE BIN\\NOTE.TXT` printed %r and BIN\\NOTE.TXT is `in the "
               "bin`. A relative path prefix resolved to the folder we were "
               "STANDING in (SPEC.md 96.30.8) - `CD BIN` then `TYPE NOTE.TXT` "
               "works, which is what made this invisible" % out[:2])
        rows = run(bx, "COPY BIN\\NOTE.TXT PULLED.TXT", settle=3.0)
        out = after(rows, "COPY BIN\\NOTE.TXT PULLED.TXT")
        say("COPY BIN\\NOTE.TXT PULLED.TXT -> %r" % out[:2])
        if not out or "copied" not in out[0]:
            no("`COPY BIN\\NOTE.TXT PULLED.TXT` printed %r: the same resolve, "
               "and COPY reads the prefix through it too" % out[:2])
        rows = run(bx, "DEL PULLED.TXT", settle=2.0)

        # ...and the DRIVE-qualified form, which was NEVER broken and which a
        # careless fix breaks: its folder part is `A:`, owned by .colon, and
        # cutting the separator off blindly makes the LEAF `\README.TXT`.
        rows = run(bx, "TYPE A:\\README.TXT", settle=8.0)
        if not any(want in r for r in rows):
            no("`TYPE A:\\README.TXT` stopped working. The drive-qualified "
               "form has its own arm and must survive the relative fix "
               "(SPEC.md 96.30.8) - this went red on the first attempt at it")

        # --- 1d: DIR ON A NAMED FOLDER (SPEC.md 96.30.8) --------------------
        # The sort claims the whole listing up front so a /P run sorts as ONE
        # listing - and it walked with no goto in front of it, so it filled the
        # claim from wherever the instance stood while dsh_dir_page correctly
        # stood in the folder that was named. A bare DIR is right because the
        # two are the same folder, which is why nothing caught it.
        rows = run(bx, "DIR BIN", settle=3.0)
        out = after(rows, "DIR BIN")
        say("DIR BIN -> %r" % out[:2])
        if not any("DOSHELLO" in r for r in out):
            no("`DIR BIN` listed %r and BIN holds DOSHELLO.COM and NOTE.TXT. "
               "dsh_sbuild walked the folder we were STANDING in rather than "
               "the one named (SPEC.md 96.30.8), and the rows came out of that "
               "claim - so it listed a plausible directory rather than failing"
               % out[:3])
        if any(r.strip().startswith("BIN ") for r in out):
            no("`DIR BIN` listed BIN itself, which means it listed the PARENT: "
               "%r" % out[:3])

        # --- 2: A FILE SHORTER THAN ONE CLUSTER -----------------------------
        rows = run(bx, "TYPE SHORT.TXT")
        out = after(rows, "TYPE SHORT.TXT")
        say("TYPE SHORT.TXT -> %r" % out[:2])
        if not out or out[0].strip() != "a short one":
            no("TYPE SHORT.TXT printed %r and the file is 13 bytes of `a "
               "short one`. A file under one cluster makes the FIRST read the "
               "tail, which is the arm 18.4.4 clamps rather than refuses"
               % out[:2])

        # --- 3: ^Z ENDS IT --------------------------------------------------
        rows = run(bx, "TYPE CTRLZ.TXT")
        out = after(rows, "TYPE CTRLZ.TXT")
        say("TYPE CTRLZ.TXT -> %r" % out[:3])
        if not out or out[0].strip() != "before the mark":
            no("TYPE CTRLZ.TXT printed %r and the file opens `before the "
               "mark`" % out[:3])
        if any("AFTERMARK" in r for r in out):
            no("TYPE printed AFTERMARK, which is the text AFTER the ^Z: DOS "
               "ends a text file at 26 and so does this (SPEC.md 96.30.2)")

        # --- 4: THE COMPRESSED ONE, WHICH IS WHAT THE FIELD TYPED -----------
        rows = run(bx, "TYPE A:\\README.TXT", settle=8.0)
        # THE WHOLE BUFFER AND NOT after(), for assertion 1's reason: the
        # README is 14,722 bytes and scrolls its own prompt row off the
        # twenty-five the console keeps, so there is no echoed line left to
        # read below.
        out = [r.strip() for r in rows if r.strip()]
        say("TYPE A:\\README.TXT left %d rows, last: %r" % (len(out), out[-1:]))
        if not any(want in r for r in out):
            no("TYPE A:\\README.TXT did not put %r on the console. README.TXT "
               "is a 'CZ' container of 8,088 bytes whose contents are 14,722 "
               "(SPEC.md 20.13) and OSAPI_FILE_READ_AT is RAW (20.14.3), so "
               "the compressed arm must read it WHOLE through "
               "OSAPI_FILE_READ, which expands (96.30.7.1). Rows: %r"
               % (want, out[-4:]))
        else:
            say("  the compressed README expanded and its last line arrived")

        # --- 5: A NAME THAT IS NOT THERE ------------------------------------
        rows = run(bx, "TYPE NOSUCH.TXT")
        out = after(rows, "TYPE NOSUCH.TXT")
        say("TYPE NOSUCH.TXT -> %r" % out[:2])
        if not out or out[0].strip() != "File not found":
            no("TYPE NOSUCH.TXT printed %r and DOS 3.30 answers `File not "
               "found`. `File creation error` is COPY's message and TYPE "
               "creates nothing - it is what sent the first person to try the "
               "verb looking for a destination to give it (SPEC.md 96.30.7)"
               % out[:2])

        # --- 6: COPY AT THE PROMPT, which needs a buffer --------------------
        # HERE AND NOT IN tests/doscon.py: the buffer is the same one TYPE now
        # uses, and at the prompt it cannot come from the DOS arena because
        # there is no program and therefore no arena (SPEC.md 96.30.7.2).
        rows = run(bx, "COPY SHORT.TXT COPIED.TXT", settle=3.0)
        out = after(rows, "COPY SHORT.TXT COPIED.TXT")
        say("COPY SHORT.TXT COPIED.TXT -> %r" % out[:2])
        if any("Insufficient memory" in r for r in out):
            no("COPY at the prompt answered `Insufficient memory` on a "
               "machine with hundreds of KB free. dos_mcb_alloc walks a chain "
               "that starts at [dos_arena], which is 0 outside a bracket, so "
               "it walks the interrupt vector table and refuses - the prompt's "
               "buffer has to come off the HEAP (SPEC.md 96.30.7.2)")
        rows = run(bx, "TYPE COPIED.TXT")
        out = after(rows, "TYPE COPIED.TXT")
        say("TYPE COPIED.TXT -> %r" % out[:2])
        if not out or out[0].strip() != "a short one":
            no("COPY wrote COPIED.TXT and TYPE reads %r out of it, where "
               "SHORT.TXT is `a short one`" % out[:2])

        # --- 7: A REFUSED REDIRECTION SPEAKS, AND DOES NOT MUTE THE BOX -----
        # **THE ORDER IS THE ASSERTION.** A box that has stopped speaking
        # passes every check above by printing nothing, so the redirection
        # goes LAST and VER and DIR are asked again after it.
        rows = run(bx, "TYPE NOTES.TXT > FILE.TXT")
        out = after(rows, "TYPE NOTES.TXT > FILE.TXT")
        say("TYPE ... > FILE.TXT -> %r" % out[:2])
        if not out or out[0].strip() != "Cannot redirect to that file":
            no("a redirection to a file that is not NUL printed %r. It is "
               "REFUSED by design (SPEC.md 96.30.3) and the refusal was "
               "suppressed by the very flag the `>` had just set, so the "
               "command read as one that had worked - the wrong KIND of "
               "answer that section refuses a non-NUL target to avoid"
               % out[:2])

        rows = run(bx, "VER")
        out = after(rows, "VER")
        say("VER after the redirection -> %r" % out[:2])
        if not out or "os8088 DOS Version" not in out[0]:
            no("VER printed %r after one redirection. [dsh_quiet] is package "
               "bss and was never cleared, so ONE `>` silenced every dsh_say "
               "in the instance for the life of the window (SPEC.md 96.30.7)"
               % out[:2])

        rows = run(bx, "DIR", settle=3.0)
        out = after(rows, "DIR")
        say("DIR after the redirection -> %d rows" % len(out))
        if not any("File(s)" in r for r in out):
            no("DIR after a redirection printed %r and lost its footer. It "
               "writes NAMES through dos_tty directly and its <DIR> markers, "
               "its sizes and its footer through dsh_say, so a muted box "
               "still lists - the listing silently changes SHAPE, which is "
               "why this is asserted on the footer and not on the rows"
               % out[-3:])
        if not any("<DIR>" in r for r in out):
            no("DIR after a redirection printed no <DIR> marker and BIN/ is a "
               "folder on this disk: same defect as the footer above")

    say("dostype: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
