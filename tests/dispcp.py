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
                      FM_ROW_Y0, FM_ROW_H)
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


def open_row(m, mo, S, settle, wx, wy, row=0, card=None):
    """Double-click a Disk window row and settle."""
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


def listing(m, S):
    """[(name, type)] of the current global mount snapshot, in display order.

    Read through [dsk_dseg]:[dsk_doff] rather than at `disk_dir`, because a
    driver-backed volume lists into its driver's claim instead (disk.inc's
    dsk_doff comment) - the floppy case is the one where those two agree.
    """
    n = _u16(m.read(S("disk_nfiles"), 2))
    seg = _u16(m.read(S("dsk_dseg"), 2))
    off = _u16(m.read(S("dsk_doff"), 2))
    if not n:
        return []
    raw = m.read((seg << 4) + off, n * DSK_DE_SIZE)
    out = []
    for i in range(n):
        e = raw[i * DSK_DE_SIZE:(i + 1) * DSK_DE_SIZE]
        out.append((e[:16].split(b"\0")[0].decode("latin-1"),
                    _u16(e, DSK_DE_TYPE)))
    return out


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


def open_named(m, mo, S, settle, wx, wy, name, card=None):
    """Double-click the row called `name` in the Disk window at (wx, wy)."""
    return open_row(m, mo, S, settle, wx, wy, row_of(m, S, name), card=card)


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
