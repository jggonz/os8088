#!/usr/bin/env python3
"""VIDEO.O88 plays 22,050 Hz through the PC speaker on a 286 or better -
SPEC.md 34.11.8.

    make && python3 tests/vidspkat.py

WHY QEMU: docs/TESTING.md's closed list, entry 1. The door's shorter pulse
(48..73 PIT counts) is a 286-and-up thing and every MartyPC machine is an
8088 - where vidspk's `--rate 22050` leg is the other half, the REFUSAL.
Nothing here is timed, and QEMU's speaker cannot sound a pulse width at all,
so what is asserted is the machine's own state, read off the guest:

  1. the clip - 22,050 Hz PCM8 as speaker counts, N = 54 - opens UNMUTED
     (only an 8086-class CPU mutes past VP_SPKMAX) and its sound goes to
     the SPEAKER ([vp_snd] = 2, no card here);
  2. mid-play, the door is OPEN (the kernel's spk_seg names the player's
     segment) and the rate divisor it set is a whole number of 54-count
     pulses - the door took N = 54;
  3. the ring is played from: its CONS word moves while the play runs;
  4. every frame is drawn, and the kernel is left as it was (channel 2
     nobody's, no sample ISR).

Broken on purpose - os88spk_init's `cmp al, 48` back to 74 in apps/os88spk.inc
(the door no longer checks N: SPEC.md 34.11.1) - the library refuses N = 54
and step 1's [vp_snd] is not 2.
"""
import atexit
import math
import os
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
import os88qemu                                             # noqa: E402
import os88build                                            # noqa: E402
import os88geom as geom                                     # noqa: E402
import os88ui                                               # noqa: E402
import os88vid as vid                                       # noqa: E402
import xmcheck                                              # noqa: E402
from cycweb import pkg_syms                                 # noqa: E402

RATE, FPS, SECS = 22050, 15.0, 6.0
VP_RL = 16384                   # apps/video/video.asm's ring


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


def clip(tmp):
    """a 40 x 100 Hercules canvas moving a block, and a 22,050 Hz sweep made
    as the speaker's counts"""
    nf = int(SECS * FPS)
    wb, h = 40, 100
    paths = []
    for f in range(nf):
        cv = bytearray(wb * h)
        x, y = (f * 2) % (wb - 8), (f * 3) % (h - 24)
        for r in range(24):
            cv[(y + r) * wb + x:(y + r) * wb + x + 8] = b"\xFF" * 8
        p = os.path.join(tmp, "f%04d.pbm" % f)
        vid._write_pbm(p, wb, h, bytes(cv))
        paths.append(p)
    n = int(RATE * nf / FPS)
    audio, ph = bytearray(), 0.0
    for i in range(n):
        ph += 2 * math.pi * (300.0 + 2000.0 * i / n) / RATE
        audio.append(128 + int(100 * math.sin(ph)))
    wav = os.path.join(tmp, "a.wav")
    vid._write_wav(wav, RATE, bytes(audio))
    out = os.path.join(tmp, "CLIP.V88")
    vid.encode_frames(paths, out, FPS, wav, "herc", "vidspkat clip",
                      audio_fmt=vid.AUD_PCM8, spk=True)
    vid.verify_v88(out)
    return out


class Q(object):
    """One private QEMU (vidxms.py's): its own socket, pidfile and disks."""

    def __init__(self, tmp, sysimg, apps):
        self.sock = os.path.join(tmp, "qmp.sock")
        self.pid = os.path.join(tmp, "qemu.pid")
        self.pidn = None
        cmd = ["qemu-system-i386", "-machine", "pc,vmport=off", "-m", "16",
               "-drive", "file=%s,format=raw,if=floppy" % sysimg,
               "-boot", "a", "-chardev", "msmouse,id=m0",
               "-serial", "chardev:m0",
               "-drive", "file=%s,format=raw,if=floppy,index=1" % apps,
               "-display", "none",
               "-qmp", "unix:%s,server,nowait" % self.sock,
               "-daemonize", "-pidfile", self.pid]
        atexit.register(self.close)
        signal.signal(signal.SIGTERM, lambda n, f: sys.exit(128 + n))
        subprocess.run(cmd, check=True)
        os88qemu.own(self.pid, self.sock)   # (and by the PID it wrote,
        self.pidn = int(open(self.pid).read().strip())  # below: `tmp`
                                        # goes before atexit, and the
                                        # pidfile with it - vidxms.py's)

    def close(self):
        if self.pidn:
            try:
                os.kill(self.pidn, signal.SIGTERM)
                os88qemu.gone(self.pidn)
            except OSError:
                pass
            self.pidn = None

    def hmp(self, *cmds):
        return xmcheck.qmp(self.sock, *cmds)

    def read(self, linear, n):
        return bytes(xmcheck.read_bytes(self.sock, linear, n))

    def readseg(self, seg, off, n):
        return self.read((seg << 4) + off, n)


def main():
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    pkg = os88build.at("build/video.o88")
    sysimg0 = os88build.at("build/os8088.img")
    for p in (pkg, sysimg0):
        if not os.path.exists(p):
            sys.exit("vidspkat: no %s - run `make`" % p)
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        v88 = clip(tmp)
        r = vid.Reader(v88)
        n = 1193182 // RATE
        print("   clip: %d frames, %d Hz, N = %d" % (r.frames, r.rate, n))
        disk = os.path.join(tmp, "spk.img")
        sysimg = os.path.join(tmp, "sys.img")
        shutil.copy(sysimg0, sysimg)
        subprocess.run([sys.executable, "tools/os88disk.py", "-o", disk,
                        "--size", "1440", v88, pkg], check=True,
                       capture_output=True)
        q = Q(tmp, sysimg, disk)
        for _ in range(150):
            if os.path.exists(q.sock):
                break
            time.sleep(0.2)
        xmcheck.wait_desktop(q.sock, "vidspkat")
        S = xmcheck.sym
        ui = os88ui.UI(q, mouse=object(), sym=S, verbose=False)

        def wait(cond, what, secs):
            if not os88qemu.acted(q, cond, secs=secs, what=what, poll=0.05):
                raise SystemExit("vidspkat: timed out waiting for %s" % what)

        x, y = geom.drive_pt(q, "B", S)
        xmcheck.dblclick(q.sock, x, y)
        box = {}

        def diskwin():
            for w in ui.windows():
                if w.visible and ui.fs_of(w) == (1, 0, 1):
                    box["w"] = w
                    return True
            return False
        wait(diskwin, "drive B's window", 30)
        os88qemu.pace(q, 1)
        w = box["w"]
        i, _ = ui.entry("CLIP.V88", w)
        before = set(o.i for o in ui.windows())
        xmcheck.dblclick(q.sock, *ui.row_xy(w, i - ui.scroll(w)))

        def player():
            for o in ui.windows():
                if o.i not in before and o.visible:
                    box["p"] = o
                    return True
            return False
        wait(player, "the Video Player's window", 30)
        rec = q.read(S("wm_wins") + box["p"].i * geom.WIN_SIZE, geom.WIN_SIZE)
        seg = u16(rec, geom.W_SEG)
        base = seg << 4

        def rb(k):
            return q.read(base + syms[k], 1)[0]

        def rw(k):
            return u16(q.read(base + syms[k], 2))

        wait(lambda: rb("vp_loaded") == 1, "the clip's header", 30)
        mw = (rb("vp_mute"), rb("vp_mwhy"))
        print("   1: opened muted %d, why %d" % mw)
        if mw != (0, 0):
            bad.append("1: a 286-or-better opened it muted (%d, %d)" % mw)
        q.hmp("sendkey p")
        wait(lambda: rb("vp_ready") == 1, "the play to start", 30)
        wait(lambda: rw("vp_done") >= 10, "ten frames", 60)
        snd = rb("vp_snd")
        isrseg = u16(q.read(S("spk_seg"), 2))
        rdiv = u16(q.read(S("sch_rdiv"), 2))
        aseg = rw("vp_aseg")
        c0 = u16(q.read((aseg << 4) + VP_RL + 2, 2)) if aseg else 0
        wait(lambda: rw("vp_done") >= 30, "thirty frames", 60)
        c1 = u16(q.read((aseg << 4) + VP_RL + 2, 2)) if aseg else 0
        print("   1: the sound went to %s ([vp_snd] %d)"
              % ({2: "THE SPEAKER", 0: "nothing"}.get(snd, "?"), snd))
        if snd != 2:
            bad.append("1: the sound is not the speaker's ([vp_snd] %d)" % snd)
        print("   2: the door %s (spk_seg %04x, the player's %04x);"
              " the rate divisor %d = %d pulses of %d%s"
              % ("OPEN" if isrseg == seg else "not the player's", isrseg,
                 seg, rdiv, rdiv // n, n,
                 "" if rdiv % n == 0 else " AND %d over" % (rdiv % n)))
        if isrseg != seg:
            bad.append("2: the door is not open to the player")
        if rdiv % n:
            bad.append("2: the divisor %d is not whole %d-count pulses"
                       % (rdiv, n))
        moved = (c1 - c0) & 0xFFFF
        print("   3: the ring's CONS moved %d samples over twenty frames"
              % moved)
        if moved < 1000:
            bad.append("3: the ring was not played from (%d)" % moved)
        wait(lambda: rb("vp_ready") == 0 and rb("vp_played") == 1,
             "the play to end", 120)
        st = (rw("vp_done"), rw("vp_stall"), rb("vp_err"))
        ch2 = q.read(S("snd_ch2mode"), 1)[0]
        isr = u16(q.read(S("spk_seg"), 2))
        print("   4: drew %d of %d, stalls %d, error %d; after it channel 2's "
              "owner mode %d, the sample ISR's segment %04x"
              % (st[0], r.frames, st[1], st[2], ch2, isr))
        if st[0] != r.frames or st[2]:
            bad.append("4: the play (drew %d, error %d)" % (st[0], st[2]))
        if ch2 or isr:
            bad.append("4: the kernel was left with the speaker taken")
        q.close()
    for b in bad:
        print("  - " + b)
    print("\nvidspkat: %s" % ("FAIL" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
