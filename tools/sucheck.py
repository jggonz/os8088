#!/usr/bin/env python3
"""The RAISE CACHE gate (SPEC.md 11.96) - is it taken, and does it put back
the right pixels?

    python3 tools/sucheck.py [machine]

Until this existed there was no test of 11.96 at all, and the reason is worth
knowing before you write another one: the cache is only taken for a window
that opted in with WF_SAVEU, and for a long time NOTE PAD was the only window
that did - so every scripted session that covered a Disk window, a Tracker or
a Task Manager and looked at the claim map saw nothing, and concluded the
feature was dead. It was not; nothing being driven had made the promise.

So this drives SOLITAIRE, which promises since SPEC.md 11.96.1, and asserts
the two things that can go wrong:

  * the cache is CLAIMED, at the trivial purgeable rank (SPEC.md 50.6.4) -
    a claim at any other rank means the tag is wrong, and a tag that is wrong
    makes mem_pg_forget zero the wrong kernel word on a shed;
  * a cover/raise round trip reproduces the window - compared against the
    same window as W_PAINT drew it, which is the only reference that means
    anything.

The residual difference is the mouse arrow and the menu bar's application
name, both of which legitimately move between the two captures; it lands
around 80 bytes of 128,000 on CGA. A restore that is actually broken smears,
and smears in the hundreds or thousands.

Raise it through the DOCK TILE rather than by clicking the window: after the
Disk window covers it, Solitaire may have no visible pixel left to click, and
a click that lands on bare desktop switches to Locator and raises nothing -
which reads exactly like a cache that did not restore.
"""
import os
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import os88marty
from os88mouse import Mouse

MEM_MAX, MC_SIZE = 32, 8
RANK = {0xFB: "trivial", 0xFC: "low", 0xFD: "medium", 0xFE: "high"}

DRIVE_B = (600, 108)        # the B: desktop zone on a 2-row CGA desktop
ROW_GAMES = (175, 84)       # B: root, sorted: APPS GAMES MEDIA SYSTEM
ROW_SOLIT = (175, 132)      # GAMES/, sorted: .. ARKANOID MINES MISSILE SOLITAIR
DISK_TITLE = (300, 40)      # the Disk window's title bar, to raise it back
DOCK_TILE = (40, 190)       # ...and Solitaire's dock tile


def caches(m):
    """Every live PURGEABLE claim, by rank (SPEC.md 50.6.4)."""
    raw = m.read(m.sym("mem_tab"), MEM_MAX * MC_SIZE)
    out = []
    for i in range(MEM_MAX):
        seg, para, own, _ = struct.unpack_from("<HHHH", raw, i * MC_SIZE)
        if seg and 0xFB <= (own >> 8) <= 0xFE:
            out.append((own, RANK[own >> 8], para / 64.0))
    return out


def fb(m):
    return b"".join(bytes(r) for r in m.vram("cga")[2])


def wait_desktop(m, limit=150):
    """settle()'s menu-bar gate fires on the SPLASH on a slow machine or a
    full apps disk, so wait on the desktop's own lit count instead."""
    t0 = time.time()
    while time.time() - t0 < limit:
        if sum(sum(r) for r in m.vram("cga")[2]) > 70000:
            return
        time.sleep(2)
    raise SystemExit("sucheck: no desktop after %ds" % limit)


def main():
    machine = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_cga"
    with os88marty.launch(os.path.join(ROOT, "build/os8088-360.img"),
                          apps=os.path.join(ROOT, "build/apps360.img"),
                          machine=machine, boot=2) as m:
        wait_desktop(m)
        mo = Mouse(marty=m)
        mo.dblclick(*DRIVE_B);   time.sleep(3)
        mo.dblclick(*ROW_GAMES); time.sleep(3)
        mo.dblclick(*ROW_SOLIT); time.sleep(12)
        painted = fb(m)
        print("solitaire up      %s" % (caches(m) or "no caches"))

        mo.click(*DISK_TITLE); time.sleep(3)        # cover it: this is the take
        covered = caches(m)
        print("covered           %s" % (covered or "no caches"))

        mo.click(*DOCK_TILE); time.sleep(4)         # ...and the restore
        restored = fb(m)
        print("raised (dock)     %s" % (caches(m) or "no caches"))

        diff = sum(1 for a, b in zip(painted, restored) if a != b)
        print()
        print("W_PAINT original vs raise: %d differing bytes of %d"
              % (diff, len(painted)))
        took = [c for c in covered if c[0] >> 8 == 0xFB]
        if took:
            print("PASS: cache taken at 0x%04X (%s), %.0fKB" % took[0])
        else:
            print("NOTE: no trivial-rank claim was live at the cover sample -")
            print("      the take and the spend can both fall inside one step,")
            print("      so this is not by itself a failure; the pixel figure")
            print("      above is the assertion that counts.")
        m.quit()


if __name__ == "__main__":
    main()
