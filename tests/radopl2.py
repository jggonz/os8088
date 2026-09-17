#!/usr/bin/env python3
"""The RAD replayer on an 8088 with an OPL2: 2.1 refused in words, 1.0 played
on the tick, and the tick given back (SPEC.md 34.12, 34.13.5, 34.13.7, 96.8).

    make && make radgate && python3 tests/radopl2.py

MartyPC's os8088_5150_sb_gla with MARTYPC_OPL2=1 - status bits 1-2 set and no
second port pair, so SOUND.DRV reads an OPL2 (the probe's negative control) -
and a -DRADLOG driver. RADGATE on B: drives the verbs.

  1. SND_CAP_RAD without SND_CAP_OPL3; tick class.
  2. The hostile-file table, every answer radsim's for an OPL2
     (`radsim.check(row, opl3=False)`): every version 2.1 row that clears
     steps 1-4 is RADE_NEEDOPL3, before a byte of it is validated.
  3. RV2.RAD refused with RADE_NEEDOPL3, which SPEC.md 96.6 words as
     "This tune needs an OPL3 - this card is an OPL2." - the sentence is read
     out of SPEC.md, so a reworded table fails here - and nothing loaded.
  4. RV1.RAD plays TICK-PACED: DSV_TICK names snd_tickp in the kernel's cell
     and the driver's; verb 6's copy says RST_PACER = 0 and 50.0 Hz; frames
     advance 2.75 a tick. Then a START WHILE PLAYING (SPEC.md 34.12.2 step
     0: the state re-zeroed, no stop sequence), 1,000 frames, stop:
     DSV_TICK 0; the log - reset by the restart - equals radsim's sent stream
     for a fresh start on an OPL2 and holds no 1xxh write at all.
  5. RV1.RAD again, and the INSTANCE KILLED (its close box): DSV_RELINST
     silences it at IF = 0, frees the claim, gives channels 0..7 back, and
     answers DX = 0 - DSV_TICK is 0 without another verb.
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import dispcp                                               # noqa: E402
import os88build                                            # noqa: E402
import os88geom                                             # noqa: E402
import radlib                                               # noqa: E402
from radopl3 import gate_open, press, wait_for              # noqa: E402

S = radlib.S
SND_CAP_OPL3, SND_CAP_RAD = 0x20, 0x40
RADE_NEEDOPL3 = 4
TICK_HZ = 18.2065

fails = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(name, cond, note=""):
    say("  %-4s %s%s" % ("ok" if cond else "FAIL", name,
                         ("  " + note) if note else ""))
    if not cond:
        fails.append(name)


def sentence(code_name):
    """SPEC.md 96.6's sentence for a RADE_* name."""
    for line in open(os.path.join(ROOT, "SPEC.md")):
        m = re.match(r"\| `%s` \| `([^`]*)` \|" % code_name, line)
        if m:
            return m.group(1)
    return None


def main():
    import os88marty as M
    os.environ.pop("MARTYPC_NO38A", None)
    os.environ["MARTYPC_OPL2"] = "1"
    t = os88build.tree("RADLOG=1", targets=("os8088-360.img",))
    t.apply()
    img = t.img("os8088-360.img")
    say("MartyPC os8088_5150_sb_gla, MARTYPC_OPL2=1, a -DRADLOG driver")
    try:
        with M.launch(img, apps=os88build.at("build/radgate360.img"),
                      machine="os8088_5150_sb_gla", boot=False) as m:
            m.run()
            M.settle(m, gate=M.desktop_up)
            M.no_saver(m)
            m.run()
            box = radlib.Box(m, ("RADLOG",))
            caps = box.caps()
            check("1: SND_CAP_RAD without SND_CAP_OPL3",
                  caps & (SND_CAP_OPL3 | SND_CAP_RAD) == SND_CAP_RAD,
                  "DSV_CAPS %04x" % caps)
            check("1: tick class", box.dbyte("rad_class") == 0)
            mo, win = gate_open(m, box, M)
            press(m, "KeyV", 1.0)
            done = wait_for(lambda: box.gread("rt_vdone", 1)[0] == 1, 240, 2)
            got = box.table()
            bad = radlib.table_diffs(got, opl3=False)
            check("2: the hostile-file table on an OPL2 - every answer "
                  "radsim's (%d rows)" % len(got), done and not bad,
                  "; ".join("%s: driver %r radsim %r" % b for b in bad[:4]))
            press(m, "Digit2", 1.5)
            n0 = box.res()[4]
            wait_for(lambda: box.res()[4] != n0 or box.res()[0] == 1, 10)
            cf, al, _ah, _cx, _n = box.res()
            words = sentence("RADE_NEEDOPL3")
            check("3: RV2.RAD refused: RADE_NEEDOPL3, which SPEC.md 96.6 "
                  "says as %r" % words,
                  cf == 1 and al == RADE_NEEDOPL3 and
                  words == "This tune needs an OPL3 - this card is an OPL2.",
                  "CF %d AL %d" % (cf, al))
            check("3: ...and nothing is loaded", box.claim() == 0,
                  "claim %04x" % box.claim())
            press(m, "Digit1", 1.0)
            ok = wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
            check("4: RV1.RAD loads and starts", ok, "res %r" % (box.res(),))
            if ok:
                k = box.kcell()
                check("4: DSV_TICK names snd_tickp, kernel's and driver's",
                      k == box.D["snd_tickp"] and box.dcell() == k,
                      "kernel %04x driver %04x" % (k, box.dcell()))
                time.sleep(3)
                st = box.gread("rt_stat", radlib.RST_LEN)
                check("4: verb 6 says tick-paced at 50.0 Hz",
                      st[0] == 2 and st[2] == 0 and radlib.u16(st, 4) == 500,
                      "block %s" % st[:8].hex())
                t0, f0 = box.tick(), box.frames()
                wait_for(lambda: (box.tick() - t0) & 0xFFFF >= 364, 120, 0.5)
                t1, f1 = box.tick(), box.frames()
                per = (f1 - f0) / max(1, (t1 - t0) & 0xFFFF)
                check("4: 50 Hz on the tick is 2.75 frames a tick",
                      abs(per - 500 / 182.0) < 0.05,
                      "%d frames in %d ticks = %.3f" % (f1 - f0, (t1 - t0) & 0xFFFF,
                                                       per))
                n0, f0 = box.res()[4], box.frames()
                press(m, "KeyG", 0.2)           # a restart while playing
                again = wait_for(lambda: box.res()[4] != n0 and
                                 (box.frames() or 0) < f0, 20, 0.1)
                cf, al, _ah, _cx, _n = box.res()
                check("4: START while playing answers CF = 0 and re-zeroes the "
                      "replay (the frame count starts again)",
                      again and cf == 0 and box.obyte("ro_state") == 2,
                      "CF %d AL %d frames %d -> %d" % (cf, al, f0, box.frames()))
                ncmp = radlib.radsim.BRANCH_COMPARED["RV1.RAD"]
                wait_for(lambda: (box.frames() or 0) >= ncmp, 240, 1.0)
                press(m, "KeyS", 1.0)
                wait_for(lambda: box.obyte("ro_state") == 1, 20)
                time.sleep(1.0)
                check("4: DSV_TICK 0 after stop", box.kcell() == 0,
                      "kernel %04x" % box.kcell())
                log = box.rlog() or []
                tune = open(os.path.join(ROOT, "tests", "fixtures", "rad",
                                         "RV1.RAD"), "rb").read()
                frames, diff = radlib.compare_log(log, tune, False)
                check("4: after a RESTART while playing, the register log is "
                      "radsim's fresh-start sent stream for an OPL2 (%d frames)"
                      % frames,
                      not box.logfull and frames >= ncmp and diff is None,
                      diff or ("the log buffer filled" if box.logfull else ""))
                hi = [r for r, v in log if 0x100 <= r < 0x200]
                check("4: no 1xxh write on an OPL2", not hi, repr(hi[:4]))
            press(m, "Digit1", 1.0)
            ok = wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
            time.sleep(2)
            check("5: RV1.RAD playing again, the tick named",
                  ok and box.kcell() == box.D["snd_tickp"],
                  "kernel %04x" % box.kcell())
            x, y = dispcp.win_rect(m, S, win)[:2]
            cx, cy = os88geom.close_xy(x, y)
            seg = box.claim()
            check("5: the tune's claim is in mem_tab while it plays",
                  seg and box.held(seg), "claim %04x" % seg)
            m.run()
            mo.click(cx, cy)
            gone = wait_for(lambda: box.dword("rad_seg") == 0, 30)
            time.sleep(1.5)
            own = m.read((box.dseg() << 4) + box.D["opl_own"], 8)
            check("5: the instance killed - DSV_RELINST FREED the claim (gone "
                  "from mem_tab, not only from rad_seg) and gave the chip and "
                  "channels 0..7 back",
                  gone and not box.held(seg) and box.dbyte("rad_chip") == 0 and
                  own == b"\xff" * 8 and box.dbyte("rad_owner") == 0xFF,
                  "rad_seg %04x chip %d own %s" % (box.dword("rad_seg"),
                                                   box.dbyte("rad_chip"),
                                                   own.hex()))
            check("5: ...and DSV_TICK is 0 without another verb",
                  box.kcell() == 0 and box.dcell() == 0,
                  "kernel %04x driver %04x" % (box.kcell(), box.dcell()))
            m.pause()
    finally:
        os.environ.pop("MARTYPC_OPL2", None)
    say("radopl2: %s" % ("FAILED: " + ", ".join(fails) if fails else
                         "2.1 refused in SPEC.md 96.6's words, 1.0 played on "
                         "the tick with radsim's stream, and the tick given "
                         "back by stop and by a killed instance"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
