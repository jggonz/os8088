#!/usr/bin/env python3
"""System Check's gate (SPEC.md 118.11) - python3 tests/syscheck.py

Builds a HARD-DISK image the way an upgraded install looks - the system
disk's own files on a FAT16 partition, booted from C: - with one of every
kind of breakage SPEC.md 118.1 names PLANTED in it, and an AUTOSTRT link that
opens SYSTEM\\SYSCHECK.O88 at boot. Then it boots the image under QEMU, lets
the scan run, saves the report, and holds SYSCHECK.TXT (read back off the
image on the host) to every planted finding; then Clean Up, and the volume
must have lost exactly SPEC.md 118.8's files and nothing else.

    python3 tests/syscheck.py              the whole gate
    python3 tests/syscheck.py --build OUT  build the fixture image only
    python3 tests/syscheck.py --keep       leave QEMU and the image for a look

THE PLANTED BREAKAGES, and the finding each must produce:

  APPS\\OLDTIMER.O88   a package whose format byte is 8   Packages, broken
  APPS\\JUNK.O88       not a package at all               Packages, broken
  APPS\\MODPLUG.O88    a retired program                  Retired, warning, removable
  APPS\\GAMES\\MINES.O88 the same program in two folders  Leftovers, warning
  KPAD.TMP            an install's pad                    Leftovers, warning, removable
  RAMDISK.DRV         driver version 9                    Drivers, broken
  DEBUG.DRV           a retired driver                    Retired, warning
  EMS.DRV             deleted                             Release files, broken
  SYSTEM.CFG          wants EMS.DRV                       Settings, broken
  APPS\\NOTEPAD.O88    deleted after ASSOC.DAT named it   Associations, warning
  HIBERNAT.IMG        an image with no pointer            Hibernation, warning, removable

Needs mtools (mdel), like tests/instkeep.py.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88lz   # noqa: E402
import os88lnk  # noqa: E402

PART_LBA = 63          # os88disk's --hdd partition starts here (SPEC.md 80.1)


def manifest():
    """The system disk's files, from the manifest `make` already wrote for
    SYSCHECK.O88 (SPEC.md 118.2) - so the fixture is the release's own list
    and this file runs nothing that builds. (kind, folder, NAME)."""
    import os88sysman
    inc = b("syscheck_man.inc")
    return [(k, f & ~os88sysman.SCF_OPT, n)
            for k, f, n, _size, _sum in os88sysman.read_inc(inc)]


def b(name):
    """An artefact of the tree under test - the run's frozen tree when there
    is one (tools/os88build.py), so the fixture and the machine agree."""
    import os88build
    return os.path.join(ROOT, os88build.at(os.path.join("build", name)))


def mtools_img(img):
    return "%s@@%d" % (img, PART_LBA * 512)


def build_fixture(out):
    """The planted hard disk. Returns the scratch dir (the caller removes it)."""
    tmp = tempfile.mkdtemp(prefix="syscheck-")

    def put(name, data):
        p = os.path.join(tmp, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)
        return p

    args = []
    folder = {0: "", 1: "SYSTEM:", 2: "STIKAPPS:"}
    for kind, f, name in manifest():
        d = b(name.lower())
        if kind == 1:
            continue                    # KERNEL.SYS: --kernel below
        if f:
            args.append(folder[f] + d)
            continue
        if name == "RAMDISK.DRV":
            # driver version 9: unwrapped, the version byte rewritten, stored
            # plain so the header is where System Check reads it first
            img = bytearray(os88lz.cz_unwrap(open(d, "rb").read()))
            img[2] = 9
            args.append(put("drv/RAMDISK.DRV", bytes(img)))
        else:
            args.append(d)
    args.append(put("drv/DEBUG.DRV", open(b("net.drv"), "rb").read()))
    args.append("SYSTEM:" + b("syscheck.o88"))
    for p in ("calc.o88", "notepad.o88", "mines.o88", "timer.o88"):
        args.append("APPS:" + b(p))
    args.append("APPS/GAMES:" + b("mines.o88"))
    raw = []
    timer = bytearray(open(b("timer.o88"), "rb").read())
    timer[2] = 8
    raw += ["APPS:" + put("raw/OLDTIMER.O88", bytes(timer))]
    raw += ["APPS:" + put("raw/JUNK.O88", b"This is not a package.\r\n" * 8)]
    raw += ["APPS:" + put("raw/MODPLUG.O88", open(b("calc.o88"), "rb").read())]
    args.append(put("root/KPAD.TMP", b"\0" * 2048))
    # an IMAGE with no pointer: another build's pointer is deleted by the
    # kernel itself at boot (SPEC.md 87.3), and its image with it, so the
    # leftover a machine can actually carry is the orphan
    args.append(put("root/HIBERNAT.IMG", b"\0" * 4096))
    cfg = b"O88CFG\0\0" + struct.pack("<H", 3)
    cfg += b"DW" + bytes([1, 2]) + struct.pack("<H", 1 << 7)   # EMS.DRV
    cfg += b"\0\0"
    args.append(put("root/SYSTEM.CFG", cfg))
    lnk = os.path.join(tmp, "auto", "SYSCHECK.LNK")
    os.makedirs(os.path.dirname(lnk), exist_ok=True)
    with open(lnk, "wb") as f:
        f.write(bytes(os88lnk.make("SYSTEM/SYSCHECK.O88")))
    args.append("AUTOSTRT:" + lnk)
    cmd = ["python3", os.path.join(ROOT, "tools", "os88disk.py"), "-o", out,
           "--hdd", "--mbr", b("mbr.bin"), "--boot", b("boothd.bin"),
           "--kernel", b("kernel.sys"), "--folder", "SYSTEM/APPDATA"]
    for r in raw:                       # the plain path skips validation;
        cmd += ["--raw", r.split(":", 1)[1]]  # the positional places it
    cmd += args + raw
    subprocess.run(cmd, check=True)
    # ...and the three deletions, after os88disk wrote ASSOC.DAT and the
    # manifest's driver list. CDROM.DRV is an OPTIONAL manifest file (SPEC.md
    # 118.2.3) - the 360KB system disk leaves it off - so its absence must add
    # NOTHING to the report's exact totals below.
    for victim in ("::/EMS.DRV", "::/CDROM.DRV", "::/APPS/NOTEPAD.O88"):
        subprocess.run(["mattrib", "-i", mtools_img(out), "-r", "-s", "-h",
                        victim], check=True)
        subprocess.run(["mdel", "-i", mtools_img(out), victim], check=True)
    return tmp


# --- the driving half ----------------------------------------------------------
# VGA, 640x480: the window opens at its template's place, so its controls are
# where the layout puts them for that content box (SPEC.md 118.4). The boot
# opens the Control Panel's Drivers page on top - SYSTEM.CFG wants a driver
# that is not there, which is the Settings breakage - so the row clicks rather
# than types: a click lands on what is under it whatever has the focus.
SOCK = os.path.join(BUILD, "qmp.sock")
CP_CLOSE = (171, 138)       # the Control Panel's close box
BTN_SCAN = (35, 441)        # Scan: checks the drive the chooser offers
BTN_SAVE = (210, 441)       # Save Report
BTN_CLEAN = (321, 441)      # Clean Up...
ALERT_PROCEED = (358, 265)  # the alert's Proceed (Cancel is the default)

EXPECT = [                  # (severity, path, a word of the reason)
    ("BROKEN", r"C:\RAMDISK.DRV", "driver version 9"),
    ("BROKEN", r"C:\EMS.DRV", "missing"),
    ("WARNING", r"C:\KPAD.TMP", "install"),
    ("WARNING", r"C:\DEBUG.DRV", "retired"),
    ("WARNING", r"C:\APPS\MINES.O88", r"C:\APPS\GAMES"),
    ("BROKEN", r"C:\APPS\OLDTIMER.O88", "format 8, kernel takes 10"),
    ("BROKEN", r"C:\APPS\JUNK.O88", "not a package"),
    ("WARNING", r"C:\APPS\MODPLUG.O88", "retired"),
    ("BROKEN", r"C:\SYSTEM.CFG", "EMS.DRV"),
    ("WARNING", r"C:\APPS\NOTEPAD.O88", "not there"),
    ("WARNING", r"C:\HIBERNAT.IMG", "no pointer"),
]
REMOVED = ["::/KPAD.TMP", "::/APPS/MODPLUG.O88", "::/HIBERNAT.IMG"]
KEPT = ["::/DEBUG.DRV", "::/APPS/GAMES/MINES.O88", "::/APPS/MINES.O88",
        "::/APPS/OLDTIMER.O88", "::/APPS/JUNK.O88", "::/RAMDISK.DRV"]


def click(x, y):
    """Closed-loop (SOAK-SPEED technique 15): to (x, y) read back off
    mouse_x/mouse_y, each button edge off mouse_btn."""
    import os88qemu
    import xmcheck
    os88qemu.mouse_click(M, xmcheck.sym, lambda *c: xmcheck.qmp(SOCK, *c),
                         x, y)


def report(img):
    """SYSCHECK.TXT off the image, or None. QEMU writes through the host's
    page cache, so the host sees it while the guest runs."""
    r = subprocess.run(["mtype", "-i", mtools_img(img), "::/SYSCHECK.TXT"],
                       capture_output=True)
    return r.stdout.decode("latin1") if r.returncode == 0 else None


def exists(img, path):
    r = subprocess.run(["mattrib", "-i", mtools_img(img), path],
                       capture_output=True, text=True)    # (mdir hides a
    return r.returncode == 0 and path.split("/")[-1] in r.stdout.upper()  # hidden file)


# The waits are the GUEST's (tests/os88qemu.py): the scan runs in WAKE
# SLICES on the UI task (SPEC.md 118.3), so `ui_done` - ui_task asleep with
# nothing queued and no wake pending - is the scan finished, capped where the
# fixed host sleep used to be so no wait is longer than it was.
M = None                    # xmcheck.Mem over the private QEMU's socket


def done(cap):
    import os88qemu
    import xmcheck
    os88qemu.ui_done(M, xmcheck.sym, cap=cap)


def save_until(img, want, secs=120):
    """Click Save Report until the report says `want`: the button is greyed
    while a scan runs, so the first report that lands is a finished scan's."""
    import os88qemu
    c = os88qemu.Clock(M)
    while c.secs() < secs:
        click(*BTN_SAVE)
        done(2.0)
        text = report(img)
        if text and want in text:
            return text
    return None


def qemu(work, img, keep=False):
    """A PRIVATE QEMU - what `make test TESTHD=img` launched, with its own
    socket, pidfile and copies of both floppies, so the row runs no `make`
    and kills no other row's emulator (it used os88qemu.kill() on
    build/qemu.pid)."""
    import signal
    import os88build
    import os88qemu
    global SOCK
    SOCK = os.path.join(work, "qmp.sock")
    pidf = os.path.join(work, "qemu.pid")
    a, bimg = os.path.join(work, "a.img"), os.path.join(work, "b.img")
    shutil.copy(b("os8088.img"), a)
    shutil.copy(b("apps.img"), bimg)
    os88qemu.start(["qemu-system-i386", "-machine", "pc,vmport=off",
                    "-drive", "file=%s,format=raw,if=floppy" % a,
                    "-boot", "c", "-chardev", "msmouse,id=m0",
                    "-serial", "chardev:m0",
                    "-drive", "file=%s,format=raw,if=floppy,index=1" % bimg,
                    "-drive", "file=%s,format=raw,if=ide,index=0,media=disk"
                    % img, "-display", "none",
                    "-qmp", "unix:%s,server,nowait" % SOCK,
                    "-daemonize", "-pidfile", pidf], check=True)
    pid = int(open(pidf).read().strip())

    def close():
        try:
            os.kill(pid, signal.SIGTERM)
            os88qemu.gone(pid)
        except OSError:
            pass
    if not keep:
        os88qemu.own(pidf, SOCK)        # atexit AND SIGTERM (t_qemuown)
    for _ in range(150):
        if os.path.exists(SOCK):
            break
        time.sleep(0.2)                 # the socket appearing is a HOST event
    return close


def drive(img, keep, work):
    global M
    sys.path.insert(0, os.path.join(ROOT, "tests"))
    import os88qemu
    import os88ui
    import xmcheck
    fails = []
    close = qemu(work, img, keep)
    M = xmcheck.Mem(SOCK)
    M.readseg = lambda seg, off, n: M.read((seg << 4) + off, n)
    ui = os88ui.UI(M, mouse=object(), sym=xmcheck.sym, verbose=False)

    def up(title):
        try:
            return any(w.visible and title in w.title for w in ui.windows())
        except Exception:               # noqa: BLE001 - not booted yet
            return False
    # boot, the autostart, the scan: both windows up and the UI done with it
    if not os88qemu.acted(M, lambda: up("Control Panel") and
                          up("System Check"), secs=60,
                          what="the Control Panel and System Check"):
        sys.exit("syscheck: the boot never showed the Control Panel and "
                 "System Check")
    done(20.0)
    click(*CP_CLOSE)
    os88qemu.acted(M, lambda: not up("Control Panel"), secs=1.5,
                   what="the Control Panel to close")
    done(1.5)
    click(*BTN_SCAN)                    # the chooser (SPEC.md 118.13) offers
    done(1.0)                           # C:, the boot hard disk: check it
    text = save_until(img, "Result: 5 broken, 6 warnings")
    if text is None:
        fails.append("the first report never said 5 broken, 6 warnings:\n%s"
                     % report(img))
        text = report(img) or ""
    lines = text.splitlines()
    import re
    if not re.search(r"checked (19|20)\d\d-\d\d-\d\d \d\d:\d\d", text):
        fails.append("the report's date is not YYYY-MM-DD hh:mm (a DEC_SEP "
                     "comma in the year was the first build's)")
    if "CDROM.DRV" in text:
        fails.append("an absent OPTIONAL file (CDROM.DRV) is reported "
                     "(SPEC.md 118.2.3)")
    for sev, path, word in EXPECT:
        hit = [ln for ln in lines if ln.startswith(sev) and
               (" %s " % path) in ln and word in ln]
        if not hit:
            fails.append("the report has no %s line for %s (%s)"
                         % (sev, path, word))
    # --- Clean Up: removes exactly 118.8's files, rebuilds ASSOC.DAT ----
    click(*BTN_CLEAN)
    done(2.0)
    click(*ALERT_PROCEED)
    done(6.0)
    after = save_until(img, "Result: 5 broken, 2 warnings")
    if after is None:
        fails.append("after Clean Up the report never said 5 broken, "
                     "2 warnings:\n%s" % report(img))
    elif "NOTEPAD.O88" in after:
        fails.append("ASSOC.DAT still names NOTEPAD.O88 after Clean Up")
    if not keep:
        close()
    for p in REMOVED:
        if exists(img, p):
            fails.append("Clean Up left %s" % p)
    for p in KEPT:
        if not exists(img, p):
            fails.append("Clean Up removed %s, which 118.8 says it keeps" % p)
    r = subprocess.run(["python3", os.path.join(ROOT, "tools", "os88disk.py"),
                        "--verify-hdd", img], capture_output=True, text=True)
    if r.returncode:
        fails.append("the volume does not verify:\n" + r.stdout + r.stderr)
    return fails


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", metavar="OUT")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    if a.build:
        tmp = build_fixture(a.build)
        shutil.rmtree(tmp, ignore_errors=True)
        print("syscheck: fixture -> %s" % a.build)
        return 0
    work = tempfile.mkdtemp(prefix="sc-")      # short: a QMP socket path
    img = os.path.join(work, "sctest.img")      # has a ~107-byte limit
    tmp = build_fixture(img)
    try:
        fails = drive(img, a.keep, work)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    if fails:
        for f in fails:
            print("FAIL: " + f)
        return 1
    print("syscheck: every planted breakage found, reported and - where "
          "SPEC.md 118.8 allows - cleaned up")
    return 0


if __name__ == "__main__":
    sys.exit(main())
