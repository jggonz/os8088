#!/usr/bin/env python3
"""A package at the display's depth (SPEC.md 120.7, 106.27).

    make vesatest && python3 tests/vesablitd.py [--png out.png]

Boots build/vesablitd.img - tests/vesa.py's disk plus \\AUTOSTRT\\CITY.LNK, a
link to the document CITY.PCX on B: - with build/vxpics.img in B: (PiXEL and
the picture at its root). The desktop comes up at 1024x768x16 and the
autostart opens the picture by its association, so PiXEL is on the glass
with no UI driven: its canvas is drawn through OSAPI_GFX_BLITD, a row of
RGB565 words a call, from the RGB565 table it builds beside its dither
tables at 16 bits a pixel.

WHAT IT ASSERTS, after PiXEL's instance is up and its decode is done:

  - the screendump holds MORE than the desktop's sixteen colours: at least
    64 distinct colours that are not EGA entries (a 16-colour PiXEL - the
    fallback path - cannot produce one);
  - every such colour is one of the picture's palette entries, quantised
    to 565 the way the table quantises them, within the rounding QEMU's
    565 -> 888 expansion adds (a wrong table, a wrong stride or a byte-swapped
    word all put colours on the glass the palette does not hold);
  - [px_bpp] is 16 and the view claim is the 31 KB layout (the table's room).

QEMU by name on CLAUDE.md's closed list, tests/vesa.py's reason.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88build as _B                                      # noqa: E402
import os88fixture                                          # noqa: E402
import os88qemu                                             # noqa: E402
import os88sym                                              # noqa: E402
import pixelsim as P                                        # noqa: E402
import shot                                                 # noqa: E402
import vesa                                                 # noqa: E402
from os88geom import VID_VESA                               # noqa: E402
from pxsyms import pkg_syms, instance, u16                  # noqa: E402

PICTURE = os.path.join(ROOT, "apps", "pixel", "samples", "CITY.PCX")
PX_VORGB = 0x7800               # apps/pixel/pxview.inc: the table's offset
EGA = None                      # the sixteen as QEMU's 565 -> 888 lands them


def launch():
    os88fixture.need("build/vesablitd.img", "build/vxpics.img")
    em = "qemu" + "-system-i386"
    subprocess.run(
        em + " -machine pc,vmport=off -vga std"
        " -drive file=%s,format=raw,if=floppy -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1"
        " -serial msmouse"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (_B.at("build/vesablitd.img"), _B.at("build/vxpics.img"),
           vesa.SOCK, vesa.PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(vesa.PIDFILE, vesa.SOCK)


def q565(c):
    """A palette entry as the 565 word's channels, back at 8 bits the way a
    565 -> 888 expansion lands them: the high bits replicated."""
    r, g, b = c[0] >> 3, c[1] >> 2, c[2] >> 3
    return ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))


def main():
    out_png = None
    if "--png" in sys.argv:
        out_png = sys.argv[sys.argv.index("--png") + 1]
    vesa.kill_stale()
    fails = []
    launch()
    q = heapmap.Qmp(vesa.SOCK)
    for _ in range(200):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.1)
    S = os88sym.linear
    os88qemu.acted(q, lambda: vesa.byte(q, "vid_kind") == VID_VESA, 120.0,
                   "vid_kind = VID_VESA")
    if vesa.byte(q, "vid_kind") != VID_VESA:
        print("vesablitd: FAIL\n  the desktop did not come up on VID_VESA")
        return 1

    # --- PiXEL, started by the autostart link, and its decode -------------
    syms, image = pkg_syms()
    os88qemu.acted(q, lambda: instance(q, S, image), 120.0, "PiXEL's instance")
    seg = instance(q, S, image)
    if not seg:
        print("vesablitd: FAIL\n  no PiXEL instance: the autostart link did "
              "not open CITY.PCX")
        return 1
    base = seg * 16
    B = lambda n: q.read(base + syms[n], 1)[0]                  # noqa: E731
    W = lambda n: u16(q.read(base + syms[n], 2))                # noqa: E731

    def idle():
        return B("px_busy") == 0 and B("px_job") == 0
    os88qemu.acted(q, lambda: W("px_ndone") and idle(), 300.0, "the decode")
    os88qemu.pace(q, 3.0)
    depth = B("px_bpp")
    if depth != 16:
        fails.append("px_bpp %d, want 16: PiXEL did not read the depth" % depth)

    # --- the glass -------------------------------------------------------
    w, h, pix = vesa.screendump(q, os.path.join(ROOT, "build", "vesablitd-shot"))
    if out_png:
        shot.png(out_png, w, h, pix)
        print("wrote", out_png)
    # the palette PiXEL built the table from - its own [px_pal], 256 RGB
    # triples - and the table itself, out of the view claim: the words must
    # be the palette's 565 quantisation, and the glass must hold nothing but
    # those (what the master IS is tests/pxopen.py's question, not this one)
    pal = [tuple(q.read(base + syms["px_pal"] + 3 * i, 3)) for i in range(256)]
    vseg = W("px_vseg")
    tab = q.read(vseg * 16 + PX_VORGB, 512)
    want = [((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3) for c in pal]
    got = [tab[2 * i] | (tab[2 * i + 1] << 8) for i in range(256)]
    if got != want:
        n = sum(1 for i in range(256) if got[i] != want[i])
        fails.append("the RGB565 table differs from the palette's quantisation "
                     "in %d of 256 entries (e.g. %04X for %r, want %04X)"
                     % (n, got[0], pal[0], want[0]))
    if B("px_vdepth") != 16:
        fails.append("px_vdepth %d, want 16: the view claim is not the 31 KB "
                     "layout the table lives in" % B("px_vdepth"))
    allowed = set(q565(c) for c in pal)
    ega = set(q565(tuple(c)) for c in P.EGA16)
    seen = {}
    for y in range(0, h):
        for x in range(0, w):
            i = (y * w + x) * 3
            c = (pix[i], pix[i + 1], pix[i + 2])
            if c in ega:
                continue
            seen[c] = seen.get(c, 0) + 1
    if len(seen) < 64:
        fails.append("%d distinct non-EGA colours on the glass, want 64 or "
                     "more: the canvas is not at depth" % len(seen))

    def near(c):
        return any(abs(c[0] - a[0]) <= 8 and abs(c[1] - a[1]) <= 4
                   and abs(c[2] - a[2]) <= 8 for a in allowed)
    foreign = [c for c in seen if not near(c)]
    # the arrow and PiXEL's own chrome are EGA colours; anything else on the
    # glass that the palette does not hold is a wrong word
    foreign_px = sum(seen[c] for c in foreign)
    if foreign_px > 200:
        fails.append("%d pixels in %d colours the palette does not hold, e.g. "
                     "%r: the table, the stride or the word order is wrong"
                     % (foreign_px, len(foreign), sorted(foreign)[:4]))
    if fails:
        print("vesablitd: FAIL")
        for f in fails:
            print("  " + f)
        return 1
    print("vesablitd: ok - PiXEL at 16bpp, %d palette colours on the glass "
          "through OSAPI_GFX_BLITD" % len(seen))
    return 0


if __name__ == "__main__":
    sys.exit(main())
