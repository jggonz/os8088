"""Video Player playing its CGA demo videos, for
tools/guide/manuals/VIDEO_O88.md.

    python3 tools/guide/capture/run.py VIDEO_O88_CGA

The disks are made the way VIDEO_O88_HERC.py makes them (its make_disks and
hook_boot, which this script loads): build/guide/scratch/vidcga.vhd holds
apps/video/demo/cga/{01-SONIC,04-MIKU,11-WILLY}.V88 and the player,
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
VIDEOS = ("cga/01-SONIC.V88", "cga/04-MIKU.V88", "cga/11-WILLY.V88")
VHD = os.path.join(H.SCRATCH, "vidcga.vhd")

H.make_disks(VHD, VIDEOS)
H.hook_boot(VHD, MACHINE)


def capture(ui, shot):
    # colour plays full screen: CGA's own four colours, 320 x 200 (the demo
    # set's RGB sixteen-colour clip went with its 2026-11-09 re-cut)
    w = H.open_video(ui, "04-MIKU.V88")
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 8.0)
    shot("video-cga-colour", settle=False)
    H.stop(ui, w)

    # Sonic 2 in 80 x 25 coloured text
    w = H.open_video(ui, "01-SONIC.V88")
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 8.0)
    shot("video-cga-text-alt", settle=False)
    os88marty.guest_sleep(ui.m, 6.0)
    shot("video-cga-text", settle=False)     # Sonic among the palm trees
    H.stop(ui, w)
