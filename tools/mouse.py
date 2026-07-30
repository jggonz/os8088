#!/usr/bin/env python3
"""Drive the emulated serial mouse to absolute positions.

    python3 tools/mouse.py <socket> <cmd> [args]

Commands:
    to X Y          pin to a corner, then walk to (X, Y)
    move DX DY      relative move (split into safe chunks)
    down [X Y]      left button press (optionally `to X Y` first)
    up [X Y]        left button release (optionally `to X Y` first)
    click X Y       to X Y + press + release
    shot PATH       screendump (absolute path)

Any other argument shape is an error - historically `down X Y` silently
pressed at the CURRENT cursor position, a footgun that read as a kernel
bug.

QEMU's msmouse backend truncates large deltas to the protocol's low bits
without clamping, so every move is split into chunks of at most 60.
Position is made absolute by first pinning against the bottom-right clamp
(kernel clamps mouse_x/y to 639/479), then walking back with exact deltas.
"""
import subprocess
import sys
import time

SOCK = sys.argv[1]
STEP = 60


def hmp(*cmds):
    subprocess.run([sys.executable, "tools/qmp.py", SOCK, *cmds], check=True)


def chunks(d):
    while d:
        c = max(-STEP, min(STEP, d))
        yield c
        d -= c


def move(dx, dy):
    xs, ys = list(chunks(dx)), list(chunks(dy))
    while len(xs) < len(ys):
        xs.append(0)
    while len(ys) < len(xs):
        ys.append(0)
    for cx, cy in zip(xs, ys):
        hmp(f"mouse_move {cx} {cy}")
    time.sleep(0.15)


def goto(x, y):
    for _ in range(12):                 # pin at (639,479)
        hmp("mouse_move 60 60")
    move(x - 639, y - 479)


def main():
    cmd, args = sys.argv[2], sys.argv[3:]
    if cmd == "to":
        goto(int(args[0]), int(args[1]))
    elif cmd == "move":
        move(int(args[0]), int(args[1]))
    elif cmd in ("down", "up"):
        if len(args) == 2:
            goto(int(args[0]), int(args[1]))
        elif args:
            print(f"mouse.py: {cmd} takes no args or X Y, got {args!r}",
                  file=sys.stderr)
            return 2
        hmp("mouse_button 1" if cmd == "down" else "mouse_button 0")
        time.sleep(0.15)
    elif cmd == "click":
        goto(int(args[0]), int(args[1]))
        hmp("mouse_button 1")
        time.sleep(0.2)
        hmp("mouse_button 0")
        time.sleep(0.2)
    elif cmd == "shot":
        hmp(f"screendump {args[0]}")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
