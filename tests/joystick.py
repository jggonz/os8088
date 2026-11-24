#!/usr/bin/env python3
"""JOYSTICK.DRV through a package's door. (SPEC.md 137.2-137.4, 137.7)

    make joygatetest && python3 tests/joystick.py

The instrument is MartyPC's game port (`os8088_5150_cga_gla_joy`) driven by
`Marty.joy()`: stick A and B at known positions or resistances, the four
buttons at a known mask, effective at once, in guest time. The subject is
reached the way every game reaches it - tests/joygate/joygate.asm includes
apps/os88joy.inc and, on a click, calls joy_find / joy_read / joy_state and
JOYV_RAW, timing two of them with pit_now, and leaves every answer in its own
image. The harness reads those bytes, not the screen. Two boots:

LEG 1, a machine WITH a game port and a stick centred:
 1. The boot sniff (SPEC.md 137.2) ticked JOYSTICK.DRV by itself: joy_find
    answers CF=0 before anything attaches it; OSAPI_DRV_USE is idempotent.
 2. INFO: all four pots answering, type 2B, two buttons, one stick.
 3. Centre answers 0 with no direction bit; the dead zone holds 4% to 0.
 4. UNCALIBRATED (SPEC.md 137.3: the first sample is the centre, MIN
    JOY_RAW0, MAX twice the centre less that) every deflection has the right
    sign, direction bit and a magnitude past 118 of 127 - the span is
    symmetric about the centre and starts at a 0 ohm stick's count, so a
    stick reaches its ends before it is calibrated (QA Q1: it was -108..+92).
 5. CALIBRATED the way the page does it (the driver's joy_cal written from
    the measured counts at rest and at both stops), full deflection answers
    +-127 on both axes, + is right and down, half travel about half, and the
    direction is 8-way.
 6. RAW counts: the span between 0 and 100 kohm is the model's 1,100 us, and
    the count rises with the resistance.
 7. A press is counted ONCE (a held button answers held and no press on the
    next read), a release then a press counts again, a button of stick B is
    in JOYV_RAW and not in a 2-button type's state.
 8. Stick B on a one-stick type answers CF=1; JOYV_STATE agrees with READ.
 9. Timing: a slow stick's read takes longer than a fast one's by about the
    physics (it IS timed), and every verb, door and cooking and all, stays
    under the 4.17 ms byte time of an 8250 at 2400 baud.

LEG 2, a machine with NO game port (the standard 5150), which is
"nothing plugged in" and the refusal control for leg 1:
 10. Nothing ticked: joy_find answers CF=1 BEFORE the attach.
 11. After OSAPI_DRV_USE the driver is up and says no stick (INFO's axes 0),
     joy_read A answers CF=1, stick B CF=1, and the read stays bounded.
"""
import argparse
import os
import re
import struct
import sys

_OS88_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tools"))
sys.path.insert(0, os.path.join(_OS88_ROOT, "tests"))
import os88marty                                       # noqa: E402
import os88ui                                          # noqa: E402
import os88build                                       # noqa: E402
import joyasm                                          # noqa: E402
from os88fixture import need                           # noqa: E402

MACHINE = "os8088_5150_cga_gla_joy"
UNIT_US = 0.838                         # one PIT count
BASE_US, PER_OHM_US = 25.2, 0.011       # MartyPC's one-shot (SPEC.md 137.7)

# the gate's result block: (name, byte offset after the magic) - every field a word
JG = ["cmd", "seq", "use_cf", "use_ax", "pr_cf", "pr_ax", "f_cf", "f_ax",
      "f_cx", "f_dx", "a1_cf", "a1_ax", "a1_bx", "a1_cx", "a1_t", "a2_cf",
      "a2_ax", "a2_bx", "a2_cx", "b_cf", "b_ax", "b_bx", "b_cx", "r_cf",
      "r_x1", "r_y1", "r_x2", "r_y2", "r_bt", "s_cf", "s_t", "t0"]
ST = dict(X1=0, Y1=1, X2=2, Y2=3, BTN=12, PRESS=13, DIRA=14, DIRB=15,
          HAT=16, THR=17, RUD=18, AXES=19)
L, R, U, D = 1, 2, 4, 8                 # JOYD_*
# (name, a=(x, y), the axis it moves, the sign it answers, the direction bit)
DEFL = (("left", (-1, 0), "x", -1, L), ("right", (1, 0), "x", 1, R),
        ("up", (0, 1), "y", -1, U), ("down", (0, -1), "y", 1, D))


class Gate(object):
    """The JOYGATE package open on a machine: one click = one probe."""

    def __init__(self, ui, img):
        self.ui, self.m, self.mo = ui, ui.m, ui.mo
        ui.open_drive("B")
        win = ui.open("JOYGATE.O88")
        rec = self.m.read(ui.sym("wm_wins") + win.i * os88ui.geom.WIN_SIZE,
                          os88ui.geom.WIN_SIZE)
        self.seg = struct.unpack_from("<H", rec, os88ui.geom.W_SEG)[0]
        at = self.m.readseg(self.seg, 0, img).find(b"JGATE:")
        if at < 0:
            sys.exit("joystick: the gate's result block is not in its image")
        self.base = at + 6

    def fields(self):
        raw = self.m.readseg(self.seg, self.base, 2 * len(JG) + 20)
        f = dict(zip(JG, struct.unpack_from("<%dH" % len(JG), raw, 0)))
        f["st"] = raw[2 * len(JG):2 * len(JG) + 20]
        return f

    def probe(self, cmd=0):
        """Write the command word, click the window, wait on the sequence."""
        s0 = self.fields()["seq"]
        self.m.write((self.seg << 4) + self.base, struct.pack("<H", cmd))
        w = self.ui.window("Joy Gate")
        self.mo.click(w.x + w.w // 2, w.y + w.h - 6)
        os88marty.until(self.m, lambda _: self.fields()["seq"] != s0,
                        "the gate's probe", poll=0.1, guest=30.0)
        return self.fields()


def say(*a):
    print(*a)
    sys.stdout.flush()


def sb(v):
    v &= 0xFF
    return v - 256 if v >= 128 else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default=None)
    ap.add_argument("--break", dest="brk", action="store_true",
                    help="the red arm: poke the loaded driver so left/up no "
                    "longer negate; the row must FAIL")
    a = ap.parse_args()
    need("build/joygate360.img", "build/os8088-360.img")
    jtmo = joyasm.equ("drivers/joy/joypkg.inc", "JOY_TMO")
    JCAL = {k: joyasm.equ("drivers/joy/joypkg.inc", "JCAL_" + k)
            for k in ("MIN", "CEN", "MAX", "DZL", "DZH", "SZ")}
    JB_DZ = joyasm.equ("drivers/joy/joypkg.inc", "JOYB_DZ")
    dimg, dsym = joyasm.assemble()
    fails = []

    def check(ok, msg):
        if not ok:
            fails.append(msg)
            say("FAIL: " + msg)

    img = os.path.getsize(os88build.at("build/joygate.bin"))
    # one 2400-baud byte on an 8250: the longest a verb may be away (CLAUDE.md,
    # *Retrace waits still owe the hardware service*)
    byte_units = int(4170 / UNIT_US)

    def session(machine):
        ui = os88ui.boot("build/os8088-360.img", apps="build/joygate360.img",
                         machine=machine, quick=True)
        return ui

    # =========================== LEG 1: a game port ==========================
    with session(MACHINE) as ui:
        m, mo = ui.m, ui.mo
        ui.still(0)
        gate = Gate(ui, img)
        probe = gate.probe
        joy = m.joy
        joy(a=(0, 0), b=(0, 0), buttons=0)
        if a.brk:
            seg = struct.unpack("<H", m.read(ui.sym("joy_row") + 2, 2))[0]
            bimg, at = joyasm.sign_break(dimg, dsym)
            m.write((seg << 4) + at, bimg[at:at + 2])
            say("red arm: joy_calpos's neg al at %04X poked to nops" % at)

        # ---- 1 the sniff ticked it, attach is idempotent ---------------------
        f = probe(0)
        say("boot sniff: joy_find cf=%d axes=%02X" % (f["f_cf"], f["f_ax"] & 255))
        check(f["f_cf"] == 0, "the boot sniff did not tick JOYSTICK.DRV on a "
              "machine with a game port and a stick at rest (SPEC.md 137.2)")
        f = probe(1)
        check(f["use_cf"] == 0, "OSAPI_DRV_USE answered CF=1 (DRVE %d) for a "
              "driver that is already up" % f["use_ax"])

        # ---- 2 INFO -----------------------------------------------------------
        say("info: axes=%02X type=%02X buttons=%d sticks=%d ver=%04X"
            % (f["f_ax"] & 255, f["f_ax"] >> 8, f["f_cx"] & 255, f["f_cx"] >> 8,
               f["f_dx"]))
        check(f["f_ax"] & 0x0F == 0x0F, "INFO's axes %02X: not all four pots "
              "answering with all four wired" % (f["f_ax"] & 255))
        check(f["f_ax"] >> 8 == 0 and f["f_cx"] == 0x0102,
              "INFO type/buttons/sticks = %02X/%d/%d, expected 2B, 2, 1"
              % (f["f_ax"] >> 8, f["f_cx"] & 255, f["f_cx"] >> 8))

        # ---- 3 centre and the dead zone ---------------------------------------
        f = probe(0)
        x, y = sb(f["a1_ax"]), sb(f["a1_ax"] >> 8)
        check(f["a1_cf"] == 0 and (x, y) == (0, 0) and f["a1_cx"] & 255 == 0,
              "centre answered CF=%d (%d,%d) dir %d, expected 0 (0,0) dir 0"
              % (f["a1_cf"], x, y, f["a1_cx"] & 255))
        joy(a=(0.04, -0.04))
        f = probe(0)
        check(sb(f["a1_ax"]) == 0 and sb(f["a1_ax"] >> 8) == 0,
              "4%% off centre answered (%d,%d) - inside the 8%% dead zone"
              % (sb(f["a1_ax"]), sb(f["a1_ax"] >> 8)))

        # ---- 4 uncalibrated: sign, direction, magnitude ------------------------
        for name, pos, ax, sign, bit in DEFL:
            joy(a=pos)
            f = probe(0)
            v = sb(f["a1_ax"] >> (0 if ax == "x" else 8))
            o = sb(f["a1_ax"] >> (8 if ax == "x" else 0))
            say("uncalibrated %-5s: %4d (other %d) dir=%d"
                % (name, v, o, f["a1_cx"] & 255))
            check(sign * v >= 118 and abs(v) <= 127,
                  "uncalibrated full %s answered %d, expected past %d"
                  % (name, v, 118 * sign))
            check(o == 0, "full %s moved the other axis to %d" % (name, o))
            check(f["a1_cx"] & 255 == bit, "full %s answered direction %d, "
                  "expected %d" % (name, f["a1_cx"] & 255, bit))

        # ---- 6 raw counts: the model's span -------------------------------------
        raws = []
        for oa in (0, 50000, 100000):
            joy(ohms_a=(oa, oa), ohms_b=(oa, oa))
            f = probe(0)
            raws.append((f["r_x1"], f["r_y1"]))
        span = [raws[2][i] - raws[0][i] for i in (0, 1)]
        model = 100000 * PER_OHM_US / UNIT_US
        say("raw 0/50K/100K ohm on A: %r; span %r vs the model's %.0f"
            % (raws, span, model))
        for i, n in enumerate(("X1", "Y1")):
            check(abs(span[i] - model) <= 100, "raw %s's 0-to-100K span is %d "
                  "units, the model says %.0f" % (n, span[i], model))
            check(raws[0][i] < raws[1][i] < raws[2][i],
                  "raw %s does not rise with the resistance: %r"
                  % (n, [r[i] for r in raws]))
        check(f["r_x2"] == 0xFFFF and f["r_y2"] == 0xFFFF,
              "stick B's axes were timed on a one-stick type: %r"
              % [f["r_x2"], f["r_y2"]])
        centre = raws[1]
        lo, hi = raws[0], raws[2]

        # ---- 5 calibrated, the way the page does it -----------------------------
        row = ui.sym("joy_row")
        dseg = struct.unpack("<H", m.read(row + 2, 2))[0]
        dz = m.read((dseg << 4) + dsym["joy_blob"] + JB_DZ, 1)[0]
        for ax in (0, 1):
            cen, mn, mx = centre[ax], lo[ax], hi[ax]
            w = struct.pack("<5H", mn, cen, mx, (cen - mn) * dz // 100,
                            (mx - cen) * dz // 100)
            m.write((dseg << 4) + dsym["joy_cal"] + ax * JCAL["SZ"], w)
        joy(a=(0, 0), b=(0, 0))
        f = probe(0)
        check(sb(f["a1_ax"]) == 0 and sb(f["a1_ax"] >> 8) == 0,
              "calibrated centre answered (%d,%d)" % (sb(f["a1_ax"]),
                                                      sb(f["a1_ax"] >> 8)))
        for name, pos, ax, sign, bit in DEFL:
            joy(a=pos)
            f = probe(0)
            v = sb(f["a1_ax"] >> (0 if ax == "x" else 8))
            say("calibrated   %-5s: %4d dir=%d" % (name, v, f["a1_cx"] & 255))
            check(sign * v >= 120, "calibrated full %s answered %d, expected "
                  "near %d" % (name, v, 127 * sign))
            check(f["a1_cx"] & 255 == bit, "calibrated full %s: direction %d, "
                  "expected %d" % (name, f["a1_cx"] & 255, bit))
        joy(a=(0.5, 0))
        f = probe(0)
        v = sb(f["a1_ax"])
        say("calibrated half right: %d" % v)
        check(45 <= v <= 70, "half travel answered %d, expected about 58" % v)
        joy(a=(0.8, -0.8))
        f = probe(0)
        st = f["st"]
        check(st[ST["DIRA"]] == R | D, "diagonal answered direction %d, "
              "expected %d" % (st[ST["DIRA"]], R | D))
        check(st[ST["AXES"]] & 0x0F == 0x0F, "STATE's answering mask %X"
              % st[ST["AXES"]])
        check(f["b_cf"] == 1, "stick B answered on a one-stick type")
        check(abs(sb(st[ST["X1"]]) - sb(f["a1_ax"])) <= 3 and
              abs(sb(st[ST["Y1"]]) - sb(f["a1_ax"] >> 8)) <= 3,
              "JOYV_STATE (%d,%d) disagrees with JOYV_READ (%d,%d)"
              % (sb(st[ST["X1"]]), sb(st[ST["Y1"]]), sb(f["a1_ax"]),
                 sb(f["a1_ax"] >> 8)))

        # ---- 9 timing -----------------------------------------------------------
        # The least of three: a timer tick landing in the door (the part with
        # interrupts ON) adds a task switch, ~700 us - 830 units - to one
        # read in a few, and the cost of the verb is what is left without it.
        def cost(pos):
            joy(a=pos)
            rs = [probe(0) for _ in range(3)]
            return min(r["a1_t"] for r in rs), min(r["s_t"] for r in rs)

        (fast, _), (slow, slow_s) = cost((-1, 1)), cost((1, -1))
        say("read: fast stick %d units (%.0f us), slow %d (%.0f us); STATE %d"
            % (fast, fast * UNIT_US, slow, slow * UNIT_US, slow_s))
        check(slow - fast >= 900, "a slow stick's read was only %d units "
              "longer than a fast one's (physics: ~1,300): the read is not "
              "timing the fall" % (slow - fast))
        for what, t in (("fast read", fast), ("slow read", slow),
                        ("JOYV_STATE", slow_s)):
            check(t <= byte_units, "%s took %d units (%.2f ms), longer than "
                  "the 4.17 ms a 2400-baud byte takes (JOY_TMO is %d)"
                  % (what, t, t * UNIT_US / 1000, jtmo))

        # ---- 7 presses ------------------------------------------------------------
        joy(a=(0, 0), b=(0, 0), buttons=0)
        probe(0)
        joy(buttons=0b0001)
        f = probe(0)
        say("press 1: read 1 held=%X press=%X, read 2 held=%X press=%X"
            % (f["a1_bx"] & 255, f["a1_bx"] >> 8, f["a2_bx"] & 255,
               f["a2_bx"] >> 8))
        check(f["a1_bx"] & 255 == 1 and f["a1_bx"] >> 8 == 1,
              "first read after a press: held/pressed = %X/%X, expected 1/1"
              % (f["a1_bx"] & 255, f["a1_bx"] >> 8))
        check(f["a2_bx"] & 255 == 1 and f["a2_bx"] >> 8 == 0,
              "a button still held was counted again: %X/%X"
              % (f["a2_bx"] & 255, f["a2_bx"] >> 8))
        joy(buttons=0)
        f = probe(0)
        check(f["a1_bx"] == 0, "release answered %04X" % f["a1_bx"])
        joy(buttons=0b0010)
        f = probe(0)
        check(f["a1_bx"] >> 8 == 2 and f["a1_bx"] & 255 == 2,
              "button 2's press answered held/pressed %X/%X, expected 2/2"
              % (f["a1_bx"] & 255, f["a1_bx"] >> 8))
        joy(buttons=0)
        probe(0)
        joy(buttons=0b0001)
        f = probe(0)
        check(f["a1_bx"] >> 8 == 1, "a second press after a release did not "
              "count: presses %X" % (f["a1_bx"] >> 8))
        joy(buttons=0b0100)                      # button 3 = stick B's first
        f = probe(0)
        check(f["r_bt"] == 0b0100, "JOYV_RAW's buttons %X with button 3 down"
              % f["r_bt"])
        check(f["st"][ST["BTN"]] & 0x0F == 0, "a 2-button type's JOYS_BTN "
              "shows button 3 (%X)" % f["st"][ST["BTN"]])
        if a.shot:
            sw, sh, srows = m.vram("cga")
            os88marty.write_png(a.shot, sw, sh, srows)

    # ====================== LEG 2: no game port at all =======================
    with session(os88marty.machine("os8088_5150_cga")) as ui:
        m = ui.m
        ui.still(0)
        gate = Gate(ui, img)
        f = gate.probe(0)
        check(f["f_cf"] == 1, "joy_find answered CF=0 with JOYSTICK.DRV not "
              "ticked on a machine with no game port - the door opened onto "
              "nothing")
        f = gate.probe(1)
        check(f["use_cf"] == 0, "OSAPI_DRV_USE could not bring JOYSTICK.DRV up "
              "with no game port (DRVE %d): it must attach and say no stick "
              "(SPEC.md 137.2)" % f["use_ax"])
        say("no port: find cf=%d axes=%02X; read A cf=%d, B cf=%d, read %d units"
            % (f["f_cf"], f["f_ax"] & 255, f["a1_cf"], f["b_cf"], f["a1_t"]))
        check(f["f_cf"] == 0 and f["f_ax"] & 0x0F == 0,
              "INFO with no game port: cf=%d axes=%02X, expected found, 0"
              % (f["f_cf"], f["f_ax"] & 255))
        check(f["a1_cf"] == 1 and f["b_cf"] == 1,
              "a stick answered with no game port: A cf=%d B cf=%d"
              % (f["a1_cf"], f["b_cf"]))
        check(f["a1_t"] <= byte_units, "a read with nothing plugged in took %d "
              "units (%.2f ms)" % (f["a1_t"], f["a1_t"] * UNIT_US / 1000))

    for f in fails:
        say("FAIL: " + f)
    say("joystick: %s" % ("FAILED" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
