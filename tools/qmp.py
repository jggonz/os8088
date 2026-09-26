#!/usr/bin/env python3
"""Tiny QMP client: send HMP monitor commands to a running QEMU.

    python3 tools/qmp.py <socket> <hmp command> [<hmp command> ...]

Each argument is one HMP command (quote it). Prints each command's reply.
Used by the test flow to drive the emulated serial mouse (mouse_move,
mouse_button), the keyboard (sendkey) and to take screendumps.

Two pacing directives, which are not HMP:

    sleep S     S HOST seconds - for QEMU's own devices, which run on its
                virtual clock, and that follows the host's without -icount
                (the msmouse line's baud rate is one)
    gsleep S    at least S seconds of the GUEST's clock: the BIOS tick count
                at 0040:006C, which only guest code advances - so a loaded
                box cannot shorten it. A gesture's spacing wants this one
"""
import json
import re
import socket
import sys
import time

TICK_HZ = 18.2065
STALL = 30.0            # host seconds of a tick count that does not move


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    path, cmds = sys.argv[1], sys.argv[2:]

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(path)
    f = s.makefile("rw")

    def send(obj):
        f.write(json.dumps(obj) + "\n")
        f.flush()
        while True:
            line = f.readline()
            if not line:
                raise RuntimeError("QMP connection closed")
            msg = json.loads(line)
            if "event" in msg:        # async events arrive interleaved; skip
                continue
            return msg

    json.loads(f.readline())          # greeting
    send({"execute": "qmp_capabilities"})

    def ticks():
        r = send({"execute": "human-monitor-command",
                  "arguments": {"command-line": "xp /1hx 0x46c"}})
        m = re.search(r":\s*0x([0-9a-fA-F]+)", r.get("return", ""))
        if not m:
            raise RuntimeError("xp answered %r" % r)
        return int(m.group(1), 16)

    def gsleep(secs):
        # Whole ticks, rounded UP and one more for the phase, so it is never
        # less than `secs`; a clock that stops ends it, as a sleep would.
        need = int(secs * TICK_HZ + 0.999) + 1
        last, n, seen = ticks(), 0, time.monotonic()
        while n < need and time.monotonic() - seen < STALL:
            time.sleep(0.01)
            t = ticks()
            d = (t - last) & 0xFFFF
            if d:
                n += 1 if d > 256 else d    # SET (POST, midnight), not run
                last, seen = t, time.monotonic()

    for cmd in cmds:
        if cmd.startswith("sleep "):  # convenience: pacing between inputs
            time.sleep(float(cmd.split()[1]))
            continue
        if cmd.startswith("gsleep "):
            gsleep(float(cmd.split()[1]))
            continue
        r = send({"execute": "human-monitor-command",
                  "arguments": {"command-line": cmd}})
        out = r.get("return", r)
        if isinstance(out, str) and out.strip():
            print(out.strip())
        elif "error" in r:
            print(f"error: {r['error']}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
