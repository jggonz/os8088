#!/usr/bin/env python3
"""os88avsync - is the row on the glass the row in the air? (SPEC.md 45.15)

WHY THIS EXISTS. Every check of SPEC.md 45.15 so far has been a check of the
model against ITSELF - the interpolation against the app's own cached copy of
the driver's counter, both of them the app's opinion. That cannot answer the
only question a listener asks. This measures the display against the
HARDWARE.

THE ONE INSTANT WHEN THE TRUTH IS EXACT. The driver's `consumed` advances one
whole DMA half per block IRQ, so between IRQs it is up to a block behind the
card - 2,048 bytes, 372 ms at XT mode's 5,500 Hz, nearly three rows. Sampled
at a random moment it can prove nothing. But AT THE INSTANT IT CHANGES it is
not an estimate at all: the DMA has just finished that half, so `consumed` IS
the byte the card is playing. So this polls until it sees the edge, and only
then compares. Everything else about the run is thrown away.

WHAT IT REPORTS, at those edges:
  PLAY - CONS   the model's lead over the truth, in bytes and ms. The model
                interpolates PAST the quantized report on purpose (45.15.1),
                so a small positive number is by design; a large one, or a
                negative one, is not.
  row error     the row being DISPLAYED minus the row the card is actually
                inside, resolved through the stamp ring. This is the number a
                listener perceives, and 1 row is 140 ms at the XT default.

The wav is deliberately NOT used. MartyPC writes one wav per source at that
source's own rate with a fixed 44100 header, so sample index does not map to
guest time (measured: 115 s of Sound Blaster, 413 s of AdLib and 224 s of
speaker over the same 184 s of guest time). Correlating audio onsets against
screen updates needs that mapping and cannot have it.

DO NOT PAUSE THE MACHINE DURING A RUN - no `pace`, no `flicker`, no `step`.

Usage:
    os88avsync.py 127.0.0.1:9001 [--secs 40] [--src apps/tracker/tracker.asm]

Tracker must be PLAYING. Either surface works: the numbers come from memory,
not from pixels.
"""
import sys, os, re, struct, subprocess, argparse, statistics
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from os88marty import Marty, MartyError

GUEST_HZ = 4772728.0
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WANT_MEM = ("trk_total", "trk_consumed", "tui_play", "tui_arow",
            "mp_stw", "mp_str")
WANT_IMM = ("mp_stamps",)
MP_ST_N, MP_ST_SIZE, MP_SP_ROW = 64, 16, 4


def symbols(src):
    """Offsets by NAME, out of a fresh NASM listing - so this survives every
    rebuild instead of carrying numbers that rot."""
    lst = "/tmp/os88avsync.lst"
    subprocess.run(["nasm", "-f", "bin", "-w+error", "-I", "apps/",
                    "-I", "apps/tracker/", "-o", "/tmp/os88avsync.bin",
                    "-l", lst, src], cwd=ROOT, check=True)
    lines = open(lst).read().splitlines()
    out = {}
    for name in WANT_MEM:                       # [addr] operands, byte-swapped
        for L in lines:
            m = re.search(r"\[([0-9A-F]{2})([0-9A-F]{2})\]", L)
            if m and re.search(r"\[%s\]" % name, L):
                out[name] = int(m.group(2) + m.group(1), 16)
                break
    for name in WANT_IMM:                       # a bare immediate, same order
        for L in lines:
            m = re.search(r"\[([0-9A-F]{2})([0-9A-F]{2})\]", L)
            if m and re.search(r",\s*%s\b" % name, L):
                out[name] = int(m.group(2) + m.group(1), 16)
                break
    missing = [n for n in WANT_MEM + WANT_IMM if n not in out]
    if missing:
        raise SystemExit("could not find %s in the listing" % ", ".join(missing))
    return out


def find_pkg(m, name=b"TRACKER"):
    buf = m.read(0x40000, 0xA0000 - 0x40000)
    for off in range(0, len(buf) - 32, 16):
        if buf[off:off + 2] == b"O8" and buf[off + 2] == 3 \
           and buf[off + 16:off + 16 + len(name)] == name:
            return (0x40000 + off) >> 4
    raise MartyError("no loaded TRACKER package found - is it running?")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("--secs", type=float, default=40.0)
    ap.add_argument("--rate", type=float, default=5500.0,
                    help="the stream's byte rate; XT mode is 5500")
    ap.add_argument("--src", default="apps/tracker/tracker.asm")
    a = ap.parse_args()

    sym = symbols(a.src)
    m = Marty(a.addr)
    seg = find_pkg(m)
    print("TRACKER at %04X; trk_consumed +%04X, tui_play +%04X, mp_stamps +%04X"
          % (seg, sym["trk_consumed"], sym["tui_play"], sym["mp_stamps"]))

    lead, rowerr, prev = [], [], None
    t0 = m.cmd(cmd="status")["cycles"] / GUEST_HZ
    while True:
        t = m.cmd(cmd="status")["cycles"] / GUEST_HZ
        tot, cons = struct.unpack("<HH", m.readseg(seg, sym["trk_total"], 4))
        if prev is not None and cons != prev:
            play, = struct.unpack("<H", m.readseg(seg, sym["tui_play"], 2))
            d = (play - cons) & 0xFFFF
            lead.append(d - 65536 if d > 0x8000 else d)
            ring = m.readseg(seg, sym["mp_stamps"], MP_ST_N * MP_ST_SIZE)
            w = m.readseg(seg, sym["mp_stw"], 2)
            arow = m.readseg(seg, sym["tui_arow"], 1)[0]
            best, i = None, w[1]
            while i != w[0]:                    # the stamp the CARD is inside
                k = (i & (MP_ST_N - 1)) * MP_ST_SIZE
                pos, = struct.unpack("<H", ring[k:k + 2])
                dl = (cons - pos) & 0xFFFF
                if dl < 0x8000 and (best is None or dl < best[0]):
                    best = (dl, ring[k + MP_SP_ROW])
                i = (i + 1) & 0xFF
            if best is not None:
                e = (arow - best[1]) & 0xFF
                if abs(e - 256) < abs(e):
                    e -= 256
                if -8 < e < 8:                  # drop pattern wraps: a row
                    rowerr.append(e)            # number alone cannot span them
        prev = cons
        if t - t0 > a.secs:
            break

    if len(lead) < 10:
        raise SystemExit("only %d block edges seen - is it playing?" % len(lead))
    ms = lambda b: b / a.rate * 1000.0
    print("\nblock edges caught: %d over %.0f guest s" % (len(lead), a.secs))
    print("PLAY - CONS (the model's lead over the truth):")
    print("   median %+d bytes (%+.0f ms)   mean %+.0f   min %+d   max %+d"
          % (statistics.median(lead), ms(statistics.median(lead)),
             statistics.mean(lead), min(lead), max(lead)))
    print("row shown MINUS row the card is inside: %s" % dict(Counter(rowerr)))
    if rowerr:
        early = sum(1 for e in rowerr if e > 0) * 100.0 / len(rowerr)
        print("   %.0f%% of edges show a row EARLY (1 row = %.0f ms)"
              % (early, ms(a.rate * 140 / 1000.0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
