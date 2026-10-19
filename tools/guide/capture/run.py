#!/usr/bin/env python3
"""Take a manual's screenshots: boot once under MartyPC, run the manual's own
capture script, write build/guide/shots/<id>.png. Never committed.

    python3 tools/guide/capture/run.py CALC_O88 [--machine os8088_xt_vga]

A capture script is `tools/guide/capture/<KEY>.py` with one function,
`capture(ui, shot)`. `shot(id, window=None, pad=6)` writes the whole screen,
or the named window cropped with a little desktop around it. Keep a script
to one boot and a handful of shots: these are pictures for a manual, not a
test, and nothing here asserts anything.
"""
import argparse
import importlib.util
import os
import sys
import zlib
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui  # noqa: E402
import os88marty  # noqa: E402

OUT = os.path.join(ROOT, "build", "guide", "shots")


def png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("key")
    ap.add_argument("--machine", default="os8088_xt_vga")
    ap.add_argument("--image", default="build/os8088-360.img")
    ap.add_argument("--apps", default="build/apps360.img")
    a = ap.parse_args(argv)
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), a.key + ".py")
    spec = importlib.util.spec_from_file_location("cap", p)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    os.makedirs(OUT, exist_ok=True)
    with os88ui.boot(os.path.join(ROOT, a.image), apps=os.path.join(ROOT, a.apps),
                     machine=a.machine) as ui:
        def shot(sid, window=None, pad=6, rect=None, settle=True):
            """Write build/guide/shots/<sid>.png: the whole screen, the window
            whose title starts with `window` (plus `pad` pixels of desktop),
            or an explicit (x, y, w, h) `rect`."""
            if settle:
                ui.settle()
            w, h, data = ui.m.fbuf()
            x0, y0, x1, y1 = 0, 0, w, h
            if window is None and rect is None:
                # MartyPC sizes a VGA's first aperture at 400 rows in some
                # modes, which cuts the bottom 40 lines off a 320x240 Mode X
                # screen: read the whole field and crop to 480 rows instead.
                v = ui.m.video()
                aps = v.get("apertures") or []
                if v.get("type") == "vga" and aps and aps[0]["h"] < 480:
                    a0 = aps[0]
                    full = max(range(len(aps)), key=lambda i: aps[i]["w"] * aps[i]["h"])
                    w, h, data = ui.m.fbuf(full)
                    x0, y0 = a0["x"] - aps[full]["x"], a0["y"] - aps[full]["y"]
                    x1, y1 = x0 + a0["w"], min(h, y0 + 480)
            if rect is not None:
                x0, y0 = max(0, rect[0]), max(0, rect[1])
                x1, y1 = min(w, rect[0] + rect[2]), min(h, rect[1] + rect[3])
            elif window is not None:
                win = ui.window(window)
                x, y, ww, hh = ui._rect(win.i)
                x0, y0 = max(0, x - pad), max(0, y - pad)
                x1, y1 = min(w, x + ww + pad + 2), min(h, y + hh + pad + 2)
            cw = x1 - x0
            crop = b"".join(data[(yy * w + x0) * 3:(yy * w + x1) * 3] for yy in range(y0, y1))
            out = os.path.join(OUT, sid + ".png")
            png(out, cw, y1 - y0, crop)
            print("shot %s %dx%d" % (sid, cw, y1 - y0))
        mod.capture(ui, shot)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
