#!/usr/bin/env python3
"""The PS/2 WHEEL reaches the kernel (SPEC.md 9.9.8, 10.4).

    python3 tests/wheelps2.py

QEMU, and on CLAUDE.md's closed list for ps2mouse.py's reason (entry 4):
MartyPC is an 8088 with no 8042, and QEMU's i8042 implements the
IntelliMouse knock, so this is the one machine here that can answer the
question at all. Nothing here is a time.

WHAT IT ASSERTS:

  mou_p2st  9    - the handshake ran to the end (ps2mouse.py's own row)
  mou_p2wz  3    - SPEC.md 9.9.8's knock took: the device answered ID 3
                   and every packet is FOUR bytes from here on
  mou_p2id  0    - ...and the RESET's ID is still what is published (9.4.4)
  the pointer lands EXACTLY on a pixel - which, with the device in four-byte
                   mode, is a statement about the FRAMING: a decoder still
                   counting three would misframe on the first Z byte and the
                   walk would wander

then the wheel itself, read where it lands. ui_task takes [mou_whl] with one
xchg every pass, so on an idle desktop the level is gone before anything can
read it - EXCEPT while a menu is held open, when the UI task is inside the
menu tracker and nothing consumes it. So: press on the first bar title and
hold it, turn the wheel, and read the word.

  +4 notches toward the user   -> [mou_whl] = +4   (the SIGN: QEMU sends
                                                    toward-the-user as +Z)
  then 6 away                  -> [mou_whl] = -2   (it SUMS: a level, not
                                                    a queue)
  release, one UI pass         -> [mou_whl] = 0    (the consumer spent it)

and the keyboard survives, as ps2mouse.py asks (the knock is seven more
commands inside the probe's 0xAD window).

IT HAS BEEN SEEN RED, for real and not on purpose (docs/WRITING-TESTS.md
1): the first knock read its table with the overlay's own offset through
DS = KERNEL_SEG, sent garbage, failed safe to three bytes, and this row
reported `mou_p2wz 00, want 03` and `[mou_whl] by 0, want +4` - while the
pointer and keyboard legs stayed green, which is what an unknocked
three-byte mouse looks like. Any break of the knock reads the same way.
"""
import os
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88fixture                                          # noqa: E402
import os88sym                                              # noqa: E402
import os88qemu                                             # noqa: E402

SOCK = os.path.join(ROOT, "build", "wheelps2.sock")
PIDFILE = os.path.join(ROOT, "build", "wheelps2.pid")
KBHEAD = 0x41A
TARGET_X, TARGET_Y = 200, 150
MB_ENTSZ = 12
MB_XL, MB_XR = 6, 8
TARGETS = ["build/os8088.img", "build/apps.img"]


def kill_stale():
    """By PIDFILE, never by pkill -f (CLAUDE.md)."""
    if os.path.exists(PIDFILE):
        try:
            pid = int(open(PIDFILE).read().strip())
            os.kill(pid, signal.SIGTERM)
            os88qemu.gone(pid)
        except Exception:
            pass
    for f in (SOCK, PIDFILE):
        if os.path.exists(f):
            os.unlink(f)


def launch():
    """ps2mouse.py's machine exactly: no UARTs, no VMware backdoor, so the
    PS/2 mouse is the only pointing device and wins the contest alone."""
    em = "qemu" + "-system-i386"     # never whole on a command line: kill_stale
    subprocess.run(
        em + " -machine pc,vmport=off"
        " -drive file=build/os8088.img,format=raw,if=floppy -boot a"
        " -drive file=build/apps.img,format=raw,if=floppy,index=1"
        " -serial none"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (SOCK, PIDFILE), cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)


def byte(q, sym):
    return q.read(os88sym.linear(sym), 1)[0]


def word(q, sym):
    b = q.read(os88sym.linear(sym), 2)
    return b[0] | (b[1] << 8)


def sword(q, sym):
    w = word(q, sym)
    return w - 0x10000 if w & 0x8000 else w


def mouse(*args, timeout=180):
    subprocess.run([sys.executable, "tools/mouse.py", SOCK] + [str(a) for a in args],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL,
                   timeout=timeout)


def main():
    kill_stale()
    os88fixture.need(*TARGETS)
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

        os88qemu.acted(q, lambda: byte(q, "mou_p2st") == 9, secs=90,
                       what="[mou_p2st] = 9", poll=0.25)
        for sym, want in (("mou_p2st", 9), ("mou_p2", 1), ("mou_p2id", 0),
                          ("mou_p2wz", 3)):
            got = byte(q, sym)
            if got != want:
                fails.append("%s %02X, want %02X%s" % (
                    sym, got, want,
                    " - the knock did not put the device in WHEEL mode "
                    "(SPEC.md 9.9.8), so no Z byte will ever come"
                    if sym == "mou_p2wz" else ""))

        # --- the FRAMING: an exact walk with four-byte packets ---------------
        mouse("to", TARGET_X, TARGET_Y)
        os88qemu.acted(q, lambda: (word(q, "mouse_x"), word(q, "mouse_y"))
                       == (TARGET_X, TARGET_Y), secs=3, what="the pointer")
        gx, gy = word(q, "mouse_x"), word(q, "mouse_y")
        if (gx, gy) != (TARGET_X, TARGET_Y):
            fails.append("pointer at %d,%d, want %d,%d - with the device in "
                         "four-byte mode a miss is the FRAMING: the Z byte "
                         "read as a header (SPEC.md 9.9.8)"
                         % (gx, gy, TARGET_X, TARGET_Y))

        # --- the wheel, held where nothing consumes it ------------------------
        ent = q.read(os88sym.linear("menu_bar"), MB_ENTSZ)
        x = (int.from_bytes(ent[MB_XL:MB_XL + 2], "little")
             + int.from_bytes(ent[MB_XR:MB_XR + 2], "little")) // 2
        mouse("down", x, 8)
        os88qemu.acted(q, lambda: byte(q, "menu_dropd") == 1
                       or word(q, "menu_sseg") != 0, secs=3,
                       what="the first bar title held open")
        w0 = sword(q, "mou_whl")
        mouse("wheel", 4, timeout=30)
        os88qemu.acted(q, lambda: sword(q, "mou_whl") - w0 == 4, secs=3,
                       what="[mou_whl] +4")
        d1 = sword(q, "mou_whl") - w0
        if d1 != 4:
            fails.append("four notches toward the user moved [mou_whl] by %d, "
                         "want +4 - %s" % (d1, "NOTHING arrived: no Z byte "
                         "reached mou_p2_byte's phase 3" if d1 == 0 else
                         "the SIGN or the sum is wrong (SPEC.md 10.4)"))
        mouse("wheel", -6, timeout=30)
        os88qemu.acted(q, lambda: sword(q, "mou_whl") - w0 == -2, secs=3,
                       what="[mou_whl] -2")
        d2 = sword(q, "mou_whl") - w0
        if d2 != -2:
            fails.append("then six away left [mou_whl] at %d over its start, "
                         "want -2: it is a LEVEL that sums (SPEC.md 10.4)" % d2)
        mouse("up", timeout=30)
        os88qemu.acted(q, lambda: word(q, "mou_whl") == 0, secs=3,
                       what="the UI task spending [mou_whl]")
        if word(q, "mou_whl") != 0:
            fails.append("[mou_whl] still %d after the menu closed: ui_task "
                         "never took it (SPEC.md 10.4)" % sword(q, "mou_whl"))

        # --- ...and the keyboard, through the knock's extra seven commands ----
        before = q.read(KBHEAD, 4)
        tail0 = before[2] | (before[3] << 8)
        for k in "abcdef":
            q.hmp("sendkey " + k)

        def queued():
            b = q.read(KBHEAD, 4)
            return ((b[2] | (b[3] << 8)) - tail0) & 0xFFFF == 12
        os88qemu.acted(q, queued, secs=3, what="six keys in the BIOS ring")
        after = q.read(KBHEAD, 4)
        if ((after[2] | (after[3] << 8)) - tail0) & 0xFFFF != 12:
            fails.append("six keys did not all reach the BIOS ring: the knock "
                         "left the keyboard interface off (SPEC.md 9.9.1)")
        q.hmp("quit")
    finally:
        kill_stale()

    if fails:
        for f in fails:
            print("wheelps2: FAIL " + f)
        return 1
    print("wheelps2: ok - knock took (ID 3), pointer exact in four-byte mode, "
          "+4 / -2 summed and signed, spent on release, keyboard intact")
    return 0


if __name__ == "__main__":
    sys.exit(main())
