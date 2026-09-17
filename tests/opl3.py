#!/usr/bin/env python3
"""SOUND.DRV tells an OPL3 from an OPL2, and FM still sounds on both (SPEC.md 34.11).

    make build/fmtest.img build/fmtest360.img && python3 tests/opl3.py
    python3 tests/opl3.py --qemu        # one arm only
    python3 tests/opl3.py --marty

FOUR MACHINES, and each is somebody's answer to SPEC.md 34.11.1's probe:

  QEMU  ADLIB=1   an OPL2 that ANSWERS THE STATUS MASK LIKE AN OPL3 (MAME's old
                  fmopl.c never sets status bits 1-2) and aliases 38Ah onto
                  388h. The probe's question A (NEW = 1, the timer must not
                  run) is what calls it an OPL2, and this arm is that
                  question's gate: SND_CAP_OPL3 clear, [opl_is3] = 0.
  MartyPC         os8088_5150_sb_gla, whose Nuked-OPL3 card decodes 38Ah/38Bh
                  as the second array (patch 05): SND_CAP_OPL3 set, [opl_is3]
                  = 1 - on an 8088, through the whole attach.
  MartyPC OPL2    the same machine with MARTYPC_OPL2=1: status bits 1-2 SET,
                  so the mask alone answers OPL2 and the probe touches no 38xh
                  port. SND_CAP_OPL3 clear.
  MartyPC NO38A   MARTYPC_NO38A=1: the OPL3 status byte and NOTHING at
                  38Ah/38Bh - the stock pin's card. Question A's writes vanish
                  and its timer "does not run", which a probe trusting that
                  absence reads as OPL3; question B (NEW = 0, the timer MUST
                  run through 38Ah) is what calls it an OPL2, and this arm is
                  B's gate: SND_CAP_OPL3 clear. The previous probe, A alone,
                  read this card as an OPL3.

On every one of them FMTEST's first click - verb 2 patch-load with a MULT=2
carrier, verb 0 note-on at 440 Hz - must SOUND at 880 Hz (tools/sndcheck.py),
which is what "every existing FM user is unchanged" means once the attach
has written 217 more registers on an OPL3. And [drv_svc+DSV_TICK] must read 0
after those successful FM verbs: both are SPEC.md 34.13.7 sites, so the kernel
took DX from each, and an FM-only machine has nothing that wants the tick.
"""
import argparse
import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import dispcp                                               # noqa: E402
import os88build                                            # noqa: E402
import os88qemu                                             # noqa: E402
import os88sym                                              # noqa: E402
from sndtick import drv_syms, u16                           # noqa: E402

S = os88sym.linear
PIDFILE = os.path.join(ROOT, "build", "qemu.pid")
SOCK = os.path.join(ROOT, "build", "qmp.sock")
DSV_CAPS, DSV_TICK = 0, 6
SND_CAP_FM, SND_CAP_OPL3 = 0x02, 0x20
FMHZ = 880

fails = []


def check(name, cond, note=""):
    print("  %-4s %s%s" % ("ok" if cond else "FAIL", name,
                           ("  " + note) if note else ""))
    sys.stdout.flush()
    if not cond:
        fails.append(name)


def facts(m, D, label, want3):
    seg = u16(m.read(S("drv_tab") + 2, 2))
    if not seg:
        check("%s: SOUND.DRV is loaded" % label, False, "drv_tab row 0 empty")
        return None
    caps = u16(m.read(S("drv_svc") + DSV_CAPS, 2))
    is3 = m.read((seg << 4) + D["opl_is3"], 1)[0]
    check("%s: an FM sink attached" % label, caps & SND_CAP_FM,
          "DSV_CAPS %04x" % caps)
    check("%s: %s" % (label, "SND_CAP_OPL3 published" if want3 else
                      "SND_CAP_OPL3 NOT published"),
          bool(caps & SND_CAP_OPL3) == want3 and is3 == int(want3),
          "DSV_CAPS %04x, [opl_is3] %d" % (caps, is3))
    return seg


def sounds(label, wav):
    r = subprocess.run(["python3", "tools/sndcheck.py", wav, str(FMHZ)],
                       capture_output=True, text=True)
    last = (r.stdout.strip().splitlines() or [r.stderr.strip()])[-1]
    check("%s: FMTEST's patched 440 Hz note sounds at %d Hz" % (label, FMHZ),
          r.returncode == 0, last)


# --- QEMU -------------------------------------------------------------------
def arm_qemu():
    print("QEMU ADLIB=1 - an OPL2 that passes the status mask")
    from ethernet import Qemu, Mouse
    D = drv_syms()
    wav = os.path.join(ROOT, "build", "opl3-qemu.wav")
    if os.path.exists(wav):
        os.remove(wav)
    os88qemu.kill(PIDFILE, SOCK)
    em = "qemu" + "-system-i386"        # never whole: tests/os88qemu.py
    subprocess.run(
        em + " -machine pc,vmport=off"
        " -drive file=build/os8088.img,format=raw,if=floppy -boot a"
        " -drive file=%s,format=raw,if=floppy,index=1"
        " -chardev msmouse,id=m0 -serial chardev:m0"
        " -audiodev wav,id=snd,path=%s -machine pcspk-audiodev=snd"
        " -device adlib,audiodev=snd"
        " -display none -qmp unix:%s,server,nowait -daemonize -pidfile %s"
        % (os88build.at("build/fmtest.img"), wav, SOCK, PIDFILE),
        cwd=ROOT, shell=True, check=True)
    os88qemu.own(PIDFILE, SOCK)
    m = Qemu(SOCK)
    t0 = time.time()
    while time.time() - t0 < 90:
        if u16(m.read(S("drv_tab") + 2, 2)) and m.read(S("spl_live"), 1)[0] == 0:
            break
        time.sleep(0.5)
    time.sleep(5)
    facts(m, D, "QEMU", False)
    mo = Mouse()
    settle = lambda *a, **k: time.sleep(2.0)            # noqa: E731
    dispcp.open_drive(m, mo, S, settle, "B")
    disk = dispcp.win_list(m, S)[-1]
    wx, wy = dispcp.win_rect(m, S, disk)[:2]
    before = set(dispcp.win_list(m, S))
    dispcp.open_named(m, mo, S, settle, wx, wy, "FMTEST.O88")
    time.sleep(3)
    new = [w for w in dispcp.win_list(m, S) if w not in before]
    if not new:
        check("QEMU: FMTEST opened", False)
        m.quit()
        return
    x, y, w, h = dispcp.win_rect(m, S, new[-1])
    mo.click(x + w // 2, y + h // 2)
    time.sleep(2.5)
    k = u16(m.read(S("drv_svc") + DSV_TICK, 2))
    check("QEMU: DSV_TICK is 0 after two successful FM verbs", k == 0,
          "kernel %04x" % k)
    m.quit()
    time.sleep(1.5)
    sounds("QEMU", wav)


# --- MartyPC ----------------------------------------------------------------
KNOBS = ("MARTYPC_OPL2", "MARTYPC_NO38A")


def arm_marty(knob=None):
    import os88marty as M
    from os88mouse import Mouse
    label = {None: "MartyPC", "MARTYPC_OPL2": "MartyPC OPL2",
             "MARTYPC_NO38A": "MartyPC NO38A"}[knob]
    want3 = knob is None
    print("%s - os8088_5150_sb_gla%s" % (label, ", %s=1" % knob if knob
                                         else ", patch 05's OPL3"))
    D = drv_syms()
    prefix = os.path.join(ROOT, "build", "opl3-marty%s" % {
        None: "", "MARTYPC_OPL2": "2", "MARTYPC_NO38A": "n"}[knob])
    for f in glob.glob(prefix + ".*"):
        os.remove(f)
    os.environ["MARTYPC_WAV"] = prefix
    for k in KNOBS:
        os.environ.pop(k, None)
    if knob:
        os.environ[knob] = "1"
    try:
        with M.launch("build/os8088-360.img",
                      apps=os88build.at("build/fmtest360.img"),
                      machine="os8088_5150_sb_gla", boot=False) as m:
            m.run()
            M.settle(m, gate=M.desktop_up)
            M.no_saver(m)
            facts(m, D, label, want3)
            mo = Mouse(marty=m)
            dispcp.open_drive(m, mo, S, M.settle, "B")
            disk = dispcp.win_list(m, S)[-1]
            wx, wy = dispcp.win_rect(m, S, disk)[:2]
            win = dispcp.open_named(m, mo, S, M.settle, wx, wy, "FMTEST.O88")
            M.settle(m)
            x, y, w, h = dispcp.win_rect(m, S, dispcp.win_list(m, S)[-1])
            mo.click(x + w // 2, y + h // 2)
            m.run()
            time.sleep(4.0)
            M.settle(m)
            k = u16(m.read(S("drv_svc") + DSV_TICK, 2))
            check("%s: DSV_TICK is 0 after two successful FM verbs" % label,
                  k == 0, "kernel %04x (window %r)" % (k, win))
            m.pause()
    finally:
        os.environ.pop("MARTYPC_WAV", None)
        for k in KNOBS:
            os.environ.pop(k, None)
    time.sleep(1.0)
    wavs = [f for f in glob.glob(prefix + ".*") if "adlib" in f]
    if not wavs:
        check("%s: an AdLib capture was written" % label, False,
              "none of %s" % glob.glob(prefix + ".*"))
        return
    sounds(label, wavs[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu", action="store_true")
    ap.add_argument("--marty", action="store_true")
    a = ap.parse_args()
    both = not (a.qemu or a.marty)
    try:
        if a.qemu or both:
            arm_qemu()
        if a.marty or both:
            arm_marty()
            arm_marty("MARTYPC_OPL2")
            arm_marty("MARTYPC_NO38A")
    finally:
        os88qemu.kill(PIDFILE, SOCK)
    print("opl3: %s" % ("FAILED: " + ", ".join(fails) if fails else
                        "QEMU reads OPL2, MartyPC OPL3, OPL2 and (no 38Ah) "
                        "OPL2, and FM sounds at 880 Hz on all four"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
