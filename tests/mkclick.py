#!/usr/bin/env python3
"""mkclick - generate CLICK.MOD, a metronome for judging A/V sync by eye and ear.

WHAT IT IS FOR. "The music is not synced to the display" is very hard to
judge against real music: notes are everywhere, so there is nothing to time
the display against. This module plays ONE short click, on ONE channel, every
TWO SECONDS, and nothing else - so the question becomes a single observation
anybody can make without instruments:

    when you HEAR the click, what row does the screen SHOW?

The notes are on rows 00, 10, 20 and 30 (hex) of a single 64-row pattern, so
the expected answer is one of those four. Anything else is the offset, read
straight off the screen in rows, and a row here is exactly 125 ms.

THE TIMING IS CHOSEN TO MAKE THAT ARITHMETIC EXACT. A MOD row lasts
speed / (BPM * 0.4) seconds. At BPM 120 and speed 6 that is 6/48 = 0.125 s
exactly, so 16 rows is 2.000 s and the whole 64-row pattern is 8 s. The
pattern is the only order, so it loops forever.

The click itself is a decaying square burst about an eighth of a second long
- a sharp attack, because an attack is what the ear times, and a long gap
after it so there is no ambiguity about which click you are hearing.

Deterministic: same bytes every run, like everything else this tree
generates. Writes to the path given, default build/click.mod.
"""
import sys, struct

TITLE = b"os8088 sync click"
SMPNAME = b"click"
PERIOD_C2 = 428                 # C-2 in the ProTracker period table
SMPLEN = 1024                   # bytes; even, as the format's word count needs
ROWS = 64
NOTE_EVERY = 16                 # rows -> 2.000 s at BPM 120 / speed 6
BPM, SPEED = 120, 6


def sample_data():
    """A decaying square burst. At period 428 the replay rate is
    7093789.2/(428*2) = 8287 Hz, so 1024 bytes is 124 ms and flipping every
    four samples is about 1 kHz - high enough to hear as a tick, low enough
    to survive an 8-bit mixer."""
    out = bytearray()
    for i in range(SMPLEN):
        amp = 127 * (SMPLEN - i) // SMPLEN          # linear decay to silence
        out.append((amp if (i // 4) % 2 == 0 else -amp) & 0xFF)
    out[0] = 0                                       # the two bytes a MOD
    out[1] = 0                                       # player may use as a loop
    return bytes(out)


def cell(sample, period, eff, parm):
    return bytes((
        (sample & 0xF0) | ((period >> 8) & 0x0F),
        period & 0xFF,
        ((sample & 0x0F) << 4) | (eff & 0x0F),
        parm & 0xFF,
    ))


def build():
    m = bytearray()
    m += TITLE.ljust(20, b"\0")
    for s in range(31):                              # 31 sample headers
        if s == 0:
            m += SMPNAME.ljust(22, b"\0")
            m += struct.pack(">H", SMPLEN // 2)      # length in WORDS, big-endian
            m += bytes((0, 64))                      # finetune 0, volume 64
            m += struct.pack(">HH", 0, 1)            # no loop: start 0, len 1
        else:
            m += b"\0" * 22 + struct.pack(">H", 0) + bytes((0, 0)) \
                 + struct.pack(">HH", 0, 1)
    m += bytes((1, 127))                             # 1 order, restart 127
    m += bytes([0] + [0] * 127)                      # order table: pattern 0
    m += b"M.K."

    pat = bytearray()
    for r in range(ROWS):
        if r == 0:
            pat += cell(1, PERIOD_C2, 0x0F, SPEED)   # note + Fxx = set speed
            pat += cell(0, 0, 0x0F, BPM)             # ...and Fxx = set tempo
            pat += cell(0, 0, 0, 0)
            pat += cell(0, 0, 0, 0)
        elif r % NOTE_EVERY == 0:
            pat += cell(1, PERIOD_C2, 0, 0)          # the click, channel 1
            pat += cell(0, 0, 0, 0) * 3
        else:
            pat += cell(0, 0, 0, 0) * 4
    m += pat
    m += sample_data()
    return bytes(m)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "build/click.mod"
    data = build()
    open(out, "wb").write(data)
    print("mkclick: %s (%d bytes) - a click on rows 00/10/20/30, "
          "%d ms a row, 2.000 s a click"
          % (out, len(data), 1000 * SPEED // (BPM * 2 // 5)))
