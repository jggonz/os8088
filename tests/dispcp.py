"""Driving the Control Panel's Display page from a scripted session.

SPEC.md 31.10.2's desktop row is how a machine gets an extended desktop at
all now - SPEC.md 39.19.1 makes Single the default, on the grounds that the
kernel can detect a second CARD and nothing can detect a second MONITOR - so
every dual-display test has to come through here first. It is shared rather
than copied into three files because the coordinates below are five nested
layouts deep and there is no second place they could be checked.

EVERY CLICK RE-READS THE WINDOW'S RECT. A click that takes posts [cp_dirty]
and runs wm_refit, so the panel may have MOVED by the time the next click is
aimed - and a click aimed at where it used to be lands on the desktop, which
switches the menu bar to Locator and looks exactly like a control that does
not work.

**THE GENERAL VERBS HAVE MOVED TO tools/os88ui.py.** `open_drive`,
`open_named` and `scroll_to` below are now thin wrappers over it and keep
their signatures, so the hundred-odd scripts importing this file did not have
to change - but a NEW script should use os88ui directly, where the same three
calls are `ui.open_drive("B")` and `ui.open("APPS")` and take no `S` or
`settle`. What the wrappers gain by delegating is the confirmation: each verb
now reads guest state to prove it did what it was asked, so a miss raises here
instead of surfacing twenty steps later as the feature under test.

What stays HERE is the Control Panel - `open_panel`, `set_mode`,
`set_primary`, `adapter_row` and the coordinates above them. That is a
specific window's layout, not a general verb, and os88ui deliberately knows
nothing about it.
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom                                              # noqa: E402
import os88marty                                             # noqa: E402
import os88ui                                                # noqa: E402
# menu.inc: the System cell is x 0..29 and a pull-down hangs from MBAR_H + 1
# with MENU_ITEM_H per item. Item 1 is CMD_CTRL (About, Control Panel, Task
# Manager, ...).
from os88geom import (MBAR_H, MENU_ITEM_H, TITLE_H, KERNEL_SEG,  # noqa: E402
                      WF_SAVEU, WF_SIZABLE, WF_KEEPH,
                      WIN_SIZE, MAX_WIN, W_FLAGS, W_X, W_Y, W_W, W_H, W_TITLE,
                      DESK_ZY0, DESK_ZW, DESK_COLW,
                      DV_KIND, DV_FLAGS, DV_SIZE, DVOL_MAX, DVK_FREE,
                      FM_ROW_Y0, FM_ROW_H, FS_SCRL, FS_N, FS_VSEG)
SYS_X, SYS_Y = 12, 8

# [vid_kind] -> the type name `cards` reports, and the --primary spelling ->
# the same. viddet.inc's VID_VGA/VID_HERC/VID_CGA are 0/1/2. Here rather than
# in each test because there are three of them now and a VGA machine is the
# case they all used to get wrong: `"mda" if kind == 1 else "cga"` looks for a
# CGA card that a VGA+Hercules machine does not have, and raises IndexError
# several frames away from the reason (docs/plans/completed/DUAL-DISPLAY-VGA.md).
KIND_CARD = {0: "vga", 1: "mda", 2: "cga"}
PRIMARY_CARD = {"herc": "mda", "cga": "cga", "vga": "vga"}

# The two two-card pairings. A VGA machine also carries the CGA bit, unprobed,
# because mode 6 is a standard BIOS mode there - so VGA+Hercules reads 7, and
# the CGA bit in it is that VGA's own second mode rather than a third card.
AVAIL_HERC_CGA, AVAIL_VGA_HERC = 0x06, 0x07

# ctrl.inc's geometry, content-relative. The item list on the left, then the
# pane, then SPEC.md 31.10.2's row inside it.
CP_IX, CP_I0Y, CP_IROWH = 6, 6, 14
CP_RX, CP_PGX = 96, 4
CPV_MY, CPV_MSTEP = 106, 74         # SPEC.md 31.10.3 moved this row DOWN, from
                                    # 96 to 106, to put 'Desktop Extension
                                    # Mode:' above it. A stale 96 here is not a
                                    # near miss: the label has no hit band, so
                                    # the click lands on nothing, set_mode's
                                    # verify sees the mode unchanged, and every
                                    # dual-display test in this directory fails
                                    # pointing at the kernel
CPV_R0Y, CPV_ROWH = 20, 16          # the ADAPTER rows, which pick the primary
CPV_BTNX, CPV_BTNY = 2, 72          # ...and the Set Primary button under them

MODES = {"single": 0, "right": 1, "below": 2}


def _u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def _cp_win(m, S):
    """(x, y) of the Control Panel's frame, or None. Matched on W_TITLE, which
    is `cp_ttl` and nothing else in the machine.

    IT USED TO MATCH ON `W_W == 320` - "the panel is the only 320-wide window,
    and a title compare would need the string's address as well". The address
    is one `os88sym` call, and the premise was false: a Disk window's template
    is 320 wide too (`fm_tpl`), so any caller that opened one before the panel
    got the DISK window's rect back and clicked its content instead. That is
    silent - the clicks land on a real window, nothing errors, and the adapter
    simply does not change - which is the same class of harness bug as the
    bare 26 below, and cost a session the same way.

    The stride is os88geom's. It was written out as a bare 26 here - in the
    one file that already had WIN_SIZE = 28 forty lines further down - so this
    walked the table wrong and finding the panel at all was luck."""
    title = S("cp_ttl") - (KERNEL_SEG << 4)         # W_TITLE is a NEAR offset
    wins = m.read(S("wm_wins"), MAX_WIN * WIN_SIZE)
    for i in range(MAX_WIN):
        b = i * WIN_SIZE
        if _u16(wins, b + W_FLAGS) & 3 != 3:        # used and visible
            continue
        if _u16(wins, b + W_TITLE) == title:
            return _u16(wins, b + W_X), _u16(wins, b + W_Y)
    return None


CP_IVID = 4                     # kernel/ctrl.inc: the Display page's RECORD
CP_ITHM = 5                     # ...and SPEC.md 76.4's Theme page


# --- the Control Panel's own verbs, CONFIRMED --------------------------------
#
# Each of these used to end in `settle(m)` - two host seconds of proven
# stillness before the next click could read the panel's rect. Every one of
# them has a word in the guest that says whether the click took, so each waits
# for THAT instead: it answers as soon as the kernel has acted, and it says
# WHICH click was lost when one is. The measurement behind the change is in
# the block above open_drive.
#
# `card` is passed through untouched, and a caller that is about to compare
# PIXELS still wants `settle` - which is what the module docstring says and
# what `paint=True` is for on the three verbs below.
CP_GUEST = 20.0                 # guest seconds a Control Panel click may take


def _cpwait(m, cond, what, guest=CP_GUEST):
    """Wait for a guest word, or raise naming the click that was lost."""
    try:
        os88marty.until(m, lambda _: cond(), what, poll=0.05, guest=guest)
    except Exception as e:
        raise RuntimeError("%s" % e)


def _b(m, S, name):
    return m.read(S(name), 1)[0]


def open_panel(m, mo, S, settle, card=None, page=CP_IVID):
    """Chip menu -> Control Panel, and leave it on `page`.

    `page` is a RECORD index in cp_items, not a drawn row - see below - and
    None opens the panel without selecting anything, which is what a caller
    on a one-adapter machine wants when the page it needs is not Display.
    It defaulted to Display for as long as Display was the only page anybody
    drove, and a Theme page on a single-card machine then raised about a
    HIDDEN DISPLAY page, which is true and is not what the caller asked for.
    """
    if _cp_win(m, S) is None:
        mo.menu(SYS_X, SYS_Y, SYS_X, MBAR_H + 1 + MENU_ITEM_H + 8, settle=0)
        try:
            _cpwait(m, lambda: _cp_win(m, S) is not None,
                    "the Control Panel window to open")
        except RuntimeError:
            raise RuntimeError("the Control Panel did not open - the chip "
                               "menu's item 1 was not where this thought")
    # WHICH ROW THE DISPLAY PAGE IS DRAWN AT IS NOT "the last one" ANY MORE.
    # It was, and this read [cp_nst] - 1 and clicked there; SPEC.md 76.4's
    # Theme page is record 5 and Display record 4, so that lands on Theme and
    # every leg then fails with "the Control Panel did not turn Extend on",
    # which points at the Display page rather than at the click that never
    # reached it.
    #
    # cp_items has no ordering rule left (kernel/ctrl.inc): [cp_hide] is a bit
    # per record and a hidden record takes no ordinal, so the drawn row is the
    # number of SHOWN records below the one wanted - cp_v2r walked backwards.
    # Its own bit being set is the assertion the page exists at all.
    if page is None:
        return
    hide = m.read(S("cp_hide"), 1)[0]
    if hide & (1 << page):
        raise RuntimeError("Control Panel record %d is hidden on this machine "
                           "(SPEC.md 39.11.1 hides Display on a single-adapter "
                           "one)" % page)
    row = sum(1 for r in range(page) if not (hide & (1 << r)))
    wx, wy = _cp_win(m, S)
    mo.click(wx + 1 + CP_IX + 30,
             wy + TITLE_H + 1 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2,
             settle=0)
    # [cp_sel] IS THE PAGE, and cp_onclick stores the RECORD index into it -
    # not the drawn row - so this is the same number the caller asked for.
    _cpwait(m, lambda: _b(m, S, "cp_sel") == page,
            "Control Panel page %d to be selected ([cp_sel] is %d)"
            % (page, _b(m, S, "cp_sel")))


def set_mode(m, mo, S, settle, which, card=None):
    """Click one of Single / Right / Below. The panel must already be open on
    the Display page (open_panel). Returns nothing - read the 'VD' block."""
    i = MODES[which]
    wx, wy = _cp_win(m, S)              # RE-READ: the last click may have
                                        # moved this window (see the module
                                        # docstring)
    mo.click(wx + 1 + CP_RX + CP_PGX + i * CPV_MSTEP + 20,
             wy + TITLE_H + 1 + CPV_MY + 6, settle=0)
    # [vid_dmode] is 0 Single / 1 Extend and [vid_dlay] 0 Right / 1 Below
    # (kernel/vidsel.inc), so the pair IS the mode this was asked for. A
    # missed click leaves them alone and says so here rather than at whatever
    # the caller measures three steps later.
    want = (0, None) if i == 0 else (1, i - 1)
    _cpwait(m, lambda: (_b(m, S, "vid_dmode") == want[0]
                        and (want[1] is None
                             or _b(m, S, "vid_dlay") == want[1])),
            "the desktop mode to become %r ([vid_dmode] %d, [vid_dlay] %d)"
            % (which, _b(m, S, "vid_dmode"), _b(m, S, "vid_dlay")))
    settle(m, card=card)                # ...and THEN the picture: a mode
                                        # change reshapes the whole screen and
                                        # every caller reads geometry next


def close_panel(m, mo, S, settle, card=None):
    """The close box, on the LEFT of the title bar - and the only thing that
    writes SYSTEM.CFG (SPEC.md 31.8). Minimizing does not, and neither does
    quitting the emulator, so a persistence test that skips this measures
    nothing."""
    w = _cp_win(m, S)
    if w is None:
        return
    wx, wy = w
    owed = _b(m, S, "cp_wdirty")        # ...is a SYSTEM.CFG write owed?
    mo.click(wx + 10, wy + TITLE_H // 2, settle=0)
    _cpwait(m, lambda: _cp_win(m, S) is None,
            "the Control Panel window to close")
    if owed:
        # THE WRITE IS THE POINT OF CLOSING (SPEC.md 31.8) and [cp_wdirty] is
        # the kernel's own record of owing one, cleared when it lands. This
        # was `time.sleep(1.0)` with "the floppy write is seconds of motor" -
        # a host second against an operation measured in GUEST time, so on a
        # busy box it returned mid-write and a persistence test then read a
        # SYSTEM.CFG that was not there yet.
        _cpwait(m, lambda: not _b(m, S, "cp_wdirty"),
                "SYSTEM.CFG to be written ([cp_wdirty] still set)", 30.0)


def adapter_row(avail, kind):
    """Which row of the Display page's adapter list is VID_* `kind`?

    The page lists the adapters the machine HAS, in VID_VGA/VID_HERC/VID_CGA
    order, so a row index is the number of available adapters below the one
    wanted - 0 for the Hercules on a Hercules+CGA machine and 1 on a
    VGA+Hercules one, where the VGA is listed above it. Derived rather than
    written down, because the two pairings disagree and a hard-coded row is
    right on whichever machine it was written on.
    """
    return bin(avail & ((1 << kind) - 1)).count("1")


def adapter_kind(avail, slot):
    """...and the INVERSE: which VID_* kind is drawn in row `slot`?

    `cp_vid_slot` walks the kinds in order and counts the ones `[vid_avail]`
    has, so a row is the `slot`-th set bit - and `cp_vid_rowok` is exactly
    `vid_avail_test`, a plain bit test, which is what makes this a faithful
    mirror rather than an approximation.

    It exists because **[cp_vsel] HOLDS THE KIND, NOT THE ROW**, and a
    confirmation that compared the two passed a slot-0 click that had
    correctly selected VID_HERC and called it a miss. `set_primary` takes a
    row, like the user's eye does; the guest word is in the other space.
    """
    n = 0
    for kind in range(4):               # VGA / HERC / CGA / EGA
        if avail & (1 << kind):
            if n == slot:
                return kind
            n += 1
    raise RuntimeError("this machine has no adapter row %d (vid_avail=%#x)"
                       % (slot, avail))


def set_primary(m, mo, S, settle, slot, card=None):
    """Click adapter row `slot` and press Set Primary - which is how the OTHER
    two of SPEC.md 39.19.2's four arrangements are reached: the primary is
    always at the virtual origin, so swapping it is what puts the other
    monitor on the left. The panel must already be open on the Display page.

    `slot` is a POSITION in the list, not a VID_* kind: cp_vid_slot draws one
    row per adapter the machine has, in kind order, so on a Hercules+Cga
    machine slot 0 is the Hercules and slot 1 the Cga.
    """
    wx, wy = _cp_win(m, S)
    mo.click(wx + 1 + CP_RX + CP_PGX + 6,
             wy + TITLE_H + 1 + CPV_R0Y + slot * CPV_ROWH + 6, settle=0)
    # THE ROW ONLY SELECTS; the button commits. [cp_vsel] is that pending
    # choice, so a row click that missed is caught here - where it used to
    # surface as "Set Primary did nothing", which is the wrong half.
    kind = adapter_kind(_b(m, S, "vid_avail"), slot)
    _cpwait(m, lambda: _b(m, S, "cp_vsel") == kind,
            "adapter row %d (VID_* kind %d) to be selected ([cp_vsel] is %d)"
            % (slot, kind, _b(m, S, "cp_vsel")))
    wx, wy = _cp_win(m, S)              # RE-READ: see the module docstring
    mo.click(wx + 1 + CP_RX + CPV_BTNX + 40, wy + TITLE_H + 1 + CPV_BTNY + 9,
             settle=0)
    # THE BUTTON PRESS IS NOT CONFIRMED, deliberately: `cpf_vidok` greys it
    # when the dot is already on the running adapter, so a legitimate NO-OP
    # and a missed click look identical from out here - and a confirmation
    # that cannot tell them apart fails the no-op, which is what a first
    # version of this did to dispthm. The ROW click above is the half that
    # can be checked, and it is the half that was silently going missing.
    settle(m, card=card)                # ...and the picture, for the reason
                                        # set_mode settles: the screen is a
                                        # new shape and callers read geometry


# --- the desktop's drive column (SPEC.md 26.1) -------------------------------
#
# EVERY NUMBER THAT CAN BE READ OUT OF THE GUEST IS READ OUT OF THE GUEST, and
# that is not tidiness: this arithmetic was mirrored in two test scripts with
# DESK_ZY0 = 24 / step 52 / width 60 baked in, and SPEC.md 26.4's square CGA
# icon changed the pitch to 34 and the width to 32. Both scripts then
# double-clicked bare desktop, one opened no Disk window and the other opened
# one instead of two, and neither said anything about zones - they reported a
# window that failed to appear. `desk_zstep`, `desk_zh1`, `desk_rows` and
# `vid_desk_zx` are all live words, so only DESK_ZY0/DESK_ZW/DESK_COLW - which
# are assembly-time constants with no published copy - need mirroring at all,
# and os88geom is where they are mirrored, once, checked against desk.inc.


def drive_ordinal(m, S, letter="B"):
    """Which desktop ZONE does drive `letter` own? (SPEC.md 26.1)

    NOT the drive number. A zone exists per volume with DV_FLAGS bit 0 set, and
    the ordinal is that volume's POSITION among the shown ones - so a machine
    whose B: was retired by SPEC.md 18.97's probe, or which mounts a hard disk,
    numbers them differently. Walking dsk_vtab is the only way to be right, and
    it turns "no window opened" into "B: has no zone", which is the difference
    between a test that fails and a test that says why.
    """
    want = ord(letter.upper()) - ord("A")
    t = m.read(S("dsk_vtab"), DVOL_MAX * DV_SIZE)
    n = 0
    for v in range(DVOL_MAX):
        r = t[v * DV_SIZE:(v + 1) * DV_SIZE]
        if r[DV_KIND] == DVK_FREE or not (r[DV_FLAGS] & 1):
            continue
        if v == want:
            return n
        n += 1
    return None


def drive_xy(m, S, ordinal):
    """The centre of volume `ordinal`'s desktop zone, in VIRTUAL coordinates.

    desk_ord_xy's arithmetic: zones fill a column downwards and wrap LEFT from
    the drive column, so ordinal 1 is BELOW ordinal 0 until [desk_rows] runs
    out - which is 2 on a CGA with the tall icon and 4 with the square one.
    """
    def w(name):
        b = m.read(S(name), 2)
        return b[0] | (b[1] << 8)

    rows, step, zh1, zx = (w("desk_rows"), w("desk_zstep"), w("desk_zh1"),
                           w("vid_desk_zx"))
    col, row = divmod(ordinal, rows)
    return (zx - col * DESK_COLW + DESK_ZW // 2,
            DESK_ZY0 + row * step + zh1 // 2)


# --- the general verbs, over tools/os88ui.py ---------------------------------
#
# These keep their old signatures so that the hundred-odd scripts importing
# this file did not have to change in one commit, and they take `S` and
# `settle` and use neither by default: os88ui resolves symbols off the Marty
# and CONFIRMS instead of settling.
#
# **THE TRAILING SETTLE IS GONE, AND `paint=True` PUTS IT BACK.** It was kept
# for one commit on the grounds that a caller might be about to compare
# pixels and this layer cannot know. It can be counted, though, and the count
# settles it: of the **401 call sites** of these three verbs in tests/, **2**
# read the framebuffer within six lines - both in tmrepair.py, and both
# already behind a `raise_win` that settles anyway. So 399 sites were paying
# for a picture nobody looked at.
#
# What that costs is not small. A settle is `stable * quiet` = 2.0 host
# seconds of PROVEN stillness before it can return, plus its captures, and it
# measured 2.9s average over four rows - so the trailing settle alone was
# about **twenty minutes of the soak**. And it is irreducible by tuning: the
# gap log (docs/plans/SOAK-PARALLEL.md 11) says a change arrives after one whole
# quiet round 1 time in 19, so `stable` cannot come down. The only way to
# spend less is not to settle, which is what confirming is for.
#
# `paint=True` is the escape hatch, and the right one to reach for the moment
# the next thing a script does is read pixels.

def _ui(m, mo, card, S=None):
    """One UI per Marty, cached on it - the mouse is already open.

    **None FOR A DRIVER THAT IS NOT A MARTY**, and every verb below falls back
    to its own blind path when it gets one. os88ui reads guest state through
    `m.sym`, `m.readseg` and `m.status` - a MartyPC debug-server surface that a
    QEMU `Qemu` object does not have and cannot grow: QEMU has no cycle counter
    to anchor a budget to and no symbol reader on the object at all.

    SIX ROWS IN THIS TREE DRIVE QEMU THROUGH THIS FILE - brnav, ethcfg,
    ethernet, ftpd, ftpdpix, minesrc - because five of docs/TESTING.md's closed
    list are theirs (the Ethernet card most of all: MartyPC has no NIC of any
    kind). `minesrc` is how this was found: the first delegation ended in
    `os88geom.drive_pt(m, ...)`, which reaches for `m.sym`, and the row died
    with `'Qemu' object has no attribute 'sym'` several frames from the cause.
    """
    if not hasattr(m, "sym"):
        return None
    u = getattr(m, "_os88ui", None)
    if u is None or u.mo is not mo or (S is not None and u.sym is not S):
        u = os88ui.UI(m, card=card, mouse=mo, sym=S)
        m._os88ui = u
    u.card = card
    return u


def open_drive(m, mo, S, settle, letter="B", card=None, paint=False):
    """Double-click drive `letter`'s desktop zone; answer the click point.

    Confirmed now: a window has to appear, or this raises naming what is
    open. Before, a zone that had moved - which is what SPEC.md 18.97's
    floppy probe does to B: - clicked bare desktop and the caller went on to
    aim at a Disk window that was never there.
    """
    if isinstance(letter, int):          # an ORDINAL was passed: no longer
        ordinal = letter                 # supported, because it is not stable
        raise TypeError("open_drive takes a DRIVE LETTER, not the ordinal %d "
                        "- see drive_ordinal()" % ordinal)
    ordinal = drive_ordinal(m, S, letter)
    if ordinal is None:
        raise RuntimeError("drive %s: has no desktop zone on this machine "
                           "(dsk_vtab says it is free or hidden)" % letter)
    x, y = drive_xy(m, S, ordinal)
    ui = _ui(m, mo, card, S)
    if ui is None:                      # QEMU: the blind path, as before
        mo.dblclick(x, y)
        settle(m, card=card)
        return x, y
    try:
        ui.open_drive(letter)
    except os88ui.UIError as e:
        raise RuntimeError(str(e))
    if paint:
        settle(m, card=card)
    return x, y


# --- a Disk window's rows (SPEC.md 22) ---------------------------------------
#
# The same discipline as drive_xy above, and for the same reason: FM_ROW_Y0
# moved 26 -> 22 under two tests at once, and neither said "the row geometry
# changed" - one reported that a package would not launch and the other that a
# window had not opened. They come from os88geom, which checks them.
FM_ROW_X = 60                   # the pen, and files.inc has no equ for it


def row_xy(wx, wy, row=0):
    """The centre of list row `row` in a Disk window at (wx, wy)."""
    return (wx + 1 + FM_ROW_X,
            wy + TITLE_H + 1 + FM_ROW_Y0 + row * FM_ROW_H + FM_ROW_H // 2)


def open_row(m, mo, S, settle, wx, wy, row=0, card=None, expect=None):
    """Double-click a VISIBLE row of a Disk window and settle.

    **PREFER open_named.** `row` here is a position on the GLASS, not a file:
    it is what the window is showing at that y, which depends on the folder's
    contents AND on where the list is scrolled. Exactly one caller in this
    tree legitimately wants that (wmartifact, which asks for "the last row
    this window can reach"); everything else means a FILE and should say so,
    because a folder that gains one entry renumbers every row after it.

    `expect` is the seatbelt for a caller that must use a row anyway: name the
    file you believe is there and this refuses rather than double-clicking
    whatever sorted into the slot. Either way the entry actually clicked is
    printed, so a wrong one is visible in the log instead of turning up
    several steps later as "the package would not launch".
    """
    got = None
    try:
        rows = listing(m, S)
        i = scroll(m, S) + row
        if 0 <= i < len(rows):
            got = rows[i][0]
    except Exception:                       # a window mid-navigation, or a
        pass                                # volume that will not list: the
                                            # click is still the caller's call
    if expect is not None and (got or "").upper() != expect.upper():
        raise RuntimeError("row %d of this window is %r, not %r"
                           % (row, got, expect))
    print("      open_row: row %d = %s" % (row, got or "?"))
    x, y = row_xy(wx, wy, row)
    mo.dblclick(x, y)
    settle(m, card=card)
    return x, y


# --- the listing, read out of the guest (SPEC.md 19.1/19.4) ------------------
#
# A ROW NUMBER IS NOT A FILE, and writing one down is the same mistake
# drive_xy's block above is about. It cost a whole investigation: a test
# navigated "row 1 of B:, then row 3" believing that was APPS then HELLO.O88,
# and it is GAMES then MISSILE.O88 - the root has no synthesized `..` (19.5)
# and a subdirectory does, so the two listings are offset by one from each
# other, and the Makefile's build order is not the display order either
# (19.4 sorts by name). The test then measured a window with a live worker
# animating in it with a method that requires a screen that settles, and
# reported the harness's own moving picture as a kernel defect.
#
# So: ask. disk_dir is the global mount snapshot and a navigation is a full
# mount, so it names the folder just entered.
DSK_DE_STRIDE = 24              # SPEC.md 19.1: name @0 NUL-padded, type @16,
DSK_DE_TYPE = 16                # first cluster @18, size @20 - and the STAGED
                                # LISTING's stride is 24, not DSK_DE_SIZE's 32.
                                # The record IS 32 bytes wide where a driver
                                # hands one over (OSAPI_FS_ENT); 24..31 are
                                # declared zero and `disk_dir` does not store
                                # them. Decoding the listing at 32 reads entry
                                # 1 onward from the wrong place, and every name
                                # comes back as the tail of the one before it


def _decode(raw, n):
    out = []
    for i in range(n):
        e = raw[i * DSK_DE_STRIDE:(i + 1) * DSK_DE_STRIDE]
        out.append((e[:16].split(b"\0")[0].decode("latin-1"),
                    _u16(e, DSK_DE_TYPE)))
    return out


def listing(m, S):
    """[(name, type)] of what the ACTING Disk window is showing, in order.

    **THE WINDOW'S OWN CACHE FIRST, and the global snapshot only as a
    fallback.** SPEC.md 22.1's rule is that paints read the window's cache and
    only actions re-sync the globals - and SPEC.md 18.9's quiet mount
    deliberately leaves `disk_nfiles` at 0 with [dsk_lstale] raised, which is
    a perfectly ordinary state after anything that moved the volume without
    navigating. Reading the globals there answers "this folder is empty" about
    a window with a dozen rows on screen, which is the wrong answer to the
    only question this is ever asked: WHAT IS THE USER LOOKING AT. Mounting a
    RAM disk from the Control Panel puts the machine in exactly that state,
    and it is what tests/rdmove.py met.

    The cache is a byte-for-byte copy of `disk_dir` living in the window's own
    FS_VSEG claim (SPEC.md 2.3/22.1), so the decode is the same one.

    The fallback reads through [dsk_dseg]:[dsk_doff] rather than at `disk_dir`,
    because a driver-backed volume lists into its DRIVER's claim instead
    (disk.inc's dsk_doff comment) - the floppy case is where those agree.
    """
    vp = _u16(m.read(S("fm_vp"), 2))
    if vp:
        base = (KERNEL_SEG << 4) + vp
        n = _u16(m.read(base + FS_N, 2))
        vseg = _u16(m.read(base + FS_VSEG, 2))
        if n and vseg:
            return _decode(m.read(vseg << 4, n * DSK_DE_STRIDE), n)
    n = _u16(m.read(S("disk_nfiles"), 2))
    if not n:
        return []
    seg = _u16(m.read(S("dsk_dseg"), 2))
    off = _u16(m.read(S("dsk_doff"), 2))
    return _decode(m.read((seg << 4) + off, n * DSK_DE_STRIDE), n)


def row_of(m, S, name):
    """Which display row is `name`? Raises rather than returning a wrong row -
    a silent miss here lands a double-click on whatever sorted into that slot,
    which is exactly the failure this exists to end."""
    rows = listing(m, S)
    for i, (nm, _) in enumerate(rows):
        if nm.upper() == name.upper():
            return i
    raise RuntimeError("%r is not in this folder - it lists %r"
                       % (name, [r[0] for r in rows]))


def scroll(m, S):
    """[FS_SCRL] of the ACTING Disk window - the first entry it is showing.

    Through [fm_vp], which files.inc publishes as "the acting window's state
    block" and which fm_vp_set writes on every raise, so it names the window a
    caller has just navigated in. [fm_vp] is a NEAR offset in KERNEL_SEG and
    S() answers a LINEAR address, so the segment base goes back on by hand -
    mixing the two reads 0x600 bytes low, lands inside the kernel image, and
    answers a plausible 0 forever (tests/fmbtn.py paid a whole debugging round
    for that one).
    """
    vp = _u16(m.read(S("fm_vp"), 2))
    return _u16(m.read((KERNEL_SEG << 4) + vp + FS_SCRL, 2))


def _scroll_to_blind(m, mo, S, settle, entry, card):
    """scroll_to for a driver os88ui cannot read - the pre-delegation code.

    Same algorithm: walk with the arrow keys (SPEC.md 22.11) and read
    [FS_SCRL] BACK, so the clamp at the end of a list is computed by the only
    thing that knows how many rows this window shows. What it cannot do is
    bound the per-key wait in GUEST time, because a QEMU object has no cycle
    counter - so this one keeps the host-clock loop, and that is a real
    difference: on a loaded box a step can be judged an END STOP when the
    guest simply had not got there (docs/plans/HANDOFF-SOAK-FINDINGS.md B5). Six
    rows take this path and every one of them is on docs/TESTING.md's closed
    list, so there is nowhere better for them to go.
    """
    if entry < 0:
        raise RuntimeError("entry %d is not a row" % entry)

    def step(key):
        was = scroll(m, S)
        m.key(key)
        for _ in range(30):
            time.sleep(0.1)
            if scroll(m, S) != was:
                return True
        return False

    for _ in range(40):
        if scroll(m, S) == 0:
            break
        if not step("ArrowUp"):
            break
    else:
        raise RuntimeError("the list would not scroll to the top")
    for _ in range(40):
        if scroll(m, S) >= entry:
            break
        if not step("ArrowDown"):
            break
    else:
        raise RuntimeError("entry %d never came on screen" % entry)
    settle(m, card=card)
    row = entry - scroll(m, S)
    if row < 0:
        raise RuntimeError("scrolled PAST entry %d - the list moved under us"
                           % entry)
    return row


def scroll_to(m, mo, S, settle, wx, wy, entry, card=None, paint=False):
    """Bring directory entry `entry` on screen; answer its VISIBLE row.

    os88ui.scroll_to, which is the same algorithm one layer down: scroll with
    the KEYBOARD and read [FS_SCRL] BACK, so the clamp at the end of a list is
    computed by the only thing that knows how many rows this window shows -
    which depends on its height, the adapter and the view mode, and is
    therefore three numbers a harness would otherwise have to keep in step.
    """
    ui = _ui(m, mo, card, S)
    if ui is None:
        return _scroll_to_blind(m, mo, S, settle, entry, card)
    try:
        row = ui.scroll_to(entry)
    except os88ui.UIError as e:
        raise RuntimeError(str(e))
    if paint:
        settle(m, card=card)
    return row


def open_named(m, mo, S, settle, wx, wy, name, card=None, expect="auto",
               paint=False):
    """Double-click the row called `name` in the front Disk window.

    **THE ONLY WAY A TEST SHOULD NAME A FILE.** os88ui.open, which looks the
    name up in the staged listing (SPEC.md 19.4 sorts by name, so the
    Makefile's order never reaches the screen), scrolls to it and CHECKS the
    row before it clicks - then waits for the thing the entry's TYPE says will
    happen: a window for a package, a changed listing for a folder.

    `expect` is passed straight through, and `expect="refusal"` is the one
    worth knowing about: a row testing a package that refuses ITSELF wants it,
    and it is a stronger assertion than the blind settle this replaces, which
    cannot tell a refusal from a launch that was merely slow.

    `wx`/`wy` NAME WHICH Disk window, and are honoured when they match one:
    os88ui raises it first, which is what makes it the acting window (the
    arrow keys only reach the frontmost one anyway). A row with TWO Disk
    windows open needs that - hdmove has B: and C: up and means B: - and a
    row with one is unaffected either way. When they match nothing, the
    acting window is used and that is the old behaviour.
    """
    ui = _ui(m, mo, card, S)
    if ui is None:                      # QEMU: find the row, click it, settle
        row = _scroll_to_blind(m, mo, S, settle,
                               row_of(m, S, name), card)
        return open_row(m, mo, S, settle, wx, wy, row, card=card, expect=name)
    named = next((w for w in os88geom.windows(m, S)
                  if (w.x, w.y) == (wx, wy) and w.visible), None)
    try:
        out = ui.open(name, expect=expect, win=named)
    except os88ui.UIError as e:
        raise RuntimeError(str(e))
    if paint:
        settle(m, card=card)
    return out


# --- the window record (SPEC.md 11) ------------------------------------------
#
# WIN_SIZE IS A STRIDE AND IT MOVES: 18 -> 20 -> ... -> 26 -> 28 over this
# tree's life, once per field added to the record. A stale one does not fail,
# it reads every window's rect out of the middle of its neighbour - so the
# clicks derived from it land on bare desktop and the test reports whatever
# did not happen next. That had cost three debugging sessions when this block
# was written down HERE, and it went on costing them, because writing it down
# in a second place is the same bug: `_cp_win` above kept a 26 of its own.
# os88geom is the one copy now, and it checks itself against wm.inc at import.


def win_rect(m, S, slot):
    """(x, y, w, h) of window `slot`."""
    r = m.read(S("wm_wins") + slot * WIN_SIZE, WIN_SIZE)
    return (_u16(r, W_X), _u16(r, W_Y), _u16(r, W_W), _u16(r, W_H))


def win_list(m, S, check=True):
    """Every used+visible window slot, newest last.

    `check` asserts the rects are PLAUSIBLE against the live desktop, which is
    what catches a moved WIN_SIZE at the point it goes wrong instead of three
    steps later.
    """
    t = m.read(S("wm_wins"), MAX_WIN * WIN_SIZE)
    out = [i for i in range(MAX_WIN)
           if _u16(t, i * WIN_SIZE + W_FLAGS) & 3 == 3]
    if check and out:
        vw = _u16(m.read(S("vid_w"), 2))
        vh = _u16(m.read(S("vid_h"), 2))
        for i in out:
            x, y, w, h = win_rect(m, S, i)
            if not (0 < w <= vw and 0 < h <= vh and x < vw and y < vh):
                raise RuntimeError(
                    "window %d reads (%d,%d) %dx%d on a %dx%d desktop - "
                    "WIN_SIZE (%d here) has moved in kernel/wm.inc"
                    % (i, x, y, w, h, vw, vh, WIN_SIZE))
    return out


# -----------------------------------------------------------------------------
# The Drivers page's list SCROLLS (SPEC.md 31.1.1), so a drv_tab row is not a
# band on screen. CP_DVIS of DRV_MAX rows are shown from [cp_dtop], and a row
# below the fold has to be scrolled to before it can be clicked at all.
#
# It is here rather than in each gate because it is geometry, and the tree's
# rule is one place for it (SPEC.md 22's fm_hit discipline): SPEC.md 31.1's
# reorder put os88net at row 4, and every test that ticks it would otherwise
# be clicking the empty pane below the last band - which does nothing, says
# nothing, and reads as the driver having failed to load.
# -----------------------------------------------------------------------------
# **THE PANE'S X CONSTANTS ARE PANE-RELATIVE AND ITS Y CONSTANTS ARE NOT.**
# `cp_page` hands a page DI = the PANE left (content left + CP_RX) and BP =
# the pane top, which IS the content top - so CP_DSX1 needs CP_RX added and
# CP_DBY1 does not. Getting that wrong puts the arrow click 96px left of the
# arrow, in the middle of the driver rows, where it does nothing at all.
CP_RX = 96
CP_DVIS, CP_DBY1, CP_DROWH = 4, 20, 26
CP_DSX1, CP_DSW, CP_DSAH = 200, 14, 14
CP_DBYN = CP_DBY1 + CP_DROWH * CP_DVIS


def drv_show(mo, x0, y0, row, settle=None):
    """Scroll the Drivers list until drv_tab `row` is visible.

    in:  mo = a mouse with .click(x, y); x0/y0 = the panel's CONTENT origin
    out: the VISIBLE index to click, i.e. row - [cp_dtop]

    The list only ever needs scrolling DOWN here: the page opens at the top
    and every gate that calls this wants one of the last rows.
    """
    top = max(0, row - (CP_DVIS - 1))
    for _ in range(top):
        mo.click(x0 + CP_RX + CP_DSX1 + CP_DSW // 2,
                 y0 + CP_DBYN - CP_DSAH // 2)
        if settle:
            settle()
    return row - top


def drv_row_y(y0, vis):
    """The y to click for a VISIBLE driver row (drv_show's answer)."""
    return y0 + CP_DBY1 + vis * CP_DROWH + CP_DROWH // 2
