#!/usr/bin/env python3
"""Can Format Disk... reclaim a disk it cannot read?

    python3 tests/fmtlow.py

SPEC.md 18.96.3. The formatter used to be high-level only - it rewrote the
boot sector, the FATs and the root directory over sectors that had to exist
already - and its probe refused any medium it could not READ with FERR_IO.
So every disk anybody actually brought to it came back 'Disk error': a
1.44MB disk taped for a 720KB drive, a disk whose ID fields a failed write had
damaged (docs/FIELD-NOTES.md 32), a foreign format on HD media. Each needed
DOS's FORMAT. Now every format lays every track down with int 13h AH=05h,
and an unreadable medium gets the geometry the DRIVE makes.

TWO LEGS, one per half of that sentence, on one boot:

  1. THE PROBE MUST NOT REFUSE. Format Disk... with every read the probe
     makes aimed, at the int 13h gate, at absent drive 3 - so none of them
     succeeds, which is what a disk this controller cannot read looks like
     from the kernel. The confirmation must come up anyway, at the size the
     DRIVE makes: 360KB here, GLaBIOS answering no AH=08h for a floppy
     exactly as the reporter's 27 Oct 82 ROM does. Esc then, so leg 2 starts
     from a clean prompt.
  2. EVERY TRACK IS LAID. Format Disk... on a disk of random bytes - a
     foreign format, as far as anything can tell - and Enter: 80 AH=05h
     calls, 'Formatted B:', and B: read back on the host a clean, empty
     FAT12 volume.

WHY THE PROBE'S FAILURE IS INJECTED rather than carried by a damaged disk:
MartyPC's disk library could not stand in for one. A sector image (IMD)
cannot be formatted at all; a bitstream track that HAD no sectors is
formatted without a track schema, so the next write fails with SchemaError
(20h - one line missing in fluxfox's `TrackData::format`); and a track laid
with sector IDs 41h and up still answered a read of sector 9. Real drives do
none of that, and the kernel's decision is the same whichever reason the
read had for failing - so the failure is made at the one place the kernel
can see it.
"""
import os
import re
import subprocess
import sys
import threading

sys.path.insert(0, "tools")
sys.path.insert(0, "tests/unit")
import os88marty as M                                     # noqa: E402
import os88ui                                             # noqa: E402
import os88build                                          # noqa: E402
from harness import check, done                           # noqa: E402

MACHINE = "os8088_5150_cga_720b_gla"
SYS = "build/os8088-360.img"


def equ(path, name):
    m = re.search(r"^%s\s+equ\s+(\d+)" % name, open(path).read(), re.M)
    if not m:
        sys.exit("%s: no `%s equ`" % (path, name))
    return int(m.group(1))


FS_EDIT = equ("kernel/files.inc", "FS_EDIT")
FS_SIZE = equ("kernel/files.inc", "FS_SIZE")


def edit(m):
    pool = m.sym("fm_pool")
    for slot in range(4):
        b = m.read(pool + slot * FS_SIZE, FS_SIZE)
        if b[FS_EDIT]:
            return b[FS_EDIT]
    return 0


def pick_format_unreadable(ui, m):
    """File > Format Disk..., with every probe read sent to absent drive 3.

    The menu is picked by the harness verb, which waits for the pick to be
    CONFIRMED - so it runs on a thread while this one sits at the int 13h
    gate. Answers how many reads were made to fail.
    """
    failed = 0
    m.breakpoints([{"type": "int", "addr": 0x13}])
    t = threading.Thread(target=lambda: ui.menu_pick("File", "Format Disk..."))
    t.start()
    while m.wait_stop(20):
        r = m.regs()
        if r["ax"] >> 8 == 0x02 and r["dx"] & 0xFF == 1:
            m.setreg("dx", (r["dx"] & 0xFF00) | 3)
            failed += 1
        m.run()
    m.breakpoints([])
    m.run()
    t.join()
    M.settle(m)
    return failed


def run_format(m):
    """Enter on the confirmation; answers how many tracks were laid."""
    tracks = 0
    m.breakpoints([{"type": "int", "addr": 0x13}])
    m.key("Enter")
    while m.wait_stop(30):
        if edit(m) == 0:                # the mode is over: this is the
            break                       # remount after it, not the format
        if m.regs()["ax"] >> 8 == 0x05:
            tracks += 1
        m.run()
    m.breakpoints([])
    m.run()
    M.settle(m, limit=600)
    return tracks


tmp = "build/fmtlow-%d" % os.getpid()
FIX = os88build.at(tmp + ".img")
OUT = os.path.abspath(os88build.at(tmp + ".out"))
_img = bytearray(os.urandom(368640))         # a foreign format, as far as
_img[11:36] = bytes(25)                      # anything can tell - with NO
open(FIX, "wb").write(_img)                  # BPB: MartyPC's raw flush sizes
                                             # the file from the BPB fluxfox
                                             # parsed at MOUNT, and a random
                                             # media byte it recognises (6 in
                                             # 256) mis-sizes or refuses the
                                             # capture (tests/fmtreach.py)
try:
    with os88ui.boot(SYS, apps=FIX, machine=MACHINE) as ui:
        m = ui.m
        print("== %s : Format Disk... on a disk it cannot read ==" % MACHINE)
        try:
            ui.open_drive("B")          # random bytes mount nothing, and
        except os88ui.UIError:          # open_drive's check wants a volume;
            pass                        # the WINDOW is what counts here
        M.settle(m)

        # --- 1. the probe reads nothing, and must not refuse ----------------
        n = pick_format_unreadable(ui, m)
        check(n >= 1, "the probe's reads were made to fail", "",
              got=n, want=">= 1")
        check(edit(m) == 5,
              "an UNREADABLE disk is offered a format, not refused",
              "FS_EDIT 5 is the format confirmation (SPEC.md 22.12). The probe "
              "answered FERR_IO when no read of track 0 succeeded, and the "
              "command ended at 'Disk error' - which is every disk the owner "
              "brought to it (SPEC.md 18.96.3)",
              got=(edit(m), ui.toast()[0]), want=(5, "(no verdict)"))
        check(m.read(m.sym("fm_fmtrow"), 1)[0] == 3,
              "...at the size the DRIVE makes: 360K on a ROM with no AH=08h",
              "", got=m.read(m.sym("fm_fmtrow"), 1)[0], want=3)
        m.key("Escape")
        M.settle(m)

        # --- 2. every track is laid ------------------------------------------
        ui.menu_pick("File", "Format Disk...")
        M.settle(m, limit=300)
        check(edit(m) == 5, "a disk of random bytes is offered a format", "",
              got=edit(m), want=5)
        tracks = run_format(m)
        check(tracks == 80, "the format lays all 80 tracks",
              "40 cylinders x 2 heads, one AH=05h each (SPEC.md 18.96.3); 0 "
              "is the high-level-only formatter", got=tracks, want=80)
        check(ui.toast()[0] == "Formatted B:", "...and says 'Formatted B:'",
              "", got=ui.toast()[0], want="Formatted B:")
        m.flush(1, OUT)

    ls = subprocess.run([sys.executable, "tools/os88fat.py", "ls", OUT],
                        capture_output=True, text=True).stdout
    check("354 of 354 cluster(s) free" in ls,
          "B: read back on the host is an empty 360KB FAT12 volume", "",
          got=ls.strip()[-120:], want="354 of 354 cluster(s) free")
finally:
    for p in (FIX, OUT):
        try:
            os.remove(p)
        except OSError:
            pass

done("fmtlow")
