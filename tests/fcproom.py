#!/usr/bin/env python3
"""A copy's room check stops counting once the file fits (SPEC.md 22.5.2.1).

    make && python3 tests/fcproom.py [empty|fits|full ...]

`fcp_room` refuses a file the destination cannot hold BEFORE anything is
created (22.5.2). It asked `dskw_dfree` for the whole free count, and on a
FAT16 hard disk that is the whole FAT through a nine-sector window - four
window loads and ~350 ms of 8088 on a 31MB partition before a 100KB file could
start, with the window left at the END of the FAT for the create to read the
start of back in. It now sets `[dsk_fcgoal]` to the file's sectors round the
count, and the count stops at the first FAT window that settles it. The trace
opens at `fcp_room` itself.

Three arms, each the user's own Copy/Paste from B: to C: on an os8088_xt_hdd
booted off a fixture VHD, the result read back off the VHD on the host:

  empty  a fresh partition. The copy must land byte for byte, and between the
         room check's entry and the create NO MORE THAN ONE hard-disk read may
         be issued - the full count made four here
  fits   a FILLER takes all but ~150KB, so every free cluster is at the far
         end of the FAT: the count cannot stop early and has to find the room
         in its last window. The copy must still land byte for byte
  full   the filler leaves ~60KB for a 100,000-byte file. The paste must end
         FERR_FULL and C: must hold no trace of the file

VERIFIED TO FAIL: with the early-out taken out of dsk_free_clus_x, `empty`
reports four reads inside the check. With fcp_room answering yes whatever the
count (`jc .yes` made a `jmp`), `full` reports the create running - fcp_undo
then deletes the partial file, which is 22.5.2's net and not this check. What that second break is about: the early count is a
LOWER BOUND, and the one thing that keeps a short count from passing a file
that does not fit is the bytes compare after it - so a goal set too low can
only ever refuse a file that fits, and is the direction a room check may err.
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
import os88marty as M                                       # noqa: E402
import os88ui                                               # noqa: E402
import instdeep as ID                                       # noqa: E402
from os88geom import FERR_FULL                              # noqa: E402

MACHINE = "os8088_xt_hdd"
TEMPLATE = os.path.join(M.base_run_dir(), "media/hdds/default_xtide.vhd")
SIZE = 100000
NAME = "BIG.V88"
LEAVE = {"fits": 150 * 1024, "full": 60 * 1024}
fails = []


def say(s):
    print("fcproom: " + s, flush=True)


def build_vhd(tmp, vhd, extra):
    args = ["python3", "tools/os88hdd.py", "--template", TEMPLATE, "--out",
            vhd, "--kernel", "build/kernel.sys", "--vbr", "build/boothd.bin",
            "--mbr", "build/mbr.bin", "--file", "HDD.DRV=build/hdd.drv"]
    for e in extra:
        args += ["--file", e]
    subprocess.check_call(args, cwd=ROOT, stdout=subprocess.DEVNULL)


def free_bytes(vhd):
    v = ID.partition(vhd)
    clusters = (len(v.b) // v.bps - v.data_lba) // v.spc
    free = sum(1 for n in range(2, clusters + 2) if v.fat(n) == 0)
    return free * v.spc * v.bps


def fixture(tmp, kind, data):
    src = os.path.join(tmp, NAME)
    open(src, "wb").write(data)
    vhd = os.path.join(tmp, "c.vhd")
    extra = []
    if kind in LEAVE:
        build_vhd(tmp, vhd, [])
        fill = free_bytes(vhd) - LEAVE[kind]
        f = os.path.join(tmp, "FILLER.BIN")
        with open(f, "wb") as fh:
            fh.truncate(fill)
        extra = ["FILLER.BIN=" + f]
    build_vhd(tmp, vhd, extra)
    say("%s: C: has %d bytes free for a %d-byte file"
        % (kind, free_bytes(vhd), len(data)))
    flop = os.path.join(tmp, "b.img")
    subprocess.check_call(["python3", "tools/os88disk.py", "-o", flop,
                           "--size", "360", src], cwd=ROOT,
                          stdout=subprocess.DEVNULL)
    return vhd, flop


def select(ui, name, win):
    i, _ = ui.entry(name, win)
    row = ui.scroll_to(i, win=win)
    ui.mo.click(*ui.row_xy(win, row))
    ui.settle(limit=10.0)


def arm(kind):
    say("--- %s ---" % kind)
    data = os.urandom(SIZE)
    tmp = tempfile.mkdtemp(prefix="fcproom-")
    try:
        vhd, flop = fixture(tmp, kind, data)
        m = M.launch(None, apps=flop, machine=MACHINE,
                     extra=["--mount", "hd:0:" + vhd])
        try:
            ui = os88ui.UI(m)
            ui.ready(limit=240)
            src = ui.open_drive("B")
            select(ui, NAME, src)
            ui.menu_pick("Edit", "Copy")
            ui.open_drive("C")
            bps = ["fcp_room", "dskw_write_x",
                   {"type": "int", "addr": 0x13}]
            with M.bp_trace(m, *bps, regs=True, cap=2000) as tr:
                ui.menu_pick("Edit", "Paste")
                M.until(m, lambda mm: mm.read(mm.sym("fcp_busy"), 1)[0] == 0,
                        "the paste to finish", guest=120.0, poll=0.5)
            err = m.read(m.sym("fcp_err"), 1)[0]
            ui.settle(limit=60)
        finally:
            m.quit()

        # hard-disk reads between the room check and the create
        inside, seen, created = 0, False, False
        for h in tr.hits:
            if h["name"] == "fcp_room":
                seen = True
            elif h["name"] == "dskw_write_x":
                created = True
                break
            elif seen and h["name"].startswith("?"):
                r = h.get("regs", {})
                if r.get("dx", 0) & 0x80 and (r.get("ax", 0) >> 8) == 2:
                    inside += 1
        say("%s: fcp_err %d, %d hard-disk reads inside the room check"
            % (kind, err, inside))
        if not seen:
            fails.append("%s: the paste never reached fcp_room" % kind)

        got = ID.partition(vhd).read(NAME)
        if kind == "full":
            if err != FERR_FULL:
                fails.append("full: the paste ended fcp_err %d, not FERR_FULL"
                             % err)
            if created:
                fails.append("full: dskw_write_x ran - the room check let a "
                             "file through that the volume cannot hold")
            if got is not None:
                fails.append("full: %s is on C: (%d bytes) after a refusal"
                             % (NAME, len(got)))
            return
        if err:
            fails.append("%s: the paste ended fcp_err %d" % (kind, err))
        if kind == "empty" and inside > 1:
            fails.append("empty: %d hard-disk reads inside the room check - "
                         "it counted past the first FAT window" % inside)
        if got != data:
            fails.append("%s: the copy on C: is %s, not the floppy's %d bytes"
                         % (kind, "missing" if got is None else
                            "%d different bytes" % len(got), len(data)))
        else:
            say("%s: the copy on C: is the floppy's %d bytes exactly"
                % (kind, len(data)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    for k in sys.argv[1:] or ["empty", "fits", "full"]:
        arm(k)
    for f in fails:
        say("FAIL: " + f)
    say("FAILED" if fails else "ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
