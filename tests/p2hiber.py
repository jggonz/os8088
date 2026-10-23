#!/usr/bin/env python3
"""kern_big: the PS/2 mouse after a hibernate's REBOOT-route resume
(SPEC.md 96.45.2.4; docs/plans/KERN-MEGA-PLAN.md 14.57).

    make && python3 tests/p2hiber.py
    python3 tests/p2hiber.py --img IMG      another system floppy (how the
                                            failing case was run)

tests/megap2hiber.py is the same question on kern_mega under Bochs; this is
kern_big's, under QEMU (docs/TESTING.md's list, entry 4: MartyPC has no 8042).
QEMU's i8042 is the useful one here for a second reason: it raises IRQ12 the
moment a byte reaches the output buffer, so where hbm_wake's re-knock polls
60h with IRQ12 open, mou_p2_isr takes EVERY reply and the knock fails at its
first ACK - deterministically, where the field machine's 8042 is a coin toss
per byte (and a toss lost on an 0xF3 kills the pointer outright).

The machine is hmahiber's with the serial mouse taken OUT, so the PS/2 mouse
QEMU's i8042 carries is the pointer: the shipped system floppy with a
SYSTEM.CFG wanting HDD.DRV (and hmadrv's other two), a FAT16 C: on IDE that
the hibernation file is written to, -m 128. Hibernate from the System menu,
Enter, the power-off (QEMU killed), a fresh QEMU, Enter to Resume. Then:

  1. [hb_resumes] is 1;
  2. the MASTER 8259's IMR is the one it had before the hibernate (`not
     [mou_line]` was 0 for MOU_P2LINE and opened it: A8h came home 00h);
  3. mou_p2wz is what it was before - 3, QEMU's mouse answering the knock
     (with IRQ12 open the ISR took the 0xF5's ACK and the wheel came home 0);
  4. the pointer moves on QEMU's relative mouse.

THE FAILING CASE (docs/WRITING-TESTS.md 1): a system floppy built from the
HIBER.DRV before 14.57 - `--img` - and 2 and 3 go red: master 00h against
A8h, mou_p2wz 0 against 3. Verified when the row was written.

Needs QEMU 9 (`qemu9`): QEMU 8.2 aborts taking an interrupt on the resume
stub's stack in the text framebuffer (tools/os88build.py's qemu9()).
"""
import atexit
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88qemu    # noqa: E402
import os88sym     # noqa: E402
import hmacold     # noqa: E402
import hmadrv      # noqa: E402
import xmcheck     # noqa: E402

S = os88sym.linear
B = os.path.join(ROOT, "build")
HB_M_RESUME = 2                         # kernel/hiber.inc


class Q(object):
    """One private QEMU with NO serial mouse: the PS/2 one is the pointer."""

    def __init__(self, tmp, sysimg, apps, hd):
        self.sock = os.path.join(tmp, "qmp.sock")
        if os.path.exists(self.sock):
            os.remove(self.sock)
        pid = os.path.join(tmp, "qemu.pid")
        subprocess.run(
            ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", "128",
             "-audiodev", "none,id=snd", "-device", "sb16,audiodev=snd",
             "-drive", "file=%s,format=raw,if=ide,index=0,media=disk" % hd,
             "-drive", "file=%s,format=raw,if=floppy" % sysimg, "-boot", "a",
             "-drive", "file=%s,format=raw,if=floppy,index=1" % apps,
             "-display", "none", "-qmp", "unix:%s,server,nowait" % self.sock,
             "-daemonize", "-pidfile", pid], check=True)
        os88qemu.own(pid, self.sock)
        # ...and by the PID it wrote as well, vidxms.Q's reason: the pidfile
        # is in a TemporaryDirectory a failing run deletes on its way out
        self.pidn = int(open(pid).read().strip())
        atexit.register(self.close)
        for _ in range(150):
            if os.path.exists(self.sock):
                break
            time.sleep(0.2)

    def close(self):
        if self.pidn:
            try:
                os.kill(self.pidn, signal.SIGTERM)
                os88qemu.gone(self.pidn)
            except OSError:
                pass
            self.pidn = None

    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)


def byte(q, name):
    return q.read(S(name), 1)[0]


def word(q, name):
    b = q.read(S(name), 2)
    return b[0] | b[1] << 8


def master_imr(q):
    out = str(xmcheck.qmp(q.sock, "info pic"))
    m = re.search(r"pic0: .*?imr=([0-9a-f]+)", out)
    return int(m.group(1), 16) if m else None


def disks(tmp, img):
    sysimg = os.path.join(tmp, "sys.img")
    shutil.copy(img, sysimg)
    cfg = os.path.join(tmp, "SYSTEM.CFG")
    with open(cfg, "wb") as f:
        f.write(b"O88CFG\0\0" + struct.pack("<H", 3) + b"DW" + bytes([1, 2])
                + struct.pack("<H", hmadrv.WANT) + b"\0\0")
    subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg, cfg],
                   check=True, capture_output=True)
    apps = os.path.join(tmp, "apps.img")
    shutil.copy(os.path.join(B, "apps.img"), apps)
    hd = os.path.join(tmp, "hd.img")
    note = os.path.join(tmp, "HELLO.TXT")
    with open(note, "w") as f:
        f.write("hello from C:\r\n")
    subprocess.run([sys.executable, "tools/os88disk.py", "-o", hd, "--hdd",
                    "--mbr", os.path.join(B, "mbr.bin"),
                    "--boot", os.path.join(B, "boothd.bin"),
                    "--kernel", os.path.join(B, "kernel.sys"), note],
                   check=True, capture_output=True)
    return sysimg, apps, hd


def main():
    os.chdir(ROOT)
    img = (sys.argv[sys.argv.index("--img") + 1] if "--img" in sys.argv
           else os.path.join(B, "os8088.img"))
    for need in (img, os.path.join(B, "apps.img"), os.path.join(B, "mbr.bin"),
                 os.path.join(B, "boothd.bin"), os.path.join(B, "kernel.sys")):
        if not os.path.exists(need):
            sys.exit("p2hiber: no %s - run `make`" % need)
    fails = []

    def want(cond, what):
        print("   %s  %s" % ("ok  " if cond else "FAIL", what), flush=True)
        if not cond:
            fails.append(what)

    with tempfile.TemporaryDirectory(dir=B) as tmp:
        sysimg, apps, hd = disks(tmp, img)
        q = Q(tmp, sysimg, apps, hd)
        xmcheck.wait_desktop(q.sock, "p2hiber")
        os88qemu.pace(q, 2)
        m0, wz0 = master_imr(q), byte(q, "mou_p2wz")
        print("   before: master %s, mou_p2 %d, mou_p2wz %d" % (
            m0 is not None and "%02X" % m0, byte(q, "mou_p2"), wz0))
        if byte(q, "mou_p2") != 1 or wz0 != 3:
            sys.exit("p2hiber: the PS/2 mouse is not the live wheel mouse "
                     "(mou_p2 %d, mou_p2wz %d) - nothing below means anything"
                     % (byte(q, "mou_p2"), wz0))
        ui = hmacold.QUI(q, mouse=hmacold.QMouse(q), sym=S, verbose=False)
        ui.menu_pick("Apple", "Hibernate...")
        ui.wait_window("Hibernate")
        os88qemu.pace(q, 1)
        xmcheck.qmp(q.sock, "sendkey ret")
        if not os88qemu.acted(q, lambda: q.read(0x449, 1)[0] in (3, 7),
                              secs=180, what="the hibernated sentence"):
            q.close()
            sys.exit("p2hiber: the hibernate never reached text mode (toast "
                     "%r)" % (ui.toast(),))
        os88qemu.pace(q, 3)
        q.close()                               # the power-off
        q = Q(tmp, sysimg, apps, hd)
        if not os88qemu.acted(q, lambda: byte(q, "hb_mode") == HB_M_RESUME,
                              secs=120, what="the resume question"):
            q.close()
            sys.exit("p2hiber: the fresh boot never asked the question")
        os88qemu.pace(q, 1)
        xmcheck.qmp(q.sock, "sendkey ret")      # Resume
        ok = os88qemu.acted(q, lambda: word(q, "hb_resumes") == 1, secs=180,
                            what="[hb_resumes] to reach 1")
        want(ok, "1. the machine resumed: [hb_resumes] %d"
             % word(q, "hb_resumes"))
        if ok:
            os88qemu.acted(q, lambda: byte(q, "sch_lock") == 0, secs=20,
                           what="the wake's last step")
            os88qemu.pace(q, 2)
            m1, wz1 = master_imr(q), byte(q, "mou_p2wz")
            want(m1 == m0, "2. the master 8259's mask came home %s, as it "
                 "hibernated (%s)" % (m1 is not None and "%02X" % m1,
                                      m0 is not None and "%02X" % m0))
            want(wz1 == wz0, "3. the wheel came home: mou_p2wz %d (was %d)"
                 % (wz1, wz0))
            xy0 = (word(q, "mouse_x"), word(q, "mouse_y"))
            for _ in range(5):
                xmcheck.qmp(q.sock, "mouse_move 20 15")
                os88qemu.pace(q, 0.3)
            xy1 = (word(q, "mouse_x"), word(q, "mouse_y"))
            want(xy1 != xy0, "4. the PS/2 pointer moves after the resume: "
                 "%s -> %s" % (xy0, xy1))
        q.close()
    if fails:
        print("p2hiber: %d FAILED" % len(fails))
        return 1
    print("p2hiber: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
