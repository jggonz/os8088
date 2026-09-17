#!/usr/bin/env python3
"""The RAD replayer on an 8088 with an OPL3: the register stream is radsim's
(SPEC.md 34.12, 96.8).

    make && make radgate && python3 tests/radopl3.py

MartyPC's os8088_5150_sb_gla - a 4.77 MHz 8088, so SOUND.DRV decides TICK
class at attach (SPEC.md 34.13.2) - with patch 05's OPL3 (38Ah/38Bh decoded)
and a -DRADLOG SOUND.DRV + RADPLAY.DRV, which log every register write a
tune's start sequence, frames and stop sequence send (SPEC.md 96.8's .RLG).
RADGATE on B: drives the verbs and keeps every answer.

One boot, in this order:

  1. SND_CAP_OPL3 and SND_CAP_RAD published; [rad_class] = 0 (tick);
     DSV_TICK 0 on the idle desktop.
  2. RADGATE's `v`: tests/unit/radrows.py's whole hostile-file table fed to
     verb 4, and every answer equal to `radsim.check(row, opl3=True)` - the
     driver's validator against the host's, on the machine - AH compared
     exactly (RADGATE calls with AH = A5h) and CX the file's length on every
     refusal but RADE_CORRUPT. Then RADGATE's `a`: the refusals the table
     cannot reach (verb 4's SI + CX wrap, verb 5's sub-op, verb 6 with and
     without a tune, an undefined verb) answer AH = 0 and keep CX.
  3. RV2.RAD (version 2.1, every algorithm, both arrays): DSV_TICK names
     snd_tickp while it plays; after 1,500 frames, stop; DSV_TICK
     is 0 again; the log equals radsim's SENT stream for exactly the frames
     the machine played, record for record, start and stop sequences
     included (the stop ends 104h <- 00h, 105h <- 00h).
  4. RV1.RAD on the same OPL3: the same compare (its stop ends 105h <- 00h
     and no other 1xxh write appears).
     RV2BPM.RAD over 760 frames and RV1SLOW.RAD over 420: the same compare
     across each order list's PLAIN wrap (frames 695 and 380), which RV2.RAD's
     and RV1.RAD's jump markers never take.
  5. RV2.RAD again, then PAUSE: DSV_TICK 0 while paused (a paused tick-class
     tune asks for nothing, SPEC.md 34.13.7); resume names it again; a load of
     RV1.RAD while RV2.RAD plays replaces it from a new claim, and the old
     claim is gone from the kernel's mem_tab; all-off frees the new one too.
  6. The AdLib capture is not silent.
  7. FAN.RAD HALTs at interrupt time (SPEC.md 96.4.5 deviation 5): stopped,
     RSTF_HALTED, the chip and channels 0..7 back, DSV_TICK 0 by the next
     poll, its log radsim's; and a start after it plays.
  8. RV2.RAD playing and RADGATE CLOSED: DSV_RELINST's rd_kill on a 2.1 tune
     on the OPL3 - the log is radsim's to the last frame and then exactly the
     kill's key-offs, 104h <- 00h and 105h <- 00h, in that order.

What this cannot see is 86Box's real YMF262 timing and a person's ears; the
stream compare is what makes "plays like RAD" a fact rather than an opinion.
"""
import glob
import os
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

S = radlib.S
SND_CAP_OPL3, SND_CAP_RAD = 0x20, 0x40
KNOBS = ("MARTYPC_OPL2", "MARTYPC_NO38A")

fails = []


def say(*a):
    print(*a)
    sys.stdout.flush()


def check(name, cond, note=""):
    say("  %-4s %s%s" % ("ok" if cond else "FAIL", name,
                         ("  " + note) if note else ""))
    if not cond:
        fails.append(name)


def gate_open(m, box, M):
    from os88mouse import Mouse
    mo = Mouse(marty=m)
    dispcp.open_drive(m, mo, S, M.settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    before = set(dispcp.win_list(m, S))
    dispcp.open_named(m, mo, S, M.settle, wx, wy, "RADGATE.O88")
    M.settle(m)
    new = [w for w in dispcp.win_list(m, S) if w not in before]
    if not new:
        raise SystemExit("radgate never opened a window")
    w = new[-1]
    box.gseg = radlib.u16(m.read(os88geom.winptr(m, w, S) + os88geom.W_SEG, 2))
    x, y, ww, _h = dispcp.win_rect(m, S, w)
    mo.click(x + ww // 2, y + 5)
    m.run()
    time.sleep(1.0)
    return mo, w


def press(m, name, wait=0.5):
    m.run()
    m.key(name)
    time.sleep(wait)


def wait_for(cond, limit, step=0.5):
    t0 = time.time()
    while time.time() - t0 < limit:
        if cond():
            return True
        time.sleep(step)
    return cond()


def play_and_compare(m, box, label, keyname, fname, opl3, frames_wanted):
    """Load + start, check the tick is armed, play, stop, compare the log."""
    press(m, keyname, 1.0)
    ok = wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
    cf, al, _ah, _cx, _n = box.res()
    check("%s: %s loads and starts" % (label, fname), ok and cf == 0,
          "CF %d AL %d" % (cf, al))
    if not ok:
        return None
    k = box.kcell()
    check("%s: DSV_TICK names snd_tickp while it plays" % label,
          k == box.D["snd_tickp"] and box.dcell() == k,
          "kernel %04x driver %04x proc %04x" % (k, box.dcell(),
                                                  box.D["snd_tickp"]))
    wait_for(lambda: (box.frames() or 0) >= frames_wanted, 240, 1.0)
    f = box.frames()
    press(m, "KeyS", 1.0)
    wait_for(lambda: box.obyte("ro_state") == 1, 20)
    check("%s: stop - the tune is stopped and the chip given back" % label,
          box.obyte("ro_state") == 1 and box.dbyte("rad_chip") == 0,
          "state %d chip %d" % (box.obyte("ro_state"), box.dbyte("rad_chip")))
    time.sleep(1.0)
    check("%s: DSV_TICK 0 after stop" % label, box.kcell() == 0,
          "kernel %04x" % box.kcell())
    log = box.rlog()
    tune = open(os.path.join(ROOT, "tests", "fixtures", "rad", fname), "rb").read()
    frames, diff = radlib.compare_log(log or [], tune, opl3)
    check("%s: the register log is radsim's sent stream (%d frames, %d "
          "records)" % (label, frames, len(log or [])),
          log and not box.logfull and frames >= frames_wanted and diff is None,
          diff or ("the log buffer filled" if box.logfull else ""))
    say("  %s: %d frames played by the pacer" % (label, f))
    return log


RSTF_HALTED = 0x10


def halt_row(m, box, label, opl3):
    """FAN.RAD: a frame asks for more than RAD_PNMAX note plays and the pacer
    HALTs the tune at interrupt time (SPEC.md 96.4.5 deviation 5) - the
    stop sequence and the default patch written there, RSTF_HALTED set, and
    the resident told to give channels 0..7 and the chip back through the
    overlay's one service call (RADS_HALTED)."""
    import radrows
    press(m, "Digit5", 1.0)
    ok = wait_for(lambda: box.claim() and box.obyte("ro_state") == 1 and
                  (box.ostat() or b"\0" * 4)[3] & RSTF_HALTED, 30)
    st = box.ostat() or bytes(72)
    own = m.read((box.dseg() << 4) + box.D["opl_own"], 8)
    check("%s: FAN.RAD HALTs at interrupt time - stopped, RSTF_HALTED, the chip "
          "and channels 0..7 given back" % label,
          ok and box.dbyte("rad_chip") == 0 and own == b"\xff" * 8,
          "state %d flags %02x chip %d own %s" % (st[0], st[3],
                                                  box.dbyte("rad_chip"), own.hex()))
    t0 = box.tick()
    wait_for(lambda: box.kcell() == 0 or (box.tick() - t0) & 0xFFFF > 8, 20, 0.2)
    check("%s: ...and DSV_TICK 0 by the owner's next poll" % label,
          box.kcell() == 0 and box.dcell() == 0,
          "kernel %04x driver %04x after %d ticks" % (box.kcell(), box.dcell(),
                                                     (box.tick() - t0) & 0xFFFF))
    log = box.rlog()
    if log is not None:
        frames, diff = radlib.compare_log(log, radrows.FAN, opl3)
        check("%s: FAN.RAD's log is radsim's: the start, the one frame, FFFDh "
              "and the stop sequence" % label,
              frames == 1 and diff is None and not box.logfull, diff or "")
    # verb 5 START on the SAME claim the pacer HALTed (not a reload): the
    # resident re-claims channels 0..7 and the chip the interrupt gave back,
    # the overlay re-zeroes, and FAN.RAD halts again on its first frame
    n0 = box.res()[4]
    press(m, "KeyG", 1.0)
    ok = wait_for(lambda: box.res()[4] != n0 and
                  (box.ostat() or b"\0" * 4)[3] & RSTF_HALTED, 30)
    cf, al, _ah, _cx, _n = box.res()
    st = box.ostat() or bytes(72)
    own = m.read((box.dseg() << 4) + box.D["opl_own"], 8)
    log = box.rlog()
    frames, diff = (radlib.compare_log(log, radrows.FAN, opl3)
                    if log is not None else (0, "no log"))
    check("%s: verb 5 START on the HALTed claim plays again - CF = 0, a fresh "
          "start marker and radsim's one frame in the log, RSTF_HALTED set "
          "afresh, chip and channels given back again" % label,
          ok and cf == 0 and radlib.u32(st, 12) == 1 and st[0] == 1 and
          frames == 1 and diff is None and box.dbyte("rad_chip") == 0 and
          own == b"\xff" * 8,
          "CF %d AL %d state %d flags %02x frames %d log %s"
          % (cf, al, st[0], st[3], radlib.u32(st, 12), diff or frames))
    press(m, "Digit1", 1.0)
    ok = wait_for(lambda: box.obyte("ro_state") == 2, 30)
    check("%s: a load and start after a HALT plays again" % label, ok,
          "res %r" % (box.res(),))
    press(m, "KeyX", 1.0)
    wait_for(lambda: box.claim() == 0, 20)


def kill_row(m, box, mo, win, label):
    """RV2.RAD - a 2.1 tune on the OPL3 - playing, and RADGATE's window closed:
    DSV_RELINST -> rad_kill -> rd_kill at IF = 0 (SPEC.md 34.12.4). The log
    must be radsim's sent stream up to the last frame the pacer finished, and
    then EXACTLY the kill: a key-off for every channel the shadow holds keyed
    (B0h-B8h, then 1B0h-1B8h, bit 5 cleared), 104h <- 00h while NEW is still 1,
    and 105h <- 00h last - through the shadow, so a write equal to it is not
    sent; the default patch after it is not logged (96.8). The claim is gone
    from mem_tab and the chip, channels and tick are given back."""
    press(m, "Digit2", 1.0)
    ok = wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
    check("%s: RV2.RAD playing on the OPL3 before the kill" % label, ok)
    if not ok:
        return
    wait_for(lambda: (box.frames() or 0) >= 400, 120, 1.0)
    seg = box.claim()
    x, y = dispcp.win_rect(m, S, win)[:2]
    m.run()
    mo.click(*os88geom.close_xy(x, y))
    gone = wait_for(lambda: box.dword("rad_seg") == 0, 30)
    time.sleep(1.5)
    own = m.read((box.dseg() << 4) + box.D["opl_own"], 8)
    check("%s: RADGATE closed mid-tune - the claim freed from mem_tab, the "
          "chip, channels 0..7 and DSV_TICK given back" % label,
          gone and not box.held(seg) and box.dbyte("rad_chip") == 0 and
          own == b"\xff" * 8 and box.kcell() == 0,
          "rad_seg %04x held %s chip %d own %s tick %04x"
          % (box.dword("rad_seg"), box.held(seg), box.dbyte("rad_chip"),
             own.hex(), box.kcell()))
    log = box.rlog() or []
    marks = [i for i, (r, _v) in enumerate(log) if r == 0xFFFF]
    frames = len(marks)
    tune = open(os.path.join(ROOT, "tests", "fixtures", "rad", "RV2.RAD"),
                "rb").read()
    ref = radlib.radsim.sent_stream(
        radlib.radsim.reference_stream(tune, frames, opl3=True))
    pre = ref[:ref.index((0xFFFD, 0))] if (0xFFFD, 0) in ref else ref
    shadow = {}
    for r, v in pre:
        if r < 0x200:
            shadow[r] = v
    want = []
    for r in list(range(0xB0, 0xB9)) + list(range(0x1B0, 0x1B9)):
        if shadow.get(r, 0) & 0x20:
            want.append((r, shadow[r] & 0xDF))
    for r in (0x104, 0x105):
        if shadow.get(r, 0) != 0:
            want.append((r, 0))
    tail = log[len(pre):]
    check("%s: the log is radsim's up to the kill's frame (%d frames) and "
          "then exactly the kill - %d key-off(s), 104h <- 00h, 105h <- 00h"
          % (label, frames, len(want) - 2),
          frames >= 400 and not box.logfull and log[:len(pre)] == pre and
          tail == want and want[-2:] == [(0x104, 0), (0x105, 0)],
          "tail %s, want %s" % (["%03x=%02x" % t for t in tail[:24]],
                                ["%03x=%02x" % t for t in want[:24]]))


def main():
    import os88marty as M
    for k in KNOBS:
        os.environ.pop(k, None)
    t = os88build.tree("RADLOG=1", targets=("os8088-360.img",))
    t.apply()
    img = t.img("os8088-360.img")
    prefix = os.path.join(ROOT, "build", "radopl3")
    for f in glob.glob(prefix + ".*"):
        os.remove(f)
    os.environ["MARTYPC_WAV"] = prefix
    say("MartyPC os8088_5150_sb_gla, patch 05's OPL3, a -DRADLOG driver")
    try:
        with M.launch(img, apps=os88build.at("build/radgate360.img"),
                      machine="os8088_5150_sb_gla", boot=False) as m:
            m.run()
            M.settle(m, gate=M.desktop_up)
            M.no_saver(m)
            m.run()
            box = radlib.Box(m, ("RADLOG",))
            caps = box.caps()
            check("1: SND_CAP_OPL3 and SND_CAP_RAD published",
                  caps & (SND_CAP_OPL3 | SND_CAP_RAD) == SND_CAP_OPL3 | SND_CAP_RAD,
                  "DSV_CAPS %04x" % caps)
            check("1: the pacer class is TICK (an 8088)",
                  box.dbyte("rad_class") == 0)
            check("1: DSV_TICK 0 on the idle desktop", box.kcell() == 0,
                  "kernel %04x" % box.kcell())
            mo, win = gate_open(m, box, M)
            m.run()
            press(m, "KeyV", 1.0)
            done = wait_for(lambda: box.gread("rt_vdone", 1)[0] == 1, 240, 2)
            got = box.table()
            bad = radlib.table_diffs(got, opl3=True)
            check("2: the hostile-file table - every row's answer is radsim's "
                  "(%d rows)" % len(got), done and not bad,
                  "; ".join("%s: driver %r radsim %r" % b for b in bad[:4]))
            check("2: the table left no tune loaded", box.claim() == 0)
            press(m, "KeyA", 1.0)
            done = wait_for(lambda: box.gread("rt_adone", 1)[0] == 1, 60, 1)
            bad = radlib.ahrow_diffs(box.ahrows())
            check("2: the register contract on refusal - AH 0 whatever went "
                  "in, CX kept (%d rows)" % len(radlib.AHROWS),
                  done and not bad,
                  "; ".join("%s: driver %r want %r" % b for b in bad[:3]))
            check("2: ...and the rows left no tune loaded", box.claim() == 0)
            log2 = play_and_compare(m, box, "3", "Digit2", "RV2.RAD", True,
                                    radlib.radsim.BRANCH_COMPARED["RV2.RAD"])
            if log2:
                stop = radlib.radsim.frames_of(log2)[2]
                check("3: the 2.1 stop ends 104h <- 00h, 105h <- 00h",
                      stop[-2:] == [(0x104, 0), (0x105, 0)], repr(stop[-2:]))
            log1 = play_and_compare(m, box, "4", "Digit1", "RV1.RAD", True, 1500)
            # (1,500 rather than BRANCH_COMPARED's 1,000: that number is the
            # tightest gate over all rows, which for RV1.RAD is radopl2's)
            if log1:
                hi = [r for r, v in log1 if 0x100 <= r < 0x200]
                check("4: a 1.0 tune on an OPL3 writes no 1xxh but its "
                      "closing 105h <- 00h", hi == [0x105], repr(hi[:6]))
            # the order lists' plain wraps, which RV2.RAD and RV1.RAD (both
            # ending in jump markers) never take: radsim's BRANCH_PROMISES
            # "order-wrap" - RV2BPM.RAD wraps at frame 695, RV1SLOW.RAD's one
            # order at frame 380. The counts are radsim's BRANCH_COMPARED, and
            # --selfcheck holds each fixture's first hit inside its own number
            play_and_compare(m, box, "4b", "Digit4", "RV2BPM.RAD", True,
                             radlib.radsim.BRANCH_COMPARED["RV2BPM.RAD"])
            play_and_compare(m, box, "4c", "Digit3", "RV1SLOW.RAD", True,
                             radlib.radsim.BRANCH_COMPARED["RV1SLOW.RAD"])
            press(m, "Digit2", 1.0)
            wait_for(lambda: box.claim() and box.obyte("ro_state") == 2, 30)
            time.sleep(3)
            press(m, "KeyP", 1.0)
            wait_for(lambda: box.obyte("ro_state") == 3, 20)
            time.sleep(1.0)
            check("5: paused - DSV_TICK 0 (a paused tick-class tune asks for "
                  "nothing)", box.obyte("ro_state") == 3 and box.kcell() == 0,
                  "state %d kernel %04x" % (box.obyte("ro_state"), box.kcell()))
            press(m, "KeyR", 1.0)
            wait_for(lambda: box.obyte("ro_state") == 2, 20)
            time.sleep(1.0)
            check("5: resumed - DSV_TICK names the proc again",
                  box.obyte("ro_state") == 2 and box.kcell() == box.D["snd_tickp"],
                  "state %d kernel %04x" % (box.obyte("ro_state"), box.kcell()))
            old = box.claim()
            press(m, "Digit1", 2.0)
            ok = wait_for(lambda: box.claim() and box.claim() != old and
                          box.obyte("ro_state") == 2, 30)
            new = box.claim()
            check("5: a load while RV2.RAD plays REPLACES it - RV1.RAD playing "
                  "from a new claim, the old claim FREED, the tick still named",
                  ok and box.ostat()[1] == 0x10 and not box.held(old) and
                  box.held(new) and box.kcell() == box.D["snd_tickp"],
                  "claim %04x (was %04x, held %s) kernel %04x"
                  % (new, old, box.held(old), box.kcell()))
            press(m, "KeyX", 2.0)
            wait_for(lambda: box.claim() == 0, 20)
            time.sleep(1.0)
            check("5: all-off unloads it - the claim freed - and DSV_TICK is 0",
                  box.claim() == 0 and not box.held(new) and box.kcell() == 0,
                  "claim %04x held %s kernel %04x" % (box.claim(), box.held(new),
                                                      box.kcell()))
            halt_row(m, box, "7", True)
            kill_row(m, box, mo, win, "8")
            m.pause()
    finally:
        os.environ.pop("MARTYPC_WAV", None)
    time.sleep(1.0)
    wavs = [f for f in glob.glob(prefix + ".*") if "adlib" in f]
    loud = radlib.loud(wavs[0]) if wavs else 0
    check("6: the AdLib capture is not silent", loud > 1000,
          "%d loud samples in %s" % (loud, wavs[:1]))
    say("radopl3: %s" % ("FAILED: " + ", ".join(fails) if fails else
                         "the driver's register streams are radsim's on an "
                         "OPL3, its validator is radsim's, and the tick is "
                         "asked for exactly while a tune plays"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
