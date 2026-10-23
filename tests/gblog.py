#!/usr/bin/env python3
"""REALDMG.O88's log to file, written and read back.

    python3 tests/gblog.py

docs/REALDMG-SPEC.md section 14.1: Machine > Log keeps a record a second
and writes them as text beside REALDMG.O88, under the first free name of
the game's six letters and a number. This opens GBDEMO.GB (tools/gbours.py,
ours) on MartyPC's 5150 Hercules and takes two logs: one turned off by the
menu (GBDEMO00.LOG), with Machine > Pause on and off inside it, and one the
close writes (GBDEMO01.LOG). It flushes B: and reads both on the host.
GBDEMO00.LOG must hold the head naming the game and the machine, PAUSE and
GO in the seconds they happened, a cpu column of milliseconds the machine
ran, and at least eight records
whose seconds rise, whose speeds average the demo's full speed on this machine
(97-103%, tests/gbdemo.py) and which drew frames. Breaking the
record (rw_logsec never counting a record) leaves a file with no lines and
the row says so.
"""
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M  # noqa: E402
import os88flush  # noqa: E402
import os88ui  # noqa: E402

# seconds, speed, then run drawn comp chg draw scan cmp cpu max owed, then
# events blocks flushes banks
LINE = re.compile(rb"^\s*(\d+)\.(\d)\s+(\d+)%\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"
                  rb"\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)"
                  rb"\s+(\d+) ([WF])")


def main():
    rom = subprocess.check_output([sys.executable, os.path.join(ROOT, "tools", "gbours.py"),
                                   "gbdemo"], text=True).strip().splitlines()[-1]
    tmp = tempfile.mkdtemp(prefix="gblog-")
    gb = os.path.join(tmp, "GBDEMO.GB")
    with open(rom, "rb") as i, open(gb, "wb") as o:
        o.write(i.read())
    disk = os.path.join(tmp, "apps.img")
    subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"), "-o", disk,
                           "--size", "720",
                           os.path.join(ROOT, "build", "realdmg.o88"), gb],
                          stdout=subprocess.DEVNULL)
    after = disk + ".after"
    with os88ui.boot(os.path.join(ROOT, "build", "os8088-720.img"), apps=disk,
                     machine="os8088_5150_herc_sb_720_gla") as ui:
        w = ui.path("B:/GBDEMO.GB")
        M.guest_sleep(ui.m, 3)
        ui.menu_pick("Machine", "Log")
        M.guest_sleep(ui.m, 6)
        ui.menu_pick("Machine", "Pause")
        M.guest_sleep(ui.m, 2)
        ui.menu_pick("Machine", "Pause")
        M.guest_sleep(ui.m, 6)
        ui.menu_pick("Machine", "Log")          # written now: GBDEMO00.LOG
        M.guest_sleep(ui.m, 4)
        ui.menu_pick("Machine", "Log")
        M.guest_sleep(ui.m, 4)
        ui.close(w)                             # ...and this one GBDEMO01.LOG
        M.guest_sleep(ui.m, 4)
        os88flush.Flush(marty=ui.m).save(1, after)
    vol = os88flush.Volume(open(after, "rb").read())
    fails = []
    try:
        text = vol.read("GBDEMO00.LOG")
    except Exception as e:                      # noqa: BLE001
        print("FAIL gblog: Log: Off wrote no GBDEMO00.LOG beside REALDMG.O88 (%s)" % e)
        return 1
    try:
        second = vol.read("GBDEMO01.LOG")
    except Exception as e:                      # noqa: BLE001
        second = b""
        fails.append("the close wrote no GBDEMO01.LOG - the second log did not "
                     "take the next name (%s)" % e)
    lines = text.split(b"\r\n")
    print("gblog: GBDEMO00.LOG %d bytes, %d lines; GBDEMO01.LOG %d bytes"
          % (len(text), len(lines), len(second)))
    for l in lines[:7]:
        print("   | " + l.decode("latin-1"))
    if not lines[0].startswith(b"REALDMG LOG - GBDEMO.GB - 32K, MBC 0, RAM 0 x 8K"):
        fails.append("the head is %r" % lines[0][:70])
    if not lines[1].startswith(b"CPU tier 0, display 1 bpp, full screen Hercules, recompiler no"):
        fails.append("the machine's line is %r" % lines[1][:70])
    if not any(b" PAUSE" in l for l in lines) or not any(b" GO" in l for l in lines):
        fails.append("Machine > Pause and its undoing are not in the log")
    recs = [LINE.match(l) for l in lines]
    recs = [r for r in recs if r]
    secs = [int(r.group(1)) * 10 + int(r.group(2)) for r in recs]
    if len(recs) < 8:
        fails.append("%d records, not at least 8" % len(recs))
    elif secs != sorted(secs):
        fails.append("the seconds do not rise: %s" % secs)
    else:
        # the seconds a pause touched are not the game's speed: the rest are.
        # A second at a time the speed jitters (frames run in bursts that
        # straddle a second's edge: 89-106 here, and the field run's "90 to
        # 110"); taken together they are the demo's full speed
        mid = [r for r in recs[2:-1]
               if b"PAUSE" not in r.string and b" GO" not in r.string]
        sp = [int(r.group(3)) for r in mid]
        dr = [int(r.group(5)) for r in mid]
        avg = sum(sp) / float(len(sp)) if sp else 0
        print("gblog: %d seconds outside the pause average %.1f%%; a second at a time %s"
              % (len(sp), avg, sp))
        if not 97 <= avg <= 103:
            fails.append("the demo ran at %.1f%% over the log, not ~100%%" % avg)
        if not all(60 <= v <= 140 for v in sp):  # (a draw of ~0.4 s on this
            fails.append("a second's speed outside 60-140%%: %s" % sp)  # 8088)
        if not all(v > 0 for v in dr):
            fails.append("seconds with no frame drawn: %s" % dr)
        cpu = [int(r.group(12)) for r in mid]   # the Game Boy's ms a second
        if not all(0 < v <= 1100 for v in cpu):
            fails.append("the cpu column is not a second's ms: %s" % cpu)
        evn = [int(r.group(14)) for r in mid]   # the core's event entries
        if not all(v > 0 for v in evn):
            fails.append("seconds with no core event counted: %s" % evn)
    for f in fails:
        print("FAIL gblog: " + f)
    print("gblog: %s" % ("PASS" if not fails else "FAIL"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
