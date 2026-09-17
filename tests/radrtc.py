#!/usr/bin/env python3
"""The RTC pacer: IRQ8 frames at the tune's rate, and the machine given back
(SPEC.md 34.13.3, 96.8).

    make && make radgate && python3 tests/radrtc.py
    python3 tests/radrtc.py --secs 10 A      # one arm, a shorter window

QEMU `ADLIB=1` - an AT (SeaBIOS, a 1,024 Hz RTC at rest) with an OPL2 - so
SOUND.DRV decides RTC class at attach (SPEC.md 34.13.2) and RADGATE's `1`
plays RV1.RAD (version 1.0, 50 Hz) on int 70h.

THREE BOOTS, one driver each:

  A  the shipped driver. RV2.RAD is REFUSED (RADE_NEEDOPL3: this is an OPL2);
     RV1.RAD plays and the frame counter advances 50 a second, measured over
     --secs of BIOS ticks (75 by default: past the 64 s at which an
     un-renormalised word counter in the tick discipline would lap), AND
     IRQ8 IS WHAT PACED IT - the claim's rp_nirq / rp_nstep counters read
     200+ IRQ8s a second, 30+ of them running frames, at most 1.6 frames
     each, because the credit alone makes 50 a second out of one interrupt a
     tick (see paced_by_irq8); the control in the same boot slows the RTC to
     16 Hz through register A and must still play ~50 and FAIL all three; the
     status block verb 6 copies agrees with the overlay's own; the kernel's
     DSV_TICK reads 0 before, throughout and after (an RTC-class tune never
     asks for the tick, SPEC.md 34.13.7); stop gives back register B exactly
     as it was, the int 70h vector and both PIC mask bits; the BIOS tick kept
     18.2 a second of host time throughout. FAN.RAD HALTs inside the
     handler (SPEC.md 34.13.3 step 5) and gives register B and the vector back
     from there, and RV1.RAD plays again after it - a 2.1 tune, loaded with
     [opl_is3] poked to 1 through QEMU's gdb stub for that one verb 4 - and
     verb 5 START on that SAME halted claim (RADGATE `g`, not a reload) plays
     and HALTs again, re-zeroed and re-claimed. Three more of arm A's rows:
     a start REFUSED RADE_PACER because register B already has AIE on (a
     stranger's alarm) leaves the tune stopped, register B exactly as set and
     no PIE on it, however many verb 6 polls follow - and with B put back the
     same START on the same claim plays (its negative control); a 10 s int 15h AH=86h
     BIOS wait made while RV1.RAD plays (RADGATE `w`) takes the pacer's chain
     path on every interrupt - SeaBIOS's own 40:A0 bit 0 is seen set - and
     returns NO LATER than the same wait with nothing armed (the shorter of
     two controls taken back to back earlier in the boot, +5%: both are
     host-paced samples and a stall can only lengthen one, so the comparison
     is one-sided) with the frames still 50 a second ACROSS the wait, PIE on,
     the busy byte clear; and the INVOLUNTARY teardown, RADGATE's window closed mid-tune:
     DSV_RELINST gives back register B, the vector and the masks, frees the
     claim from mem_tab and leaves rad_iarm and DSV_TICK 0.
  B  a -DRADSLOW driver: every frame busy-waits 4 ms at IF = 0, so the RTC
     loses about four periods in five around each frame. The tick
     discipline's credit must still hold 50 a second, +-1%, still paced by
     IRQ8 by the same three checks.
  C  -DRADSLOW -DRADNOCREDIT: the same frames with the credit taken out must
     fall measurably short (below 47) - the negative control that makes B a
     test of the credit rather than of luck - while IRQ8s are still
     delivered and at least 10 frames a second still run, so a pacer that
     ran nothing cannot pass it.

What this row does NOT reach is named in SPEC.md 96.8: the handler's step 0 (a
nested IRQ8 inside a ROM's sti) and an alarm chain.
"""
import argparse
import os
import subprocess
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
import os88qemu                                             # noqa: E402
import radlib                                               # noqa: E402
from ethernet import Qemu, Mouse                            # noqa: E402
from sndtick import Gdb, free_port                          # noqa: E402

S = radlib.S
PIDFILE = os.path.join(ROOT, "build", "qemu.pid")
SOCK = os.path.join(ROOT, "build", "qmp.sock")
SND_CAP_FM, SND_CAP_OPL3, SND_CAP_RAD = 0x02, 0x20, 0x40
RADE_NEEDOPL3, RADE_PACER = 4, 9
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


def launch(img):
    os88qemu.kill(PIDFILE, SOCK)
    global GDBPORT
    GDBPORT = free_port()
    em = "qemu" + "-system-i386"        # never whole: tests/os88qemu.py
    subprocess.run(
        em + " -machine pc,vmport=off"
        " -drive file=%s,format=raw,if=floppy -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1"
        " -chardev msmouse,id=m0 -serial chardev:m0"
        " -audiodev none,id=snd -device adlib,audiodev=snd"
        " -gdb tcp:127.0.0.1:%d"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (img, os88build.at("build/radgate.img"), GDBPORT, SOCK, PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)
    m = Qemu(SOCK)
    t0 = time.time()
    while time.time() - t0 < 90:
        if radlib.u16(m.read(S("drv_tab") + 2, 2)) and \
                m.read(S("spl_live"), 1)[0] == 0:
            break
        time.sleep(0.5)
    time.sleep(5)
    return m


def open_gate(m, box):
    mo = Mouse()
    settle = lambda *a, **k: time.sleep(2.0)            # noqa: E731
    dispcp.open_drive(m, mo, S, settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    before = set(dispcp.win_list(m, S))
    dispcp.open_named(m, mo, S, settle, wx, wy, "RADGATE.O88")
    time.sleep(3)
    new = [w for w in dispcp.win_list(m, S) if w not in before]
    if not new:
        raise SystemExit("radrtc: RADGATE never opened a window")
    w = new[-1]
    box.gseg = radlib.u16(m.read(os88geom.winptr(m, w, S) + os88geom.W_SEG, 2))
    x, y, ww, _h = dispcp.win_rect(m, S, w)
    mo.click(x + ww // 2, y + 5)                        # focus it
    time.sleep(1.0)
    return mo, w


def key(m, k):
    m.hmp("sendkey " + k)
    time.sleep(0.4)


def cmos(m, index):
    """Read a CMOS register with the guest stopped, and leave the index where
    the kernel's discipline leaves it (0Dh)."""
    m.hmp("stop")
    try:
        m.hmp("o /b 0x70 0x%02x" % (0x80 | index))
        out = m.hmp("i /b 0x71")
        m.hmp("o /b 0x70 0x0d")
        m.hmp("i /b 0x71")
    finally:
        m.hmp("cont")
    return int(out.strip().split()[-1], 16)


def cmos_write(m, index, val):
    m.hmp("stop")
    try:
        m.hmp("o /b 0x70 0x%02x" % (0x80 | index))
        m.hmp("o /b 0x71 0x%02x" % val)
        m.hmp("o /b 0x70 0x0d")
        m.hmp("i /b 0x71")
    finally:
        m.hmp("cont")


def pic_masks(m):
    m.hmp("stop")
    try:
        a = int(m.hmp("i /b 0x21").strip().split()[-1], 16)
        b = int(m.hmp("i /b 0xa1").strip().split()[-1], 16)
    finally:
        m.hmp("cont")
    return a, b


def vec70(m):
    b = m.read(0x1C0, 4)
    return radlib.u16(b, 2), radlib.u16(b, 0)


def irqs(box):
    """The claim's two gate counters (SPEC.md 34.13.3): (IRQ8s that reached
    step 5, those that ran a frame)."""
    seg = box.claim()
    b = box.m.read((seg << 4) + box.O["rp_nirq"], 8)
    return radlib.u32(b, 0), radlib.u32(b, 4)


def measure(box, secs, label):
    """(frames per second, BIOS ticks per host second, worst DSV_TICK, IRQ8s
    a second, stepping IRQ8s a second, frames per stepping IRQ8) over `secs`
    of ticks."""
    t0, f0, w0 = box.tick(), box.frames(), time.time()
    i0, s0 = irqs(box)
    need = int(secs * TICK_HZ)
    kmax = 0
    while (box.tick() - t0) & 0xFFFF < need:
        kmax = max(kmax, box.kcell())
        time.sleep(1.0)
    t1, f1, w1 = box.tick(), box.frames(), time.time()
    i1, s1 = irqs(box)
    dt = (t1 - t0) & 0xFFFF
    sec = dt / TICK_HZ
    fps = (f1 - f0) / sec
    ips, sps = (i1 - i0) / sec, (s1 - s0) / sec
    per = (f1 - f0) / max(1, s1 - s0)
    say("  %s: %d frames in %d ticks = %.2f frames a second; %.1f ticks a "
        "host second; %.0f IRQ8s a second, %.1f of them ran frames, %.2f "
        "frames each" % (label, f1 - f0, dt, fps, dt / (w1 - w0), ips, sps,
                         per))
    return fps, dt / (w1 - w0), kmax, ips, sps, per


# IRQ8 PACING, NOT TICK PACING (the verifier's M1). 50 frames a second is what
# the tick discipline's credit ALSO produces when IRQ8 arrives once a tick:
# rd_isr70 credits the missed periods at the first interrupt of each BIOS tick
# and runs up to RAD_FRMAX = 7 frames there. So the frame rate alone proves
# nothing about who paced it, and these do: interrupts delivered at many times
# the tick rate, a frame-running interrupt for most frames, and about one
# frame each. Arm A's control slows the RTC to 16 Hz through register A -
# below the tick - where the frame rate must STAY at 50 and all three of
# these must fail.
IRQ_MIN, STEP_MIN, PER_MAX = 200.0, 30.0, 1.6


def paced_by_irq8(ips, sps, per):
    return ips >= IRQ_MIN and sps >= STEP_MIN and per <= PER_MAX


def arm(label, knobs, secs):
    say("%s: QEMU ADLIB=1 - %s" % (label, ", ".join(knobs) or "the shipped "
                                                              "driver"))
    if knobs:
        t = os88build.tree(*["%s=1" % k for k in knobs],
                           targets=("os8088.img",))
        img = t.img("os8088.img")
    else:
        img = os88build.at("build/os8088.img")
    m = launch(img)
    box = radlib.Box(m, knobs)
    try:
        caps = box.caps()
        check("%s: FM and the RAD verbs, no OPL3" % label,
              caps & (SND_CAP_FM | SND_CAP_RAD | SND_CAP_OPL3)
              == SND_CAP_FM | SND_CAP_RAD, "DSV_CAPS %04x" % caps)
        check("%s: the pacer class is RTC" % label, box.dbyte("rad_class") == 1,
              "[rad_class] %d" % box.dbyte("rad_class"))
        b0 = cmos(m, 0x0B)
        v0 = vec70(m)
        p0 = pic_masks(m)
        mo, win = open_gate(m, box)
        if not knobs:
            refused_row(m, box, b0)
            # TWICE, back to back, and the SHORTER is the control: both are
            # host-paced samples (QEMU's RTC is the host's clock and SeaBIOS
            # counts DELIVERED periods), and a host stall can only LENGTHEN
            # one. A single stretched control once made this the one RAD
            # assertion that went red on unchanged bytes - 282 ticks against
            # a 317-tick control, where every other run of either read 282-288.
            ok, cf, _ah, c1, pending = bios_wait(m, box)[:5]
            ok2, cf2, _ah2, c2, pending2 = bios_wait(m, box)[:5]
            ctl = min(c1, c2)
            check("A: the control - the same wait with no tune armed returns, "
                  "40:A0 bit 0 set during it, twice",
                  ok and ok2 and cf == 0 and cf2 == 0 and pending and pending2,
                  "%d and %d ticks" % (c1, c2))
            if not (ok and ok2 and cf == 0 and cf2 == 0):
                ctl = 0
        if not knobs:
            key(m, "2")
            time.sleep(2)
            cf, al, _ah, _cx, _n = box.res()
            check("A: RV2.RAD is refused on an OPL2 (RADE_NEEDOPL3)",
                  cf == 1 and al == RADE_NEEDOPL3, "CF %d AL %d" % (cf, al))
        check("%s: DSV_TICK 0 before the start" % label, box.kcell() == 0,
              "kernel %04x" % box.kcell())
        key(m, "1")
        time.sleep(3)
        cf, al, _ah, _cx, _n = box.res()
        playing = box.claim() and box.obyte("ro_state") == 2
        check("%s: RV1.RAD loads and starts" % label, cf == 0 and playing,
              "CF %d AL %d, claim %04x" % (cf, al, box.claim()))
        if not playing:
            return
        check("%s: register B has PIE on while it plays" % label,
              cmos(m, 0x0B) & 0x40 != 0)
        check("%s: int 70h names SOUND.DRV's rad_i70 while it plays - the "
              "BIOS chain returns into the resident, never a claim (34.13.3)"
              % label, vec70(m) == (box.dseg(), box.D["rad_i70"]),
              "vector %04x:%04x, rad_i70 %04x:%04x"
              % (vec70(m) + (box.dseg(), box.D["rad_i70"])))
        fps, tps, kmax, ips, sps, per = measure(box, secs, label)
        if label == "C":
            check("C: without the credit, measurably short of 50 - and not "
                  "because nothing ran: IRQ8s still delivered and frames "
                  "still stepped",
                  10.0 <= fps < 47.0 and ips >= IRQ_MIN,
                  "%.2f frames, %.0f IRQ8s a second" % (fps, ips))
        else:
            check("%s: 50 frames a second +-1%%" % label,
                  abs(fps - 50.0) <= 0.5, "%.2f" % fps)
            check("%s: ...PACED BY IRQ8 - at least %d IRQ8s a second, %d of "
                  "them running frames, at most %.1f frames each"
                  % (label, IRQ_MIN, STEP_MIN, PER_MAX),
                  paced_by_irq8(ips, sps, per),
                  "%.0f IRQ8s, %.1f stepping, %.2f frames each"
                  % (ips, sps, per))
        if label == "A":
            slow_rtc_row(m, box)
        check("%s: DSV_TICK 0 throughout (an RTC-class tune never asks)"
              % label, kmax == 0 and box.kcell() == 0,
              "worst %04x" % kmax)
        if label == "A":
            wait_row(m, box, ctl)
            check("A: the BIOS tick kept time (18.2 a host second, +-5%)",
                  abs(tps - TICK_HZ) <= TICK_HZ * 0.05, "%.2f" % tps)
            st = box.gread("rt_stat", radlib.RST_LEN)
            mine = box.ostat()
            check("A: verb 6's copy is the status block (state, version, "
                  "pacer, rate)",
                  box.gread("rt_statok", 1)[0] == 1 and st[0] == 2 and
                  st[1] == 0x10 and st[2] == 1 and radlib.u16(st, 4) == 500
                  and mine[1:3] == st[1:3],
                  "block %s" % st[:12].hex())
        key(m, "s")
        time.sleep(2)
        cf, al, _ah, _cx, _n = box.res()
        check("%s: stop answers CF = 0 and the tune is stopped" % label,
              cf == 0 and box.obyte("ro_state") == 1 and
              box.dbyte("rad_chip") == 0,
              "CF %d AL %d, state %d, chip %d" % (cf, al, box.obyte("ro_state"),
                                                 box.dbyte("rad_chip")))
        b1 = cmos(m, 0x0B)
        check("%s: register B restored after stop" % label, b1 == b0,
              "before %02x after %02x" % (b0, b1))
        check("%s: the int 70h vector restored" % label, vec70(m) == v0,
              "before %04x:%04x after %04x:%04x" % (v0 + vec70(m)))
        p1 = pic_masks(m)
        check("%s: the PIC mask bits restored (IRQ2 master, IRQ8 slave)"
              % label, (p1[0] & 4, p1[1] & 1) == (p0[0] & 4, p0[1] & 1),
              "before %02x/%02x after %02x/%02x" % (p0 + p1))
        f = box.frames()
        time.sleep(2)
        check("%s: no frame after stop" % label, box.frames() == f)
        if label == "A":
            # FAN.RAD is a version 2.1 tune and this is an OPL2, so the one
            # way to reach a HALT on the RTC class here is to tell verb 4 the
            # chip is an OPL3: [opl_is3] <- 1 through the gdb stub for the
            # load, and back to 0 after. Its second-array writes land on
            # QEMU's alias of 388h, which is noise nobody listens to.
            is3 = (box.dseg() << 4) + box.D["opl_is3"]
            g = Gdb(GDBPORT)
            try:
                g.write(is3, b"\x01")
            finally:
                g.detach()
            key(m, "5")
            time.sleep(4)
            st = box.ostat() or bytes(72)
            check("A: FAN.RAD HALTs inside the RTC handler - stopped, "
                  "RSTF_HALTED, the chip back",
                  box.claim() and box.obyte("ro_state") == 1 and st[3] & 0x10
                  and box.dbyte("rad_chip") == 0,
                  "state %d flags %02x chip %d" % (st[0], st[3],
                                                   box.dbyte("rad_chip")))
            check("A: ...and the HALT gave back register B and the vector",
                  cmos(m, 0x0B) == b0 and vec70(m) == v0,
                  "B %02x vector %04x:%04x" % ((cmos(m, 0x0B),) + vec70(m)))
            n0 = box.res()[4]
            key(m, "g")                     # verb 5 START on the SAME claim
            time.sleep(3)
            cf, al, _ah, _cx, n = box.res()
            st = box.ostat() or bytes(72)
            check("A: verb 5 START on the claim the handler HALTed plays and "
                  "HALTs again - CF = 0, re-zeroed (one frame), RSTF_HALTED "
                  "set afresh, the chip, register B and the vector back",
                  n != n0 and cf == 0 and radlib.u32(st, 12) == 1 and
                  st[0] == 1 and st[3] & 0x10 and box.dbyte("rad_chip") == 0
                  and cmos(m, 0x0B) == b0 and vec70(m) == v0,
                  "CF %d AL %d, state %d flags %02x frames %d chip %d"
                  % (cf, al, st[0], st[3], radlib.u32(st, 12),
                     box.dbyte("rad_chip")))
            g = Gdb(GDBPORT)
            try:
                g.write(is3, b"\x00")
            finally:
                g.detach()
            key(m, "1")
            time.sleep(3)
            f = box.frames()
            time.sleep(2)
            check("A: RV1.RAD plays again after the HALT",
                  box.obyte("ro_state") == 2 and box.frames() > f,
                  "state %d" % box.obyte("ro_state"))
            key(m, "s")
            time.sleep(2)
        key(m, "x")
        time.sleep(2)
        check("%s: all-off unloads the tune and frees its claim" % label,
              box.claim() == 0 and box.kcell() == 0,
              "claim %04x kernel %04x" % (box.claim(), box.kcell()))
        if label == "A":
            kill_row(m, box, mo, win, b0, v0, p0)
    finally:
        m.quit()
        time.sleep(1.5)


def slow_rtc_row(m, box):
    """The negative control for paced_by_irq8: register A's rate select to
    16 Hz (RS = 1100b), under the BIOS tick. The credit keeps the frame rate
    at 50 - which is exactly why a frame-rate check cannot see pacing - and
    the IRQ8 checks must all fail. Register A is put back and read back."""
    a0 = cmos(m, 0x0A)
    cmos_write(m, 0x0A, (a0 & 0xF0) | 0x0C)
    try:
        time.sleep(2)
        fps, _tps, _k, ips, sps, per = measure(box, 20, "A, RTC at 16 Hz")
        check("A: the control - the RTC at 16 Hz still plays ~50 frames a "
              "second on the credit (so a frame rate proves no pacing) and "
              "FAILS every IRQ8 pacing check",
              abs(fps - 50.0) <= 3.0 and ips < IRQ_MIN and sps < STEP_MIN and
              per > PER_MAX and not paced_by_irq8(ips, sps, per),
              "%.2f frames, %.0f IRQ8s, %.1f stepping, %.2f each"
              % (fps, ips, sps, per))
    finally:
        cmos_write(m, 0x0A, a0)
    check("A: register A back at 1,024 Hz", cmos(m, 0x0A) == a0,
          "%02x" % cmos(m, 0x0A))


def refused_row(m, box, b0):
    """A start the RTC refuses (SPEC.md 34.13.3 step 1: AIE is a stranger's)
    must commit "stopped" in its own window and never set PIE."""
    b_aie = b0 | 0x20
    cmos_write(m, 0x0B, b_aie)
    try:
        key(m, "1")
        time.sleep(3)
        cf, al, _ah, _cx, _n = box.res()
        p0 = radlib.u16(box.gread("rt_polls", 2))
        time.sleep(3)                       # a dozen verb 6 polls
        polls = (radlib.u16(box.gread("rt_polls", 2)) - p0) & 0xFFFF
        st = box.gread("rt_stat", radlib.RST_LEN)
        b1 = cmos(m, 0x0B)
        check("A: a start with AIE already on is refused RADE_PACER, stopped, "
              "and register B untouched (no PIE) across %d verb 6 polls"
              % polls,
              cf == 1 and al == RADE_PACER and box.claim() and
              box.obyte("ro_state") == 1 and box.obyte("rp_armed") == 0 and
              st[0] == 1 and box.dbyte("rad_chip") == 0 and b1 == b_aie
              and polls >= 4,
              "CF %d AL %d, state %d, verb 6 state %d, chip %d, B set %02x "
              "read %02x" % (cf, al, box.obyte("ro_state") if box.claim()
                             else -1, st[0], box.dbyte("rad_chip"), b_aie, b1))
        check("A: ...and the int 70h vector never became ours",
              box.dbyte("rad_iarm") == 0)
    finally:
        cmos_write(m, 0x0B, b0)
    # THE NEGATIVE CONTROL: the same loaded claim, the same verb, register B
    # as the BIOS left it - so the refusal above was AIE's and nothing else's
    n0 = box.res()[4]
    key(m, "g")
    time.sleep(3)
    cf, al, _ah, _cx, n = box.res()
    check("A: the control - with AIE clear again the same START on the same "
          "claim answers CF = 0, plays and turns PIE on",
          n != n0 and cf == 0 and box.obyte("ro_state") == 2 and
          box.obyte("rp_armed") == 1 and cmos(m, 0x0B) & 0x40 != 0 and
          box.dbyte("rad_iarm") == 1,
          "CF %d AL %d state %d B %02x" % (cf, al, box.obyte("ro_state"),
                                           cmos(m, 0x0B)))
    key(m, "s")
    time.sleep(2)
    check("A: ...and its stop gives register B back as the BIOS left it",
          box.obyte("ro_state") == 1 and cmos(m, 0x0B) == b0,
          "B %02x, before %02x" % (cmos(m, 0x0B), b0))


def bios_wait(m, box):
    """RADGATE `w`: (done, CF, AH, ticks, 40:A0 bit 0 seen, t0, f0, t1, f1)."""
    n0 = box.gread("rt_wn", 1)[0]
    k0, f0 = box.tick(), box.frames()
    key(m, "w")
    time.sleep(2)
    pending = m.read(0x4A0, 1)[0] & 1
    ok = False
    t0 = time.time()
    while time.time() - t0 < 60:
        if box.gread("rt_wn", 1)[0] != n0:
            ok = True
            break
        time.sleep(0.5)
    k1, f1 = box.tick(), box.frames()
    t_0 = radlib.u16(box.gread("rt_wt0", 2))
    t_1 = radlib.u16(box.gread("rt_wt1", 2))
    dt = (t_1 - t_0) & 0xFFFF
    cf, ah = box.gread("rt_wcf", 2)
    return ok, cf, ah, dt, pending, k0, f0, k1, f1


def wait_row(m, box, ctl):
    """int 15h AH=86h while RV1.RAD plays: SeaBIOS owns 40:A0 and PIE for the
    wait, so every IRQ8 takes steps 1-2's chain through rad_i70ch. `ctl` is
    the shorter of two such waits taken back to back with nothing armed:
    QEMU's RTC is host paced and the BIOS counts DELIVERED periods, so the
    wait is compared with itself rather than with 182.

    THE COMPARISON IS ONE-SIDED, and that is the property rather than a
    tolerance: the claim is that our IRQ8 chain does not make the BIOS wait
    LONGER. A wait that comes back early against a control the host stretched
    is not a failure of anything, and rejecting it made this row go red on
    unchanged bytes. Both samples are host-paced; the rest of the row - the
    frame rate ACROSS the wait, 40:A0's bit, PIE, the busy byte, the vector -
    is not."""
    ok, cf, ah, dt, pending, k0, f0, k1, f1 = bios_wait(m, box)
    check("A: a 10 s int 15h AH=86h wait made while it plays returns (CF = 0) "
          "no LONGER than the same wait with nothing playing (+5%), with "
          "40:A0 bit 0 seen set during it",
          ok and cf == 0 and ctl and dt <= ctl * 1.05 and pending,
          "done %s CF %d AH %02x, %d ticks against %d idle, 40:A0 bit 0 %d"
          % (ok, cf, ah, dt, ctl, pending))
    kd = (k1 - k0) & 0xFFFF
    during = (f1 - f0) / (kd / TICK_HZ) if kd else 0.0
    check("A: ...and the tune paced 50 frames a second +-1% ACROSS the "
          "wait, every interrupt of it chained",
          ok and abs(during - 50.0) <= 0.5,
          "%d frames in %d ticks = %.2f" % (f1 - f0, kd, during))
    time.sleep(1)
    check("A: ...PIE on after SeaBIOS cleared it at expiry, the busy byte "
          "clear, int 70h still rad_i70",
          cmos(m, 0x0B) & 0x40 != 0 and box.dbyte("rad_ibusy") == 0 and
          vec70(m) == (box.dseg(), box.D["rad_i70"]),
          "B %02x busy %d" % (cmos(m, 0x0B), box.dbyte("rad_ibusy")))


def kill_row(m, box, mo, win, b0, v0, p0):
    """The involuntary end on the RTC class: DSV_RELINST -> rad_kill ->
    rd_kill -> rp_disarm (RADS_UNHOOK at IF = 0) -> MEM_FREE."""
    key(m, "1")
    time.sleep(3)
    seg = box.claim()
    playing = seg and box.obyte("ro_state") == 2 and \
        vec70(m) == (box.dseg(), box.D["rad_i70"])
    check("A: RV1.RAD playing again on int 70h before the kill", playing,
          "claim %04x" % seg)
    if not playing:
        return
    x, y = dispcp.win_rect(m, S, win)[:2]
    cx, cy = os88geom.close_xy(x, y)
    mo.click(cx, cy)
    t0 = time.time()
    while time.time() - t0 < 30 and box.dword("rad_seg"):
        time.sleep(0.5)
    time.sleep(1.5)
    p1 = pic_masks(m)
    b1 = cmos(m, 0x0B)
    check("A: RADGATE killed mid-tune - DSV_RELINST gave back register B, "
          "the int 70h vector and both mask bits, cleared rad_iarm, freed "
          "the claim from mem_tab and left DSV_TICK 0",
          box.dword("rad_seg") == 0 and not box.held(seg) and b1 == b0 and
          vec70(m) == v0 and
          (p1[0] & 4, p1[1] & 1) == (p0[0] & 4, p0[1] & 1) and
          box.dbyte("rad_iarm") == 0 and box.kcell() == 0 and
          box.dbyte("rad_chip") == 0,
          "seg %04x held %s B %02x/%02x vector %04x:%04x masks %02x/%02x "
          "iarm %d tick %04x" % ((box.dword("rad_seg"), box.held(seg), b0, b1)
                                 + vec70(m) + p1 +
                                 (box.dbyte("rad_iarm"), box.kcell())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--secs", type=int, default=75)
    ap.add_argument("arms", nargs="*", default=["A", "B", "C"])
    a = ap.parse_args()
    try:
        if "A" in a.arms:
            arm("A", (), a.secs)
        if "B" in a.arms:
            arm("B", ("RADSLOW",), a.secs)
        if "C" in a.arms:
            arm("C", ("RADSLOW", "RADNOCREDIT"), a.secs)
    finally:
        os88qemu.kill(PIDFILE, SOCK)
    say("radrtc: %s" % ("FAILED: " + ", ".join(fails) if fails else
                        "IRQ8 paces 50 a second, the credit holds it under "
                        "4 ms frames and nothing without it does, and stop "
                        "gives the RTC back"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
