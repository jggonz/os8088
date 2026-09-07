#!/usr/bin/env python3
"""tests/lembenchmarty.py - RUN tests/lembench ON A 4.77 MHz 8088 (SPEC.md 92.7).

    python3 tests/lembenchmarty.py                 # every machine it can reach
    python3 tests/lembenchmarty.py --machine os8088_5150_cga

THIS IS WHERE THE NUMBERS COME FROM. `make test TESTAPPS=build/lembench.img`
boots the same bench under QEMU and every row reads 0 us, because QEMU is
exact about how much work the guest does and useless about how long it takes
(CLAUDE.md). MartyPC models the 8088's instruction timing, its prefetch queue
and its bus contention, so a microsecond measured here is a microsecond on the
target - and the whole of wave 2's raster design rests on four of these
numbers.

IT IS NOT A GATE AND IT IS NOT IN THE SUITE. It takes minutes, it needs
build/martypc, and what it produces is a TABLE for a human to read and paste
into SPEC.md 92.7 and PERFORMANCE.md - which is exactly what
docs/MARTYPC-DEBUG.md says the instrument is for: it writes a PNG of the
bench's own screen and a person reads the table off it, which is the same act
the bench was written for.

MARTYPC IS CYCLE-ACCURATE AND NOT DISK-ACCURATE (tools/os88marty.py's own
header): every figure below is CPU and VRAM only, which is what the raster is.
The one number in the bench with a disk in its path - there is none - would be
wrong here.
"""
import argparse
import os

import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import os88marty                                            # noqa: E402
import os88mouse                                            # noqa: E402

SYS360 = "build/os8088-360.img"
BENCH = "build/lembench360.img"

# The machines this can reach, and the backend each one exercises. MartyPC's
# os8088_xt_vga is the only one with a VGA in it; _5150_cga and _herc are the
# 4.77 MHz 8088 with a CGA and a Hercules.
# The machines this can reach, and the backend each one exercises.
#
# THEY ARE ALL IBM 5160s AND THAT IS THE ROM SET, not a choice: the 5150's
# ibm5150v256k feature resolves to a ROM set this tree does not carry
# (`ROM set ibm5150_82_v4 not found in ROM set map`), so every 5150 machine in
# tools/martypc/configs refuses at once. An XT is the same 8088 at the same
# 4.77 MHz, which is what these numbers are about.
#
# HERCULES IS NOT HERE and the reason is worth stating: os8088_xt_vga_herc is a
# TWO-CARD machine whose primary is the VGA, so reaching the Hercules backend
# needs a VIDEO=herc kernel as well as that machine. The shadow backend is ONE
# composer with two presenters (lemblit.inc), and the presenter is the only
# thing that differs - 16,000 bytes to a four-way interleave against 16,000 to
# a two-way - so the CGA row bounds it to within the blit.
MACHINES = [
    ("os8088_xt_vga", "VGA mode 0Dh, 4.77 MHz 8088 (IBM 5160)"),
    ("os8088_xt_hdd", "CGA mode 4, 4.77 MHz 8088 (IBM 5160)"),
]


def _png(path, w, h, px):
    """m.fbuf()'s rgb24 into a PNG, so the table can be LOOKED at.

    The bench letters its own results, and reading them off the rasterised
    frame is the same act a person does - which is the point: what a script
    checks and what a reader checks are the same pixels
    (docs/MARTYPC-DEBUG.md).
    """
    import struct
    import zlib

    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + \
            struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))
    return path


def run(machine, what, png_dir, wait):
    print("== %s (%s)" % (machine, what))
    if not os.path.exists(BENCH):
        print("   %s is missing - run `make lembench` first" % BENCH)
        return 1
    os.makedirs(png_dir, exist_ok=True)
    with os88marty.launch(SYS360, apps=BENCH, machine=machine,
                          label="lembench") as m:
        mo = os88mouse.Mouse(marty=m)
        # THE DESKTOP'S HEIGHT DECIDES WHERE THE ICONS ARE, and hard-coding a
        # VGA's is how the first CGA run photographed an untouched desktop: on
        # a 640x200 screen drive B's icon is at y = 86 and not 110, so both
        # double-clicks landed on the wallpaper and the bench never launched
        # (build/port-shots/wave2-lembench-cga-missed.png). SPEC.md 39 in one
        # line: nothing anchors to a screen edge without reading it.
        w, h, _px = m.fbuf()
        if h <= 240:
            b_y, row_y = 86, 68
        else:
            b_y, row_y = 110, 128
        mo.dblclick(w - 38, b_y)                # drive B
        time.sleep(2)
        mo.dblclick(165, row_y)                 # LEMBENCH.O88, the first row
        time.sleep(3)
        _png(os.path.join(png_dir, "wave2-lembench-%s-idle.png" % machine),
             *m.fbuf())
        m.key("Space")
        # THE BENCH IS MINUTES ON A 4.77 MHz 8088 and there is nothing to poll:
        # its results are lettered when it comes back out of the bracket, and
        # until then the screen is a foreign mode. So this waits, takes the
        # picture, and the reader reads it.
        # IT NUDGES THE MOUSE WHILE IT WAITS, and the first run needed the
        # lesson: 420 seconds later the picture was SEA LIFE - the bench had
        # finished, the desktop had come back and the screen saver had taken
        # it, so what the PNG carried was a fish
        # (build/port-shots/wave2-lembench-saver.png). Moving the pointer a
        # pixel every half minute costs the guest nothing and keeps the saver
        # off; the bench does not read the mouse at all.
        print("   running (%d s)..." % wait)
        left = wait
        nudge = 0
        while left > 0:
            time.sleep(30 if left > 30 else left)
            left -= 30
            nudge = 1 - nudge
            try:
                mo.to(300 + nudge, 300)
            except Exception:                               # noqa: BLE001
                pass
        time.sleep(2)
        path = _png(os.path.join(png_dir, "wave2-lembench-%s.png" % machine),
                    *m.fbuf())
        print("   wrote %s" % path)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine")
    ap.add_argument("--png-dir", default="build/port-shots")
    ap.add_argument("--wait", type=int, default=420,
                    help="seconds to let the bench run before the picture")
    a = ap.parse_args()
    if not os.path.exists("build/martypc/run/martypc_headless"):
        print("lembenchmarty: SKIP - build/martypc is not built "
              "(brew rust; tools/martypc/build.sh)")
        return 0
    bad = 0
    for name, what in MACHINES:
        if a.machine and a.machine != name:
            continue
        try:
            bad |= run(name, what, a.png_dir, a.wait)
        except Exception as e:                              # noqa: BLE001
            print("   %s: %s" % (name, str(e)[:200]))
            bad = 1
    return bad


if __name__ == "__main__":
    sys.exit(main())
