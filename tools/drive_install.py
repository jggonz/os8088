#!/usr/bin/env python3
"""Drive the HDD driver's Install flow on MartyPC, for SPEC.md 52.10.4.

A scripted UI walk, kept because getting to the installer is nine clicks deep
- System menu, Control Panel, Drivers, tick Hard Drive, its page, Install,
pick a slot, arm, go - and re-deriving them by hand every time is most of the
cost of testing this.

TWO THINGS ABOUT MARTYPC'S MOUSE that this exists to encapsulate:

  * it is RELATIVE, so an absolute position is "pin against the kernel's own
    edge clamp, then step out" - the same trick tools/mouse.py plays on QEMU;
  * a MENU needs press / move / release as separate packets, because
    menu_track draws the pull-down and then polls a level (SPEC.md 9.6.1's
    reasoning). os88marty's `click` presses and releases in place, which
    opens a menu and closes it again in the same breath.

Positions are for a 640x200 CGA, which is what os8088_xt_hdd has.
"""
import argparse
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from os88mouse import Mouse                                # noqa: E402


class UI(Mouse):
    """tools/os88mouse.py's absolute driver, plus a screenshot helper."""

    def shot(self, path):
        import subprocess
        subprocess.run(["python3", "tools/os88marty.py", ADDR, "shot",
                        "--rendered", path], check=False)


def main():
    global ADDR
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("--stop", default="install",
                    help="page | slots | arm | install")
    ap.add_argument("--shot", default="/tmp/step.png")
    a = ap.parse_args()
    ADDR = a.addr
    u = UI(a.addr, verbose=False)

    # --- System menu -> Control Panel -------------------------------------
    u.menu(12, 8, 40, 45)
    # --- the Drivers page, then tick Hard Drive ---------------------------
    u.click(190, 118)                       # 'Drivers' in the page list
    u.click(268, 118, settle=5.0)           # its checkbox
    u.click(196, 132, settle=2.0)           # the 'Hard Drive' row that appears
    u.shot(a.shot)
    if a.stop == "page":
        return 0

    # --- Install, and move the window clear of the panel -------------------
    u.click(445, 153)                       # the page's third button
    u.drag(200, 78, 200, 120)               # clear of the Control Panel
    u.shot(a.shot)
    if a.stop == "slots":
        return 0

    print("arming...")
    u.click(110, 233)                       # Install: arms
    u.shot(a.shot)
    if a.stop == "arm":
        return 0

    print("installing...")
    u.click(110, 233, settle=0)             # Install: goes
    return 0


if __name__ == "__main__":
    sys.exit(main())
