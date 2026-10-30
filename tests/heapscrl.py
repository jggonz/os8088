#!/usr/bin/env python3
"""The Task Manager's memory page scrolls, and its bar survives the refresh.

The page was the HEAP page until SPEC.md 28.4.10 merged it into the memory
page; its list is groups now, and the row opens all of them (`+ALL`) first,
because a closed list is one row per owner and does not overflow.

    make marty && python3 tests/heapscrl.py [--machine os8088_5150_cga_gla]

SPEC.md 28.4.4. `TMH_ROWS` is 47 and no machine here shows more than 22, so
this page has always been able to run off the bottom of its own frame - and it
did so silently, because `tm_row_place` refuses a row past `tm_ylim` and says
nothing at all about the rows behind it. It has a scroll bar now, and there
are two things about that a screenshot taken at the wrong moment cannot tell
you:

1. **THE BAR SURVIVES ITS OWN LIST.** A row's last chunk scrubs the band past
   the text (SPEC.md 28.2), and on this page that band would otherwise run
   straight through the bar - so a row redrawing would white the bar out from
   under itself. `tm_rowr` is the fix, and what makes it hard to photograph is
   that `tm_hbar` runs AFTER the rows in the same paint: whenever the bar's
   own key moves it is drawn again anyway, so the damage is repaired before
   any capture can see it. What is NOT repaired is a row that changes while
   the key does not - a claim replaced by another of the same count, which on
   a page whose subject is claims coming and going is an ordinary Tuesday.
   THAT is what the bar checks below can see, and they are a floor rather
   than a proof: they hold the bar over a quiet list, they hold what the
   incremental path left - at the bottom end stop, where the scroll MOVED the
   thumb - against what a full repaint draws, and they hold that repaint's bar
   over four more refreshes. Each pair is read by `glass_bar`, with the guest
   stopped and the block it was drawn from, and compared only when the block
   held still: a list that moved between two captures is a bar that SHOULD
   differ, and reading it as tm_rowr's defect was a false red. On the target
   machine the same defect is also a PERFORMANCE.md Part 1 double-draw - the
   bar erased and redrawn on every scroll, ~756 us a call - and nothing here
   can see that at all.

2. **A SCROLL MOVES THE LIST AND COMES BACK.** The offset is spent in
   `tm_mrow_close`, which makes `[tm_mrow]` a SCREEN row rather than a table
   row and leaves SPEC.md 28.2's per-chunk cache indexed by where a row is
   actually drawn. That is what makes a scroll cheap, and it is also what
   makes a wrong version of it plausible: a cache that is one row out redraws
   nothing and the list is simply stale. So the assertion is a ROUND TRIP -
   down and up must return the row area byte-identical - which is the
   discipline tests/trkscrl.py and tests/brtest.py hold their blits to.

**MARTYPC, and the machine is a 5150 with a CGA** - what this OS is for, and
the one whose heap page runs in TWO COLUMNS (SPEC.md 28.1.1), which is the
harder layout for everything below: the bar goes beside the LAST column and a
scroll moves rows between columns rather than up a list. QEMU is a fallback
with a closed list (CLAUDE.md's Testing section) and nothing here is on it.
GLaBIOS rather than the period ROM only because the IBM ROM is not in this
tree; nothing here is a disk measurement, which is the one thing that would
make the twin the wrong machine.

**NOTHING IS HARD-CODED TO A DESKTOP.** The window's rect comes out of its own
record, the bar's out of `[tm_hsb]`, the drive zones out of `dsk_vtab` through
`dispcp.open_drive`, and the z-order out of `wm_zord`.

**THE Z-ORDER IS READ RATHER THAN ASSUMED, and that is not fussiness.** Every
window this file opens to make the list overflow lands IN FRONT of the Task
Manager, and `kernel/ui.inc`'s content path raises a background window and
returns without ever reaching `W_ONCLICK` - System 1's click-to-front, and
deliberate. So the first press on the page after all that is spent activating
it, and a version of this file that did not know so read as a scroll bar
ignoring its first click. It is worse than it sounds: the obvious fix, a click
on the TITLE BAR after every opener, RAISES THE WRONG WINDOW, because by the
third opener that point is inside the cascade (measured under QEMU: the Timer
lands at 327,60 and a Disk window at 119,96, and between them they cover the
title bar of a window at 247,100). So the raise is done once, at a point that
is provably the Task Manager's, and then CHECKED against `wm_zord` before a
single assertion runs.

MAKING THE LIST OVERFLOW IS PART OF THE TEST, not a fixture: it opens
programs until the guest's own `[tm_hrows]` exceeds its own `[tm_maxrow]`,
which is self-adjusting across adapters where a fixed number of Bounces would
be right on one and pointless on another. The programs are TIMER.O88 and
BOUNCE.O88 out of the apps disk's APPS/ folder (they were the kernel's Timer
and Bounce kinds, launched from Locator's Builtins menu, until SPEC.md 14.8
made them packages and the menu went), opened BY NAME through dispcp.open_named
so the row waits for each window rather than guessing. If it cannot get there
- the scheduler's task slots are the usual reason, a Bounce owning a worker -
it says so and fails rather than passing quietly, because a "scroll test" that
never scrolled is worse than no test at all.
"""
import argparse
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from os88pkg import PKG_FMT                                # noqa: E402
sys.path.insert(0, HERE)
import os88build
import os88pkg
import os88marty                                             # noqa: E402
import os88fixture                                          # noqa: E402
import os88mouse                                             # noqa: E402
import os88sym                                               # noqa: E402
import os88geom as geom                                      # noqa: E402
import dispcp                                                # noqa: E402

S = os88sym.linear
APP = "taskmgr"
NAME = b"TaskMgr"

# The chip menu's items. MENU-BAR coordinates, which are the same on every
# adapter because the bar is.
CHIP, MI_TASKS, MI_CTRL = (12, 8), (60, 60), (60, 44)
# ...and the two packages that make the list overflow, in the apps disk's
# APPS/ folder (SPEC.md 111, 112). Opened by name: dispcp.open_named reads
# the listing and waits for the window, so nothing here depends on where a
# row is drawn or how long a launch takes.
PKG_FOLDER, PKG_TIMER, PKG_BOUNCE = "APPS", "TIMER.O88", "BOUNCE.O88"

# How much deeper than the frame the list has to be before anything is
# asserted about scrolling it. NOT one row: a window's raise cache is a
# purgeable System row that appears and disappears without anybody asking
# (SPEC.md 11.96/50.6), so at one row of headroom the clamp can take the whole
# scroll range away between two reads - and every assertion below then fails
# at once with nothing wrong.
SLACK = 3
TM_PEN, TMM_HDR_Y = 8, 66               # apps/taskmgr/taskmgr.asm

# One unattended paint of this page, in CGA frames. TM_INT is 9 ticks and
# TM_SLOW is 4 of those (SPEC.md 28.6), so a refresh is 36 ticks = 1.98 s, and
# a 60 Hz card draws ~119 frames in that. Derived rather than typed, because
# the whole reason this number exists is that the page's refresh rate moved
# once already and a fixed frame count would have gone on measuring nothing.
TICKS_HZ, CARD_HZ = 18.2, 60.0
TM_INT, TM_SLOW = 9, 4
SLOW_FRAMES = int(TM_INT * TM_SLOW / TICKS_HZ * CARD_HZ)

FAILED = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(cond, what, why="", got=None, want=None):
    if cond:
        say("  ok   %s" % what)
        return True
    m = what
    if got is not None or want is not None:
        m += "\n        want: %s\n        got:  %s" % (want, got)
    if why:
        m += "\n        why:  " + why
    FAILED.append(m)
    say("  FAIL %s" % what)
    return False


def report():
    if FAILED:
        say("heapscrl: %d FAILED" % len(FAILED))
        for m in FAILED:
            say("  FAIL: %s" % m)
        sys.exit(1)
    say("heapscrl: pass")
    sys.exit(0)


# --- the package's own state --------------------------------------------------
_MAP = {}
# kern_mega (OS88_MEGA, docs/BOCHS.md): mega.img's SYSTEM/ carries the Task
# Manager's ARM (SPEC.md 28.4.15), assembled -DPKG_MEGA - a different layout -
# and its region is an LDT SELECTOR, reached through m.lin
MEGA = bool(os.environ.get("OS88_MEGA"))


def lin(m, seg, off=0):
    return m.lin(seg, off) if MEGA else (seg << 4) + off


def sym(name):
    """A taskmgr symbol's link address, out of NASM's own map."""
    if not _MAP:
        src = os.path.join(ROOT, "apps", APP, APP + ".asm")
        tmp = tempfile.mktemp(suffix=".asm")
        mp = tempfile.mktemp(suffix=".map")
        open(tmp, "w").write(open(src).read() + "\n[map all %s]\n" % mp)
        r = subprocess.run(["nasm", "-f", "bin", "-w+error",
                            "-I", os.path.join(ROOT, "apps") + os.sep]
                           + (["-DPKG_MEGA"] if MEGA else [])
                           + ["-o", tmp + ".bin", tmp],
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit("heapscrl: could not map %s:\n%s" % (APP, r.stderr[:400]))
        for line in open(mp):
            p = line.split()                # "<vaddr> <raddr> <name>", HEX
            if len(p) == 3:
                try:
                    _MAP[p[2]] = int(p[0], 16)
                except ValueError:
                    pass
        for f in (tmp, mp, tmp + ".bin"):
            if os.path.exists(f):
                os.remove(f)
        if "os88_image_end" not in _MAP:
            sys.exit("heapscrl: taskmgr's map has no os88_image_end")
    if name not in _MAP:
        sys.exit("heapscrl: taskmgr has no symbol %s" % name)
    return _MAP[name]


def img_size():
    """The package's image size, which is where the loader puts its bss.

    **THE FILE IS NOT THE IMAGE** (SPEC.md 20.13.5): `PKGZ ?= lz4`, so every
    shipped .o88 is a compressed container and `image` at +8 keeps meaning the
    UNPACKED size on both. `n != len(d)` was a layout check and became the
    ordinary shape of a shipped package - it read `image=8917 but is 7242` on
    a perfectly good build. Unwrapped it is a layout check again, which is
    still worth having.
    """
    raw = open(os88build.at("build/%s%s.o88" % ("megak/apps/" if MEGA else "",
                                                APP)), "rb").read()
    d = os88pkg.image_unwrap(raw)
    n = int.from_bytes(d[8:10], "little")   # +8 = image size (SPEC.md 20.2)
    if n != len(d):
        sys.exit("heapscrl: %s.o88 says image=%d and unwraps to %d - the "
                 "header layout has moved" % (APP, n, len(d)))
    return n


class Tm:
    """The window's live state, read out of its own bss."""

    def __init__(self, m, slot, seg):
        self.m, self.slot, self.seg = m, slot, seg
        self.bss = lin(m, seg, img_size())

    def _at(self, name, i=0):
        return self.bss + sym(name) - sym("os88_image_end") + 2 * i

    def word(self, name, i=0):
        return int.from_bytes(self.m.read(self._at(name, i), 2), "little")

    def byte(self, name):
        return self.m.read(self._at(name), 1)[0]

    def dword(self, name):
        return int.from_bytes(self.m.read(self._at(name), 4), "little")

    def win(self):
        return dispcp.win_rect(self.m, S, self.slot)

    def bar(self):
        """The scroll bar's block: x1, y1, x2, y2, total, fit, pos."""
        return [self.word("tm_hsb", i) for i in range(7)]


def pkg_slot(m, name):
    """(slot, segment) of the visible window whose package is called `name`.

    The window record carries W_SEG (SPEC.md 20.1) and a package's segment
    begins with its header, so the name at +16 identifies it with no listing
    offset and no guess about launch order.
    """
    t = m.read(S("wm_wins"), geom.MAX_WIN * geom.WIN_SIZE)
    for i in range(geom.MAX_WIN):
        b = i * geom.WIN_SIZE
        if int.from_bytes(t[b + geom.W_FLAGS:b + geom.W_FLAGS + 2],
                          "little") & 3 != 3:
            continue
        seg = int.from_bytes(t[b + geom.W_SEG:b + geom.W_SEG + 2], "little")
        if not seg:
            continue
        hdr = m.read(lin(m, seg), 32)
        if hdr[:3] == b"O8" + bytes([PKG_FMT]) and hdr[16:32].split(b"\0")[0] == name:
            return i, seg
    return None


def zorder(m):
    """Window slots back to FRONT, out of wm_zord (SPEC.md 11)."""
    zn = m.read(S("wm_zn"), 1)[0]
    return list(m.read(S("wm_zord"), 16)[:zn])


def frontmost(m):
    """The slot wm_top would answer: the last VISIBLE entry in wm_zord."""
    t = m.read(S("wm_wins"), geom.MAX_WIN * geom.WIN_SIZE)
    for slot in reversed(zorder(m)):
        b = slot * geom.WIN_SIZE
        if int.from_bytes(t[b + geom.W_FLAGS:b + geom.W_FLAGS + 2],
                          "little") & 2:
            return slot
    return None


def band(m, x1, y1, x2, y2):
    """One rectangle of the card's RENDERED framebuffer, packed rgb24."""
    return os88marty.crop_rgb(m, x1, y1, x2 - x1 + 1, y2 - y1 + 1)


def tick(m, frames):
    """Let the guest run for `frames` and leave it running."""
    m.advance(frames=frames)
    m.run()


def stopped_read(m, tm, *names):
    """Read several of the window's words with the GUEST STOPPED.

    This branch learned it once already (`tests/dtfield: read the pair with
    the guest STOPPED, not across a tick`) and this file learned it again:
    [tm_htop] and [tm_hrows] are two reads, the machine runs between them, and
    a claim made in that gap makes the pair describe two different instants.
    The end stop is total - fit, so a total that grew after htop was sampled
    reads as a scroll that stopped one row short of a bound it was never
    given. Bounded and cheap: the guest is paused for the reads and put back.
    """
    m.pause()
    try:
        return [tm.word(n) for n in names]
    finally:
        m.run()


def glass_bar(m, tm, tries=8):
    """The bar ON THE GLASS and the block it was drawn from, read as ONE fact.

    `band` reads the last frame the card FINISHED while the guest runs on, and
    `tm.bar()` is a second read at a second instant. Compared across a round
    trip, that pair cannot tell a defect from two legitimate events: a paint
    still in flight (the worker letters the rows and draws the bar LAST, so a
    frame can hold the rows of one walk beside the bar of the one before), and
    a claim arriving or going - the list is live (SPEC.md 50.6), and a total
    one row longer is a different thumb. Check 3 already excuses the second
    with a re-read of the row count; the bar checks had nothing, so either one
    landing between their two captures read as tm_rowr's defect.

    So the guest is STOPPED, and the answer is taken only when it is STILL:
    the block, the pixels, then two frames later the block and the pixels
    again, all four agreeing. A paint in progress moves one of them; the block
    holding across the pair says the pixels are a function of it. Bounded: a
    bar that never holds still fails here, saying so, rather than being
    compared at all.
    """
    x1, y1, x2, y2 = tm.bar()[:4]
    last = None
    for _ in range(tries):
        m.pause()
        try:
            blk = tm.bar() + [tm.word("tm_hrows")]
            img = band(m, x1, y1, x2, y2)
            m.advance(frames=2)
            if tm.bar() + [tm.word("tm_hrows")] == blk and \
                    band(m, x1, y1, x2, y2) == img:
                return blk, img
            last = blk
        finally:
            m.run()
        tick(m, 20)
    sys.exit("heapscrl: the bar never held still for two frames (last block "
             "%r) - a paint that never ends, or a list that never stops "
             "moving; nothing below can be compared against it" % (last,))


def dump_bar(tag, blk, img):
    """Save a bar capture where a failure can be looked at (PPM, rgb24)."""
    x1, y1, x2, y2 = blk[:4]
    fd, path = tempfile.mkstemp(prefix="heapscrl-%s-" % tag, suffix=".ppm")
    with os.fdopen(fd, "wb") as f:
        f.write(b"P6 %d %d 255\n" % (x2 - x1 + 1, y2 - y1 + 1) + img)
    return path


def bar_diff(a, b, blk):
    """The differing pixels of two bar captures: count and bounding box."""
    x1, y1, x2, y2 = blk[:4]
    w = x2 - x1 + 1
    pts = [(i // 3 % w + x1, i // 3 // w + y1)
           for i in range(0, min(len(a), len(b)), 3) if a[i:i + 3] != b[i:i + 3]]
    if not pts:
        return "no pixel differs"
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return "%d px differ in (%d,%d)-(%d,%d)" % (len(pts), min(xs), min(ys),
                                              max(xs), max(ys))


def scroll_to_end(m, mo, tm, where, limit, top=False):
    """Click `where` until the view is AT the end stop, and answer the pair.

    NOT a fixed number of clicks, and not stability either - both were tried.
    The list is live: a purgeable claim comes and goes on its own (SPEC.md
    50.6), and tm_hclamp measures against the LAST walk's count by design
    (SPEC.md 28.4.4), so a list that GREW hands the view range it did not have
    when the click landed. A fixed count then stops one row short of a bound
    that moved, and stability stops there too, because the view really has
    stopped - the range opened up after it did, and nothing auto-scrolls.

    So the loop's condition is the invariant itself, read with the guest
    STOPPED, and it is bounded: a list that never stops growing fails here
    rather than spinning.
    """
    pair = ("tm_htop", "tm_hrows", "tm_maxrow")
    for _ in range(limit):
        pos, rows, fit = stopped_read(m, tm, *pair)
        end = max(0, rows - fit)
        if pos == (0 if top else end):
            return pos, end
        mo.click(*where)
        tick(m, 25)
    pos, rows, fit = stopped_read(m, tm, *pair)
    return pos, max(0, rows - fit)


def settle_rows(m, tm, tries=30):
    """Wait for the table to stop changing size, and answer how big it is.

    NOT belt and braces. Opening the last Disk window leaves claims in flight
    - the directory read-ahead window is PURGEABLE and is shed again (SPEC.md
    18.95/50.6), a volume's FAT window and ASSOC cache arrive with the mount -
    so [tm_hrows] moves for several seconds after the click that caused it.
    Measure the scroll range in the middle of that and the range is a lie:
    tm_hclamp is entitled to take the whole of it away between the read and
    the click, and every assertion downstream then fails with nothing wrong.
    """
    last, same = None, 0
    for _ in range(tries):
        n = tm.word("tm_hrows")
        same = same + 1 if n == last else 0
        last = n
        if same >= 4:
            return n
        tick(m, 12)
    say("heapscrl: WARNING - the table is still resizing (%d rows)" % last)
    return last


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_cga_gla")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default=None,
                    help="default: apps360.img plus TIMER.O88, BOUNCE.O88 (SPEC.md 24.6.1 "
                    "took it off), on a disk of this run's own")
    a = ap.parse_args(argv)
    if a.apps is None:
        a.apps = os88fixture.apps360_plus(
            "APPS:build/timer.o88", "APPS:build/bounce.o88", tag="heapscrl")
    os.chdir(ROOT)

    with os88marty.launch(a.image, apps=a.apps, machine=a.machine, boot=False) as m:
        m.run()
        os88marty.kernel_up(m)          # the boot gate on STATE
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)

        # THE SCREEN NEVER STOPS ON THIS PAGE, so os88marty.settle cannot be
        # the wait anywhere below: the Task Manager repaints twice a second by
        # design and five Bounce windows animate beside it, and `settle` waits
        # for stillness that will not arrive - measured, it spends its whole
        # 120 s limit and then raises. dispapps.py's opener has the same note.
        # A frame count is the honest instrument here, and dispcp.open_drive
        # takes the wait as a parameter for exactly this reason.
        def wait(mm, card=None, frames=90):
            tick(mm, frames)

        # --- the DRIVE WINDOWS FIRST, while the desktop is still bare ------
        # Order, and it is the CGA desktop that forces it: 640x200 with a dock
        # leaves the Task Manager's own 464x155 frame covering the drive zones
        # at the right edge, so a double-click meant for drive A: lands on the
        # Task Manager's content instead - which cycles its page and opens no
        # window at all (measured: two Disk openers, zero rows). Opening them
        # before it exists needs no window moved and no coordinate guessed.
        for letter in ("A", "B"):
            dispcp.open_drive(m, mo, S, wait, letter)
            tick(m, 60)

        # --- the Task Manager, and its memory page -------------------------
        mo.menu(CHIP[0], CHIP[1], MI_TASKS[0], MI_TASKS[1])
        tick(m, 150)                # NOT settle: this window repaints twice a
                                    # second, so the screen never stops
        got = pkg_slot(m, NAME)
        if got is None:
            sys.exit("heapscrl: the Task Manager did not open")
        slot, seg = got
        tm = Tm(m, slot, seg)
        wx, wy, ww, wh = tm.win()
        say("heapscrl: %s - window %d at (%d,%d) %dx%d, segment %04x"
            % (a.machine, slot, wx, wy, ww, wh, seg))

        # The memory page - the heap page merged into it (SPEC.md 28.4.10).
        # The click that cycles it goes ABOVE the list, on the captions and
        # the map: a click on the list is a group's, and opens or closes it.
        cx, cy = wx + 40, wy + 36
        for _ in range(2):
            if tm.byte("tm_view") == 1:
                break
            mo.click(cx, cy)
            tick(m, 40)
        if not check(tm.byte("tm_view") == 1,
                     "a click reaches the memory page",
                     "the click cycles 0 -> 1 -> 0 and this is where the rest "
                     "of the file is looking"):
            return report()
        # ...and every group OPEN: closed, the list is one row per owner and
        # no number of programs would run it off the frame. `+ALL` is the
        # header's right end, off the page's own origin and rise.
        if tm.dword("tm_exp") != 0xFFFFFFFF:
            mo.click(tm.word("tm_cx") + TM_PEN + 22 * 8 + 4,
                     tm.word("tm_cy") + TMM_HDR_Y - tm.word("tm_xoff") + 4)
            tick(m, 40)
        if not check(tm.dword("tm_exp") == 0xFFFFFFFF,
                     "+ALL opens every group",
                     got="%08X" % tm.dword("tm_exp")):
            return report()

        # --- make the list longer than the frame ---------------------------
        # NOTHING RAISES THE TASK MANAGER IN HERE and it does not need to be:
        # the worker's paint walks the list and settles [tm_hrows] whatever
        # the z-order is, and only the DRAWING is clipped away. The programs
        # come off B:'s APPS/ folder: open_named raises that Disk window
        # itself (the acting one, which B: is - it was opened last) and waits
        # for each package's window before answering.
        dispcp.open_named(m, mo, S, wait, 0, 0, PKG_FOLDER, expect="nav")
        tick(m, 40)
        plan = ([("Timer", PKG_TIMER)] + [("Bounce", PKG_BOUNCE)] * 6 +
                [("Control", None)])
        for what, how in plan:
            if tm.word("tm_hrows") >= tm.word("tm_maxrow") + SLACK:
                break
            if what == "Control":           # the chip menu is the KERNEL's
                mo.menu(CHIP[0], CHIP[1], MI_CTRL[0], MI_CTRL[1])
            else:
                dispcp.open_named(m, mo, S, wait, 0, 0, how, expect="window")
            tick(m, 60)
            say("  ... %-8s -> %d rows" % (what, tm.word("tm_hrows")))

        rows, fit = settle_rows(m, tm), tm.word("tm_maxrow")
        say("heapscrl: %d rows in the table, %d on screen, %d column(s)"
            % (rows, fit, tm.word("tm_cols")))
        if not check(rows >= fit + SLACK,
                     "the list is %d rows longer than the frame" % SLACK,
                     "nothing below this can scroll, and a scroll test that "
                     "never scrolled passes for the wrong reason. The usual "
                     "cause is MAX_TASKS: a Bounce owns a worker (SPEC.md "
                     "112) and the sixth one does not get a slot"):
            return report()

        # --- put it in front, and PROVE it ---------------------------------
        x1, y1, x2, y2, total, sfit, _pos = tm.bar()
        up = ((x1 + x2) // 2, y1 + 4)       # the arrow cells: y1+10 and
        down = ((x1 + x2) // 2, y2 - 4)     # y2-10 are their rules
        say("heapscrl: bar (%d,%d)-(%d,%d), total %d fit %d"
            % (x1, y1, x2, y2, total, sfit))
        check(total == rows and sfit == fit,
              "the bar was drawn from the numbers the list produced",
              "os88ui_sbthumb sizes the thumb out of these three; a stale "
              "total is a thumb that says the wrong thing about how much is "
              "off screen")

        say("heapscrl: z-order back..front %r, front = %r"
            % (zorder(m), frontmost(m)))
        for _ in range(3):
            if frontmost(m) == slot:
                break
            mo.click(*down)                 # the arrow is below and right of
            tick(m, 40)                     # everything the cascade opens
        if not check(frontmost(m) == slot,
                     "the Task Manager is frontmost before anything is asserted",
                     "kernel/ui.inc's content path raises a background window "
                     "and returns without reaching W_ONCLICK, so every press "
                     "below would be spent on the raise instead of on the bar",
                     got=frontmost(m), want=slot):
            return report()
        pos = tm.word("tm_htop")
        say("heapscrl: frontmost, pos %d" % pos)

        rowarea = (wx + 2, y1, x1 - 1, y2)          # the rows, not the bar
        park = (6, wy + 40)                         # the cursor is IN the
                                                    # framebuffer, so every
                                                    # capture is taken with it
                                                    # off the window

        # --- 1. the bar survives its own list's refresh --------------------
        mo.to(*park)
        tick(m, 30)
        for _ in range(3):                  # ...over a list that held still:
            b0, before = glass_bar(m, tm)   # a claim made in the window is a
            tick(m, 4 * SLOW_FRAMES)        # bar that SHOULD move (glass_bar)
            b1, after = glass_bar(m, tm)    # four unattended refreshes
            if b1 == b0:
                break
            say("heapscrl: the list moved under the refreshes (%r -> %r); "
                "again" % (b0, b1))
        if not check(b1 == b0 and after == before,
                     "the bar is untouched by four refreshes of the list beside it",
                     "tm_rowr: the row band would otherwise run through the "
                     "bar. This half is the unconditional case - a scrub that "
                     "happens on every pass rather than only when a row's "
                     "text moves",
                     got="%r, %s" % (b1, bar_diff(before, after, b0)),
                     want=b0):
            say("  saved: %s %s" % (dump_bar("before", b0, before),
                                    dump_bar("after", b1, after)))
        top = band(m, *rowarea)

        # --- 2. a scroll moves the list, and comes back --------------------
        mo.click(*down)
        tick(m, 60)
        got = tm.word("tm_htop")
        check(got == pos + 1, "the down arrow scrolls the view by one row",
              "os88ui_sbhit reports the cell and tm_hscroll owns what it means",
              got=got, want=pos + 1)
        mo.to(*park)
        tick(m, 30)
        check(band(m, *rowarea) != top,
              "...and the rows on screen actually changed",
              "the offset is spent in tm_mrow_close, so a version that moved "
              "[tm_htop] and not the walk would leave the list as it was")

        mo.click(*up)
        tick(m, 60)
        got = tm.word("tm_htop")
        check(got == pos, "the up arrow brings it back", got=got, want=pos)
        mo.to(*park)
        tick(m, 30)
        check(band(m, *rowarea) == top or tm.word("tm_hrows") != rows,
              "...to the same pixels it started at",
              "SPEC.md 28.2's cache is indexed by the SCREEN row, so a scroll "
              "reuses it rather than clearing it - and a cache one row out of "
              "step redraws nothing at all, which reads as a list that is "
              "merely stale. (A claim made or purged under the round trip is "
              "not a failure, which is why the row count is re-read here.)")

        # --- 3. the end stops hold -----------------------------------------
        got, end = scroll_to_end(m, mo, tm, down, rows + 8)
        check(got == end, "the bottom end stop is total - fit (%d)" % end,
              "tm_hclamp is the only thing that knows how far down there is "
              "to go, and past it the walk produces blank rows, not claims",
              got=got, want=end)
        got, _end = scroll_to_end(m, mo, tm, up, rows + 8, top=True)
        check(got == 0, "...and the top one is row 0", got=got, want=0)
        mo.to(*park)
        tick(m, 30)
        check(band(m, *rowarea) == top or tm.word("tm_hrows") != rows,
              "the list is back where it started after both end stops")

        # --- 4. the bar the REFRESH left is the bar a repaint draws ---------
        # The other half of tm_rowr. Check 1 holds the bar over a QUIET list,
        # and a quiet list redraws no chunk at all - so it sees a scrub that
        # happens unconditionally and not one that happens when a row's text
        # moves. This one sees what a run's worth of moving rows left behind:
        # a bar damaged while its own key stood still is never put back, so
        # the incremental picture drifts away from the drawn-from-scratch one
        # and stays away.
        #
        # AT THE BOTTOM END STOP, not at the top. Every bar this file has
        # looked at so far was last drawn at pos 0 - which is also what a full
        # repaint draws there, so a scroll path that never moved the thumb at
        # all would have compared equal. From the end stop the thumb the
        # incremental path left is the one it MOVED, and the page turn redraws
        # it from nothing at the same pos (tm_htop survives the turn).
        #
        # Both captures are glass_bar's, and the pair is only compared when
        # the block is the same at both ends: the list is live, and a claim
        # made or purged across the page turn is a bar that legitimately
        # differs. That case goes round again, scrolled away and back so the
        # bar is once more the incremental path's.
        for _ in range(3):
            scroll_to_end(m, mo, tm, down, rows + 8)
            mo.to(*park)
            tick(m, 30)
            b0, worn = glass_bar(m, tm)
            mo.click(cx, cy)                # 1 -> 0 -> 1, and each one
            tick(m, 50)                     # white-fills the content first
            mo.click(cx, cy)                # ...and the click returns when
            b1, fresh = glass_bar(m, tm)    # the UI task has drawn it: taken
            tick(m, 4 * SLOW_FRAMES)        # AT ONCE, before a refresh can
            b2, later = glass_bar(m, tm)    # touch it, and again after four
            if b1 == b0 == b2:
                break
            say("heapscrl: the list moved across the page turn (%r -> %r "
                "-> %r); again" % (b0, b1, b2))
            scroll_to_end(m, mo, tm, up, rows + 8, top=True)
        check(tm.byte("tm_view") == 1,
              "two more clicks come back to the memory page")
        check(b0[6] > 0, "...with the view scrolled off the top (pos %d)"
              % b0[6], "a bar compared at pos 0 is one no scroll ever moved")
        if not check(b1 == b0 and worn == fresh,
                     "the bar the refresh left is the one a full repaint draws",
                     "tm_rowr, the half that survives its own repair: a row "
                     "redrawn while the bar's key has not moved erases part "
                     "of it and nothing puts it back, so what the incremental "
                     "path leaves on the glass drifts away from what "
                     "tm_draw_mem would put there",
                     got="%r, %s" % (b1, bar_diff(worn, fresh, b0)),
                     want=b0):
            say("  saved: %s %s" % (dump_bar("worn", b0, worn),
                                    dump_bar("fresh", b1, fresh)))
        # ...and check 1 again, against a REFERENCE this time. Check 1 asks
        # whether the bar CHANGES over four refreshes, which a scrub that
        # happens on every pass answers "no" to: the bar is already white by
        # the time it looks, and stays white. Here the "before" is the page
        # turn's own bar, drawn whole and taken before any refresh ran.
        if not check(b2 == b1 and later == fresh,
                     "...and four refreshes after it leave that bar alone",
                     "tm_rowr's unconditional half: a refresh that letters "
                     "the rows beside the bar scrubs it, and the bar's key "
                     "has not moved, so nothing draws it back",
                     got="%r, %s" % (b2, bar_diff(fresh, later, b1)),
                     want=b1):
            say("  saved: %s %s" % (dump_bar("fresh", b1, fresh),
                                    dump_bar("later", b2, later)))

    return report()


main(sys.argv[1:])
