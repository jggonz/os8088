#!/usr/bin/env python3
"""Drive the emulated serial mouse to absolute positions.

    python3 tools/mouse.py [--screen WxH] <socket> <cmd> [args]

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

--screen WxH names the guest's screen, default 640x480. It MUST match the
adapter the kernel detected (SPEC.md 39), because the absolute position is
derived by pinning against the kernel's own edge clamp - so on a
`make test VIDEO=cga` boot this has to be `--screen 640x200` or every y
lands 280 pixels off, silently.

Any other argument shape is an error - historically `down X Y` silently
pressed at the CURRENT cursor position, a footgun that read as a kernel
bug.

QEMU's msmouse backend truncates large deltas to the protocol's low bits
without clamping, so every move is split into chunks of at most 60.
Position is made absolute by first pinning against the bottom-right clamp
(the kernel clamps mouse_x/y to [vid_wm1]/[vid_hm1]), then walking back with
exact deltas.

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

ARGV = sys.argv[1:]
SCREEN_W, SCREEN_H = 640, 480
if ARGV and ARGV[0] == "--screen":
    SCREEN_W, SCREEN_H = (int(v) for v in ARGV[1].split("x"))
    ARGV = ARGV[2:]

SOCK = ARGV[0]
STEP = 60
PACE = 0.06                             # seconds between packets, see paced()
BTN_L = 1                               # HMP mouse_button bitmask, see above
BTN_R = 2


def hmp(*cmds):
    subprocess.run([sys.executable, "tools/qmp.py", SOCK, *cmds], check=True)


def chunks(d):
    while d:
        c = max(-STEP, min(STEP, d))
        yield c
        d -= c


def paced(moves):
    """Emit moves down ONE connection with a sleep between each.

    The msmouse backend runs at 1200 baud - about 25ms per 3-byte packet -
    and a move whose packet is still in flight when the next one is queued
    is simply lost. Spawning one qmp.py per move used to be slow enough to
    hide that on its own; on a fast host it is not, and the symptom is a
    cursor that does not move at all while every screendump still looks
    plausible. Pace it explicitly instead (CLAUDE.md's documented rule).
    """
    cmds = []
    for cx, cy in moves:
        cmds += [f"mouse_move {cx} {cy}", f"sleep {PACE}"]
    for i in range(0, len(cmds), 60):   # keep argv a sane length
        hmp(*cmds[i:i + 60])


def move(dx, dy):
    xs, ys = list(chunks(dx)), list(chunks(dy))
    while len(xs) < len(ys):
        xs.append(0)
    while len(ys) < len(xs):
        ys.append(0)
    paced(list(zip(xs, ys)))
    time.sleep(0.15)


def goto(x, y):
    pins = (max(SCREEN_W, SCREEN_H) + STEP - 1) // STEP
    paced([(STEP, STEP)] * pins)        # pin at the bottom-right clamp
    move(x - (SCREEN_W - 1), y - (SCREEN_H - 1))


def main():
    cmd, args = ARGV[1], ARGV[2:]
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
