#!/usr/bin/env python3
"""MIDIRack's demo songs: compose them, and check them with a second reader.

    python3 tools/os88midsong.py --out apps/midirack/songs
    python3 tools/os88midsong.py --check apps/midirack/songs

These are the ten Standard MIDI Files MIDIRack ships beside itself (SPEC.md
§105): a title fanfare, two pieces of 1990s-style game music, six classics
everyone knows and a feature test.

The fanfare, the game music and the feature test are ORIGINAL - composed here,
quoting no existing melody. The six classics are PUBLIC-DOMAIN compositions -
Beethoven's Fur Elise, Joplin's The Entertainer, Grieg's In the Hall of the
Mountain King, the Russian folk song Korobeiniki, Offenbach's Can-can and
Mozart's Rondo alla Turca, all published before 1903 - and only their
melodies are taken; every arrangement (voicing, accompaniment, drums, form)
is written here. Copyrighted popular songs are deliberately NOT here: a MIDI
transcription of one is a copy of the composition, and these files ship. The
originals, every arrangement and the files this tool writes are dedicated to
the public domain under CC0 1.0, and every file says which it is in its
first track.

`--out` is byte-for-byte deterministic: nothing reads the clock, and the only
randomness (a few units of velocity, so a part does not sound typed in) comes
from a `random.Random` seeded from the file name.

The songs are written for the machine that plays them - an 8086 driving an
OPL2 - so the composer keeps to what it can voice: at most 8 melodic notes
sounding at once (6 most of the time) and at most 3 drum notes, drum hits no
longer than a 16th, drums only on channel 10 (index 9), GM programs and GM
drum keys only, every file under 12KB and the ten under 64KB. Both SMF
formats are exercised (INTRO is format 0, the rest format 1), and both a
small and a large division (96, and 480 for DEMO). The writer
uses running status wherever the spec allows it.

`--check` is a SECOND, INDEPENDENT reader: it shares no code or data
structure with the writer above it, re-parses each file from its bytes and
refuses anything the constraints above forbid. Polyphony is counted at the
worst case - at a tick where one note ends and another starts, the start is
counted first - and a note the sustain pedal holds counts as sounding.
"""

import argparse
import itertools
import os
import random
import sys
import zlib

COPYRIGHT = 'MIDIRack demo song for os8088 - CC0'
PD_COPYRIGHT = ('Public-domain composition; this arrangement for os8088 '
                'MIDIRack is CC0')

SONGS = ('INTRO.MID', 'SPACEJAM.MID', 'BATTLE1.MID', 'ENTERTNR.MID',
         'FURELISE.MID', 'MOUNTKNG.MID', 'KOROBEIN.MID', 'CANCAN.MID',
         'TURKISH.MID', 'DEMO.MID')

FILE_MAX = 12 * 1024
TOTAL_MAX = 64 * 1024
MELODIC_MAX = 8
DRUM_MAX = 3
DRUM_CH = 9

# GM drum keys
K2, K, SS, SN, CL, ES = 35, 36, 37, 38, 39, 40
T_LF, HH, T_HF, PH, T_LM, OH, T_HM = 41, 42, 43, 44, 45, 46, 47
T_LH, CR, T_HH, RD, CHN, TAM, MAR = 48, 49, 50, 51, 52, 54, 70


# ---------------------------------------------------------------------------
# the writer
# ---------------------------------------------------------------------------

NOTE_PC = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}

QUAL = {'': (0, 4, 7), 'm': (0, 3, 7), '7': (0, 4, 7, 10),
        'm7': (0, 3, 7, 10), 'maj7': (0, 4, 7, 11), 'sus4': (0, 5, 7),
        'sus2': (0, 2, 7), 'dim': (0, 3, 6), '5': (0, 7)}

# which chord tones survive when a voicing has fewer voices than the chord:
# the third first, then the seventh, then the root, the fifth last
KEEP = (1, 3, 0, 2)


def pitch(tok):
    """'c4' is 60, 'f#5' 78, 'bb3' 58."""
    t = tok.lower()
    pc = NOTE_PC[t[0]]
    i = 1
    if len(t) > 2 and t[1] in '#b':
        pc += 1 if t[1] == '#' else -1
        i = 2
    return 12 * (int(t[i:]) + 1) + pc


def root_pc(name):
    pc = NOTE_PC[name[0].lower()]
    n = 1
    if len(name) > 1 and name[1] in '#b':
        pc += 1 if name[1] == '#' else -1
        n = 2
    return pc % 12, name[n:]


def chord_tones(sym):
    main, _, bass = sym.partition('/')
    root, qual = root_pc(main)
    pcs = [(root + i) % 12 for i in QUAL[qual]]
    return pcs, (root_pc(bass)[0] if bass else root)


def voicing(sym, n, center, prev=None):
    pcs, _ = chord_tones(sym)
    order = [i for i in KEEP if i < len(pcs)]
    pcs = [pcs[i] for i in sorted(order[:n])]
    cands = [[p for p in range(center - 9, center + 10) if p % 12 == pc]
             for pc in pcs]
    best = None
    for combo in itertools.product(*cands):
        s = sorted(combo)
        if s[-1] - s[0] > 14 or len(set(s)) < len(s):
            continue
        mid = abs(sum(s) / len(s) - center)
        if prev and len(prev) == len(s):
            score = sum(abs(a - b) for a, b in zip(s, prev)) + mid / 4
        else:
            score = mid
        key = (score, s)
        if best is None or key < best:
            best = key
    return best[1]


def bass_note(sym, lo):
    return lo + (chord_tones(sym)[1] - lo) % 12


def prog(text, beat):
    out = []
    for tok in text.split():
        sym, beats = tok.rsplit(':', 1)
        out.append((sym, round(float(beats) * beat)))
    return out


def tokens(text):
    """'e5:4 g5:2! r:2 | ...' -> bars of (pitches or None, units, accent).
    A length carries forward when a token leaves it off; '!' accents a note
    and '~' softens it; 'c4+e4' is a chord; integers are relative pitches."""
    bars, last = [], None
    for seg in text.split('|'):
        bar = []
        for tok in seg.split():
            acc = 0
            while tok[-1] in '!~':
                acc += 18 if tok[-1] == '!' else -18
                tok = tok[:-1]
            if ':' in tok:
                tok, d = tok.split(':')
                last = int(d)
            if last is None:
                raise ValueError('no length on ' + tok)
            bar.append((None if tok == 'r' else tok.split('+'), last, acc))
        if bar:
            bars.append(bar)
    return bars


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


class Part:
    def __init__(self, song, name, ch, prog_no, vol, pan):
        self.song, self.name, self.ch = song, name, ch
        self.notes = []
        self.ctl = []
        if prog_no is not None:
            self.program(0, prog_no)
        self.cc(0, 7, vol)
        self.cc(0, 10, pan)

    def cmsg(self, t, hi, *data):
        self.ctl.append((t, self.song.nseq(), hi | self.ch, bytes(data)))

    def cc(self, t, c, v):
        self.cmsg(t, 0xB0, c, v)

    def program(self, t, p):
        self.cmsg(t, 0xC0, p)

    def bend(self, t, v):
        self.cmsg(t, 0xE0, v & 0x7F, v >> 7)

    def note(self, t, dur, key, vel=90, gate=0.9):
        div = self.song.div
        if self.ch == DRUM_CH:
            ln = min(dur, div // 8)
        else:
            ln = max(1, min(int(dur * gate), dur - max(1, div // 48)))
        vel = max(1, min(127, vel + self.song.rng.randint(-4, 4)))
        assert 0 <= key <= 127, (self.name, key)
        self.notes.append((t, t + ln, key, vel, self.song.nseq()))

    def mel(self, t, text, unit, vel=96, gate=0.9, tr=0, bar=None):
        for b in tokens(text):
            n = sum(x[1] for x in b)
            if bar is not None and n != bar:
                raise ValueError('%s %s: a bar of %d units, not %d: %r'
                                 % (self.song.fname, self.name, n, bar, b))
            for ps, d, acc in b:
                for p in ps or ():
                    self.note(t, d * unit, pitch(p) + tr, vel + acc, gate)
                t += d * unit
        return t

    def riff(self, t, chords, text, unit, lo, vel=96, gate=0.9):
        pat = [x for b in tokens(text) for x in b]
        for sym, dur in chords:
            end, i = t + dur, 0
            if sym != 'r':
                root = bass_note(sym, lo)
                while t < end:
                    ps, d, acc = pat[i % len(pat)]
                    i += 1
                    dd = min(d * unit, end - t)
                    for p in ps or ():
                        self.note(t, dd, root + int(p), vel + acc, gate)
                    t += dd
            t = end
        return t

    def pad(self, t, chords, center=60, vel=70, n=3, gate=1.0, tie=True):
        merged = []
        for sym, d in chords:
            if tie and merged and merged[-1][0] == sym:
                merged[-1][1] += d
            else:
                merged.append([sym, d])
        prev = None
        for sym, d in merged:
            if sym != 'r':
                prev = voicing(sym, n, center, prev)
                for k in prev:
                    self.note(t, d, k, vel, gate)
            t += d
        return t

    def arp(self, t, chords, idx, step, center, lo, vel=80, gate=0.9):
        prev = None
        for sym, d in chords:
            prev = voicing(sym, 3, center, prev)
            tones = [bass_note(sym, lo)] + prev + [k + 12 for k in prev]
            for k in range(d // step):
                self.note(t + k * step, step, tones[idx[k % len(idx)]],
                          vel + (8 if k == 0 else 0), gate)
            t += d
        return t

    def stabs(self, t, chords, pattern, step, center, vel=84, n=3, steps=1,
              gate=0.85):
        t0, prev = t, None
        for sym, d in chords:
            prev = voicing(sym, n, center, prev)
            for tt in range(t, t + d, step):
                c = pattern[((tt - t0) // step) % len(pattern)]
                if c in 'xX':
                    for k in prev:
                        self.note(tt, steps * step, k,
                                  vel + (16 if c == 'X' else 0), gate)
            t += d
        return t

    def beat(self, t, reps, pat, step, vel=96, vels=None):
        length = len(next(iter(pat.values())))
        assert all(len(s) == length for s in pat.values()), pat
        for _ in range(reps):
            for i in range(length):
                for key, s in pat.items():
                    c = s[i]
                    if c == '.':
                        continue
                    v = (vels or {}).get(key, vel)
                    v += {'x': 0, 'X': 24, 'g': -40}[c]
                    self.note(t + i * step, step, key, v)
            t += length * step
        return t

    def events(self):
        gap = max(1, self.song.div // 48)
        by_key = {}
        for n in self.notes:
            by_key.setdefault(n[2], []).append(n)
        evs = []
        for key in sorted(by_key):
            ns = sorted(by_key[key])
            for i, (s, e, k, v, sq) in enumerate(ns):
                if i + 1 < len(ns):
                    nxt = ns[i + 1][0]
                    if nxt <= s:        # the same key struck twice at once
                        continue
                    e = min(e, max(s + 1, nxt - gap))
                evs.append((s, 3, sq, ('ch', 0x90 | self.ch, bytes((k, v)))))
                evs.append((e, 1, sq, ('ch', 0x90 | self.ch, bytes((k, 0)))))
        for t, sq, st, data in self.ctl:
            evs.append((t, 2, sq, ('ch', st, data)))
        return evs


class Song:
    def __init__(self, fname, title, div, fmt, bpm, ts, notice=COPYRIGHT):
        self.fname, self.title, self.div, self.fmt = fname, title, div, fmt
        self.rng = random.Random(zlib.crc32(fname.encode('ascii')))
        self.parts, self.metas, self.seq = [], [], 0
        self.meta(0, 0x02, notice.encode('ascii'))
        self.timesig(0, *ts)
        self.tempo(0, bpm)
        self.bar = div * 4 * ts[0] // ts[1]

    def nseq(self):
        self.seq += 1
        return self.seq

    def meta(self, t, typ, data):
        self.metas.append((t, 0, self.nseq(), ('meta', typ, data)))

    def tempo(self, t, bpm):
        self.meta(t, 0x51, round(60000000 / bpm).to_bytes(3, 'big'))

    def timesig(self, t, num, den):
        clocks = 36 if den == 8 and num % 3 == 0 else 24
        self.meta(t, 0x58, bytes((num, den.bit_length() - 1, clocks, 8)))

    def part(self, name, ch, prog_no, vol=100, pan=64):
        p = Part(self, name, ch, prog_no, vol, pan)
        self.parts.append(p)
        return p

    @staticmethod
    def track(name, evs, end):
        evs = sorted([(0, -1, 0, ('meta', 0x03, name.encode('ascii')))]
                     + evs + [(end, 9, 0, ('meta', 0x2F, b''))],
                     key=lambda e: e[:3])
        out, last, rs = bytearray(), 0, None
        for tick, _, _, (kind, a, b) in evs:
            out += vlq(tick - last)
            last = tick
            if kind == 'meta':
                out += bytes((0xFF, a)) + vlq(len(b)) + b
                rs = None               # a meta event cancels running status
            else:
                if a != rs:
                    out.append(a)
                    rs = a
                out += b
        return b'MTrk' + len(out).to_bytes(4, 'big') + bytes(out)

    def build(self):
        part_evs = [p.events() for p in self.parts]
        last = max([e[0] for evs in part_evs for e in evs]
                   + [m[0] for m in self.metas])
        end = last + self.div           # one beat of rest after the last note
        if self.fmt == 0:
            tracks = [self.track(self.title, self.metas + sum(part_evs, []),
                                 end)]
        else:
            tracks = [self.track(self.title, list(self.metas), end)]
            tracks += [self.track(p.name, evs, end)
                       for p, evs in zip(self.parts, part_evs)]
        head = (b'MThd' + (6).to_bytes(4, 'big')
                + self.fmt.to_bytes(2, 'big')
                + len(tracks).to_bytes(2, 'big')
                + self.div.to_bytes(2, 'big'))
        return head + b''.join(tracks)


# ---------------------------------------------------------------------------
# the songs
# ---------------------------------------------------------------------------

def song_intro():
    s = Song('INTRO.MID', 'Title Fanfare', 96, 0, 110, (4, 4))
    S, B = 24, s.bar
    lead = s.part('Brass lead', 0, 61, 112, 64)
    horn = s.part('French horn', 1, 60, 92, 40)
    strg = s.part('Strings', 2, 48, 84, 88)
    tuba = s.part('Tuba', 3, 58, 96, 60)
    timp = s.part('Timpani', 4, 47, 100, 64)
    drum = s.part('Drums', DRUM_CH, None, 96, 64)
    body = ("C:4 F:2 G:2 C:4 G:4 F:4 C:4 G7:4 G:4 C:4 Am:2 G:2 F:4 G:4 "
            "Ab:4")
    P = prog(body + " Bb:4 C:4", 96)
    lead.mel(0, """
        g4:3! g4:1 c5:4 e5:4 g5:4 | f5:6! e5:2 d5:4 g4:4 |
        c5:3! c5:1 e5:4 g5:4 c6:4 | b5:6 a5:2 g5:8 |
        a5:4! f5:4 c5:4 a5:4 | g5:4 e5:4 c5:4 e5:4 |
        f5:4 d5:4 b4:4 d5:4 | g5:12! g4:4 |
        c5:3! c5:1 e5:4 g5:4 c6:4 | e6:6! d6:2 c6:4 b5:4 |
        a5:4 c6:4 f5:4 a5:4 | g5:6 e5:2 d5:4 b4:4 |
        c6:4! eb6:4 ab5:4 c6:4 | d6:4! f6:4 bb5:4 d6:4 | c6:16!
    """, S, vel=100, gate=0.88, bar=16)
    horn.pad(0, P, center=64, vel=78, n=1, tie=False)
    strg.pad(0, P, center=60, vel=70)
    tuba.riff(0, P[:-1], "0:4 -5:4 0:4 -5:4", S, lo=36, vel=92, gate=0.7)
    tuba.note(14 * B, B, 36, 104, 0.95)
    timp.riff(0, prog(body, 96), "0:4! r:4 0:4 r:4", S, lo=36, vel=88,
              gate=0.5)
    for i in range(16):                 # a roll into the last chord
        timp.note(13 * B + i * S, S, 41, 56 + i * 4, 0.8)
    timp.note(14 * B, 8 * S, 36, 120, 0.9)
    march = {K: "x.......x.......", SN: "....x..x....x.xx"}
    drum.beat(0, 13, march, S, vel=86)
    drum.beat(13 * B, 1, {K: "x.......x.......", SN: "x.x.x.x.xxxxxxxX"},
              S, vel=80)
    for bar in (0, 8, 14):
        drum.note(bar * B, S, CR, 112)
    drum.note(14 * B, S, K, 118)
    return s


def song_spacejam():
    s = Song('SPACEJAM.MID', 'Orbital Groove', 96, 1, 118, (4, 4))
    S, B = 24, s.bar
    bass = s.part('Synth bass', 0, 38, 108, 64)
    lead = s.part('Synth lead', 1, 81, 96, 76)
    stab = s.part('Poly stabs', 2, 90, 80, 44)
    pad = s.part('Synth strings', 3, 50, 70, 92)
    drum = s.part('Drums', DRUM_CH, None, 100, 64)
    PI = prog("Em7:4 A:4", 96)
    PA = PI * 4
    PB = prog("C:4 D:4 Em:4 Em:4 C:4 D:4 B7:4 B7:4", 96)
    PO = prog("Em7:4 A:4 Em7:4", 96)
    MA = """
        r:4 b4:2 d5:2 e5:3! d5:1 b4:2 a4:2 | g4:2 a4:4 r:2 e4:2 g4:2 a4:4 |
        r:4 b4:2 d5:2 e5:3! g5:1 e5:2 d5:2 | c#5:4 b4:2 a4:2 r:8 |
        r:2 e5:2 g5:2 a5:2 b5:4! a5:2 g5:2 | e5:6 d5:2 e5:4 r:4 |
        r:2 g5:2 f#5:2 e5:2 d5:2 e5:2 b4:4 | a4:4 c#5:4 e5:8!
    """
    MB = """
        g5:8! e5:4 g5:4 | f#5:8! a5:4 f#5:4 | e5:12 r:2 d5:2 |
        e5:4 b4:4 g5:4 e5:4 | c6:8! b5:4 g5:4 | a5:8 f#5:4 d5:4 |
        d#5:6 f#5:2 a5:4 b5:4 | b5:12! r:4
    """
    bass.riff(0, PI + PA + PB + PA + PO,
              "0:2! r:1 0:1 12:1 r:1 10:1 12:1 r:2 7:2 5:1 r:1 7:2",
              S, lo=28, vel=100, gate=0.8)
    bass.note(29 * B, 8 * S, 40, 116)
    stab.stabs(2 * B, PA + PB + PA + PO, "..x...x...X..x..", S, center=64,
               vel=78)
    stab.pad(29 * B, prog("Em7:2", 96), center=64, vel=96)
    pad.pad(10 * B, PB, center=67, vel=70, n=2)
    lead.mel(2 * B, MA, S, vel=94, bar=16)
    lead.mel(10 * B, MB, S, vel=96, gate=0.95, bar=16)
    lead.mel(18 * B, MA, S, vel=100, bar=16)
    lead.mel(26 * B, """
        e5:4! r:4 b4:4 r:4 | a4:4 c#5:4 e5:4 r:4 |
        e5:4 g5:4 b5:4 d6:4 | e6:8! r:8
    """, S, vel=100, bar=16)
    drum.beat(0, 1, {K: "x.....x.x.....x.", HH: "x.x.x.x.x.x.x.x."},
              S, vel=96)
    drum.beat(B, 28, {K: "x.....x.x.....x.",
                      SN: "....x..g....x..g",
                      HH: "x.x.x.x.x.x.x...",
                      OH: "..............x."}, S, vel=96,
              vels={HH: 76, OH: 80})
    for bar in (2, 10, 18, 26, 29):
        drum.note(bar * B, S, CR, 108)
    drum.note(29 * B, S, K, 118)
    return s




def song_battle1():
    s = Song('BATTLE1.MID', 'Steel Rain (Battle 1)', 96, 1, 150, (4, 4))
    S, B = 24, s.bar
    brass = s.part('Brass', 0, 61, 112, 64)
    strg = s.part('Strings', 1, 48, 88, 86)
    gtr = s.part('Distortion guitar', 2, 30, 82, 40)
    bass = s.part('Bass', 3, 33, 100, 60)
    drum = s.part('Drums', DRUM_CH, None, 100, 64)
    PI = prog("Am:4 Am:4 Am:4 E:4", 96)
    PA = prog("Am:4 F:4 G:4 Am:4 Am:4 F:4 Dm:4 E:4", 96)
    PB = prog("F:4 G:4 Em:4 Am:4 Dm:4 G:4 C:4 E:4", 96)
    PO = prog("Am:4 F:4 E:4", 96)
    MA = """
        a4:4! c5:2 e5:2 a5:6 g5:2 | f5:4 e5:4 c5:4 a4:4 |
        b4:4! d5:2 g5:2 b5:6 a5:2 | a5:8 e5:4 c5:4 |
        a4:4! c5:2 e5:2 a5:6 b5:2 | c6:4! b5:4 a5:4 f5:4 |
        d5:4 f5:4 a5:4 d6:4 | b5:8! g#5:4 e5:4
    """
    MB = """
        c6:8! a5:8 | b5:8 d6:8 | e6:12! b5:4 | c6:8 a5:8 |
        f5:8 a5:8 | g5:8 b5:4 d6:4 | e6:8! c6:4 g5:4 | g#5:8 b5:4 e6:4
    """
    CHUG = "0+7:6! 0:2 0+7:6! 0:2"
    brass.mel(2 * B, "a4:2! r:2 a4:2! r:10 | e5:4! e5:4 g#5:4 b5:4",
              S, vel=104, gate=0.85, bar=16)
    for bar in (4, 12, 28, 36):
        brass.mel(bar * B, MA, S, vel=102, gate=0.88, bar=16)
    for bar in (20, 44):
        brass.mel(bar * B, MB, S, vel=106, gate=0.95, bar=16)
    brass.mel(52 * B, """
        a5:4! e5:4 c5:4 a4:4 | f5:4 c5:4 a4:4 f4:4 |
        e5:4! g#5:4 b5:4 e6:4 | a5:8! r:8
    """, S, vel=108, bar=16)
    strg.pad(0, PI + PA, center=60, vel=70)
    strg.mel(12 * B, MA, S, vel=74, gate=0.95, tr=-12, bar=16)
    strg.pad(20 * B, PB + PA, center=60, vel=74)
    strg.mel(36 * B, MA, S, vel=78, gate=0.95, tr=-12, bar=16)
    strg.pad(44 * B, PB + PO + prog("Am:2", 96), center=60, vel=76)
    gtr.riff(0, PI + PA + PA, CHUG, S, lo=40, vel=84, gate=0.6)
    gtr.riff(20 * B, PB, "0+7:16", S, lo=40, vel=80, gate=0.95)
    gtr.riff(28 * B, PA + PA, CHUG, S, lo=40, vel=86, gate=0.6)
    gtr.riff(44 * B, PB, "0+7:8! 0+7:8", S, lo=40, vel=82, gate=0.9)
    gtr.riff(52 * B, PO, CHUG, S, lo=40, vel=88, gate=0.6)
    gtr.riff(55 * B, prog("Am:2", 96), "0+7:8", S, lo=40, vel=104)
    LINE = "0:6! 0:2 0:8"
    bass.riff(2 * B, PI[2:], "0:16", S, lo=28, vel=96)
    bass.riff(4 * B, PA + PA, LINE, S, lo=28, vel=98)
    bass.riff(20 * B, PB, "0:16", S, lo=28, vel=96)
    bass.riff(28 * B, PA + PA, LINE, S, lo=28, vel=100)
    bass.riff(44 * B, PB, "0:16", S, lo=28, vel=98)
    bass.riff(52 * B, PO, LINE, S, lo=28, vel=100)
    bass.note(55 * B, 8 * S, 33, 112)
    GA = {K: "x.....x.x.x.....", SN: "....x.......x...",
          HH: "x...x...x...x..."}
    GB = {K: "x.........x.....", SN: "........X.......",
          RD: "x...x...x...x..."}
    FILL = {K: "x.......x.......", SN: "....x...x.xxXXXX",
            T_HF: "..........x....."}
    hv = {HH: 78, RD: 80}
    drum.beat(0, 3, {K: "x.......x.......", T_LF: "....x.......x..."},
              S, vel=92)
    drum.beat(3 * B, 1, FILL, S, vel=88)
    for first, reps, groove in ((4, 15, GA), (20, 7, GB), (28, 15, GA),
                                (44, 7, GB)):
        drum.beat(first * B, reps, groove, S, vel=98, vels=hv)
        drum.beat((first + reps) * B, 1, FILL, S, vel=90)
    drum.beat(52 * B, 3, GA, S, vel=100, vels=hv)
    for bar in (4, 12, 20, 28, 36, 44, 52, 55):
        drum.note(bar * B, S, CR, 112)
    drum.note(55 * B, S, K, 120)
    return s












def song_demo():
    s = Song('DEMO.MID', 'MIDIRack Feature Test', 480, 1, 120, (4, 4))
    Q, E, S, B = 480, 240, 120, s.bar
    f = s.part('Feature channel', 0, 0, 100, 64)
    # RPN 0,0 (pitch-bend range) = 2 semitones, then the null RPN
    for c, v in ((101, 0), (100, 0), (6, 2), (38, 0), (101, 127),
                 (100, 127), (11, 127), (1, 0), (64, 0)):
        f.cc(0, c, v)
    f.bend(0, 8192)
    drum = s.part('GM drum walk', DRUM_CH, None, 100, 64)
    keys = s.part('Finale piano', 1, 0, 90, 64)

    # 1. ten programs on one channel, two beats each (120 BPM)
    walk = ((0, 'C'), (16, 'Am'), (24, 'F'), (33, 'G'), (48, 'C'),
            (61, 'Am'), (65, 'F'), (73, 'G'), (80, 'C'), (88, 'C'))
    for i, (p, sym) in enumerate(walk):
        t = i * 2 * Q
        if i:                           # Part() sent the first at time 0
            f.program(t, p)
        v = voicing(sym, 3, 60)
        tr = -24 if p == 33 else 0
        for j, k in enumerate(v + [v[0] + 12]):
            f.note(t + j * E, E, k + tr, 92 + (12 if j == 0 else 0))
    t = 10 * 2 * Q

    # 2. pitch bend: up two semitones and back, then down and back
    f.program(t, 80)
    f.note(t, 4 * Q, 67, 100, 0.98)
    for i in range(16):
        f.bend(t + Q + i * Q // 16, 8192 + 8191 * (i + 1) // 16)
        f.bend(t + 3 * Q + i * Q // 16, 16383 - 8191 * (i + 1) // 16)
    t += B
    f.note(t, 2 * Q, 76, 100, 0.98)
    for i in range(16):
        f.bend(t + i * Q // 16, 8192 - 8192 * (i + 1) // 16)
        f.bend(t + Q + i * Q // 16, 8192 * (i + 1) // 16)
    f.bend(t + 2 * Q, 4096)             # a scoop up into the last note
    for i in range(8):
        f.bend(t + 2 * Q + (i + 1) * E // 8, 4096 + 4096 * (i + 1) // 8)
    f.note(t + 2 * Q, 2 * Q, 72, 100, 0.95)
    t += B

    # 3. modulation wheel up over one note and down over the next
    f.program(t, 73)
    f.note(t, B, 74, 96, 0.97)
    f.note(t + B, B, 72, 96, 0.97)
    for i in range(16):
        f.cc(t + i * B // 16, 1, round(127 * (i + 1) / 16))
        f.cc(t + B + i * B // 16, 1, 127 - round(127 * (i + 1) / 16))
    f.cc(t + 2 * B, 1, 0)
    t += 2 * B

    # 4. tempo 90; a volume swell (CC7) on a string chord
    s.tempo(t, 90)
    f.program(t, 48)
    f.cc(t, 7, 10)
    for k in (60, 64, 67):
        f.note(t, 2 * B, k, 100, 0.98)
    for i in range(16):
        f.cc(t + (i + 1) * B // 16, 7, 10 + round(117 * (i + 1) / 16))
        f.cc(t + B + (i + 1) * B // 16, 7, 127 - round(117 * (i + 1) / 16))
    f.cc(t + 2 * B, 7, 100)
    t += 2 * B

    # 5. expression (CC11) dipping and rising on a brass chord
    f.program(t, 61)
    for k in (65, 69, 72):
        f.note(t, B, k, 100, 0.98)
    for i in range(16):
        f.cc(t + i * B // 16, 11,
             127 - round(97 * (i + 1) / 8) if i < 8
             else 30 + round(97 * (i - 7) / 8))
    f.cc(t + B, 11, 127)
    t += B

    # 6. 3/4; a pan sweep (CC10) left to right over an organ arpeggio
    s.timesig(t, 3, 4)
    B3 = 3 * Q
    f.program(t, 16)
    for i in range(24):
        f.cc(t + i * S, 10, round(127 * i / 23))
    for i, k in enumerate((60, 64, 67, 72, 67, 64) * 2):
        f.note(t + i * E, E, k, 92)
    f.cc(t + 2 * B3, 10, 64)
    t += 2 * B3

    # 7. sustain pedal (CC64): staccato notes the pedal holds
    f.program(t, 0)
    for chord, (down, up) in (((48, 55, 60, 64, 67, 72), (0, B3 - 20)),
                              ((53, 60, 65, 69, 72, 77), (20, B3 - 20))):
        f.cc(t + down, 64, 127)
        for i, k in enumerate(chord):
            f.note(t + i * E, E, k, 88 + 4 * i, 0.5)
        f.cc(t + up, 64, 0)
        t += B3

    # 8. 4/4 at 140; every GM drum key, 35 to 81, one a 16th
    s.timesig(t, 4, 4)
    s.tempo(t, 140)
    for i, k in enumerate(range(35, 82)):
        drum.note(t + i * S, S, k, 100)
    f.program(t, 33)
    f.riff(t, prog("C:4 F:4 G:4", Q), "0:4! 0:4 12:4 0:4", S, lo=36,
           vel=92, gate=0.8)
    t += 3 * B

    # 9. everything together, then the last chord
    drum.beat(t, 2, {K: "x.......x.......", SN: "....x.......x...",
                     HH: "x.x.x.x.x.x.x.x."}, S, vel=96, vels={HH: 72})
    f.riff(t, prog("F:4 G:4", Q), "0:4! 0:2 0:2 7:4 0:4", S, lo=36,
           vel=94, gate=0.8)
    keys.pad(t, prog("F:4 G:4 C:4", Q), center=64, vel=80)
    t += 2 * B
    f.note(t, B, 36, 104, 0.95)
    drum.note(t, S, K, 116)
    drum.note(t, S, CR, 112)
    return s


# ---------------------------------------------------------------------------
# the CLASSICS: six public-domain pieces everyone knows, arranged here for an
# OPL2 (the melodies are the composers'; every arrangement - the voicing, the
# accompaniment, the drums - is this file's, and CC0 like the rest)
# ---------------------------------------------------------------------------

def song_furelise():
    """Beethoven, Bagatelle in A minor WoO 59 (1810). 3/8, a 16th a unit."""
    s = Song('FURELISE.MID', 'Fur Elise (Beethoven)', 96, 1, 112, (3, 8),
             PD_COPYRIGHT)
    S = 24
    rh = s.part('Right hand', 0, 0, 108, 72)
    lh = s.part('Left hand', 1, 0, 96, 52)
    A = """e5:1 d#5:1 e5:1 b4:1 d5:1 c5:1 | a4:2 r:1 c4:1 e4:1 a4:1 |
           b4:2 r:1 e4:1 g#4:1 b4:1 | c5:2 r:1 e4:1 e5:1 d#5:1 |
           e5:1 d#5:1 e5:1 b4:1 d5:1 c5:1 | a4:2 r:1 c4:1 e4:1 a4:1 |
           b4:2 r:1 e4:1 c5:1 b4:1 |"""
    A_LH = ("r:6 | a2:1 e3:1 a3:1 r:3 | e2:1 e3:1 g#3:1 r:3 | "
            "a2:1 e3:1 a3:1 r:3 | r:6 | a2:1 e3:1 a3:1 r:3 | "
            "e2:1 e3:1 g#3:1 r:3 |")
    END1, END1_LH = "a4:2 r:2 e5:1 d#5:1 |", "a2:1 e3:1 a3:1 r:3 |"
    END2, END2_LH = "a4:2 r:1 b4:1 c5:1 d5:1 |", "a2:1 e3:1 a3:1 r:3 |"
    B = """e5:3 g4:1 f5:1 e5:1 | d5:3 f4:1 e5:1 d5:1 | c5:3 e4:1 d5:1 c5:1 |
           b4:2 r:1 e4:1 e5:1 r:1 | r:1 e5:1 e6:1 r:1 d#5:1 e5:1 |
           r:1 d#5:1 e5:1 d#5:1 e5:1 d#5:1 |"""
    B_LH = ("c3:1 g3:1 c4:1 r:3 | g2:1 g3:1 b3:1 r:3 | a2:1 e3:1 a3:1 r:3 | "
            "e2:1 e3:1 e4:1 r:3 | r:6 | r:6 |")
    t = rh.mel(0, "e5:1 d#5:1", S, vel=86)          # the pickup
    form = [(A, A_LH), (END1, END1_LH), (A, A_LH), (END2, END2_LH),
            (B, B_LH), (A, A_LH), (END1, END1_LH), (A, A_LH),
            (END2, END2_LH), (B, B_LH), (A, A_LH)]
    for r, l in form:
        lh.mel(t, l, S, vel=80, gate=0.95, bar=6)
        t = rh.mel(t, r, S, vel=100, gate=0.9, bar=6)
    rh.mel(t, "a4:6", S, vel=96, gate=1.0, bar=6)
    lh.mel(t, "a2+a3:6", S, vel=84, gate=1.0, bar=6)
    return s


def song_entertainer():
    """Joplin, The Entertainer (1902). 2/4, a 16th a unit: the A strain,
    twice, over a stride left hand - a bass on the beat, a chord off it."""
    s = Song('ENTERTNR.MID', 'The Entertainer (Joplin)', 96, 1, 80,
             (2, 4), PD_COPYRIGHT)
    S, B = 24, s.bar
    rh = s.part('Melody', 0, 3, 108, 70)
    bs = s.part('Stride bass', 1, 0, 100, 56)
    ch = s.part('Stride chords', 2, 0, 80, 60)
    LEAD = "r:6 d5:1 d#5:1 |"
    STRAIN = """e5:1 c6:2 e5:1 c6:2 e5:1 c6:1 | c6:5 c6:1 d6:1 d#6:1 |
        e6:1 c6:1 d6:1 e6:2 b5:1 d6:2 | c6:6 d5:1 d#5:1 |
        e5:1 c6:2 e5:1 c6:2 e5:1 c6:1 | c6:6 a5:1 g5:1 |
        f#5:1 a5:1 c6:1 e6:2 d6:1 c6:1 a5:1 | d6:6 d5:1 d#5:1 |
        e5:1 c6:2 e5:1 c6:2 e5:1 c6:1 | c6:5 c6:1 d6:1 d#6:1 |
        e6:1 c6:1 d6:1 e6:2 b5:1 d6:2 | c6:4 c6:1 d6:1 e6:1 c6:1 |
        d6:1 e6:2 c6:1 d6:1 c6:1 e6:1 c6:1 |
        d6:1 e6:2 c6:1 d6:1 c6:1 e6:1 c6:1 |
        d6:1 e6:2 b5:1 d6:2 c6:2 |"""
    P = prog("C:2 C:2 G7:2 C:2 C:2 C:2 D7:2 G7:2 C:2 C:2 G7:2 C:2 "
             "C:2 G7:2 G7:2 C:2", 96)
    t = rh.mel(0, LEAD, S, vel=96, bar=8)
    for last in ("c6:6 d5:1 d#5:1 |", "c6:6 r:2 |"):
        bs.riff(t, P, "0:2! r:2 7:2 r:2", S, lo=36, vel=96, gate=0.8)
        ch.stabs(t, P, "..x...x.", S, center=60, vel=76, n=3, steps=2)
        t = rh.mel(t, STRAIN + last, S, vel=100, gate=0.86, bar=8)
    bs.note(t, B, 36, 104, 1.0)
    ch.mel(t, "c4+e4+g4:8", S, vel=80, gate=1.0, bar=8)
    rh.note(t, B, 72, 100, 1.0)
    return s


def song_mountainking():
    """Grieg, In the Hall of the Mountain King (1875). The theme four times,
    louder, higher and faster each time, the way the piece builds."""
    s = Song('MOUNTKNG.MID', 'Hall of the Mountain King (Grieg)', 96, 1,
             84, (4, 4), PD_COPYRIGHT)
    S, B = 24, s.bar
    bsn = s.part('Bassoon', 0, 70, 104, 54)
    strg = s.part('Strings', 1, 48, 96, 74)
    brass = s.part('Brass', 2, 61, 100, 64)
    pizz = s.part('Pizzicato bass', 3, 45, 104, 60)
    timp = s.part('Timpani', 4, 47, 100, 64)
    drum = s.part('Drums', DRUM_CH, None, 96, 64)
    THEME = """b3:2 c#4:2 d4:2 e4:2 f#4:2 d4:2 f#4:4 |
               f4:2 c#4:2 f4:4 e4:2 c4:2 e4:4 |
               b3:2 c#4:2 d4:2 e4:2 f#4:2 d4:2 f#4:2 b4:2 |
               a4:2 f#4:2 d4:2 f#4:2 a4:8 |"""
    BASS = "b2:4 r:4 b2:4 r:4 | f#2:4 r:4 f#2:4 r:4 | " \
           "b2:4 r:4 b2:4 r:4 | f#2:4 r:4 f#2:4 r:4 |"
    t = 0
    for i, bpm in enumerate((84, 104, 132, 168)):
        if i:
            s.tempo(t, bpm)
        for _ in range(2):
            pizz.mel(t, BASS, S, vel=88 + 8 * i, gate=0.4, tr=-12 if i > 1
                     else 0, bar=16)
            if i == 0:
                bsn.mel(t, THEME, S, vel=96, gate=0.6, bar=16)
            elif i == 1:
                bsn.mel(t, THEME, S, vel=104, gate=0.55, bar=16)
                strg.mel(t, THEME, S, vel=84, gate=0.5, tr=12, bar=16)
            elif i == 2:
                strg.mel(t, THEME, S, vel=104, gate=0.6, tr=12, bar=16)
                brass.mel(t, THEME, S, vel=96, gate=0.5, bar=16)
                timp.mel(t, BASS, S, vel=96, gate=0.5, bar=16)
            else:
                strg.mel(t, THEME, S, vel=118, gate=0.6, tr=12, bar=16)
                brass.mel(t, THEME, S, vel=116, gate=0.55, tr=12, bar=16)
                bsn.mel(t, THEME, S, vel=110, gate=0.55, bar=16)
                timp.mel(t, BASS, S, vel=112, gate=0.5, bar=16)
            if i >= 2:
                drum.beat(t, 4, {K: "x...x...x...x...",
                                 SN: "....x.......x..." if i == 2 else
                                     "..x...x...x...x."}, S, vel=86 + 10 * i)
            t += 4 * B
    for k, bar in ((0, 0), (8, 1)):     # the crash: B minor, twice, and out
        tt = t + k * S
        for part, key in ((strg, 71), (strg, 66), (brass, 59), (brass, 62),
                          (pizz, 35)):
            part.note(tt, 4 * S, key, 120, 0.8)
        timp.note(tt, 4 * S, 35, 120, 0.8)
        drum.note(tt, S, CR, 120)
        drum.note(tt, S, K, 120)
    return s


def song_korobeiniki():
    """Korobeiniki, the Russian folk song (1861) - and the tune everyone
    knows from the game. A minor, a square lead over an octave bass."""
    s = Song('KOROBEIN.MID', 'Korobeiniki (Russian folk song)', 96, 1, 144,
             (4, 4), PD_COPYRIGHT)
    S, B = 24, s.bar
    lead = s.part('Square lead', 0, 80, 100, 64)
    harm = s.part('Harmony', 1, 81, 76, 84)
    bass = s.part('Synth bass', 2, 38, 104, 52)
    pad = s.part('Strings', 3, 48, 64, 64)
    drum = s.part('Drums', DRUM_CH, None, 90, 64)
    A = """e5:4 b4:2 c5:2 d5:4 c5:2 b4:2 | a4:4 a4:2 c5:2 e5:4 d5:2 c5:2 |
           b4:6 c5:2 d5:4 e5:4 | c5:4 a4:4 a4:8 |"""
    Bp = """r:2 d5:4 f5:2 a5:4 g5:2 f5:2 | e5:6 c5:2 e5:4 d5:2 c5:2 |
            b4:4 b4:2 c5:2 d5:4 e5:4 | c5:4 a4:4 a4:8 |"""
    HA = """g#4:4 g#4:2 a4:2 b4:4 a4:2 g#4:2 | e4:4 e4:2 a4:2 c5:4 b4:2 a4:2 |
            g#4:6 a4:2 b4:4 c5:4 | a4:4 e4:4 e4:8 |"""
    HB = """r:2 f4:4 a4:2 c5:4 b4:2 a4:2 | g4:6 e4:2 g4:4 f4:2 e4:2 |
            g#4:4 g#4:2 a4:2 b4:4 c5:4 | a4:4 e4:4 e4:8 |"""
    PA = prog("E:4 Am:4 E:4 Am:4", 96)
    PB = prog("Dm:4 C:4 E:4 Am:4", 96)
    t = 0
    for n, (m, h, P) in enumerate([(A, HA, PA), (Bp, HB, PB), (A, HA, PA),
                                   (Bp, HB, PB), (A, HA, PA), (Bp, HB, PB)]):
        lead.mel(t, m, S, vel=100, gate=0.86, bar=16)
        if n >= 2:
            harm.mel(t, h, S, vel=78, gate=0.8, bar=16)
        bass.riff(t, P, "0:2! 12:2", S, lo=33, vel=96, gate=0.7)
        if n >= 1:
            pad.pad(t, P, center=60, vel=56, n=3)
        if n >= 2:
            drum.beat(t, 4, {K: "x.......x.......", SN: "....x.......x...",
                             HH: "x.x.x.x.x.x.x.x."}, S, vel=88,
                      vels={HH: 64})
        t += 4 * B
    lead.note(t, B, 69, 104, 1.0)
    bass.note(t, B, 33, 104, 1.0)
    drum.note(t, S, CR, 110)
    drum.note(t, S, K, 110)
    return s


def song_cancan():
    """Offenbach, the Galop infernal from Orpheus in the Underworld (1858)
    - the Can-can. 2/4, a trumpet over an oom-pah piano and a snare."""
    s = Song('CANCAN.MID', 'Can-can (Offenbach)', 96, 1, 150, (2, 4),
             PD_COPYRIGHT)
    S, B = 24, s.bar
    tpt = s.part('Trumpet', 0, 56, 104, 70)
    fl = s.part('Piccolo', 1, 72, 80, 84)
    bs = s.part('Oom', 2, 0, 100, 52)
    ch = s.part('Pah', 3, 0, 80, 60)
    drum = s.part('Drums', DRUM_CH, None, 92, 64)
    TUNE = """c5:4 c5:4 | d5:2 f5:2 e5:2 d5:2 | g5:4 g5:4 |
              g5:2 a5:2 e5:2 f5:2 | d5:4 d5:4 | d5:2 f5:2 e5:2 d5:2 |
              c5:2 c6:2 b5:2 a5:2 | g5:2 f5:2 e5:2 d5:2 |
              c5:4 c5:4 | d5:2 f5:2 e5:2 d5:2 | g5:4 g5:4 |
              g5:2 a5:2 e5:2 f5:2 | d5:4 d5:4 | d5:2 f5:2 e5:2 d5:2 |
              c5:2 c6:2 g5:2 e5:2 | c5:4 r:4 |"""
    P = prog("C:2 G7:2 C:2 C:2 G7:2 G7:2 C:2 G7:2 "
             "C:2 G7:2 C:2 C:2 G7:2 G7:2 C:2 C:2", 96)
    t = 0
    for n in range(3):
        tpt.mel(t, TUNE, S, vel=100 + 6 * n, gate=0.7, bar=8)
        if n:
            fl.mel(t, TUNE, S, vel=80, gate=0.6, tr=12, bar=8)
        bs.riff(t, P, "0:2! r:2 -5:2 r:2", S, lo=40, vel=96, gate=0.7)
        ch.stabs(t, P, "..x...x.", S, center=60, vel=76, n=3, steps=2)
        drum.beat(t, 16, {K: "x...x...", SN: "..x...x."}, S, vel=84)
        drum.note(t, S, CR, 108)
        t += 16 * B
    for part, key in ((tpt, 72), (fl, 84), (bs, 36), (ch, 64), (ch, 67)):
        part.note(t, 2 * S, key, 120, 0.8)
    drum.note(t, S, CR, 120)
    drum.note(t, S, K, 118)
    return s


def song_turkish():
    """Mozart, Rondo alla Turca from the Sonata in A K. 331 (1783). 2/4, a
    harpsichord over a broken-chord left hand."""
    s = Song('TURKISH.MID', 'Rondo alla Turca (Mozart)', 96, 1, 116,
             (2, 4), PD_COPYRIGHT)
    S, B = 24, s.bar
    rh = s.part('Right hand', 0, 6, 104, 74)
    lh = s.part('Left hand', 1, 6, 92, 52)
    LEAD = "r:4 b4:1 a4:1 g#4:1 a4:1 |"
    TUNE = """c5:2 r:2 d5:1 c5:1 b4:1 c5:1 | e5:2 r:2 f5:1 e5:1 d#5:1 e5:1 |
        b5:1 a5:1 g#5:1 a5:1 b5:1 a5:1 g#5:1 a5:1 | c6:4 a5:2 c6:2 |
        b5:2 a5:2 g5:2 a5:2 | b5:2 a5:2 g5:2 a5:2 | b5:2 a5:2 g5:2 f#5:2 |
        e5:4 b4:1 a4:1 g#4:1 a4:1 |
        c5:2 r:2 d5:1 c5:1 b4:1 c5:1 | e5:2 r:2 f5:1 e5:1 d#5:1 e5:1 |
        b5:1 a5:1 g#5:1 a5:1 b5:1 a5:1 g#5:1 a5:1 | c6:4 a5:2 b5:2 |
        c6:2 b5:2 a5:2 g#5:2 | a5:2 e5:2 f5:2 d5:2 | c5:4 b4:4 |"""
    P = prog("Am:2 Am:2 Am:2 Am:2 C:2 C:2 B7:2 Em:2 "
             "Am:2 Am:2 Am:2 Am:2 E:2 Dm:2 E7:2 Am:2", 96)
    t = rh.mel(0, LEAD, S, vel=96, bar=8)
    for last in ("a4:4 b4:1 a4:1 g#4:1 a4:1 |", "a4:4 b4:1 a4:1 g#4:1 a4:1 |",
                 "a4:8 |"):
        lh.arp(t, P, (0, 1, 2, 1), 2 * S, center=57, lo=33, vel=78,
               gate=0.8)
        t = rh.mel(t, TUNE + last, S, vel=100, gate=0.8, bar=8)
    lh.note(t - B, B, 45, 90, 1.0)
    return s


COMPOSERS = (song_intro, song_spacejam, song_battle1, song_entertainer,
             song_furelise, song_mountainking, song_korobeiniki, song_cancan,
             song_turkish, song_demo)


def write_all(outdir):
    os.makedirs(outdir, exist_ok=True)
    for make in COMPOSERS:
        song = make()
        data = song.build()
        with open(os.path.join(outdir, song.fname), 'wb') as fp:
            fp.write(data)
        print('wrote %-12s %6d bytes' % (song.fname, len(data)))


# ---------------------------------------------------------------------------
# the second reader: shares nothing with the writer above
# ---------------------------------------------------------------------------

class Bad(Exception):
    pass


def rd_vlq(buf, i):
    value = 0
    for _ in range(4):
        if i >= len(buf):
            raise Bad('variable-length quantity runs off the track')
        byte = buf[i]
        i += 1
        value = (value << 7) | (byte & 0x7F)
        if byte < 0x80:
            return value, i
    raise Bad('variable-length quantity longer than 4 bytes')


def rd_track(buf):
    """-> list of (tick, kind, a, b), kind 'meta' (type, data), 'sysex'
    or 'chan' (status, data bytes); and the End-of-Track tick."""
    out, i, tick, running, eot = [], 0, 0, None, None
    while i < len(buf):
        if eot is not None:
            raise Bad('bytes after End-of-Track')
        delta, i = rd_vlq(buf, i)
        tick += delta
        if i >= len(buf):
            raise Bad('track ends inside an event')
        st = buf[i]
        if st == 0xFF:
            if i + 1 >= len(buf):
                raise Bad('truncated meta event')
            typ = buf[i + 1]
            ln, i = rd_vlq(buf, i + 2)
            data = bytes(buf[i:i + ln])
            if len(data) != ln:
                raise Bad('truncated meta event')
            i += ln
            running = None
            if typ == 0x2F:
                if ln:
                    raise Bad('End-of-Track with a length of %d' % ln)
                eot = tick
            out.append((tick, 'meta', typ, data))
        elif st in (0xF0, 0xF7):
            ln, i = rd_vlq(buf, i + 1)
            i += ln
            running = None
            out.append((tick, 'sysex', st, b''))
        else:
            if st >= 0x80:
                if st >= 0xF0:
                    raise Bad('system message 0x%02X in a file' % st)
                running = st
                i += 1
            elif running is None:
                raise Bad('data byte with no running status')
            need = 1 if running & 0xF0 in (0xC0, 0xD0) else 2
            data = bytes(buf[i:i + need])
            if len(data) != need:
                raise Bad('truncated channel message')
            if any(b >= 0x80 for b in data):
                raise Bad('data byte >= 0x80 at tick %d' % tick)
            i += need
            out.append((tick, 'chan', running, data))
    if eot is None:
        raise Bad('no End-of-Track (FF 2F 00)')
    return out, eot


def check_file(path):
    """-> (summary dict, list of problems)."""
    with open(path, 'rb') as fp:
        raw = fp.read()
    probs = []
    if raw[:4] != b'MThd':
        return None, ['no MThd chunk']
    if int.from_bytes(raw[4:8], 'big') != 6:
        return None, ['MThd length is not 6']
    fmt = int.from_bytes(raw[8:10], 'big')
    ntrk = int.from_bytes(raw[10:12], 'big')
    div = int.from_bytes(raw[12:14], 'big')
    if fmt not in (0, 1):
        probs.append('format %d' % fmt)
    if div & 0x8000 or div == 0:
        return None, ['division 0x%04X is not ticks per quarter' % div]
    pos, chunks = 14, []
    while pos < len(raw):
        if pos + 8 > len(raw):
            return None, ['truncated chunk header at %d' % pos]
        cid = raw[pos:pos + 4]
        clen = int.from_bytes(raw[pos + 4:pos + 8], 'big')
        if pos + 8 + clen > len(raw):
            return None, ['chunk %r runs off the file' % cid]
        if cid == b'MTrk':
            chunks.append(raw[pos + 8:pos + 8 + clen])
        pos += 8 + clen
    if len(chunks) != ntrk:
        probs.append('header says %d tracks, file has %d' % (ntrk,
                                                              len(chunks)))
    if fmt == 0 and len(chunks) != 1:
        probs.append('format 0 with %d tracks' % len(chunks))
    tracks, end = [], 0
    for n, buf in enumerate(chunks):
        try:
            evs, eot = rd_track(buf)
        except Bad as e:
            return None, probs + ['track %d: %s' % (n, e)]
        tracks.append(evs)
        end = max(end, eot)
        if not any(e[1] == 'meta' and e[2] == 0x03 for e in evs):
            probs.append('track %d has no track name (FF 03)' % n)
        if evs[-1][1] != 'meta' or evs[-1][2] != 0x2F:
            probs.append('track %d does not end with FF 2F 00' % n)
    if not tracks:
        return None, probs + ['no tracks']

    first0 = {e[2] for e in tracks[0] if e[0] == 0 and e[1] == 'meta'}
    for typ, what in ((0x58, 'time signature'), (0x51, 'tempo')):
        if typ not in first0:
            probs.append('no %s at time 0 of the first track' % what)
    if not any(e[1] == 'meta' and e[2] in (0x01, 0x02) for e in tracks[0]):
        probs.append('no text or copyright meta in the first track')

    # one stream: (tick, track, index) keeps each track's own order
    stream = sorted(((e[0], tn, ix) + e[1:]
                     for tn, evs in enumerate(tracks)
                     for ix, e in enumerate(evs)), key=lambda x: x[:3])

    # tempo map and duration
    tmap = sorted((x[0], int.from_bytes(x[5], 'big')) for x in stream
                  if x[3] == 'meta' and x[4] == 0x51)
    secs, at, us = 0.0, 0, 500000
    for tk, val in tmap:
        if tk > end:
            break
        secs += (tk - at) * us / div / 1e6
        at, us = tk, val
    secs += (end - at) * us / div / 1e6

    # pairing, in stream order
    sounding, notes, progs, prog_seen, init = {}, [], set(), set(), set()
    for x in stream:
        if x[3] != 'chan':
            continue
        tk, st, d = x[0], x[4], x[5]
        ch, hi = st & 0x0F, st & 0xF0
        if hi == 0xC0:
            prog_seen.add(ch)
            if ch != DRUM_CH:
                progs.add(d[0])
        elif hi == 0xB0 and tk == 0 and d[0] in (7, 10):
            init.add((ch, d[0]))
        elif hi == 0x90 and d[1]:
            if (ch, d[0]) in sounding:
                probs.append('ch %d key %d struck again at %d while it '
                             'still sounds' % (ch + 1, d[0], tk))
                continue
            if ch != DRUM_CH and ch not in prog_seen:
                probs.append('ch %d plays at %d before any program change'
                             % (ch + 1, tk))
                prog_seen.add(ch)
            sounding[(ch, d[0])] = tk
        elif hi == 0x80 or hi == 0x90:
            if (ch, d[0]) not in sounding:
                probs.append('ch %d key %d released at %d but never struck'
                             % (ch + 1, d[0], tk))
                continue
            notes.append((ch, d[0], sounding.pop((ch, d[0])), tk))
    for (ch, key), tk in sorted(sounding.items()):
        probs.append('ch %d key %d struck at %d never released'
                     % (ch + 1, key, tk))
    chans = {n[0] for n in notes}
    for ch in sorted(chans):
        for cc in (7, 10):
            if (ch, cc) not in init:
                probs.append('ch %d has no CC%d at time 0' % (ch + 1, cc))
    for ch, key, a, b in notes:
        if ch == DRUM_CH:
            if b - a > div // 4:
                probs.append('drum key %d at %d is longer than a 16th'
                             % (key, a))
            if not 35 <= key <= 81:
                probs.append('drum key %d is not a GM drum' % key)

    # polyphony, worst case: at each tick starts before ends, and a note
    # the pedal holds is still sounding
    by_tick = {}
    for x in stream:
        if x[3] == 'chan':
            by_tick.setdefault(x[0], []).append((x[4], x[5]))
    live, held, pedal = {}, {}, {}
    worst = [0, 0]                      # melodic, drums
    for tk in sorted(by_tick):
        msgs = by_tick[tk]
        for st, d in msgs:
            if st & 0xF0 == 0x90 and d[1]:
                k = (st & 0x0F, d[0])
                if held.get(k):
                    held[k] -= 1
                live[k] = live.get(k, 0) + 1
        mel = sum(v for k, v in live.items() if k[0] != DRUM_CH)
        mel += sum(held.values())
        drm = sum(v for k, v in live.items() if k[0] == DRUM_CH)
        worst = [max(worst[0], mel), max(worst[1], drm)]
        for st, d in msgs:
            if st & 0xF0 == 0xB0 and d[0] == 64 and d[1] >= 64:
                pedal[st & 0x0F] = True
        for st, d in msgs:
            if st & 0xF0 == 0x80 or (st & 0xF0 == 0x90 and not d[1]):
                k = (st & 0x0F, d[0])
                if live.get(k):
                    live[k] -= 1
                    if pedal.get(k[0]) and k[0] != DRUM_CH:
                        held[k] = held.get(k, 0) + 1
        for st, d in msgs:
            if st & 0xF0 == 0xB0 and d[0] == 64 and d[1] < 64:
                ch = st & 0x0F
                pedal[ch] = False
                for k in [k for k in held if k[0] == ch]:
                    del held[k]
    if any(pedal.values()) or any(held.values()):
        probs.append('the sustain pedal is still down at the end')
    if worst[0] > MELODIC_MAX:
        probs.append('%d melodic notes at once (limit %d)'
                     % (worst[0], MELODIC_MAX))
    if worst[1] > DRUM_MAX:
        probs.append('%d drum notes at once (limit %d)'
                     % (worst[1], DRUM_MAX))
    if len(raw) > FILE_MAX:
        probs.append('%d bytes (limit %d)' % (len(raw), FILE_MAX))
    return dict(size=len(raw), fmt=fmt, ntrk=len(chunks), div=div,
                secs=secs, mel=worst[0], drm=worst[1],
                progs=sorted(progs)), probs


def check_dir(d):
    bad, total = 0, 0
    present = {n.upper() for n in os.listdir(d)} if os.path.isdir(d) else set()
    for name in SONGS:
        if name not in present:
            print('%-12s MISSING' % name)
            bad += 1
            continue
        info, probs = check_file(os.path.join(d, name))
        if info:
            total += info['size']
            print('%-12s %6d B  fmt %d  trk %2d  div %3d  %5.1f s  '
                  'poly %d  drums %d  progs %s'
                  % (name, info['size'], info['fmt'], info['ntrk'],
                     info['div'], info['secs'], info['mel'], info['drm'],
                     ','.join(map(str, info['progs']))))
        for p in probs:
            print('    %s: %s' % (name, p))
        bad += bool(probs)
    print('total        %6d B  (limit %d)' % (total, TOTAL_MAX))
    if total > TOTAL_MAX:
        bad += 1
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--out', metavar='DIR', help='write the ten songs')
    g.add_argument('--check', metavar='DIR',
                   help='re-read and verify the ten songs')
    a = ap.parse_args()
    if a.out:
        write_all(a.out)
        return 0
    return 1 if check_dir(a.check) else 0


if __name__ == '__main__':
    sys.exit(main())
