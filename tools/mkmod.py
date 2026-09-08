#!/usr/bin/env python3
"""mkmod: generate the deterministic os8088 test MOD.

    python3 tools/mkmod.py OUT.mod

Emits a 5,596-byte 31-instrument ProTracker module (title "OS8088 TEST",
magic M.K.) for the Tracker package's scripted tests (docs/history/TRACKER-PLAN.md):
4 synthesized samples (square lead, triangle bass, one-shot LCG-noise hat,
sine pad -- 416 sample bytes total, all finetune 0), 4 patterns, order
table [0,1,2,3], restart 127 (which exercises the restart >= songLength ->
0 rule). The melody is Ode to Joy; a pinned, screendump-able SPREAD of v1
effects appears on fixed rows (Fxx both branches, Cxx, Axy, E9x, 4xy, 3xx,
D00 -- NOT the full v1 set, which is TRACKER-PLAN.md's 22-effect list;
arpeggio, porta up/down, sample offset, pattern loop etc. are implemented
but deliberately absent here, because adding rows would change the pinned
placements and the sndcheck solo-intro contract).

Rows 0..7 of pattern 0 play the square lead SOLO -- bass/hat/pad enter at
row 8 -- so tools/sndcheck.py sees a clean dominant frequency at song
start: period 339 -> 10,463 Hz through the 32-byte loop -> ~327 Hz.

stdlib only, no random module (the hat uses a fixed LCG): rebuilds are
byte-identical, which is what lets the row placements below be pinned by
tests. The asserts at the end are a structural self-check run on every
generation. Any failure exits 1 with a message on stderr; OUT.mod is not
written on failure.
"""
import math
import struct
import sys


def fail(msg: str) -> None:
    print(f"mkmod: error: {msg}", file=sys.stderr)
    sys.exit(1)


def be16(v: int) -> bytes:
    return struct.pack(">H", v)


def s8(v: float) -> int:
    """Clamp to a signed byte, return its unsigned 0..255 encoding."""
    v = max(-127, min(127, int(round(v))))
    return v & 0xFF


# ---- samples (computed, deterministic) --------------------------------------

def square(n=32, amp=96):
    return bytes(s8(amp if i < n // 2 else -amp) for i in range(n))


def triangle(n=64, amp=96):
    # -amp .. +amp .. -amp over n bytes (any symmetric triangle is fine).
    return bytes(s8(amp * (1 - abs((i / (n / 2.0)) % 2 - 1)) * 2 - amp)
                 for i in range(n))


def noise(n=256, amp=90):
    x, out = 0x1234, []
    for i in range(n):
        x = (x * 25173 + 13849) & 0xFFFF
        env = (n - i) / n                      # linear decay
        out.append(s8(((x >> 8) - 128) / 128.0 * amp * env))
    out[0] = out[1] = 0                        # one-shot first-word convention
    return bytes(out)


def sine(n=64, amp=80):
    return bytes(s8(amp * math.sin(2 * math.pi * i / n)) for i in range(n))


# (name, data, finetune, volume, loopstart_words, looplen_words)
SAMPLES = [
    (b"SQUARE LEAD", square(),   0, 48, 0, 16),
    (b"TRI BASS",    triangle(), 0, 56, 0, 32),
    (b"NOISE HAT",   noise(),    0, 40, 0, 1),   # looplen 1 word = one-shot
    (b"SINE PAD",    sine(),     0, 32, 0, 32),
]

# ---- pattern cells ----------------------------------------------------------

def cell(period=0, smp=0, fx=0, param=0):
    return bytes([(smp & 0xF0) | (period >> 8), period & 0xFF,
                  ((smp & 0x0F) << 4) | fx, param])


EMPTY = cell()


def pattern():
    return [[EMPTY] * 4 for _ in range(64)]


def put(pat, row, ch, *a, **k):
    pat[row][ch] = cell(*a, **k)


def decode(pat_bytes, row, ch):
    """Decode one cell out of raw pattern bytes (for the self-check)."""
    o = row * 16 + ch * 4
    b0, b1, b2, b3 = pat_bytes[o:o + 4]
    return (((b0 & 0x0F) << 8) | b1, (b0 & 0xF0) | (b2 >> 4),
            b2 & 0x0F, b3)


# Finetune-0 periods (research-mod-format.md / ft2_tables.c ptPeriods).
E2, F2, G2, D2, C2, G2B = 339, 320, 302, 381, 428, 285
C1, G1 = 856, 570

# Ode to Joy, phrase 1: E E F G  G F E D  C C D E  E D D(hold)
MELODY1 = [E2, E2, F2, G2, G2, F2, E2, D2, C2, C2, D2, E2, E2, D2, D2]
# Phrase 2: E E F G  G F E D  C C D E  D C C(hold)
MELODY2 = [E2, E2, F2, G2, G2, F2, E2, D2, C2, C2, D2, E2, D2, C2, C2]
# Closing: E D C C
MELODY3 = [E2, D2, C2, C2]


def build_patterns():
    # -- pattern 0: phrase 1. Rows 0..7 are the square lead SOLO (bass and
    # hat enter at row 8) so sndcheck gets a clean ~327 Hz at song start.
    p0 = pattern()
    put(p0, 0, 2, fx=0xF, param=0x7D)              # F7D tempo 125 (BPM branch)
    put(p0, 0, 3, fx=0xF, param=0x06)              # F06 speed 6 (parse proof)
    for i, per in enumerate(MELODY1):
        put(p0, i * 4, 0, per, 1, 0xC, 0x30)       # lead, C30 each note
    for r in range(8, 64, 8):                      # bass enters at row 8
        put(p0, r, 1, C1, 2)
        if r + 4 < 64:
            put(p0, r + 4, 1, G1, 2)
    for r in range(8, 64, 4):                      # hat enters at row 8
        put(p0, r, 2, C2, 3, 0xA, 0x08)            # hat + A08 fade tail
    put(p0, 60, 2, C2, 3, 0xE, 0x93)               # E93 retrig fill

    # -- pattern 1: phrase 2, vibrato (4x2 -> param 0x42) on the held final
    # notes; full bass/hat scheme from row 0.
    p1 = pattern()
    for i, per in enumerate(MELODY2):
        if i >= 13:
            put(p1, i * 4, 0, per, 1, 0x4, 0x42)   # 4x2 vibrato, held C's
        else:
            put(p1, i * 4, 0, per, 1, 0xC, 0x30)
    for r in range(0, 64, 8):
        put(p1, r, 1, C1, 2)
        if r + 4 < 64:
            put(p1, r + 4, 1, G1, 2)
    for r in range(0, 64, 4):
        put(p1, r, 2, C2, 3, 0xA, 0x08)
    put(p1, 60, 2, C2, 3, 0xE, 0x93)

    # -- pattern 2: the pad + tone-portamento pattern. Pad chord C-2 + G-2
    # at C20; row 16 glides ch0 to G-2 via 308; row 20 chokes ch1 with C00;
    # row 32 speeds up (F03); row 48 breaks to pattern 3 row 0 (D00), so
    # rows 49..63 never play -- the E93 row sits at 44, before the break.
    p2 = pattern()
    put(p2, 0, 0, C2, 4, 0xC, 0x20)                # pad C-2, C20
    put(p2, 0, 1, G2B, 4, 0xC, 0x20)               # pad G-2, C20 (the chord)
    put(p2, 16, 0, G2B, 0, 0x3, 0x08)              # 308 tone porta C-2 -> G-2
    put(p2, 20, 1, fx=0xC, param=0x00)             # C00 volume cut test
    put(p2, 32, 3, fx=0xF, param=0x03)             # F03 speed change
    put(p2, 48, 3, fx=0xD, param=0x00)             # D00 pattern break
    for r in range(0, 48, 4):
        put(p2, r, 2, C2, 3, 0xA, 0x08)
    put(p2, 44, 2, C2, 3, 0xE, 0x93)

    # -- pattern 3: closing phrase on half-note rows, second BPM value.
    p3 = pattern()
    put(p3, 0, 3, fx=0xF, param=0x78)              # F78 tempo 120
    for i, per in enumerate(MELODY3):
        put(p3, i * 8, 0, per, 1, 0xC, 0x30)
    for r in range(0, 64, 8):
        put(p3, r, 1, C1, 2)
        if r + 4 < 64:
            put(p3, r + 4, 1, G1, 2)
    for r in range(0, 64, 4):
        put(p3, r, 2, C2, 3, 0xA, 0x08)
    put(p3, 60, 2, C2, 3, 0xE, 0x93)

    return [p0, p1, p2, p3]


ORDERS = [0, 1, 2, 3]
RESTART = 127          # >= songLength: the loader must treat it as 0


def build() -> bytes:
    patterns = build_patterns()

    out = b"OS8088 TEST".ljust(20, b"\x00")
    for name, data, ft, vol, ls, ll in SAMPLES:
        assert len(data) % 2 == 0, "sample byte length must be even"
        out += name.ljust(22, b"\x00") + be16(len(data) // 2)
        out += bytes([ft & 0x0F, vol]) + be16(ls) + be16(ll)
    out += (b"\x00" * 30) * (31 - len(SAMPLES))    # unused slots: all zero
    out += bytes([len(ORDERS), RESTART])           # songLength, restart
    out += bytes(ORDERS).ljust(128, b"\x00")
    out += b"M.K."
    for pat in patterns:
        out += b"".join(c for row in pat for c in row)
    for _, data, *_ in SAMPLES:
        out += data
    return out


def self_check(out: bytes) -> None:
    nsmp = sum(len(d) for _, d, *_ in SAMPLES)
    assert nsmp == 416, f"sample data is {nsmp} bytes, plan says 416"
    assert len(out) == 1084 + 1024 * len(ORDERS) + nsmp
    assert len(out) == 5596, f"file is {len(out)} bytes, plan says 5596"
    assert out[1080:1084] == b"M.K."
    assert out[950] == 4 and out[951] == 127
    assert bytes(out[952:956]) == bytes(ORDERS)
    # Pattern count P = max over ALL 128 order bytes, +1.
    assert max(out[952:1080]) + 1 == 4
    pats = [out[1084 + 1024 * p:1084 + 1024 * (p + 1)] for p in range(4)]
    # The pinned effect rows.
    assert decode(pats[0], 0, 2) == (0, 0, 0xF, 0x7D)     # F7D tempo
    assert decode(pats[0], 0, 3) == (0, 0, 0xF, 0x06)     # F06 speed
    assert decode(pats[0], 60, 2)[2:] == (0xE, 0x93)      # E93 retrig
    assert decode(pats[1], 56, 0) == (C2, 1, 0x4, 0x42)   # 4x2 vibrato
    assert decode(pats[2], 16, 0) == (G2B, 0, 0x3, 0x08)  # 308 tone porta
    assert decode(pats[2], 20, 1) == (0, 0, 0xC, 0x00)    # C00 cut
    assert decode(pats[2], 32, 3) == (0, 0, 0xF, 0x03)    # F03 speed
    assert decode(pats[2], 48, 3) == (0, 0, 0xD, 0x00)    # D00 break
    assert decode(pats[3], 0, 3) == (0, 0, 0xF, 0x78)     # F78 tempo
    # The solo intro: rows 0..7 of pattern 0 carry NO note outside ch0
    # (the two row-0 cells on ch2/ch3 are effect-only).
    for row in range(8):
        for ch in (1, 2, 3):
            period, smp, _, _ = decode(pats[0], row, ch)
            assert period == 0 and smp == 0, \
                f"pattern 0 row {row} ch {ch} breaks the solo intro"
    assert decode(pats[0], 0, 0)[:2] == (E2, 1)           # ...and ch0 plays
    # One-shot hat: first word of its sample data is 0,0.
    hat_off = 1084 + 4096 + len(SAMPLES[0][1]) + len(SAMPLES[1][1])
    assert out[hat_off:hat_off + 2] == b"\x00\x00"


def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: python3 tools/mkmod.py OUT.mod")
    out = build()
    self_check(out)
    try:
        with open(sys.argv[1], "wb") as f:
            f.write(out)
    except OSError as e:
        fail(f"cannot write {sys.argv[1]}: {e}")
    print(f"mkmod: {sys.argv[1]} ({len(out)} bytes, 4 patterns, "
          f"{sum(len(d) for _, d, *_ in SAMPLES)} sample bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
