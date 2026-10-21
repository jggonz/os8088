"""Video Player playing its CGA demo videos, for
tools/guide/manuals/VIDEO_O88.md.

    python3 tools/guide/capture/run.py VIDEO_O88_CGA

The disks are made the way VIDEO_O88_HERC.py makes them (its make_disks and
hook_boot, which this script loads): build/guide/scratch/vidcga.vhd holds
apps/video/demo/cga/{01-COTXT,03-CGA4,07-CGA16,11-LIVE}.V88 and the player,
mounted as C: on the CGA 5150 with a hard disk (os8088_5150_cga_hdd_gla, no
sound card, so the plays are silent). Composite videos (08, 09, 10) are left
off: MartyPC's capture is RGB and shows them as stripes.
"""

import importlib.util
import os

import os88marty

_spec = importlib.util.spec_from_file_location(
    "video_o88_herc", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "VIDEO_O88_HERC.py"))
H = importlib.util.module_from_spec(_spec)
H.SKIP_SETUP = True
_spec.loader.exec_module(H)

MACHINE = "os8088_5150_cga_hdd_gla"
VIDEOS = ("cga/01-COTXT.V88", "cga/03-CGA4.V88", "cga/07-CGA16.V88",
          "cga/11-LIVE.V88")
VHD = os.path.join(H.SCRATCH, "vidcga.vhd")

H.make_disks(VHD, VIDEOS)
H.hook_boot(VHD, MACHINE)


def capture(ui, shot):
    # colour plays full screen: sixteen colours, 160 x 75, through the
    # text-mode trick...
    w = H.open_video(ui, "07-CGA16.V88")
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 8.0)
    shot("video-cga-colour", settle=False)
    H.stop(ui, w)

    # ...and CGA's own four colours, 320 x 150, as an alternative
    w = H.open_video(ui, "03-CGA4.V88")
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 6.0)
    shot("video-cga-colour-alt", settle=False)
    H.stop(ui, w)

    # Sonic 2 in 80 x 25 coloured text
    w = H.open_video(ui, "01-COTXT.V88")
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 8.0)
    shot("video-cga-text-alt", settle=False)
    os88marty.guest_sleep(ui.m, 6.0)
    shot("video-cga-text", settle=False)     # Sonic among the palm trees
    H.stop(ui, w)
