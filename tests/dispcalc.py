#!/usr/bin/env python3
"""Does the Calculator add up, fold cleanly and redraw nothing spare?
(SPEC.md 65)

    make && python3 tests/dispcalc.py [--machine os8088_5150_herc_gla]

Four questions, and two of them are ones a screenshot cannot answer.

ARITHMETIC. Every case is driven through the REAL keyboard path - the 8255,
int 09h, the BIOS buffer, W_ONKEY - and read back out of the display's own
composition buffer, which is what the glass was lettered from (SPEC.md 65.4).
So a "correct" answer that never reached the screen fails here.

THE FOLD IS DERIVED, NOT ASSUMED (SPEC.md 65.3). [cal_nvis] is compared
against what the window's own content box can hold, so a CGA showing two rows
and a VGA showing eight are the SAME assertion rather than two expected
pictures. That is the half a fixed screenshot gets wrong, and it is the whole
of what makes this app correct on the small adapter.

NOTHING STALE. After each step the framebuffer is banked, `wm_paint_all` is
forced from outside the guest, and the two are diffed: 0 differing pixels
means what the incremental paths put on the glass is exactly what a full
repaint would. A scroll that shifted the wrong band, a shadow gone out of
step with what was drawn, and a pressed key left inverted all show up here
and in no screenshot.

THE HISTORY LOADS. A row is clicked and the entry must become that row's
ANSWER, which is the reason the pane exists at all.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
sys.path.insert(0, os.path.dirname(__file__))
import os88marty, os88mouse, os88sym, dispcp

S = os88sym.linear
KERNEL_SEG = 0x0060
CAL_NUM_N = 13
CAL_CTX_N = 13
CAL_BASE_H = 118
CAL_ROW_H = 9
CAL_HIST_N = 8
CAL_CW = 224
TITLE_H = 18
CAL_KEY_X0, CAL_KEY_Y0 = 4, 24
CAL_BTN_W, CAL_BTN_H = 51, 14
CAL_KEY_PIT, CAL_BTN_HG = 16, 4
MBAR_H, MENU_ITEM_H, MB_ENTSZ, MB_XL = 20, 16, 14, 6
CAL_HROW_N, CAL_CUT = 26, ord("~")
# the keypad, row-major, exactly as cal_keytab lists it
KEYPAD = ["C", "CE", "<-", "/",
          "7", "8", "9", "*",
          "4", "5", "6", "-",
          "1", "2", "3", "+",
          "+/-", "0", ".", "="]


def key_xy(cx, cy, i):
    """The centre of keypad button `i`, in SCREEN coordinates."""
    col, row = i % 4, i // 4
    return (cx + 1 + CAL_KEY_X0 + col * (CAL_BTN_W + CAL_BTN_HG)
            + CAL_BTN_W // 2,
            cy + TITLE_H + 1 + CAL_KEY_Y0 + row * CAL_KEY_PIT + CAL_BTN_H // 2)

MACHINE = "os8088_5150_cga_gla"      # the IBM ROM is not in this tree
for i, a in enumerate(sys.argv):
    if a == "--machine":
        MACHINE = sys.argv[i + 1]

fails = []


def u16(b, i=0):
    return b[i] | (b[i + 1] << 8)


def pkg_seg(m, slot):
    """The segment the package owning window `slot` runs in (W_SEG)."""
    r = m.read(S("wm_wins") + slot * dispcp.WIN_SIZE, dispcp.WIN_SIZE)
    return u16(r, 22)


def bss(m, seg, off, n):
    return m.read(seg * 16 + off, n)


def frame(m):
    """The screen, as bytes. VGA is deliberately not vram()-able - mode 12h is
    four planes behind the Graphics Controller - so that one asks the CARD
    what it rasterised (os88marty.fbuf) and the 1bpp pair read memory."""
    if "vga" in MACHINE:
        w, h, px = m.fbuf()
        return w, h, bytes(px[i] for i in range(0, len(px), 3))
    w, h, rows = m.vram("cga" if "cga" in MACHINE else "herc")
    return w, h, bytes(b for r in rows for b in r)


def full_repaint(m):
    """Make the GUEST do the `wm_paint_all`, and leave the machine paused.

    One byte: `[cp_dirty]` is the Control Panel's "the scheduler mode changed,
    repaint everything" post, and `ui_task`'s step 3 drains it with exactly
    `gfx_lock` / `wm_paint_all` / `gfx_unlock` (kernel/ui.inc). So the repaint
    runs on the UI task's own stack, under the kernel's own lock, at a moment
    the kernel chose - and the harness's only involvement is a store and a
    poll of the same byte going back to 0.

    IT IS WRITTEN THIS WAY BECAUSE THE OBVIOUS WAY CORRUPTS THE GUEST. The
    first version parked the CPU on a stub built in `menu_bcell` and called
    the three routines from outside. `park` is a CPU RESET (the debug server's
    own header says so), so every register it does not set is cleared - SS:SP
    included - and the banked frame it is handed belongs to whichever task the
    pause caught. Catch the UI task inside a lock hold and the stub's own
    `gfx_lock` spins on `sti / task_yield`, the scheduler switches away, and
    the CS:IP restored at the end names a task whose stack has moved on.
    Measured, that showed up THREE assertions later as a window rect reading
    2056x2056 and a keyboard that had stopped arriving - a failure nowhere
    near the call that caused it, and the reason this note is longer than the
    code. Poking a flag the kernel already polls has none of that surface:
    nothing is parked, nothing is reset, no scratch is borrowed, and the lock
    is never taken by anybody but the kernel.

    The arrow is handled for free by the same fact - `gfx_lock` takes it off
    and `gfx_unlock` puts it back with a fresh bank (SPEC.md 7.1), so a
    forced repaint leaves the pointer exactly as any drawing path does.
    """
    m.cmd(cmd="run")
    m.write(S("cp_dirty"), b"\x01")
    for _ in range(200):
        time.sleep(0.05)
        if m.read(S("cp_dirty"), 1)[0] == 0:
            break
    else:
        raise RuntimeError("ui_task never drained [cp_dirty]")
    os88marty.settle(m)
    m.cmd(cmd="pause")


def repaint_diff(m, mo, what, corner=None, win=None, soft=False):
    """0 means the incremental drawing agrees with a full repaint.

    The mouse arrow is NOT excluded: the repaint runs inside `ui_task`'s own
    gfx_lock/gfx_unlock, so the pointer comes off and goes back exactly as
    every drawing path in the system does it.

    ONE pixel is excluded, and it is not this app's. A window's own
    (W_X, W_Y + W_H) - the bottom-left corner of its drop shadow - reads
    differently on the incremental path and after a full repaint. It is
    PRE-EXISTING and belongs to the window manager: measured on this same
    harness, two Disk windows with no package open differ by 0 pixels, and
    opening `hello` - which draws nothing but a greeting - reproduces it
    exactly, one pixel, at its own (199,175). No package can produce it: the
    gfx primitives are handed content coordinates and this pixel is outside
    every content box. Excluding it keeps this test about the Calculator.
    """
    px, py = mo.where()[:2]
    m.cmd(cmd="pause")
    w, h, before = frame(m)
    full_repaint(m)
    _, _, after = frame(m)
    d = [i for i in range(len(before)) if before[i] != after[i]]
    if corner:
        d = [i for i in d if (i % w, i // w) != corner]
    # THE CLOCK IS EXCLUDED, and it is the harness's own doing. The forced
    # repaint is the guest's now (full_repaint), so it takes real WALL time -
    # a second or two of settling - and the menu bar's clock changes once a
    # MINUTE (SPEC.md 12.9). Sooner or later a run straddles that edge and two
    # glyph cells at the top right differ for a reason that has nothing to do
    # with any window: measured, 42 pixels at x 616..630, y 6..12. The field
    # starts at [vid_clk_hx] and no package can draw there - windows clamp to
    # y >= MBAR_H - so dropping it costs this test nothing.
    clk = u16(m.read(S("vid_clk_hx"), 2))
    d = [i for i in d if not (i // w < MBAR_H and i % w >= clk)]
    # THE ARROW IS EXCLUDED ONLY WHERE THIS APP CANNOT DRAW. Parked on bare
    # desktop, its 8x12 cell reads differently before and after a forced
    # repaint - the dither under it is redrawn and the arrow recomposed over
    # it - and no package can reach those pixels: gfx primitives are handed
    # content coordinates. Over the WINDOW it is not excluded, because that is
    # exactly where SPEC.md 6.1.9 hid.
    if win and not (win[0] <= px <= win[0] + win[2]
                    and win[1] <= py <= win[1] + win[3]):
        d = [i for i in d
             if not (px - 1 <= i % w <= px + 8 and py - 1 <= i // w <= py + 12)]
    print("   %-30s %d differing pixels of %d after a forced wm_paint_all"
          % (what + ":", len(d), len(before)))
    if d:
        xs = [i % w for i in d]
        ys = [i // w for i in d]
        print("      bbox x %d..%d  y %d..%d"
              % (min(xs), max(xs), min(ys), max(ys)))
        if not soft:
            fails.append("%s left %d pixels a full repaint disagrees with"
                         % (what, len(d)))
    m.cmd(cmd="run")
    os88marty.settle(m)
    return len(d)


def menu_pick(m, mo, cell, item):
    """Drop menu-bar cell `cell` and pick item `item`.

    The x comes out of the kernel's own `menu_bar` (MB_XL), not out of
    arithmetic on MENU_NAME_X and the title widths: the bar is REBUILT
    whenever the owner changes (SPEC.md 12.2), so its cells are a runtime
    fact, and a computed x is a second opinion about a table that is right
    there. Cell 0 is the chip and an app's own menus start at 1: the app-NAME
    cell (SPEC.md 12.7) is APPENDED LAST in the table even though it is drawn
    leftmost, precisely so that bar index == set index + 1.
    """
    t = m.read(S("menu_bar") + cell * MB_ENTSZ, MB_ENTSZ)
    x = u16(t, MB_XL) + 6
    mo.menu(x, 8, x, MBAR_H + 1 + item * MENU_ITEM_H + 8)
    os88marty.settle(m)


def menu_title(m, seg, cell):
    """The cell's title, read out of the bar - so a test that picks the wrong
    menu says which one, instead of failing three steps later. Every string a
    set points at is an offset in the OWNING WINDOW's segment (SPEC.md 12.2),
    which is what MB_SEG carries; the kernel's own cells have MB_SEG 0."""
    t = m.read(S("menu_bar") + cell * MB_ENTSZ, MB_ENTSZ)
    p, sg = u16(t, 0), u16(t, 12)
    if not p:
        return "<logo>"
    return m.read((sg or KERNEL_SEG) * 16 + p,
                  12).split(b"\0")[0].decode("latin-1")


def hist_line(m, seg, cal, row):
    """History ring slot `row` (0 = newest), as text."""
    head = m.read(seg * 16 + cal["hhead"], 1)[0]
    n = m.read(seg * 16 + cal["hn"], 1)[0]
    if row >= n:
        return None
    slot = (head - row) & (CAL_HIST_N - 1)
    off = cal["htext"] + slot * (CAL_HROW_N + 1)
    return m.read(seg * 16 + off, CAL_HROW_N).decode("latin-1")


def shown(m, seg, cal):
    """The number field, as the glass was lettered from it."""
    return bss(m, seg, cal["nbuf"], CAL_NUM_N).decode("latin-1").strip()


def context(m, seg, cal):
    return bss(m, seg, cal["cbuf"], CAL_CTX_N).decode("latin-1").strip()


# os88marty.type_text covers letters, digits, space and Enter; a calculator is
# mostly the characters it does not. The unshifted ones are their own
# MartyKey, and '+' / '*' / '%' are ShiftLeft held across one - which is the
# real 8255 path either way, so the guest sees exactly what a hand sends.
# '+' is the NUMPAD plus, not Shift+Equal, and that is not cosmetic: a key
# injected while the machine is PAUSED lands in the emulator's queue with no
# time between the events, and a three-event shifted key then did not arrive
# at all - measured as an operator that changed 0 pixels, which reads as the
# app not drawing rather than as the key never reaching it. Scancode 0x4E is
# unshifted and NumLock does not touch it.
PLAIN = {".": "Period", "/": "Slash", "-": "Minus", "=": "Equal",
         "+": "NumpadAdd", "\b": "Backspace"}
SHIFTED = {"*": "Digit8", "%": "Digit5"}


def press(m, ch):
    if ch in SHIFTED:
        m.key("ShiftLeft", down=True, up=False)
        m.key(SHIFTED[ch])
        m.key("ShiftLeft", down=False, up=True)
    elif ch in PLAIN:
        m.key(PLAIN[ch])
    else:
        m.type_text(ch)


def typed(m, text, settle=True):
    for ch in text:
        press(m, ch)
        time.sleep(0.15)
    if settle:
        os88marty.settle(m)


def ctrl(m, letter, settle=True):
    """Ctrl+<letter> as the 8255 sees it: the modifier is HELD across the
    key, exactly as SHIFTED above does it, so what arrives at int 16h is the
    control byte (3 for C, 22 for V) and not the letter."""
    m.key("ControlLeft", down=True, up=False)
    time.sleep(0.05)
    m.key("Key" + letter.upper())
    time.sleep(0.05)
    m.key("ControlLeft", down=False, up=True)
    time.sleep(0.15)
    if settle:
        os88marty.settle(m)


with os88marty.launch("build/os8088-360.img", apps="build/apps360.img",
                      machine=MACHINE) as m:
    mo = os88mouse.Mouse(marty=m)

    # --- get the Calculator up ------------------------------------------------
    dispcp.open_drive(m, mo, S, os88marty.settle, "B")
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, 0)          # APPS
    w = dispcp.win_list(m, S)
    wx, wy, ww, wh = dispcp.win_rect(m, S, w[-1])
    dispcp.open_row(m, mo, S, os88marty.settle, wx, wy, 2)          # CALC.O88
    time.sleep(2)
    slot = dispcp.win_list(m, S)[-1]
    cx, cy, cw, ch = dispcp.win_rect(m, S, slot)
    seg = pkg_seg(m, slot)
    print("CALC window slot %d at (%d,%d) %dx%d, package segment %04X"
          % (slot, cx, cy, cw, ch, seg))
    if cw != CAL_CW + 2:
        fails.append("the window is %dpx wide, not the %d CAL_CW asks for"
                     % (cw, CAL_CW + 2))

    # The bss layout, from the source's own equ block. os88sym answers for the
    # KERNEL; a package's bss is at fixed offsets past its image, and the one
    # number needed to reach it is where that image ends - which the package
    # header carries at +8 (SPEC.md 20.2).
    img_end = u16(bss(m, seg, 0, 32), 8)
    cal = {"nvis": img_end + 8, "open": img_end + 22, "err": img_end + 23,
           "op": img_end + 24, "hn": img_end + 27, "hhead": img_end + 28,
           "cbuf": img_end + 104, "nbuf": img_end + 136,
           "htext": img_end + 296}
    print("image ends at +%d; [cal_nvis] at +%d" % (img_end, cal["nvis"]))

    # The BASELINE, before a single key: whatever this reports is the window
    # manager's and not the app's, because nothing here has drawn twice yet.
    print("\n-- the baseline, before anything has been typed --")
    repaint_diff(m, mo, "freshly opened", (cx, cy + ch), (cx, cy, cw, ch))

    # --- 1. arithmetic --------------------------------------------------------
    print("\n-- arithmetic, through the real keyboard path --")
    cases = [
        ("12+34=",        "46",           "two integers"),
        ("c1.5*1.5=",     "2.25",         "the decimals add"),
        ("c1/3=",         "0.33333333",   "eight decimals, not zero"),
        ("c1000*1000=",   "1000000",      "past a 10^4 fixed point's range"),
        ("c7-9=",         "-2",           "a negative result"),
        ("c2.50",         "2.50",         "a typed trailing zero is kept"),
        ("c2.50+0=",      "2.5",          "...and a COMPUTED one is stripped"),
        ("c9q",           "3",            "square root"),
        ("c2q",           "1.41421356",   "...and an irrational one"),
        ("c4r",           "0.25",         "reciprocal"),
        ("c200+10%=",     "220",          "percent of the accumulator"),
        ("c5/0=",         "Error",        "a zero divisor is refused"),
        ("c0.05+0.05=",   "0.1",          "trailing zeros are stripped"),
        ("c999999999+1=", "Error",        "a tenth digit has nowhere to go"),
        ("c123456789",    "123456789",    "...but nine of them fit"),
        ("c8*8=*2=",      "128",          "the answer chains into the next sum"),
        ("c5+3=",         "8",            "and the machine still works after"),
    ]
    for keys, want, why in cases:
        typed(m, keys)
        got = shown(m, seg, cal)
        ok = got == want
        print("   %-14s -> %-12s %s   (%s)"
              % (keys, repr(got), "ok" if ok else "WANTED " + repr(want), why))
        if not ok:
            fails.append("%s gave %r, wanted %r (%s)" % (keys, got, want, why))

    typed(m, "c12+")
    ctx = context(m, seg, cal)
    print("   pending expression reads %r" % ctx)
    if ctx != "12 +":
        fails.append("the context field reads %r, wanted '12 +'" % ctx)
    typed(m, "c")

    # --- the KEYPAD, clicked (SPEC.md 65.5) ----------------------------------
    #
    # THE KEYS ARE PRESSED WITH THE MOUSE HERE, and that is not a duplicate of
    # the block above. Every case so far went in through W_ONKEY, which shares
    # the engine with the buttons and shares NOTHING with cal_layout - so a
    # keypad laid out at the wrong coordinates passes all of it. It happened:
    # `mul` writes DX, cal_layout held the row's y there, and all twenty
    # buttons were built at y = 0 and drawn stacked over the MENU BAR with
    # nothing in the window at all. Every keyboard case still passed, and the
    # forced-repaint diff passed too - the wrong picture was drawn
    # consistently by both paths.
    print("\n-- the keypad, clicked (SPEC.md 65.5) --")
    typed(m, "c")
    # The + step wants '' and not '12': the operator empties the field
    # (SPEC.md 65.2.1). The operand is still live, which the = two steps later
    # is what proves - 12 + 4 = 16.
    for i, want in ((12, "1"), (13, "12"), (15, ""), (8, "4"), (19, "16")):
        kxp, kyp = key_xy(cx, cy, i)
        mo.click(kxp, kyp)
        os88marty.settle(m)
        got = shown(m, seg, cal)
        ok = got == want
        print("   %-4s at (%3d,%3d) -> %-6s %s"
              % (KEYPAD[i], kxp, kyp, repr(got),
                 "ok" if ok else "WANTED " + repr(want)))
        if not ok:
            fails.append("clicking the %s key gave %r, wanted %r"
                         % (KEYPAD[i], got, want))
    mo.to(cx - 60, cy + 4)
    os88marty.settle(m)

    # --- the clipboard round-trips (SPEC.md 55/65.5) -------------------------
    #
    # A PASTE MUST GIVE BACK WHAT WAS COPIED, and the way it failed is the
    # reason this is its own case: the paste fed cal_key one character at a
    # time and cal_key ends in cal_show, which composes the display through
    # the same scratch buffer the paste was being read out of - so each
    # keystroke overwrote the characters not yet read. '88' came back '87'.
    print("\n-- copy and paste (SPEC.md 55) --")
    bar = [menu_title(m, seg, c) for c in range(5)]
    print("   the bar reads: %s" % " | ".join(bar))
    if bar[1] != "Edit":
        fails.append("menu cell 1 is %r, not Edit - the picks below are wrong"
                     % bar[1])
    for typedin, want in (("88", "88"), ("12.5", "12.5"),
                          ("7", "7"), ("100.25", "100.25"),
                          ("123456789", "123456789")):
        typed(m, "c" + typedin)
        menu_pick(m, mo, 1, 0)                  # Edit > Copy
        typed(m, "c")
        menu_pick(m, mo, 1, 1)                  # Edit > Paste
        got = shown(m, seg, cal)
        ok = got == want
        print("   copied %-10s pasted -> %-11s %s"
              % (repr(typedin), repr(got), "ok" if ok else "WANTED " + repr(want)))
        if not ok:
            fails.append("copied %r and pasted %r" % (typedin, got))
    typed(m, "c1.5+")                           # a negative, through the sign
    typed(m, "c")
    typed(m, "c8n")
    menu_pick(m, mo, 1, 0)
    typed(m, "c")
    menu_pick(m, mo, 1, 1)
    got = shown(m, seg, cal)
    print("   copied '-8'      pasted -> %-11s %s"
          % (repr(got), "ok" if got == "-8" else "WANTED '-8'"))
    if got != "-8":
        fails.append("copied -8 and pasted %r" % got)

    typed(m, "c777")                        # does the KEYBOARD still work
    probe = shown(m, seg, cal)               # after a menu interaction?
    print("   the keyboard after the menus: %r" % probe)
    if probe != "777":
        fails.append("after using the menus the keyboard gave %r" % probe)

    # --- ...and the same two verbs from the keyboard (SPEC.md 65.5) ----------
    #
    # THE HOTKEYS ARE NOT THE MENU ITEMS, however identical the outcome: they
    # arrive as int 16h bytes 3 and 22 through cal_asckey, past a table where
    # 'c' is already Clear and 'v' is nothing at all. So the case worth
    # asserting is that Ctrl-C is not read as a `c` - which would CLEAR the
    # number instead of copying it, silently, and leave the clipboard holding
    # whatever it held before.
    print("\n-- the clipboard hotkeys, Ctrl-C and Ctrl-V --")
    for typedin in ("88", "12.5", "123456789"):
        typed(m, "c" + typedin)
        ctrl(m, "C")
        held = shown(m, seg, cal)
        typed(m, "c")
        ctrl(m, "V")
        got = shown(m, seg, cal)
        ok = got == typedin and held == typedin
        print("   %-11s Ctrl-C left %-11s Ctrl-V gave %-11s %s"
              % (repr(typedin), repr(held), repr(got),
                 "ok" if ok else "WANTED " + repr(typedin)))
        if held != typedin:
            fails.append("Ctrl-C changed the display to %r (a bare 'c' would "
                         "clear it)" % held)
        if got != typedin:
            fails.append("Ctrl-C then Ctrl-V gave %r, wanted %r"
                         % (got, typedin))

    # --- an operator empties the number field (SPEC.md 65.2.1) --------------
    print("\n-- an operator empties the number field (SPEC.md 65.2.1) --")
    cases = [
        ("c12+",        "",     "12 +",  "the operand moved left, right blank"),
        ("=",           "24",   "",      "...and = still uses it: 12 + 12"),
        ("c12+5=",      "17",   "",      "a digit after the operator is new"),
        ("c2+3+",       "",     "5 +",   "a chain shows the running total left"),
        ("c2+q",        "1.41421356", "2 +", "a unary key acts, so it un-blanks"),
    ]
    for keys, wnum, wctx, why in cases:
        typed(m, keys)
        gnum, gctx = shown(m, seg, cal), context(m, seg, cal)
        ok = gnum == wnum and gctx == wctx
        print("   %-10s -> number %-12s context %-8s %s   (%s)"
              % (keys, repr(gnum), repr(gctx),
                 "ok" if ok else "WANTED %r / %r" % (wnum, wctx), why))
        if not ok:
            fails.append("%s showed %r / %r, wanted %r / %r"
                         % (keys, gnum, gctx, wnum, wctx))

    # BACKSPACE IS THE EXCEPTION and it is the one that broke first: every
    # other key clears the wait at one choke point, and backspace does nothing
    # to a result - so clearing on it would put the operand back on a field
    # that is asking for a new number.
    typed(m, "c12+")
    typed(m, "\b")
    gnum = shown(m, seg, cal)
    print("   backspace after `12 +` leaves %r (it must not resurrect '12')"
          % gnum)
    if gnum != "":
        fails.append("backspace after an operator put %r back" % gnum)

    # ...and Copy takes the LIVE value, because the operand was taken and not
    # forgotten - the field is blank, the number is not gone.
    typed(m, "c12+")
    ctrl(m, "C")
    typed(m, "c")
    ctrl(m, "V")
    got = shown(m, seg, cal)
    print("   Ctrl-C with the field blank copied %r (the live operand)" % got)
    if got != "12":
        fails.append("Copy on a blank field gave %r, wanted the operand '12'"
                     % got)
    # ...and the blanking ERASES cells, which is the half of cal_emit's span
    # diff nothing else here exercises: every case above it re-letters a cell,
    # this one takes eleven of them away.
    typed(m, "c123456789+")
    mo.to(cx - 60, cy + 4)
    os88marty.settle(m)
    repaint_diff(m, mo, "after the field was emptied",
                 (cx, cy + ch), (cx, cy, cw, ch))
    typed(m, "c")

    # --- a history line keeps its HEAD (SPEC.md 65.4.3) ----------------------
    print("\n-- a history line too long for the field --")
    typed(m, "c12345.678x1234.5=")
    line = hist_line(m, seg, cal, 0)
    print("   the row reads %r" % line)
    if not line.startswith("12345.678 * 1234.5 ="):
        fails.append("the history line %r does not open with the expression"
                     % line)
    if line[-1] != "~":
        fails.append("the history line %r is not marked as cut" % line)
    typed(m, "c2+2=")
    short = hist_line(m, seg, cal, 0)
    print("   a line that FITS reads %r" % short)
    if short.rstrip() != "2 + 2 = 4":
        fails.append("a short history line reads %r" % short)
    if "~" in short:
        fails.append("a line that fits was marked as cut: %r" % short)

    print("\n-- the incremental drawing agrees with a full repaint --")
    mo.to(cx - 60, cy + 4)                   # the pointer off the bar and the
    os88marty.settle(m)                      # window before the diff
    repaint_diff(m, mo, "after arithmetic", (cx, cy + ch), (cx, cy, cw, ch))
    typed(m, "c555")
    probe = shown(m, seg, cal)
    print("   the keyboard after a forced repaint: %r" % probe)
    if probe != "555":
        fails.append("after a forced repaint the keyboard gave %r" % probe)

    # --- 2. the fold is DERIVED ----------------------------------------------
    print("\n-- folding the history down (SPEC.md 65.3) --")
    hn = bss(m, seg, cal["hn"], 1)[0]
    print("   %d calculations kept (the ring holds %d)" % (hn, CAL_HIST_N))
    if hn == 0:
        fails.append("no calculation was recorded in the history at all")

    typed(m, "h")
    time.sleep(1.5)
    os88marty.settle(m)
    cx2, cy2, cw2, ch2 = dispcp.win_rect(m, S, slot)
    content_h = ch2 - TITLE_H - 1
    nvis = u16(bss(m, seg, cal["nvis"], 2))
    want = max(0, min(CAL_HIST_N, (content_h - CAL_BASE_H) // CAL_ROW_H))
    print("   frame is now %dx%d -> %d rows of content" % (cw2, ch2, content_h))
    print("   [cal_nvis] = %d, and (%d - %d) / %d clamped to 0..%d is %d"
          % (nvis, content_h, CAL_BASE_H, CAL_ROW_H, CAL_HIST_N, want))
    if nvis != want:
        fails.append("[cal_nvis] is %d, but the box it was given holds %d"
                     % (nvis, want))
    if nvis == 0:
        fails.append("folding the pane down showed no rows at all")
    dock = u16(m.read(S("vid_dock_y0"), 2))
    print("   window ends at y=%d, the dock starts at %d" % (cy2 + ch2, dock))
    if cy2 + ch2 > dock:
        fails.append("the window hangs over the dock: y+h = %d, dock at %d"
                     % (cy2 + ch2, dock))
    repaint_diff(m, mo, "history folded down", (cx2, cy2 + ch2), (cx2, cy2, cw2, ch2))

    # --- 3. a completed sum scrolls the band ---------------------------------
    print("\n-- a sum with the pane open scrolls it (SPEC.md 65.4.1) --")
    typed(m, "c11*11=")
    repaint_diff(m, mo, "after a sum scrolled the band", (cx2, cy2 + ch2), (cx2, cy2, cw2, ch2))

    # --- 4. clicking a row loads its ANSWER ----------------------------------
    print("\n-- a history row loads its answer (SPEC.md 65.4.1) --")
    typed(m, "c")
    row = min(1, nvis - 1) if nvis else 0
    rx = cx2 + 1 + 60
    ry = cy2 + TITLE_H + 1 + CAL_BASE_H + row * CAL_ROW_H + CAL_ROW_H // 2
    mo.click(rx, ry)
    os88marty.settle(m)
    got = shown(m, seg, cal)
    print("   clicking row %d at (%d,%d) put %r in the entry"
          % (row, rx, ry, got))
    if got in ("", "0"):
        fails.append("clicking history row %d loaded nothing (%r)" % (row, got))
    # THE POINTER STAYS ON THE ROW, and that is the assertion. It used to be
    # soft, because what it found - the arrow's top rows missing, healing the
    # moment the hand moved - was read as the harness disturbing the cursor.
    # It was not: SPEC.md 6.1.9 is the kernel bug it was reporting, and every
    # package that letters through OSAPI_FONT_RUN on a 1bpp adapter had it.
    repaint_diff(m, mo, "...with the pointer still on the row",
                 (cx2, cy2 + ch2), (cx2, cy2, cw2, ch2))
    mo.to(cx2 + cw2 + 40, cy2 + 4)
    os88marty.settle(m)
    repaint_diff(m, mo, "after a history row was clicked",
                 (cx2, cy2 + ch2), (cx2, cy2, cw2, ch2))

    # --- 5. a pressed key slid off cancels -----------------------------------
    print("\n-- a press slid off a key cancels and leaves nothing behind --")
    kx = cx2 + 1 + 4 + 25
    ky = cy2 + TITLE_H + 1 + 24 + 7            # the C key's centre
    before = shown(m, seg, cal)
    mo.to(kx, ky)
    mo._edge(True)
    time.sleep(0.5)
    mo.to(cx2 + 1 + 200, ky)                   # ...slide off it
    mo._edge(False)
    os88marty.settle(m)
    after = shown(m, seg, cal)
    print("   entry %r -> %r (a cancelled press must change nothing)"
          % (before, after))
    if before != after:
        fails.append("a press slid off a key still acted on it")
    repaint_diff(m, mo, "after a cancelled press", (cx2, cy2 + ch2), (cx2, cy2, cw2, ch2))

    # --- 6. folding back up --------------------------------------------------
    print("\n-- folding it back up --")
    typed(m, "h")
    time.sleep(1.5)
    os88marty.settle(m)
    cx3, cy3, cw3, ch3 = dispcp.win_rect(m, S, slot)
    nvis3 = u16(bss(m, seg, cal["nvis"], 2))
    print("   frame back to %dx%d, [cal_nvis] = %d" % (cw3, ch3, nvis3))
    if nvis3 != 0:
        fails.append("[cal_nvis] is %d with the pane folded up" % nvis3)
    if ch3 != CAL_BASE_H + TITLE_H + 1:
        fails.append("folded up the frame is %d rows, wanted %d"
                     % (ch3, CAL_BASE_H + TITLE_H + 1))
    repaint_diff(m, mo, "history folded back up", (cx3, cy3 + ch3), (cx3, cy3, cw3, ch3))

    # --- 7. and it still adds up after all that ------------------------------
    typed(m, "c6*7=")
    got = shown(m, seg, cal)
    print("\n   6*7 = %r after the whole session" % got)
    if got != "42":
        fails.append("6*7 gave %r at the end of the session" % got)

print()
if fails:
    print("dispcalc: FAIL")
    for f in fails:
        print("  - " + f)
    sys.exit(1)
print("dispcalc: the Calculator adds up, folds to the box it was given, and "
      "its incremental drawing matches a full repaint - PASS")
