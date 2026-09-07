#!/usr/bin/env python3
"""PACCMAN's tick path on a cycle-accurate 8088 (SPEC.md 91).

    make paccman
    python3 tests/paccman.py --machine os8088_xt_vga
    python3 tests/paccman.py --machine os8088_5150_cga_gla
    python3 tests/paccman.py --machine os8088_5150_herc_gla

apps/paccman/hosttest/pmcuitest.c already drives the same C against a modelled
glass and audits every pixel of every frame; what it cannot do is answer the
five questions that are about the MACHINE rather than about the program:

  * does the program come up on its ATTRACT SCREEN, does a real key press
    reach it through the kernel's own keyboard path and start a round, and
    does the game then advance without anybody touching it - the whole tick
    path (accumulator, triggers, movement, the four ghosts) running on the
    real scheduler at the real 18.2 Hz;
  * is the layout the one this ADAPTER needs (SPEC.md 39: alternate rows where
    the display is short, every row where it is not);
  * and how deep does the worker's stack actually go, against the
    OS88_STACK_256 the package asked for. tools/stkdepth.py's static chain is
    142 bytes at wave 2's end (it drifts with every build); a measured water
    mark is the only thing that says the interrupt floor on top of it fits.

  * do the two SCORING paths score, in the real compiled game on the real
    scheduler - a frightened ghost eaten and the bonus fruit taken, each
    placed by writing the actor arrays BY SYMBOL at the worker's own frame
    boundary (everything written is bss, so the image check still covers
    every byte of code and every arcade table);
  * and what it all COSTS: one game_tick(), then fps / ms per frame / gfx
    calls per frame / effective game speed for PACCMAN.O88 and, through the
    same `bracket()` on the same profile, for PACMAN.O88 - with the verdict
    on the user's "maybe this port is more performant on XTs" printed either
    way (SPEC.md 91).

STATE IS READ BY SYMBOL from the real compiled game - no host reimplementation
of gameplay. The symbols carry
SmallerC's leading underscore (`_pmc_score`), and tests/dispapps.py's `_map`
reaches them because it now passes `-I build/`, which is what a C package's
shim needs to %include its own compiled C.
"""
import argparse
import os
from pathlib import Path
import re
import sys

ROOT = str(Path(__file__).resolve().parents[1])
sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools'),
                str(Path(__file__).resolve().parent)]
import os88marty
import os88mouse
import os88sym
import stkwater
import dispcp
import dispapps

# The static chain tools/stkdepth.py composes for cc_worker is 160 bytes
# (SPEC.md 91). The interrupt floor on top of it is 32-38 measured here and 64
# on the worst real machine (docs/STACK-SLOTS-PLAN.md 7.1), and QEMU/MartyPC
# understate a real BIOS by ~46 (tools/stkwater.py's own note). 208 of 256 is
# that sum with the ROM's int 08h chain allowed for; a build that passes it has
# to move to OS88_STACK_384 as a stated decision.
#
# WHAT THE MARK ACTUALLY READS, and it is worth watching: 188 / 190 / 188 on
# the three profiles, so this bar leaves 18 bytes. Wave 4 read 178 and the
# sprite row merge spent ten of the thirty it had, pmc_band_sprites being on
# the worker's deepest chain and SmallerC giving every declared local its own
# slot. The next feature down there is the one that takes the decision.
WATER_MAX = 208

# The 5150's 14.31818 MHz / 3, which is what MartyPC counts cycles in
# (tools/os88boot.py, tools/os88lockw.py). Every millisecond printed below is
# `cycles / HZ` and is therefore a figure about a 4.77 MHz 8088 rather than
# about this host.
HZ = 4772727.0
OS_TICK_HZ = 18.2065                    # the PIT's, SPEC.md 8
PMC_TICK_HZ = 60.0                      # pacman.c's fixed step

# The four DRAWING slots, counted for both ports through the KERNEL's own API
# table rather than through either package's counters - which is the only way
# the two numbers below are the same measurement. A far call to
# `KERNEL_SEG:slot` executes the table entry at that address whoever made it,
# so an exec breakpoint there counts calls from any task.
KERNEL_SEG = 0x0060


def _gfx_slots():
    """The four slot offsets, READ OUT OF apps/os88api.inc rather than copied.

    A hardcoded table here is a second copy of four constants the SDK already
    owns, and nothing checks the mirror: tests/unit/t_apitable.py compares
    kernel.bin against the SDK and never against this file. Renumber a slot and
    these breakpoints land on some other table entry or on none, and the row
    goes on printing a plausible calls-per-frame - most likely a SMALLER one,
    which reads as "the port got faster" rather than as a broken instrument.
    So they are parsed, and a slot that has gone raises here.
    """
    src = open(os.path.join(ROOT, 'apps/os88api.inc')).read()
    out = {}
    for name in ("GFX_FILL", "GFX_BLIT4", "GFX_BLIT1", "GFX_BLITP"):
        m = re.search(r'^%%define\s+OSAPI_%s\s+KERNEL_SEG:(0x[0-9A-Fa-f]+)'
                      % name, src, re.M)
        if not m:
            raise SystemExit("tests/paccman.py: apps/os88api.inc no longer "
                             "defines OSAPI_%s as a KERNEL_SEG slot - the "
                             "call-counting breakpoints cannot be placed"
                             % name)
        out[name] = int(m.group(1), 16)
    return out


GFX_SLOTS = _gfx_slots()


def _pc(m):
    r = m.regs()
    return r["cs"] * 16 + r["ip"]


def bracket(m, frame_addr, ticks, tick_hz, frames=16, calls_over=3):
    """fps, ms per frame, gfx calls per frame and EFFECTIVE GAME SPEED.

    THE SAME FUNCTION MEASURES BOTH PORTS (SPEC.md 91): `frame_addr` is
    PACCMAN's `_pmc_frame` or PACMAN's `pm_step`, `ticks` reads whichever
    counter that port advances its game by and `tick_hz` is the rate it means
    to advance it at - PACCMAN's 60 Hz accumulator, PACMAN's one step per
    18.2 Hz deadline. Everything else - the clock, the slots counted, the
    number of frames - is identical, because a side-by-side whose two halves
    were measured differently answers nothing.

    AND `frame_addr` MUST BE A DRAWN FRAME ON BOTH SIDES, which is the whole
    reason PACMAN is bracketed at `pm_step` rather than at `pm_frame`.
    `_pmc_frame` has ONE exit and every entry of it reaches `pmc_flush`, so
    there a frame proc entered IS a frame drawn; `pm_frame`
    (apps/pacman/pacman.asm) returns without drawing on five guards - not the
    top window, paused, the About card up, game over, and the hold countdown -
    and its worker sleeps to an 18.2 Hz deadline, so counting ITS entries
    counts the scheduler's cadence on every frame it declines to draw. It read
    18.21 "fps" on os8088_5150_cga that way, which is not a rate a 4.77 MHz
    8088 can redraw this game at, against a speed column derived from
    `pm_frames` that said a quarter of that. `pm_step` is called from exactly
    one place - pm_frame's `.move` path, immediately before `pm_redraw` - and
    it is what increments `pm_frames`, so one hit here is one drawn frame on
    both ports and the two columns agree by construction.

    THE SPEED IS READ OVER THE VERY FRAMES THAT WERE TIMED, which is not a
    detail: measured over a separate later window it came out at 71% against
    a frame rate that cannot produce more than 33%, because the window had
    drifted into a freeze - where game time runs on and nothing is drawn, so
    the frames are cheap and there are many of them. One window, both
    numbers.

    Counting the calls needs one debugger stop per call, so that is
    deliberately a SECOND, short pass: folding it into the timed one would
    price the stops.
    """
    m.bp_exec(frame_addr)
    m.run()
    assert m.wait_stop(60) == "breakpoint", "the worker never reached a frame"
    c0, t0 = m.status()["cycles"], ticks()
    for _ in range(frames):
        m.run()
        assert m.wait_stop(60) == "breakpoint", "the worker stopped mid-bracket"
    secs = (m.status()["cycles"] - c0) / HZ
    speed = (((ticks() - t0) & 0xFFFF) / secs) / tick_hz
    fps = frames / secs

    slots = [KERNEL_SEG * 16 + v for v in GFX_SLOTS.values()]
    m.bp_exec(frame_addr, *slots)
    # ...AND ANCHOR ON A FRAME ENTRY BEFORE COUNTING ANYTHING. The timing loop
    # left the guest stopped AT frame_addr; bp_exec REPLACES the whole
    # breakpoint set (tools/os88marty.py), so the first run after it stops on
    # the frame's first GFX CALL - which was then thrown away unclassified, and
    # the counting loop saw (C - 1) + C + C calls over three windows. Both
    # published columns were a third of a call low. Run on to the next frame
    # entry first, and every window counted is a whole one.
    while True:
        m.run()
        assert m.wait_stop(60) == "breakpoint"
        if _pc(m) == frame_addr:
            break
    calls = seen = 0
    while seen < calls_over:
        m.run()
        assert m.wait_stop(60) == "breakpoint"
        if _pc(m) == frame_addr:
            seen += 1
        else:
            calls += 1
    m.bp_exec()
    return fps, 1000.0 / fps, calls / float(calls_over), speed


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

        # --- the attract screen, which is what the program opens on ---------
        # pacman.c's own init() does `start(&state.intro.started)` and nothing
        # else; the port matches it, so a launch that lands in PMC_MODE_GAME
        # would mean the intro was skipped rather than that the game works.
        # The two RAMs are read by symbol: video_ram holds the arcade tile
        # CODES, so '1UP   HIGH SCORE   2UP' at (3,0) is ASCII in the RAM and
        # needs no pixel reading to check. Row stride is PMC_VSTRIDE = 32.
        assert read('_pmc_hired', 2) == 1, 'the first paint did not hire a worker'
        assert read('_pmc_mode', 2) == 0, \
            'the program did not open on the attract screen'
        header = raw('_pmc_vram', 22, 3)
        assert header == b'1UP   HIGH SCORE   2UP'.replace(b' ', b'\x40'), \
            ('the attract screen header is not in video_ram', header)
        # ...and the reveal, POLLED rather than timed: the attract screen puts
        # BLINKY on at game tick 150, and how many drawn frames that is depends
        # on the adapter (the accumulator caps a frame's game time at
        # PMC_CATCHUP_MAX OS ticks, so a slow 1bpp frame carries fewer game
        # ticks than a VGA one). Waiting a fixed number of frames would make
        # this row a speed measurement by accident.
        for _ in range(12):
            if raw('_pmc_vram', 6, (7 << 5) + 17) == b'BLINKY':
                break
            m.advance(frames=120)
            boundary()
            m.bp_exec()
        assert raw('_pmc_vram', 6, (7 << 5) + 17) == b'BLINKY', \
            'the CHARACTER / NICKNAME reveal never ran'
        print('  attract screen: header and the first reveal: pass', flush=True)

        # --- a real key press starts the round -------------------------------
        # THE KEY GOES THROUGH THE KERNEL, which is the whole point of doing
        # this here: the harness pokes the package's own latch byte, and only a
        # machine can say that a scancode arriving at int 09h reaches
        # os88_onkey, is latched, is folded in by the worker's next poll, and
        # is read by the attract screen as its any-key.
        m.key('Space')
        for _ in range(8):
            m.advance(frames=120)
            boundary()
            m.bp_exec()
            if read('_pmc_mode', 2) == 1:
                break
        assert read('_pmc_mode', 2) == 1, \
            'a key press on the attract screen did not start a game'
        assert read('_pmc_input_on', 2) == 1, \
            'the round started with input still disabled'
        print('  a key started the round: pass', flush=True)

        # --- and the speaker was asked for something -------------------------
        # _pmc_snd_last is the Hz the frame last handed to OSAPI_SND_TONE. The
        # prelude runs for its first four seconds of game time, so a sample
        # taken across it must catch a tone; what the tune IS is asserted on
        # the host, by name, in pmcuitest.c.
        #
        # THE FLOOR IS TWO AND THE COUNT IS NOT THE ASSERTION. How many
        # distinct tones six samples catch depends on how many frames the
        # adapter draws across the prelude, which is the speed measurement
        # every row here deliberately avoids making. Two says the speaker is
        # being driven from a TUNE and not stuck on one note, which is the
        # fact that survives a slower or faster machine; a run that sees more
        # prints more and asserts nothing about it.
        tones = set()
        for _ in range(6):
            m.advance(frames=60)
            boundary()
            m.bp_exec()
            tones.add(read('_pmc_snd_last', 2))
        assert len([t for t in tones if t]) >= 2, \
            ('the speaker was not driven from a tune across the prelude',
             sorted(tones))
        print('  the speaker was asked for %d distinct tone(s), floor 2: pass'
              % len([t for t in tones if t]), flush=True)

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

        # --- THE FIXTURES: the two scoring paths, placed by symbol ----------
        # The harness drives these against a modelled glass; what only a
        # machine can say is that the REAL compiled game, on the real
        # scheduler, scores them. Everything written here is bss - the actor
        # arrays, the ghost states, the bonus - so the image check below still
        # covers every byte of code and every arcade table.
        #
        # Written AT THE FRAME BOUNDARY, which is what makes the write safe:
        # the worker is stopped at the top of pmc_frame, so nothing is halfway
        # through a tick and no band is halfway composed.
        def score_of(name="_pmc_score"):
            d = raw(name, 8)
            v = 0
            for i in range(7, -1, -1):
                v = v * 10 + d[i]
            return v * 10

        def put(name, value, size=2, offset=0):
            m.write(base + symbols[name] + offset,
                    int(value).to_bytes(size, "little"))

        def unfrozen(limit=40):
            for _ in range(limit):
                boundary()
                m.bp_exec()
                if read("_pmc_freeze", 2) == 0:
                    return True
                m.advance(frames=60)
            return False

        assert unfrozen(), "the game never came out of its freezes"

        # (1) a FRIGHTENED ghost, eaten. pacman.c scores 200/400/800/1600 for
        # the first, second, third and fourth of a fright; this is the first,
        # so the score must rise by exactly 200 and the ghost must become
        # EYES.
        before = score_of()
        got = None
        for _ in range(12):
            px = read("_pmc_ax", 2)
            py = read("_pmc_ay", 2)
            put("_pmc_ax", px, offset=2)        # ghost 0 onto Pac-Man's tile
            put("_pmc_ay", py, offset=2)
            put("_pmc_gstate", 3, size=1)       # PMC_GS_FRIGHTENED
            m.advance(frames=20)
            boundary()
            m.bp_exec()
            if read("_pmc_nghosts_eaten", 2) > 0:
                got = score_of() - before
                break
        assert got == 200, ("a frightened ghost was not scored at 200", got)
        assert read("_pmc_gstate", 1) == 4, "the eaten ghost did not become EYES"
        print("  fixture: a frightened ghost scored 200: pass", flush=True)

        # (2) the BONUS FRUIT, taken. Round 1's is the cherry at 100 points
        # (pacman.c's levelspec table, pmc_lvl_bonus[0] = 10 in tens).
        assert unfrozen(), "the eat-ghost freeze never lifted"
        before = score_of()
        put("_pmc_fruit", 1)
        got = None
        for _ in range(12):
            put("_pmc_ax", 112)                 # (ax + 4) >> 3 == 14
            put("_pmc_ay", 20 * 8 + 4)          # ay >> 3 == 20
            m.advance(frames=20)
            boundary()
            m.bp_exec()
            if read("_pmc_fruit", 2) == 0:
                got = score_of() - before
                break
        assert got == 100, ("the bonus fruit was not scored at 100", got)
        print("  fixture: the bonus fruit scored 100: pass", flush=True)

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
        #   _pmc_promptph   the attract prompt's LAST DRAWN phase, initialised
        #                   to -1 so that the first pass writes the row
        #                   whatever the blink phase then is. A bss byte could
        #                   not carry it: 0 and 1 are both real phases, so
        #                   "never drawn" needs a third value and a non-zero
        #                   initialiser is what puts it in .data. Both arms see
        #                   this one, because -1 is not a phase.
        #
        # Everything else - 37KB of code and arcade ROM tables - must be byte
        # for byte the file.
        disk = Path('build/paccman.o88').read_bytes()
        live = m.read(base, len(disk))
        allow = []
        for name, span in (('_pmc_items', 8), ('_pmc_mset', 64),
                           ('cc_tpl', 10),
                           ('_pmc_step', 2), ('_pmc_rows', 2),
                           ('_pmc_rsh', 2), ('_pmc_promptph', 2)):
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

        # --- PMC_T_LOGIC: what ONE game_tick() costs ------------------------
        # The band bench cannot measure this: it is a C function and the bench
        # is a standalone assembly package that cannot call one. So it is
        # bracketed here, around pmc_game_tick's entry and its own return
        # address, and the MINIMUM of the samples is the term - the larger
        # ones carry whatever interrupts landed inside the call, and IRQ0 is
        # charged to the machine rather than to the function
        # (apps/paccman/build.sh's PMC_T_LOGIC).
        assert unfrozen(), 'the game never came out of its freezes'
        gt = base + symbols['_pmc_game_tick']
        best = None
        for _ in range(11):
            m.bp_exec(gt)
            m.run()
            if m.wait_stop(60) != 'breakpoint':
                break
            c0 = m.status()['cycles']
            r = m.regs()
            ret = int.from_bytes(m.read(r['ss'] * 16 + r['sp'], 2), 'little')
            m.bp_exec(r['cs'] * 16 + ret)
            m.run()
            if m.wait_stop(60) != 'breakpoint':
                break
            d = m.status()['cycles'] - c0
            if best is None or d < best:
                best = d
        m.bp_exec()
        assert best, 'pmc_game_tick was never reached'
        logic_us = best * 1e6 / HZ
        print('  one game_tick(), five actors: %d cycles = %.0f us'
              % (best, logic_us), flush=True)

        # --- THE MEASUREMENT, and the hypothesis it answers -----------------
        fps_c, ms_c, calls_c, speed_c = bracket(
            m, base + symbols['_pmc_frame'],
            lambda: read('_pmc_tick_lo', 2), PMC_TICK_HZ)

    # ...and the SAME bracket on the assembly port, which is the whole point
    # of printing either (SPEC.md 91). A second machine rather than a second
    # window: PACMAN.O88 is a package on a disk of its own, and the two must
    # not share a floppy, a port or a directory (memory: martyconc).
    peer = os88marty.scratch_disk('build/pacman-peer.img', 'build/pacman.o88')
    psym = dispapps._map('pacman')
    with os88marty.launch('build/os8088-360.img', apps=peer,
                          machine=args.machine, label='pacman-peer') as m:
        os88marty.no_saver(m)
        mo = os88mouse.Mouse(marty=m)
        dispcp.open_drive(m, mo, S, os88marty.settle, 'B')
        w = dispcp.win_list(m, S)
        wx, wy, _, _ = dispcp.win_rect(m, S, w[-1])

        def moving(m, **kw):
            m.advance(frames=120)
            m.run()

        dispcp.open_named(m, mo, S, moving, wx, wy, 'PACMAN.O88')
        m.advance(frames=240)
        pkg = dispapps.pkg_seg(m, 0)
        assert pkg, 'PACMAN did not launch'
        pbase = pkg[1] * 16
        # No key: PACMAN.O88 opens straight into a round (SPEC.md 89) where
        # PACCMAN opens on the attract screen, which is the reference's own
        # posture and not a difference this measurement may charge either way
        # - both brackets below start from a worker that is already drawing.
        m.advance(frames=120)
        # ...AND THE TWO PORTS ARE TIMED IN THE SAME STATE. PaccMan's round
        # was started by a real key press further up and is in play; PACMAN
        # opens on a READY hold, and pm_frame declines to draw all the way
        # through one - so a bracket that happened to start inside a hold
        # would time the hold. That is not a hypothetical: a bracket on
        # pm_frame once read 18.21 "fps" with a pm_frames column saying a
        # quarter of that, which is exactly what a window straddling a hold
        # looks like. PM_PLAY is 0 (apps/pacman/pacman.asm).
        for _ in range(60):
            if not m.read(pbase + psym['pm_mode'], 1)[0]:
                break
            m.advance(frames=60)
        else:
            raise AssertionError('PACMAN never reached PM_PLAY')
        # PACMAN steps its game ONCE per drawn frame against an 18.2 Hz
        # deadline it never bursts past (apps/pacman/pacman.asm's pm_worker),
        # so its effective speed is pm_frames over that deadline - the same
        # quantity as PACCMAN's game ticks over 60. The bracket is on pm_step
        # and not on pm_frame: see bracket()'s docstring, and expect the two
        # columns to agree exactly here, because this port has no catch-up -
        # its game advances once per frame DRAWN and PaccMan's advances up to
        # PMC_CATCHUP_MAX times, which is the difference the table shows.
        fps_a, ms_a, calls_a, speed_a = bracket(
            m, pbase + psym['pm_step'],
            lambda: int.from_bytes(
                m.read(pbase + psym['pm_frames'], 2), 'little'), OS_TICK_HZ)

    print('')
    print('  %-28s %8s %10s %12s %10s' % ('on ' + args.machine, 'fps',
                                          'ms/frame', 'gfx calls', 'speed'))
    print('  %-28s %8.2f %10.1f %12.1f %9.0f%%'
          % ('PACCMAN.O88 (C, SPEC.md 91)', fps_c, ms_c, calls_c,
             speed_c * 100))
    print('  %-28s %8.2f %10.1f %12.1f %9.0f%%'
          % ('PACMAN.O88  (asm, SPEC.md 89)', fps_a, ms_a, calls_a,
             speed_a * 100))
    print('')
    print('  VERDICT: "maybe this port is more performant on XTs" - %s'
          % ('HOLDS on this profile (%.2f >= %.2f fps)' % (fps_c, fps_a)
             if fps_c >= fps_a else
             'DOES NOT hold on this profile (%.2f < %.2f fps)'
             % (fps_c, fps_a)))
    print('')

    print('paccman: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
