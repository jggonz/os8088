#!/usr/bin/env python3
"""PACCMAN's tick path on a cycle-accurate 8088 (SPEC.md 91).

    make paccman
    python3 tests/paccman.py --machine os8088_xt_vga
    python3 tests/paccman.py --machine os8088_5150_cga_gla
    python3 tests/paccman.py --machine os8088_5150_herc_gla

apps/paccman/hosttest/pmcuitest.c already drives the same C against a modelled
glass and audits every pixel of every frame; what it cannot do is answer the
three questions that are about the MACHINE rather than about the program:

  * does the worker start at all, and does the game advance without anybody
    touching it - the whole tick path (accumulator, triggers, movement, the
    four ghosts) running on the real scheduler at the real 18.2 Hz;
  * is the layout the one this ADAPTER needs (SPEC.md 39: alternate rows where
    the display is short, every row where it is not);
  * and how deep does the worker's stack actually go, against the
    OS88_STACK_256 the package asked for. tools/stkdepth.py's static chain is
    142 bytes at wave 2's end (it drifts with every build); a measured water
    mark is the only thing that says the interrupt floor on top of it fits.

STATE IS READ BY SYMBOL from the real compiled game - no host reimplementation
of gameplay. This row WRITES nothing into the guest: the fixtures (a frightened
ghost scored, a fruit taken) and the fps / ms-per-frame / calls-per-frame
bracket beside PACMAN.O88 are wave 4's measurement work and are not here yet.
The symbols carry
SmallerC's leading underscore (`_pmc_score`), and tests/dispapps.py's `_map`
reaches them because it now passes `-I build/`, which is what a C package's
shim needs to %include its own compiled C.
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
import stkwater
import dispcp
import dispapps

# The static chain tools/stkdepth.py composes for cc_worker is 142 bytes
# (SPEC.md 91). The interrupt floor on top of it is 32-38 measured here and 64
# on the worst real machine (docs/STACK-SLOTS-PLAN.md 7.1), and QEMU/MartyPC
# understate a real BIOS by ~46 (tools/stkwater.py's own note). 208 of 256 is
# that sum with the ROM's int 08h chain allowed for and ~48 bytes still spare;
# a build that passes it has to move to OS88_STACK_384 as a stated decision.
WATER_MAX = 208


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--machine', default='os8088_xt_vga')
    ap.add_argument('--capture', help='PNG of ordinary play')
    args = ap.parse_args()

    image = os88marty.scratch_disk('build/paccman-test.img',
                                   'build/paccman.o88')
    symbols = dispapps._map('paccman')
    S = os88sym.linear
    with os88marty.launch('build/os8088-360.img', apps=image,
                          machine=args.machine, label='paccman-test') as m:
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, 'B')
        w = dispcp.win_list(m, S)
        wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])

        def moving(m, **kw):
            m.advance(frames=120)
            m.run()     # the game never settles: the worker draws for ever

        dispcp.open_named(m, mo, S, moving, wx, wy, 'PACCMAN.O88')
        m.advance(frames=240)
        package = dispapps.pkg_seg(m, 0)
        assert package, 'PaccMan did not launch'
        base = package[1] * 16

        def read(name, size=1, offset=0):
            return int.from_bytes(
                m.read(base + symbols[name] + offset, size), 'little')

        def raw(name, size, offset=0):
            return m.read(base + symbols[name] + offset, size)

        def boundary():
            m.bp_exec(base + symbols['_pmc_frame'])
            m.run()
            assert m.wait_stop(20) == 'breakpoint', 'the worker stopped'

        boundary()
        m.bp_exec()

        # --- the worker, and the game running on its own ---------------------
        assert read('_pmc_hired', 2) == 1, 'the first paint did not hire a worker'
        t0 = read('_pmc_tick_lo', 2)

        # THE GHOSTS ARE THE LIVENESS SIGNAL AND PAC-MAN IS NOT.
        # pmc_ax is int[5] - Pac-Man, then the four ghosts - and nothing here
        # touches the keyboard, so input_dir's default is Pac-Man's CURRENT
        # direction and can_move refuses it the moment he reaches a wall. That
        # is the reference's own behaviour (pacman.c 926-946), so a Pac-Man
        # who has not moved between two samples proves nothing: on the 5150
        # CGA profile he has already run left off his start tile, eaten seven
        # dots and parked at x = 52 before the first sample is taken. The
        # ghosts never park, so THEY are what says the tick path is running,
        # and the dot count below is what says Pac-Man moved at all.
        def ghosts():
            return raw('_pmc_ax', 8, 2)         # ax[1..4], the four ghosts

        gh = ghosts()
        m.advance(frames=240)
        boundary()
        m.bp_exec()
        assert read('_pmc_tick_lo', 2) != t0, 'the game tick never advanced'
        assert read('_pmc_mode', 2) == 1, 'the game state is not GAME'
        assert ghosts() != gh or read('_pmc_freeze', 2) != 0, \
            'no ghost moved and the game is not frozen either'
        m.advance(frames=600)
        boundary()
        m.bp_exec()
        assert read('_pmc_dots_eaten', 2) > 0, 'no dot was ever eaten'
        assert read('_pmc_lives', 2) == 2, \
            'the round did not take a life off the reserve strip'
        print('  launch, worker, autonomous movement and dots: pass',
              flush=True)

        # --- the layout this adapter needs (SPEC.md 39) ----------------------
        # A 200-line screen cannot hold 288 rows of field, so CGA and only CGA
        # samples alternate source rows into 4-row bands.
        step = read('_pmc_step', 2)
        want = 2 if 'cga' in args.machine else 1
        assert step == want, ('the wrong row step for this adapter', step, want)
        assert read('_pmc_rows', 2) == (4 if want == 2 else 8)
        assert read('_pmc_fx', 2) % 8 == 0, \
            "the field's x is not a multiple of 8 - BLITP and BLIT1 refuse"
        print('  adapter layout: step %d, %d-row bands: pass'
              % (step, read('_pmc_rows', 2)), flush=True)

        if args.capture:
            wid, hgt, pixels = m.fbuf()
            os88marty.write_png_rgb(args.capture, wid, hgt, pixels)

        # --- the image, which nothing but the writable statics may change ----
        # A C package's .data holds its initialised statics, and a handful are
        # written by design:
        #
        #   _pmc_items      pmc_items[2] follows the sound state;
        #   _pmc_mset       os88_menu_set patches the set's oncmd field (a set
        #                   in .rodata would take that patch silently and
        #                   wrongly);
        #   cc_tpl          the SDK's 16-byte wm_create template
        #                   (apps/cc/crt0.asm) has its first FIVE words -
        #                   WT_X, WT_Y, WT_W, WT_H, WT_TITLE - written by
        #                   os88_wm_create() at launch. Only ten bytes are
        #                   allowed: the last three words are the callback
        #                   pointers crt0 built, and a package whose paint or
        #                   key vector had moved would be a real finding;
        #   _pmc_step /     pmc_layout writes the adapter's layout into all
        #   _pmc_rows /     three, and they carry the VGA answer (1, 8, 3) as
        #   _pmc_rsh        their initialiser. THE VGA ARM CANNOT SEE THIS,
        #                   because on VGA the value written IS the
        #                   initialiser; the CGA arm writes 2, 4, 2 over it
        #                   and is what makes this row mean anything. (pmc_ssh
        #                   has no initialiser, so it is bss and not in the
        #                   image at all.)
        #
        # Everything else - 37KB of code and arcade ROM tables - must be byte
        # for byte the file.
        disk = Path('build/paccman.o88').read_bytes()
        live = m.read(base, len(disk))
        allow = []
        for name, span in (('_pmc_items', 8), ('_pmc_mset', 64),
                           ('cc_tpl', 10),
                           ('_pmc_step', 2), ('_pmc_rows', 2),
                           ('_pmc_rsh', 2)):
            if name in symbols:
                allow.append((symbols[name], symbols[name] + span))
        bad = [(i, a, b) for i, (a, b) in enumerate(zip(live, disk))
               if a != b and not any(lo <= i < hi for lo, hi in allow)]
        assert not bad, ('the image was modified', bad[:32])
        print('  the image is unmodified outside its writable statics: pass',
              flush=True)

        # --- the stack, measured -------------------------------------------
        # OS88_STACK_256 is ONE number and every document quotes it; this is
        # the only thing that says it is the right one. The worker has been
        # drawing for thousands of frames by now, so its slice carries the
        # high water of the whole tick path plus whatever interrupts landed
        # on it.
        defs = ()
        sizes = stkwater.slice_sizes(defs)
        nslot = stkwater.slots(defs)
        mem = m.read(m.sym('sch_stacks', defs), sum(sizes[:nslot]))
        worst = 0
        for slot, used, free in stkwater.water(mem, nslot, sizes):
            if used is None:
                continue
            if sizes[slot - 1] == 256 and used > worst:
                worst = used
        print('  worker slice water mark: %d of 256 (limit %d)'
              % (worst, WATER_MAX), flush=True)
        assert worst > 0, 'no 256-byte slice was ever used - is the worker up?'
        assert worst < WATER_MAX, \
            ('the worker outgrew OS88_STACK_256 - see SPEC.md 91', worst)
        print('  stack: pass', flush=True)

    print('paccman: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
