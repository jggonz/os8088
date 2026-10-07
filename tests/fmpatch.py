#!/usr/bin/env python3
"""Does an FM patch-load name ITS channel? (SPEC.md 34.2.2)

`OSAPI_SND_FM` verb 2 stages the caller's eleven bytes with a `loop` over CX
and used to hand the driver CX = 0, so every patch on every channel landed on
channel 0. This drives fmtest's two clicks - channel 0, then channel 1 - and
reads CL where the staging calls `osapi_snd_fm`. The channel-0 click is the
control: it reads 0 on either build, or the breakpoint is not where the
question is. VERIFIED TO FAIL with the three pushes in their old order: the
channel-1 load reads CL = 0.

No card is needed: the router is entered whether or not SOUND.DRV is mounted,
and it is the channel it is HANDED that is the question. With no driver the
verb is refused and the handler has to come back to take the second click -
which is tests/fmrefuse.py's subject (SPEC.md 2.6.1.1), and asserted here
only as the precondition for the second reading.

    make && make build/fmtest.o88 && python3 tests/fmpatch.py [machine]
"""
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88ui                                             # noqa: E402
import os88marty                                          # noqa: E402
import os88geom                                           # noqa: E402

SYMS = ("ft_stage", "ft_s_line1")
fails = []


def check(ok, what, got=""):
    print("  %-4s %s%s" % ("ok" if ok else "FAIL", what,
                           "" if ok else "   got: %s" % got))
    if not ok:
        fails.append(what)


def offsets():
    """fmtest's own offsets, read by assembling it with a table after it."""
    src = open(os.path.join(ROOT, "tests/fmtest/fmtest.asm"),
               encoding="utf-8").read()
    tmp = os.path.join(ROOT, "build", "fmpatch-off.asm")
    out = os.path.join(ROOT, "build", "fmpatch-off.bin")
    open(tmp, "w", encoding="utf-8").write(
        src + "\n" + "".join("dw %s\n" % s for s in SYMS))
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/", "-o", out,
                    tmp], cwd=ROOT, check=True)
    blob = open(out, "rb").read()
    base = len(blob) - 2 * len(SYMS)
    return {s: struct.unpack_from("<H", blob, base + 2 * i)[0]
            for i, s in enumerate(SYMS)}


def disk():
    """A 360KB scratch floppy with fmtest on it - its own target's is 1.44MB,
    which no 5150 profile here can read."""
    img = os.path.join(ROOT, "build", "fmpatch360.img")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", img,
                    "--size", "360", "build/fmtest.o88"], cwd=ROOT, check=True,
                   stdout=subprocess.DEVNULL)
    return img


def click_cl(ui, win):
    """Click fmtest once and answer CL at the verb-2 dispatch.

    The PRESS is proven before the breakpoint is armed and the release is
    sent after it is cleared: a machine stopped at a breakpoint decodes no
    packet, so a click spanning the stop raises about the UART. W_ONCLICK
    runs on the UI task well after the ISR has published the press."""
    m, mo = ui.m, ui.mo
    x1, y1, x2, y2 = win.content
    mo.to((x1 + x2) // 2, (y1 + y2) // 2)
    mo._sep()                           # not the second half of a double
    m.bp_exec("osapi_snd_fm")
    mo._edge(True)
    cl = None
    for _ in range(4):                  # verb 2 is the first stop
        if not m.wait_stop(20):
            break
        r = m.regs()
        if r["ax"] & 0xFF == 2:
            cl = r["cx"] & 0xFF
            break
        m.run()
    m.bp_exec()
    m.run()
    mo._edge(False)
    os88marty.guest_sleep(m, 1.5)
    return cl


def main():
    off = offsets()
    mach = sys.argv[1] if len(sys.argv) > 1 else "os8088_5150_herc_gla"
    with os88ui.boot("build/os8088-360.img", apps=disk(), machine=mach) as ui:
        m = ui.m
        ui.path("B:/FMTEST.O88")
        win = ui.window("FM Test")
        seg = struct.unpack("<H", bytes(
            m.read(os88geom.winptr(m, win) + os88geom.W_SEG, 2)))[0]
        stage = lambda: m.readseg(seg, off["ft_stage"], 1)[0]
        status = lambda: chr(m.readseg(seg, off["ft_s_line1"] + 10, 1)[0])

        c0 = click_cl(ui, win)
        check(c0 == 0, "the channel-0 patch-load reaches the router with "
              "CL = 0 (the control)", c0)
        check(stage() == 1 and status() == "P", "the refused call came back "
              "(fmrefuse's subject; the second click needs it)",
              "stage %d, status %r" % (stage(), status()))
        if not fails:
            c1 = click_cl(ui, win)
            check(c1 == 1, "the channel-1 patch-load reaches the router with "
                  "CL = 1", c1)
    print("fmpatch: %s" % ("ok" if not fails else "FAILED %d" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
