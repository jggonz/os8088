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
"""
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "tools"))
import os88geom                                              # noqa: E402
# menu.inc: the System cell is x 0..29 and a pull-down hangs from MBAR_H + 1
# with MENU_ITEM_H per item. Item 1 is CMD_CTRL (About, Control Panel, Task
# Manager, ...).
from os88geom import (MBAR_H, MENU_ITEM_H, TITLE_H, KERNEL_SEG,  # noqa: E402
                      WF_SAVEU,
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
# several frames away from the reason (docs/DUAL-DISPLAY-VGA.md).
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


def open_panel(m, mo, S, settle, card=None):
    """Chip menu -> Control Panel, and leave it on the Display page."""
    if _cp_win(m, S) is None:
        mo.menu(SYS_X, SYS_Y, SYS_X, MBAR_H + 1 + MENU_ITEM_H + 8)
        settle(m, card=card)
        if _cp_win(m, S) is None:
            raise RuntimeError("the Control Panel did not open - the chip "
                               "menu's item 1 was not where this thought")
    # The Display item is LAST in cp_items and [cp_nst] is how many static
    # rows are showing - CP_ITEMS, or one fewer when SPEC.md 31.10.1 hid the
    # page. Reading it is also the assertion that the page exists at all.
    nst = m.read(S("cp_nst"), 1)[0]
    wx, wy = _cp_win(m, S)
    row = nst - 1
    mo.click(wx + 1 + CP_IX + 30,
             wy + TITLE_H + 1 + CP_I0Y + row * CP_IROWH + CP_IROWH // 2)
    settle(m, card=card)


def set_mode(m, mo, S, settle, which, card=None):
    """Click one of Single / Right / Below. The panel must already be open on
    the Display page (open_panel). Returns nothing - read the 'VD' block."""
    i = MODES[which]
    wx, wy = _cp_win(m, S)              # RE-READ: the last click may have
                                        # moved this window (see the module
                                        # docstring)
    mo.click(wx + 1 + CP_RX + CP_PGX + i * CPV_MSTEP + 20,
             wy + TITLE_H + 1 + CPV_MY + 6)
    settle(m, card=card)


def close_panel(m, mo, S, settle, card=None):
    """The close box, on the LEFT of the title bar - and the only thing that
    writes SYSTEM.CFG (SPEC.md 31.8). Minimizing does not, and neither does
    quitting the emulator, so a persistence test that skips this measures
    nothing."""
    w = _cp_win(m, S)
    if w is None:
        return
    wx, wy = w
    mo.click(wx + 10, wy + TITLE_H // 2)
    settle(m, card=card)
    time.sleep(1.0)                     # the floppy write is seconds of motor
                                        # on the machine this is modelled on


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
             wy + TITLE_H + 1 + CPV_R0Y + slot * CPV_ROWH + 6)
    settle(m, card=card)
    wx, wy = _cp_win(m, S)              # RE-READ: see the module docstring
    mo.click(wx + 1 + CP_RX + CPV_BTNX + 40, wy + TITLE_H + 1 + CPV_BTNY + 9)
    settle(m, card=card)


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


def open_drive(m, mo, S, settle, letter="B", card=None):
    """Double-click drive `letter`'s desktop zone and settle."""
    if isinstance(letter, int):          # an ORDINAL was passed: no longer
        ordinal = letter                 # supported, because it is not stable
        raise TypeError("open_drive takes a DRIVE LETTER, not the ordinal %d "
                        "- see drive_ordinal()" % ordinal)
    ordinal = drive_ordinal(m, S, letter)
    if ordinal is None:
        raise RuntimeError("drive %s: has no desktop zone on this machine "
                           "(dsk_vtab says it is free or hidden)" % letter)
    x, y = drive_xy(m, S, ordinal)
    mo.dblclick(x, y)
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
DSK_DE_SIZE = 32                # SPEC.md 19.1: name @0 NUL-padded, type @16,
DSK_DE_TYPE = 16                # first cluster @18, size @20


def _decode(raw, n):
    out = []
    for i in range(n):
        e = raw[i * DSK_DE_SIZE:(i + 1) * DSK_DE_SIZE]
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
            return _decode(m.read(vseg << 4, n * DSK_DE_SIZE), n)
    n = _u16(m.read(S("disk_nfiles"), 2))
    if not n:
        return []
    seg = _u16(m.read(S("dsk_dseg"), 2))
    off = _u16(m.read(S("dsk_doff"), 2))
    return _decode(m.read((seg << 4) + off, n * DSK_DE_SIZE), n)


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


def scroll_to(m, mo, S, settle, wx, wy, entry, card=None):
    """Bring directory entry `entry` on screen; answer its VISIBLE row.

    THE POINT OF THIS IS THAT IT NEEDS NO `fit`. How many rows a Disk window
    shows depends on its height, the adapter and the view mode, and every one
    of those is a number a harness would have to keep in step. Instead: ask
    the window to scroll to `entry`, then read [FS_SCRL] BACK. At the top of
    the list it lands exactly there and the answer is visible row 0; near the
    end it CLAMPS, and `entry - FS_SCRL` is then the row it really is on.
    Either way the arithmetic is done by the kernel, which is the only thing
    that knows.

    It scrolls with the KEYBOARD (Up/Down, SPEC.md 22.11's eight ways) rather
    than the bar, because the bar's cells are five nested layouts deep while a
    key is a key. A bounded number of steps: the list caps at 32 entries
    (FS_N), so 40 is past any list that can exist.
    """
    if entry < 0:
        raise RuntimeError("entry %d is not a row" % entry)

    def step(key):
        """One arrow, then wait for [FS_SCRL] to move. Answers whether it did.

        POLLING THE WORD rather than settling on the picture: a settle is two
        identical frames a second apart and there are up to a dozen of these,
        which turned one navigation into half a minute. The word is the thing
        the answer is computed from anyway, so waiting on anything else is
        waiting on a proxy.
        """
        was = scroll(m, S)
        m.key(key)
        for _ in range(30):
            time.sleep(0.1)
            if scroll(m, S) != was:
                return True
        return False

    for _ in range(40):                         # to the top first, so the
        if scroll(m, S) == 0:                   # walk below is one-directional
            break                               # and cannot oscillate
        if not step("ArrowUp"):
            break                               # already at the top stop
    else:
        raise RuntimeError("the list would not scroll to the top")
    for _ in range(40):
        if scroll(m, S) >= entry:
            break
        if not step("ArrowDown"):               # the END STOP: it clamped, so
            break                               # `entry` is as visible as it
    else:                                       # is ever going to get
        raise RuntimeError("entry %d never came on screen" % entry)
    settle(m, card=card)                        # ...and ONE settle at the end,
                                                # because the click that
                                                # follows is aimed at pixels
    row = entry - scroll(m, S)
    if row < 0:
        raise RuntimeError("scrolled PAST entry %d - the list moved under us"
                           % entry)
    return row


def open_named(m, mo, S, settle, wx, wy, name, card=None):
    """Double-click the row called `name` in the Disk window at (wx, wy).

    **THE ONLY WAY A TEST SHOULD NAME A FILE.** row_of answers which DIRECTORY
    ENTRY it is (SPEC.md 19.4 sorts by name, so the Makefile's order never
    reaches the screen), and scroll_to turns that into a row on the GLASS - so
    a folder that gains a file shifts nothing and a folder that outgrows the
    window scrolls itself. Both of those have broken tests in this tree, and
    the second is why open_row(literal) could not simply be replaced by
    open_row(row_of(...)): CYCLONE took GAMES past what a CGA Disk window
    shows, and an entry index below the fold clicks on whatever is drawn at
    that y - or on nothing at all.
    """
    entry = row_of(m, S, name)
    row = scroll_to(m, mo, S, settle, wx, wy, entry, card=card)
    return open_row(m, mo, S, settle, wx, wy, row, card=card, expect=name)
                                            # ...and CHECKED before it clicks,
                                            # which closes the one hole left in
                                            # the scroll: the arrows only reach
                                            # this window while it is frontmost,
                                            # and a caller that forgot to raise
                                            # it would otherwise scroll nothing
                                            # and double-click the wrong row


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
