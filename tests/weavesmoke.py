#!/usr/bin/env python3
"""Does WEAVE open FORM.WAB and draw a window, on both 1bpp adapters?

    make weavedisk && python3 tests/weavesmoke.py
    python3 tests/weavesmoke.py --machine os8088_5150_herc_gla
    python3 tests/weavesmoke.py --no-make      # use build/weave360.img as-is
    python3 tests/weavesmoke.py --png shots/   # ...and LOOK at what it drew

THE WEAVE FAMILY'S ONE FULL-TIER ROW, FOREVER (WEAVE-SPEC 12.3). It costs an
emulator boot per adapter, and the full tier is a handful of such rows for the
whole repo; everything narrower belongs in soak, where
`python3 tools/os88test.py soak -k 'weave*'` is one command away.

It is deliberately NOT a screenshot comparison, for tests/bootsmoke.py's
reason: a golden image fails on every legitimate pixel change - a font tweak,
a moved control, a component that gains a border - and a gate that cries wolf
is turned off. What it asserts is the STRUCTURE a Weave window must have on
any build:

  1. a PACKAGE window opened that was not there before, and the package
     behind it calls itself WEAVE - read out of its own header (SPEC.md
     20.2), because "some window appeared" is also what a Disk window, an
     alert or a stale Note Pad looks like,
  2. it opened at SPEC.md 11.95's standard rect, and the content box that
     rect yields is CW x CH cells - the grid the flow walk runs on
     (WEAVE-SPEC 7.1.1, 7.1.2), with the numbers coming from
     `weavesim --render` because it is the oracle every differential in the
     family diffs against (WEAVE-SPEC 12.1) and because 7.1.1 forbids
     hard-coding them. This is a fact about the WINDOW rather than about what
     was painted in it, so it survives every font, inset and component change
     and still catches a grid that is one row out,
  3. the title strip is drawn - a lit block above the black separator row,
     which is bootsmoke's menu-bar-and-rule idiom applied to a frame,
  4. the frame's edges are where the window record says they are - and there
     are THREE of them, not four: a snapped window spanning the screen draws
     no left border, because a border separates a window from what is beside
     it and at x = 0 there is nothing beside it (SPEC.md 11.95.2). WEAVE's
     standard rect is exactly that window. Which shape applies is computed
     with the kernel's own predicate rather than assumed, and each edge is
     paired with something NOT dark just inside it - the drop shadow at
     (+1,+1) is dark too, so a dark bottom line alone cannot pin W_H, and at
     column 0 the pairing is what catches a suppressed border drawn anyway,
  5. the content rect holds ink and is neither empty nor solid,
  6. that ink lands in more than one band, which separates a laid-out flow
     (WEAVE-SPEC 7) from a single line of refusal text - a refusal is ink
     too, and passes 5,

...all read from ONE framebuffer capture, because two reads can answer about a
state that never existed: MartyPC runs the guest several times real time, so a
round trip is most of a desktop paint on a 4.77MHz machine. The window record
is read on either side of that capture and must not have moved, which is how
the rect and the pixels are known to describe the same machine. Then:

  7. the guest is still EXECUTING. A task frozen holding the gfx lock draws a
     perfect window and never draws another (SPEC.md 59.7), so stillness
     alone cannot tell a healthy machine from a dead one.

WHY IT BUILDS ITS OWN DISK. WEAVE is a C package, so `all` does not build it
(SmallerC is not in this tree) and `make test-full` names only the shipped
images - the disk this boots would otherwise be whatever an earlier session
left in build/, and a stale disk runs an earlier build's package while every
assertion below reports on this one. `full` is the tier that is allowed to
build (tools/os88test.py), and `make` is what knows whether anything is stale.

WHY THE WHOLE DISK IS ONE FOLDER. WEAVE.OVL and the .WAB bundles ride the root
beside WEAVE.O88: a double-click on a bundle leaves the launched instance's
current directory on the DOCUMENT's (SPEC.md 54.9), and the overlay is
resolved in that directory (SPEC.md 73.14) - so a bundle in a folder of its
own opens a program whose every overlay path then refuses, politely and
inexplicably.

BOTH 1bpp ADAPTERS out of one body, because they are the target class and they
differ in kind rather than in depth - 640x200 against 720x348, two different
banked layouts (SPEC.md 39.3) - and because the GLaBIOS twins are the only
pair that boots in a tree with no IBM ROM under tools/martypc/roms/, which is
every checkout of this one (tools/martypc/build.sh says why and continues).

WHAT IS RETRIED, AND WHAT IS NOT. Getting to the bundle is a double-click, and
os88mouse refuses one whose two presses straddled the kernel's 9-tick window,
saying so as "a host that cannot keep up" - a statement about the box running
the test rather than about the tree, seen here at 9 ticks against a window of
9 on a machine with three concurrent builds on it. So the NAVIGATION is
retried up to three times and every retry is printed. Nothing else is: the
retry catches MartyError from the mouse alone, so a failed `check` never
reaches it, and a WEAVE that stops launching, stops drawing or lays out on the
wrong grid fails on the first attempt. It can hide a slow host; it cannot hide
a regression.

BOTH WINDOWS ARE WAITED FOR WITH `os88marty.until`, NOT WITH A SETTLE, and
that is the same defect found twice. A settle is two identical frames a second
apart; opening a Disk window READS A DIRECTORY and launching a package READS
THE PACKAGE, and neither draws anything while it runs - so the screen is more
still while the machine is busy than when it has finished, which is `until`'s
own documented case. Settling instead cost this gate one run reported as "the
bundle did not launch" on a machine that was loading it, and two runs killed
outright by an IndexError on an empty window list. The limit on each is 20s,
and 20s is not "how long may a load take": it is how long a LOST DOUBLE-CLICK
costs before the retry gets its turn, because a click the guest never saw
waits out the whole limit every time. At 60s one lost click turned a 21s
adapter into 103s and put the row past its declared budget.
"""
import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "unit"))
import os88geom                                             # noqa: E402
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402
import os88sym                                              # noqa: E402
import dispcp                                               # noqa: E402
from harness import check, done                             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (machine, card, width, height) - the GLaBIOS twins, so this runs in a tree
# with no IBM ROM. `card` is the string m.vram() takes AND the spelling
# `weavesim --adapter` takes; they agree today and _oracle_cells is the one
# place that would have to learn otherwise.
MACHINES = [
    ("os8088_5150_cga_gla",  "cga",  640, 200),
    ("os8088_5150_herc_gla", "herc", 720, 348),
]

# The 360KB geometry, because the 5150 machines above have 360KB drives. It is
# also the tightest of the three, which is the one worth booting.
DISK = "build/weave360.img"
BUNDLE = "FORM.WAB"             # the file this double-clicks in the B: window
PKG = "WEAVE"                   # CC_PKG_NAME, as os88pkg.py stamps it at +16
RENDER_WAB = "build/FORM.WAB"   # ...and the one weavesim renders for its cell
                                # grid. The same file today and deliberately a
                                # separate name: the grid falls out of the
                                # standard rect, so it is a property of the
                                # ADAPTER and any bundle answers it.


def _row_lit(rows, y, x0, x1):
    """Lit pixels in rows[y][x0:x1].

    `Marty.vram()` hands back ONE BYTE PER PIXEL, already de-banked - so a row
    is `w` entries and not `w / 8` packed ones. Slicing it as if it were
    packed reads the first eighth of the row and reports it as the whole:
    measured once on a perfectly good desktop, where an all-white menu bar
    came back as "80 of 640 lit".
    """
    return sum(1 for v in rows[y][x0:x1] if v)


def _col_lit(rows, x, y0, y1):
    return sum(1 for yy in range(y0, y1) if rows[yy][x])


def _box_lit(rows, x0, y0, x1, y1):
    return sum(1 for yy in range(y0, y1) for v in rows[yy][x0:x1] if v)


def _bands(rows, x0, y0, x1, y1, ink_is_dark):
    """How many separated horizontal bands of ink the box holds.

    A band is a run of rows with at least one ink pixel, ended by a row with
    none. Polarity comes from the caller because the kernel white-fills a
    content rect before W_PAINT (kernel/wm.inc) but an app that took WF_OWNBG
    may have filled it the other way - and a band count that quietly measured
    the BACKGROUND would be a number that always passed.
    """
    n, run = 0, False
    for yy in range(y0, y1):
        row = rows[yy][x0:x1]
        inked = any(not v for v in row) if ink_is_dark else any(row)
        if inked and not run:
            n += 1
        run = inked
    return n


def _shot(png_dir, machine, vw, vh, rows, rect=None):
    """Write THE CAPTURE THE GATE ALREADY TOOK out as a PNG, and the window
    crop beside it. Answers the paths written.

    CLAUDE.md: a drawing change is not done until it has been LOOKED AT on a
    1bpp adapter (SPEC.md 39.4), and the three defects that matter most here -
    a visible redraw, a double-draw flash, input overrun - do not show in a
    transcript of passing checks. Thirty-eight green assertions are not a
    picture.

    NO SECOND CAPTURE AND NO SECOND BOOT. It is handed the same `rows` every
    assertion is reading, so the file is the frame the gate judged rather than
    a later one - which is the whole one-capture rule, and a --png that took
    its own read would quietly break it for the one run a person actually
    looks at. It costs nothing when the flag is absent.
    """
    out = []
    os.makedirs(png_dir, exist_ok=True)
    full = os.path.join(png_dir, "weave-%s.png" % machine)
    os88marty.write_png(full, vw, vh, rows)
    out.append(full)
    if rect:
        x, y, w, h = rect
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(vw, x + w), min(vh, y + h)   # clamped, so a window that
        if x1 > x0 and y1 > y0:                   # ran off the screen is still
            crop = [r[x0:x1] for r in rows[y0:y1]]   # something to look at
            win = os.path.join(png_dir, "weave-%s-window.png" % machine)
            os88marty.write_png(win, x1 - x0, y1 - y0, crop)
            out.append(win)
    return out


def _pkg_name(m, seg):
    """The package's own name, out of its header at seg:0x10 (SPEC.md 20.2).

    16 bytes, NUL-padded, which is what tools/os88pkg.py validates and stamps.
    Reading it is the difference between "a window opened" and "WEAVE opened a
    window": a Disk window, an alert and any other package all satisfy the
    first.
    """
    raw = m.read((seg << 4) + 16, 16)
    return raw.split(b"\0", 1)[0].decode("latin-1")


def _u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def _win(m, S, slot):
    """(x, y, w, h, seg, flags) of one slot, from ONE read of its record."""
    r = m.read(S("wm_wins") + slot * os88geom.WIN_SIZE, os88geom.WIN_SIZE)
    return (_u16(r, os88geom.W_X), _u16(r, os88geom.W_Y),
            _u16(r, os88geom.W_W), _u16(r, os88geom.W_H),
            _u16(r, os88geom.W_SEG), _u16(r, os88geom.W_FLAGS))


def _flush(x, w, flags, vidw):
    """Are this window's SIDE BORDERS suppressed? (SPEC.md 11.95.2/11.95.3)

    `wm_flush` is `wm_snap_want` AND `W_X == 0` AND `W_X + W_W >= [vid_w]`,
    and `wm_snap_want` is neither WF_NOSNAP nor WF_FULL (kernel/wm.inc). It is
    DERIVED, NEVER TRACKED - there is no flag to read, so the only honest way
    to ask is to ask the same question the kernel asks.

    WEAVE opens at SPEC.md 11.95's standard rect (WEAVE-SPEC 7.1.1), so for
    this gate the answer is always yes - which is exactly why it must be
    computed rather than assumed. A content rect derived as `W_X + 1` and
    `W_W - 2` is right for every window in this tree EXCEPT the one under
    test, and it fails by reading the frame's own column as content: off by
    one pixel, in the direction that still produces plausible numbers.
    """
    if flags & (os88geom.WF_NOSNAP | os88geom.WF_FULL):
        return False
    return x == 0 and x + w >= vidw


def _oracle_cells(adapter):
    """(CW, CH) for this adapter, from tools/weavesim.py (WEAVE-SPEC 12.1).

    THE ORACLE ANSWERS, NOT THIS FILE. WEAVE-SPEC 7.1.1 ends "Nothing else in
    this document may hard-code 80, 90, 17, 35 or 52", and a test that mirrors
    a constant is a constant that goes stale - so the numbers come from the
    reference implementation every differential in the family already diffs
    against. The formula is `CW = floor([vid_w]/8)`,
    `CH = floor(([vid_h]-64)/8)`; it is here as documentation and is computed
    nowhere in this file. It was `([vid_w]-1)/8` while only SPEC.md 11.95.2's
    LEFT border had gone; 11.95.3 took the right one and `_flush` below is
    where that is decided.

    The grid is a property of the ADAPTER rather than of the bundle - it falls
    out of the standard rect - so which bundle is rendered does not matter.
    """
    out = subprocess.run(["python3", "tools/weavesim.py", "--render",
                          RENDER_WAB, "--adapter", adapter],
                         cwd=ROOT, capture_output=True, text=True)
    if out.returncode:
        raise RuntimeError("weavesim --render %s: %s"
                           % (adapter, (out.stderr or out.stdout)[-200:]))
    mm = re.search(r"content (\d+)x(\d+) cells", out.stdout)
    if not mm:
        raise RuntimeError("weavesim --render printed no cell grid: %r"
                           % out.stdout.splitlines()[:1])
    return int(mm.group(1)), int(mm.group(2))


def smoke(machine, card, want_w, want_h, png_dir=None):
    t0 = time.time()
    S = os88sym.linear
    try:
        _smoke(machine, card, want_w, want_h, S, t0, png_dir)
    except os88marty.MartyError as e:
        # ONLY `launch`'s own boot gate reaches here - _smoke catches every
        # LATER MartyError itself, and that split is not tidiness. MartyError
        # is one type over many causes: a desktop that never appeared, a mouse
        # that would not reach a coordinate, a double-click the guest saw as
        # two first clicks. Reported as one, a lost double-click is announced
        # as "never reached a desktop" on a machine that booted perfectly -
        # the same true-failure-false-cause the symbol map already cost us
        # below. `launch` closes the emulator on every one of its own failure
        # paths, so nothing leaks by the `with` being inside _smoke.
        check(False, "%s: never reached a desktop" % machine,
              "the machine did not finish booting, so nothing else here would "
              "mean what it says", got=str(e).split(" - ")[0], want="a desktop")
    except (OSError, EOFError) as e:
        # THE EMULATOR WENT AWAY MID-SESSION. This is not a MartyError - the
        # socket to a process that no longer exists raises BrokenPipeError,
        # which is an OSError - so without this it escapes as a traceback, and
        # a traceback in a permanent gate tells its reader nothing about which
        # of the two causes it was.
        #
        # THE SECOND CAUSE IS THE ONE NOBODY GUESSES: `os88marty.launch` kills
        # every running martypc_headless by PID before it starts its own, on
        # purpose - a survivor holding 127.0.0.1:9001 means the client
        # silently drives the STALE machine. So two harnesses running in one
        # tree kill each other's emulators, and the one that loses sees
        # exactly this. It is why every emulator row here is serial=True, and
        # the suite can only enforce that WITHIN one runner: a second person
        # or agent driving MartyPC in the same checkout is outside its reach.
        check(False, "%s: the emulator went away mid-session" % machine,
              "either martypc_headless died, or a SECOND one was started "
              "against this tree and killed this one - launch() sweeps "
              "survivors by PID, so two harnesses in one checkout take turns "
              "killing each other. `ps -Ao pid=,args= | grep martypc` says "
              "which", got=type(e).__name__ + ": " + str(e)[:120],
              want="a live emulator")
    except RuntimeError as e:
        # dispcp raises this for "that file is not in this folder" and "the
        # list would not scroll" - both of which mean the DISK is not what
        # this expects, and both of which are the gate's answer rather than a
        # crash in it.
        #
        # THE SYMBOL MAP IS RESOLVED IN main() FOR THIS REASON. os88sym raises
        # RuntimeError too, and a stale build/kernel.bin caught here reported
        # itself as "the bundle was not on the disk" - a true failure with a
        # false cause, which is worse than a traceback. Asking for one address
        # before any emulator starts turns that into its own sentence.
        check(False, "%s: could not reach %s" % (machine, BUNDLE),
              "the bundle was not on the disk, or the Disk window would not "
              "navigate to it", got=str(e)[:160], want="a double-click on "
              + BUNDLE)


def _smoke(machine, card, want_w, want_h, S, t0, png_dir=None):
    # The launch is OUTSIDE the try below on purpose: a MartyError from here is
    # the boot gate and smoke() names it as such, while one from anywhere after
    # it is the session breaking off on a healthy machine (see smoke()).
    with os88marty.launch("build/os8088-360.img", apps=DISK,
                          machine=machine) as m:
        try:
            _drive(machine, card, want_w, want_h, S, t0, png_dir, m)
        except os88marty.MartyError as e:
            check(False, "%s: the scripted session broke off" % machine,
                  "the machine booted, so this is the HARNESS losing its grip "
                  "on it rather than the tree being wrong - a mouse that would "
                  "not reach a coordinate, or two presses the guest saw as two "
                  "first clicks because the host was busy between them",
                  got=str(e)[:200], want="a driven session")


def _open_bundle(m, mo, S, machine):
    """Open drive B's Disk window and double-click BUNDLE in it.

    Answers (the window slots before the launch, the slots after).

    RETRIED TWICE, AND EVERY RETRY IS PRINTED. os88mouse refuses a double-click
    whose two presses straddled the kernel's 9-tick window and says exactly
    why: "a mount, a package load, or a host that cannot keep up". That is a
    statement about the BOX THIS IS RUNNING ON, not about the tree - measured
    twice inside `os88test full` on a machine with three concurrent builds on
    it, at 9 ticks against a window of 9, and never once standalone. A
    permanent gate that fails for the load on the machine running it is a gate
    that gets turned off, which is the same argument as the no-goldens rule.

    Printed, because a silent retry hides a real slowdown: the day this needs
    its second attempt every run, somebody should see that in the log. Three
    attempts costs at most ~10s of navigation on a bad day and no extra boot.

    WHAT THE RETRY CAN AND CANNOT MASK, which is the whole reason it is
    allowed to exist: it catches MartyError from the MOUSE and nothing else.
    A failed assertion is not retried, so if WEAVE stops launching, stops
    drawing, or lays out on the wrong grid, `check` fails on the first attempt
    and this loop never runs. It can hide a slow host. It cannot hide a
    regression.

    Both steps are idempotent, which is what makes the retry safe. A
    double-click on drive B's zone RAISES a Disk window that is already open,
    and `open_named` re-reads the listing and re-scrolls before it clicks - so
    an attempt that ended with the row merely SELECTED (which is what two
    first clicks leave behind) is recovered by doing the same thing again.
    """
    last = None
    for attempt in (1, 2, 3):
        try:
            # WAIT FOR THE DISK WINDOW THE SAME WAY, and for the same reason
            # as the bundle's below. `open_drive`'s settle is two identical
            # frames a second apart, and opening a Disk window READS THE
            # DIRECTORY - int 13h, during which nothing is drawn - so a settle
            # can return before the window exists. `win_list(...)[-1]` was
            # then an IndexError, which is not a MartyError, so it escaped
            # both the retry and every handler and killed the run with a
            # traceback. Measured: 2 of 5 runs on an otherwise quiet box.
            # Waiting on the window turns it into a bounded wait that the
            # retry above can actually catch.
            desk = set(dispcp.win_list(m, S))
            wx, wy = dispcp.open_drive(m, mo, S, os88marty.settle, "B")
            os88marty.until(m, lambda mm: set(dispcp.win_list(mm, S)) - desk,
                            "drive B's Disk window to open", limit=20.0)
            disk = sorted(set(dispcp.win_list(m, S)) - desk)[-1]
            wx, wy = dispcp.win_rect(m, S, disk)[:2]
            # BEFORE is taken with the Disk window already open, not at the
            # desktop: taken earlier it counts the Disk window's own arrival
            # as the bundle's, and the gate then passes on a machine where
            # nothing launched at all.
            before = set(dispcp.win_list(m, S))
            dispcp.open_named(m, mo, S, os88marty.settle, wx, wy, BUNDLE)
            # WAIT ON THE WINDOW, NOT ON THE PICTURE. `open_named`'s settle is
            # two identical frames a second apart, and a package LOAD draws
            # nothing while it runs - so on a busy host the settle sees
            # perfect stillness partway through the load and returns, and the
            # window list read straight after it is empty. That is
            # os88marty.until's own documented case ("the screen is MORE still
            # while it is busy"), and it cost this gate a run reported as "the
            # bundle did not launch" on a machine that was loading it. The
            # settle is still wanted afterwards, because the assertions below
            # are aimed at pixels.
            # 20s, not 60. This limit is not "how long may a package take" -
            # it is how long a LOST double-click costs before the retry gets
            # its turn, because a click the guest never saw makes this wait
            # out its whole limit every time. Measured: at 60s one lost click
            # turned a 21s adapter into 103s and put the row past its declared
            # 65. A 27KB package load is a handful of int 13h calls and has
            # never taken more than a second here, so 20 is ~20x margin on the
            # thing being waited for and 3x cheaper on the thing that goes
            # wrong. Worst case is bounded: three attempts, 60s of waiting.
            os88marty.until(m, lambda mm: set(dispcp.win_list(mm, S)) - before,
                            "%s's window to open" % BUNDLE, limit=20.0)
            os88marty.settle(m)
            return before, set(dispcp.win_list(m, S))
        except os88marty.MartyError as e:
            last = e
            if attempt == 3:
                raise
            print("      %s: navigation attempt %d lost the guest - %s"
                  % (machine, attempt, str(e).split(" - ")[0]))
    raise last                                  # unreachable; the loop raises


def _drive(machine, card, want_w, want_h, S, t0, png_dir, m):
    mo = os88mouse.Mouse(marty=m)        # ONE connection, shared - a
                                         # second client to the debug
                                         # server HANGS rather than
                                         # erroring
    before, after = _open_bundle(m, mo, S, machine)

    # 1 - a window opened, and it belongs to WEAVE.
    new = sorted(after - before)
    if not check(len(new) == 1, "%s: one window opened for %s"
                 % (machine, BUNDLE),
                 "the bundle did not launch, so nothing below would mean "
                 "what it says", got=sorted(new), want="exactly one slot"):
        return
    x, y, w, h, seg, flags = _win(m, S, new[0])
    if not check(seg != 0, "%s: it is a PACKAGE window" % machine,
                 "W_SEG is 0 for a kernel-owned window - a Disk window, "
                 "an alert - so the bundle opened something that is not a "
                 "program", got=seg, want="non-zero"):
        return
    name = _pkg_name(m, seg)
    if not check(name == PKG, "%s: the package is %s" % (machine, PKG),
                 "the .WAB association launched some OTHER program, which "
                 "draws a perfectly good window and proves nothing about "
                 "Weave", got=name, want=PKG):
        return

    # ONE CAPTURE. Everything from here to assertion 6 is about THIS
    # frame; the record is re-read afterwards and must not have moved.
    vw, vh, rows = m.vram(card)
    check((vw, vh) == (want_w, want_h), "%s: geometry" % machine,
          "the adapter came up in a mode nobody expects",
          got=(vw, vh), want=(want_w, want_h))
    if len(rows) < vh:
        check(False, "%s: framebuffer is short" % machine, got=len(rows))
        return
    # --png, if asked: the SAME bytes, written before any assertion can
    # end this run early - a window that opened in the wrong place is
    # precisely the one worth looking at.
    if png_dir:
        for f in _shot(png_dir, machine, vw, vh, rows, (x, y, w, h)):
            print("      wrote %s" % f)
    if not check(0 <= x and 0 <= y and x + w <= vw and y + h <= vh,
                 "%s: the window is wholly on screen" % machine,
                 "wm_fit clamps a window to the display, so a frame that "
                 "runs off it is a layout bug - and every edge read below "
                 "would be out of the framebuffer",
                 got=(x, y, w, h), want="inside %dx%d" % (vw, vh)):
        return

    # 1b - THE OPENING RECT AND ITS CELL GRID (WEAVE-SPEC 7.1.1). WEAVE's
    # chrome is the browser's: SPEC.md 11.95's standard rect, the whole
    # desktop band. Every screen fact here is READ from the kernel rather
    # than computed from a constant - [vid_dock_y0] is where the dock
    # starts on THIS adapter, and MBAR_H/TITLE_H come from os88geom, which
    # checks itself against kernel/wm.inc at import.
    th = os88geom.TITLE_H
    vidw = _u16(m.read(S("vid_w"), 2))
    docky = _u16(m.read(S("vid_dock_y0"), 2))
    want_rect = (0, os88geom.MBAR_H, vidw, docky - os88geom.MBAR_H - 1)
    check((x, y, w, h) == want_rect,
          "%s: opened at the standard rect" % machine,
          "WEAVE-SPEC 7.1.1 derives CW x CH from this rect and nothing "
          "else, so a window that opens anywhere else has a different "
          "grid - and a grid that is one row out is a layout the oracle "
          "cannot be diffed against",
          got=(x, y, w, h), want=want_rect)

    # ...and the content box the rect yields, in CELLS. This is the check
    # that catches an 89x36 - a structural fact about the WINDOW, not
    # about what was painted in it, so it survives every font, inset and
    # component change. The pixel content box is SPEC.md 11.95.2's, and
    # the cell arithmetic is WEAVE-SPEC 7.1.2's general form - which
    # reduces to CW = content_w / 8 at content_left = 0, and is written
    # out so a resized window would still be measured correctly.
    flush = _flush(x, w, flags, vidw)
    cl = x if flush else x + 1
    cwpx = w if flush else (w - 2)
    ct, chpx = y + th, h - th - 1
    ox = (cl + 7) & ~7
    cells = ((cl + cwpx - ox) // 8, chpx // 8)
    try:
        want_cells = _oracle_cells(card)
    except RuntimeError as e:
        want_cells = None
        check(False, "%s: weavesim answered a cell grid" % machine,
              "WEAVE-SPEC 12.1 makes weavesim the oracle every "
              "differential diffs against, so without it there is nothing "
              "to diff", got=str(e)[:160], want="content CWxCH cells")
    if want_cells:
        check(cells == want_cells,
              "%s: the content box is %dx%d cells" % ((machine,) + want_cells),
              "the grid the flow walk runs on. weavesim printed the want "
              "and the window record the got, so a disagreement is the "
              "8086 and the oracle laying out on different paper "
              "(WEAVE-SPEC 7.1.1, 12.1)",
              got=cells, want=want_cells)

    # 2 - the title strip: a lit block above a dark separator. SPEC.md 11's
    # frame is a white title bar over rows W_Y+1..W_Y+TITLE_H-2 with black
    # pinstripes through it, then a black separator at W_Y+TITLE_H-1. The
    # strip spans the CONTENT columns, which is `cl` wide and not w-2, for
    # SPEC.md 11.95.2's reason: wm_draw_title is one of the six sites that
    # answer the no-left-border question.
    strip = _box_lit(rows, cl, y + 1, cl + cwpx, y + th - 1)
    area = cwpx * (th - 2)
    check(strip > area * 0.25, "%s: the title strip is drawn" % machine,
          "the strip is white with six pinstripes and a title through it, "
          "so a quarter lit is a floor and not a target - what this "
          "rejects is a strip that is blank, black, or not there",
          got=strip, want="> %d of %d" % (int(area * 0.25), area))
    sep = _row_lit(rows, y + th - 1, cl, cl + cwpx)
    check(sep < cwpx * 0.1,
          "%s: the black separator under the title" % machine,
          "row W_Y+TITLE_H-1 is the rule between the strip and the "
          "content - it is what makes the lit block above it a TITLE BAR "
          "rather than any lit region at that y",
          got=sep, want="< %d" % int(cwpx * 0.1))

    # 3 - THE EDGES, AND HOW MANY OF THEM THERE ARE. SPEC.md 11.95.2 and
    # 11.95.3: a snapped window spanning the screen draws TWO sides, because
    # a border separates a window from what is beside it and at x = 0 - and
    # at the far edge, which is the same sentence - there is nothing beside
    # it. It was three sides until 11.95.3 took the right border too. WEAVE's standard rect is exactly that window,
    # so a gate that asserted a dark column 0 would fail on the correct
    # build - and one that asserted it only for the non-flush case would
    # be asserting nothing at all here. Both shapes are checked, and which
    # one applies comes from the kernel's own predicate (_flush).
    #
    # The outline alone is never a pin: the drop shadow at (+1,+1) is dark
    # too, so "row y+h-1 is dark" is equally true of y+h. Each edge is
    # paired with something NOT dark just inside it.
    edges = [("top",    _row_lit(rows, y, x, x + w),         w),
             ("bottom", _row_lit(rows, y + h - 1, x, x + w), w)]
    if not flush:
        edges.append(("left",  _col_lit(rows, x, y, y + h), h))
        edges.append(("right", _col_lit(rows, x + w - 1, y, y + h), h))
    for what, got, span in edges:
        check(got < span * 0.1,
              "%s: the %s frame edge is where the record says"
              % (machine, what),
              "SPEC.md 11's outline is 1px black on the window rect - a "
              "lit line here means W_X/W_Y/W_W/W_H and the pixels "
              "disagree", got=got, want="< %d of %d"
              % (int(span * 0.1), span))

    # The columns just inside the left and right edges. For a flush window
    # `cl` IS column 0 and there is no border to be inside of - so this
    # asserts the opposite thing about the same column, and it is the
    # check that catches the border being drawn and then covered, which
    # SPEC.md 11.95.2 forbids by name (every pixel of it written twice is
    # PERFORMANCE.md Part 1's double-draw flash, invisible in an
    # emulator).
    for what, cx in (("left", cl), ("right", cl + cwpx - 1)):
        got = _col_lit(rows, cx, y + 1, y + th - 1)
        check(got > (th - 2) * 0.5,
              "%s: the title strip reaches the %s content edge"
              % (machine, what),
              "the pinstripes are inset 3px, so the strip's outermost "
              "content column is white for its whole height - dark here "
              "means this frame is a pixel wider than the record, or "
              "(at column 0) that the suppressed border was drawn anyway",
              got=got, want="> %d of %d" % (int((th - 2) * 0.5), th - 2))

    # ...and the bottom, whose outline the shadow makes ambiguous: the last
    # content rows are the kernel's white fill. Any ONE of the last three
    # being mostly lit pins W_H, and a card would have to paint a
    # full-width dark footer three rows deep to fool it.
    foot = max(_row_lit(rows, yy, cl, cl + cwpx)
               for yy in range(y + h - 4, y + h - 1))
    check(foot > cwpx * 0.5,
          "%s: the content reaches the bottom edge" % machine,
          "the drop shadow at (+1,+1) is dark too, so the dark bottom "
          "outline alone cannot say whether W_H is right - the white fill "
          "above it can", got=foot,
          want="> %d of %d" % (int(cwpx * 0.5), cwpx))

    # 4 - the content holds ink, and is neither empty nor solid. The rect
    # is SPEC.md 11.95.2's, computed above with the frame.
    cx0, cy0, cx1, cy1 = cl, ct, cl + cwpx, ct + chpx
    carea = cwpx * chpx
    clit = _box_lit(rows, cx0, cy0, cx1, cy1)
    frac = clit / float(carea) if carea else 0.0
    minority = min(frac, 1.0 - frac)
    check(minority > 0.01, "%s: the content holds ink (%.1f%% lit)"
          % (machine, frac * 100),
          "an all-white content is the kernel's fill with nothing painted "
          "on it and an all-black one is a card that drew a rectangle - "
          "1%% of this content is about %d pixels, or a dozen glyph cells, "
          "and FORM's card draws several times that" % int(carea * 0.01),
          got="%.4f" % minority, want="> 0.01 either way")

    # 5 - and the ink is in more than one band.
    nb = _bands(rows, cx0, cy0, cx1, cy1, ink_is_dark=frac > 0.5)
    check(nb >= 2, "%s: the flow placed more than one band" % machine,
          "a refusal message is ink too and passes the check above - what "
          "separates it from a flow walk (WEAVE-SPEC 7) is that a flow "
          "leaves blank rows between the things it placed",
          got=nb, want=">= 2")

    # THE CAPTURE'S SEAL: the record must not have moved across it.
    again = _win(m, S, new[0])
    check(again == (x, y, w, h, seg, flags),
          "%s: the window did not move across the capture" % machine,
          "MartyPC runs the guest several times real time, so a rect read "
          "before a framebuffer read can describe a machine that no "
          "longer exists - if this fires, every edge assertion above was "
          "asked about the wrong pixels", got=again,
          want=(x, y, w, h, seg, flags))

    # 6 - and the machine is still alive.
    c0 = m.status().get("cycles", 0)
    time.sleep(0.4)
    c1 = m.status().get("cycles", 0)
    check(c1 > c0, "%s: the guest is still executing" % machine,
          "a task frozen holding the gfx lock draws a perfect window and "
          "never draws another (SPEC.md 59.7) - stillness alone cannot "
          "see it", got="cycles %d -> %d" % (c0, c1), want="advancing")
    print("  %-24s %dx%d  %s at %d,%d %dx%d  %s  grid %dx%d cells  "
          "content %.0f%% lit, %d bands  %.1fs"
          % (machine, vw, vh, name, x, y, w, h,
             "flush" if flush else "bordered", cells[0], cells[1],
             frac * 100, nb, time.time() - t0))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", help="just this one")
    ap.add_argument("--no-make", action="store_true",
                    help="boot %s as it stands" % DISK)
    ap.add_argument("--png", metavar="DIR",
                    help="also write the capture each machine already takes "
                         "as DIR/weave-<machine>.png, plus the window crop "
                         "beside it - no extra boot and no second read")
    a = ap.parse_args()
    rows = [r for r in MACHINES if not a.machine or r[0] == a.machine]
    if not rows:
        print("weavesmoke: no such machine", file=sys.stderr)
        return 2

    # THE SYMBOL MAP, ONCE, BEFORE ANY EMULATOR STARTS. os88sym re-assembles
    # kernel.asm and refuses an address unless the result is byte-identical to
    # build/kernel.bin - and a commit moves three bytes of .text, because the
    # About box's build number is the commit count (SPEC.md 14.2). Asked for
    # the first time halfway through a scripted session, that refusal arrives
    # as a RuntimeError inside the navigation and gets reported as "the bundle
    # was not on the disk": a true failure with a false cause. The suite runner
    # asks the same question in its own preflight; this is what a standalone
    # run gets. `make` is the fix, and the message says so.
    try:
        os88sym.linear("wm_wins")
    except Exception as e:
        check(False, "the kernel map describes build/kernel.bin",
              "every address this reads is resolved through it, so nothing "
              "below would mean what it says", got=str(e)[:200],
              want="a map of the kernel that is actually booting")
        return done("weavesmoke")

    # ALWAYS through make, never cached on existence: a disk left by an earlier
    # build carries an earlier WEAVE, and every assertion here would then
    # report on that one. make is what knows whether anything is stale, and it
    # is a no-op when nothing is.
    if not a.no_make:
        r = subprocess.run(["make", DISK], cwd=ROOT,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           text=True)
        if r.returncode:
            print(r.stdout[-2000:], file=sys.stderr)
            check(False, "make %s" % DISK,
                  "make's own last lines are above and say which it was - the "
                  "package would not build, or SmallerC is not there. The "
                  "second is why the registry declares needs=('cc',) beyond "
                  "WEAVE-SPEC 12.3's marty: a tree without tools/setup-cc.sh's "
                  "compiler should SKIP this row rather than fail it",
                  got="exit %d" % r.returncode, want="the disk")
            return done("weavesmoke")
    if not os.path.exists(os.path.join(ROOT, DISK)):
        check(False, "%s exists" % DISK,
              "nothing to boot - `make weavedisk` builds all three geometries",
              got="missing", want="a floppy image")
        return done("weavesmoke")

    for machine, card, w, h in rows:
        smoke(machine, card, w, h, png_dir=a.png)
    done("weavesmoke")


if __name__ == "__main__":
    sys.exit(main() or 0)
