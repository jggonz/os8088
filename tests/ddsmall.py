#!/usr/bin/env python3
"""DOT DELIRIUM runs on `kern_small`'s 128KB floor machine (SPEC.md 24.5.5).

    make small smallapps && python3 tests/ddsmall.py [machine]

**This row exists because the package was taken OFF the small floppies and put
BACK, and only one of those two decisions was ever measured.** It came off on
the ground that *"kern_small carries no `gfx_blit1` body at all and this
renderer is that one call"* — true when it was written, and made false the next
cycle by §5.4.2.5.1, which gave both builds the body. Nothing re-read the
omission when its reason was withdrawn. So what goes on the disk now is a
MEASUREMENT and this is where it lives: `tanksmall`'s shape (§85.3.5.1) for
the package §24.5 put back rather than the one it substituted.

It is deliberately NOT a second copy of `tests/dotdel.py`. That row asks
whether the game is CORRECT — the attract screen, the blink, the tile against
§93.3's table, the frame rate against the tick — on kernels and adapters where
it has always run. This one asks the only question the disk list rests on:
**on the floor machine, does it get a window, a board, a claim and a game?**
Five things, each of which is a different way for a 128KB machine to fail:

  A  THE WINDOW OPENS.  `ld_status` = 0 with no window is the shape a package
     refusing itself takes, and it is what a missing API slot would look like.
  B  THE LAYOUT ACCEPTS THE SURFACE.  `dd_ok` = 0 draws `Window too small.`
     and nothing else — a window that is up and a game that is not.
  C  THE BOARD PICTURE IS CLAIMED.  `dd_bdseg` = 0 is `dd_fit_claim` refused
     by the arena, which is the failure a 128KB machine is actually at risk
     of: the picture is sized from the SURFACE (§93.3), so it is 4KB on a
     windowed CGA and 19KB on a fullscreen Hercules.
  D  SMILES EATS.  The dot count falls and the score rises — the worker
     spawned on a `OS88_STACK_256` slice this kernel had to find, and the
     game logic is running on it.
  E  FULLSCREEN RE-CUTS IT BIGGER AND COMES BACK.  `OSAPI_MEM_REGROW` on a
     nearly-full arena is the one call here that can fail on the floor
     machine and not on a 640KB one, and Escape has to put the board back.

**THE ARENA READING IS THE POINT OF THE LAST LEG.**  A pass with 0 bytes free
is a pass that the next package to grow turns into a failure nobody predicted,
so the row PRINTS the free arena at the game's widest and fails only on a
refusal — the number is for whoever reads the log, in `kernsize`'s spirit.
Measured when this was written: 52.5KB of arena, 46.0KB claimed at the widest,
**6.5KB free**.

BREAK IT ON PURPOSE: put `$(BUILD)/dotdel.o88` back in `$(SMALLOMIT_GAMES)`
and leg A fails naming what the folder does hold. Poke `dd_ok` to 0 after the
layout and leg B goes red with `Window too small.` on the glass. Force
`dd_fit_claim`'s `.no` and leg C reads `dd_bdseg = 0`.

**…AND DELETE THE TREE'S FLOPPY WHEN YOU DO.** `os88build.tree` keys its
directory on the GOALS, so a Makefile-only edit re-runs `make` into a tree
that already has an up-to-date `smallapps360.img` — no shipped file depends on
the Makefile, so nothing rebuilds and the row passes against yesterday's disk.
That is a FALSE GREEN of exactly the shape docs/WRITING-TESTS.md 1 is about,
and it caught this row's own author: the first break-on-purpose reported `ok`
and read as proof the guard worked. `rm <tree>/smallapps360.img` first, or the
experiment is not the experiment.

THE MACHINE IS `os8088_5150_cga_128k` and there is no Hercules twin of it —
every other 1bpp profile here is 640KB — so the Hercules arm runs at 640KB and
answers the ADAPTER question while the CGA arm answers the MEMORY one. That
split is deliberate and is why two arms are cheaper than one: RAM was ruled
out first (the two behaved identically at 128KB and 640KB), so neither arm has
to carry both variables.
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))

# **BEFORE os88geom IMPORTS.** That module picks its arm off the defines at
# import time, and a kern_small guest decoded at kern_big's stride returns
# PLAUSIBLE NUMBERS rather than an error - `wm_wins` is 28 bytes x 6 here
# against 34 x 12 there, so every window reads as empty and the failure looks
# like "the package put up no window" (docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md 9.2
# found the same trap in every kern_small script in the tree). It cost this
# row's own investigation three runs and a wrong conclusion.
os.environ["OS88_DEFINES"] = "KERN_SMALL"
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)

import os88build                                             # noqa: E402
import os88sym                                               # noqa: E402
import os88geom as G                                         # noqa: E402

os88build.use_build("build/smallk")
os.environ["OS88_BUILD"] = os.path.join(ROOT, "build", "smallk")
DEFS = ("KERN_SMALL",)
os88sym.default_defines(*DEFS)

import os88marty                                             # noqa: E402
import os88ui                                                # noqa: E402
import dotdel as DD                                          # noqa: E402

PKG = "B:/GAMES/DOTDEL.O88"

# (tag, machine, what the arm is for). CGA is the FLOOR MACHINE and the only
# 128KB profile in the tree; Hercules is the bigger board and so the deepest
# claim this kernel can be asked for.
ARMS = (("cga",  "os8088_5150_cga_128k",  "the 128KB floor machine"),
        ("herc", "os8088_5150_herc_gla",  "the deepest 1bpp board"))

fails = []


def check(ok, name, note=""):
    print("  [%s] %s %s" % ("PASS" if ok else "FAIL", name, note))
    if not ok:
        fails.append(name)


def arena(m):
    """(arena bytes, claimed bytes) off mem_tab - SPEC.md 50's own record.

    **ONLY ROWS INSIDE THE ARENA ARE SUMMED.** `mem_tab` is the record of every
    claim the kernel is holding, and the arena is `int 12h` RAM above
    `HEAP_SEG`; summing the table flat and subtracting it from that is an
    apples-to-pears figure that can go NEGATIVE, which is what a first version
    of this printed (62.0 KB "claimed" against a 52.5 KB arena). A number a
    reader cannot sanity-check is worse than no number, so the base is tested
    rather than assumed and `free` is reported as unknown if it still comes out
    below zero.
    """
    eq = os88sym.equates(DEFS)
    MC, base = G.MC_SIZE, eq["HEAP_SEG"] * 16
    ram = struct.unpack("<H", bytes(m.read(0x413, 2)))[0] * 1024
    tab = bytes(m.read(m.sym("mem_tab", DEFS), eq["MEM_MAX"] * MC))
    used = 0
    for i in range(eq["MEM_MAX"]):
        r = tab[i * MC:(i + 1) * MC]
        if not struct.unpack_from("<H", r, G.MC_OWN)[0]:
            continue
        if struct.unpack_from("<H", r, G.MC_SEG)[0] * 16 < base:
            continue                    # not the heap's - not this sum's
        used += struct.unpack_from("<H", r, G.MC_PARA)[0] * 16
    return ram - base, used


def wait_for(m, cond, guest, poll=0.25):
    """Run until `cond`, bounded by `guest` seconds of the GUEST's clock.

    The budget is the machine's own time so a loaded box cannot shorten it
    (docs/plans/SOAK-PARALLEL.md 1); the CONDITION is what actually ends it,
    so a fast machine does not sit out the budget.
    """
    import time as _t
    end = _t.time() + guest * 20            # a host ceiling well clear of the
    while _t.time() < end:                  # guest budget, so a STOPPED guest
        if cond():                          # fails here instead of hanging
            return True
        os88marty.guest_sleep(m, poll)
    return bool(cond())


def run(tag, machine, why, image, apps):
    print("== %s: %s (%s) ==" % (tag, machine, why))
    names = DD.bss()
    with os88ui.boot(image, apps=apps, machine=machine) as ui:
        m = ui.m

        # --- A: the window opens -------------------------------------------
        try:
            ui.path(PKG)
        except Exception as e:
            check(False, "%s: the window opens" % tag,
                  "- %s" % str(e).splitlines()[0][:120])
            return
        p = DD.Probe(ui, names)
        # the layout and the board claim going still, where this was a blind
        # three guest seconds
        layout = lambda: tuple(p.w(n) for n in (
            "dd_ok", "dd_bdseg", "dd_tw", "dd_th", "dd_cw", "dd_ch"))
        os88marty.quiesce(m, layout, guest=0.5,
                          what="Dot Delirium's layout")
        check(True, "%s: the window opens" % tag,
              "(package segment 0x%04X)" % p.seg)

        # --- B and C: a board, and a picture to draw it on -----------------
        check(p.b("dd_ok") == 1, "%s: the layout accepts the surface" % tag,
              "(content %dx%d)" % (p.w("dd_cw"), p.w("dd_ch")))
        check(p.w("dd_bdseg") != 0, "%s: the board picture is claimed" % tag,
              "(tile %dx%d, board %dx%d, %d KB)"
              % (p.w("dd_tw"), p.w("dd_th"), p.w("dd_mw"), p.w("dd_mh"),
                 p.w("dd_bdkb")))

        # --- D: Smiles eats ------------------------------------------------
        # ON THE GAME'S OWN CLOCK, which is tests/dotdel.py's idiom and not a
        # sleep of any kind: `dd_anim` counts the animation ticks the game has
        # actually taken, so this waits for PLAY rather than for time. A host
        # sleep hands a loaded lane a third less of it and a guest sleep is
        # still the wrong quantity - a game spends its first seconds on READY!
        # with nothing to eat, so "12 guest seconds" is a budget that passes or
        # fails on where that pause happened to fall (measured: 237 -> 237).
        f0 = p.w("dd_frames")
        m.key("Enter")
        if not wait_for(m, lambda: p.b("dd_state") != 0, 30):
            check(False, "%s: Enter starts a game" % tag,
                  "(dd_state stayed 0)")
        d0, s0 = p.w("dd_ndots"), p.w("dd_score")
        t0 = p.w("dd_anim")
        wait_for(m, lambda: (p.w("dd_anim") - t0) & 0xFFFF >= 200, 90)
        d1, s1 = p.w("dd_ndots"), p.w("dd_score")
        check(d1 < d0 or s1 > s0, "%s: Smiles eats" % tag,
              "(dots %d -> %d, score %d -> %d over %d animation ticks)"
              % (d0, d1, s0, s1, (p.w("dd_anim") - t0) & 0xFFFF))
        check(p.w("dd_frames") != f0, "%s: the worker is rendering" % tag,
              "(dd_frames %d -> %d on an OS88_STACK_256 slice)"
              % (f0, p.w("dd_frames")))

        # --- E: fullscreen re-cuts the board, and Escape puts it back ------
        tile0 = (p.w("dd_tw"), p.w("dd_th"))
        m.key("KeyF")
        # the re-cut is a claim, a regrow and a whole repaint of a bigger
        # board: the tile moving and dd_full clearing is that being done
        # (tests/dotdel.py's leg E), inside the 10 guest seconds it was given
        wait_for(m, lambda: (p.w("dd_tw"), p.w("dd_th")) != tile0
                 and p.b("dd_full") == 0, 10)
        os88marty.quiesce(m, layout, guest=0.5, what="the re-cut")
        big = (p.w("dd_tw"), p.w("dd_th"))
        check(p.b("dd_ok") == 1 and p.w("dd_bdseg") != 0 and big > tile0,
              "%s: fullscreen re-cuts the board BIGGER" % tag,
              "(tile %dx%d -> %dx%d, board %dx%d, %d KB)"
              % (tile0[0], tile0[1], big[0], big[1],
                 p.w("dd_mw"), p.w("dd_mh"), p.w("dd_bdkb")))
        a, used = arena(m)
        print("       arena %.1f KB, claimed at the widest %.1f KB, free %s"
              % (a / 1024.0, used / 1024.0,
                 "%.1f KB" % ((a - used) / 1024.0) if used <= a
                 else "UNKNOWN - the claim sum exceeds the arena, so this "
                      "reading is not to be quoted"))
        m.key("Escape")
        wait_for(m, lambda: (p.w("dd_tw"), p.w("dd_th")) == tile0
                 and p.b("dd_full") == 0, 10)
        os88marty.quiesce(m, layout, guest=0.5, what="the re-cut")
        check(p.b("dd_ok") == 1 and (p.w("dd_tw"), p.w("dd_th")) == tile0,
              "%s: Escape puts the windowed board back" % tag,
              "(tile %dx%d)" % (p.w("dd_tw"), p.w("dd_th")))


def main(argv):
    only = argv[0] if argv else None
    t = os88build.tree(targets=("small", "smallapps"))
    image, apps = t.img("small360.img"), t.img("smallapps360.img")
    for tag, machine, why in ARMS:
        if only and only not in (tag, machine):
            continue
        run(tag, machine, why, image, apps)
    if fails:
        print("\nddsmall: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nddsmall: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
