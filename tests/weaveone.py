#!/usr/bin/env python3
"""ONE Weave app on a 256KB machine, and the second refused (WEAVE-SPEC 1.4).

    make weavedisk && python3 tests/weaveone.py
    python3 tests/weaveone.py --png shots/

WHAT WEAVE-SPEC 1.4 CLAIMS, and this row is what reads it off the glass rather
than off the document: "256KB XT, ~140.5KB heap: exactly ONE Weave app at a
time... the second launch refuses BEFORE ANY I/O". A 256KB XT is the family's
FLOOR (9.12: kern_small at 128KB refuses GFX_BLIT1, WM_TIMER and WM_ONDRAG by
CF=1), so it is the machine the whole memory argument is written about - and
an arithmetic in a document that nothing exercises is an arithmetic that goes
quietly wrong.

WHICH REFUSAL ACTUALLY FIRES, because wave 7 asked the machine and the
document had it in the wrong place. The second launch does not reach WEAVE at
all. A package region is claimed by the KERNEL's loader before the package
runs (SPEC.md 20.1, 21), WEAVE's is 60,320 bytes, and with one instance up
there is not that much left - so `loader_run` answers LD_ENOMEM and the Finder
says `Out of memory` (SPEC.md 22.9's status ladder, and the toast beside it,
SPEC.md 59). WEAVE-SPEC 10.1's sentence - `This app needs <N>KB; the largest
free run is <M>KB.` - is the RUNTIME refusing its own bundle's claims, which
is one step later and needs the package to be resident to say. Both are
"before any I/O" and the kernel's is earlier; 1.4 now says which.

SO WHAT IS ASSERTED IS [ld_status] AND NOT A SENTENCE, and that is the
stronger reading rather than a weaker one: the verdict is a kernel byte
(kernel/loader.inc, LD_OK..LD_ENOMEM), the toast that draws it is a ~3s
transient this harness cannot catch reliably at any polling rate worth having,
and the byte is what the toast is drawn FROM. The three facts together are the
whole claim:

  1. the FIRST bundle opens - so the machine is one that runs Weave apps at
     all, and a failure below is about the second launch rather than about
     256KB being unusable;
  2. the SECOND opens NO window and [ld_status] is LD_ENOMEM;
  3. the first app IS STILL THERE. kernel/loader.inc's own opening promise is
     that "a package that cannot fit fails while running instances stay", and
     a refusal that took the running app down with it would pass 1 and 2.

MartyPC, on `os8088_5150_cga_gla_256k` - a GLaBIOS machine, because a machine
asking for IBM's ROM cannot boot in this tree (tools/martypc/build.sh) and a
gate that only runs where a licensed ROM happens to be is a gate that quietly
does not run. `make xt-weave-256` is the same question on 86Box and is MANUAL
evidence: 86Box has no automation socket (docs/TESTING.md), so nothing here
can rest on it.

THE SECOND OPEN NEEDS THE FIRST APP MOVED OUT OF THE WAY, and that is a fact
about the desktop rather than a trick: WEAVE opens a window that fills the
content area, so the Disk window it was launched from is completely covered
and `open_named` cannot see the row it needs. Dragging the app's title bar
down is one gesture, it is what a person would do, and it happens before the
second launch so it cannot affect the arithmetic.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import weavesmoke                                           # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MACHINE = "os8088_5150_cga_gla_256k"
CARD = "cga"
DISK = "build/weave360.img"

# kernel/loader.inc's own enum. LD_ENOMEM is "the instance table or pool
# exhausted", which on this machine is the heap and not the table.
LD_OK, LD_ENOMEM = 0, 5

FIRST = "SHEET.WAB"     # the biggest instance the demo disk carries: a <grid>
                        # bundle takes the grid claim as well (WEAVE-SPEC 5.6)
SECOND = "FORM.WAB"     # ...and the smallest, so a pass is not "the second one
                        # was simply too big" - if even FORM will not open,
                        # nothing will


def _drive(m, png_dir):
    S = os88sym.linear
    mo = os88mouse.Mouse(marty=m)

    # --- the first app ------------------------------------------------------
    weavesmoke.BUNDLE = FIRST
    before, after = weavesmoke._open_bundle(m, mo, S, MACHINE)
    new = sorted(after - before)
    if not check(len(new) == 1, "%s: %s opens on a 256KB machine"
                 % (MACHINE, FIRST),
                 "WEAVE-SPEC 1.4's first half - ONE Weave app fits, with "
                 "~30KB of slack. If this fails the machine is not the "
                 "family's floor and 9.12 is wrong, which is a bigger finding "
                 "than the one below",
                 got=new, want="exactly one window"):
        return
    app = new[0]
    disk = sorted(w for w in dispcp.win_list(m, S) if w != app)
    if not check(disk, "%s: the Disk window is still open" % MACHINE,
                 "the second launch comes out of it", got=disk, want="a slot"):
        return
    dw = disk[-1]

    # --- get the app off the Disk window ------------------------------------
    ax, ay, aw, ah = dispcp.win_rect(m, S, app)
    mo.drag(ax + aw // 2, ay + 4, ax + aw // 2, 150)
    os88marty.settle(m)
    wx, wy = dispcp.win_rect(m, S, dw)[:2]
    mo.click(wx + 60, wy + 4)                   # ...and raise it
    os88marty.settle(m)

    # --- the second ---------------------------------------------------------
    st = m.read(S("ld_status"), 1)[0]
    check(st == LD_OK, "%s: the first launch's verdict was LD_OK" % MACHINE,
          "the byte the assertion below reads, established as a control "
          "BEFORE the refusal rather than assumed",
          got=st, want=LD_OK)

    live = set(dispcp.win_list(m, S))
    try:
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, SECOND)
    except os88marty.MartyError as e:
        check(False, "%s: the second double-click reached %s"
              % (MACHINE, SECOND),
              "this is the navigation and not the machine - the row cannot "
              "say anything about a launch it never asked for",
              got=str(e)[:200], want="a double-click on the row")
        return
    os88marty.settle(m)

    now = set(dispcp.win_list(m, S))
    check(now == live, "%s: %s opened NO second window"
          % (MACHINE, SECOND),
          "WEAVE-SPEC 1.4: exactly ONE Weave app at a time on a 256KB "
          "machine. A second window here would mean the arithmetic in 1.4 is "
          "wrong in the other direction and the machine holds two",
          got=sorted(now - live) or "none", want="no new window")

    st = m.read(S("ld_status"), 1)[0]
    check(st == LD_ENOMEM,
          "%s: the second launch answered LD_ENOMEM" % MACHINE,
          "the KERNEL's loader refuses it before WEAVE runs at all - a "
          "package region is claimed before the package (SPEC.md 20.1, 21) "
          "and WEAVE's is 60,320 bytes. `Out of memory` is what the Finder "
          "draws from this byte (SPEC.md 22.9) and what the toast says "
          "(SPEC.md 59)",
          got=st, want="%d (LD_ENOMEM)" % LD_ENOMEM)

    check(app in dispcp.win_list(m, S),
          "%s: the app that was already running is still there" % MACHINE,
          "kernel/loader.inc's own opening promise - 'a package that cannot "
          "fit fails while running instances stay'. A refusal that took the "
          "running app down with it would pass both assertions above",
          got=dispcp.win_list(m, S), want="the first app's slot, %d" % app)

    if png_dir:
        vw, vh, rows = m.vram(CARD)
        weavesmoke._shot(png_dir, "%s-one-app" % MACHINE, vw, vh, rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png")
    ap.add_argument("--no-make", action="store_true")
    a = ap.parse_args()
    if not a.no_make:
        import subprocess
        r = subprocess.run(["make", DISK], cwd=ROOT, capture_output=True,
                           text=True)
        if r.returncode:
            print("weaveone: `make %s` failed:\n%s"
                  % (DISK, (r.stderr or r.stdout)[-800:]))
            return 1
    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=MACHINE) as m:
        try:
            _drive(m, a.png)
        except os88marty.MartyError as e:
            check(False, "%s: the session broke off" % MACHINE,
                  "the machine booted, so this is the harness losing its "
                  "grip on it rather than the tree being wrong",
                  got=str(e)[:200], want="a driven session")
    return done("weaveone")


if __name__ == "__main__":
    sys.exit(main())
