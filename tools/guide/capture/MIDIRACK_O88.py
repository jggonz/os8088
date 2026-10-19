"""MIDIRack's screenshots, for tools/guide/manuals/MIDIRACK_O88.md.

Run with MIDIRack's own disk (the player, its songs and the wavetable bank)
on a VGA XT with an AdLib and a Sound Blaster:

    python3 tools/guide/capture/run.py MIDIRACK_O88 \\
        --apps build/midirack360.img --machine os8088_xt_vga_sb
"""
import os88marty

TITLE = "MIDIRack"


def _menu_shot(ui, shot, title, sid):
    cells = ui.menus()
    cell = [c for c in cells if c[0].upper() == title.upper()][0]
    _, x0, x1, items = cell
    ui.mo.to((x0 + x1) // 2, 5)
    ui.mo._edge(True)
    os88marty.guest_sleep(ui.m, 2.0)
    mx1, mx2 = ui._word("menu_x1"), ui._word("menu_x2")
    my2 = ui._word("menu_y2")
    left = max(0, min(x0, mx1) - 6)
    shot(sid, rect=(left, 0, max(x1, mx2) - left + 8, my2 + 6), settle=False)
    ui.mo.to(600, 460, l=True)
    ui.mo._edge(False)
    os88marty.guest_sleep(ui.m, 1.5)


def capture(ui, shot):
    # Opened with nothing, MIDIRack lists the songs in MEDIA\MIDI\ itself.
    ui.path("B:/MIDIRACK.O88")
    ui.wait_window(TITLE)
    os88marty.guest_sleep(ui.m, 8.0)
    print("menus:", [(c[0], c[3]) for c in ui.menus()])

    # The third song, played with Enter.
    ui.m.key("ArrowDown")
    ui.m.key("ArrowDown")
    ui.m.key("Enter")
    ui.mo.to(636, 470)
    os88marty.guest_sleep(ui.m, 6.0)
    shot("midirack-window", window=TITLE, pad=4, settle=False)

    ui.m.key("KeyI")                    # MIDI Info
    os88marty.guest_sleep(ui.m, 2.0)
    shot("midirack-info", window=TITLE, pad=4, settle=False)
    ui.m.key("KeyI")                    # any key takes the card down
    os88marty.guest_sleep(ui.m, 2.0)

    _menu_shot(ui, shot, "Options", "midirack-options-menu")

    ui.menu_pick("Options", "Settings...")
    os88marty.guest_sleep(ui.m, 2.0)
    shot("midirack-settings", window=TITLE, pad=4, settle=False)
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 2.0)
