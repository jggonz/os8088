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
import time

# menu.inc: the System cell is x 0..29 and a pull-down hangs from MBAR_H + 1
# with MENU_ITEM_H per item. Item 1 is CMD_CTRL (About, Control Panel, Task
# Manager, ...).
MBAR_H, MENU_ITEM_H = 20, 16
SYS_X, SYS_Y = 12, 8

# ctrl.inc's geometry, content-relative. The item list on the left, then the
# pane, then SPEC.md 31.10.2's row inside it.
TITLE_H = 18
CP_IX, CP_I0Y, CP_IROWH = 6, 6, 14
CP_RX, CP_PGX = 96, 4
CPV_MY, CPV_MSTEP = 96, 74
CPV_R0Y, CPV_ROWH = 20, 16          # the ADAPTER rows, which pick the primary
CPV_BTNX, CPV_BTNY = 2, 72          # ...and the Activate button under them

MODES = {"single": 0, "right": 1, "below": 2}


def _u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def _cp_win(m, S):
    """(x, y) of the Control Panel's frame, or None. It is the window whose
    W_TITLE is the panel's - found by rect instead, because the panel is the
    only 320-wide window in the machine and a title compare would need the
    string's address as well."""
    wins = m.read(S("wm_wins"), 12 * 26)
    for i in range(12):
        f = _u16(wins, i * 26)
        if f & 3 != 3:                      # used and visible
            continue
        if _u16(wins, i * 26 + 6) == 320:   # W_W
            return _u16(wins, i * 26 + 2), _u16(wins, i * 26 + 4)
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


def set_primary(m, mo, S, settle, slot, card=None):
    """Click adapter row `slot` and press Activate - which is how the OTHER
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
