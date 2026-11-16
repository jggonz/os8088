#!/usr/bin/env python3
"""qemuapi - does `OS88_EMU=qemu` answer the os88marty API, and is a row
written against os88ui the same row on QEMU? (tools/os88qemuapi.py,
docs/plans/soak-speed-2/qemubox.md)

    python3 tests/qemuapi.py

Sets $OS88_EMU=qemu for itself, so `os88ui.boot` launches a private QEMU,
and drives the desktop with the verbs a MartyPC row uses - nothing in the
body below names QEMU:

  1. boot(quick=True): launched paused, run, `up` on the kernel's state;
  2. open_drive("B"), open("APPS"), open("CALC.O88") - every step confirmed
     off guest state (wm_wins, the listing), through os88mouse's CLOSED-LOOP
     pointer on the API's relative `mouse` packets;
  3. key_taken: a key the guest TOOK - the ROM stored it (the ring's tail
     moved) and the UI drained it;
  4. menu_pick("Calc", "Close"): the window gone;
  5. a write read back (the GDB stub's `M`), an `fbuf` the VGA's size, and
     the clock: `status()["cycles"]` moves while running and stands while
     paused;
  6. the refusals NAMED: a breakpoint, `Forks`, a Hercules machine - each a
     MartyError saying what the backend has not got, never a stand-in.

RED: break the backend's key map (`qcode`) or its mouse (`_c_mouse` sending
nothing) and 2-3 fail at the verb that missed, naming what the guest showed.
"""
import os
import sys

os.environ["OS88_EMU"] = "qemu"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [os.path.join(ROOT, "tools"), HERE]

import os88build                                                # noqa: E402
import os88marty                                                # noqa: E402
import os88ui                                                   # noqa: E402

IMG = os88build.at("build/os8088.img")
APPS = os88build.at("build/apps.img")
fails = []


def check(ok, what):
    print("   %s %s" % ("ok  " if ok else "FAIL", what), flush=True)
    if not ok:
        fails.append(what)
    return ok


def refuses(fn, what, word):
    try:
        fn()
    except os88marty.MartyError as e:
        return check(word in str(e), "%s refused, naming it: %s"
                     % (what, str(e).splitlines()[0][:90]))
    except Exception as e:                                  # noqa: BLE001
        return check(False, "%s raised %s, not a MartyError" % (what, e))
    return check(False, "%s was NOT refused" % what)


def main():
    for p in (IMG, APPS):
        if not os.path.exists(p):
            sys.exit("qemuapi: no %s - make" % p)
    print("qemuapi: os88ui on QEMU ($OS88_EMU=qemu)")
    with os88ui.boot(IMG, apps=APPS, machine="os8088_xt_vga",
                     verbose=False, quick=True) as ui:
        m = ui.m
        check(type(m).__name__ == "ApiQemu", "the machine is %s"
              % type(m).__name__)
        c0 = m.status()["cycles"]
        print("   up at %.1f guest s" % (c0 / os88marty.GUEST_HZ))

        d = ui.open_drive("B")
        check(bool(d.title.strip()), "open_drive B: -> %r" % d.title)
        ui.open("APPS")
        names = [r[0] for r in ui.listing()]
        check("CALC.O88" in names, "open('APPS'): %d entries" % len(names))
        w = ui.open("CALC.O88")
        check(w.title == "Calculator", "open('CALC.O88') -> %r" % w.title)

        t0 = m.read(0x41C, 2)
        os88marty.key_taken(m, "Digit7")
        kb = m.read(0x41A, 4)
        got = m.read(0x400 + (t0[0] | t0[1] << 8), 2)  # the entry it stored
        check(m.read(0x41C, 2) != t0 and kb[0:2] == kb[2:4]
              and got == b"7\x08", "key_taken: the ROM stored '7' (scan 08h, "
              "got %s) and the UI took it" % got.hex())

        ui.menu_pick("Calc", "Close")

        def gone():
            return not [x for x in ui.windows() if x.title == "Calculator"]
        # (the pick is confirmed when the item FIRES; the close it asks for
        # is the package's, a pass later - so the state is waited for)
        try:
            os88marty.until(m, lambda _: gone(), "the Calculator closed",
                            poll=0.02, guest=5.0)
        except os88marty.MartyError:
            pass
        check(gone(), "menu_pick('Calc', 'Close'): the window is gone")

        # the clock, a write, the glass
        os88marty.ui_done(m)
        a = 0x4F0                       # the BIOS's ICA: free to write
        m.write(a, b"\x5a\xa5")
        check(m.read(a, 2) == b"\x5a\xa5", "a write (GDB `M`) reads back")
        m.write(a, b"\0\0")
        c1 = m.status()["cycles"]
        os88marty.guest_sleep(m, 0.3)
        c2 = m.status()["cycles"]
        check(c2 > c1, "the clock moves while running (+%d cycles)"
              % (c2 - c1))
        m.pause()
        c3 = m.status()["cycles"]
        c4 = m.status()["cycles"]
        check(c3 == c4, "...and stands while paused")
        m.run()
        fw, fh, rgb = m.fbuf()
        check((fw, fh) == (640, 480) and len(rgb) == fw * fh * 3,
              "fbuf is the VGA's %dx%d" % (fw, fh))

        refuses(lambda: m.breakpoints([{"type": "exec", "addr": 0x600}]),
                "a breakpoint", "QEMU backend")
        refuses(lambda: m.step(), "a single step", "QEMU backend")
    import os88qemuapi
    refuses(lambda: os88qemuapi.machine_args("os8088_5150_herc_gla"),
            "a Hercules machine", "HERC")
    print("qemuapi: %s" % ("FAILED (%d)" % len(fails) if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
