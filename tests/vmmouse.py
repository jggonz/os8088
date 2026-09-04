#!/usr/bin/env python3
"""The VMware absolute pointer wins the contest and tracks (SPEC.md 9.10).

    python3 tests/vmmouse.py

QEMU's `pc` machine carries a `vmport` and a `vmmouse` by default, so the
kernel's backdoor probe (`vmm_init`) succeeds here exactly as it does under
v86 in the browser - which is the one place this feature actually runs. That
makes it the rare browser-only feature with a CI gate: nearly every other
`make test` recipe turns the port OFF (SPEC.md 9.10, the Makefile's `VMPORT`)
because tools/mouse.py drives the msserial mouse and the backdoor would
otherwise leave that device retired; this one turns it back ON and drives the
backdoor instead.

QEMU by name on CLAUDE.md's closed list - MartyPC has no backdoor of any
kind, so `vmm_init` fails there and the serial path runs. Nothing here is a
time: the machine under it is not a 4.77 MHz 8088.

WHAT IT ASSERTS:

  cpu_tier   2 (CPU_386)   - the island's run-time gate would refuse a lower
                             tier; QEMU is a 386+, so this is a sanity check
  vmm_on     1             - the GETVERSION probe answered and REQUEST /
                             ABSOLUTE went through (SPEC.md 9.10.1)
  mou_bases  0000 0000     - `-serial none`: no UART, so the serial contest
                             cannot even be entered and this is the backdoor
                             alone, like the browser
  mou_seen   1
  mou_port   6             - VMM_ROW, past MOU_P2ROW - so mou_lockon retired
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
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88sym                                              # noqa: E402

SOCK = os.path.join(ROOT, "build", "vmm.sock")
PIDFILE = os.path.join(ROOT, "build", "vmm.pid")

QEMU_ABS_MAX = 0x7FFF          # QEMU's INPUT_EVENT_ABS_MAX
SCREEN_W, SCREEN_H = 640, 480  # `make test` default: VGA
TOL = 4                        # px: two fixed-point scalings, each rounding


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
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    subprocess.run(
        em + " -machine pc,vmport=on"
        " -drive file=build/os8088.img,format=raw,if=floppy -boot a"
        " -drive file=build/apps.img,format=raw,if=floppy,index=1"
        " -serial none"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (SOCK, PIDFILE), cwd=ROOT, shell=True, check=True)


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


def abs_to(px, py):
    vx = round(px / (SCREEN_W - 1) * QEMU_ABS_MAX)
    vy = round(py / (SCREEN_H - 1) * QEMU_ABS_MAX)
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

        # vmm_init runs inside kmain; [vmm_on] settling is the signal.
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
                         "(SPEC.md 9.10.1)" % on)
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
        if not fails:
            for px, py in ((400, 300), (40, 40), (600, 450), (320, 240)):
                abs_to(px, py)
                time.sleep(0.4)
                gx, gy = word(q, "mouse_x"), word(q, "mouse_y")
                if abs(gx - px) > TOL or abs(gy - py) > TOL:
                    fails.append("pointer at %d,%d, want ~%d,%d (+-%d) - a miss "
                                 "this large is the sign handling or a swapped "
                                 "axis, not scaling" % (gx, gy, px, py, TOL))

        # --- press, move WHILE HELD, release: the freeze regression --------
        # vmmouse has no ISR, so a spin loop (a drag, a menu) that does not
        # pump the backdoor never sees the release. task_yield is what drains
        # it for every such loop (SPEC.md 9.10.3); if that stops working the
        # machine wedges here with [mouse_btn] stuck at 1.
        if not fails:
            t0 = word(q, "ticks")
            abs_to(200, 200)
            time.sleep(0.3)
            btn(True)
            time.sleep(0.2)
            if byte(q, "mouse_btn") != 1:
                fails.append("press not seen: mouse_btn 0 after btn-down")
            for yy in range(200, 280, 8):        # drag it
                abs_to(200, yy)
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
                             "9.10.3), the freeze this test exists for")
            t1 = word(q, "ticks")
            if (t1 - t0) & 0xFFFF < 10:
                fails.append("clock barely moved (%d ticks in ~1.5s) - the "
                             "machine is wedged" % ((t1 - t0) & 0xFFFF))

        q.hmp("quit")
    finally:
        kill_stale()

    if fails:
        for f in fails:
            print("vmmouse: FAIL " + f)
        return 1
    print("vmmouse: ok - vmm_on 1, port 06, line FF, ptr 1; pointer tracks "
          "absolute within %d px; press/drag/release clean, no freeze" % TOL)
    return 0


if __name__ == "__main__":
    sys.exit(main())
