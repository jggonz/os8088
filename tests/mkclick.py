#!/usr/bin/env python3
"""mkclick - generate CLICK.MOD, a metronome for judging A/V sync by eye and ear.

WHAT IT IS FOR. "The music is not synced to the display" is very hard to
judge against real music: notes are everywhere, so there is nothing to time
the display against. This module plays ONE short click, on ONE channel, every
TWO SECONDS, and nothing else - so the question becomes a single observation
anybody can make without instruments:

    when you HEAR the click, what row does the screen SHOW?

The notes are on rows 00, 10, 20 and 30 (hex), so the expected answer is one
of those four. Anything else is the offset, read straight off the screen in
rows, and a row here is exactly 125 ms.

THE TIMING IS CHOSEN TO MAKE THAT ARITHMETIC EXACT. A MOD row lasts
speed / (BPM * 0.4) seconds. At BPM 120 and speed 6 that is 6/48 = 0.125 s
exactly, so 16 rows is 2.000 s and a 64-row pattern is 8 s.

FOUR ORDERS AND FOUR PITCHES, which the first version did not have and which
cost a round of field testing. With one order and one pitch:

  - `Pos` never leaves 00, so half the readout is dead and the P key - the
    PATTERN LOOP toggle - has nothing to loop that the song does not already
    play, which makes it look like a bare restart;
  - and every click sounds the same, so the ear cannot tell one from the next.
    "The row count changes, but most of them are unrelated to an actual noise"
    is that in the field's own words: 60 of the 64 rows are silent by design,
    and with nothing to distinguish the four that are not, the display and the
    music cannot be tied together by ear at all.

So: four patterns, four orders, and a pitch per pattern (C-2, E-2, G-2, C-3).
`Pos` walks 00 01 02 03 and the ear hears the song climb, so the position on
screen is CHECKABLE against what is sounding - and P now loops one rung of
that climb, audibly.

THE DOWNBEAT IS MARKED TOO. Row 00 of every pattern is a fifth above that
pattern's other three clicks, so a bar of four reads as TICK tick tick tick -
the ear knows which of the four it is hearing without counting from the start.

The click itself is a decaying square burst about an eighth of a second long
- a sharp attack, because an attack is what the ear times, and a long gap
after it so there is no ambiguity about which click you are hearing.

Deterministic: same bytes every run, like everything else this tree
generates. Writes to the path given, default build/click.mod.
"""
import sys, struct

TITLE = b"os8088 sync click"
SMPNAME = b"click"
SMPLEN = 1024                   # bytes; even, as the format's word count needs
ROWS = 64
NOTE_EVERY = 16                 # rows -> 2.000 s at BPM 120 / speed 6
BPM, SPEED = 120, 6

# ProTracker periods. One per PATTERN, so the order position is audible; the
# downbeat of each bar is a fifth above its pattern's own pitch, so which of
# the four clicks in a bar you are hearing is audible too.
PERIOD = [428, 339, 285, 214]           # C-2  E-2  G-2  C-3
DOWNBEAT = [285, 226, 190, 143]         # G-2  B-2  D-3  G-3
NPAT = len(PERIOD)


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


def pattern(n):
    pat = bytearray()
    for r in range(ROWS):
        if r % NOTE_EVERY:
            pat += cell(0, 0, 0, 0) * 4
            continue
        p = DOWNBEAT[n] if r == 0 else PERIOD[n]
        if r == 0 and n == 0:                        # the song's own first row
            pat += cell(1, p, 0x0F, SPEED)           # sets speed...
            pat += cell(0, 0, 0x0F, BPM)             # ...and tempo
            pat += cell(0, 0, 0, 0) * 2
        else:
            pat += cell(1, p, 0, 0)                  # the click, channel 1
            pat += cell(0, 0, 0, 0) * 3
    return bytes(pat)


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
    m += bytes((NPAT, 127))                          # orders, restart 127
    m += bytes(list(range(NPAT)) + [0] * (128 - NPAT))
    m += b"M.K."
    for n in range(NPAT):
        m += pattern(n)
    m += sample_data()
    return bytes(m)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "build/click.mod"
    data = build()
    open(out, "wb").write(data)
    print("mkclick: %s (%d bytes) - %d patterns, a click on rows 00/10/20/30 "
          "of each, %d ms a row, 2.000 s a click, %d s a position"
          % (out, len(data), NPAT, 1000 * SPEED // (BPM * 2 // 5),
             ROWS * SPEED // (BPM * 2 // 5)))
