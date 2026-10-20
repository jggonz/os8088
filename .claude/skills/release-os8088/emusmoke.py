#!/usr/bin/env python3
"""Boot the release's emulator disk the way its README tells a reader to.

    python3 .claude/skills/release-os8088/emusmoke.py [--shot build/smoke-emu.png]

build/emu.img (`make emu`, SPEC.md 9.11.7) exists for one reason: in an
emulator the pointer follows the host mouse with no grab. A screenshot of a
desktop proves it boots and cannot prove that, because an emulator disk whose
driver never attached boots to the same desktop -- the system falls back to
the serial mouse without a word. So this reads the answer out of the guest.

THE COMMAND LINE IS THE README'S, plus only what a headless run needs: no
`-machine` flag, so QEMU's own default `pc` machine and its default vmport,
and no mouse line. If the README's line ever stops working, this stops
passing, which is the point of not using tests/vmmouse.py's line (it spells
`vmport=on` and `-serial none`, both of which a reader would not type).

It asserts:

  vmm_on   1   the driver loaded, the backdoor answered and absolute mode is on
  mou_port 6   MOU_VMROW: the backdoor won the mouse contest
  three absolute positions land within 2px of where they were sent

It reads guest memory through tools/os88sym.py against build/emuk/, the
kern_emu build `make emu` leaves there -- the shipped kernel has no vmm_on
symbol at all. Kills only the QEMU it started, by its pidfile: other
worktrees on this machine run QEMUs of their own.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

ROOT = subprocess.check_output(["git", "rev-parse", "--show-toplevel"],
                               text=True).strip()
os.chdir(ROOT)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88build                                            # noqa: E402
os88build.use_build("build/emuk")
os.environ.setdefault("OS88_DEFINES", "KERN_EMU")
import heapmap                                              # noqa: E402
import os88sym                                              # noqa: E402

SOCK = os.path.join(ROOT, "build", "emusmoke.sock")
PID = os.path.join(ROOT, "build", "emusmoke.pid")
ABS_MAX = 0x7FFF                # QEMU's INPUT_EVENT_ABS_MAX
TOL = 2


def qmp(*events):
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
            while "event" in json.loads(f.readline() or '{"x":0}'):
                pass
    finally:
        f.close()
        s.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shot", default="build/smoke-emu.png")
    args = ap.parse_args()
    for img in ("build/emu.img", "build/apps.img", "build/emuk/kernel.bin"):
        if not os.path.isfile(img):
            print("emusmoke: %s is missing -- run `make && make emu`" % img)
            return 1
    for f in (SOCK, PID):
        if os.path.exists(f):
            os.unlink(f)
    em = "qemu" + "-system-i386"
    subprocess.run([em,
                    "-drive", "file=%s/build/emu.img,format=raw,if=floppy" % ROOT,
                    "-boot", "a",
                    "-drive", "file=%s/build/apps.img,format=raw,if=floppy,index=1"
                    % ROOT,
                    "-display", "none", "-qmp", "unix:%s,server,nowait" % SOCK,
                    "-daemonize", "-pidfile", PID], check=True)
    fails = []
    try:
        q = heapmap.Qmp(SOCK)

        def byte(s):
            return q.read(os88sym.linear(s), 1)[0]

        def word(s):
            b = q.read(os88sym.linear(s), 2)
            return b[0] | b[1] << 8

        t = time.time()
        while time.time() - t < 120 and byte("vmm_on") != 1:
            time.sleep(1)
        if byte("vmm_on") != 1:
            fails.append("vmm_on %d after 120s: the driver did not attach"
                         % byte("vmm_on"))
        if byte("mou_port") != 6:
            fails.append("mou_port %d, want 6: the backdoor did not win the "
                         "mouse" % byte("mou_port"))
        w, h = word("vid_w"), word("vid_h")
        if not fails:
            for px, py in ((100, 100), (w - 60, h - 50), (w // 2, h // 2)):
                qmp({"type": "abs", "data": {"axis": "x",
                     "value": round(px / (w - 1) * ABS_MAX)}},
                    {"type": "abs", "data": {"axis": "y",
                     "value": round(py / (h - 1) * ABS_MAX)}})
                t = time.time()
                while time.time() - t < 3 and (
                        abs(word("mouse_x") - px) > TOL or
                        abs(word("mouse_y") - py) > TOL):
                    time.sleep(0.1)
                gx, gy = word("mouse_x"), word("mouse_y")
                print("emusmoke: sent %d,%d  pointer %d,%d" % (px, py, gx, gy))
                if abs(gx - px) > TOL or abs(gy - py) > TOL:
                    fails.append("pointer at %d,%d, want ~%d,%d" % (gx, gy, px, py))
        subprocess.run([sys.executable, "tools/shot.py", SOCK, args.shot],
                       check=False)
    finally:
        try:
            os.kill(int(open(PID).read().strip()), 15)
        except Exception:
            pass
        for f in (SOCK, PID):
            if os.path.exists(f):
                os.unlink(f)
    for f in fails:
        print("emusmoke: FAIL %s" % f)
    if not fails:
        print("emusmoke: PASS - the driver attached and the pointer tracks; "
              "now LOOK at %s" % args.shot)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
