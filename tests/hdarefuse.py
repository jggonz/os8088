#!/usr/bin/env python3
"""An unsupported HDA codec refuses promptly instead of freezing boot."""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import heapmap                                              # noqa: E402
import os88sym                                              # noqa: E402
import os88qemu                                             # noqa: E402

SOCK = os.path.join(ROOT, "build", "hdarefuse.sock")
PIDFILE = os.path.join(ROOT, "build", "hdarefuse.pid")
DRVR_SIZE = 16
HDAREC_SEG = 2
DRVR_WANT = 13
DRVR_ERR = 14
HDA_ROW = 6
DRVE_HW = 5


def main():
    os88qemu.kill(PIDFILE, SOCK, wait=0)
    subprocess.run(["make", "-s", "hda-refuse-test"], cwd=ROOT,
                   check=True, stdout=subprocess.DEVNULL)
    em = "qemu" + "-system-i386"
    subprocess.run(
        em + " -machine pc,vmport=off"
        " -drive file=build/hdarefuse.img,format=raw,if=floppy -boot a"
        " -serial none -display none"
        " -audiodev none,id=snd -device intel-hda,addr=0x1b"
        " -device hda-output,audiodev=snd"
        " -qmp unix:%s,server,nowait -daemonize -pidfile %s" % (SOCK, PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)
    q = heapmap.Qmp(SOCK)
    for _ in range(200):
        try:
            q.hmp("info status")
            break
        except OSError:
            time.sleep(0.05)

    # snd_init follows drv_boot. If the HDA timeout behaves like the reported
    # freeze, this byte never becomes live within the deliberately short bound.
    deadline = time.time() + 12
    live = 0
    while time.time() < deadline:
        live = q.read(os88sym.linear("snd_live"), 1)[0]
        if live == 1:
            break
        time.sleep(0.05)

    fails = []
    if live != 1:
        fails.append("snd_live did not reach 1 in 12s: HDA attach blocked boot")
    row = os88sym.linear("drv_tab") + HDA_ROW * DRVR_SIZE
    rec = q.read(row, DRVR_SIZE)
    seg = rec[HDAREC_SEG] | rec[HDAREC_SEG + 1] << 8
    if rec[DRVR_WANT] != 1:
        fails.append("HDA row was not requested by SYSTEM.CFG")
    if seg != 0 or rec[DRVR_ERR] != DRVE_HW:
        fails.append("HDA refusal was seg=%04x err=%d, want seg=0000 err=%d"
                     % (seg, rec[DRVR_ERR], DRVE_HW))

    if fails:
        print("hdarefuse: FAILED\n  " + "\n  ".join(fails))
        return 1
    print("hdarefuse: unsupported codec refused and boot completed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
