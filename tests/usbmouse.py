#!/usr/bin/env python3
"""The CH375 USB mouse enumerates, feeds, clicks, unplugs and detaches
(SPEC.md 9.12.6).

    make usbmousetest && python3 tests/usbmouse.py

NO EMULATOR IN THIS TREE CARRIES A CH375, so the disks `make usbmousetest`
builds carry USBMOUSE.DRV assembled with -DCH375SIM: the same driver with a
model of the chip and one device under its four port primitives
(drivers/usbmouse/ch375sim.inc). The model has a MAILBOX, found by its
'CH375SIM' magic inside the driver's own segment, and this drives it: plug a
device, post a report, pull the plug. Everything above the four primitives -
attach, the worker, enumeration, the descriptor walk, the toggle, the
remainder-carrying halving, OSAPI_MOUSE_FEED and the contest it settles - is
the shipped code, on MartyPC's 8088.

WHAT THE MODEL CANNOT SAY is whether the datasheet was read right: a
misreading the driver and the model share passes here. SPEC.md 9.12.4 says so.

THE LEGS, on build/usbmsim.img (nothing plugged at power-on):

  boot      the row is loaded with no error, a worker is alive, and the
            contest is NOT settled - a CH375 with nothing in it has proven no
            pointer and must not retire the serial port
  plug      a low-speed boot mouse: the worker reaches US_RUN, INT# is proven
            readable (UI_GOOD), one SET_CONFIGURATION, SET_PROTOCOL(boot)
  move      five reports of (+20, -6) move the pointer (+50, -15): halved.
            Then four of +1 move it +2 and two of -3 move it -3 - the carried
            remainder, which a plain shift would lose; mou_port is MOU_FEEDROW
  click     the pointer steered onto the menu bar by reports alone, a press
            opens a pull-down and a release closes it - so reports keep
            arriving while the kernel's menu tracker spins
  unplug    with the button HELD: mouse_btn falls to 0 (the driver fed the
            release) and the worker goes back to US_IDLE
  disk      a full-speed flash drive: US_OTHER, and no SET_CONFIGURATION
            sent to it - found by the descriptor walk, after the low-speed
            attempt fails and the full-speed one succeeds
  poll      INT# NOT WIRED (the model's nostat) and the driver back in
            UI_PROBE: a mouse plugged now must be read in poll mode
            (UI_POLL), and its reports must still move the pointer
  detach    a Restart: drv_shutdown detaches, the worker releases, aborts
            and RESET_ALLs the chip, and exits - read at dsk_rb_go, the last
            instruction before int 19h

and on build/usbmbusy.img (a flash drive the BIOS already configured):

  busy      attach refuses with DRVE_BUSY and the image is handed back

Each leg's failure CAN be produced: stub OSAPI_MOUSE_FEED's body to `stc`
(move), drop the halving's carry (move), skip um_release (unplug), accept any
interface AND any endpoint type in um_parse (disk - either check alone still
refuses a mass-storage device, whose endpoints are bulk), never fall back to
UI_POLL in um_wait (poll), leave um_reset out of the worker's exit (detach), or
skip the READY check in um_attach (busy).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty                                            # noqa: E402
import os88sym                                              # noqa: E402

S = os88sym.linear

MACHINE = "os8088_5150_cga_gla"
SIM_IMG = "build/usbmsim.img"
BUSY_IMG = "build/usbmbusy.img"
APPS = "build/apps360.img"

UI_RBQ_NOFLUSH = 2              # kernel/ui.inc
MOU_FEEDROW = 8                 # kernel/mouse.inc
DRVE_BUSY = 6                   # kernel/driver.inc
DRVR_SEG, DRVR_ERR = 2, 14

US_IDLE, US_ENUM, US_RUN, US_OTHER = 0, 1, 2, 3     # usbmouse.asm
UI_POLL, UI_GOOD, UI_PROBE = 0, 1, 2

# the mailbox, from the magic (ch375sim.inc)
MB_PLUG, MB_NOSTAT, MB_REPN, MB_REP = 8, 9, 10, 11
MB_CONFIGS, MB_PROTO, MB_RESETS, MB_BUSRST = 16, 17, 18, 19
MB_STATE, MB_INTOK, MB_BTN = 20, 22, 24
K_NONE, K_MOUSE, K_DISK = 0, 1, 2


def say(*a):
    print(*a)
    sys.stdout.flush()


class Rig(object):
    """The driver's row, its segment, and the model's mailbox in it."""

    def __init__(self, m):
        self.m = m
        self.row = S("usbm_row")
        self.lost = 0           # reports no IN token ever took

    def rowbytes(self):
        return self.m.read(self.row, 16)

    def find(self):
        r = self.rowbytes()
        self.seg = int.from_bytes(r[DRVR_SEG:DRVR_SEG + 2], "little")
        if not self.seg:
            return False
        img = self.m.read(self.seg * 16, 8192)
        off = img.find(b"CH375SIM")
        if off < 0:
            sys.exit("usbmouse: USBMOUSE.DRV is loaded but has no CH375SIM "
                     "mailbox - this is the SHIPPED driver, not the gate "
                     "build. `make usbmousetest` builds the disks this row "
                     "boots")
        self.box = self.seg * 16 + off

        def var(i):
            return self.seg * 16 + int.from_bytes(
                self.m.read(self.box + i, 2), "little")
        self.a_state, self.a_intok, self.a_btn = (var(MB_STATE),
                                                  var(MB_INTOK), var(MB_BTN))
        return True

    def mb(self, i):
        return self.m.read(self.box + i, 1)[0]

    def put(self, i, val):
        self.m.write(self.box + i, bytes([val & 0xFF]))

    def state(self):
        return self.m.read(self.a_state, 1)[0]

    def intok(self):
        return self.m.read(self.a_intok, 1)[0]

    def post(self, btn, dx, dy):
        """One report through the model; returns once the guest has fed it."""
        self.m.write(self.box + MB_REP,
                     bytes([btn & 0xFF, dx & 0xFF, dy & 0xFF, 0]))
        self.put(MB_REPN, 1)
        if not until(self.m, lambda: self.mb(MB_REPN) == 0,
                     "the model to hand the report to an IN token", 20.0,
                     required=False):
            self.put(MB_REPN, 0)
            self.lost += 1
            return False
        # delivered: the feed follows within the same worker pass
        until(self.m, lambda: self.m.read(self.a_btn, 1)[0] == (btn & 3)
              or (btn & 3) == self.m.read(S("mouse_btn"), 1)[0],
              "the report to reach the kernel", 5.0, required=False)
        os88marty.pace(self.m, 0.15)
        return True


def until(m, cond, what, limit=30.0, required=True):
    """os88marty.until with a bool answer: `limit` becomes a GUEST budget, so
    a loaded box cannot cut a wait short and fail it as the driver's."""
    try:
        os88marty.until(m, lambda _: cond(), what, poll=0.05, limit=limit)
        return True
    except os88marty.MartyError as e:
        if required:
            raise AssertionError("timed out waiting for %s: %s" % (what, e))
        return False


def pos(m):
    return (int.from_bytes(m.read(S("mouse_x"), 2), "little"),
            int.from_bytes(m.read(S("mouse_y"), 2), "little"))


def steer(rig, tx, ty, btn=0):
    """Put the pointer on (tx, ty) with reports alone. Each report moves
    at most 60 px (120 counts, halved); the halving's carry can leave it one
    short, so it iterates on the position the kernel reports."""
    for _ in range(40):
        x, y = pos(rig.m)
        dx, dy = tx - x, ty - y
        if dx == 0 and dy == 0:
            return True
        cx = max(-60, min(60, dx)) * 2
        cy = max(-60, min(60, dy)) * 2
        if cx == 0 and dx:
            cx = 1 if dx > 0 else -1
        if cy == 0 and dy:
            cy = 1 if dy > 0 else -1
        rig.post(btn, cx, cy)
    return pos(rig.m) == (tx, ty)


def leg_sim(fail):
    with os88marty.launch(SIM_IMG, apps=APPS, machine=MACHINE) as m:
        os88marty.no_saver(m)
        rig = Rig(m)
        r = rig.rowbytes()

        # --- boot --------------------------------------------------------
        if not rig.find():
            fail.append("boot: USBMOUSE.DRV is not loaded (row error %d). "
                        "SYSTEM.CFG bit 6 is set on this disk, so the row "
                        "was wanted and attach refused it" % r[DRVR_ERR])
            return
        if r[DRVR_ERR]:
            fail.append("boot: the row carries error %d" % r[DRVR_ERR])
        if m.read(S("mou_port"), 1)[0] == MOU_FEEDROW:
            fail.append("boot: mou_port is MOU_FEEDROW before any report - "
                        "the contest was settled by a CH375 with nothing "
                        "plugged in")
        say("boot     loaded at %04X, state %d, intok %d, mou_port %d"
            % (rig.seg, rig.state(), rig.intok(),
               m.read(S("mou_port"), 1)[0]))

        # --- plug ----------------------------------------------------------
        rig.put(MB_PLUG, K_MOUSE)
        if not until(m, lambda: rig.state() == US_RUN, "US_RUN", 60.0,
                     required=False):
            fail.append("plug: the worker never reached US_RUN (state %d, "
                        "bus resets %d, configs %d)" % (rig.state(),
                        rig.mb(MB_BUSRST), rig.mb(MB_CONFIGS)))
            return
        say("plug     US_RUN: intok %d, configs %d, proto %d, bus resets %d"
            % (rig.intok(), rig.mb(MB_CONFIGS), rig.mb(MB_PROTO),
               rig.mb(MB_BUSRST)))
        if rig.intok() != UI_GOOD:
            fail.append("plug: INT# reads in bit 7 on this model and the "
                        "driver did not prove it (intok %d)" % rig.intok())
        if rig.mb(MB_CONFIGS) != 1:
            fail.append("plug: %d SET_CONFIGURATIONs, want 1"
                        % rig.mb(MB_CONFIGS))
        if rig.mb(MB_PROTO) != 0:
            fail.append("plug: SET_PROTOCOL(boot) was not sent (model "
                        "recorded %02X)" % rig.mb(MB_PROTO))

        # --- move ----------------------------------------------------------
        x0, y0 = pos(m)
        for _ in range(5):
            rig.post(0, 20, -6)
        x1, y1 = pos(m)
        say("move     5 x (+20,-6) -> (%+d,%+d)" % (x1 - x0, y1 - y0))
        if (x1 - x0, y1 - y0) != (50, -15):
            fail.append("move: five reports of (+20,-6) moved the pointer "
                        "(%+d,%+d), want (+50,-15) - each delta halved"
                        % (x1 - x0, y1 - y0))
        for _ in range(4):
            rig.post(0, 1, 0)
        x2, _ = pos(m)
        for _ in range(2):
            rig.post(0, -3, 0)
        x3, _ = pos(m)
        say("carry    4 x (+1) -> %+d, then 2 x (-3) -> %+d"
            % (x2 - x1, x3 - x2))
        if x2 - x1 != 2 or x3 - x2 != -3:
            fail.append("move: the halving's remainder is not carried - four "
                        "+1s moved %+d (want +2) and two -3s moved %+d "
                        "(want -3)" % (x2 - x1, x3 - x2))
        if m.read(S("mou_port"), 1)[0] != MOU_FEEDROW:
            fail.append("move: mou_port is %d, want MOU_FEEDROW (8) once a "
                        "report is accepted" % m.read(S("mou_port"), 1)[0])

        # --- click ---------------------------------------------------------
        steer(rig, 0, 0)
        if not steer(rig, 20, 4):
            fail.append("click: reports could not put the pointer on the "
                        "menu bar (it is at %r)" % (pos(m),))
        rig.post(1, 0, 0)
        opened = until(m, lambda: m.read(S("menu_dropd"), 1)[0] != 0,
                       "a pull-down", 10.0, required=False)
        rig.post(0, 0, 0)
        if not opened:
            fail.append("click: a press on the menu bar opened no pull-down")
        else:
            # off the menu entirely, then a click there: whether a release on
            # the title leaves the menu down or not, this closes it
            steer(rig, 300, 150)
            if m.read(S("menu_dropd"), 1)[0]:
                rig.post(1, 0, 0)
                rig.post(0, 0, 0)
            closed = until(m, lambda: m.read(S("menu_dropd"), 1)[0] == 0,
                           "the pull-down to close", 10.0, required=False)
            if not closed:
                fail.append("click: the pull-down never closed - a release "
                            "did not reach the menu tracker")
        say("click    pull-down opened %s, mouse_btn %d"
            % (opened, m.read(S("mouse_btn"), 1)[0]))
        os88marty.settle(m)

        # --- unplug with the button held -----------------------------------
        rig.post(1, 0, 0)
        held = m.read(S("mouse_btn"), 1)[0]
        rig.put(MB_PLUG, K_NONE)
        idle = until(m, lambda: rig.state() == US_IDLE, "US_IDLE", 20.0,
                     required=False)
        released = until(m, lambda: m.read(S("mouse_btn"), 1)[0] == 0,
                         "the release", 5.0, required=False)
        say("unplug   held %d -> mouse_btn %d, state %d"
            % (held, m.read(S("mouse_btn"), 1)[0], rig.state()))
        if held != 1:
            fail.append("unplug: SETUP - the press did not register "
                        "(mouse_btn %d)" % held)
        if not released:
            fail.append("unplug: mouse_btn is still %d after the device "
                        "went away - a drag that never ends"
                        % m.read(S("mouse_btn"), 1)[0])
        if not idle:
            fail.append("unplug: the worker stayed in state %d" % rig.state())
        os88marty.settle(m)

        # --- a flash drive -------------------------------------------------
        cfg0 = rig.mb(MB_CONFIGS)
        rig.put(MB_PLUG, K_DISK)
        other = until(m, lambda: rig.state() == US_OTHER, "US_OTHER", 60.0,
                      required=False)
        say("disk     state %d, configs %d -> %d" % (rig.state(), cfg0,
                                                     rig.mb(MB_CONFIGS)))
        if not other:
            fail.append("disk: a mass-storage device left the worker in "
                        "state %d, want US_OTHER" % rig.state())
        if rig.mb(MB_CONFIGS) != cfg0:
            fail.append("disk: the driver CONFIGURED a flash drive")
        rig.put(MB_PLUG, K_NONE)
        until(m, lambda: rig.state() == US_IDLE, "US_IDLE after the disk",
              20.0, required=False)

        # --- poll mode -----------------------------------------------------
        rig.put(MB_NOSTAT, 1)
        m.write(rig.a_intok, bytes([UI_PROBE]))
        rig.put(MB_PLUG, K_MOUSE)
        ran = until(m, lambda: rig.state() == US_RUN, "US_RUN in poll mode",
                    90.0, required=False)
        say("poll     state %d, intok %d" % (rig.state(), rig.intok()))
        if not ran:
            fail.append("poll: with INT# dead the worker never reached "
                        "US_RUN (state %d)" % rig.state())
        else:
            if rig.intok() != UI_POLL:
                fail.append("poll: INT# never moved and the driver did not "
                            "fall back to UI_POLL (intok %d)" % rig.intok())
            xa, _ = pos(m)
            took = sum(1 for _ in range(3) if rig.post(0, -10, 0))
            xb, _ = pos(m)
            if took != 3:
                fail.append("poll: %d of 3 reports were never read - the "
                            "worker is waiting on an INT# that never comes"
                            % (3 - took))
            say("poll     3 x (-10) -> %+d" % (xb - xa))
            if xb - xa != -15:
                fail.append("poll: three reports of -10 in poll mode moved "
                            "%+d, want -15" % (xb - xa))

        # --- detach, by way of a Restart -------------------------------------
        resets0 = rig.mb(MB_RESETS)
        m.bp_exec(S("dsk_rb_go"))
        m.write(S("ui_rebootq"), bytes([UI_RBQ_NOFLUSH]))
        m.run()
        if not m.wait_stop(60.0):
            fail.append("detach: the Restart never reached dsk_rb_go")
            return
        img = m.read(rig.seg * 16, 8192)
        off = img.find(b"CH375SIM")
        resets = img[off + MB_RESETS] if off >= 0 else None
        say("detach   RESET_ALLs %d -> %s, drv_wcnt %d"
            % (resets0, resets, m.read(S("drv_wcnt"), 1)[0]))
        if m.read(S("drv_wcnt"), 1)[0] != 0:
            fail.append("detach: a driver worker is still counted at int 19h "
                        "- the worker did not exit on DRVV_DETACH")
        if rig.lost and not any(f.startswith("poll:") for f in fail):
            fail.append("%d report(s) were never taken by an IN token"
                        % rig.lost)
        if resets is None or resets <= resets0:
            fail.append("detach: the chip was not reset on the way out "
                        "(RESET_ALLs %s, was %d)" % (resets, resets0))


def leg_busy(fail):
    with os88marty.launch(BUSY_IMG, apps=APPS, machine=MACHINE) as m:
        r = m.read(S("usbm_row"), 16)
        seg = int.from_bytes(r[DRVR_SEG:DRVR_SEG + 2], "little")
        say("busy     row error %d, segment %04X" % (r[DRVR_ERR], seg))
        if r[DRVR_ERR] != DRVE_BUSY:
            fail.append("busy: a flash drive the BIOS configured was not "
                        "refused with DRVE_BUSY (row error %d) - attach would "
                        "take the bus from under a boot disk" % r[DRVR_ERR])
        if seg:
            fail.append("busy: the refused image was not handed back")


def main():
    os.chdir(ROOT)
    for img in (SIM_IMG, BUSY_IMG, APPS):
        if not os.path.exists(img):
            sys.exit("usbmouse: %s is missing - `make usbmousetest` first"
                     % img)
    fail = []
    leg_sim(fail)
    leg_busy(fail)
    if fail:
        say("\nusbmouse: FAIL")
        for f in fail:
            say("  - " + f)
        return 1
    say("\nusbmouse: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
