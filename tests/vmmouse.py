#!/usr/bin/env python3
"""The VMware absolute pointer wins the contest and tracks (SPEC.md 9.11).

    python3 tests/vmmouse.py

QEMU's `pc` machine carries a `vmport` and a `vmmouse` by default, so the
backdoor probe in VMMOUSE.DRV succeeds here exactly as it does under
v86 in the browser - which is the one place this feature actually runs. That
makes it the rare browser-only feature with a CI gate: nearly every other
`make test` recipe turns the port OFF (SPEC.md 9.11, the Makefile's `VMPORT`)
because tools/mouse.py drives the msserial mouse and the backdoor would
otherwise leave that device retired; this one turns it back ON and drives the
backdoor instead.

QEMU by name on CLAUDE.md's closed list - MartyPC has no backdoor of any
kind, so the attach refuses there and the serial path runs. Nothing here is a
time: the machine under it is not a 4.77 MHz 8088.

WHAT IT ASSERTS:

  cpu_tier   2 (CPU_386)   - vmm_boot_x refuses to READ the image below this,
                             which is the last CPU gate in the tree: there is
                             no 386 instruction in the kernel to guard past it
  vmm_on     1             - the image attached, the GETVERSION probe answered
                             and REQUEST_ABSOLUTE went through (SPEC.md
                             9.11.1). It is set LAST by vmm_boot_x, so it is
                             also the signal that the whole sequence finished
  mou_bases  0000 0000     - `-serial none`: no UART, so the serial contest
                             cannot even be entered and this is the backdoor
                             alone, like the browser
  mou_seen   1
  mou_port   6             - MOU_VMROW, past MOU_P2ROW - so mou_lockon retired
                             every serial row and the PS/2 aux port
  mou_line   FF            - MOU_P2LINE
  mou_ptr    1             - the machine HAS a pointer, so the keyboard mouse
                             stands down (SPEC.md 9.6.6)

then absolute positions injected through QEMU's vmmouse, each of which must
land within a few pixels of the requested one after two rounds of fixed-point
scaling (QEMU 0..0x7FFF -> vmmouse 0..0xFFFF -> `(v * [vid_w]) >> 16`). A sign
slip or a swapped axis moves the pointer somewhere else entirely, which is
what this catches that a boot-state read cannot.
"""
import json
import os
import signal
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
# tests/ FIRST and tools/ SECOND, so that tools/ ends up at index 0: there is a
# tests/heapmap.py as well as a tools/heapmap.py, and the wrong order shadows
# the one with Qmp in it.
sys.path.insert(0, os.path.join(ROOT, "tests"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88sym                                              # noqa: E402
import os88qemu                                             # noqa: E402

SOCK = os.path.join(ROOT, "build", "vmm.sock")
PIDFILE = os.path.join(ROOT, "build", "vmm.pid")

QEMU_ABS_MAX = 0x7FFF          # QEMU's INPUT_EVENT_ABS_MAX
TOL = 2                        # px. Two fixed-point scalings, each rounding


def kill_stale():
    if os.path.exists(PIDFILE):
        try:
            os.kill(int(open(PIDFILE).read().strip()), signal.SIGTERM)
            time.sleep(1)
        except Exception:
            pass
    for f in (SOCK, PIDFILE):
        if os.path.exists(f):
            os.unlink(f)


def build():
    subprocess.run(["make", "build/os8088.img", "build/apps.img"],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)


def launch():
    """vmport ON (the pc-machine default, spelled out) and NO serial mouse:
    the VMware backdoor is the only pointing device, like v86 in the browser.
    QEMU's vmmouse hangs off the i8042 the pc machine has anyway."""
    # The gate disk, built here rather than assumed: the row is builds=True
    # (tests/suite.py) precisely so it may rewrite build/, and a reader running
    # this script by hand should not have to know the target's name.
    subprocess.run(["make", "-s", "vmmousetest"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    subprocess.run(
        em + " -machine pc,vmport=on"
        # build/vmmouse.img and NOT os8088.img: VMMOUSE.DRV is DRVC_OVL with a
        # drv_tab row, so nothing loads it unless SYSTEM.CFG asks (SPEC.md
        # 51.3). `make vmmousetest` builds a disk whose settings file has the
        # driver's bit set, exactly as ether360.img does for the card - so the
        # pointer is up before the first paint and this reads state instead of
        # driving the Control Panel through a scripted mouse.
        " -drive file=build/vmmouse.img,format=raw,if=floppy -boot a"
        " -drive file=build/apps.img,format=raw,if=floppy,index=1"
        " -serial none"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (SOCK, PIDFILE), cwd=ROOT, shell=True, check=True)
    # -daemonize: the emulator outlives this script unless somebody kills it,
    # and the somebody is os88qemu. AT THE LAUNCH SITE and nowhere else, so a
    # script that drives an instance it did not start cannot kill it.
    os88qemu.own(PIDFILE, SOCK)


def qmp_raw(*events):
    """One QMP input-send-event, its own connection (heapmap.Qmp's note)."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SOCK)
    f = s.makefile("rw")
    try:
        f.readline()
        for o in ({"execute": "qmp_capabilities"},
                  {"execute": "input-send-event",
                   "arguments": {"events": list(events)}}):
            f.write(json.dumps(o) + "\n")
            f.flush()
            while True:
                line = f.readline()
                if not line:
                    raise RuntimeError("QMP closed")
                if "event" not in json.loads(line):
                    break
    finally:
        f.close()
        s.close()


def abs_to(px, py, w, h):
    """Put the host pointer on guest pixel (px, py) of a w x h screen.

    The screen size is passed rather than assumed: the caller reads [vid_w] and
    [vid_h] out of the guest, so this row means the same thing under VIDEO=cga
    and VIDEO=ega as it does on the VGA `make test` defaults to."""
    vx = round(px / (w - 1) * QEMU_ABS_MAX)
    vy = round(py / (h - 1) * QEMU_ABS_MAX)
    qmp_raw({"type": "abs", "data": {"axis": "x", "value": vx}},
            {"type": "abs", "data": {"axis": "y", "value": vy}})


def btn(down):
    qmp_raw({"type": "btn", "data": {"button": "left", "down": down}})


def byte(q, sym):
    return q.read(os88sym.linear(sym), 1)[0]


def word(q, sym):
    b = q.read(os88sym.linear(sym), 2)
    return b[0] | (b[1] << 8)


def main():
    kill_stale()
    build()
    fails = []
    try:
        launch()
        q = heapmap.Qmp(SOCK)
        for _ in range(200):
            try:
                q.hmp("info status")
                break
            except OSError:
                time.sleep(0.1)

        # vmm_boot_x runs inside kmain, after drv_boot_x has read the image
        # off the floppy; [vmm_on] settling is the signal, and it is written
        # last on purpose.
        t0, on = time.time(), 0
        while time.time() - t0 < 90:
            on = byte(q, "vmm_on")
            if on == 1:
                break
            time.sleep(0.25)

        tier = byte(q, "cpu_tier")
        if tier != 2:
            fails.append("cpu_tier %d, want 2 (CPU_386): QEMU should be a 386+ "
                         "and the island's gate keys on this" % tier)
        if on != 1:
            fails.append("vmm_on %d, want 1: the backdoor probe or the "
                         "REQUEST/ABSOLUTE handshake did not complete "
                         "(SPEC.md 9.11.1)" % on)
        bases = q.read(os88sym.linear("mou_bases"), 4)
        if any(bases):
            fails.append("mou_bases %s - a serial row probed present; this run "
                         "is meant to be the backdoor alone" % bases.hex())
        for sym, want in (("mou_seen", 1), ("mou_port", 6), ("mou_line", 0xFF),
                          ("mou_ptr", 1)):
            got = byte(q, sym)
            if got != want:
                fails.append("%s %02X, want %02X" % (sym, got, want))

        # --- absolute positions: the sign handling and the axis order ------
        #
        # THE SCREEN IS READ, NOT ASSUMED. It was hardcoded 640x480, which is
        # right for `make test` and wrong the moment anybody runs this row
        # against VIDEO=cga or VIDEO=ega - and wrong SILENTLY, because a
        # too-small target still lands inside a too-large screen.
        w, h = word(q, "vid_w"), word(q, "vid_h")
        if not fails and (w < 64 or h < 64):
            fails.append("vid_w/vid_h read back as %d x %d - the screen is "
                         "not up, so nothing below means anything" % (w, h))

        # THE EXTREMES ARE THE POINT. SPEC.md 9.11.3 claims `(v * vid_w) >> 16`
        # maps 0 to 0 and 0xFFFF to vid_w - 1 for every width, i.e. that the
        # top of the range can never address one past the edge - which is what
        # leaves mou_clamp nothing to catch. The interior points below cannot
        # see an off-by-one at either end; these two are the assertion.
        if not fails:
            for px, py in ((0, 0), (w - 1, h - 1),
                           (w // 2, h // 2), (40, 40), (w - 40, h - 30)):
                abs_to(px, py, w, h)
                time.sleep(0.4)
                gx, gy = word(q, "mouse_x"), word(q, "mouse_y")
                if abs(gx - px) > TOL or abs(gy - py) > TOL:
                    fails.append("pointer at %d,%d, want ~%d,%d (+-%d) - a miss "
                                 "this large is the sign handling or a swapped "
                                 "axis, not scaling" % (gx, gy, px, py, TOL))
                # ...and the edges must be ON the screen, whatever the
                # tolerance allows in the middle: one past either is a cursor
                # drawn off the framebuffer.
                if not (0 <= gx < w and 0 <= gy < h):
                    fails.append("pointer at %d,%d is OUTSIDE %d x %d - the "
                                 "scaling addressed one past the edge, which "
                                 "SPEC.md 9.11.3 says it cannot"
                                 % (gx, gy, w, h))

        # (The RELATIVE_PACKET path - "capture pointer" - is exercised by the
        #  v86 headless smoke in ../v86/examples/os8088-smoke.mjs; QEMU's
        #  vmmouse only sends relative packets in relative mode, which this
        #  driver never requests.)

        # --- press, move WHILE HELD, release: the freeze regression --------
        # vmmouse has no ISR, so a spin loop (a drag, a menu) that does not
        # pump the backdoor never sees the release. task_yield is what drains
        # it for every such loop (SPEC.md 9.11.3); if that stops working the
        # machine wedges here with [mouse_btn] stuck at 1.
        if not fails:
            t0 = word(q, "ticks")
            abs_to(200, 200, w, h)
            time.sleep(0.3)
            btn(True)
            time.sleep(0.2)
            if byte(q, "mouse_btn") != 1:
                fails.append("press not seen: mouse_btn 0 after btn-down")
            for yy in range(200, 280, 8):        # drag it
                abs_to(200, yy, w, h)
                time.sleep(0.05)
            gy = word(q, "mouse_y")
            if gy < 250:
                fails.append("pointer did not track while held: y=%d, want "
                             ">=250 - the drag-loop poll (task_yield) stalled" % gy)
            btn(False)
            time.sleep(0.3)
            if byte(q, "mouse_btn") != 0:
                fails.append("RELEASE NEVER ARRIVED: mouse_btn stuck at 1 - a "
                             "spin loop that does not pump vmmouse (SPEC.md "
                             "9.11.3), the freeze this test exists for")
            t1 = word(q, "ticks")
            if (t1 - t0) & 0xFFFF < 10:
                fails.append("clock barely moved (%d ticks in ~1.5s) - the "
                             "machine is wedged" % ((t1 - t0) & 0xFFFF))

        # --- the keyboard survives vmm_p2wake's 8042 poke -----------------
        # the driver sends AUX 0xF4/0xF5 through the 8042 (SPEC.md 9.11.2) so v86
        # starts delivering mouse events. A stray aux ack left in the output
        # buffer would be read as a scancode by int 09h. Six keys must advance
        # the BIOS keyboard buffer tail (0040:001C) by exactly twelve bytes -
        # ps2mouse.py's check, same reason.
        if not fails:
            KBHEAD = 0x41A                     # 0040:001A: head, then tail
            before = q.read(KBHEAD, 4)
            tail0 = before[2] | (before[3] << 8)
            for k in "abcdef":
                q.hmp("sendkey " + k)
            time.sleep(1.0)
            after = q.read(KBHEAD, 4)
            tail1 = after[2] | (after[3] << 8)
            if (tail1 - tail0) & 0xFFFF != 12:
                fails.append("BIOS keyboard tail %04X -> %04X, want +12 for six "
                             "keys: vmm_p2wake left a byte in the 8042 that "
                             "int 09h ate (SPEC.md 9.11.2)" % (tail0, tail1))

        q.hmp("quit")
    finally:
        kill_stale()

    if fails:
        for f in fails:
            print("vmmouse: FAIL " + f)
        return 1
    print("vmmouse: ok - vmm_on 1, port 06, line FF, ptr 1; pointer tracks "
          "absolute within %d px; press/drag/release clean, no freeze; "
          "keyboard intact" % TOL)
    return 0


if __name__ == "__main__":
    sys.exit(main())
