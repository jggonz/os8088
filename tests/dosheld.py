#!/usr/bin/env python3
"""A launch with Shift or Alt HELD names the program and runs nothing
(SPEC.md 96.21.9).

Opening a program or a shortcut from a Disk window runs it, so there was no
way to reach a .LNK's settings, or the prompt beside a SETUP.EXE, without the
program starting first. The row holds ALT across the double-click - the
mouse's key, because a Shift+click in a Disk window is a range select and
never opens anything (SPEC.md 22.27) - and asserts three things:

  1  DOS reaches DST_HELD, with the path box filled - the launch staged
     everything it stages - and NOT DST_READY or beyond.
  2  THE PROGRAM DID NOT RUN: DOSARGS.COM prints READY the moment it starts,
     and it is not on the glass after several guest seconds.
  3  ENTER STARTS IT: with the key let go, one Enter runs the held program
     and READY appears.

Shift is the same test on the same byte (bits 0-1 beside Alt's bit 3 of the
BIOS shift state); it is the keyboard route, Shift+Enter on a selected entry.

Break it on purpose: take the `jnz .held` out of dos_entry and step 1 fails
with DST_READY / RAN, step 2 with READY on the glass.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty                                               # noqa: E402
import os88ui                                                  # noqa: E402
import dosmap                                                  # noqa: E402

SYS = "build/os8088-360.img"
APPS = "build/dosargs360.img"
DST_HELD = 4                        # apps/dos/dos.asm's own
NAMES = {0: "IDLE", 1: "READY", 2: "RAN", 3: "ERR", 4: "HELD", 5: "CPWAIT"}


def fail(msg):
    print("dosheld: FAIL: %s" % msg)
    sys.exit(1)


def ready_on_glass(m):
    return any("READY" in r for r in (m.screen() or []))


def main():
    for p in (SYS, APPS):
        if not os.path.exists(p):
            fail("%s is missing - `make doscom` builds the gate disks" % p)
    dm = dosmap.package()
    for n in ("dos_state", "dos_path"):
        if n not in dm:
            fail("dosmap has no %s" % n)

    with os88ui.boot(SYS, apps=APPS, machine="os8088_5150_herc_gla") as ui:
        m = ui.m
        if not ui.path("B:/BIN"):
            fail("could not reach B:/BIN")

        m.key("AltLeft", down=True, up=False)
        try:
            ui.open("DOSARGS.COM", expect="window")

            def _state(mm):
                ps = dosmap.instance(mm)
                return mm.read(dosmap.lin(mm, ps, dm["dos_state"]), 1)[0]

            def _path(mm):
                ps = dosmap.instance(mm)
                return bytes(mm.read(dosmap.lin(mm, ps, dm["dos_path"]),
                                     40)).split(b"\0")[0]

            # --- 1: staged and held ----------------------------------------
            try:
                os88marty.until(m, lambda mm: _path(mm).upper()
                                .endswith(b"DOSARGS.COM"),
                                "the entry proc to fill the path box",
                                limit=60.0)
            except os88marty.MartyError:
                fail("the path box never named DOSARGS.COM (it holds %r)"
                     % _path(m))
            os88marty.pace(m, 3.0)      # guest seconds: a run would have begun
            st = _state(m)
            if st != DST_HELD:
                fail("with Alt held DOS is in DST_%s, not DST_HELD - the "
                     "launch ran (or queued) the program (SPEC.md 96.21.9)"
                     % NAMES.get(st, st))
            # --- 2: nothing ran --------------------------------------------
            if ready_on_glass(m):
                fail("READY is on the glass: DOSARGS.COM ran although Alt was "
                     "held")
            print("dosheld: Alt held - DST_HELD, path %r, nothing ran"
                  % _path(m).decode("latin-1"))
        finally:
            m.key("AltLeft", down=False, up=True)

        # --- 3: Enter starts it --------------------------------------------
        os88marty.settle(m)
        m.key("Enter")
        try:
            os88marty.until(m, ready_on_glass, "READY", poll=0.3, limit=120.0)
        except os88marty.MartyError:
            fail("Enter after a held launch did not start the program "
                 "(DST_%s)" % NAMES.get(_state(m), _state(m)))
        print("dosheld: Enter started it - READY")
    print("dosheld: ok")


if __name__ == "__main__":
    main()
