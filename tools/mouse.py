#!/usr/bin/env python3
"""Drive the emulated serial mouse to absolute positions.

    python3 tools/mouse.py <socket> <cmd> [args]

Commands:
    to X Y          pin to a corner, then walk to (X, Y)
    move DX DY      relative move (split into safe chunks)
    down [X Y]      left button press (optionally `to X Y` first)
    up [X Y]        left button release (optionally `to X Y` first)
    click X Y       to X Y + press + release
    rdown [X Y]     RIGHT button press (context menus, SPEC.md 12.4)
    rup [X Y]       right button release
    rclick X Y      to X Y + right press + release
    shot PATH       screendump (absolute path)

Any other argument shape is an error - historically `down X Y` silently
pressed at the CURRENT cursor position, a footgun that read as a kernel
bug.

QEMU's msmouse backend truncates large deltas to the protocol's low bits
without clamping, so every move is split into chunks of at most 60.
Position is made absolute by first pinning against the bottom-right clamp
(kernel clamps mouse_x/y to 639/479), then walking back with exact deltas.

BTN_L / BTN_R are the HMP `mouse_button` bitmask, and the mask is NOT what
QEMU's own help string says. hmp-commands.hx documents "1=L, 2=M, 4=R", but
hmp_mouse_button hands the value straight to qemu_input_update_buttons
against the legacy MOUSE_EVENT_* bits - 1 = left, 2 = RIGHT, 4 = middle -
and msmouse.c folds MOUSE_EVENT_RBUTTON into byte 0 bit 4, which is where
mouse.inc's decoder looks. Verified end to end against this kernel: only
`mouse_button 2` moves the right button.
"""
import subprocess
import sys
import time

SOCK = sys.argv[1]
STEP = 60
BTN_L = 1                               # HMP mouse_button bitmask, see above
BTN_R = 2


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
    elif cmd in ("down", "up", "rdown", "rup"):
        if len(args) == 2:
            goto(int(args[0]), int(args[1]))
        elif args:
            print(f"mouse.py: {cmd} takes no args or X Y, got {args!r}",
                  file=sys.stderr)
            return 2
        btn = BTN_R if cmd.startswith("r") else BTN_L
        hmp(f"mouse_button {btn}" if cmd.endswith("down") else "mouse_button 0")
        time.sleep(0.15)
    elif cmd in ("click", "rclick"):
        btn = BTN_R if cmd == "rclick" else BTN_L
        goto(int(args[0]), int(args[1]))
        hmp(f"mouse_button {btn}")
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
