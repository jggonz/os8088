#!/usr/bin/env python3
"""THE ENCODER WINDOW'S HARD DISK BOOTS AND PLAYS - SPEC.md 98.2.12.1.

    make && python3 tests/vidhdmake.py [--machine NAME]

"...and make a disk of it" in tools/os88vencgui.py offers three BOOTABLE
hard disks beside the four floppies: an ST-225 on an ST11M (615/4/17), an
ST-238R on an ST11R (615/4/26) and IDE (250/15/17). tests/vencguitest.py
checks all three are what they say - a fixed VHD of that geometry,
os88disk --verify-hdd clean, the kernel, HDD.DRV, VIDEO.O88 and the video
in the root. This row BOOTS one.

MartyPC's only fixed-disk controller is XT-IDE, which boots neither an
ST11's layout (the card hides cylinder 0 behind its own record) nor 15
heads; both of those are the geometries the owner's machines boot on
86Box and on the 5150. So the disk here is the window's OWN command line -
disk_argv for the IDE entry, every file it names - with only the geometry
put to the one MartyPC's XT-IDE takes (615/4/26, no ST11 layout): the files,
the footer os88hdd now writes itself (no template on a machine that has
never built MartyPC) and the 8.3 name a long one is cut to.

1. IT BOOTS: the desktop comes up off C:, with no floppy at all.
2. THE VIDEO OPENS: C:/<its 8.3 name> opens the Video Player, and the
   player reads its header and draws its poster off the disk.

...and the disk the window makes with NO os8088 tree - the encoder handed
to people who have none - which is formatted and does not boot:
3. IT MOUNTS AND PLAYS: the shipped 360 KB system floppy, with a SYSTEM.CFG
   that wants the hard-disk driver (the Control Panel tick, saved), boots in
   A: with the apps floppy in B:. HDD.DRV comes up and mounts the disk as C:
   (SPEC.md 52.4). Then C:/<its 8.3 name> opens the Video Player and its
   poster is read off the hard disk. The disk carries no player of its own
   and B: is never opened first: the association LOOKS for VIDEO.O88 across
   the drives (SPEC.md 54.4.3), which is what makes this disk usable with
   nothing but the shipped floppies. (Before 54.4.3 it failed here unless
   the apps floppy had been opened.)
4. IT SAYS IT DOES NOT BOOT: alone in the machine, the ROM runs its MBR,
   which prints "Not a bootable disk" on the screen.

Broken on purpose, each on its own:
- the unbootable disk's MBR without its 55AA signature: 3 FAILS (HDD.DRV
  finds no partition table, so there is no C:) and so does 4;
- VIDEO.O88 left off the window's disk (player_path answering None): 2
  FAILS:
the file has no program to open it. (HDD.DRV taken out instead FAILS
NOTHING, and that is not the row missing it: the kernel mounts its own boot
partition through the ROM's int 13h, so a disk behind an option ROM reaches
C: without the driver. The disk carries it for the partitions it does not
boot from, as `make videnchd`'s do.)
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M, os88ui, os88build, os88vid as vid, os88geom as geom  # noqa: E402
import os88vencgui as G                                       # noqa: E402
from cycweb import pkg_syms                                   # noqa: E402

MARTY_GEOM = ("615", "4", "26")     # what MartyPC's XT-IDE boots
HDD_CFGBIT = 1                      # kernel/driver.inc's drv_cfgbit, row 1


def play(ui, m, syms, name, q, bad):
    """C:/NAME opens the Video Player, and its poster is read off C:"""
    try:
        w = ui.path("C:/" + name)
    except Exception as e:
        bad.append("%s: C:/%s did not open: %s"
                   % (q, name, str(e).split(".")[0]))
        raise
    rec = m.read(ui._S("wm_wins") + w.i * geom.WIN_SIZE, geom.WIN_SIZE)
    base = struct.unpack_from("<H", rec, geom.W_SEG)[0] << 4
    rw = lambda n: struct.unpack_from("<H", m.read(base + syms[n], 2))[0]
    M.until(m, lambda mm: rw("vp_ploads") >= 1, "the poster",
            poll=0.3, limit=600.0, guest=60.0)
    print("   %s: C:/%s opened %r and its poster was read off the disk"
          % (q, name, w.title))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--machine", default="os8088_5150_herc_hdd_sb_gla")
    a = ap.parse_args()
    os.chdir(ROOT)
    syms, _ = pkg_syms("apps/video/video.asm", ("apps/",))
    bad = []
    with tempfile.TemporaryDirectory(dir=os.path.join(ROOT, "build")) as tmp:
        g = vid.Geom(vid.LAY_HERC, 40, 100)
        cvs = [bytes(((x * 5 + y * 3 + f) & 0xFF) for y in range(100)
                     for x in range(40)) for f in range(8)]
        v88 = os.path.join(tmp, "My Own Video, Encoded.V88")
        vid.encode_canvases(cvs, g, v88, 15.0, title="hd", keysecs=1.0)
        ide = [d[1] == "hd" and d[2][0] == "IDE" for d in G.DISKS].index(True)
        miss = G.hd_missing(os88build.at("build"))
        if miss:
            print("   SKIP: build/ has no %s" % ", ".join(miss))
            return 0
        cmd, img = G.disk_argv(v88, ide, out=os.path.join(tmp, "V.VHD"),
                               build=os88build.at("build"))
        for flag, val in zip(("--cyls", "--heads", "--spt"), MARTY_GEOM):
            cmd[cmd.index(flag) + 1] = val
        p = subprocess.run(cmd, capture_output=True, text=True)
        print("   the window's disk: %s" % (p.stdout.strip() or
                                             p.stderr.strip()))
        if p.returncode:
            print("   FAIL: os88hdd refused it")
            return 1
        name = G.short83(v88)
        m = M.launch(None, machine=a.machine,
                     extra=["--mount", "hd:0:" + img])
        try:
            ui = os88ui.UI(m)
            try:
                ui.ready(limit=300)
                print("   1: the desktop is up, off the hard disk")
            except Exception as e:
                bad.append("1: no desktop off the disk: %s"
                           % str(e).split(".")[0])
                raise
            play(ui, m, syms, name, "2", bad)
        except Exception:
            pass
        finally:
            m.close()
        # --- 3, 4: the disk the window makes with no tree
        empty = os.path.join(tmp, "notree")
        os.makedirs(empty)
        cmd, img = G.disk_argv(v88, ide, out=os.path.join(tmp, "NB.VHD"),
                               build=empty)
        for flag, val in zip(("--cyls", "--heads", "--spt"), MARTY_GEOM):
            cmd[cmd.index(flag) + 1] = val
        p = subprocess.run(cmd, capture_output=True, text=True)
        print("   the window's disk with no tree: %s" % (
            p.stdout.strip() or p.stderr.strip()))
        if p.returncode or "--noboot" not in cmd or any(
                a_.startswith("VIDEO.O88=") for a_ in cmd):
            print("   FAIL: not the unbootable disk with no player on it")
            return 1
        sysimg = os.path.join(tmp, "SYS.IMG")
        shutil.copyfile(os88build.at("build/os8088-360.img"), sysimg)
        cfg = os.path.join(tmp, "SYSTEM.CFG")
        with open(cfg, "wb") as f:              # (tests/kdnoprog.py's bytes)
            f.write(b"O88CFG\0\0" + (3).to_bytes(2, "little") + b"DW" +
                    bytes([1, 2]) + (1 << HDD_CFGBIT).to_bytes(2, "little") +
                    b"\0\0")
        subprocess.run([sys.executable, "tools/os88fat.py", "add", sysimg,
                        cfg, "SYSTEM.CFG"], check=True, capture_output=True)
        m = M.launch(sysimg, apps=os88build.at("build/apps360.img"),
                     machine=a.machine, extra=["--mount", "hd:0:" + img])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=300)
            play(ui, m, syms, name, "3", bad)
        except Exception:
            pass
        finally:
            m.close()
        m = M.launch(None, machine=a.machine, extra=["--mount", "hd:0:" + img],
                     boot=False)
        try:
            m.run()                     # (launched paused: boot=False)
            text = lambda mm: bytes(mm.read(0xB0000, 4000)[0::2]) + \
                bytes(mm.read(0xB8000, 4000)[0::2])
            try:                        # (the ROM tries the empty A: first)
                M.until(m, lambda mm: b"Not a bootable disk" in text(mm),
                        "the stub's words", poll=0.5, limit=300.0,
                        guest=90.0)
            except M.MartyError:
                pass
            said = b"Not a bootable disk" in text(m)
            print("   4: alone in the machine it %s" % (
                "says \"Not a bootable disk\"" if said else
                "does not say it is not bootable"))
            if not said:
                bad.append("4: the MBR's stub printed nothing")
        finally:
            m.close()
    for b in bad:
        print("   FAIL: %s" % b)
    if not bad:
        print("   ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
