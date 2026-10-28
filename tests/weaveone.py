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

AND SINCE NO-PINNING C3 IT IS THE RUNTIME AGAIN (WEAVE-SPEC 1.2.6, 1.4). The
first instance's claims MOVE now, so the compactor packs them and the second
region fits; what cannot be had is the second instance's overlay (~15KB),
op_fetch toasts `Not enough memory`, and the window carries 10.3's sentence
with no bundle, VM or grid behind it. So the second fact below takes EITHER
shape - no window and LD_ENOMEM, or one window in W_ST_ERR with no VM claim -
and prints which; 1.4's promise, one Weave APP at a time, is what both keep.

SO WHAT IS ASSERTED (in the loader's shape) IS [ld_status] AND NOT A SENTENCE, and that is the
stronger reading rather than a weaker one: the verdict is a kernel byte
(kernel/loader.inc, LD_OK..LD_ENOMEM), the toast that draws it is a ~3s
transient this harness cannot catch reliably at any polling rate worth having,
and the byte is what the toast is drawn FROM. The three facts together are the
whole claim:

  1. the FIRST bundle opens - so the machine is one that runs Weave apps at
     all, and a failure below is about the second launch rather than about
     256KB being unusable;
  2. the SECOND opens NO window and [ld_status] is LD_ENOMEM - or opens one
     that refuses in W_ST_ERR holding no bundle (see above);
  3. the first app IS STILL THERE. kernel/loader.inc's own opening promise is
     that "a package that cannot fit fails while running instances stay", and
     a refusal that took the running app down with it would pass 1 and 2.

MartyPC, on `os8088_5150_cga_gla_256k` - a GLaBIOS machine, because a machine
asking for IBM's ROM cannot boot in this tree (tools/martypc/build.sh) and a
gate that only runs where a licensed ROM happens to be is a gate that quietly
does not run. `make xt-weave-256` is the same question on 86Box and is MANUAL
evidence: no row launches 86Box (tools/os88box.py could - docs/86BOX.md),
so nothing here rests on it.

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
import os88fixture                                           # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
import os88geom                                             # noqa: E402
from cmemmove import pkg_syms                               # noqa: E402
import weavesmoke                                           # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MACHINE = "os8088_5150_cga_gla_256k"
CARD = "cga"
DISK = "build/weave360.img"

# kernel/loader.inc's own enum. LD_ENOMEM is "the instance table or pool
# exhausted", which on this machine is the heap and not the table.
LD_OK, LD_ENOMEM = 0, 5
W_ST_DECK, W_ST_RUN, W_ST_ERR = 0, 1, 2     # apps/weave/weave.h

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
        # THE SECOND LAUNCH IS SUPPOSED TO GET NO BUNDLE OPEN (WEAVE-SPEC
        # 1.4) - that is the whole row - and there are two honest shapes of
        # that, so the click confirms nothing and the outcome is read below.
        dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, SECOND,
                          expect=None)
    except os88marty.MartyError as e:
        check(False, "%s: the second double-click reached %s"
              % (MACHINE, SECOND),
              "this is the navigation and not the machine - the row cannot "
              "say anything about a launch it never asked for",
              got=str(e)[:200], want="a double-click on the row")
        return
    os88marty.ui_done(m)
    os88marty.settle(m)
    C = pkg_syms("apps/weave/weave.asm")

    def wword(win, name):
        seg = int.from_bytes(m.read(os88geom.winptr(m, win, S)
                                    + os88geom.W_SEG, 2), "little")
        return int.from_bytes(m.read(seg * 16 + C[name], 2), "little")

    new2 = sorted(set(dispcp.win_list(m, S)) - live)
    st = m.read(S("ld_status"), 1)[0]
    if not new2:
        # SHAPE A, the loader's: the region did not fit, no window at all.
        print("      second launch: the LOADER refused (no window)")
        check(st == LD_ENOMEM,
              "%s: the second launch answered LD_ENOMEM" % MACHINE,
              "the kernel's loader refused the region before WEAVE ran "
              "(SPEC.md 20.1, 21) - `Out of memory` is drawn from this byte",
              got=st, want="%d (LD_ENOMEM)" % LD_ENOMEM)
    else:
        # SHAPE B, the runtime's - and the one a 256KB machine takes since
        # NO-PINNING C3 (WEAVE-SPEC 1.2.6, 1.4): the first instance's claims
        # MOVE now, so the compactor packs them and the second 53KB region
        # fits; it is the second instance's own overlay that then cannot be
        # had, op_fetch toasts why, and its window carries 10.3's sentence.
        # What 1.4 promises is still true: no second BUNDLE is open.
        w2 = new2[0]
        os88marty.until(m, lambda _: wword(w2, "_w_state") != W_ST_DECK,
                        "the second instance's verdict", poll=0.5, guest=60)
        print("      second launch: the RUNTIME refused (its window, "
              "w_state %d)" % wword(w2, "_w_state"))
        check(len(new2) == 1 and wword(w2, "_w_state") == W_ST_ERR
              and wword(w2, "_w_vmseg") == 0,
              "%s: the second instance opened NO bundle" % MACHINE,
              "WEAVE-SPEC 1.4: exactly ONE Weave app at a time on a 256KB "
              "machine. The second region may fit (its neighbour's claims "
              "move, 1.2.6), but its overlay cannot, so it refuses in its "
              "own window and holds no bundle, VM or grid",
              got="%d window(s), w_state %d, w_vmseg %04x"
              % (len(new2), wword(w2, "_w_state"), wword(w2, "_w_vmseg")),
              want="one window, W_ST_ERR, no VM claim")

    check(app in dispcp.win_list(m, S) and wword(app, "_w_state") == W_ST_RUN,
          "%s: the app that was already running is still there" % MACHINE,
          "kernel/loader.inc's own opening promise - 'a package that cannot "
          "fit fails while running instances stay'. A refusal that took the "
          "running app down with it would pass both assertions above",
          got=dispcp.win_list(m, S), want="the first app's slot, %d, "
          "running its bundle" % app)

    if png_dir:
        vw, vh, rows = m.vram(CARD)
        weavesmoke._shot(png_dir, "%s-one-app" % MACHINE, vw, vh, rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--png")
    ap.add_argument("--no-make", action="store_true")
    a = ap.parse_args()
    if not a.no_make:
        # THE SAME `make`, AND IT DOES NOTHING once the runner has built the
        # artefact: Row(wants=...) declares it and os88test's prebuild builds
        # it before any row starts. That is what lets this row drop
        # builds=True and share the emulator lane.
        os88fixture.need(DISK)
    with weavesmoke.booted("build/os8088-360.img", apps=DISK,
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
