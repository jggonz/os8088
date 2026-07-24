#!/usr/bin/env python3
"""Tiny QMP client: send HMP monitor commands to a running QEMU.

    python3 tools/qmp.py <socket> <hmp command> [<hmp command> ...]

Each argument is one HMP command (quote it). Prints each command's reply.
Used by the test flow to drive the emulated serial mouse (mouse_move,
mouse_button), the keyboard (sendkey) and to take screendumps.
"""
import json
import socket
import sys
import time


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

    for cmd in cmds:
        if cmd.startswith("sleep "):  # convenience: pacing between inputs
            time.sleep(float(cmd.split()[1]))
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
