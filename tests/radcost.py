#!/usr/bin/env python3
"""What a RAD replay frame costs on the 4.77 MHz 8088 (SPEC.md 34.13.6).

    make && make radgate && python3 tests/radcost.py [--frames 600]

An INSTRUMENT, not a gate: it asserts nothing and prints the figures
PERFORMANCE.md records. MartyPC's os8088_5150_sb_gla (patch 05's OPL3) with the
SHIPPED driver - no -DRADLOG, whose per-write logging would be priced in -
and RADGATE on B:. Exec breakpoints on the overlay's `rp_frame` entry and its
`.exit`, and on `rp_tick` and `rp_tick.ret`, bracket every frame and every
pacer tick; MartyPC's cycle counter is the clock, so a figure is exact for
this emulated machine and independent of the host.

  RV2.RAD  --frames frames of the 2.1 fixture: the worst frame that played a
           note (a line or a riff line - rd_notes > 0) and the worst frame
           that played none (continuous effects only)
  RV1.RAD  the same for the 1.0 fixture (no note count: its worst frame)
  HEAVY.RAD  t_rad's heavy-but-legal tune at BPM 300, 30 note plays a frame:
           the worst whole pacer tick, which SPEC.md 34.13.6 bounds
  FAN.RAD  the HALT (rd_halt .. rd_halt.exit): the stop sequence and the
           default patch through opl_wrf, at IF = 0 inside IRQ0, and the
           whole tick it ends
  KILL     RV2.RAD playing and RADGATE's window closed: rd_kill .. rd_kill.done
           inside snd_release_inst's cli (DSV_RELINST - the same body a
           package entering a SPEC.md 53 bracket mid-tune pays)
  verb 6   rad_stat's own pushf/cli .. ret, per tune: the window a package
           enters EVERY UI FRAME (34.12.7), with the far call, the dispatch,
           the engine's status build and the 72-byte copy inside it
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88build                                            # noqa: E402
import radlib                                               # noqa: E402
from radopl3 import gate_open, press, wait_for              # noqa: E402

HZ = 4772727.0


def ms(c):
    return c * 1000.0 / HZ


def bracket(m, box, enter, leave, count, notes=False):
    """[(cycles, rd_notes)] for `count` enter..leave pairs."""
    seg = box.claim()
    a, b = (seg << 4) + box.O[enter], (seg << 4) + box.O[leave]
    m.breakpoints([{"type": "exec", "addr": a}, {"type": "exec", "addr": b}])
    out = []
    c0 = None
    while len(out) < count:
        m.run()
        if m.wait_stop(20) is None:
            break
        st = m.status()
        flat = (st["cs"] << 4) + st["ip"]
        cyc = int(st["cycles"])
        if flat == a:
            c0 = cyc
        elif flat == b and c0 is not None:
            n = m.read((seg << 4) + box.O["rd_notes"], 1)[0] if notes else 0
            out.append((cyc - c0, n))
            c0 = None
    m.breakpoints([])
    m.run()
    return out


def dbracket(m, box, enter, leave, count):
    """[cycles] for `count` enter..leave pairs on RESIDENT symbols."""
    seg = box.dseg()
    a, b = (seg << 4) + box.D[enter], (seg << 4) + box.D[leave]
    m.breakpoints([{"type": "exec", "addr": a}, {"type": "exec", "addr": b}])
    out, c0 = [], None
    t0 = time.time()
    while len(out) < count and time.time() - t0 < 120:
        m.run()
        if m.wait_stop(20) is None:
            break
        st = m.status()
        flat = (st["cs"] << 4) + st["ip"]
        if flat == a:
            c0 = int(st["cycles"])
        elif flat == b and c0 is not None:
            out.append(int(st["cycles"]) - c0)
            c0 = None
    m.breakpoints([])
    m.run()
    return out


def stops(m, addrs, want, limit=60):
    """[(flat, cycles)] for the next `want` stops on exec breakpoints."""
    m.breakpoints([{"type": "exec", "addr": x} for x in addrs])
    out = []
    t0 = time.time()
    while len(out) < want and time.time() - t0 < limit:
        m.run()
        if m.wait_stop(20) is None:
            break
        st = m.status()
        out.append(((st["cs"] << 4) + st["ip"], int(st["cycles"])))
    m.breakpoints([])
    m.run()
    return out


def teardown(m, box, mo, win):
    """The HALT's and the KILL's IF = 0 cost, SPEC.md 34.13.6."""
    import dispcp
    import os88geom
    # FAN.RAD: load + start, the first frame HALTs. The breakpoints go in
    # before the key, on the claim the load WILL use - load first ('l' is
    # RV1 only), so read the claim after the key's load and catch the next
    # tick's HALT, which the start's first pacer tick takes
    press(m, "Digit5", 0.0)
    seg = 0
    t0 = time.time()
    while not seg and time.time() - t0 < 30:
        seg = box.claim()
    base = (seg << 4)
    hs = stops(m, [base + box.O["rp_tick"], base + box.O["rd_halt"],
                   base + box.O["rd_halt.exit"], base + box.O["rp_tick.ret"]],
               4)
    names = {base + box.O[k]: k for k in ("rp_tick", "rd_halt",
                                          "rd_halt.exit", "rp_tick.ret")}
    seq = [(names.get(f, "%05x" % f), c) for f, c in hs]
    d = dict((n, c) for n, c in seq)
    if [n for n, _ in seq] == ["rp_tick", "rd_halt", "rd_halt.exit",
                               "rp_tick.ret"]:
        print("FAN.RAD HALT: rd_halt %d cycles = %.2f ms; the whole tick that "
              "halted %d cycles = %.2f ms" % (
                  d["rd_halt.exit"] - d["rd_halt"],
                  ms(d["rd_halt.exit"] - d["rd_halt"]),
                  d["rp_tick.ret"] - d["rp_tick"],
                  ms(d["rp_tick.ret"] - d["rp_tick"])))
    else:
        print("FAN.RAD HALT: the bracket missed - stops %r" % seq)
    press(m, "KeyX", 1.0)
    wait_for(lambda: box.claim() == 0, 20)
    # KILL: RV2.RAD playing, the window closed
    press(m, "Digit2", 1.0)
    wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
    seg = box.claim()
    base = seg << 4
    a, b = base + box.O["rd_kill"], base + box.O["rd_kill.done"]
    x, y = dispcp.win_rect(m, radlib.S, win)[:2]
    cx, cy = os88geom.close_xy(x, y)
    mo.to(cx, cy)
    mo._edge(True)                  # the press, proven, with no breakpoint in
    m.breakpoints([{"type": "exec", "addr": a}, {"type": "exec", "addr": b}])
    got = []
    t0 = time.time()
    while len(got) < 2 and time.time() - t0 < 60:
        if not got:                 # the release is what closes: a packet the
            m.mouse(0, 0, l=False)  # UART dropped is re-sent (a level, so a
        m.run()                     # duplicate is harmless)
        if m.wait_stop(5 if not got else 20) is None:
            continue
        st = m.status()
        got.append(((st["cs"] << 4) + st["ip"], int(st["cycles"])))
    m.breakpoints([])
    m.run()
    if [f for f, _ in got] == [a, b]:
        c = got[1][1] - got[0][1]
        print("RV2.RAD KILL (DSV_RELINST, the window closed mid-tune): rd_kill "
              "%d cycles = %.2f ms at IF = 0" % (c, ms(c)))
    else:
        print("KILL: the bracket missed - stops %r (want %05x, %05x)"
              % (got, a, b))


def main():
    import os88marty as M
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--ticks", type=int, default=64)
    ap.add_argument("--statpolls", type=int, default=30)
    ap.add_argument("--apps", default="build/radgate360.img",
                    help="the B: disk; a scratch copy of RADGATE's disk with "
                         "another tune NAMED RV2.RAD prices that tune (keep "
                         "it out of the tree - SPEC.md 96.7)")
    ap.add_argument("--teardown-only", action="store_true",
                    help="only the HALT and KILL brackets")
    a = ap.parse_args()
    for k in ("MARTYPC_OPL2", "MARTYPC_NO38A"):
        os.environ.pop(k, None)
    with M.launch(os88build.at("build/os8088-360.img"),
                  apps=os88build.at(a.apps),
                  machine="os8088_5150_sb_gla", boot=False) as m:
        m.run()
        M.settle(m, gate=M.desktop_up)
        M.no_saver(m)
        m.run()
        box = radlib.Box(m)
        mo, win = gate_open(m, box, M)
        for keyname, fname, notes in (() if a.teardown_only else
                                      (("Digit2", "RV2.RAD", True),
                                       ("Digit1", "RV1.RAD", False))):
            press(m, keyname, 1.0)
            wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
            got = bracket(m, box, "rp_frame", "rp_frame.exit", a.frames, notes)
            if notes:
                line = [c for c, n in got if n]
                eff = [c for c, n in got if not n]
                print("%s: %d frames; worst note frame %d cycles = %.2f ms "
                      "(of %d), worst effect-only frame %d cycles = %.2f ms "
                      "(of %d); mean %.2f ms" % (
                          fname, len(got), max(line), ms(max(line)), len(line),
                          max(eff), ms(max(eff)), len(eff),
                          ms(sum(c for c, _ in got) / len(got))))
            else:
                print("%s: %d frames; worst frame %d cycles = %.2f ms; mean "
                      "%.2f ms" % (fname, len(got), max(c for c, _ in got),
                                   ms(max(c for c, _ in got)),
                                   ms(sum(c for c, _ in got) / len(got))))
            ticks = bracket(m, box, "rp_tick", "rp_tick.ret", 3 * a.ticks)
            print("%s: %d pacer ticks; worst tick %.2f ms, mean %.2f ms" % (
                fname, len(ticks), ms(max(c for c, _ in ticks)),
                ms(sum(c for c, _ in ticks) / len(ticks))))
            # VERB 6's own IF = 0 window (SPEC.md 34.12.7): rad_stat's pushf
            # /cli to its return, so the far call, the dispatch, the engine's
            # status build and the copy are all inside the bracket. This is
            # the window a package enters EVERY UI FRAME, and the only one in
            # the design that had no number in 34.13.6. RADGATE polls verb 6
            # every 2 ticks, so ~30 samples is ~60 ticks of the guest.
            st6 = dbracket(m, box, "rad_stat.inseg", "rad_stat.out",
                           a.statpolls)
            if st6:
                print("%s: %d verb 6 windows (rad_stat cli..ret); worst %d "
                      "cycles = %.3f ms, mean %.3f ms" % (
                          fname, len(st6), max(st6), ms(max(st6)),
                          ms(sum(st6) / len(st6))))
            else:
                print("%s: the verb 6 bracket caught nothing" % fname)
            press(m, "KeyX", 1.0)
            wait_for(lambda: box.claim() == 0, 20)
        if not a.teardown_only:
            press(m, "Digit7", 1.0)
            wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
            ticks = bracket(m, box, "rp_tick", "rp_tick.ret", a.ticks)
            worst = max(c for c, _ in ticks)
            print("HEAVY.RAD: %d pacer ticks; worst tick %d cycles = %.2f ms, "
                  "mean %.2f ms" % (len(ticks), worst, ms(worst),
                                    ms(sum(c for c, _ in ticks) / len(ticks))))
            press(m, "KeyX", 1.0)
            wait_for(lambda: box.claim() == 0, 20)
        teardown(m, box, mo, win)
        m.pause()
    return 0


if __name__ == "__main__":
    sys.exit(main())
