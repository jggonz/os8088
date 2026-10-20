#!/usr/bin/env python3
"""VIDEO.O88 waits out a monitor's re-sync after a REAL mode change, and
only then - SPEC.md 98.3.20.

    make && python3 tests/vidmsync.py

A two-second clip with sound, made here by tools/os88venc.py, goes on a
bootable XT-IDE disk with VIDEO.O88 and SOUND.DRV and is played full
screen. The disk is the point: it fills the player's ring in a few ticks,
as the XMS hold does on the owner's machine, so the fill does not hide the
wait the way a floppy's does.

  1. CGA4 on the CGA desktop (os8088_xt_hdd_sb): 320 x 200 over 640 x 200,
     a real mode change. The bracket must arm the wait ([vp_msync] 1 when
     vp_mwait is reached) and the play must start - [vp_t0], the tick its
     card opened and its first frame was due - VP_MSYNC ticks or more after
     the mode was set ([vp_mtk]). Broken on purpose (VP_MSYNC 0) it FAILS
     at 6 ticks, the fill alone being that short.
  2. HERCULES on the Hercules desktop (os8088_5150_herc_hdd_sb_gla): 720 x
     348 over 720 x 348, the same hardware mode, which no monitor sees -
     the wait must NOT be armed.

Both must play to the end with no error.
"""
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path[:0] = [HERE, os.path.join(ROOT, "tools")]
import os88build, os88marty, os88ui                           # noqa: E402
import os88geom as geom                                       # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

TEMPLATE = "build/martypc/run/media/hdds/default_xtide.vhd"
SKIP = 77


def u16(b, i=0):
    return struct.unpack_from("<H", b, i)[0]


RESYNC = 18             # SPEC.md 98.3.20's ~1 s, stated here and NOT read
                        # out of video.asm: a constant read back from the
                        # code under test passes whatever the code says


def clip(tmp, preset, name):
    src = os.path.join(tmp, "src.mkv")
    if not os.path.exists(src):
        subprocess.run(
            ["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
             "testsrc2=size=320x240:rate=15:duration=2", "-f", "lavfi", "-i",
             "sine=frequency=440:duration=2:sample_rate=22050",
             "-c:v", "ffv1", "-c:a", "pcm_s16le", "-shortest", src],
            check=True)
    out = os.path.join(tmp, name)
    subprocess.run(
        [sys.executable, "tools/os88venc.py", src, out, "--quiet",
         "--preset", preset, "--profile", "5150-xtide", "--fps", "15",
         "--rate", "11025", "--buffer-sound-ahead", "off"], check=True)
    return out


def leg(tmp, v88, machine, want, syms, bad):
    vhd = os.path.join(tmp, "%s.vhd" % machine)
    subprocess.run(
        [sys.executable, "tools/os88hdd.py", "--template", TEMPLATE,
         "--out", vhd, "--kernel", os88build.at("build/kernel.sys"),
         "--vbr", os88build.at("build/boothd.bin"),
         "--mbr", os88build.at("build/mbr.bin"),
         "--file", "HDD.DRV=" + os88build.at("build/hdd.drv"),
         "--file", "CTRL.DRV=" + os88build.at("build/ctrl.drv"),
         "--file", "SOUND.DRV=" + os88build.at("build/sound.drv"),
         "--file", "VIDEO.O88=" + os88build.at("build/video.o88"),
         "--file", "CLIP.V88=" + v88], check=True, capture_output=True)
    m = os88marty.launch(None, machine=machine,
                         extra=["--mount", "hd:0:" + vhd])
    try:
        ui = os88ui.UI(m)
        ui.ready(limit=240)
        w = ui.path("C:/CLIP.V88")
        rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
        base = u16(rec, geom.W_SEG) << 4

        def rb(n):
            return m.read(base + syms[n], 1)[0]

        def rw(n):
            return u16(m.read(base + syms[n], 2))

        def until(cond, what, guest=60.0):
            os88marty.until(m, lambda mm: cond(), what, poll=0.1,
                            limit=600.0, guest=guest)
        until(lambda: rb("vp_loaded") == 1, "the clip's header")
        if rb("vp_ok") != 1:
            bad.append("%s: the player will not play the clip" % machine)
            return
        armed = []
        with os88marty.bp_trace(m, base + syms["vp_mwait"],
                                on_hit=lambda mm, r: armed.append(
                                    (rb("vp_msync"), rw("vp_mtk")))) as tr:
            m.write(base + syms["vp_nowin"], b"\1")    # (the full screen)
            m.write(base + syms["vp_played"], b"\0")
            m.type_text("p")
            tr.until(lambda: bool(armed), "the play's start", 600)
        until(lambda: rb("vp_ready") == 1, "the play to start")
        msync, mtk = armed[0]
        t0 = rw("vp_t0")
        until(lambda: rb("vp_played") == 1 and rb("vp_ready") == 0,
              "the play to end", 120.0)
        gap = (t0 - mtk) & 0xFFFF
        print("   %-32s winm %d, wait armed %d, mode set at tick %d, play "
              "at %d: %d ticks; %d frames, error %d"
              % (machine, rb("vp_winm"), msync, mtk, t0, gap, rw("vp_done"),
                 rb("vp_err")))
        if rb("vp_winm"):
            bad.append("%s: the play was in the window" % machine)
        if rb("vp_err") or rw("vp_done") == 0:
            bad.append("%s: the play drew %d frames, error %d"
                       % (machine, rw("vp_done"), rb("vp_err")))
        if want:
            if msync != 1:
                bad.append("%s: a real mode change and no wait armed"
                           % machine)
            elif gap < RESYNC:
                bad.append("%s: the play started %d ticks after the mode "
                           "change, not %d" % (machine, gap,
                                               RESYNC))
        elif msync:
            bad.append("%s: the same hardware mode and the wait armed"
                       % machine)
    finally:
        m.quit()


def main():
    import shutil
    if not shutil.which("ffmpeg"):
        print("   SKIP: needs ffmpeg")
        return SKIP
    os.chdir(ROOT)
    syms, image = pkg_syms("apps/video/video.asm", ("apps/",))
    if open(os88build.at("build/video.bin"), "rb").read() != image:
        sys.exit("vidmsync: build/video.bin is behind the tree - run `make`")
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        leg(tmp, clip(tmp, "cga4-small", "CGA.V88"), "os8088_xt_hdd_sb",
            True, syms, bad)
        leg(tmp, clip(tmp, "herc", "HERC.V88"),
            "os8088_5150_herc_hdd_sb_gla", False, syms, bad)
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("\n   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
