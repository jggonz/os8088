#!/usr/bin/env python3
"""Run a play/idle-mode tests/rdgcore image in QEMU and render its last frame.

    python3 tests/rdgcore/rdgshot.py IMG -o OUT.pgm [--timeout S]

MODES 3 and 4 of tests/rdgcore/rdghost.asm end by sending VRAM, OAM, the
LCD registers, A000-A0FF and the CPU's registers on COM1 after "RDGVRAM". This draws them as a DMG would -
background, window and sprites, a whole frame at the registers' final values
- into a 160x144 PGM, the format tools/gbprof.py --shots writes, so the core's
picture can be put beside Peanut-GB's before the core has a renderer of its
own. A raster effect shows as one frame of its last values.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rdgqemu  # noqa: E402

SHADE = (255, 170, 85, 0)


def capture(img, timeout):
    fd, log = tempfile.mkstemp(prefix="rdgs-")
    os.close(fd)
    p = subprocess.Popen(["qemu-system-i386", "-drive", "file=%s,format=raw,if=floppy" % img,
                          "-boot", "a", "-display", "none", "-serial", "file:%s" % log,
                          "-no-reboot"] + rdgqemu.tcg_args(),   # (its SAHF probe)
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    need = 0x2000 + 0xA0 + 0x0C + 0x100 + 12 + 5760 + 768 + 8 + 4
    t0, data = time.time(), b""
    try:
        while time.time() - t0 < timeout:
            data = open(log, "rb").read()
            k = data.find(b"RDGVRAM")
            m = k + len(b"RDGVRAM")
            if k >= 0 and len(data) >= m + need:
                return data[:k], data[m:m + need]
            if b"RDGHOST BADOP" in data:     # the core stopped: no picture coming
                time.sleep(0.3)
                break
            time.sleep(0.05)                # (was 0.3: a floor per capture)
    finally:
        p.kill()
        p.wait()
        os.unlink(log)
    sys.exit("rdgshot: no RDGVRAM block (%s)" % ("the core hit a bad opcode"
                                               if b"BADOP" in data else "%ds" % timeout))


def blargg(blk):
    """A Blargg ROM's verdict from A000-A0FF: (status, text), or None when
    the signature DE B0 61 is not at A001."""
    a = blk[0x20AC:0x21AC]
    if a[1:4] != b"\xde\xb0\x61":
        return None
    return a[0], a[4:].split(b"\0")[0].decode("latin-1")


def mooneye(blk):
    """Mooneye's verdict from the registers at the end of the run: True for
    B,C,D,E,H,L = 3,5,8,13,21,34 (its pass), False for all 42h (its fail),
    None for neither - the test never finished."""
    r = blk[0x21AC:0x21B8]
    b, c, d, e, h, l = r[3], r[2], r[5], r[4], r[7], r[6]
    if (b, c, d, e, h, l) == (3, 5, 8, 13, 21, 34):
        return True
    if (b, c, d, e, h, l) == (0x42,) * 6:
        return False
    return None


def core_frame(blk):
    """rdg_render's own frame: 144 lines of two 20-byte shade planes ->
    160x144 shades 0-3."""
    f = blk[0x21B8:0x21B8 + 5760]
    out = bytearray(160 * 144)
    for y in range(144):
        lo, hi = f[y * 40:y * 40 + 20], f[y * 40 + 20:y * 40 + 40]
        for x in range(160):
            b = 7 - (x & 7)
            out[y * 160 + x] = ((lo[x >> 3] >> b) & 1) | (((hi[x >> 3] >> b) & 1) << 1)
    return bytes(out)


def rlog(blk):
    """(frames, snaptag, entries, snapshot, [(line, register, value)...])."""
    o = 0x21B8 + 5760
    log, snap = blk[o:o + 768], blk[o + 768:o + 776]
    frames = blk[o + 776] | blk[o + 777] << 8
    tag, n = blk[o + 778], blk[o + 779]
    return frames, tag, n, snap, [tuple(log[i * 4:i * 4 + 3]) for i in range(min(n, 192))]


def render(blk):
    vram, oam, reg = blk[:0x2000], blk[0x2000:0x20A0], blk[0x20A0:0x20AC]
    lcdc, scy, scx, bgp, obp0, obp1, wy, wx = (reg[0], reg[2], reg[3], reg[7],
                                              reg[8], reg[9], reg[10], reg[11])
    pix = bytearray(160 * 144)
    raw = bytearray(160 * 144)                  # BG colour index, for priority

    def tile(map_base, mx, my, x, y):
        t = vram[map_base - 0x8000 + my * 32 + mx]
        a = t * 16 if lcdc & 0x10 else 0x1000 + ((t ^ 0x80) - 0x80) * 16
        lo, hi = vram[a + y * 2], vram[a + y * 2 + 1]
        return ((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1)

    if lcdc & 0x80 and lcdc & 0x01:
        bgmap = 0x9C00 if lcdc & 0x08 else 0x9800
        wmap = 0x9C00 if lcdc & 0x40 else 0x9800
        for y in range(144):
            for x in range(160):
                if lcdc & 0x20 and y >= wy and x >= wx - 7:
                    px, py = x - (wx - 7), y - wy
                    c = tile(wmap, px >> 3, py >> 3, px & 7, py & 7)
                else:
                    px, py = (x + scx) & 255, (y + scy) & 255
                    c = tile(bgmap, px >> 3, py >> 3, px & 7, py & 7)
                raw[y * 160 + x] = c
                pix[y * 160 + x] = (bgp >> (c * 2)) & 3
    if lcdc & 0x80 and lcdc & 0x02:
        h = 16 if lcdc & 0x04 else 8
        for i in reversed(range(40)):
            sy, sx, t, fl = oam[i * 4] - 16, oam[i * 4 + 1] - 8, oam[i * 4 + 2], oam[i * 4 + 3]
            if h == 16:
                t &= 0xFE
            pal = obp1 if fl & 0x10 else obp0
            for yy in range(h):
                for xx in range(8):
                    x, y = sx + xx, sy + yy
                    if not (0 <= x < 160 and 0 <= y < 144):
                        continue
                    ty = h - 1 - yy if fl & 0x40 else yy
                    tx = 7 - xx if fl & 0x20 else xx
                    a = t * 16 + ty * 2
                    c = ((vram[a] >> (7 - tx)) & 1) | (((vram[a + 1] >> (7 - tx)) & 1) << 1)
                    if c == 0 or (fl & 0x80 and raw[y * 160 + x]):
                        continue
                    pix[y * 160 + x] = (pal >> (c * 2)) & 3
    return bytes(SHADE[p] for p in pix)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("img")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--timeout", type=float, default=300)
    ap.add_argument("--python", action="store_true",
                    help="draw with this script's renderer instead of rdg_render's frame")
    a = ap.parse_args()
    text, blk = capture(a.img, a.timeout)
    sys.stdout.write(text.decode("latin-1"))
    v = blargg(blk)
    if v:
        print("rdgshot: Blargg status %02X: %s" % (v[0], v[1].strip().replace("\n", " / ")))
    with open(a.out, "wb") as f:
        f.write(b"P5 160 144 255\n" + (render(blk) if a.python
                                        else bytes(SHADE[p] for p in core_frame(blk))))
    print("rdgshot: %s" % a.out)


if __name__ == "__main__":
    main()
