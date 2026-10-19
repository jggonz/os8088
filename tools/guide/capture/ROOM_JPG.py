"""ROOM.JPG alone, for ROOM_JPG.md: a progressive JPEG takes the longest of
the gallery to appear, so it can be retaken on its own.

    python3 tools/guide/capture/run.py ROOM_JPG --machine os8088_xt_vga_144 \\
        --image build/os8088.img --apps build/pixel.img
"""
import importlib.util
import os

_p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "VACATION_JPG.py")
_s = importlib.util.spec_from_file_location("vacation_cap", _p)
_v = importlib.util.module_from_spec(_s)
_s.loader.exec_module(_v)


def capture(ui, shot):
    ui.path("B:/PICTURES/ROOM.JPG")
    _v._take(ui, shot, _v._pix(ui))
