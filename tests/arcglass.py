#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcglass.py - the landscape boards' picture IS their VRAM
# (docs/ARCADE-SPEC.md 4.6)
#
#     python3 tests/arcglass.py ["Gun Fight" "Sea Wolf" ...]
#
# Each game picked from Game on MartyPC's Hercules 5150, a coin, a start and
# a few seconds of play; then, paused, the guest's own video RAM (the view's
# 2400-3FFFh, found by its 'ARCV' signature) drawn as the board draws it -
# a byte 8 pixels across, bit 0 leftmost, 224 lines - against the Hercules
# Hercules page itself, read at the instant ar_render has drawn its list (a
# breakpoint on its end), where ARCADE puts it 1:1 from row 62, each pixel
# doubled across from byte 13. Compared CELL by cell (8 lines of a byte
# column, the write list's unit) in three frames: none may differ. A cell
# the host never redrew - a write it was never told of - stays wrong. Sea
# Wolf
# erases its text with SP in VRAM, and until a PUSH there listed its cells
# (arcaot.py, ARCADE-SPEC 3.3) "INSERT COIN" stayed on the glass in play.
# =============================================================================
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                    # noqa: E402
import os88marty                    # noqa: E402
import os88ui                       # noqa: E402
from arcspeed import find_view      # noqa: E402

ALLOW = 0


def render_end(m):
    """The flat address of ar_render's `.drawn` in the running package: the
    listing's offset, and the image found in guest memory by the bytes of
    ar_render itself."""
    import re
    off = {}
    cur = None
    pend = []
    for ln in open(os88build.at('build/arcade.lst')):
        a = re.match(r'\s*\d+\s+([0-9A-F]{8})\s', ln)
        l = re.match(r'\s*\d+\s+(?:<\d+>\s+)?(\.?[A-Za-z_]\w*):', ln)
        if l:
            name = l.group(1)
            if not name.startswith('.'):
                cur = name
            pend.append(name if not name.startswith('.') else cur + name)
        if a and pend:
            for n in pend:
                off.setdefault(n, int(a.group(1), 16))
            pend = []
    img = open(os88build.at('build/arcade.bin'), 'rb').read()
    sig = img[off['ar_render']:off['ar_render'] + 24]
    mem = bytes(m.read(0, 0xA0000))
    at = mem.find(sig)
    if at < 0:
        raise SystemExit('arcglass: FAIL - the package is not in guest memory')
    return at - off['ar_render'] + off['ar_render.drawn']


def cells_bad(vram, herc):
    """The cells whose 8 lines on the Hercules page (rows 62.., bytes 13..,
    each pixel doubled, MSB left) are not their VRAM bytes (bit 0 left)."""
    dbl = {}
    for b in range(256):
        w = 0
        for k in range(8):
            if (b >> k) & 1:
                w |= 3 << (14 - 2 * k)
        dbl[b] = w
    bad = []
    for band in range(28):
        for col in range(32):
            for r in range(8):
                line = band * 8 + r
                row = line + 62
                o = (row >> 2) * 90 + (row & 3) * 0x2000 + 13 + col * 2
                if (herc[o] << 8 | herc[o + 1]) != dbl[vram[line * 32 + col]]:
                    bad.append((band, col))
                    break
    return bad


def check(game):
    with os88ui.boot('build/os8088-360.img', apps='build/arcade360.img',
                     machine='os8088_5150_herc_gla', quick=True) as ui:
        m = ui.m
        ui.path('B:/ARCADE.O88')
        m.run()
        os88marty.guest_sleep(m, 1)
        ui.menu_pick('Game', game)
        os88marty.guest_sleep(m, 6)         # the board's power-on
        m.key('Digit5')
        os88marty.guest_sleep(m, 1)
        m.key('Digit1')
        os88marty.guest_sleep(m, 3)
        m.key('Space', down=True, up=False)
        m.key('ArrowLeft', down=True, up=False)
        os88marty.guest_sleep(m, 4)
        m.pause()
        v = find_view(m)
        drawn = render_end(m)
        looks = []
        for _ in range(3):                  # three frames, each the instant
            m.bp_exec(drawn)                #   ar_render has drawn its list
            m.run()
            if m.wait_stop(20) != 'breakpoint':
                raise SystemExit('arcglass: FAIL - ar_render never finished a frame')
            m.bp_exec()
            looks.append((bytes(m.read(v + 0x2400, 0x1C00)), bytes(m.read(0xB0000, 0x8000))))
            m.advance(frames=2)
    bad = set()
    for vram, herc in looks:
        bad |= set(cells_bad(vram, herc))
    bad = sorted(bad)
    print('arcglass: %s - %d of 896 cells differ from its VRAM%s'
          % (game, len(bad), (': ' + ' '.join('%d,%d' % c for c in bad[:12])) if bad else ''))
    return len(bad) <= ALLOW


def main():
    games = sys.argv[1:] or ['Gun Fight', 'Sea Wolf']
    fails = [g for g in games if not check(g)]
    for g in fails:
        print('arcglass: FAIL - %s: more than %d cells are not what its VRAM says' % (g, ALLOW))
    if fails:
        return 1
    print('arcglass: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
