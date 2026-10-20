"""A song opened by double-click, for the MIDI song pages (FURELISE_MID.md and
the nine beside it). The music disk carries the songs in MEDIA/MIDI and
MIDIRack at its root; a Sound Blaster machine gives it an FM chip to play on:

    python3 tools/guide/capture/run.py FURELISE_MID --apps build/media360.img \\
        --machine os8088_xt_vga_sb
"""

import os88marty


def capture(ui, shot):
    ui.path("B:/MEDIA/MIDI/FURELISE.MID")
    w = ui.wait_window("MIDIRack")
    ui.mo.to(636, 470)
    os88marty.guest_sleep(ui.m, 12.0)
    print("titles:", ui.titles())
    shot("furelise-mid", window=w.title, pad=4, settle=False)
