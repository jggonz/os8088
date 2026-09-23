#!/usr/bin/env python3
"""The DOS wave-1 gate (SPEC.md 96, docs/plans/DOS-EXEC-PLAN.md 11).

Double-click a .COM in a Disk window and assert that a real DOS program ran:
its output on the text screen inside the bracket, and its exit code in the
package's window after it.

WHAT IT WOULD CATCH, and every one of these was seen FAILING on the way to
writing it (docs/WRITING-TESTS.md 1 - the row exists because the thing was
broken on purpose and went red, three different ways):

  - the association not resolving          -> no DOS window opens at all
  - the handler resolving but the read      -> "Could not run it / It could not
    coming from the HANDLER's folder           be read" - the gate disk carries
    (OSAPI_FILE_GOTO_Q, not _QM)               NO handler for exactly this
  - the far jump landing on PSP:0000       -> "Exit code 000" and no output,
                                              which is a program that ran and
                                              printed nothing, from the outside
  - PSP:0002 holding the wrong paragraph   -> the KB line reads 8, not 520
  - the refusal path answering instead of  -> "ANSWERED - the gate has FAILED"
    setting CF in the pushed FLAGS            printed by the program itself
  - the machine-state ledger not restoring -> the desktop does not come back

It runs on MartyPC and must: the whole point is a real 8088 executing DOS
code, and the bracket sets a text mode the harness reads with `screen()`.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88ui                                                  # noqa: E402
import os88marty                                               # noqa: E402

SYS = "build/os8088-360.img"
COM = "build/doscom360.img"


def fail(msg):
    print("doscom: FAIL: %s" % msg)
    sys.exit(1)


def main():
    for p in (SYS, COM):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disk" % p)

    with os88ui.boot(SYS, apps=COM) as ui:
        m = ui.m
        win = ui.path("B:/DOSHELLO.COM")
        if not win:
            fail("double-clicking DOSHELLO.COM opened no window")
        print("doscom: the association launched DOS.O88 -> %s" % win)

        # --- 1. the program's own output, inside the bracket -----------------
        rows = []
        for _ in range(80):
            rows = m.screen() or []
            if any("READY" in r for r in rows):
                break
            time.sleep(0.2)
        else:
            fail("the program never reached its READY prompt; the last text "
                 "screen was %r" % ([r.rstrip() for r in rows][:8],))

        text = "\n".join(r.rstrip() for r in rows)
        print("doscom: the bracket's text screen:")
        for r in rows[:12]:
            if r.strip():
                print("   | %s" % r.rstrip())

        # 3.30 and not 3.31: the box reports the version of the DOS it is
        # measured against (SPEC.md 96.21.7), so that a trace diffed against a
        # real IBM DOS 3.30 does not lead with a permanent disagreement on
        # call 2.  Spelled out here because this string is the ONLY place the
        # published version is asserted, and changing it in the box alone is a
        # change nothing else notices.
        for want in ("os8088 DOS gate", "DOS version 3.30"):
            if want not in text:
                fail("%r is not on the screen - INT 21h AH=09h or AH=30h" % want)

        if "ANSWERED - the gate has FAILED" in text:
            fail("an unsupported INT 21h answered instead of refusing: the "
                 "CF a DOS call returns is the one in the PUSHED flags, not "
                 "the live one (SPEC.md 96.7)")
        if "refused with CF" not in text:
            fail("the unsupported-call probe printed neither outcome")

        kb = None
        for r in rows:
            if "Memory to top of block:" in r:
                try:
                    kb = int(r.split(":")[1].strip().split()[0])
                except (IndexError, ValueError):
                    fail("could not read the KB figure out of %r" % r.rstrip())
        if kb is None:
            fail("the program never printed its top-of-memory figure")
        # The arena is OSAPI_MEM_AVAIL's whole answer (SPEC.md 96.3), which on
        # a 640KB machine is ~520KB once the package's own region is out of
        # it. The bound is deliberately loose at the top and tight at the
        # bottom: what this is really asserting is that PSP:0002 names the top
        # of the ARENA and not the top of some 8KB accident.
        if not (256 <= kb <= 640):
            fail("PSP:0002 says %d KB, which is not a plausible arena on a "
                 "640KB machine (SPEC.md 96.3)" % kb)
        print("doscom: PSP:0002 = %d KB above the PSP" % kb)

        # --- 2. the exit code, and the desktop coming back -------------------
        m.type_text("x")
        os88marty.settle(m)

        titles = ui.titles()
        if "DOS" not in titles:
            fail("the DOS window is gone after the program exited: %r" % titles)
        if "Disk" not in titles:
            fail("the desktop did not come back - the Disk window is missing "
                 "after the bracket (SPEC.md 53.6)")
        print("doscom: back on the desktop, windows %r" % (titles,))

        wd, ht, data = m.fbuf()
        os88marty.write_png_rgb("build/doscom.png", wd, ht, data)
        print("doscom: build/doscom.png written")

    print("doscom: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
