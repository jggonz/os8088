#!/usr/bin/env python3
"""A 22 kHz Sound Blaster stream from a floppy on kern_mega: no long silence
(docs/plans/KERN-MEGA-PLAN.md 14.156, SPEC.md 34.5.2, 86.5.2).

    python3 tests/megasbgap.py              the row
    python3 tests/megasbgap.py --keep DIR   ...and keep the capture there

AUDIO.O88 plays a 10 s, 22,050 Hz 8-bit mono WAV off a scratch 1.44MB B: on
86Box's at286_vga_sb (the owner's mr286, a Sound Blaster 2.0), the card's DAC
captured ($MARTYPC_WAV, os88box86api "sound"). 22,050 bytes a second is half
of what a 1.44MB floppy turning at 300 rpm can deliver at ONE call a track,
so the look-ahead ring is refilled by floppy reads all through the play, and
each one either keeps up or the card runs dry - which SOUND.DRV turns into a
pause (D0h) until the next feed, heard as silence. Measured, kern_mega had
two costs a real-mode kernel has not, each a missed revolution a call:

  BOUNCE  dsk_runcap answered 0 for any buffer above 1MB, so dsk_xfer read
          the look-ahead ring (a pool claim there) through the read-ahead's
          9-sector landing, a call per 9 sectors and a copy each; the native
          floppy path's 8237 programs all 24 bits and reads it in place.
  SETTLE  mega_fdgo sought and waited the head-settle time (15 ms) on every
          call, seek or not, and so lost the sector after the last call's.

What must hold, from the capture: the play is there (at least 9.5 s from its
first sound to its last), the FDC completed READ DATA commands during it, no
silent stretch inside it is longer than 400 ms, and the silences inside it add
up to under 1.5 s. kern_big on the same machine, measured: longest 100 ms,
290 ms in all. This tree: longest 290-300 ms (the start, the ring's first
pass), 880-940 ms in all. What is left is the look-ahead ring's own 64KB
page: a pool claim crosses one, so one run a pass splits there (docs/plans/
KERN-MEGA-PLAN.md 14.156 has the measurement and the choice it leaves).

RED (docs/WRITING-TESTS.md 1), measured: the base tree (both costs) has a
450-540 ms silence every 2.1 s, 3.57 s in all; SETTLE alone put back
(OS88_MEGA_POKE='mega_fdgo.ss+0xD=9090', the head always sought) longest
500 ms, 2.84 s; BOUNCE alone (OS88_MEGA_POKE='dsk_runcap.hi+0x10=9090')
longest 450 ms, 2.24 s.
"""
import argparse
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, HERE)
os.environ.setdefault("OS88_MEGA", "1")
os.environ.setdefault("OS88_MEGA_EMU", "86box")
import os88build  # noqa: E402
import os88marty as M  # noqa: E402
import os88ui  # noqa: E402

MACHINE = "at286_vga_sb"
RATE, SECS, TONE = 22050, 10.0, 660.0
LONGEST_MS, TOTAL_MS, MIN_PLAY = 400, 1500, 9.5

fails = []


def check(what, ok, more=""):
    print("%s %s%s" % ("ok  " if ok else "FAIL", what, (" - " + more) if more else ""))
    if not ok:
        fails.append(what)
    return ok


def wav(path):
    n = int(SECS * RATE)
    data = bytes(int(128 + 100 * math.sin(2 * math.pi * TONE * i / RATE))
                 for i in range(n))
    hdr = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
           struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE, 1, 8) +
           b"data" + struct.pack("<I", len(data)))
    open(path, "wb").write(hdr + data)


def silences(path):
    """(first, last, [(start_s, ms)]): the capture in 10 ms cells, a cell
    LOUD when its RMS about its own mean is over 300, and every run of quiet
    cells between the first loud one and the last"""
    import numpy as np
    raw = open(path, "rb").read()
    pos, fmt, data = 12, None, b""
    while pos + 8 <= len(raw):
        cid = raw[pos:pos + 4]
        sz = struct.unpack("<I", raw[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", raw[pos + 8:pos + 24])
        elif cid == b"data":
            data = raw[pos + 8:]
            break
        pos += 8 + sz + (sz & 1)
    ch, rate = fmt[1], fmt[2]
    s = np.frombuffer(data[:len(data) // 2 * 2], dtype="<i2").astype(float)
    if ch > 1:
        s = s[:len(s) // ch * ch].reshape(-1, ch).mean(axis=1)
    bl = rate // 100
    nb = len(s) // bl
    x = s[:nb * bl].reshape(nb, bl)
    rms = np.sqrt(((x - x.mean(axis=1, keepdims=True)) ** 2).mean(axis=1))
    loud = rms > 300
    idx = np.nonzero(loud)[0]
    if not len(idx):
        return None, None, []
    a, b = int(idx[0]), int(idx[-1])
    out, st = [], None
    for i in range(a, b + 1):
        if not loud[i] and st is None:
            st = i
        if loud[i] and st is not None:
            out.append((st / 100.0, (i - st) * 10))
            st = None
    return a / 100.0, b / 100.0, out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--keep", help="a directory to keep the capture in")
    a = ap.parse_args()
    pkg = os88build.at("build/megak/apps/audio.o88")
    tmp = tempfile.mkdtemp(prefix="sbgap-")
    try:
        clip = os.path.join(tmp, "TONE22.WAV")
        wav(clip)
        disk = os.path.join(tmp, "sb.img")
        subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "os88disk.py"),
                               "-o", disk, "--size", "1440", "APPS:" + pkg,
                               "MEDIA:" + clip], stdout=subprocess.DEVNULL)
        cap = os.path.join(tmp, "cap")
        os.environ["MARTYPC_WAV"] = cap
        with os88ui.boot(os88build.at("build/mega.img"), apps=disk,
                         machine=MACHINE, label="sbgap") as ui:
            m = ui.m
            ui.path("B:/MEDIA/TONE22.WAV", limit=120)
            r0 = m.disk()["reads"]
            M.guest_sleep(m, SECS + 3)
            reads = m.disk()["reads"] - r0
            check("the window is the Audio Player's",
                  any("Audio" in t for t in ui.titles()), "titles %s" % ui.titles())
        got = cap + ".sound_blaster.wav"
        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            shutil.copy(got, a.keep)
        t0, t1, gaps = silences(got)
        if not check("the play is in the capture", t0 is not None):
            return 1
        print("     play %.2f-%.2f s (%.2f s); silences %s" % (
            t0, t1, t1 - t0, ", ".join("%.2f+%dms" % g for g in gaps) or "none"))
        check("it played %.1f s or more" % MIN_PLAY, t1 - t0 >= MIN_PLAY,
              "%.2f s" % (t1 - t0))
        check("the floppy was read during it (%d READ DATA)" % reads, reads >= 20)
        longest = max([ms for _, ms in gaps] or [0])
        total = sum(ms for _, ms in gaps)
        check("no silence inside it over %d ms" % LONGEST_MS, longest <= LONGEST_MS,
              "longest %d ms" % longest)
        check("its silences under %d ms in all" % TOTAL_MS, total < TOTAL_MS,
              "%d ms" % total)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megasbgap: %s" % ("PASS" if not fails else "FAIL (%s)" % ", ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
