#!/usr/bin/env python3
"""Native Pac-Man gameplay and repaint regression on a cycle-accurate 8088.

    make
    python3 tests/pacman.py --machine os8088_xt_vga
    python3 tests/pacman.py --machine os8088_5150_cga_gla
    python3 tests/pacman.py --machine os8088_5150_herc_gla

Fixtures are placed at the worker's frame boundary under the graphics lock,
then the real assembled game advances. No host reimplementation of gameplay.
"""
import argparse
import os
from pathlib import Path
import sys

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools'),
               str(Path(__file__).resolve().parent)]
import os88marty
import os88mouse
import os88sym
import os88geom
import dispcp
import dispapps


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--machine', default='os8088_5150_cga_gla')
    ap.add_argument('--capture', help='PNG of ordinary play before the fixtures')
    args = ap.parse_args()
    image = os88marty.scratch_disk('build/pacman-test.img', 'build/pacman.o88')
    symbols = dispapps._map('pacman')
    S = os88sym.linear
    with os88marty.launch('build/os8088-360.img', apps=image,
                         machine=args.machine, label='pacman-test') as m:
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, 'B')
        w = dispcp.win_list(m, S)
        wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])

        def moving(m, **kw):
            m.advance(frames=120)
            m.run()  # Mouse requires a running guest; the game never settles.

        dispcp.open_named(m, mo, S, moving, wx, wy, 'PACMAN.O88')
        m.advance(frames=240)
        package = dispapps.pkg_seg(m, 0)
        assert package, 'Pac-Man did not launch'
        base = package[1] * 16

        def read(name, size=1):
            return int.from_bytes(m.read(base + symbols[name], size), 'little')

        def put(name, value, size=1, offset=0):
            m.write(base + symbols[name] + offset, value.to_bytes(size, 'little'))

        def raw(name, data):
            m.write(base + symbols[name], bytes(data))

        def boundary():
            m.bp_exec(base + symbols['pm_frame'])
            m.run()
            assert m.wait_stop(20) == 'breakpoint', 'worker stopped advancing'

        def key(name):
            # Stop at delivery and then at the next completed UI lock hold.
            # A fixed number of VGA frames is not a keyboard-delivery fence
            # on the 5150 BIOSes (whose typematic/keyboard polling differs).
            m.bp_exec(base + symbols['pm_key_body'])
            m.key(name)
            m.run()
            assert m.wait_stop(20) == 'breakpoint', 'key was not delivered: ' + name
            boundary()
            m.bp_exec()

        boundary()
        assert read('pm_hired') == 1
        assert read('pm_score', 4) >= 10 and read('pm_eaten', 2) > 0
        assert read('pm_half') == (1 if 'cga' in args.machine else 0)
        disk = Path('build/pacman.o88').read_bytes()
        live = m.read(base, len(disk))
        changes = [(i,a,b) for i,(a,b) in enumerate(zip(live,disk)) if a != b and not any(symbols[n] <= i < symbols[n]+20 for n in ('pm_score_text','pm_lives_text','pm_level_text','pm_tpl'))]
        assert not changes, ('image modified', changes[:32])
        print('  launch, autonomous movement, dots and adapter layout: pass', flush=True)
        if args.capture:
            w, h, pixels = m.fbuf()
            os88marty.write_png_rgb(args.capture, w, h, pixels)

        # Keyboard pause must freeze both positions and game timers.
        m.bp_exec()
        key('KeyP')
        assert read('pm_pause') == 1
        key('ArrowDown')
        m.advance(frames=12)
        assert read('pm_want') == 2
        key('KeyA')
        m.advance(frames=12)
        assert read('pm_want') == 4
        frozen = m.read(base + symbols['pm_x'], 10)
        frame = read('pm_frames', 2)
        m.advance(frames=120)
        assert m.read(base + symbols['pm_x'], 10) == frozen
        assert read('pm_frames', 2) == frame
        key('KeyP')
        m.advance(frames=12)
        boundary()
        assert read('pm_pause') == 0

        # Score overflow and one-time bonus life, using a real dot eat.
        def fixture(x=61, y=44, direction=4):
            put('pm_mode', 0)
            put('pm_pause', 0)
            put('pm_full', 1)
            put('pm_fright', 0, 2)
            put('pm_phase_ticks', 500, 2)
            put('pm_fruit', 0, 2)
            raw('pm_x', [124, 124, 116, 132, x])
            raw('pm_y', [100, 116, 116, 116, y])
            raw('pm_release', [255] * 4)
            raw('pm_eyes', [0] * 5)
            raw('pm_dir', [4, 4, 4, 4, direction])
            put('pm_want', direction)

        fixture()
        raw('pm_x', [124, 124, 116, 132, 61])
        raw('pm_y', [102, 116, 116, 116, 44])
        raw('pm_release', [0, 255, 255, 255])
        boundary()
        assert read('pm_y') == 100
        boundary()
        assert read('pm_y') == 100 and read('pm_x') == 123, 'ghost missed the house exit turn'

        fixture(x=63)
        put('pm_map', 1, offset=44)  # row 1 col 4; h=62/v=44
        put('pm_score', 65530, 4)
        put('pm_bonus', 0)
        put('pm_lives', 3)
        boundary()
        assert read('pm_score', 4) == 65540
        assert read('pm_lives') == 4 and read('pm_bonus') == 1

        # Buffered turn: north is illegal in the middle of the top corridor;
        # keep moving left until h=58, where the pending south turn is legal.
        fixture(x=61)
        put('pm_want', 2)
        for _ in range(3):
            boundary()
        assert read('pm_x', 5) >> 32 == 58
        boundary()
        assert read('pm_y', 5) >> 32 == 46
        fixture(x=58, direction=1)
        boundary()
        assert read('pm_y', 5) >> 32 == 44, 'walked through the top wall'
        fixture(x=48, y=116)
        boundary()
        assert read('pm_x', 5) >> 32 == 200, 'left tunnel did not wrap'
        print('  pause, buffered turns, walls, tunnel and 32-bit score: pass', flush=True)

        fixture(x=58, y=62, direction=1)
        put('pm_map', 2, offset=3 * 40 + 3)
        put('pm_score', 0, 4)
        put('pm_level', 0, 2)
        boundary()
        assert read('pm_fright', 2) > 0 and read('pm_score', 4) == 50
        # Four stationary ghosts at the next player position, still scared.
        raw('pm_x', [58] * 5)
        raw('pm_y', [58] * 4 + [60])
        raw('pm_release', [0] * 4)
        raw('pm_dir', [4] * 4 + [1])
        put('pm_want', 1)
        boundary()
        assert read('pm_score', 4) == 3050  # 50 + 200 + 400 + 800 + 1600
        assert m.read(base + symbols['pm_eyes'], 4) == bytes([1] * 4)

        fixture(x=63)
        put('pm_map', 1, offset=44)
        put('pm_eaten', 79, 2)
        boundary()
        assert read('pm_fruit', 2) == 182
        # An active fruit is collected below the house.
        raw('pm_x', [124, 124, 116, 132, 123])
        raw('pm_y', [100, 116, 116, 116, 132])
        raw('pm_dir', [4] * 4 + [8])
        put('pm_want', 8)
        score = read('pm_score', 4)
        boundary()
        assert read('pm_fruit', 2) == 0
        assert read('pm_score', 4) == score + 100

        fixture(x=63)
        put('pm_map', 1, offset=44)
        put('pm_eaten', 259, 2)
        boundary()
        assert read('pm_mode') == 3
        put('pm_hold', 1, 2)
        boundary()
        assert read('pm_level', 2) == 1 and read('pm_eaten', 2) == 0
        maze = m.read(base + symbols['pm_map'], 880)
        original = m.read(base + symbols['pm_maze_source'], 880)
        assert original.count(1) == 256, ('source maze changed', original.count(1))
        assert maze == original, [(i, a, b) for i, (a, b) in enumerate(zip(maze, original)) if a != b][:24]
        print('  pellet, four-ghost chain, fruit and next maze: pass', flush=True)

        fixture(x=61)
        raw('pm_x', [60, 124, 116, 132, 61])
        raw('pm_y', [44, 116, 116, 116, 44])
        raw('pm_release', [0, 255, 255, 255])
        put('pm_lives', 1)
        boundary()
        assert read('pm_mode') == 4 and read('pm_lives') == 0
        m.bp_exec()
        key('KeyN')
        m.advance(frames=30)
        boundary()
        assert read('pm_lives') == 3 and read('pm_score', 4) == 0

        # A forced complete reconstruction must equal the incremental canvas.
        # This sees sprite residue and erased pellets even when the glass looks
        # plausible. Freeze gameplay; full-screen input invokes W_PAINT.
        put('pm_pause', 1)
        canvas = m.read(base + symbols['pm_canvas'], 28160)
        m.bp_exec()
        key('KeyF')
        m.advance(frames=120)
        boundary()
        assert read('pm_fs') == 1
        assert m.read(base + symbols['pm_canvas'], 28160) == canvas
        m.bp_exec()
        key('Escape')
        m.advance(frames=120)
        boundary()
        assert read('pm_fs') == 0, (read('pm_fs'), m.regs())
        assert m.read(base + symbols['pm_canvas'], 28160) == canvas
        print('  game over, restart, full-screen round trip and repaint: pass', flush=True)

        # Close through the real window control and watch instance teardown.
        m.bp_exec()
        m.run()
        wins = os88geom.windows(m)
        game = next(w for w in wins if w.title == 'Pac-Man')
        mo.click(game.x + 12, game.y + 9)
        m.advance(frames=120)
        assert not any(w.title == 'Pac-Man' for w in os88geom.windows(m))
        assert dispapps.pkg_seg(m, 0) is None
        print('  close releases the instance and its worker: pass', flush=True)
    print('pacman: PASS on ' + args.machine)


if __name__ == '__main__':
    main()
