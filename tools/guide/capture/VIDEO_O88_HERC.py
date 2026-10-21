"""Video Player playing its Hercules demo videos, for
tools/guide/manuals/VIDEO_O88.md.

    python3 tools/guide/capture/run.py VIDEO_O88_HERC

The demo videos are megabytes each, more than any floppy, so they ride a
HARD DISK. Loading this script builds two scratch disks in build/guide/scratch/
(rebuilt every run, from files already in the tree):

  vidherc.vhd   a formatted (not bootable) XT-IDE fixed disk, made by
                tools/os88hdd.py --noboot over MartyPC's own default_xtide.vhd,
                holding apps/video/demo/herc/{03-PCM11,04-ADPCM,06-ADPCM,
                08-LIVE}.V88 and build/video.o88 in its root
  vidsys360.img build/os8088-360.img plus a SYSTEM.CFG that wants HDD.DRV,
                so the floppy boot mounts that disk as C:

and wraps os88ui.boot so run.py's one boot uses them: the system disk above
in place of the default one, the VHD mounted as hd:0, and - when run.py's
default machine is asked for - the Hercules 5150 with a hard disk and a Sound
Blaster (os8088_5150_herc_hdd_sb_gla). build/apps360.img stays in B: for the
Calculator. With OS88_BUILD set (MANUALS.md, "When a capture will not boot")
the system disk and the player come from that tree. No SOUND.DRV is wanted, so the plays are silent: pictures only.
"""

import os
import shutil
import subprocess
import sys

import os88marty
import os88ui

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
SCRATCH = os.path.join(ROOT, "build", "guide", "scratch")
TEMPLATE = os.path.join(ROOT, "build", "martypc", "run", "media", "hdds",
                        "default_xtide.vhd")
DEMO = os.path.join(ROOT, "apps", "video", "demo")
BUILD = os.environ.get("OS88_BUILD") or os.path.join(ROOT, "build")
SYS = os.path.join(SCRATCH, "vidsys360.img")
RUN_DEFAULT = "os8088_xt_vga"           # run.py's --machine default
HDD_CFGBIT = 1                          # kernel/driver.inc's drv_cfgbit: HDD

MACHINE = "os8088_5150_herc_hdd_sb_gla"
VIDEOS = ("herc/03-PCM11.V88", "herc/04-ADPCM.V88", "herc/06-ADPCM.V88",
          "herc/08-LIVE.V88")
VHD = os.path.join(SCRATCH, "vidherc.vhd")


def make_disks(vhd, videos):
    """Build `vhd` (the videos and the player in its root) and SYS."""
    os.makedirs(SCRATCH, exist_ok=True)
    cmd = [sys.executable, os.path.join(ROOT, "tools", "os88hdd.py"),
           "--template", TEMPLATE, "--out", vhd, "--noboot",
           "--file", "VIDEO.O88=" + os.path.join(BUILD, "video.o88")]
    for v in videos:
        cmd += ["--file", "%s=%s" % (os.path.basename(v),
                                     os.path.join(DEMO, v))]
    subprocess.run(cmd, check=True, capture_output=True)
    shutil.copyfile(os.path.join(BUILD, "os8088-360.img"), SYS)
    cfg = os.path.join(SCRATCH, "SYSTEM.CFG")
    with open(cfg, "wb") as f:              # (tests/vidhdmake.py's bytes)
        f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
                bytes([1, 2]) + (1 << HDD_CFGBIT).to_bytes(2, "little") +
                b"\0\0")
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "os88fat.py"),
                    "add", SYS, cfg, "SYSTEM.CFG"], check=True,
                   capture_output=True)


def hook_boot(vhd, machine):
    """Point run.py's os88ui.boot at SYS, `vhd` as hd:0, and `machine`."""
    orig = os88ui.boot

    def boot(image, apps=None, **kw):
        if os.path.basename(image) == "os8088-360.img":
            image = SYS
        m = kw.pop("machine", None)
        if m in (None, RUN_DEFAULT):
            m = machine
        kw["extra"] = list(kw.get("extra", ())) + ["--mount", "hd:0:" + vhd]
        return orig(image, apps=apps, machine=m, **kw)
    os88ui.boot = boot


def park(ui, x=None, y=None):
    """The pointer somewhere it covers nothing that is being shot."""
    ui.mo.to(x if x is not None else ui.m.fbuf()[0] - 4,
             y if y is not None else ui.m.fbuf()[1] - 4)


def open_video(ui, name, wait=4.0):
    """Open C:/NAME in the Video Player and hand back its window (whose
    title is the file's own, so it is not looked up by name)."""
    w = ui.path("C:/" + name)
    os88marty.guest_sleep(ui.m, wait)       # the header and the poster
    park(ui)
    ui.settle(limit=300.0)
    return w


def place(ui, w, x, y):
    """Move a window if the window manager takes the drag; a picture with the
    window where it opened is still a picture."""
    for _ in range(2):
        try:
            ui.move_window(w, x, y)
            return
        except os88ui.UIError as e:
            print("   (move of %r did not take: %s)" % (w.title, str(e)[:80]))
            os88marty.guest_sleep(ui.m, 2.0)


def stop(ui, w):
    """Esc ends the play (back to the window from the full screen); then
    close the player."""
    ui.m.key("Escape")
    os88marty.guest_sleep(ui.m, 4.0)
    ui.settle(limit=300.0)
    ui.close(w)


if not globals().get("SKIP_SETUP"):    # (VIDEO_O88_CGA.py borrows the helpers)
    make_disks(VHD, VIDEOS)
    hook_boot(VHD, MACHINE)


def capture(ui, shot):
    # 1. Star Wars, 600 x 165: plays in its window
    w = open_video(ui, "04-ADPCM.V88")
    place(ui, w, 56, 60)
    park(ui)
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 14.0)
    shot("video-herc-window", settle=False)
    stop(ui, w)

    # 2. a 720 x 261 video is too big for a window, so Play is full screen
    w = open_video(ui, "06-ADPCM.V88")       # Sin City
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 12.0)
    shot("video-herc-full", settle=False)
    stop(ui, w)
    w = open_video(ui, "03-PCM11.V88")       # Bad Carrot, as an alternative
    ui.m.type_text(" ")
    os88marty.guest_sleep(ui.m, 40.0)
    shot("video-herc-full-alt", settle=False)
    stop(ui, w)

    # 3. Steamboat Willie, Live: on the desktop, beside a Calculator. The
    # Live player reads the whole file into memory and a drag can miss it
    # while it does (place() carries on: the picture still shows the play)
    w = open_video(ui, "08-LIVE.V88", wait=8.0)
    place(ui, w, 40, 50)
    ui.path("B:/APPS/CALC.O88")
    ui.m.type_text("12*3=")
    place(ui, ui.window("Calculator"), 400, 70)
    try:
        ui.raise_window(w)
    except os88ui.UIError as e:
        print("   (raise did not take: %s)" % str(e)[:80])
    park(ui, 700, 330)
    ui.m.type_text(" ")                      # plays on the desktop
    os88marty.guest_sleep(ui.m, 12.0)
    shot("video-live", settle=False)
