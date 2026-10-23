"""Mixer's guide pictures.

Use --image build/mixer360.img --apps build/mixplay.img
--machine os8088_xt_vga_sb: SOUND is wanted and Mixer is on the apps disk.
"""


def capture(ui, shot):
    w = ui.path("B:/MIXER.O88")
    x, y, _, _ = w.content
    ui.mo.click(x + 240, y + 44)
    ui.settle()
    shot("mixer-window", window="Mixer", pad=10)
    ui.mo.click(x + 30, y + 31)
    ui.mo.click(x + 200, y + 44)
    ui.settle()
    shot("mixer-pcm", window="Mixer", pad=10)
    ui.menu_pick("Mixer", "About Mixer")
    ui.settle()
    shot("mixer-about", window="Mixer", pad=10)
