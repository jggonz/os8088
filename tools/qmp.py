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
STORE = 10.0            # GUEST seconds a key's press may take to be STORED -
                        # a ceiling the wait ends at the moment it holds,
                        # never a pause (tests/os88qemu.py STORE_SECS)
# The names the ROM keeps a FLAG for at 0040:0017 rather than a buffer entry:
# their press is confirmed by that byte changing, every other key's by the
# buffer's tail moving (tests/os88qemu.py FLAG_KEYS - the same set)
FLAG_KEYS = frozenset(("shift", "shift_r", "alt", "alt_r", "ctrl", "ctrl_r",
                       "caps_lock", "num_lock", "scroll_lock"))


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    out, err, rc = run(sys.argv[1], sys.argv[2:])
    if out:
        print(out)
    if err:
        print(err, file=sys.stderr)
    return rc


def run(path, cmds):
    """The command line's work IN PROCESS: (stdout, stderr, exit code), the
    text exactly what main() prints. tests/xmcheck.py's `qmp` calls this
    rather than spawning a Python per memory read (~60 ms each)."""
    c = Client(path)
    try:
        return c.run(cmds)
    finally:
        c.close()


class Client(object):
    """One QMP connection, kept: `run(cmds)` is run()'s work without the
    connect, greeting and qmp_capabilities - three round trips a call where a
    loaded QEMU answers each in ~10 ms. QMP serves ONE client: while this is
    open every other client (tools/mouse.py, tools/shot.py) waits in the
    listen backlog for ever, so close it before handing the socket on."""

    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.f = self.s.makefile("rw")
        json.loads(self.f.readline())         # greeting
        self.send({"execute": "qmp_capabilities"})

    def send(self, obj):
        f = self.f
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

    def close(self):
        try:
            self.f.close()
        finally:
            self.s.close()

    def run(self, cmds):
        outs = []
        send = self.send

        def ticks():
            r = send({"execute": "human-monitor-command",
                      "arguments": {"command-line": "xp /1hx 0x46c"}})
            m = re.search(r":\s*0x([0-9a-fA-F]+)", r.get("return", ""))
            if not m:
                raise RuntimeError("xp answered %r" % r)
            return int(m.group(1), 16)

        def gsleep(secs):
            # Whole ticks, rounded UP and one more for the phase, so it is
            # never less than `secs`; a clock that stops ends it, as a sleep
            # would.
            need = int(secs * TICK_HZ + 0.999) + 1
            last, n, seen = ticks(), 0, time.monotonic()
            while n < need and time.monotonic() - seen < STALL:
                time.sleep(0.01)
                t = ticks()
                d = (t - last) & 0xFFFF
                if d:
                    n += 1 if d > 256 else d    # SET (POST, midnight), not run
                    last, seen = t, time.monotonic()

        def tail():
            r = send({"execute": "human-monitor-command",
                      "arguments": {"command-line": "xp /1hx 0x41c"}})
            m = re.search(r":\s*0x([0-9a-fA-F]+)", r.get("return", ""))
            return int(m.group(1), 16) if m else None

        def kflags():
            r = send({"execute": "human-monitor-command",
                      "arguments": {"command-line": "xp /1bx 0x417"}})
            m = re.search(r":\s*0x([0-9a-fA-F]+)", r.get("return", ""))
            return int(m.group(1), 16) if m else None

        def until(cond, secs):
            # `cond` true, or `secs` of GUEST time (BIOS ticks), or a stall;
            # answers whether `cond` held
            need = int(secs * TICK_HZ + 0.999) + 1
            last, n, seen = ticks(), 0, time.monotonic()
            while (n < need and not cond()
                   and time.monotonic() - seen < STALL):
                time.sleep(0.005)
                t = ticks()
                d = (t - last) & 0xFFFF
                if d:
                    n += 1 if d > 256 else d
                    last, seen = t, time.monotonic()
            return bool(cond())

        def key(spec, secs=0.3):
            # A PLAIN `sendkey` IS CONFIRMED (docs/plans/SOAK-PARALLEL.md
            # 17.5): HMP's own queues the release 100 ms of HOST time after
            # the press, and a guest the host starved past that took both at
            # one IRQ1 and lost the key. So the press goes, then the release
            # only once the ROM has stored it - the BIOS buffer's tail at
            # 0040:001C moved. Modifiers are held across the key.
            #
            # A KEY THE ROM STORES IS NEVER RELEASED BEFORE IT IS STORED
            # (17.11). The release used to go after `secs` - 0.3 guest
            # seconds, seven ticks - whatever the tail did, for "a key the
            # ROM stores nothing for"; but that ceiling applied to EVERY
            # key, and a release that reaches the 8042 before the guest has
            # taken the press is the queued pair SPEC.md 9.7 says QEMU loses
            # whenever kbm_isr peeks port 60h (a package asked for key
            # state, or no mouse has spoken) - measured: Gorillas' setup page
            # took 0 of 7 Enters sent that way, and the press-to-store time
            # in the same row under two CPU hogs ran to 4 of those 7 ticks.
            # So only a FLAG key (a modifier, a lock: confirmed by 0040:0017)
            # keeps the short ceiling, and every other key waits up to STORE
            # guest seconds for the tail, saying so if it never moved.
            names = spec.split("-")

            def ev(name, down):
                r = send({"execute": "input-send-event", "arguments": {
                    "events": [{"type": "key", "data": {
                        "down": down,
                        "key": {"type": "qcode", "data": name}}}]}})
                if "error" in r:
                    raise RuntimeError(r["error"])
            # EACH MODIFIER IS CONFIRMED TOO, before the next event goes: a
            # make code queued behind another at one IRQ1 is the case the
            # kernel's 8042 peek loses on QEMU (SPEC.md 9.7), and a shift lost
            # that way typed `http;;//` for `http://`. The ROM's shift-state
            # byte at 0040:0017 changing is the proof; a modifier the ROM
            # keeps no bit for runs out the same guest budget instead.
            for n in names[:-1]:
                f0 = kflags()
                ev(n, True)
                until(lambda: kflags() != f0, secs)
            last = names[-1]
            if last in FLAG_KEYS:
                f0 = kflags()
                ev(last, True)
                until(lambda: kflags() != f0, secs)
            else:
                t0 = tail()
                ev(last, True)
                if not until(lambda: tail() != t0, STORE):
                    sys.stderr.write("qmp.py: sendkey %s: the ROM had not "
                                     "stored the press after %.0f guest "
                                     "seconds - released anyway\n"
                                     % (spec, STORE))
            ev(last, False)
            for n in reversed(names[:-1]):
                f0 = kflags()
                ev(n, False)
                until(lambda: kflags() != f0, secs)

        for cmd in cmds:
            k = re.match(r"^sendkey (\S+)$", cmd)
            if k is not None:
                try:
                    key(k.group(1))
                except RuntimeError as e:
                    return "\n".join(outs), "error: %s" % e, 1
                continue
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
                outs.append(out.strip())
            elif "error" in r:
                return "\n".join(outs), f"error: {r['error']}", 1
        return "\n".join(outs), "", 0


if __name__ == "__main__":
    sys.exit(main())
