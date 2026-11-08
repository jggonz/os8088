#!/usr/bin/env python3
"""A 22 kHz Sound Blaster stream from a floppy on kern_mega: no long silence
(docs/plans/KERN-MEGA-PLAN.md 14.156, SPEC.md 34.5.2, 86.5.2).

    python3 tests/megasbgap.py              the row (streamed)
    python3 tests/megasbgap.py --hold       megasbhold: the clip HELD first
    python3 tests/megasbgap.py --keep DIR   ...and keep the capture there

THE STREAMED CASE is a file too big to hold. kern_mega's AUDIO arm reads a
WAV the machine can hold WHOLE into one huge claim and plays it from memory
(SPEC.md 86.5.4, apps/audio/aphold.inc), which this 220 KB clip always is -
so the player is launched first, its [aph_off] set (an arm before the hold
has no such byte and streams anyway), and the clip opened through File >
Open. --hold leaves the byte alone and sets [ap_repeat]: the first play
streams and its own reads fill the hold (aph_on, KERN-MEGA-PLAN 14.186), the
second - the capture's last SECS - reads no floppy, and the silences inside
it are held to 60 ms and 120 ms in all (measured: none). RED for --hold:
--pkg the arm before the hold (14.173's base, no aphold.inc) - aph_on fails,
and its streamed silences (90-100 ms, 290-340 ms in all, measured) fail the
held bars.

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

  PAGE    the look-ahead ring was a plain pool claim, which crossed a 64KB
          physical page, so one run a pass split there and lost a
          revolution. AUDIO claims it through the DMA door with all of it
          the head (KERN-MEGA-PLAN 14.160), which on kern_mega is a pool
          block below 16MB whose 32 KB lie inside one page.

What must hold: the ring, read off its DESCRIPTOR (AUDIO's [ap_la_seg]), is a
pool block - based at or above 1MB, ending below 16MB, its 32 KB inside one
64KB page; and from the capture, the play is there (at least 9.5 s from its
first sound to its last), the FDC completed READ DATA commands during it, no
silent stretch inside it is longer than 220 ms, and the silences inside it add
up to under 700 ms. kern_big on the same machine, measured: longest 100 ms,
290 ms in all. This tree, four runs: longest 90-150 ms, 290-530 ms in all.

RED (docs/WRITING-TESTS.md 1), measured: PAGE put back - `--pkg` an
AUDIO.O88 PKG_MEGA arm built from this tree with apengine.inc's base
ap_ensure_bufs (the plain claim), `--src` that tree - the ring at 3C9C00,
across 3D0000: the placement check fails, longest 290 and 350 ms, 870 and
1,110 ms in all (each time check fails too). The base of 14.156 (both
earlier costs) had a 450-540 ms silence every 2.1 s, 3.57 s in all; SETTLE
alone put back (OS88_MEGA_POKE='mega_fdgo.ss+0xD=9090', the head always
sought) longest 500 ms, 2.84 s; BOUNCE alone (OS88_MEGA_POKE=
'dsk_runcap.hi+0x10=9090') longest 450 ms, 2.24 s.
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
import dispapps  # noqa: E402
from cycweb import pkg_syms  # noqa: E402

MACHINE = "at286_vga_sb"
RATE, SECS, TONE = 22050, 10.0, 660.0
LONGEST_MS, TOTAL_MS, MIN_PLAY = 220, 700, 9.5
HOLD_LONGEST_MS, HOLD_TOTAL_MS = 60, 120    # --hold (megasbhold): 20 ms, 30 ms

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
    ap.add_argument("--pkg", help="an AUDIO.O88 (PKG_MEGA arm) to run instead of "
                    "build/megak/apps/'s - the red leg's")
    ap.add_argument("--src", default=ROOT,
                    help="the tree whose apps/audio/ that package was built from")
    ap.add_argument("--hold", action="store_true",
                    help="megasbhold: the clip HELD whole first (SPEC.md 86.5.4) "
                    "and played from memory, the floppy idle")
    a = ap.parse_args()
    pkg = a.pkg or os88build.at("build/megak/apps/audio.o88")
    syms, img = pkg_syms(os.path.join(a.src, "apps/audio/audio.asm"),
                         (os.path.join(a.src, "apps") + "/",
                          os.path.join(a.src, "apps/audio") + "/"), ("PKG_MEGA",))
    if not a.pkg and img != open(os88build.at("build/megak/apps/audio.bin"), "rb").read():
        sys.exit("megasbgap: build/megak/apps/audio.bin is not this source's - make mega")
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
            # the player first, then the clip through File > Open: kern_mega's
            # arm HOLDS a file it can (SPEC.md 86.5.4, aphold.inc), and this
            # row's streamed case is the one a file too big to hold takes -
            # so [aph_off] is set before the open (an arm with no hold has no
            # such byte, and streams anyway)
            ui.path("B:/APPS/AUDIO.O88", limit=120)
            got = dispapps.pkg_seg(m, 0)
            pseg = got and int(got[1])
            if not a.hold and pseg and "aph_off" in syms:
                m.write(m.lin(pseg, syms["aph_off"]), b"\x01")
            if a.hold and pseg:             # the one-track list played twice
                m.write(m.lin(pseg, syms["ap_repeat"]), b"\x01")
            ui.menu_pick("File", "Open...")
            ui.chooser_open("../MEDIA/TONE22.WAV")

            def rb(name, n=1):
                return pseg and name in syms and int.from_bytes(
                    m.readseg(pseg, syms[name], n), "little")
            nd1 = None
            if a.hold:
                on = None
                for _ in range(int(SECS) * 2 + 20):   # the first play,
                    M.guest_sleep(m, 0.5)   # streamed, fills the hold
                    on = rb("aph_on")
                    if on:
                        break
                nd1 = rb("aph_ndisk", 2)
                check("HOLD: the first play's own reads hold the clip whole "
                      "(aph_on)", bool(on), "aph_on %r" % on)
                for _ in range(40):         # ...and the second opened: its
                    M.guest_sleep(m, 0.25)  # own reads (the folder, the
                    if rb("aph_ncopy", 2) and rb("ap_state") == 1:
                        break               # header) are before it, not in it
                m.write(m.lin(pseg, syms["ap_repeat"]), b"\x00")
            r0 = m.disk()["reads"]
            M.guest_sleep(m, SECS + 3)
            reads = m.disk()["reads"] - r0
            ndisk = None
            if a.hold and pseg and "aph_ndisk" in syms:
                ndisk = rb("aph_ndisk", 2) - (nd1 or 0)
            check("the window is the Audio Player's",
                  any("Audio" in t for t in ui.titles()), "titles %s" % ui.titles())
            got = dispapps.pkg_seg(m, 0)    # THE RING, off its descriptor
            sel = got and int.from_bytes(m.readseg(int(got[1]), syms["ap_la_seg"], 2),
                                         "little")
            d = m.selector(sel) if sel else None
            check("the look-ahead ring is a pool block, below 16MB, its 32 KB "
                  "inside one 64KB page (the DMA door, 14.160)",
                  d is not None and d[1] == 0x7FFF and 0x100000 <= d[0]
                  and d[0] + d[1] < 0x1000000 and d[0] >> 16 == (d[0] + d[1]) >> 16,
                  "selector %s, base, limit = %s" % (
                      sel and "%04X" % sel, d and "%06X, %04X" % d))
        got = cap + ".sound_blaster.wav"
        if a.keep:
            os.makedirs(a.keep, exist_ok=True)
            shutil.copy(got, a.keep)
        t0, t1, gaps = silences(got)
        if not check("the play is in the capture", t0 is not None):
            return 1
        if a.hold:                          # the SECOND play: the capture's
            t0 = t1 - SECS + 0.1            # last SECS (the first streamed)
            gaps = [g for g in gaps if g[0] >= t0]
        print("     play %.2f-%.2f s (%.2f s); silences %s" % (
            t0, t1, t1 - t0, ", ".join("%.2f+%dms" % g for g in gaps) or "none"))
        check("it played %.1f s or more" % MIN_PLAY, t1 - t0 >= MIN_PLAY - 0.1,
              "%.2f s" % (t1 - t0))
        if a.hold:
            check("HOLD: the floppy was NOT read for the second play (%d READ "
                  "DATA in all, at most 2 the desktop's own; aph_ndisk +%s - no "
                  "gulp off the disk)" % (reads, ndisk), reads <= 2 and ndisk == 0)
        else:
            check("the floppy was read during it (%d READ DATA)" % reads, reads >= 20)
        longest = max([ms for _, ms in gaps] or [0])
        total = sum(ms for _, ms in gaps)
        lim, tot = (HOLD_LONGEST_MS, HOLD_TOTAL_MS) if a.hold else (LONGEST_MS, TOTAL_MS)
        check("no silence inside it over %d ms" % lim, longest <= lim,
              "longest %d ms" % longest)
        check("its silences under %d ms in all" % tot, total < tot,
              "%d ms" % total)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("megasbgap%s: %s" % (" --hold" if a.hold else "", "PASS" if not fails else "FAIL (%s)" % ", ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
