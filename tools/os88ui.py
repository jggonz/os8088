#!/usr/bin/env python3
"""os88ui: the things a test MEANS, resolved by NAME and CONFIRMED.

    import os88ui

    with os88ui.boot("build/os8088-360.img", apps="build/apps360.img") as ui:
        disk = ui.open_drive("B")            # ...whatever ordinal B: has
        ui.open("APPS")                      # ...scrolling if it must
        w = ui.open("MINES.O88")             # ...and the window it opened
        ui.menu_pick("File", "Close")        # ...off menu_bar[], not (12, 8)

WHY THIS EXISTS. Six operations account for most of what a scripted session
does, every one of them has been written out by hand a hundred times, and each
has a way of going wrong that produces NO ERROR at the point of failure:

  * **boot and wait for the desktop** - 175 of 231 scripts open with the same
    three lines, and 171 of the 206 that launch never turn the screen saver
    off;
  * **open a Disk window on drive X** - a click at a remembered zone, on a
    machine whose B: was retired by SPEC.md 18.97's probe, lands on bare
    desktop;
  * **open a folder** - a row number is not a file: SPEC.md 19.4 sorts by
    name, a folder that gains one entry renumbers every row after it, and a
    folder that outgrows the window needs scrolling first;
  * **open a package** - the same, plus the file gets renamed;
  * **bring a window to the front** - a title bar aimed at by arithmetic, and
    then no check that anything came forward;
  * **drag a window** - a press, a move and a release, any of which the UART
    may drop, and no check on where it landed.

None of those announce themselves. The click lands somewhere harmless, the
script carries on, and the failure surfaces twenty steps later wearing the
costume of the feature under test. THAT is the expense - not the typing.

WHAT THIS DOES DIFFERENTLY - three rules, and they are the whole design:

 1. **Nothing is aimed at a remembered coordinate.** Every position is derived
    from the kernel's own live tables: `dsk_vtab` for which zone a drive owns,
    `wm_wins` for where a window is, the staged listing for which row a name
    is on, `menu_bar[]` for where a menu title sits. A layout change moves
    them all at once.
 2. **Every verb CONFIRMS, by reading guest state rather than by settling.**
    `settle` waits for two identical frames a second apart and reads the
    framebuffer over the socket to do it; polling `wm_wins` for the window
    that was supposed to appear is a 408-byte read and answers the actual
    question. Confirming is therefore FASTER than not confirming, which is the
    happy part of this: the cheap thing and the correct thing are the same.
 3. **A verb that cannot confirm RAISES, naming the step and what it saw.** A
    UIError says "no window called 'MINES' opened; what is open is ['Disk B',
    'Apps']" - so the failure is reported where it happened.

WHAT IT IS NOT. It is not a replacement for `os88marty.settle`: a test that
compares PIXELS still has to wait for the picture, and `ui.settle()` is here
for that. It is a replacement for settling as a way of waiting for something
to HAPPEN, which is what settle is usually being asked to do.

THE MOUSE UNDER IT is tools/os88mouse.py - absolute, closed-loop, proven
button edges. tools/os88mouserel.py is the relative one and is not used here;
if the mouse itself is what you are testing, this file is the wrong layer.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import os88geom as geom                                          # noqa: E402
import os88marty                                                 # noqa: E402
from os88marty import MartyError                                 # noqa: E402
from os88mouse import Mouse                                      # noqa: E402


class UIError(Exception):
    """A verb that could not confirm what it did. The message names the step."""


# --- how long a confirmation may take ---------------------------------------
#
# GUEST seconds, not host ones - `os88marty.until` anchors its deadline to the
# emulator's own cycle counter, so what a wait allows is the same on a loaded
# box as an idle one (docs/plans/SOAK-PARALLEL.md 2). These are budgets a healthy
# machine never approaches: the poll returns the moment the condition is true,
# so a generous number costs nothing and a tight one turns a slow host into a
# false failure.
T_WINDOW = 30.0         # a package to load and put a window up: SPEC.md 21's
                        # whole path, several int 13h calls at ~400 ms each
T_NAV = 20.0            # a folder navigation: one mount and a repaint
T_RAISE = 8.0           # a z-order change: no disk in it at all
T_MOVE = 15.0           # a drag: the XOR outline is drawn per packet
T_MENU = 8.0            # a pull-down to appear, and an item to highlight
# THE FIRST LOOK, for a verb that RE-SENDS its input rather than waiting longer
# (`close` below, and the same shape in tests/hibernate.py). It is deliberately
# short: the whole budget is still spent on the second attempt, so what this
# buys is finding out sooner that the first one was not taken. GUEST seconds
# like every other budget here.
T_SHORT = 3.0
# `scroll_to`'s per-arrow-key budget. It is named apart from the others
# because a "no" from that wait is read as an END STOP rather than as a
# failure - so a budget cut short by a busy box does not raise, it decides
# the list has run out and clicks the wrong row. GUEST seconds, for exactly
# that reason.
T_STEP = 3.0
POLL = 0.05             # ...and how often to ask. A word read is ~1 ms.


def _u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


# =============================================================================
# booting
# =============================================================================

class _Booted(object):
    """`with os88ui.boot(...) as ui:` - the context manager `boot` returns."""

    def __init__(self, cm, **kw):
        self._cm = cm
        self._kw = kw

    def __enter__(self):
        m = self._cm.__enter__()
        try:
            ui = UI(m, card=self._kw.get("card"),
                    verbose=self._kw.get("verbose", True))
            limit = self._kw.get("limit", 180.0)
            # THE ORDER MATTERS AND IT IS NOT THE OBVIOUS ONE. The saver has
            # to go off between the two halves of `ready`: after the word gate
            # (there is no [ss_idle] to write until the kernel is up) and
            # BEFORE the settle (a settle with the saver running can never
            # end - it draws). Writing it after a full `ready` works on a
            # healthy boot and fails on a slow one, which is the worst
            # available split.
            ui.up(limit=limit)
            if not self._kw.get("saver", False):
                os88marty.no_saver(m)
            ui.settle(limit=limit)
        except BaseException:
            self._cm.__exit__(*sys.exc_info())
            raise
        return ui

    def __exit__(self, *e):
        return self._cm.__exit__(*e)


def boot(image, apps=None, machine="os8088_5150_cga", card=None,
         saver=False, why_ibm=None, verbose=True, limit=180.0, **kw):
    """Launch, boot to a settled desktop, turn the saver off, hand back a UI.

    THE THREE LINES 175 SCRIPTS OPEN WITH, and the two things most of them
    forget:

      * `machine` goes through `os88marty.machine`, so an IBM-romset name
        resolves to its GLaBIOS twin unless the row makes a case for the real
        ROM in `why_ibm` (docs/plans/SOAK-PARALLEL.md 5). A row that hardcodes an
        IBM name runs on a container that has the ROM and does not run on one
        that does not, and nothing in its output says which.
      * **the screen saver is turned OFF by default.** It is five GUEST
        minutes of no input, which ought to be unreachable and is not: a row
        that waits on a slow build, or an emulator lane sharing four cores
        with three others, gets there - and what it then compares is a black
        screen. `saver=True` keeps it, for the rows whose subject it is.

    Everything else is `os88marty.launch`'s, passed straight through.
    """
    cm = os88marty.launch(image, apps=apps,
                          machine=os88marty.machine(machine, why_ibm),
                          card=card, **kw)
    return _Booted(cm, card=card, saver=saver, verbose=verbose, limit=limit)


# =============================================================================
# the layer itself
# =============================================================================

class UI:
    """One machine, driven by name.

    `ui.m` is the Marty and `ui.mo` the mouse, so dropping to either layer is
    always available and needs no second connection.
    """

    def __init__(self, m, card=None, verbose=True, mouse=None, sym=None):
        self.m = m
        self.card = card
        self.verbose = verbose
        self.mo = mouse if mouse is not None else Mouse(marty=m, verbose=False)
        # **WHICH KERNEL'S MAP**, and it is not always the Marty's own.
        # `m.sym` answers for the build $OS88_DEFINES / $OS88_BUILD name, which
        # is right for every row that says so in its environment - and WRONG
        # for one that builds a kern_small map IN PROCESS and passes it around
        # as `S`, which tests/dispclose.py does for its `--small` arm. Reading
        # `dsk_vtab` at the big kernel's address then answers garbage, and the
        # symptom is "drive B: has no desktop zone on this machine" about a
        # machine that has one. Every geom call below takes this.
        self.sym = sym if sym is not None else m.sym

    # --- primitives ---------------------------------------------------------
    def _S(self, name):
        return self.sym(name)

    def _word(self, name):
        return _u16(self.m.read(self._S(name), 2))

    def _byte(self, name):
        return self.m.read(self._S(name), 1)[0]

    def _say(self, msg):
        if self.verbose:
            print("      %s" % msg)

    def _wait(self, cond, what, guest, snapshot=None):
        """Poll a guest condition, or raise a UIError that says what it saw.

        `guest` is a budget in the GUEST's own seconds - `os88marty.until`
        anchors its deadline to the emulator's cycle counter - so what a wait
        allows does not shrink when the box is busy. That is the whole of why
        "it passed alone" stopped being a diagnosis here.

        `snapshot` is a callable rendering the state this verb cares about;
        its answer goes in the message. That is the difference between "timed
        out" and "no window called 'MINES' opened - what is open is [...]",
        and it is the whole reason this wrapper exists rather than a bare
        `os88marty.until`.
        """
        try:
            return os88marty.until(self.m, lambda _: cond(), what,
                                   poll=POLL, guest=guest)
        except MartyError as e:
            extra = ""
            if snapshot is not None:
                try:
                    extra = "\n  what the guest shows: %s" % (snapshot(),)
                except Exception as se:              # a snapshot that itself
                    extra = "\n  (could not read the guest: %s)" % (se,)
            raise UIError("%s%s" % (e, extra))

    def settle(self, **kw):
        """`os88marty.settle`, for a test that compares PIXELS.

        Every verb here confirms without it. Reach for this when the next
        thing you do is read the framebuffer, and not as a way of waiting for
        an action to take effect - that is what the verbs already do, faster.
        """
        kw.setdefault("card", self.card)
        return os88marty.settle(self.m, **kw)

    # --- the desktop --------------------------------------------------------
    def up(self, limit=180.0):
        """Wait until the kernel's DESKTOP STATE exists - the first half of
        `ready`, and the half that costs nothing.

        `desk_rows` and `menu_nbar` are written by `desk_rowcalc` and
        `menu_relayout`, and both read 0 until the boot reaches them.
        Measured on a 360KB CGA boot in this container: they go live at 4.4 s
        against a picture that settles at ~19 s.

        So this is NOT a substitute for the settle - a boot has several still
        screens in it and the loading screen between two disk reads is as
        still as a finished desktop. It is what makes the settle safe, and
        the window in between is where the screen saver has to be turned off.
        """
        self._wait(lambda: self._word("desk_rows") and self._word("menu_nbar"),
                   "the kernel to reach the desktop", limit)
        return self

    def ready(self, limit=180.0, saver=False):
        """Both halves: the desktop state, the saver off, then the picture."""
        self.up(limit=limit)
        if not saver:
            os88marty.no_saver(self.m)
        self.settle(limit=limit)
        return self

    # =========================================================================
    # windows
    # =========================================================================
    def windows(self):
        """Every USED window, newest slot last. `os88geom.Win` records."""
        return geom.windows(self.m, self.sym)

    def titles(self):
        return [w.title for w in self.windows()]

    def front(self):
        """The frontmost VISIBLE window, or None - off wm_zord, as wm_top."""
        ptr = geom.top(self.m, self.sym)
        if not ptr:
            return None
        i = (ptr - self._S("wm_wins")) // geom.WIN_SIZE
        for w in self.windows():
            if w.i == i:
                return w
        return None

    def window(self, title, among=None):
        """The window called `title`, matched leniently and reported loudly.

        Exact (case-insensitive) first, then prefix, then substring - so
        `ui.window("Mines")` finds "Mines" and "Mines 1" alike, and an
        ambiguous match RAISES rather than picking one. A title is what the
        window record points at (W_TITLE through W_SEG), so a package's own
        caption is read out of the package's segment and not guessed.
        """
        wins = self.windows() if among is None else among
        t = title.upper()
        for pick in (lambda s: s == t,
                     lambda s: s.startswith(t),
                     lambda s: t in s):
            hit = [w for w in wins if pick(w.title.upper())]
            if len(hit) == 1:
                return hit[0]
            if len(hit) > 1:
                raise UIError(
                    "%r matches %d windows - %r. Give more of the title."
                    % (title, len(hit), [w.title for w in hit]))
        raise UIError("no window called %r is open. What is open: %r"
                      % (title, [w.title for w in wins]))

    def wait_window(self, title, limit=T_WINDOW, among=None):
        """`window`, but wait for it to appear first."""
        box = {}

        def got():
            try:
                box["w"] = self.window(title, among=among)
                return True
            except UIError:
                return False

        self._wait(got, "a window called %r to open" % title, limit,
                   snapshot=self.titles)
        return box["w"]

    def raise_window(self, w, limit=T_RAISE):
        """Bring a window to the front, and PROVE it came.

        FREE WHEN IT IS ALREADY THERE, which is most calls: the check is one
        read of wm_zord and no click at all. That matters more than it looks -
        a redundant title-bar click is a press and a release on a live window,
        and a press on a title bar is the first half of a DRAG.

        THE AIM IS A POINT THAT IS ACTUALLY ON THE GLASS, and that is the part
        every hand-written version gets wrong. The window record says where
        the title bar is and says nothing about whether anything is on top of
        it - so `click(w.x + w.w // 2, w.y + TITLE_H // 2)` on a window that
        is behind another one clicks THE OTHER ONE, raises that instead, and
        the script goes on believing it raised what it asked for. Measured
        here on the second run of this file: Calculator behind a Disk window,
        the click landed on the Disk window, and the wait then reported the
        thing the click had actually done.

        So: walk the title bar for a column no higher window covers, avoiding
        both boxes (SPEC.md 11's WM_BOX_*, `os88geom.close_xy`'s other end),
        and click there. If the bar is wholly covered - or the window is
        MINIMIZED and has no bar on the screen at all - use the dock tile,
        which is always visible and whose click "does whatever its own mark
        says is not true yet" (SPEC.md 30): for a window that is neither
        front nor minimized, that is a raise.
        """
        w = self._as_win(w)
        top = self.front()
        if top is not None and top.i == w.i:
            return w
        pt, how = self._raise_point(w)
        self.mo.click(pt[0], pt[1], settle=0)
        self._wait(lambda: (lambda f: f is not None and f.i == w.i)(self.front()),
                   "window %r to come to the front (clicked its %s at %r)"
                   % (w.title, how, pt), limit,
                   snapshot=lambda: "front is %r, z-order bottom-first is %r"
                   % (getattr(self.front(), "title", None),
                      [self._title_of(i) for i in geom.zorder(self.m, self.sym)]))
        return self._refresh(w)

    def _title_of(self, slot):
        for o in self.windows():
            if o.i == slot:
                return o.title
        return "?%d" % slot

    def _raise_point(self, w):
        """((x, y), what) - somewhere clickable that will bring `w` forward."""
        z = geom.zorder(self.m, self.sym)
        above = []
        if w.i in z:
            over = z[z.index(w.i) + 1:]
            byslot = {o.i: o for o in self.windows()}
            above = [byslot[i] for i in over if i in byslot and byslot[i].visible]
        y = w.y + geom.TITLE_H // 2
        # ...between the two boxes, and stepped in 4s from the middle
        # outwards, so a bar that is only partly covered is still hit near
        # its centre where a stray pixel of a neighbour cannot matter.
        lo = w.x + geom.WM_BOX_X0 + geom.WM_BOX_W + 4
        hi = w.x + w.w - 1 - geom.WM_BOX_X0 - geom.WM_BOX_W - 4
        if w.visible and hi > lo:
            mid = (lo + hi) // 2
            span = [mid]
            for d in range(4, (hi - lo) // 2 + 4, 4):
                span += [mid - d, mid + d]
            for x in span:
                if lo <= x <= hi and not any(o.covers(x, y) for o in above):
                    return (x, y), "title bar"
        try:
            return geom.tile_xy(self.m, w, self.sym), "dock tile"
        except geom.GeomError as e:
            raise UIError(
                "window %r has no clickable way to the front: its title bar "
                "is wholly covered by %r%s, and %s"
                % (w.title, [o.title for o in above],
                   "" if w.visible else " (and it is not visible)", e))

    def close(self, w, limit=T_RAISE):
        """Click the close box and wait for the record to go free.

        **THE CLICK IS RE-SENT ONCE**, and the gate for that is the exact
        negation of what we are waiting for: if the window is STILL OPEN the
        click did nothing, and clicking the same close box again is harmless.
        A close box whose window has gone is a click on whatever is underneath
        it, which is why this may not simply retry on a timer.

        Every packet of `mo.click` is already proven - the pointer against the
        published cursor, both edges against the guest's own `mouse_btn` - and
        the gesture as a whole still is not: what is missing is the window
        manager RECEIVING it, which no fact about the mouse can show. Measured
        at a lane of four, this was 1 run in 12 (docs/plans/SOAK-PARALLEL.md
        15.2), and `move_window` above has the same hole with a kernel witness
        to close it.
        """
        w = self._as_win(w)
        self.raise_window(w)
        # **THE RECT IS RE-READ HERE**, and that is not belt and braces. The
        # caller's handle is a SNAPSHOT: `wait_window` answers the instant a
        # record goes visible, which is before the app has finished laying
        # itself out, and `drag_window` hands back a new object that an older
        # variable does not become. Clicking `close_xy` of a rect the window
        # has since left is two confirmed clicks on empty desktop - which is
        # exactly what a lane of four produced here, twice in twelve runs,
        # with the retry below firing and the second click missing for the
        # same reason as the first.
        w = self._refresh(w)
        x, y = geom.close_xy(w.x, w.y)
        gone = lambda: all(o.i != w.i for o in self.windows())    # noqa: E731
        for attempt in (1, 2):
            self.mo.click(x, y, settle=0)
            try:
                self._wait(gone, "window %r to close" % w.title,
                           limit if attempt == 2 else min(limit, T_SHORT),
                           snapshot=self.titles)
                if attempt == 2:
                    self._say("the close of %r needed a second click"
                              % w.title)
                return
            except UIError:
                if attempt == 2 or gone():
                    raise

    def move_window(self, w, x, y, limit=T_MOVE):
        """Drag a window's title bar so its FRAME lands at (x, y), and check.

        THE CHECK IS THE POINT. A drag is a press, a walk and a release, the
        UART may drop any of them, and the window manager may refuse the
        destination - SPEC.md 11 clamps a drag so the title bar stays
        reachable. All three failures leave a window somewhere other than
        asked with nothing raised, and the classic symptom is a screenshot
        comparison against a rect the window is not in.

        So it reads the record back, and if the window did not arrive it says
        where it actually is. It does NOT retry: a clamp is not a dropped
        packet and retrying a clamp is an infinite loop with a timeout on it.
        """
        w = self._as_win(w)
        self.raise_window(w)
        was = (w.x, w.y)
        # WHERE IT WILL ACTUALLY LAND, not where we asked. SPEC.md 11.94 snaps
        # a frame's x so its CONTENT origin is a multiple of 8, so a request
        # for 195 lands at 191 - deterministically, every time. A check
        # written against the request fails on a window manager doing exactly
        # what the spec says, and that is a false failure this layer exists to
        # stop rather than one to inherit.
        want = (geom.snapx(x, bool(w.flags & geom.WF_NOSNAP)), y)
        gx = w.x + w.w // 2
        gy = w.y + geom.TITLE_H // 2
        self._grab(w, gx, gy)
        self.mo.to(gx + (x - w.x), gy + (y - w.y), l=True)
        self.mo._edge(False)
        try:
            self._wait(lambda: self._rect(w.i)[:2] == want,
                       "window %r to arrive at (%d,%d)"
                       % ((w.title,) + want), limit)
        except UIError:
            got = self._rect(w.i)[:2]
            if got == was:
                raise UIError(
                    "window %r did not move at all: it is still at (%d,%d) "
                    "after a drag to (%d,%d). The press, the walk or the "
                    "release was dropped by the 1200-baud UART, or the title "
                    "bar was not where the record said it was."
                    % ((w.title,) + was + want))
            raise UIError(
                "window %r was dragged towards (%d,%d) - which SPEC.md 11.94 "
                "snaps to (%d,%d) - and landed at (%d,%d). It MOVED, so the "
                "gesture arrived; the window manager put it somewhere else. "
                "wm_land_fit and wm_dock_snap both refuse a destination, and "
                "a window may not be dragged off its display."
                % ((w.title, x, y) + want + got))
        return self._refresh(w)

    def _grab(self, w, gx, gy, guest=T_MOVE):
        """Press on a window's title bar, and CONFIRM the drag was taken.

        **THE STEP `mo.drag` CANNOT DO.** Its press and release are each
        proven against the guest's own `mouse_btn`, and the pointer is proven
        against the published cursor - so every packet is confirmed and the
        gesture as a whole is still not. What is missing is between them: the
        window manager has to RECEIVE the press and enter `ui_drag` before the
        pointer walks off the title bar. Nothing in the mouse layer can see
        that, because it is not a fact about the mouse.

        `ui_drag`'s first act is `mov [ui_dragwin], bx` (kernel/ui.inc), and
        it then runs a modal loop until the release - so that word naming our
        record IS the confirmation, and it is the kernel's own rather than a
        second opinion.

        It is ZEROED FIRST, which is what makes it exact. The word is not
        cleared when a drag ends, so a second drag of the SAME window would
        read the first one's value and confirm a press that never landed -
        the precise failure this is here to catch. Nothing reads it while the
        button is up, so clearing it costs the guest nothing.

        Measured: `uilayer` failed at a lane of four with `drag +20+12 from
        (175, 38) -> (175, 38)` - a window that did not move, from a gesture
        every packet of which was confirmed (docs/plans/SOAK-PARALLEL.md 15.2).
        """
        rec = (self._S("wm_wins") + w.i * geom.WIN_SIZE
               - geom.KERNEL_SEG * 16) & 0xFFFF
        self.mo.to(gx, gy)
        if self.mo.where()[2] & 1:      # a press that finds the button down
            self.mo._edge(False)        # is no edge at all
        self.m.write(self._S("ui_dragwin"), b"\x00\x00")
        self.mo._edge(True)
        self._wait(
            lambda: self._word("ui_dragwin") == rec,
            "the window manager to take a title-bar press on %r (ui_dragwin "
            "= %#06x)" % (w.title, rec), guest,
            snapshot=lambda: "ui_dragwin = %#06x, mouse_btn = %02x, pointer "
                             "%r, press aimed at (%d,%d)"
                             % (self._word("ui_dragwin"), self.mo.where()[2],
                                self.mo.where()[:2], gx, gy))

    def drag_window(self, w, dx, dy, limit=T_MOVE):
        """move_window by a DELTA. The spelling most callers mean."""
        w = self._as_win(w)
        return self.move_window(w, w.x + dx, w.y + dy, limit=limit)

    # --- window bookkeeping -------------------------------------------------
    def _as_win(self, w):
        """Take a Win, a slot index or a TITLE. Callers pass all three."""
        if isinstance(w, str):
            return self.window(w)
        if isinstance(w, int):
            for o in self.windows():
                if o.i == w:
                    return o
            raise UIError("window slot %d is not in use. Open: %r"
                          % (w, self.titles()))
        return w

    def _refresh(self, w):
        for o in self.windows():
            if o.i == w.i:
                return o
        raise UIError("window %r (slot %d) went away while we were using it"
                      % (w.title, w.i))

    def _rect(self, slot):
        return geom.win_rect(self.m, slot, self.sym)

    # =========================================================================
    # drives, folders and files (SPEC.md 19, 22, 26)
    # =========================================================================
    def _fsblk(self, w):
        """The linear address of a window's own KD_POOL state block, or None.

        window slot -> wm_owner -> instance -> I_SPTR, which is the route the
        kernel itself keeps. Everything that used to go through [fm_vp] takes
        this instead, for the reason `fs_of` gives: [fm_vp] does not follow a
        FRONT, so it can name a different window from the one being asked
        about.
        """
        i = w.i if hasattr(w, "i") else w
        inst = self.m.read(self._S("wm_owner"), geom.MAX_WIN)[i]
        if inst == 0xFF:
            return None
        rec = self._S("inst_tab") + inst * geom.I_RECSZ
        blk = _u16(self.m.read(rec + geom.I_SPTR, 2))
        return None if not blk else (geom.KERNEL_SEG << 4) + blk

    def fs_of(self, w):
        """A Disk window's OWN state block, or None if it is not one.

        (FS_DRV, FS_CWD, FS_MOK) - the volume it shows, the folder, and
        whether that listing came from a good mount.

        **IT ASKS THE WINDOW, NOT [fm_vp].** `fm_vp_set` publishes "the
        acting window" at fm_kinit and on a navigation - and NOT when
        `fm_choose` FRONTS a window that is already showing what was asked
        for, which is one of its three outcomes (kernel/files.inc: that
        branch is gfx_lock / inst_unmin / wm_show / gfx_unlock and no more).
        So after fronting, [fm_vp] still names whatever acted last, and a
        post-condition written on it is asking the wrong window. hdmove has
        two Disk windows and read `drive 2` about the one showing B:.

        The route is the one the kernel keeps: window slot -> wm_owner ->
        instance -> I_SPTR, which is that kind's KD_POOL block (FS_SIZE
        wide for the Disk kind).
        """
        base = self._fsblk(w)
        if base is None:
            return None
        b = self.m.read(base + geom.FS_CWD, geom.FS_MOK - geom.FS_CWD + 1)
        return (b[geom.FS_DRV - geom.FS_CWD], _u16(b, 0),
                b[geom.FS_MOK - geom.FS_CWD])

    def uncover(self, x, y, how="move", limit=T_RAISE):
        """Close whatever visible windows cover (x, y), topmost first.

        THE SCALPEL VERSION OF `clear_desktop`, and usually the right one: a
        desktop zone is behind every window, so what a row needs is that ONE
        point on the glass - not a bare desktop. hdmove is the worked example.
        It needs the hard disk's zone clickable and it needs the Heap
        Compaction window it opened earlier still there; clearing the desktop
        got it the first and cost it the second, and the row then failed
        several steps later on "window slot 1 has no instance, so no dock
        tile".

        **IT MOVES RATHER THAN CLOSES**, which is what a user would do and is
        the difference between freeing a zone and editing the experiment. A
        closed window takes its slot, its instance and its dock tile with it,
        and hdmove needed all three afterwards - closing B:'s window to reach
        C:'s zone left the row aiming at a stale slot and reading the wrong
        volume's root. `how="close"` is there for a caller that means it.

        Answers what it did, so a caller can say so.
        """
        did = []
        for _ in range(geom.MAX_WIN + 1):
            over = [w for w in self.windows() if w.visible and w.covers(x, y)]
            if not over:
                return did
            w = over[-1]
            if how == "close":
                did.append("closed %r" % w.title)
                self.close(w, limit=limit)
                continue
            # LEFT, far enough that its right edge clears the point. Desktop
            # zones live at the right of the screen (SPEC.md 26.1 fills a
            # column downwards and wraps to a NEW column on the LEFT), so
            # left is the direction with room in it.
            nx = x - w.w - 8
            if nx < 0:
                did.append("closed %r (no room to move it)" % w.title)
                self.close(w, limit=limit)
                continue
            did.append("moved %r to x=%d" % (w.title, nx))
            self.move_window(w, nx, w.y, limit=T_MOVE)
        raise UIError("could not uncover (%d,%d) - still covered by %r"
                      % (x, y, [w.title for w in self.windows()
                                if w.visible and w.covers(x, y)]))

    def clear_desktop(self, limit=T_RAISE):
        """Close every visible window, and PROVE the desktop is bare.

        **A DESKTOP ZONE IS BEHIND EVERY WINDOW**, so a drive whose zone a
        window happens to cover cannot be opened at all - the double-click
        goes to the window. `open_drive` names that rather than timing out,
        and this is what a row does about it.

        It is a verb here rather than a loop in each row because three of them
        wrote it: rdmove and hdmove both left a Disk window over the zone they
        needed next, and hdmove swallowed the resulting exception for four
        candidate drives and reported "the hard disk mounted no browsable
        volume" - which named neither the drive that exists nor the window in
        front of it.

        It is deliberately NOT what `open_drive` does on its own: closing a
        window is a change to state a test may be measuring, and a harness
        that quietly tidied the desktop would be editing the experiment.
        """
        for _ in range(geom.MAX_WIN + 1):
            wins = [w for w in self.windows() if w.visible]
            if not wins:
                return
            self.close(wins[-1], limit=limit)
        raise UIError("could not clear the desktop - still open: %r"
                      % self.titles())


    def open_drive(self, letter="B", limit=T_WINDOW):
        """Double-click drive `letter`'s desktop zone; answer its Disk window.

        BY LETTER, NEVER BY ORDINAL. A zone's position is its volume's place
        among the SHOWN ones, so a machine whose B: was retired by SPEC.md
        18.97's probe, or which mounts a hard disk, numbers them differently -
        and a remembered ordinal then double-clicks the wrong drive or bare
        desktop. `os88geom.drive_pt` walks `dsk_vtab`, so "drive C: has no
        desktop zone on this machine" is what comes back instead of a window
        that never opened.
        """
        try:
            x, y = geom.drive_pt(self.m, letter, self.sym)
        except geom.GeomError as e:
            raise UIError(str(e))
        drv = (ord(letter.upper()) - ord("A") if isinstance(letter, str)
               else letter)

        # **A NEW WINDOW IS ONLY ONE OF THREE OUTCOMES**, and requiring it is
        # what a first version did. desk_click_x's own contract:
        # `files_open_drive` "fronts a window already showing that drive's
        # root, OR opens one, OR at the cap moves the front one" (SPEC.md
        # 22.1) - so on a machine that already has B: open, the correct
        # behaviour is a RAISE and there is no new window to wait for. That
        # cost dispprefer and rdmove a run each, and dispprefer's own comment
        # has said "open_drive RAISES an existing" for longer than this layer
        # has existed.
        #
        # The post-condition covers all three in one statement: the ACTING
        # Disk window shows this drive, at its root. FS_DRV is the volume and
        # FS_CWD == 0 is a root (kernel/files.inc's only clear site). It is
        # also correct for the legitimate NO-OP - a double-click on a drive
        # already showing its root changes nothing, and the condition is
        # already true.
        #
        # **FS_MOK IS THE THIRD TERM AND IT IS NOT OPTIONAL.** dsk_chdir
        # records the drive and the folder BEFORE the listing exists, so the
        # first two alone are satisfied by a window that has not listed yet -
        # and the caller's very next line then reads an EMPTY folder. fcpsmall
        # found it: `B:\ = []` on a disk with four entries. That gap is what
        # the settle this replaces was accidentally covering, which is a good
        # illustration of why a confirmation has to name the whole
        # post-condition and not the first part of it that becomes true.
        #
        # FS_MOK ("that listing came from a good mount") and not `FS_N > 0`,
        # because an EMPTY VOLUME is a legitimate answer and would never
        # satisfy the second.
        def there():
            # THE FRONT WINDOW'S OWN STATE, asked of the window rather than of
            # [fm_vp] - see `fs_of`, because fronting an already-open window
            # does not republish the acting one. VISIBLE is part of it too:
            # FS_DRV/FS_CWD/FS_MOK are the file manager's state and say
            # nothing about the window manager's, so a window that is USED but
            # not yet WF_VIS satisfies all three while the caller's next line
            # reads an empty window list (hdmove: `HIDDEN+saveu`).
            f = self.front()
            if f is None or not f.visible:
                return False
            fs = self.fs_of(f)
            return fs is not None and fs == (drv, 0, 1)

        # **IS THE ZONE ACTUALLY ON THE GLASS?** A desktop zone is BEHIND every
        # window, so one sitting over it eats the double-click and the drive
        # never opens - and the only symptom is a wait that ends. This is the
        # same hazard `raise_window` handles for title bars, one layer down,
        # and it is worth naming rather than timing out: rdmove met it with a
        # "Heap Compaction" window over the RAM disk's zone.
        over = [w for w in self.windows()
                if w.visible and w.covers(x, y)]
        if over and not there():
            raise UIError(
                "drive %s:'s desktop zone at (%d,%d) is COVERED by %r, so a "
                "double-click there goes to that window and the drive cannot "
                "open. Close or move it first - a desktop zone is behind "
                "every window and there is no way to click through one."
                % (letter.upper(), x, y, [w.title for w in over]))

        self.mo.dblclick(x, y, settle=0)
        self._wait(there, "a Disk window showing %s: at its root"
                   % letter.upper(), limit,
                   snapshot=lambda: "front is %r %s; [fm_vinst] = %04X; "
                   "every window: %r"
                   % (getattr(self.front(), "title", None),
                      self.fs_of(self.front()) if self.front() else None,
                      self._word("fm_vinst"),
                      [(w.title, self.fs_of(w)) for w in self.windows()]))
        # **THE WINDOW THIS VERB OPENED IS THE ONE IT CONFIRMED**, and that
        # is `front()`. It used to hand back `disk_window()` - the ACTING one -
        # which is a different question and one the kernel is entitled to
        # answer with "nobody": [fm_vinst] is the window's OWNER instance and
        # `fm_vp_set` writes 0 for an unowned one (kernel/files.inc says so at
        # the store). `dispprefer` reached that state in its seventh section,
        # with seven windows up and four of them file windows, and the verb
        # raised "no Disk window is acting" about a wait that had just
        # succeeded on a Disk window showing B: at its root.
        w = self.front()
        self._say("open_drive %s: -> %r at (%d,%d)"
                  % (letter.upper(), w.title, w.x, w.y))
        return w

    def _wait_new(self, before, what, limit):
        box = {}

        def got():
            new = [w for w in self.windows() if w.i not in before and w.visible]
            if new:
                box["w"] = new[-1]
                return True
            return False

        self._wait(got, what + " to open", limit, snapshot=self.titles)
        return box["w"]

    # --- the listing --------------------------------------------------------
    def disk_window(self):
        """The ACTING Disk window - the one [fm_vp] names, not the front one.

        THE TWO ARE NOT THE SAME AND ASSUMING SO IS A REAL BUG. `fm_vp_set`
        runs on a file-manager raise and on its navigations; nothing calls it
        when a Calculator comes forward, so [fm_vp] goes on naming the last
        Disk window while `front()` names something else entirely. A row
        computed off `front()` there aims at rows inside the CALCULATOR.

        It is resolved through [fm_vinst] -> I_WIN, which is the link the
        kernel itself keeps, rather than by guessing which open window looks
        like a Disk window by its size - which is what three scripts in this
        tree do, each with its own heuristic.
        """
        vi = self._word("fm_vinst")
        if not vi:
            # NOBODY IS ACTING, which is a state and not a failure: [fm_vinst]
            # is the acting window's OWNER instance and `fm_vp_set` stores 0
            # for an unowned window. So fall back to the front window WHEN IT
            # IS A FILE WINDOW - the docstring's warning is that front and
            # acting are different, and with nothing acting there is no other
            # answer to disagree with.
            f = self.front()
            if f is not None and f.visible and self.fs_of(f) is not None:
                return f
            raise UIError("no Disk window is acting - [fm_vinst] is 0 and the "
                          "front window %r is not a file window either, so "
                          "nothing has opened one on this machine yet"
                          % (getattr(f, "title", None),))
        ptr = _u16(self.m.read((geom.KERNEL_SEG << 4) + vi + geom.I_WIN, 2))
        if not ptr:
            raise UIError("[fm_vinst] names an instance with no window")
        i = (ptr - self._S("wm_wins") + (geom.KERNEL_SEG << 4)) \
            // geom.WIN_SIZE
        for o in self.windows():
            if o.i == i:
                return o
        raise UIError("[fm_vinst] names window slot %d, which is not in use. "
                      "Open: %r" % (i, self.titles()))

    def listing(self, win=None):
        """[(name, type)] of what a Disk window is SHOWING, in display order.

        Type is SPEC.md 19.1's: 1 = loadable package, 2 = subdirectory,
        3 = the parent link, 0 = inert.

        The window's OWN cache first and the global snapshot only as a
        fallback - SPEC.md 22.1 has paints read the cache and only actions
        re-sync the globals, and SPEC.md 18.9's quiet mount deliberately
        leaves `disk_nfiles` at 0. Reading the globals there answers "this
        folder is empty" about a window with a dozen rows on it.
        """
        # THE WINDOW'S OWN BLOCK when one is named, [fm_vp] otherwise. A
        # raise does not republish the acting window (see `fs_of`), so a
        # caller that named a window and got [fm_vp]'s listing was reading a
        # different window's folder - hdmove asked its B: window for
        # HEAPFRAG.O88 and was told the C: root's contents.
        base = self._fsblk(win) if win is not None else None
        if base is None:
            vp = self._word("fm_vp")
            base = (geom.KERNEL_SEG << 4) + vp if vp else None
        if base is not None:
            n = _u16(self.m.read(base + geom.FS_N, 2))
            vseg = _u16(self.m.read(base + geom.FS_VSEG, 2))
            if n and vseg:
                return _decode(self.m.read(vseg << 4,
                                           n * geom.DSK_DE_STRIDE), n)
        n = self._word("disk_nfiles")
        if not n:
            return []
        seg = self._word("dsk_dseg")
        off = self._word("dsk_doff")
        return _decode(self.m.read((seg << 4) + off,
                                   n * geom.DSK_DE_STRIDE), n)

    def entry(self, name, win=None):
        """(index, type) of `name` in `win` (or the acting window), or raise
        saying what the folder does hold."""
        rows = self.listing(win)
        for i, (nm, ty) in enumerate(rows):
            if nm.upper() == name.upper():
                return i, ty
        raise UIError("%r is not in this folder. It holds %r"
                      % (name, [r[0] for r in rows]))

    def scroll(self, win=None):
        """[FS_SCRL] - the first entry `win` (or the acting window) shows."""
        base = self._fsblk(win) if win is not None else None
        if base is None:
            vp = self._word("fm_vp")
            if not vp:
                return 0
            base = (geom.KERNEL_SEG << 4) + vp
        return _u16(self.m.read(base + geom.FS_SCRL, 2))

    def scroll_to(self, entry, limit=T_NAV, win=None):
        """Bring directory entry `entry` on screen; answer its VISIBLE row.

        NO `fit` ARITHMETIC ANYWHERE. How many rows a Disk window shows
        depends on its height, the adapter and the view mode; instead this
        walks with the arrow keys (SPEC.md 22.11) and reads [FS_SCRL] BACK, so
        the clamp at the end of a list is computed by the only thing that
        knows. Keys rather than the scroll bar because the bar's cells are
        five nested layouts deep and a key is a key.
        """
        if entry < 0:
            raise UIError("entry %d is not a row" % entry)

        def step(key):
            """One arrow, then wait for [FS_SCRL] to move. Did it?

            THE BUDGET IS THE GUEST'S OWN CLOCK, not the host's, and that
            matters here more than almost anywhere else in this file: a
            "no" from this routine is read as THE END STOP, so a wait cut
            short by a busy box does not fail - it silently decides the list
            has run out, and the click that follows lands on the wrong row.
            That is docs/plans/HANDOFF-SOAK-FINDINGS.md B5's mechanism exactly, and
            the version this replaces had a 3.0-second `time.sleep` loop.

            Polling the WORD rather than settling on the picture: a settle is
            two identical frames a second apart and there are up to a dozen of
            these, which used to turn one navigation into half a minute. The
            word is what the answer is computed from anyway.
            """
            was = self.scroll(win)
            self.m.key(key)
            c0 = self.m.status()["cycles"]
            while True:
                if self.scroll(win) != was:
                    return True
                spent = (self.m.status()["cycles"] - c0) / os88marty.GUEST_HZ
                if spent >= T_STEP:
                    return False
                time.sleep(POLL)

        for _ in range(40):                 # to the top first, so the walk
            if self.scroll(win) == 0:       # below is one-directional and
                break                       # cannot oscillate
            if not step("ArrowUp"):
                break
        else:
            raise UIError("the list would not scroll to the top")
        for _ in range(40):
            if self.scroll(win) >= entry:
                break
            if not step("ArrowDown"):       # the END STOP: it clamped, so
                break                       # `entry` is as visible as it gets
        else:
            raise UIError("entry %d never came on screen" % entry)
        row = entry - self.scroll(win)
        if row < 0:
            raise UIError("scrolled PAST entry %d - the list moved under us"
                          % entry)
        return row

    def row_xy(self, win, row):
        """The centre of visible row `row` in a Disk window (SPEC.md 22)."""
        return (win.x + 1 + FM_ROW_X,
                win.y + geom.TITLE_H + 1 + geom.FM_ROW_Y0
                + row * geom.FM_ROW_H + geom.FM_ROW_H // 2)

    def open(self, name, expect="auto", limit=None, win=None):
        """Open the entry called `name` in the front Disk window.

        **THE ONLY WAY A TEST SHOULD NAME A FILE**, and what it waits for
        depends on the entry's TYPE, read before anything is clicked:

          * a **package** (type 1) opens a WINDOW, so this waits for one and
            returns it. If none arrives it reads `ld_status` and says which of
            SPEC.md 21's refusals happened, instead of timing out on a window
            that was never going to exist.
          * a **folder** (types 2 and 3, the synthesized `..` included)
            navigates IN PLACE, so this waits for the listing to change and
            returns the same window.
          * a **type-0 entry** is the interesting one, and it is NOT inert:
            SPEC.md 54's file association is what makes `DEMO.HTM` open the
            browser. So it is waited on exactly like a package, and only the
            failure message differs - "no association" is the diagnosis, and
            it is the one a caller would otherwise spend a session reaching.

        `expect` overrides that reading when a test knows better:

          * `"window"` / `"nav"` - force one of the two;
          * `"refusal"` - require that NO window opens, and report `ld_status`
            as the answer. A row testing a package that refuses ITSELF wants
            this, and it is a stronger assertion than the blind settle it
            replaces: today such a row cannot tell a refusal from a launch
            that was simply slow;
          * `None` - click and return at once, confirming nothing. The escape
            hatch, and worth a comment at every use.

        Scrolling is done for you: SPEC.md 19.4 sorts by name, a folder that
        gains an entry renumbers every row after it, and one that outgrows the
        window puts the row below the fold - where a click lands on whatever
        is drawn at that y, or on nothing.
        """
        if expect not in ("auto", "window", "nav", "refusal", None):
            raise UIError("expect=%r is not one of auto/window/nav/refusal/None"
                          % (expect,))
        # THE ACTING DISK WINDOW, AND RAISED FIRST. `scroll_to` walks with the
        # arrow keys and a key only reaches the frontmost window, so a caller
        # that had something else in front used to scroll nothing and then
        # double-click whatever was drawn at that y. Five scripts in this tree
        # carry their own `raise_win` for exactly this, one of them with a
        # docstring about the symptom: "the listing simply does not change,
        # and the NEXT open says the file it wants is not in this folder while
        # naming the folder it never left."
        # `win` NAMES WHICH DISK WINDOW, and a row with two of them needs to
        # say. Raising it is what makes it the acting one - fm_vp_set runs on
        # a raise - so naming it and raising it are the same act. Without it
        # this takes whichever window acted last, which is right for the one
        # -window case and silently wrong for hdmove, which has B: and C: open
        # and meant B:.
        win = self.raise_window(win if win is not None else self.disk_window())
        idx, ty = self.entry(name, win)
        want = expect
        if want == "auto":
            want = "nav" if ty in (2, 3) else "window"
        row = self.scroll_to(idx, win=win)
        x, y = self.row_xy(win, row)
        before = {w.i for w in self.windows()}
        was = [r[0] for r in self.listing(win)]
        self.mo.dblclick(x, y, settle=0)

        if want is None:
            return None

        if want == "window":
            lim = limit if limit is not None else T_WINDOW
            try:
                w = self._wait_new(before, "%r's window" % name, lim)
            except UIError as e:
                raise UIError("%s\n  %s%s" % (e, self._load_failure(name),
                                               _assoc_note(name, ty)))
            self._say("open %s -> %r" % (name, w.title))
            return w

        if want == "refusal":
            lim = limit if limit is not None else T_NAV
            try:
                w = self._wait_new(before, "%r's window" % name, lim)
            except UIError:
                self._say("open %s refused: %s" % (name,
                                                   self._load_failure(name)))
                return None
            raise UIError(
                "%r was expected to REFUSE and it opened %r instead "
                "(ld_status = %d). A row asserting a refusal has to fail on a "
                "launch, or it passes on the very regression it guards."
                % (name, w.title, self._byte("ld_status")))

        lim = limit if limit is not None else T_NAV
        self._wait(lambda: [r[0] for r in self.listing(win)] != was,
                   "the folder %r to open" % name, lim,
                   snapshot=lambda: "the listing is still %r" % (was,))
        self._say("open %s -> %d entries" % (name, len(self.listing(win))))
        return self._refresh(win)

    def _load_failure(self, name):
        """What SPEC.md 21 says about the launch that produced no window."""
        try:
            st = self._byte("ld_status")
        except Exception:
            return "(no ld_status in this kernel)"
        why = {0: "the loader reports SUCCESS, so the package ran and put up "
                  "no window - which is a package question, not a load one",
               1: "LD_EDISK: disk_read failed",
               2: "LD_EBAD: not a valid package (entry or header)",
               3: "LD_EBIG: too large (file, image, or image+bss)",
               4: "LD_EABORT: the package's entry returned CF=1 - it refused "
                  "ITSELF, and its own toast says why",
               5: "LD_ENOMEM: the instance table or the pool is exhausted"}
        return "ld_status = %d for %s: %s" % (
            st, name, why.get(st, "an unknown loader status"))

    def path(self, spec, limit=None):
        """A whole navigation: `ui.path("B:/APPS/MINES.O88")`.

        The drive is optional - `ui.path("APPS/MINES.O88")` starts from the
        ACTING Disk window (`disk_window`, not the front one) - and every step
        is `open`, so every step confirms.
        """
        parts = [p for p in spec.replace("\\", "/").split("/") if p]
        if parts and len(parts[0]) == 2 and parts[0][1] == ":":
            w = self.open_drive(parts[0][0])
            parts = parts[1:]
        else:
            w = self.disk_window()
        for p in parts:
            w = self.open(p, limit=limit)
        return w

    # =========================================================================
    # the toast strip (SPEC.md 59)
    # =========================================================================
    def toast(self):
        """(text, showing) - the STAGED message and whether a strip is up.

        Two separate facts, and reading only one of them is the mistake this
        exists to stop. `toast_buf` is what was last staged and it is NOT
        cleared when the strip goes away - so a script polling the buffer
        alone cannot tell "the machine is saying X" from "the machine said X a
        minute ago". [toast_on] is that second bit.

        Twenty-one scripts in this tree read the toast and each wrote its own
        24-byte read; TOAST_MAX is the tight constant SPEC.md 59 warns about,
        so a local copy that is one short truncates the one message it was
        added for.
        """
        n = geom.TOAST_MAX
        buf = bytes(self.m.read(self._S("toast_buf"), n + 1))
        return (buf.split(b"\0")[0].decode("latin-1"),
                bool(self._byte("toast_on")))

    def wait_toast(self, says=None, unlike=None, limit=T_NAV):
        """Wait for the toast to say something, and answer what it says.

        `says` is a substring the message must contain; `unlike` is one it
        must stop containing - which is how a script waits for a LONG
        operation whose message changes when it finishes, without needing to
        know what the next message will be.
        """
        box = {}

        def got():
            txt, on = self.toast()
            box["t"] = txt
            if says is not None:
                return says.lower() in txt.lower()
            if unlike is not None:
                return unlike.lower() not in txt.lower()
            return bool(on and txt)

        what = ("the toast to say %r" % says if says is not None else
                "the toast to stop saying %r" % unlike if unlike is not None
                else "any toast")
        self._wait(got, what, limit,
                   snapshot=lambda: "toast_buf = %r, toast_on = %s"
                   % self.toast())
        return box["t"]

    # =========================================================================
    # the menu bar (SPEC.md 12)
    # =========================================================================
    def menus(self):
        """The LIVE bar, decoded out of menu_bar[]: [(title, x0, x1, items)].

        menu_bar[] is rebuilt by `menu_layout` on every raise, so this is the
        bar for the window that is actually frontmost - which is why a menu
        picked by name cannot land on the previous window's menu, and a menu
        picked by coordinate routinely does.

        It lives in `.lowbss` and is read through SS by the kernel; os88sym
        knows that and answers a linear address, so nothing here has to.
        Cell 0's title pointer is 0, meaning the logo glyph - it is named
        'Apple' here because that is what every script calls it, and the
        System menu answers to both.
        """
        n = self._word("menu_nbar")
        raw = self.m.read(self._S("menu_bar"), max(n, 1) * geom.MB_ENTSZ)
        out = []
        for i in range(n):
            b = i * geom.MB_ENTSZ
            tp = _u16(raw, b + geom.MB_TITLE)
            ip = _u16(raw, b + geom.MB_ITEMS)
            ni = _u16(raw, b + geom.MB_NITEM)
            x0 = _u16(raw, b + geom.MB_XL)
            x1 = _u16(raw, b + geom.MB_XR)
            seg = _u16(raw, b + geom.MB_SEG) or geom.KERNEL_SEG
            title = "Apple" if not tp else self._str(seg, tp)
            out.append((title, x0, x1, self._items(seg, ip, ni)))
        return out

    def _str(self, seg, off, n=40):
        return bytes(self.m.readseg(seg, off, n)).split(b"\0")[0] \
                    .decode("latin-1")

    def _items(self, seg, ptr, n):
        """[(text, enabled)] for one cell. A leading byte 1 is MENU_DIS."""
        if not ptr or not n:
            return []
        raw = self.m.readseg(seg, ptr, n * 2)
        out = []
        for i in range(n):
            p = _u16(raw, i * 2)
            if not p:
                out.append(("", False))
                continue
            s = self._str(seg, p)
            if s[:1] == chr(geom.MENU_DIS):
                out.append((s[1:], False))
            else:
                out.append((s, True))
        return out

    def menu_pick(self, menu, item, limit=T_MENU):
        """Press a menu title, highlight an item BY NAME, release.

        Every coordinate comes off menu_bar[] and the kernel's own dropped
        rect, and there are two confirmations in the middle of the gesture -
        which is what makes this different from four numbers and a drag:

          1. after the press, `menu_y1` says the pull-down is actually on the
             screen (a press that missed the bar drops nothing);
          2. after the move, `menu_sel` says THE ITEM WE MEANT is the one
             highlighted - checked BEFORE the release, so a mis-aimed pick
             raises instead of running the wrong command.

        The second is the one worth having. A menu cannot be opened with a
        click - `menu_track` draws the pull-down and then polls a level - so
        the gesture is press/move/release, and a release over the wrong item
        activates it. Nothing in the picture afterwards says the command that
        ran was not the command asked for.

        A DISABLED item is refused before the press, with the whole menu
        printed: `menu_hover` will not stop on one, so the drag would end
        parked on a neighbour and release there.
        """
        cells = self.menus()
        # AN INT IS A POSITION, a string is a name. Names are what a test
        # should say - they survive a menu gaining an item - but a row whose
        # subject IS the bar's layout means the position, and making it spell
        # that in coordinates would put it back where this started.
        if isinstance(menu, int):
            if not 0 <= menu < len(cells):
                raise UIError("this window's bar has %d cells, so there is no "
                              "cell %d. The bar is %r"
                              % (len(cells), menu, [c[0] for c in cells]))
            hit = [menu]
        else:
            want = menu.upper()
            hit = [i for i, c in enumerate(cells) if c[0].upper() == want]
            if not hit:
                hit = [i for i, c in enumerate(cells)
                       if c[0].upper().startswith(want)]
        if len(hit) != 1:
            raise UIError(
                "no single menu called %r on this window's bar. The bar is %r"
                % (menu, [c[0] for c in cells]))
        cell = hit[0]
        title, x0, x1, items = cells[cell]

        if isinstance(item, int):
            if not 0 <= item < len(items):
                raise UIError("the %s menu has %d items, so there is no item "
                              "%d. It holds %r"
                              % (title, len(items), item, [t for t, _ in items]))
            idx = [item]
        else:
            iw = item.upper()
            idx = [i for i, (t, _) in enumerate(items) if t.upper() == iw]
            if not idx:
                idx = [i for i, (t, _) in enumerate(items)
                       if t.upper().startswith(iw)]
        if len(idx) != 1:
            raise UIError("no single item %r in the %s menu. It holds %r"
                          % (item, title, [t for t, _ in items]))
        k = idx[0]
        if not items[k][1]:
            raise UIError(
                "%s -> %r is DISABLED (SPEC.md 12's MENU_DIS prefix), so "
                "menu_hover will not stop on it and a drag would release over "
                "a neighbour. The menu is %r"
                % (title, item, [(t, e) for t, e in items]))

        bx, by = (x0 + x1) // 2, geom.MBAR_H // 2
        self.mo.to(bx, by)
        if self.mo.where()[2] & 1:
            self.mo._edge(False)
        self.mo._edge(True)
        self._wait(lambda: self._word("menu_y1") and self._byte("menu_dropd"),
                   "the %s menu to drop" % title, limit,
                   snapshot=lambda: "menu_dropd = %d, menu_y1 = %d"
                   % (self._byte("menu_dropd"), self._word("menu_y1")))

        # ...AND THAT IT IS THIS TITLE'S MENU. `menu_sel` below confirms the
        # item INDEX, which a wrong-menu drop passes: press File, get the
        # System menu, ask for its item 0 and release - and 'About os8088...'
        # runs where 'Close Window' was meant, with nothing raised. That is not
        # hypothetical, it is SPEC.md 87.2 on a hard-disk boot, where every
        # press in the bar hit-tested at x = 0 because a greying predicate
        # clobbered the press's x. `menu_cell` is what menu_track resolved the
        # press to, so comparing it with the cell we AIMED at is the whole
        # check, and it costs one byte read.
        got = self._byte("menu_cell")
        if got != cell:
            self.mo._edge(False)
            raise UIError(
                "pressed %r at x=%d (cell %d of this bar) and menu_track "
                "dropped CELL %d instead%s. The bar is %r - so this is the "
                "hit test disagreeing with menu_bar[], not a mis-aimed click"
                % (title, bx, cell, got,
                   ", %r" % (cells[got][0],) if got < len(cells) else "",
                   [(c[0], c[1], c[2]) for c in cells]))

        y1 = self._word("menu_y1")
        ix = self._word("menu_x1") + 8
        iy = y1 + 1 + k * geom.MENU_ITEM_H + geom.MENU_ITEM_H // 2
        self.mo.to(ix, iy, l=True)
        try:
            self._wait(lambda: self._word("menu_sel") == k,
                       "%s -> %r (item %d) to highlight" % (title, item, k),
                       limit,
                       snapshot=lambda: "menu_sel = %d" % _sel(self))
        except UIError:
            self.mo._edge(False)            # ...and NEVER leave the button
            raise                           # down: a stuck press wedges every
                                            # later verb, and the failure then
                                            # reads as the next thing
        self.mo._edge(False)
        self._say("menu_pick %s -> %s" % (title, item))


def _assoc_note(name, ty):
    """The extra sentence a type-0 miss deserves.

    A type-0 entry is not inert: SPEC.md 54's file association is what makes
    DEMO.HTM open the browser, and what makes it NOT open one is the handler
    being absent from the disk rather than anything about the double-click.
    Saying so is the difference between a timeout and a diagnosis.
    """
    if ty != 0:
        return ""
    return ("\n  %s is a type-0 entry (SPEC.md 19.1), so the only thing that "
            "can open it is a FILE ASSOCIATION (SPEC.md 54) - and an "
            "association resolves its handler on the disk it was launched "
            "from. Check the handler package is on this floppy." % name)


def _sel(ui):
    v = ui._word("menu_sel")
    return -1 if v == 0xFFFF else v


def _decode(raw, n):
    out = []
    for i in range(n):
        e = raw[i * geom.DSK_DE_STRIDE:(i + 1) * geom.DSK_DE_STRIDE]
        out.append((e[:16].split(b"\0")[0].decode("latin-1"),
                    _u16(e, geom.LD_DE_TYPE)))
    return out


# --- the one number os88geom cannot hold ------------------------------------
#
# EVERY OTHER CONSTANT HERE COMES FROM os88geom, which checks each one against
# the kernel source at import (tests/unit/t_mirror.py runs that check in the
# fast tier). A local copy is exactly the defect that module exists to stop:
# it does not fail, it returns a NUMBER, and the click derived from it lands
# somewhere plausible.
#
# FM_ROW_X is the exception because files.inc has no `equ` for it - the pen is
# a literal at its one use - so there is nothing to compare against and
# registering it would make the guard agree with a copy of itself.
FM_ROW_X = 60
