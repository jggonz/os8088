"""WaveRack's screenshots, for tools/guide/manuals/WAVERACK_O88.md.

No 360KB disk carries WaveRack, so run it with the WAV fixture floppy
(`make waverackfix`) on the VGA XT with two 1.44MB drives - which has no
Sound Blaster, so the pictures show a WAV open, a selection and the compact
layout, and the manual says what playing and recording need:

    python3 tools/guide/capture/run.py WAVERACK_O88 --image build/os8088.img \\
        --apps build/wrfix.img --machine os8088_xt_vga_144
"""
import os88marty

TITLE = "WaveRack"


def capture(ui, shot):
    ui.path("B:/WAVERACK.O88")
    ui.wait_window(TITLE)
    os88marty.guest_sleep(ui.m, 4.0)
    ui.menu_pick("File", "Open...")
    ui.chooser()
    ui.chooser_open("LONG.WAV")              # (the chooser opens on MEDIA)
    os88marty.guest_sleep(ui.m, 120.0)      # the scan of 1.1 MB
    ui.mo.to(636, 470)
    shot("waverack-window", window=TITLE, pad=4)

    # a selection DRAGGED across the waveform, as a user makes one (Shift
    # held through MartyPC's key events did not register: the first capture
    # moved the cursor and selected nothing)
    x, y, w, h = ui._rect(ui.window(TITLE).i)
    ly = y + 255                        # mid-lane, FULL's waveform
    ui.mo.to(x + 140, ly)
    ui.mo._edge(True)
    ui.mo.to(x + 260, ly, l=True)       # (the button held through the move)
    ui.mo.to(x + 330, ly, l=True)
    ui.mo._edge(False)
    ui.mo.to(636, 470)
    os88marty.guest_sleep(ui.m, 3.0)
    shot("waverack-selection", window=TITLE, pad=4)

    ui.menu_pick("View", "Compact Layout")
    os88marty.guest_sleep(ui.m, 6.0)
    shot("waverack-compact", window=TITLE, pad=4)
