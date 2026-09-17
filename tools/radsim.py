#!/usr/bin/env python3
"""RAD tunes on the host: the validator, both replay engines, the register log.

SPEC.md 96.4 is the format and 96.8 is this tool's contract; SPEC.md 34.12 is
the driver it is the reference for.  htmsim's shape: the thing the 8086 code
is checked against, run before (and beside) a byte of it exists.

What is in here:

  * THE VALIDATOR (SPEC.md 96.4.4) - the same rules, in the same order, with
    the same (code, detail, offset) answer as SOUND.DRV's verb 4.  Every byte
    of a tune is hostile; the reference players check nothing.
  * THE 2.1 ENGINE (SPEC.md 96.4.5) - Reality's RAD V2.0a player, re-written
    from its behaviour: every 8- and 16-bit wrap it has, the riff recursion
    and its depth-8 guard, the order of every register write.  Checked
    against the real `player20.cpp` by --crosscheck, which compiles it on the
    host out of an archive that is NEVER in this tree (decision D4).
  * THE 1.0 ENGINE - RAD V1.1a's PLAYER.ASM, re-written the same way, plus
    the three deviations SPEC.md 96.4.5 names (V1_DEVIATIONS below).
  * THE LOG (SPEC.md 96.8) - `.RLG`, 3-byte records, register word + value,
    FFFEh/FFFFh/FFFDh markers - in two streams: the REFERENCE stream (every
    write the reference player makes) and the SENT stream (SPEC.md 34.12.6's
    shadow filter applied - what reaches the ports and what the driver's
    -DRADLOG build logs).
  * THE FIXTURES (SPEC.md 96.7) - `--make` composes five tunes of OUR OWN into
    tests/fixtures/rad/, byte-deterministic; `--make --check` fails when the
    committed files differ.  No Reality tune is ever committed.

    python3 tools/radsim.py TUNE.RAD                 # validate + describe
    python3 tools/radsim.py TUNE.RAD --log OUT.RLG --frames 1000 [--sent]
    python3 tools/radsim.py TUNE.RAD --dump --frames 20 [--sent]
    python3 tools/radsim.py --make [--check]
    python3 tools/radsim.py --selfcheck              # `make` runs this
    python3 tools/radsim.py --crosscheck [--ref DIR] # host only; SKIPs politely

--crosscheck looks for Reality's RAD V2.0a archive (its Source/player20.cpp and
Tunes/*.rad) in --ref, or $RADSIM_REF; with no archive, or no C++ compiler, it
says which and exits 0.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXDIR = os.path.join(ROOT, "tests", "fixtures", "rad")
API_INC = os.path.join(ROOT, "apps", "os88api.inc")

SIG = b"RAD by REALiTY!!"

# --- SPEC.md 34.12 / apps/os88api.inc - mirrored, and --selfcheck compares ---
RAD_MAXLEN = 49152
RAD_PNMAX = 32              # SPEC.md 96.4.5, 2.1 deviation 5: notes per frame,
                            # and 34.13.3/34.13.5: the per-tick budget
RAD_FRMAX = 7               # SPEC.md 34.13.5: frames one pacer interrupt runs
RADE = dict(NOSINK=0, NOTRAD=1, VERSION=2, CORRUPT=3, NEEDOPL3=4, BIG=5,
            BUSY=6, NOMEM=7, NOTUNE=8, PACER=9, BADARG=10)
RADC = dict(TRUNC=1, FLAGS=2, BPM=3, INST=4, MIDI=5, ORDLEN=6, JUMP=7,
            ORDER=8, PATNUM=9, PATOFF=10, PTRUNC=11, EXTRA=12, LINE=13,
            CHAN=14, NOTE=15, INSNUM=16, EFFECT=17, RIFFID=18)
# SPEC.md 96.6's <reason> words, by detail
RADC_REASON = {1: "the file ends early", 2: "bad header flags",
               3: "BPM out of range", 4: "bad instrument",
               5: "unknown MIDI instrument", 6: "bad order list",
               7: "bad jump marker", 8: "bad order entry",
               9: "bad pattern number", 10: "bad pattern offset",
               11: "a pattern ends early", 12: "extra bytes",
               13: "bad line number", 14: "bad channel", 15: "bad note",
               16: "bad instrument number", 17: "bad effect",
               18: "bad riff number"}

# A MIDI instrument's body, COUNTING its algorithm byte.  SPEC.md 96.4.3/96.4.4
# as first written said 6, from RAD.HTML and validate20.cpp; player20.cpp skips
# 7, and both of Reality's MIDI tunes (MIDI/*.rad in the V2.0a archive) only
# parse at 7 - their instruments are `07 00 04 2c 00 00 40`.  7 is the file
# format; see the wave 1 radsim report.
MIDI_BODY = 7
FM_BODY = 24

LOG_START_END = 0xFFFE
LOG_FRAME_END = 0xFFFF
LOG_STOP = 0xFFFD


class Refusal(Exception):
    """A verb 4 refusal: (code, detail, offset) - SPEC.md 34.12.1."""

    def __init__(self, code, detail=0, offset=0):
        Exception.__init__(self, code, detail, offset)
        self.code, self.detail, self.offset = code, detail, offset

    def triple(self):
        return (self.code, self.detail, self.offset)


# =============================================================================
# The validator - SPEC.md 34.12.1 steps 2..6 and 96.4.4, to the byte
# =============================================================================
def check(b, opl3=True):
    """None when verb 4 would accept `b`, else (RADE_*, RADC_*, offset).

    Step 1 (SI + CX wrapping a segment) has no host meaning and is not here;
    steps 7 and 8 are about the machine's state, not the file.
    """
    try:
        validate(b, opl3)
    except Refusal as r:
        return r.triple()
    return None


def validate(b, opl3=True):
    """Raise Refusal, or return the version byte (10h / 21h)."""
    b = bytes(b)
    n = len(b)
    if n < 17 or b[:16] != SIG:
        raise Refusal(RADE["NOTRAD"])
    ver = b[0x10]
    if ver not in (0x10, 0x21):
        raise Refusal(RADE["VERSION"])
    if n > RAD_MAXLEN:
        raise Refusal(RADE["BIG"])
    if ver == 0x21 and not opl3:
        raise Refusal(RADE["NEEDOPL3"])
    v2 = ver == 0x21
    st = [0]                                   # the cursor, p

    def fail(d, at):
        raise Refusal(RADE["CORRUPT"], RADC[d], at)

    def need(k, at):
        if st[0] + k > n:
            fail("TRUNC", at)

    def w(i):
        return b[i] | (b[i + 1] << 8)

    # V1
    st[0] = 0x11
    need(1, 0x11)
    flags = b[0x11]
    st[0] = 0x12
    # V2
    if v2:
        if flags & 0x80:
            fail("FLAGS", 0x11)
        if flags & 0x20:
            need(2, 0x12)
            if not 46 <= w(0x12) <= 300:
                fail("BPM", 0x12)
            st[0] = 0x14
        desc = True
    else:
        if flags & 0x20:
            fail("FLAGS", 0x11)
        desc = bool(flags & 0x80)
    # V3
    if desc:
        d = st[0]
        while True:
            need(1, d)
            c = b[st[0]]
            st[0] += 1
            if c == 0:
                break

    def track(kind, rec):
        """TRACK(kind, rec): kind 'P' pattern, 'I' instrument riff, 'C' channel riff."""
        need(2, rec)
        size = w(st[0])
        st[0] += 2
        if st[0] + size > n:
            fail("TRUNC", rec)
        e = st[0] + size
        last_line = -1
        while True:
            lp = st[0]
            if st[0] >= e:
                fail("PTRUNC", lp)
            lb = b[lp]
            st[0] += 1
            if lb & 0x7F >= 64 or lb & 0x7F <= last_line:
                fail("LINE", lp)
            last_line = lb & 0x7F
            last_ch = -1
            while True:
                c = st[0]
                if st[0] >= e:
                    fail("PTRUNC", c)
                x = b[c]
                st[0] += 1
                if kind == "C":
                    if not x & 0x80:
                        fail("CHAN", c)
                else:
                    if x & 0x0F >= 9 or x & 0x0F <= last_ch:
                        fail("CHAN", c)
                    last_ch = x & 0x0F
                if x & 0x40:
                    q = st[0]
                    if st[0] >= e:
                        fail("PTRUNC", q)
                    if b[q] & 0x0F in (0, 13, 14):
                        fail("NOTE", q)
                    st[0] += 1
                if x & 0x20:
                    q = st[0]
                    if st[0] >= e:
                        fail("PTRUNC", q)
                    if b[q] == 0 or b[q] >= 128:
                        fail("INSNUM", q)
                    st[0] += 1
                if x & 0x10:
                    q = st[0]
                    if st[0] + 2 > e:
                        fail("PTRUNC", q)
                    if b[q] > 31 or b[q + 1] > 99:
                        fail("EFFECT", q)
                    st[0] += 2
                if kind == "C" or x & 0x80:
                    break
            if lb & 0x80:
                break
        if st[0] != e:
            fail("EXTRA", st[0])

    # V4
    last = 0
    while True:
        i = st[0]
        need(1, i)
        num = b[i]
        st[0] += 1
        if num == 0:
            break
        if v2:
            if num > 127 or num <= last:
                fail("INST", i)
            last = num
            need(1, i)
            ln = b[st[0]]
            st[0] += 1
            need(ln + 1, i)
            st[0] += ln
            alg = b[st[0]]
            if alg & 7 == 7:
                need(MIDI_BODY, i)
                if b[st[0] + 2] >> 4:
                    fail("MIDI", i)
                st[0] += MIDI_BODY
            else:
                need(FM_BODY, i)
                st[0] += FM_BODY
            if alg & 0x80:
                track("I", i)
        else:
            if num > 31 or num <= last:
                fail("INST", i)
            last = num
            need(11, i)
            st[0] += 11
    # V5
    o = st[0]
    need(1, o)
    ln = b[o]
    st[0] += 1
    if ln == 0 or ln > 128:
        fail("ORDLEN", o)
    need(ln, o)
    for k in range(ln):
        e = b[o + 1 + k]
        if e & 0x80:
            t = e & 0x7F
            if t >= ln or b[o + 1 + t] & 0x80 or (not v2 and k == 0):
                fail("JUMP", o + 1 + k)
        elif e >= (100 if v2 else 32):
            fail("ORDER", o + 1 + k)
    st[0] += ln
    if v2:
        # V6
        seen = set()
        while True:
            r = st[0]
            need(1, r)
            num = b[r]
            st[0] += 1
            if num == 0xFF:
                break
            if num >= 100 or num in seen:
                fail("PATNUM", r)
            seen.add(num)
            track("P", r)
        # V7
        seen = set()
        while True:
            r = st[0]
            need(1, r)
            rid = b[r]
            st[0] += 1
            if rid == 0xFF:
                break
            if rid >> 4 > 9 or rid & 0x0F == 0 or rid & 0x0F > 9 or rid in seen:
                fail("RIFFID", r)
            seen.add(rid)
            track("C", r)
        if st[0] != n:
            fail("EXTRA", st[0])
        return ver
    # V8
    t = st[0]
    need(64, t)
    for k in range(32):
        q = t + 2 * k
        off = w(q)
        if off == 0:
            continue
        if off < t + 64 or off >= n:
            fail("PATOFF", q)
        st[0] = off
        last_line = -1
        while True:
            lp = st[0]
            if lp >= n:
                fail("TRUNC", lp)
            lb = b[lp]
            st[0] += 1
            if lb & 0x7F >= 64 or lb & 0x7F <= last_line:
                fail("LINE", lp)
            last_line = lb & 0x7F
            last_ch = -1
            while True:
                c = st[0]
                need(3, c)
                if b[c] & 0x7F >= 9 or b[c] & 0x7F <= last_ch:
                    fail("CHAN", c)
                last_ch = b[c] & 0x7F
                st[0] += 3
                if b[c + 1] & 0x0F in (13, 14):
                    fail("NOTE", c + 1)
                if b[c + 2] & 0x0F:
                    q2 = st[0]
                    need(1, q2)
                    if b[q2] > 99:
                        fail("EFFECT", q2)
                    st[0] += 1
                if b[c] & 0x80:
                    break
            if lb & 0x80:
                break
    return ver


def rate_tenths(b):
    """SPEC.md 34.13.1: frames per second in tenths.  A slow-timer tune wins."""
    flags = b[0x11]
    if flags & 0x40:
        return 182
    if b[0x10] == 0x21 and flags & 0x20:
        return (b[0x12] | (b[0x13] << 8)) * 4
    return 500


def description(b):
    """The description text's lines (SPEC.md 96.4.2), [] when there is none."""
    if b[0x10] == 0x21:
        p = 0x14 if b[0x11] & 0x20 else 0x12
    elif b[0x11] & 0x80:
        p = 0x12
    else:
        return []
    lines, cur = [], []
    while p < len(b) and b[p]:
        c = b[p]
        if c == 1:
            lines.append("".join(cur))
            cur = []
        elif c < 0x20:
            cur.append(" " * c)
        else:
            cur.append(chr(c))
        p += 1
    lines.append("".join(cur))
    return lines


def title_line(b):
    """RADBOX's title line (SPEC.md 96.2)."""
    lines = description(b)
    return lines[0] if lines and lines[0] else "(no description)"


# =============================================================================
# Integer helpers - the reference players' C and assembly widths
# =============================================================================
def s8(x):
    return ((x & 0xFF) ^ 0x80) - 0x80


def u8(x):
    return x & 0xFF


def u16(x):
    return x & 0xFFFF


NOTE_FREQ = (0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5,
             0x202, 0x220, 0x241, 0x263, 0x287, 0x2AE)


# =============================================================================
# The 2.1 engine - SPEC.md 96.4.5, the reference stream of player20.cpp
# =============================================================================
V2_TRACKS, V2_CHANNELS, V2_LINES, V2_RIFFS = 100, 9, 64, 10
CM_PORTUP, CM_PORTDN, CM_TONESLIDE, CM_TONEVOL = 1, 2, 3, 5
CM_VOLSLIDE, CM_SETVOL, CM_JUMP, CM_SPEED = 10, 12, 13, 15
CM_IGNORE, CM_MULT, CM_RIFF = ord("I") - 55, ord("M") - 55, ord("R") - 55
CM_TRANSPOSE, CM_FEEDBACK, CM_VOLUME = ord("T") - 55, ord("U") - 55, ord("V") - 55
S_NONE, S_RIFF, S_IRIFF = 0, 1, 2
F_KEYON, F_KEYOFF, F_KEYEDON = 1, 2, 4
NOTE_SIZE = (0, 2, 1, 3, 1, 3, 2, 4)
CHAN_OFF3 = (0, 1, 2, 0x100, 0x101, 0x102, 6, 7, 8)
CHN2_OFF3 = (3, 4, 5, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108)
OP_OFF3 = ((0x00B, 0x008, 0x003, 0x000), (0x00C, 0x009, 0x004, 0x001),
           (0x00D, 0x00A, 0x005, 0x002), (0x10B, 0x108, 0x103, 0x100),
           (0x10C, 0x109, 0x104, 0x101), (0x10D, 0x10A, 0x105, 0x102),
           (0x113, 0x110, 0x013, 0x010), (0x114, 0x111, 0x014, 0x011),
           (0x115, 0x112, 0x015, 0x012))
ALG_CARRIERS = ((1, 0, 0, 0), (1, 1, 0, 0), (1, 0, 0, 0), (1, 0, 0, 1),
                (1, 0, 1, 0), (1, 0, 1, 1), (1, 1, 1, 1))
BLANK_OP = (0, 0x3F, 0, 0xF0, 0)


class _Inst(object):
    __slots__ = ("feedback", "panning", "alg", "detune", "volume",
                 "riff_speed", "riff", "ops")

    def __init__(self):
        # SPEC.md 96.4.5 deviation 2: an undefined instrument is all zero
        self.feedback, self.panning = [0, 0], [0, 0]
        self.alg = self.detune = self.volume = self.riff_speed = 0
        self.riff = None
        self.ops = [[0] * 5 for _ in range(4)]


class _FX(object):
    __slots__ = ("port", "vol", "tfreq", "toct", "tspeed", "tdir")

    def __init__(self):
        self.port = self.vol = self.tfreq = self.toct = self.tspeed = self.tdir = 0


class _Riff(object):
    __slots__ = ("fx", "track", "start", "line", "speed", "cnt", "toct",
                 "tnote", "last_inst")

    def __init__(self):
        self.fx = _FX()
        self.track = self.start = None
        self.line = self.speed = self.cnt = self.toct = self.tnote = 0
        self.last_inst = 0


class _Chan(object):
    __slots__ = ("last_inst", "inst", "volume", "deta", "detb", "keys",
                 "freq", "oct", "fx", "riff", "iriff")

    def __init__(self):
        self.last_inst, self.inst = 0, None
        self.volume = self.deta = self.detb = self.keys = 0
        self.freq = self.oct = 0
        self.fx, self.riff, self.iriff = _FX(), _Riff(), _Riff()


class _Holder(object):
    __slots__ = ("last_inst",)

    def __init__(self, v=0):
        self.last_inst = v


class EngineV2(object):
    """RAD 2.1.  `write(reg, val)` receives every write in the reference order."""

    def __init__(self, b, write):
        self.b = bytes(b)
        self.write = write
        self.regs = [255] * 512
        self.midi_volume = 0          # SetVolume on an algorithm 7 channel
        self.inst = [_Inst() for _ in range(127)]
        self.chans = [_Chan() for _ in range(V2_CHANNELS)]
        self.tracks = [None] * V2_TRACKS
        self.riffs = [[None] * V2_CHANNELS for _ in range(V2_RIFFS)]
        self.note_num = self.oct_num = self.inst_num = 0
        self.effect = self.param = 0
        self.frames = 0
        self.notes = 0                # note plays this frame (deviation 5)
        self.max_notes = 0            # the most any frame has played
        self.pnmax = RAD_PNMAX
        self.halted = False           # a note play was refused: the tune ends
        self.halt_on_cap = True       # False = round 0's rule (the negative control)
        self._parse()
        self.stop()

    def restart(self):
        """SPEC.md 34.12.2 step 0: a start re-zeroes the replay state to its
        at-load values (96.4.5), then writes the start sequence less its
        leading 105h, which the caller writes as reference_stream does.
        player20.cpp's Stop() does NOT do the first half."""
        self.chans = [_Chan() for _ in range(V2_CHANNELS)]
        self.note_num = self.oct_num = self.inst_num = 0
        self.effect = self.param = 0
        self.frames = self.notes = 0
        self.halted = False
        self.speed = self.b[0x11] & 0x1F
        self.stop()

    # -- Init --------------------------------------------------------------
    def _parse(self):
        b = self.b
        s = 0x11
        flags = b[s]
        s += 1
        self.speed = flags & 0x1F
        if flags & 0x20:
            s += 2
        while b[s]:
            s += 1
        s += 1
        while True:
            num = b[s]
            s += 1
            if num == 0:
                break
            s += b[s] + 1
            ins = self.inst[num - 1]
            alg = b[s]
            s += 1
            ins.alg = alg & 7
            ins.panning = [(alg >> 3) & 3, (alg >> 5) & 3]
            if ins.alg < 7:
                ins.feedback = [b[s] & 15, b[s] >> 4]
                ins.detune, ins.riff_speed = b[s + 1] >> 4, b[s + 1] & 15
                ins.volume = b[s + 2]
                s += 3
                for i in range(4):
                    ins.ops[i] = list(b[s:s + 5])
                    s += 5
            else:
                s += MIDI_BODY - 1
            if alg & 0x80:
                size = b[s] | (b[s + 1] << 8)
                s += 2
                ins.riff = s
                s += size
            else:
                ins.riff = None
        self.order_len = b[s]
        s += 1
        self.order_list = s
        s += self.order_len
        while True:
            num = b[s]
            s += 1
            if num >= V2_TRACKS:
                break
            size = b[s] | (b[s + 1] << 8)
            s += 2
            self.tracks[num] = s
            s += size
        while True:
            rid = b[s]
            s += 1
            rn, cn = rid >> 4, rid & 15
            if rn >= V2_RIFFS or cn > V2_CHANNELS:
                break
            size = b[s] | (b[s + 1] << 8)
            s += 2
            self.riffs[rn][cn - 1] = s
            s += size

    def set(self, reg, val):
        val = u8(val)
        self.regs[reg] = val
        self.write(reg, val)

    def get(self, reg):
        return self.regs[reg]

    # -- Stop: the start sequence (less 96.4.6's leading 105h) and the stop --
    def stop(self):
        for reg in range(0x20, 0xF6):
            val = 0xFF if 0x60 <= reg < 0xA0 else 0
            self.set(reg, val)
            self.set(reg + 0x100, val)
        self.set(0x01, 0x20)
        self.set(0x08, 0)
        self.set(0xBD, 0)
        self.set(0x104, 0)
        self.set(0x105, 1)
        self.speed_cnt = 1
        self.order = 0
        self.track = self.get_track()
        self.line = 0
        self.entrances = 0
        self.master_vol = 64
        for ch in self.chans:
            ch.last_inst = 0
            ch.inst = None
            ch.volume = ch.deta = ch.detb = ch.keys = 0
            ch.riff.cnt = 0
            ch.iriff.cnt = 0

    # -- Update --------------------------------------------------------------
    def update(self):
        self.notes = 0
        for i, ch in enumerate(self.chans):
            self.tick_riff(i, ch.iriff, False)
            self.tick_riff(i, ch.riff, True)
        self.play_line()
        for i, ch in enumerate(self.chans):
            self.continue_fx(i, ch.iriff.fx)
            self.continue_fx(i, ch.riff.fx)
            self.continue_fx(i, ch.fx)
        self.frames += 1
        if self.notes > self.max_notes:
            self.max_notes = self.notes

    def unpack_note(self, s, holder):
        b = self.b
        chanid = b[s]
        s += 1
        self.inst_num = self.effect = self.param = 0
        note = 0
        if chanid & 0x40:
            n = b[s]
            s += 1
            note = n & 0x7F
            if n & 0x80:
                self.inst_num = holder.last_inst
        if chanid & 0x20:
            self.inst_num = b[s]
            s += 1
            holder.last_inst = self.inst_num
        if chanid & 0x10:
            self.effect = b[s]
            self.param = b[s + 1]
            s += 2
        self.note_num = note & 15
        self.oct_num = note >> 4
        return s, bool(chanid & 0x80)

    def get_track(self):
        if self.order >= self.order_len:
            self.order = 0
        tn = self.b[self.order_list + self.order]
        if tn & 0x80:
            self.order = tn & 0x7F
            tn = self.b[self.order_list + self.order] & 0x7F
        return self.tracks[tn]

    def skip_to_line(self, trk, linenum, chan_riff):
        b = self.b
        while True:
            lineid = b[trk]
            if lineid & 0x7F >= linenum:
                return trk
            if lineid & 0x80:
                break
            trk += 1
            while True:
                chanid = b[trk]
                trk += 1
                trk += NOTE_SIZE[(chanid >> 4) & 7]
                if chanid & 0x80 or chan_riff:
                    break
        return None

    def play_line(self):
        self.speed_cnt = u8(self.speed_cnt - 1)
        if self.speed_cnt > 0:
            return
        self.speed_cnt = self.speed
        for ch in self.chans:
            self.reset_fx(ch.fx)
        self.line_jump = -1
        trk = self.track
        if trk is not None and self.b[trk] & 0x7F <= self.line:
            lineid = self.b[trk]
            trk += 1
            while True:
                cn = self.b[trk] & 15
                trk, last = self.unpack_note(trk, self.chans[cn])
                self.play_note(cn, self.note_num, self.oct_num, self.inst_num,
                               self.effect, self.param)
                if last:
                    break
            if lineid & 0x80:
                trk = None
            self.track = trk
        self.line = u8(self.line + 1)
        if self.line >= V2_LINES or self.line_jump >= 0:
            self.line = self.line_jump if self.line_jump >= 0 else 0
            self.order = u8(self.order + 1)
            self.track = self.get_track()

    def play_note(self, cn, notenum, octave, instnum, cmd=0, param=0,
                  src=S_NONE, op=0):
        ch = self.chans[cn]
        if self.entrances >= 8:
            return
        if self.notes >= self.pnmax:  # SPEC.md 96.4.5 deviation 5 - the fan-out
            if self.halt_on_cap:      # the refused play ends the tune after
                self.halted = True    # this frame
            return
        self.notes += 1
        self.entrances += 1
        fx = ch.riff.fx if src == S_RIFF else ch.iriff.fx if src == S_IRIFF else ch.fx
        transposing = False
        if cmd == CM_TONESLIDE:
            if 0 < notenum <= 12:
                fx.toct = u8(octave)
                fx.tfreq = NOTE_FREQ[notenum - 1]
            self._toneslide(cn, fx, param)
            self.entrances -= 1
            return
        if instnum > 0:
            old = ch.inst
            ins = self.inst[instnum - 1]
            ch.inst = ins
            if ins.alg == 7:
                self.entrances -= 1
                return
            self.load_instrument(cn)
            ch.keys |= F_KEYOFF | F_KEYON
            self.reset_fx(ch.iriff.fx)
            if src != S_IRIFF or ins is not old:
                if ins.riff is not None and ins.riff_speed > 0:
                    r = ch.iriff
                    r.track = r.start = ins.riff
                    r.line = 0
                    r.speed = ins.riff_speed
                    r.last_inst = 0
                    if 1 <= notenum <= 12:
                        r.toct, r.tnote = s8(octave), s8(notenum)
                        transposing = True
                    else:
                        r.toct, r.tnote = 3, 12
                    r.cnt = 1
                    self.tick_riff(cn, r, False)
                else:
                    ch.iriff.cnt = 0
        if cmd == CM_RIFF or cmd == CM_TRANSPOSE:
            self.reset_fx(ch.riff.fx)
            p0, p1 = param // 10, param % 10
            ch.riff.track = self.riffs[p0][p1 - 1] if p1 > 0 else None
            if ch.riff.track is not None:
                r = ch.riff
                r.start = r.track
                r.line = 0
                r.speed = self.speed
                r.last_inst = 0
                if cmd == CM_TRANSPOSE and 1 <= notenum <= 12:
                    r.toct, r.tnote = s8(octave), s8(notenum)
                    transposing = True
                else:
                    r.toct, r.tnote = 3, 12
                r.cnt = 1
                self.tick_riff(cn, r, True)
            else:
                ch.riff.cnt = 0
        if not transposing and notenum > 0:
            if notenum == 15:
                ch.keys |= F_KEYOFF
            if ch.inst is None or ch.inst.alg < 7:
                self.play_note_opl3(cn, octave, notenum)
        if cmd == CM_SETVOL:
            self.set_volume(cn, param)
        elif cmd == CM_SPEED:
            if src == S_NONE:
                self.speed = self.speed_cnt = param
            elif src == S_RIFF:
                ch.riff.speed = ch.riff.cnt = param
            else:
                ch.iriff.speed = ch.iriff.cnt = param
        elif cmd == CM_PORTUP:
            fx.port = s8(param)
        elif cmd == CM_PORTDN:
            fx.port = s8(-s8(param))
        elif cmd == CM_TONEVOL or cmd == CM_VOLSLIDE:
            val = s8(param)
            if val >= 50:
                val = s8(-(val - 50))
            fx.vol = val
            if cmd == CM_TONEVOL:
                self._toneslide(cn, fx, param)
        elif cmd == CM_JUMP:
            if param < V2_LINES and src == S_NONE:
                self.line_jump = param
        elif cmd == CM_MULT:
            if src == S_IRIFF:
                reg = 0x20 + OP_OFF3[cn][op]
                self.set(reg, (self.get(reg) & 0xF0) | (param & 15))
        elif cmd == CM_VOLUME:
            if src == S_IRIFF:
                reg = 0x40 + OP_OFF3[cn][op]
                self.set(reg, (self.get(reg) & 0xC0) | ((param & 0x3F) ^ 0x3F))
        elif cmd == CM_FEEDBACK:
            if src == S_IRIFF:
                which, fb = param // 10, param % 10
                if which in (0, 1):
                    reg = 0xC0 + (CHN2_OFF3[cn] if which == 0 else CHAN_OFF3[cn])
                    self.set(reg, (self.get(reg) & 0x31) | ((fb & 7) << 1))
        self.entrances -= 1

    def _toneslide(self, cn, fx, param):
        if param:
            fx.tspeed = param
        self.get_slide_dir(cn, fx)

    def load_instrument(self, cn):
        ch = self.chans[cn]
        ins = ch.inst
        if ins is None:
            return
        alg = ins.alg
        ch.volume = ins.volume
        ch.deta = (ins.detune + 1) >> 1
        ch.detb = ins.detune >> 1
        if cn < 6:
            mask = 1 << cn
            self.set(0x104, (self.get(0x104) & ~mask) | (mask if alg in (2, 3) else 0))
        self.set(0xC0 + CHAN_OFF3[cn], ((ins.panning[1] ^ 3) << 4) |
                 ins.feedback[1] << 1 | (1 if alg in (3, 5, 6) else 0))
        self.set(0xC0 + CHN2_OFF3[cn], ((ins.panning[0] ^ 3) << 4) |
                 ins.feedback[0] << 1 | (1 if alg in (1, 6) else 0))
        for i in range(4):
            o = BLANK_OP if (alg < 2 and i >= 2) else ins.ops[i]
            reg = OP_OFF3[cn][i]
            vol = ~o[1] & 0x3F
            if ALG_CARRIERS[alg][i]:
                vol = u16(vol * ins.volume // 64)
                vol = u16(vol * self.master_vol // 64)
            self.set(reg + 0x20, o[0])
            self.set(reg + 0x40, (o[1] & 0xC0) | ((vol ^ 0x3F) & 0x3F))
            self.set(reg + 0x60, o[2])
            self.set(reg + 0x80, o[3])
            self.set(reg + 0xE0, o[4])

    def play_note_opl3(self, cn, octave, note):
        ch = self.chans[cn]
        o1, o2 = CHAN_OFF3[cn], CHN2_OFF3[cn]
        if ch.keys & F_KEYOFF:
            ch.keys &= ~(F_KEYOFF | F_KEYEDON)
            self.set(0xB0 + o1, self.get(0xB0 + o1) & ~0x20)
            self.set(0xB0 + o2, self.get(0xB0 + o2) & ~0x20)
        if note == 15:
            return
        op4 = ch.inst is not None and ch.inst.alg >= 2
        freq = NOTE_FREQ[note - 1]
        frq2 = freq
        ch.freq = freq
        ch.oct = s8(octave)
        freq = u16(freq + ch.deta)
        frq2 = u16(frq2 - ch.detb)
        if op4:
            self.set(0xA0 + o1, frq2 & 0xFF)
        self.set(0xA0 + o2, freq & 0xFF)
        if ch.keys & F_KEYON:
            ch.keys = (ch.keys & ~F_KEYON) | F_KEYEDON
        key = 0x20 if ch.keys & F_KEYEDON else 0
        if op4:
            self.set(0xB0 + o1, (frq2 >> 8) | (octave << 2) | key)
        else:
            self.set(0xB0 + o1, 0)
        self.set(0xB0 + o2, (freq >> 8) | (octave << 2) | key)

    @staticmethod
    def reset_fx(fx):
        fx.port = fx.vol = fx.tdir = 0

    def tick_riff(self, cn, riff, chan_riff):
        b = self.b
        if riff.cnt == 0:
            self.reset_fx(riff.fx)
            return
        riff.cnt = u8(riff.cnt - 1)
        if riff.cnt > 0:
            return
        riff.cnt = riff.speed
        line = riff.line
        riff.line = u8(riff.line + 1)
        if riff.line >= V2_LINES:
            riff.cnt = 0
        self.reset_fx(riff.fx)
        trk = riff.track
        if trk is not None and b[trk] & 0x7F == line:
            lineid = b[trk]
            trk += 1
            if chan_riff:
                trk, _ = self.unpack_note(trk, riff)
                self.transpose(riff.tnote, riff.toct)
                self.play_note(cn, self.note_num, self.oct_num, self.inst_num,
                               self.effect, self.param, S_RIFF)
            else:
                while True:
                    col = b[trk] & 15
                    trk, last = self.unpack_note(trk, riff)
                    if self.effect != CM_IGNORE:
                        self.transpose(riff.tnote, riff.toct)
                    self.play_note(cn, self.note_num, self.oct_num, self.inst_num,
                                   self.effect, self.param, S_IRIFF,
                                   ((col - 1) & 3) if col > 0 else 0)
                    if last:
                        break
            if lineid & 0x80:
                trk = None
            riff.track = trk
        if trk is None or b[trk] & 0x7F != riff.line:
            return
        trk += 1
        self.unpack_note(trk, _Holder(0))
        if self.effect == CM_JUMP and self.param < V2_LINES:
            riff.line = self.param
            riff.track = self.skip_to_line(riff.start, self.param, chan_riff)

    def continue_fx(self, cn, fx):
        ch = self.chans[cn]
        if fx.port:
            self.portamento(cn, fx, fx.port, False)
        if fx.vol:
            vol = s8(s8(ch.volume) - fx.vol)
            if vol < 0:
                vol = 0
            self.set_volume(cn, vol)
        if fx.tdir:
            self.portamento(cn, fx, fx.tdir, True)

    def set_volume(self, cn, vol):
        ch = self.chans[cn]
        vol = u8(vol)
        if vol > 64:
            vol = 64
        ch.volume = vol
        vol = u8(vol * self.master_vol // 64)
        ins = ch.inst
        if ins is None:
            return
        if ins.alg == 7:
            # SPEC.md 96.4.5 deviation 4: player20.cpp indexes AlgCarriers[7],
            # one row past its seven.  An algorithm 7 instrument has no
            # carriers: the channel's volume is recorded, no register written.
            self.midi_volume += 1
            return
        for i in range(4):
            if not ALG_CARRIERS[ins.alg][i]:
                continue
            opvol = u8(((ins.ops[i][1] & 63) ^ 63) * vol // 64)
            reg = 0x40 + OP_OFF3[cn][i]
            self.set(reg, (self.get(reg) & 0xC0) | (opvol ^ 0x3F))

    def get_slide_dir(self, cn, fx):
        ch = self.chans[cn]
        speed = s8(fx.tspeed)
        if speed > 0:
            oct_, freq = fx.toct, fx.tfreq
            oldfreq, oldoct = ch.freq, u8(ch.oct)
            if oldoct > oct_:
                speed = s8(-speed)
            elif oldoct == oct_:
                if oldfreq > freq:
                    speed = s8(-speed)
                elif oldfreq == freq:
                    speed = 0
        fx.tdir = speed

    def portamento(self, cn, fx, amount, toneslide):
        ch = self.chans[cn]
        freq = u16(ch.freq + amount)
        oct_ = u8(ch.oct)
        if freq < 0x156:
            if oct_ > 0:
                oct_ -= 1
                freq = u16(freq + 0x2AE - 0x156)
            else:
                freq = 0x156
        elif freq > 0x2AE:
            if oct_ < 7:
                oct_ += 1
                freq = u16(freq - (0x2AE - 0x156))
            else:
                freq = 0x2AE
        if toneslide:
            if amount >= 0:
                if oct_ > fx.toct or (oct_ == fx.toct and freq >= fx.tfreq):
                    freq, oct_ = fx.tfreq, fx.toct
            else:
                if oct_ < fx.toct or (oct_ == fx.toct and freq <= fx.tfreq):
                    freq, oct_ = fx.tfreq, fx.toct
        ch.freq = freq
        ch.oct = s8(oct_)
        frq2 = u16(freq - ch.detb)
        freq = u16(freq + ch.deta)
        off = CHN2_OFF3[cn]
        self.set(0xA0 + off, freq & 0xFF)
        self.set(0xB0 + off, ((freq >> 8) & 3) | oct_ << 2 | (self.get(0xB0 + off) & 0xE0))
        off = CHAN_OFF3[cn]
        self.set(0xA0 + off, frq2 & 0xFF)
        self.set(0xB0 + off, ((frq2 >> 8) & 3) | oct_ << 2 | (self.get(0xB0 + off) & 0xE0))

    def transpose(self, note, octave):
        if 1 <= self.note_num <= 12:
            toct = s8(octave - 3)
            if toct != 0:
                self.oct_num = s8(self.oct_num + toct)
                if self.oct_num < 0:
                    self.oct_num = 0
                elif self.oct_num > 7:
                    self.oct_num = 7
            tnot = s8(note - 12)
            if tnot != 0:
                self.note_num = s8(self.note_num + tnot)
                if self.note_num < 1:
                    self.note_num = s8(self.note_num + 12)
                    if self.oct_num > 0:
                        self.oct_num -= 1
                    else:
                        self.note_num = 1


# =============================================================================
# The 1.0 engine - SPEC.md 96.4.5, RAD V1.1a's PLAYER.ASM
# =============================================================================
V1_CHAN_OFFS = (0x20, 0x21, 0x22, 0x28, 0x29, 0x2A, 0x30, 0x31, 0x32)
FREQ_START, FREQ_RANGE = 0x156, 0x158

# Deviations from PLAYER.ASM beyond the note nibble - SPEC.md 96.4.5's 1.0
# deviations 2 and 3:
#   1. The LAST LINE of a pattern ends it.  PLAYER.ASM zeroes PatternPos on the
#      last line and then overwrites the zero with the pointer past the line
#      (`mov cs:PatternPos,si` after the channel loop), so it goes on reading
#      whatever bytes follow the pattern as further lines - the next pattern,
#      or past the end of the file.  The intent in its own comment ("mark rest
#      of pattern as blank") is what plays here.
#   2. A jump-to-line's line seek reads the entry's command nibble from the
#      TUNE.  PLAYER.ASM reads it through CS: (`test byte ptr cs:[si-1],15`),
#      which is the tune only when the tune is in the player's own segment.
V1_DEVIATIONS = ("last line ends the pattern", "jump seek reads the tune")


class EngineV1(object):
    """RAD 1.0.  `write(reg, val)` receives every write in PLAYER.ASM's order."""

    def __init__(self, b, write):
        self.b = bytes(b)
        self.write = write
        self.frames = 0
        self.inst_ptrs = [0] * 31
        self.old43 = [0] * 9
        self.olda0 = [0] * 9
        self.oldb0 = [0] * 9
        self.zero_state()
        self.start()

    def zero_state(self):
        """SPEC.md 96.4.5: the 1.0 state a start begins from."""
        self.frames = 0
        self.old43 = [0] * 9
        self.olda0 = [0] * 9
        self.oldb0 = [0] * 9
        self.ts_speed = [1] * 9
        self.ts_freq = [0] * 9
        self.ts_on = [0] * 9
        self.port = [0] * 9
        self.vols = [0] * 9

    def restart(self):
        """SPEC.md 34.12.2 step 0, then the start sequence."""
        self.zero_state()
        self.start()

    def w(self, i):
        return self.b[i] | (self.b[i + 1] << 8)

    def adlib(self, reg, val):
        self.write(reg, u8(val))

    def end_player(self):
        for reg in range(0x20, 0xF6):
            self.adlib(reg, 0)

    def start(self):
        b = self.b
        self.end_player()
        self.adlib(0x01, 0x20)
        self.adlib(0x08, 0x00)
        self.adlib(0xBD, 0x00)
        self.speed = b[0x11] & 0x1F
        si = 0x12
        if b[0x11] & 0x80:
            while b[si]:
                si += 1
            si += 1
        while True:
            num = b[si]
            si += 1
            if num == 0:
                break
            self.inst_ptrs[num - 1] = si
            si += 11
        self.order_size = b[si]
        si += 1
        self.order_list = si
        first = b[si]
        si += self.order_size
        self.pattern_list = si
        self.pattern_pos = self.w(si + first * 2)
        self.order_pos = 0
        self.speed_cnt = 0
        self.line = 0

    def frame(self):
        b = self.b
        if self.speed_cnt != 0:
            self.speed_cnt -= 1
        else:
            for c in range(9):
                self.port[c] = self.vols[c] = self.ts_on[c] = 0
            jumped = False
            si = self.pattern_pos
            if si and b[si] & 0x7F == self.line:
                last_line = b[si] & 0x80
                if last_line:
                    self.pattern_pos = 0
                si += 1
                while True:
                    cb = b[si]
                    nb, ib = b[si + 1], b[si + 2]
                    si += 3
                    param = 0
                    if ib & 15:
                        param = b[si]
                        si += 1
                    jump = self.play_note(cb & 0x7F, nb, ib, param)
                    if jump is not None:
                        self.jump_to(jump)
                        jumped = True
                        break
                    if cb & 0x80:
                        break
                if not jumped:
                    self.pattern_pos = 0 if last_line else si     # deviation 1
            if not jumped:
                self.speed_cnt = u8(self.speed - 1)
                self.line += 1
                if self.line >= 64:
                    self.line = 0
                    self.next_pattern()
        self.update_notes()
        self.frames += 1

    def jump_to(self, line):
        b = self.b
        self.speed_cnt = self.speed
        self.line = line
        si = self.next_pattern()
        if not si:
            return
        while True:
            if b[si] & 0x7F >= line:
                break
            if b[si] & 0x80:
                si = 0
                break
            si += 1
            while True:
                cl = b[si]
                si += 3
                if b[si - 1] & 15:                          # deviation 2
                    si += 1
                if cl & 0x80:
                    break
        self.pattern_pos = si

    def next_pattern(self):
        bx = self.order_pos + 1
        if bx >= self.order_size:
            bx = 0
        while True:
            self.order_pos = bx
            e = self.b[self.order_list + bx]
            if e & 0x80:
                bx = e & 0x7F
                continue
            break
        self.pattern_pos = self.w(self.pattern_list + e * 2)
        return self.pattern_pos

    def play_note(self, cn, al, ah, param):
        cmd = ah & 15
        if al & 0x0F:          # SPEC.md 96.4.5: a note nibble of 0 is no note
            if cmd == CM_TONESLIDE:
                note, octave = al & 15, (al >> 4) & 7
                if note - 1 >= 12:
                    return None
                self.ts_freq[cn] = u16(octave * FREQ_RANGE + NOTE_FREQ[note - 1] - FREQ_START)
                self.ts_on[cn] = 1
                if param:
                    self.ts_speed[cn] = param
                return None
            self.oldb0[cn] &= ~0x20 & 0xFF
            self.adlib(0xB0 + cn, self.oldb0[cn])
            inst = ((al >> 7) << 4) | (ah >> 4)
            if inst:
                self.load_inst(cn, inst)
            note = al & 15
            if note != 15:
                f = NOTE_FREQ[note - 1]
                val = (((al >> 4) & 7) << 2) | 0x20 | (f >> 8)
                self.oldb0[cn] = val
                self.olda0[cn] = f & 0xFF
                self.adlib(0xA0 + cn, f & 0xFF)
                self.adlib(0xB0 + cn, val)
        if cmd == CM_PORTUP:
            self.port[cn] = param
        elif cmd == CM_PORTDN:
            self.port[cn] = u8(-param)
        elif cmd == CM_TONESLIDE:
            if param:
                self.ts_speed[cn] = param
            self.ts_on[cn] = 1
        elif cmd in (CM_TONEVOL, CM_VOLSLIDE):
            c = param
            if c >= 50:
                c = u8(-(c - 50))
            self.vols[cn] = c
            if cmd == CM_TONEVOL:
                self.ts_on[cn] = 1
        elif cmd == CM_SETVOL:
            self.set_volume(cn, param)
        elif cmd == CM_JUMP:
            if param < 64:
                return param
        elif cmd == CM_SPEED:
            self.speed = param
        return None

    def load_inst(self, cn, num):
        b = self.b
        off = self.inst_ptrs[num - 1]
        if not off:
            return
        reg = V1_CHAN_OFFS[cn]
        self.old43[cn] = b[off + 2]
        for k in range(4):
            self.adlib(reg, b[off + 2 * k + 1])
            self.adlib(reg + 3, b[off + 2 * k])
            reg += 0x20
        reg += 0x40
        self.adlib(reg, b[off + 10])
        self.adlib(reg + 3, b[off + 9])
        self.adlib(0xC0 + cn, b[off + 8])

    def set_volume(self, cn, v):
        if v >= 64:
            v = 63
        al = (self.old43[cn] & 0xC0) | (v ^ 0x3F)
        self.old43[cn] = al
        self.adlib(V1_CHAN_OFFS[cn] + 0x23, al)

    def get_freq(self, c):
        cx = u16((((self.oldb0[c] & 3) << 8) | self.olda0[c]) - FREQ_START)
        return u16(((self.oldb0[c] >> 2) & 7) * FREQ_RANGE + cx)

    def set_freq(self, c, ax):
        q, r = divmod(ax, FREQ_RANGE)
        dx = r + FREQ_START
        al = ((q << 2) & 0xFF) | (self.oldb0[c] & 0xE0) | (dx >> 8)
        self.oldb0[c] = u8(al)
        self.adlib(0xB0 + c, al)
        self.olda0[c] = dx & 0xFF
        self.adlib(0xA0 + c, dx & 0xFF)

    def update_notes(self):
        for c in range(9):
            if self.port[c]:
                self.set_freq(c, u16(self.get_freq(c) + s8(self.port[c])))
            vs = self.vols[c]
            cl = (self.old43[c] & 0x3F) ^ 0x3F
            if vs:
                cl = u8(cl - vs)
                if vs & 0x80:
                    if cl >= 64:
                        cl = 63
                elif cl & 0x80:
                    cl = 0
                self.set_volume(c, cl)
            if self.ts_on[c]:
                spd = s8(self.ts_speed[c])
                ax = self.get_freq(c)
                cx = self.ts_freq[c]
                if ax == cx:
                    self.ts_on[c] = 0
                elif ax > cx:
                    ax = u16(ax - spd)
                    if not ax > cx:
                        ax = cx
                        self.ts_on[c] = 0
                else:
                    ax = u16(ax + spd)
                    if not ax < cx:
                        ax = cx
                        self.ts_on[c] = 0
                self.set_freq(c, ax)


# =============================================================================
# The streams and the log - SPEC.md 96.4.6, 34.12.6, 96.8
# =============================================================================
def reference_stream(b, frames, opl3=None):
    """[(reg, val)] - start, FFFEh, frames each ending FFFFh, FFFDh, stop.

    `opl3` only matters to a version 1.0 tune: its stop gains 105h <- 00h on
    an OPL3 (SPEC.md 96.4.6).  A 2.1 tune is always on an OPL3.
    """
    validate(b)
    out = []
    wr = lambda r, v: out.append((r, v))
    if b[0x10] == 0x21:
        wr(0x105, 0x01)
        eng = EngineV2(b, wr)
        is3 = True
    else:
        eng = EngineV1(b, wr)
        is3 = bool(opl3)
    wr(LOG_START_END, 0)
    for _ in range(frames):
        eng.update() if isinstance(eng, EngineV2) else eng.frame()
        wr(LOG_FRAME_END, 0)
        if getattr(eng, "halted", False):   # SPEC.md 96.4.5 deviation 5
            break
    wr(LOG_STOP, 0)
    _stop_sequence(eng, wr, is3)
    return out


def _stop_sequence(eng, wr, is3):
    """SPEC.md 96.4.6 / 34.11.3: 104h only where NEW was set (2.1)."""
    if isinstance(eng, EngineV2):
        eng.stop()
        wr(0x104, 0)
        wr(0x105, 0)
    else:
        eng.end_player()
        if is3:
            wr(0x105, 0)


def restart_stream(b, first, frames, opl3=None, rezero=True):
    """A start, `first` frames, a stop, then a SECOND start and `frames` frames
    and a stop on the same engine - returned in reference_stream's shape for
    the second session only.  SPEC.md 34.12.2 step 0 says it equals
    reference_stream(b, frames); `rezero=False` is player20.cpp's behaviour and
    the selfcheck's negative control."""
    validate(b)
    sink = []
    wr = lambda r, v: sink.append((r, v))
    v2 = b[0x10] == 0x21
    if v2:
        wr(0x105, 0x01)
        eng = EngineV2(b, wr)
    else:
        eng = EngineV1(b, wr)
    is3 = v2 or bool(opl3)
    for _ in range(first):
        eng.update() if v2 else eng.frame()
    _stop_sequence(eng, wr, is3)
    out = []
    wr2 = lambda r, v: out.append((r, v))
    eng.write = wr2
    if v2:
        wr2(0x105, 0x01)
        eng.restart() if rezero else eng.stop()
    else:
        eng.restart() if rezero else eng.start()
    wr2(LOG_START_END, 0)
    for _ in range(frames):
        eng.update() if v2 else eng.frame()
        wr2(LOG_FRAME_END, 0)
        if getattr(eng, "halted", False):
            break
    wr2(LOG_STOP, 0)
    _stop_sequence(eng, wr2, is3)
    return out


def paced(b, ticks, rtc=False, budget=True, halt=True, pnmax=None):
    """The WORK a pacer does, tick by tick - SPEC.md 34.13.3 step 5 and 34.13.5.

    Returns (per_tick, halted_at): per_tick is [(frames, note plays, computed
    reference writes)] for each BIOS tick, halted_at the tick the tune halted
    on or None.  `rtc` models every period delivered (56.2436 a tick, at most
    RAD_FRMAX frames an interrupt); the tick class runs its frames in the tick
    itself.  `budget=False, halt=False, pnmax=81` is round 0's rule - a cap per
    FRAME and nothing per tick - which t_rad uses as the negative control.
    A 2.1 tune only (a 1.0 frame plays at most nine notes).  Tick 0 is the
    first tick the pacer is delivered: SPEC.md 34.13.7's switched DSV_TICK
    can deliver it one tick after the start verb returns, which moves the
    ticks' timing and never the work in any one of them."""
    cnt = [0]
    eng = EngineV2(b, lambda r, v: cnt.__setitem__(0, cnt[0] + 1))
    eng.pnmax = RAD_PNMAX if pnmax is None else pnmax
    eng.halt_on_cap = halt
    rate = rate_tenths(b)
    out = []
    acc = 0
    frac = 0
    for t in range(ticks):
        cnt[0] = 0
        frames = notes = 0
        if rtc:
            frac += 290672
            periods = 56
            if frac >= 1193182:
                frac -= 1193182
                periods += 1
            unit = 10240
        else:
            periods = 1
            unit = 182
        for _ in range(periods):
            acc += rate
            run = 0
            while (acc >= unit and run < RAD_FRMAX and
                   (not budget or notes < RAD_PNMAX)):
                acc -= unit
                eng.update()
                run += 1
                frames += 1
                notes += eng.notes
                if eng.halted:
                    out.append((frames, notes, cnt[0]))
                    return out, t
            if acc >= unit:
                acc = unit - 1
        out.append((frames, notes, cnt[0]))
    return out, None


def most_notes(b, frames, window=1):
    """The most note plays any `window` consecutive 2.1 frames of `frames`
    make.  window=1 is the figure SPEC.md 96.4.5 deviation 5 says RAD_PNMAX
    never reaches on music; window=RAD_FRMAX is the most one tick of a 120 Hz
    tick-paced tune asks of 34.13.5's per-tick budget."""
    eng = EngineV2(b, lambda r, v: None)
    per = []
    for _ in range(frames):
        eng.update()
        per.append(eng.notes)
    if window <= 1:
        return eng.max_notes
    run = best = sum(per[:window])
    for i in range(window, len(per)):
        run += per[i] - per[i - window]
        best = max(best, run)
    return best


def new_rule_violations(stream):
    """SPEC.md 34.11.2: no 100h-1FFh write but 105h while NEW = 0.  NEW starts
    at 0 - attach leaves the chip in OPL2 mode (34.11.3).  Returns the record
    indices that break the rule."""
    new, bad = 0, []
    for i, (reg, val) in enumerate(stream):
        if reg >= 0x200:
            continue
        if reg == 0x105:
            new = val & 1
        elif reg >= 0x100 and not new:
            bad.append(i)
    return bad


def sent_stream(ref):
    """SPEC.md 34.12.6: frame writes equal to the shadow are not sent."""
    shadow = [None] * 512
    out = []
    phase = 0                         # 0 start, 1 frames, 2 stop
    for reg, val in ref:
        if reg == LOG_START_END:
            phase = 1
        elif reg == LOG_STOP:
            phase = 2
        elif reg < 0x200:
            if phase == 1 and shadow[reg] == val:
                continue
            shadow[reg] = val
        out.append((reg, val))
    return out


def encode_rlg(stream):
    return b"".join(struct.pack("<HB", r, v) for r, v in stream)


def decode_rlg(data):
    if len(data) % 3:
        raise ValueError("an .RLG is 3-byte records; %d bytes" % len(data))
    return [struct.unpack_from("<HB", data, i) for i in range(0, len(data), 3)]


def frames_of(stream):
    """Split a stream into (start, [frame writes...], stop)."""
    start, frames, stop, cur, phase = [], [], [], [], 0
    for r, v in stream:
        if r == LOG_START_END:
            phase = 1
        elif r == LOG_FRAME_END:
            frames.append(cur)
            cur = []
        elif r == LOG_STOP:
            phase = 2
        elif phase == 0:
            start.append((r, v))
        elif phase == 1:
            cur.append((r, v))
        else:
            stop.append((r, v))
    return start, frames, stop


# =============================================================================
# The fixtures - SPEC.md 96.7.  Composed here, byte-deterministic, OURS.
# =============================================================================
def _desc(text):
    """Encode description text: newline 01h, runs of 2..31 spaces as a count."""
    out = bytearray()
    for li, line in enumerate(text.split("\n")):
        if li:
            out.append(1)
        for m in re.finditer(r" {2,31}|.", line):
            s = m.group(0)
            out.append(len(s) if len(s) > 1 else ord(s))
    return bytes(out) + b"\0"


def _v1_note(n):
    """(octave, note) or 'off' or None -> the 1.0 note nibbles, without bit 7."""
    if n is None:
        return 0
    if n == "off":
        return 15
    return (n[0] << 4) | n[1]


def build_v1(desc, speed, slow, instruments, orders, patterns):
    """instruments {num: 11 bytes}; patterns {num: [(line, [entry])]} where an
    entry is (channel, note, inst, effect, param); patterns are laid out in
    DESCENDING number order, so offsets are not in table order."""
    out = bytearray(SIG + b"\x10")
    out.append((0x80 if desc else 0) | (0x40 if slow else 0) | speed)
    if desc:
        out += _desc(desc)
    for num in sorted(instruments):
        out.append(num)
        out += bytes(instruments[num])
    out.append(0)
    out.append(len(orders))
    out += bytes(orders)
    table = len(out)
    out += b"\0" * 64
    for num in sorted(patterns, reverse=True):
        struct.pack_into("<H", out, table + 2 * num, len(out))
        lines = patterns[num]
        for li, (line, entries) in enumerate(lines):
            out.append(line | (0x80 if li == len(lines) - 1 else 0))
            for ei, (ch, note, inst, eff, par) in enumerate(entries):
                out.append(ch | (0x80 if ei == len(entries) - 1 else 0))
                out.append(_v1_note(note) | (0x80 if inst & 0x10 else 0))
                out.append(((inst & 15) << 4) | eff)
                if eff:
                    out.append(par)
    return bytes(out)


def _v2_track(lines, chan_riff=False):
    """lines [(line, [entry])], entry (ch, note, inst, effect, param, reuse)."""
    body = bytearray()
    for li, (line, entries) in enumerate(lines):
        body.append(line | (0x80 if li == len(lines) - 1 else 0))
        if chan_riff:
            assert len(entries) == 1
        for ei, (ch, note, inst, eff, par, reuse) in enumerate(entries):
            x = ch & 15
            if ei == len(entries) - 1:
                x |= 0x80
            if note is not None:
                x |= 0x40
            if inst:
                x |= 0x20
            if eff is not None:
                x |= 0x10
            body.append(x)
            if note is not None:
                nb = 0x0F if note == "off" else (note[0] << 4) | note[1]
                body.append(nb | (0x80 if reuse else 0))
            if inst:
                body.append(inst)
            if eff is not None:
                body += bytes((eff, par))
    return struct.pack("<H", len(body)) + bytes(body)


def E(ch, note=None, inst=0, eff=None, par=0, reuse=False):
    """One 2.1 entry.  `note` is (octave, note 1..12) or 'off'."""
    return (ch, note, inst, eff, par, reuse)


def V1E(ch, note=None, inst=0, eff=0, par=0):
    return (ch, note, inst, eff, par)


def fm_inst(alg, pan12, pan34, fb12, fb34, detune, volume, seed,
            riff_speed=0, riff=None, name=""):
    """A 2.1 FM instrument record body (without its number)."""
    ops = bytearray()
    for i in range(4):
        k = seed * 7 + i * 5
        ops += bytes((0x20 | ((k + 1) % 4 + 1),           # EG on, multiplier
                      (0x40 if i == 0 else 0) | (8 + (k * 3) % 24),
                      0xF0 | (k % 3 + 1),
                      0x40 | (k % 6 + 3),
                      k % 8))
    head = bytes(((0x80 if riff is not None else 0) | pan34 << 5 | pan12 << 3 | alg,
                  fb34 << 4 | fb12, detune << 4 | riff_speed, volume))
    body = bytes((len(name),)) + name.encode() + head + bytes(ops)
    if riff is not None:
        body += _v2_track(riff)
    return body


def midi_inst(name="MIDI"):
    body = bytes((len(name),)) + name.encode()
    return body + bytes((0x07, 0x00, 0x04, 0x2C, 0x00, 0x00, 0x40))


def build_v2(desc, speed, slow, bpm, instruments, orders, patterns, riffs):
    out = bytearray(SIG + b"\x21")
    out.append((0x40 if slow else 0) | (0x20 if bpm else 0) | speed)
    if bpm:
        out += struct.pack("<H", bpm)
    out += _desc(desc)
    for num in sorted(instruments):
        out.append(num)
        out += instruments[num]
    out.append(0)
    out.append(len(orders))
    out += bytes(orders)
    for num in sorted(patterns):
        out.append(num)
        out += _v2_track(patterns[num])
    out.append(0xFF)
    for rid in sorted(riffs):
        out.append(rid)
        out += _v2_track(riffs[rid], chan_riff=True)
    out.append(0xFF)
    return bytes(out)


def _v1_instr(seed):
    """11 bytes: 23h 20h 43h 40h 63h 60h 83h 80h C0h E3h E0h."""
    k = seed * 5
    return bytes((0x21, 0x31 + seed % 3, 0x08 + k % 16, 0x18 + k % 20,
                  0xF2, 0xE1 + seed % 8, 0x74, 0x56 + seed % 9,
                  (seed % 7) << 1 | seed % 2, seed % 4, (seed + 1) % 4))


def compose_rv1():
    C, Cs, D, Ds, Ee, F, Fs, G, Gs, A, As, B = range(1, 13)
    ins = {1: _v1_instr(1), 2: _v1_instr(2), 3: _v1_instr(3),
           17: _v1_instr(17), 22: _v1_instr(22), 31: _v1_instr(31)}
    chord = [V1E(0, (3, C), 1), V1E(1, (3, Ee), 2), V1E(2, (3, G), 3),
             V1E(3, (4, C), 17), V1E(4, (2, C), 22), V1E(5, (4, Ee), 31),
             V1E(6, (4, G), 1), V1E(7, (5, C), 2), V1E(8, (1, C), 3)]
    p0 = [
        (0, chord),
        (4, [V1E(0, None, 0, CM_PORTUP, 6), V1E(1, None, 0, CM_PORTDN, 4)]),
        (8, [V1E(2, (3, B), 0, CM_TONESLIDE, 5),          # 3 with a note
             V1E(3, None, 0, CM_VOLSLIDE, 4),              # A down
             V1E(4, None, 0, CM_VOLSLIDE, 57)]),           # A up
        (12, [V1E(2, None, 0, CM_TONESLIDE, 0),           # 3 without a note
              V1E(5, (4, D), 0, CM_TONEVOL, 3),
              V1E(6, None, 0, CM_SETVOL, 20)]),
        (16, [V1E(5, (3, A), 0, CM_TONESLIDE, 9),
              V1E(7, None, 0, CM_SETVOL, 90),              # clamps at 63
              V1E(8, "off")]),                             # key-off
        (20, [V1E(0, (7, B), 17, CM_PORTUP, 40),           # past the top
              V1E(1, (0, Cs), 31, CM_PORTDN, 40)]),        # past the bottom
        (24, [V1E(3, (4, F), 22, CM_TONEVOL, 60)]),
        (28, [V1E(0, (3, G), 0, CM_SPEED, 2)]),
        (40, [V1E(1, (2, As), 0), V1E(4, (3, Fs)), V1E(8, (5, Gs), 17)]),
        (44, [V1E(2, None, 0, CM_PORTDN, 2)]),
        (50, [V1E(0, (0x3, C), 0x1F)]),                     # instrument 31, bit 7
        (52, [V1E(1, (3, C), 1), V1E(2, "off", 0, CM_TONESLIDE, 5),
              V1E(3, (3, 0), 2, CM_SETVOL, 30)]),          # nibble 0: no note
        (53, [V1E(1, (3, Cs), 0, CM_TONESLIDE, 22)]),      # lands exactly: up
        (54, [V1E(1, (3, C), 0, CM_TONESLIDE, 0)]),        # ... down, speed kept
    ]
    p1 = [
        (0, [V1E(k, (2 + k % 4, 1 + (k * 5) % 12), (1, 2, 3, 17, 22, 31)[k % 6])
             for k in range(9)]),
        (6, [V1E(0, None, 0, CM_SPEED, 5), V1E(4, "off")]),
        (12, [V1E(3, (5, Ds), 17, CM_TONESLIDE, 30)]),
        (20, [V1E(3, None, 0, CM_VOLSLIDE, 49), V1E(6, (4, D), 2, CM_SETVOL, 63)]),
        (32, [V1E(2, (3, Ee), 3),
              V1E(5, None, 0, CM_JUMP, 70),                # >= 64: ignored
              V1E(7, (4, B), 22, CM_JUMP, 8),              # the jump
              V1E(8, (4, A), 1)]),                         # abandoned
    ]
    # Pattern 2 lies straight after pattern 4 in the file and starts at line
    # 36, past pattern 4's last line: PLAYER.ASM would play it as pattern 4's
    # line 36 (V1_DEVIATIONS 1).  Its jump seeks through pattern 4's line 7,
    # whose entries carry commands (V1_DEVIATIONS 2).
    p2 = [
        (36, [V1E(k, (3, 1 + (k * 7) % 12), 2) for k in range(0, 9, 2)]),
        (40, [V1E(0, None, 0, CM_SPEED, 3), V1E(6, None, 0, CM_VOLSLIDE, 50)]),
        (45, [V1E(1, (4, C), 31), V1E(2, (4, Ee), 17, CM_JUMP, 30)]),
    ]
    p4 = [
        (7, [V1E(k, (2, 12 - k), 22, (CM_PORTUP, 0, CM_SETVOL)[k % 3], 9 + k)
             for k in range(9)]),
        (30, [V1E(8, "off", 0, CM_SETVOL, 10), V1E(8 - 8, (3, D), 1)][::-1]),
    ]
    # pattern 3 is named by the order list and has no data (offset 0)
    return build_v1("RV1 - radsim fixture\nRAD 1.0, every effect,     nine channels",
                    4, False, ins, [0, 1, 3, 2, 4, 0x82],
                    {0: p0, 1: p1, 2: p2, 4: p4})


def compose_rv1slow():
    ins = {1: _v1_instr(5), 2: _v1_instr(9)}
    p0 = [(0, [V1E(0, (3, 1), 1), V1E(1, (3, 5), 2)]),
          (2, [V1E(0, None, 0, CM_PORTUP, 3)]),
          (5, [V1E(1, "off")]),
          (9, [V1E(0, (4, 8), 2, CM_VOLSLIDE, 2), V1E(1, None, 0, CM_SPEED, 0)]),
          (10, [V1E(1, None, 0, CM_SPEED, 2)])]            # a 256-frame line
    return build_v1(None, 2, True, ins, [0], {0: p0})


def compose_rv2():
    C, Cs, D, Ds, Ee, F, Fs, G, Gs, A, As, B = range(1, 13)
    iriff9 = [                                   # instrument 9's riff
        (0, [E(1, None, 0, CM_MULT, 3), E(2, None, 0, CM_VOLUME, 20),
             E(3, (3, C), 0, CM_IGNORE, 0)]),
        (1, [E(1, None, 0, CM_FEEDBACK, 13), E(4, None, 0, CM_FEEDBACK, 5),
             E(5, None, 0, CM_MULT, 9)]),
        (2, [E(0, (4, Ee), 11)]),                # an instrument with a riff
        (3, [E(2, None, 0, CM_FEEDBACK, 27), E(8, None, 0, CM_VOLUME, 63)]),
        (5, [E(0, None, 0, CM_JUMP, 1)]),        # jump-to-line in a riff
        (6, [E(1, (5, G), 0)]),
    ]
    iriff10 = [
        (0, [E(0, (3, G), 0, CM_PORTUP, 4)]),
        (2, [E(0, None, 0, CM_VOLSLIDE, 3), E(1, None, 0, CM_SPEED, 2)]),
        (4, [E(0, (3, Ds), 0, CM_TONESLIDE, 8)]),
        (8, [E(0, (2, As), 0, CM_TONEVOL, 55)]),
        (12, [E(0, None, 0, CM_RIFF, 9)]),       # an instrument riff starts a channel riff
    ]
    iriff11 = [
        (0, [E(0, (3, Ee), 11)]),                # same instrument: no restart
        (1, [E(1, None, 0, CM_VOLUME, 30)]),
        (2, [E(0, None, 0, CM_SPEED, 1)]),
        (5, [E(0, "off")]),
    ]
    ins = {
        1: fm_inst(0, 0, 1, 3, 0, 0, 60, 1, name="alg0"),
        2: fm_inst(1, 1, 2, 5, 2, 0, 50, 2, name="alg1"),
        3: fm_inst(2, 2, 3, 1, 7, 0, 63, 3, name="alg2"),
        4: fm_inst(3, 3, 0, 7, 4, 0, 40, 4, name="alg3"),
        5: fm_inst(4, 0, 2, 2, 6, 0, 58, 5, name="alg4"),
        6: fm_inst(5, 1, 3, 4, 1, 0, 45, 6),
        7: fm_inst(6, 2, 0, 6, 3, 0, 64, 7, name="alg6"),
        8: midi_inst(),
        9: fm_inst(0, 3, 1, 0, 0, 0, 55, 9, riff_speed=2, riff=iriff9, name="riff"),
        10: fm_inst(4, 1, 1, 2, 2, 5, 62, 10, riff_speed=3, riff=iriff10,
                    name="detune"),
        11: fm_inst(1, 2, 2, 4, 4, 3, 48, 11, riff_speed=1, riff=iriff11),
        12: fm_inst(0, 1, 0, 1, 0, 0, 40, 13, riff_speed=1,
                    riff=[(0, [E(0, (3, C), 13)])]),        # 12 and 13 recurse:
        13: fm_inst(1, 2, 1, 2, 0, 0, 41, 14, riff_speed=1,  # the depth-8 guard
                    riff=[(0, [E(0, (3, Ee), 12)])]),
        40: fm_inst(3, 0, 3, 9, 15, 15, 99, 12, name="wild"),   # fb/vol overflow
    }
    p0 = [
        (0, [E(0, (3, C), 3), E(1, (3, Ee), 4), E(2, (3, G), 5), E(3, (4, C), 6),
             E(4, (4, Ee), 7), E(5, (2, C), 1), E(6, (2, G), 2), E(7, (4, G), 3),
             E(8, (3, A), 8)]),
        (4, [E(0, None, 0, CM_PORTUP, 5), E(1, None, 0, CM_PORTDN, 4),
             E(8, (3, A), 1)]),
        (8, [E(2, (3, B), 0, CM_TONESLIDE, 6), E(3, None, 0, CM_VOLSLIDE, 5),
             E(4, None, 0, CM_VOLSLIDE, 55)]),
        (12, [E(2, None, 0, CM_TONESLIDE, 0), E(3, None, 0, CM_SETVOL, 30),
              E(5, None, 0, CM_SETVOL, 80)]),
        (16, [E(5, (2, Ee), 0, CM_TONEVOL, 20), E(6, "off")]),
        (20, [E(0, (3, D), 0, None, 0, True), E(1, None, 0, CM_RIFF, 11)]),
        (24, [E(2, (4, D), 0, CM_TRANSPOSE, 21), E(3, None, 0, CM_IGNORE, 0),
              E(7, (1, F), 0, CM_TRANSPOSE, 9)]),
        (28, [E(4, (3, C), 9), E(6, (3, G), 10)]),
        (32, [E(0, None, 0, CM_SPEED, 3), E(7, None, 0, CM_MULT, 5),
              E(8, None, 0, CM_FEEDBACK, 12)]),
        (36, [E(5, (4, A), 40), E(8, None, 0, CM_VOLUME, 7)]),
        (40, [E(0, None, 0, CM_JUMP, 4), E(1, (2, B), 2)]),
        (44, [E(2, (5, C), 3)]),                 # never reached: the jump
    ]
    p1 = [
        (4, [E(0, (7, B), 3, CM_PORTUP, 60), E(1, (0, Cs), 4, CM_PORTDN, 60),
             E(4, (3, Fs), 10)]),
        (10, [E(1, None, 0, CM_RIFF, 10), E(2, (2, Gs), 9)]),   # p1 = 0: riff off
        (16, [E(3, (5, F), 6, CM_TONESLIDE, 40), E(8, (3, D), 11)]),
        (20, [E(4, None, 0, CM_VOLSLIDE, 50),
              E(5, (7, Ds), 0, CM_TRANSPOSE, 9), E(7, (3, C), 12)]),
        (22, [E(0, None, 0, CM_SPEED, 5), E(5, None, 0, CM_VOLSLIDE, 49),
              E(6, None, 0, CM_SETVOL, 2)]),
        (23, [E(6, None, 0, CM_VOLSLIDE, 20)]),
        (30, [E(6, (4, Ee), 7, CM_TRANSPOSE, 99), E(7, (3, Fs), 5)]),
        (32, [E(7, (3, Fs), 0, CM_TONESLIDE, 4)]),   # already there: no slide
        (48, [E(k, "off") for k in range(9)]),
    ]
    p3 = [
        (0, [E(k, (3, 1 + (k * 7) % 12), (1, 2, 3, 4, 5, 6, 7, 11, 40)[k])
             for k in range(9)]),
        (2, [E(4, None, 0, CM_SPEED, 2)]),
        (9, [E(1, (4, C), 9, CM_RIFF, 91), E(3, None, 0, CM_JUMP, 70)]),
        (50, [E(8, (2, C), 0, CM_TONEVOL, 7)]),
    ]
    riffs = {
        0x11: [(0, [E(0, (3, C), 2)]), (2, [E(0, (3, Ee), 10, CM_PORTDN, 3)]),   # both riffs slide at once
               (4, [E(0, None, 0, CM_RIFF, 21)]),            # a riff starts a riff
               (5, [E(0, (3, G), 2, CM_PORTUP, 2)]),
               (6, [E(0, None, 0, CM_JUMP, 0)])],            # jump-to-line in a riff
        0x21: [(0, [E(0, (3, C), 4)]), (1, [E(0, (3, D), 0, CM_TONESLIDE, 3)]),
               (3, [E(0, "off")]), (4, [E(0, (2, B), 0, CM_SPEED, 2)]),
               (6, [E(0, None, 0, CM_VOLSLIDE, 52)]),
               (63, [E(0, (4, Gs), 0, CM_PORTDN, 3)])],       # fx outlives line 63
        0x09: [(0, [E(0, (4, A), 5, CM_SETVOL, 40)]), (3, [E(0, (3, C), 0, None, 0, True)])],
        0x91: [(1, [E(0, (3, F), 6)]), (2, [E(0, None, 0, CM_TRANSPOSE, 21)])],
    }
    return build_v2("RV2 - radsim fixture\nevery algorithm, every effect", 4,
                    False, 0, ins, [0, 1, 5, 3, 0x81], {0: p0, 1: p1, 3: p3},
                    riffs)


def compose_rv2bpm():
    ins = {1: fm_inst(2, 3, 3, 4, 4, 2, 60, 21), 2: fm_inst(0, 0, 0, 2, 0, 0, 50, 22)}
    p0 = [(0, [E(0, (3, 1), 1), E(6, (4, 8), 2)]),
          (3, [E(0, None, 0, CM_PORTUP, 2)]),
          (7, [E(6, "off"), E(8, None, 0, CM_SPEED, 0)]),     # a 256-frame line
          (9, [E(8, None, 0, CM_SPEED, 3)])]
    return build_v2("RV2BPM", 3, False, 150, ins, [0], {0: p0}, {})


def compose_rv2slow():
    ins = {1: fm_inst(6, 1, 2, 3, 3, 0, 60, 31)}
    p0 = [(0, [E(2, (3, 5), 1)]), (4, [E(2, None, 0, CM_VOLSLIDE, 2)]),
          (9, [E(2, "off")])]
    return build_v2("", 2, True, 0, ins, [0], {0: p0}, {})


FIXTURES = (("RV1.RAD", compose_rv1), ("RV1SLOW.RAD", compose_rv1slow),
            ("RV2.RAD", compose_rv2), ("RV2BPM.RAD", compose_rv2bpm),
            ("RV2SLOW.RAD", compose_rv2slow))


def make_fixtures(check_only=False, out=sys.stdout):
    bad = 0
    if not check_only:
        os.makedirs(FIXDIR, exist_ok=True)
    for name, fn in FIXTURES:
        data = fn()
        validate(data)
        path = os.path.join(FIXDIR, name)
        if check_only:
            try:
                have = open(path, "rb").read()
            except OSError:
                have = None
            if have != data:
                out.write("radsim: %s differs from what --make writes now\n" % path)
                bad += 1
        else:
            with open(path, "wb") as f:
                f.write(data)
            out.write("radsim: wrote %s (%d bytes)\n" % (path, len(data)))
    return bad == 0


# =============================================================================
# Structure walk - for coverage (SPEC.md 96.7: from the fixtures, not a list)
# =============================================================================
def structure(b):
    """What a (valid) tune contains, as plain Python data."""
    validate(b)
    v2 = b[0x10] == 0x21
    s = {"version": b[0x10], "flags": b[0x11], "rate": rate_tenths(b),
         "desc": description(b), "inst": {}, "orders": [], "tracks": {},
         "riffs": {}, "iriffs": {}}
    p = 0x12

    def walk(p, chan_riff):
        size = b[p] | (b[p + 1] << 8)
        p += 2
        e = p + size
        lines = []
        while p < e:
            lb = b[p]
            p += 1
            ents = []
            while True:
                x = b[p]
                p += 1
                ent = {"ch": x & 15, "note": None, "reuse": False, "inst": 0,
                       "eff": None, "par": 0}
                if x & 0x40:
                    ent["note"] = b[p] & 0x7F
                    ent["reuse"] = bool(b[p] & 0x80)
                    p += 1
                if x & 0x20:
                    ent["inst"] = b[p]
                    p += 1
                if x & 0x10:
                    ent["eff"], ent["par"] = b[p], b[p + 1]
                    p += 2
                ents.append(ent)
                if chan_riff or x & 0x80:
                    break
            lines.append((lb & 0x7F, ents))
        return e, lines

    if v2:
        if b[0x11] & 0x20:
            p = 0x14
        while b[p]:
            p += 1
        p += 1
        while b[p]:
            num = b[p]
            ln = b[p + 1]
            q = p + 2 + ln
            alg = b[q]
            rec = {"alg": alg & 7, "pan12": (alg >> 3) & 3, "pan34": (alg >> 5) & 3}
            if alg & 7 != 7:
                rec.update(detune=b[q + 2] >> 4, riff_speed=b[q + 2] & 15)
                q += FM_BODY
            else:
                q += MIDI_BODY
            if alg & 0x80:
                q, s["iriffs"][num] = walk(q, False)
            s["inst"][num] = rec
            p = q
        p += 1
        s["orders"] = list(b[p + 1:p + 1 + b[p]])
        p += 1 + b[p]
        while b[p] != 0xFF:
            num = b[p]
            p, s["tracks"][num] = walk(p + 1, False)
        p += 1
        while b[p] != 0xFF:
            rid = b[p]
            p, s["riffs"][rid] = walk(p + 1, True)
    else:
        if b[0x11] & 0x80:
            while b[p]:
                p += 1
            p += 1
        while b[p]:
            s["inst"][b[p]] = {}
            p += 12
        p += 1
        s["orders"] = list(b[p + 1:p + 1 + b[p]])
        p += 1 + b[p]
        for k in range(32):
            off = b[p + 2 * k] | (b[p + 2 * k + 1] << 8)
            if not off:
                continue
            lines = []
            q = off
            while True:
                lb = b[q]
                q += 1
                ents = []
                while True:
                    cb, nb, ib = b[q], b[q + 1], b[q + 2]
                    q += 3
                    ent = {"ch": cb & 0x7F, "note": nb & 0x7F if nb & 15 else None,
                           "inst": ((nb >> 7) << 4) | (ib >> 4), "eff": ib & 15 or None,
                           "par": 0, "reuse": False}
                    if ib & 15:
                        ent["par"] = b[q]
                        q += 1
                    ents.append(ent)
                    if cb & 0x80:
                        break
                lines.append((lb & 0x7F, ents))
                if lb & 0x80:
                    break
            s["tracks"][k] = lines
    return s


def coverage(b):
    """A set of facts about a tune, named the way SPEC.md 96.7's table names them."""
    s = structure(b)
    f = set()
    v2 = s["version"] == 0x21
    f.add("v2" if v2 else "v1")
    f.add("rate%d" % s["rate"])
    if s["flags"] & 0x40:
        f.add("slow")
    if v2 and s["flags"] & 0x20:
        f.add("bpm")
    if s["desc"] and any(s["desc"]):
        f.add("description")
    if any(o & 0x80 for o in s["orders"]):
        f.add("jump-marker")
    for num, rec in s["inst"].items():
        if num > 15:
            f.add("inst>15")
        if v2:
            f.add("alg%d" % rec["alg"])
            if rec["alg"] == 7:
                f.add("midi")
            else:
                f.add("pan12=%d" % rec["pan12"])
                f.add("pan34=%d" % rec["pan34"])
                if rec["detune"]:
                    f.add("detune")
    defined = set(s["inst"])
    kinds = [("pattern", t) for t in s["tracks"].values()] + \
            [("chanriff", t) for t in s["riffs"].values()] + \
            [("instriff", t) for t in s["iriffs"].values()]
    for kind, lines in kinds:
        for _, ents in lines:
            for e in ents:
                if kind == "pattern":
                    f.add("chan%d" % e["ch"])
                if e["inst"] and e["inst"] not in defined:
                    f.add("UNDEFINED-INSTRUMENT")
                if e["note"] is not None and e["note"] & 15 == 15:
                    f.add("key-off")
                if e["reuse"]:
                    f.add("reuse-instrument")
                eff = e["eff"]
                if eff is None or eff == 0:
                    continue
                f.add("fx%X" % eff)
                f.add("%s:fx%X" % (kind, eff))
                if eff == CM_TONESLIDE:
                    f.add("fx3+note" if e["note"] is not None else "fx3-note")
                if eff == CM_VOLSLIDE:
                    f.add("fxA-down" if e["par"] < 50 else "fxA-up" if e["par"] > 50 else "fxA-0")
                if kind != "pattern" and eff in (CM_RIFF, CM_TRANSPOSE):
                    f.add("riff-starts-riff")
                if kind != "pattern" and e["inst"] and s["iriffs"].get(e["inst"]):
                    f.add("riff-starts-riff")
    if s["riffs"]:
        f.add("channel-riff")
    if s["iriffs"]:
        f.add("instrument-riff")
    return f


# What SPEC.md 96.7's table promises for each fixture.
FIXTURE_PROMISES = {
    "RV1.RAD": {"v1", "description", "rate500", "fx1", "fx2", "fx3+note",
                "fx3-note", "fx5", "fxA-up", "fxA-down", "fxC", "fxD", "fxF",
                "key-off", "jump-marker", "inst>15"} |
               {"chan%d" % k for k in range(9)},
    "RV1SLOW.RAD": {"v1", "slow", "rate182"},
    "RV2.RAD": {"v2", "rate500", "midi", "channel-riff", "instrument-riff",
                "riff-starts-riff", "detune", "jump-marker", "pattern:fxD",
                "chanriff:fxD", "instriff:fxD",
                "key-off", "reuse-instrument"} |
               {"alg%d" % k for k in range(7)} |
               {"pan12=%d" % k for k in range(4)} |
               {"pan34=%d" % k for k in range(4)} |
               {"fx%X" % k for k in (1, 2, 3, 5, 10, 12, 13, 15, CM_IGNORE,
                                     CM_MULT, CM_RIFF, CM_TRANSPOSE,
                                     CM_FEEDBACK, CM_VOLUME)} |
               {"chan%d" % k for k in range(9)},
    "RV2BPM.RAD": {"v2", "bpm", "rate600"},
    "RV2SLOW.RAD": {"v2", "slow", "rate182"},
}


# =============================================================================
# --selfcheck
# =============================================================================
# Digests of each fixture's REFERENCE stream over SELFCHECK_FRAMES frames, taken
# when --crosscheck had proved every one of them equal to the reference player's
# (player20.cpp for 2.1, PLAYER.ASM with V1_DEVIATIONS for 1.0).  A change that
# moves one is a change to RE-PROVE with --crosscheck, not a number to paste.
SELFCHECK_FRAMES = 1600
PINNED = {
    "RV1.RAD": "9522d696a17f737c",
    "RV1SLOW.RAD": "783119c100a4edeb",
    "RV2.RAD": "8d277fa19ff492e3",
    "RV2BPM.RAD": "b94c3e98a6d12328",
    "RV2SLOW.RAD": "1808c0e0f83eb7e5",
}


def _api_constants():
    out = {}
    try:
        text = open(API_INC).read()
    except OSError:
        return None
    for m in re.finditer(r"^(RAD[EC]_\w+|RAD_MAXLEN|RAD_PNMAX)\s+equ\s+(\d+)", text, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


def selfcheck(verbose=False):
    fails = []

    def ck(cond, what):
        if not cond:
            fails.append(what)

    # 1. the mirror with apps/os88api.inc
    api = _api_constants()
    ck(api is not None, "apps/os88api.inc unreadable")
    if api:
        ck(api.get("RAD_MAXLEN") == RAD_MAXLEN, "RAD_MAXLEN differs from os88api.inc")
        ck(api.get("RAD_PNMAX") == RAD_PNMAX, "RAD_PNMAX differs from os88api.inc")
        for k, v in RADE.items():
            ck(api.get("RADE_" + k) == v, "RADE_%s differs from os88api.inc" % k)
        for k, v in RADC.items():
            ck(api.get("RADC_" + k) == v, "RADC_%s differs from os88api.inc" % k)
        ck(len([k for k in api if k.startswith("RADC_")]) == len(RADC),
           "os88api.inc has a RADC_ radsim does not")
        ck(set(RADC_REASON) == set(RADC.values()), "a RADC_ detail has no reason")

    # 2. the committed fixtures are what --make writes
    import io
    buf = io.StringIO()
    ck(make_fixtures(check_only=True, out=buf), "fixtures: " + buf.getvalue().strip())

    streams = {}
    for name, fn in FIXTURES:
        data = fn()
        # 3. valid, the right version and rate; the promises of SPEC.md 96.7
        ck(check(data) is None, "%s does not validate: %r" % (name, check(data)))
        cov = coverage(data)
        missing = FIXTURE_PROMISES[name] - cov
        ck(not missing, "%s no longer exercises %s" % (name, sorted(missing)))
        ck("UNDEFINED-INSTRUMENT" not in cov, "%s names an undefined instrument" % name)
        if data[0x10] == 0x21:
            ck(check(data, opl3=False) == (RADE["NEEDOPL3"], 0, 0),
               "%s is not refused on an OPL2" % name)
        ref = reference_stream(data, SELFCHECK_FRAMES)
        streams[name] = (data, ref)
        digest = hashlib.sha256(encode_rlg(ref)).hexdigest()[:16]
        if name in PINNED:
            ck(PINNED[name] == digest, "%s reference stream moved: %s, pinned %s"
               % (name, digest, PINNED[name]))
        if verbose:
            print("  %-12s %5d bytes  rate %4d  %6d ref writes  %6d sent  %s"
                  % (name, len(data), rate_tenths(data), len(ref),
                     len(sent_stream(ref)), digest))

    # 4. the start and stop sequences, SPEC.md 96.4.6
    data, ref = streams["RV2.RAD"]
    start, frames, stop = frames_of(ref)
    want = [(0x105, 1)]
    for r in range(0x20, 0xF6):
        v = 0xFF if 0x60 <= r < 0xA0 else 0
        want += [(r, v), (r + 0x100, v)]
    want += [(0x01, 0x20), (0x08, 0), (0xBD, 0), (0x104, 0), (0x105, 1)]
    ck(start == want, "2.1 start sequence is not SPEC.md 96.4.6's")
    ck(stop == want[1:] + [(0x104, 0), (0x105, 0)], "2.1 stop sequence is not 96.4.6's")
    ck(len(frames) == SELFCHECK_FRAMES, "frame markers miscounted")
    data1, ref1 = streams["RV1.RAD"]
    s1, f1, t1 = frames_of(ref1)
    w1 = [(r, 0) for r in range(0x20, 0xF6)] + [(0x01, 0x20), (0x08, 0), (0xBD, 0)]
    ck(s1 == w1, "1.0 start sequence is not 96.4.6's")
    ck(t1 == [(r, 0) for r in range(0x20, 0xF6)], "1.0 stop (OPL2) is not 96.4.6's")
    _, _, t13 = frames_of(reference_stream(data1, 1, opl3=True))
    ck(t13 == [(r, 0) for r in range(0x20, 0xF6)] + [(0x105, 0)],
       "1.0 stop on an OPL3 is not 96.4.6's (20h..F5h, then 105h alone)")

    # 4b. SPEC.md 34.11.2: nothing in 100h-1FFh but 105h while NEW = 0, over
    # every stream - reference and sent, 1.0 on an OPL3 included
    for name, (data, ref) in streams.items():
        variants = [("ref", ref), ("sent", sent_stream(ref))]
        if data[0x10] == 0x10:
            r3 = reference_stream(data, 200, opl3=True)
            variants += [("ref opl3", r3), ("sent opl3", sent_stream(r3))]
        for what, st in variants:
            bad = new_rule_violations(st)
            ck(not bad, "%s %s: a 1xxh write while NEW = 0 at record %r"
               % (name, what, bad[:3]))
    ck(new_rule_violations([(0x105, 1), (0x105, 0), (0x104, 0)]) == [2],
       "the NEW-rule checker does not catch a 104h after 105h <- 00h")

    # 4c. SPEC.md 34.12.2 step 0: a start after a stop plays what a fresh start
    # does - and without the re-zeroing (player20.cpp's Stop()) it does not
    # RV2.RAD re-triggers every channel before it slides, so it cannot see a
    # channel's frequency carried over; RESTART2 can - its first line is a
    # tone slide with no note before it, whose direction is read from the
    # channel's CURRENT frequency (0 fresh, the target after a first run)
    restart2 = build_v2("", 6, False, 0, {1: fm_inst(0, 0, 0, 0, 0, 0, 63, 1)}, [0],
                        {0: [(0, [E(0, note=(4, 1), inst=1, eff=CM_TONESLIDE, par=9)])]},
                        {})
    for name, data in (("RV1.RAD", streams["RV1.RAD"][0]),
                       ("RV2.RAD", streams["RV2.RAD"][0]), ("RESTART2", restart2)):
        fresh = reference_stream(data, 300)
        ck(restart_stream(data, 437, 300) == fresh,
           "%s: a restart does not play what a fresh start plays" % name)
        if name != "RV2.RAD":
            ck(restart_stream(data, 437, 300, rezero=False) != fresh,
               "%s: the restart row cannot tell re-zeroing from not" % name)

    # 5. what the engines must do, frame by frame
    # 1.0 frame 1: channel 0 keyed off, instrument 1 loaded in PLAYER.ASM's
    # order, A0h then B0h with key-on (C-3: 16Bh, octave 3)
    fr = f1[0]
    ck(fr[:1] == [(0xB0, 0)] and
       [r for r, _ in fr[1:12]] == [0x20, 0x23, 0x40, 0x43, 0x60, 0x63, 0x80, 0x83,
                                    0xE0, 0xE3, 0xC0],
       "1.0 instrument load order is not PLAYER.ASM's")
    ck(fr[12:14] == [(0xA0, 0x6B), (0xB0, 0x20 | 3 << 2 | 1)],
       "1.0 note-on is not A0h then B0h: %r" % (fr[12:14],))
    ins1 = _v1_instr(1)
    ck([v for _, v in fr[1:12]] == [ins1[i] for i in (1, 0, 3, 2, 5, 4, 7, 6, 10, 9, 8)],
       "1.0 instrument bytes are not routed per SPEC.md 96.4.2's table")
    # 2.1 frame 1: 104h carries channels 0 and 1 (algorithms 2, 3) only
    f1v2 = frames[0]
    last104 = [v for r, v in f1v2 if r == 0x104]
    ck(last104 and last104[-1] == 0x03, "104h after RV2 line 0 is %r, not 03h" % last104)
    # pan: instrument 3 (pan12 2, pan34 3, fb12 1, fb34 7) on channel 0
    c0 = [v for r, v in f1v2 if r == 0xC0]
    c3 = [v for r, v in f1v2 if r == 0xC3]
    ck(c0[:1] == [((3 ^ 3) << 4) | 7 << 1 | 0] and c3[:1] == [((2 ^ 3) << 4) | 1 << 1 | 0],
       "C0h/C3h for an algorithm 2 instrument: %r %r" % (c0, c3))
    # the MIDI instrument on channel 8 wrote no operator
    ck(not [r for r, _ in f1v2 if r in (0x40 + 0x115, 0x40 + 0x112, 0xC8, 0x1C8)],
       "a MIDI instrument wrote an FM register")
    ck(any(r >= 0x100 for r, _ in f1v2), "RV2 frame 1 never touched the second array")
    # rates, SPEC.md 34.13.1
    ck(rate_tenths(compose_rv2bpm()) == 600, "BPM 150 is not 60.0 Hz")
    ck(rate_tenths(compose_rv1slow()) == 182, "1.0 slow-timer is not 18.2 Hz")

    # 6. the sent stream is the reference less repeats, and the log round-trips
    for name, (data, ref) in streams.items():
        sent = sent_stream(ref)
        ck(decode_rlg(encode_rlg(sent)) == sent, "%s: .RLG does not round-trip" % name)
        ck(len(sent) < len(ref), "%s: the shadow filtered nothing" % name)
        ss, sf, st = frames_of(sent)
        rs, rf, rt = frames_of(ref)
        ck(ss == rs and st == rt, "%s: start/stop were filtered" % name)
        ck(len(sf) == len(rf), "%s: sent stream lost a frame marker" % name)
        shadow = {}
        for r, v in rs:
            shadow[r] = v
        ok = True
        for rfw, sfw in zip(rf, sf):
            want = []
            for r, v in rfw:
                if shadow.get(r) != v:
                    want.append((r, v))
                shadow[r] = v
            ok = ok and want == sfw
        ck(ok, "%s: the sent stream is not the reference less repeats" % name)
    # engines reach every order entry and wrap
    eng = EngineV2(compose_rv2(), lambda r, v: None)
    seen = set()
    for _ in range(SELFCHECK_FRAMES * 2):
        eng.update()
        seen.add(eng.order)
    ck(seen >= {0, 1, 2, 3}, "RV2 did not visit every order entry: %r" % sorted(seen))
    ck(title_line(compose_rv1()) == "RV1 - radsim fixture", "title line decode")
    ck(description(compose_rv1())[1] == "RAD 1.0, every effect,     nine channels",
       "space-run decode")
    ck(title_line(compose_rv2slow()) == "(no description)", "empty title")
    return fails


# =============================================================================
# --crosscheck - player20.cpp on the host, never in this tree
# =============================================================================
HARNESS_CPP = r"""
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "player20.cpp"
static FILE *g_out;
static void wr(void *, uint16_t reg, uint8_t val) {
    unsigned char rec[3] = { (unsigned char)(reg & 0xFF), (unsigned char)(reg >> 8), val };
    fwrite(rec, 1, 3, g_out);
}
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 3;
    static unsigned char buf[0x20000];
    fread(buf, 1, sizeof(buf) - 256, f);
    fclose(f);
    long frames = atol(argv[2]);
    g_out = fopen(argv[3], "wb");
    void *mem = calloc(1, sizeof(RADPlayer));
    RADPlayer *p = new (mem) RADPlayer();
    p->Init(buf, wr, 0);
    wr(0, 0xFFFE, 0);
    for (long i = 0; i < frames; i++) { p->Update(); wr(0, 0xFFFF, 0); }
    wr(0, 0xFFFD, 0);
    p->Stop();
    wr(0, 0x104, 0); wr(0, 0x105, 0);
    fclose(g_out);
    return 0;
}
"""


def crosscheck(ref_dir, frames=3000, extra=()):
    """0 pass or skip, 1 a mismatch."""
    if not ref_dir or not os.path.isfile(os.path.join(ref_dir, "Source", "player20.cpp")):
        print("radsim crosscheck: SKIP - no RAD V2.0a archive (pass --ref DIR or set "
              "RADSIM_REF to the unzipped archive; it is never in this tree)")
        return 0
    cxx = shutil.which("c++") or shutil.which("clang++") or shutil.which("g++")
    if not cxx:
        print("radsim crosscheck: SKIP - no C++ compiler (c++, clang++ or g++)")
        return 0
    tmp = tempfile.mkdtemp(prefix="radsim-")
    try:
        shutil.copy(os.path.join(ref_dir, "Source", "player20.cpp"), tmp)
        with open(os.path.join(tmp, "harness.cpp"), "w") as f:
            f.write(HARNESS_CPP)
        exe = os.path.join(tmp, "harness")
        r = subprocess.run([cxx, "-O0", "-w", "-o", exe, os.path.join(tmp, "harness.cpp")],
                           capture_output=True, text=True)
        if r.returncode:              # the archive AND a compiler are here, so a
            print("radsim crosscheck: FAIL - the player20.cpp harness did not "
                  "compile:\n" + r.stderr[-800:])      # broken harness is a failure,
            return 1                  # never "nothing to check"
        tunes = [(n, fn()) for n, fn in FIXTURES if fn()[0x10] == 0x21]
        for sub in ("Tunes", "MIDI"):
            d = os.path.join(ref_dir, sub)
            if os.path.isdir(d):
                for fname in sorted(os.listdir(d)):
                    if fname.lower().endswith(".rad"):
                        tunes.append((sub + "/" + fname, open(os.path.join(d, fname), "rb").read()))
        tunes += list(extra)
        bad = 0
        for name, data in tunes:
            why = check(data)
            if why is not None:
                print("  %-40s REFUSED %r" % (name, why))
                bad += 1
                continue
            if data[0x10] != 0x21:
                continue
            tpath = os.path.join(tmp, "t.rad")
            with open(tpath, "wb") as f:
                f.write(data)
            lpath = os.path.join(tmp, "t.rlg")
            r = subprocess.run([exe, tpath, str(frames), lpath], capture_output=True)
            if r.returncode:
                print("  %-40s harness exit %d" % (name, r.returncode))
                bad += 1
                continue
            theirs = decode_rlg(open(lpath, "rb").read())
            ours = reference_stream(data, frames)
            ck = ours[1:]                 # SPEC.md 96.4.5 deviation 1
            if ck == theirs:
                sent = sent_stream(ours)
                print("  %-40s %7d writes identical over %d frames (sent %d, %.0f%%; "
                      "busiest frame %d notes, busiest %d frames %d, of RAD_PNMAX %d)"
                      % (name, len(theirs), frames, len(sent),
                         100.0 * len(sent) / len(ours), most_notes(data, frames),
                         RAD_FRMAX, most_notes(data, frames, RAD_FRMAX),
                         RAD_PNMAX))
            else:
                i = next((k for k in range(min(len(ck), len(theirs))) if ck[k] != theirs[k]),
                         min(len(ck), len(theirs)))
                frame = sum(1 for r_, _ in ck[:i] if r_ == LOG_FRAME_END)
                print("  %-40s DIFFER at record %d (frame %d): ours %r theirs %r"
                      % (name, i, frame, ck[i:i + 3], theirs[i:i + 3]))
                bad += 1
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# --- the 1.0 half: RAD V1.1a's PLAYER.ASM, run in Unicorn -------------------
# PLAYER.ASM is TASM; this translates it to NASM IN A SCRATCH DIRECTORY (never
# in this tree), replaces its `Adlib` port writer with a logger, applies the
# three deviations this engine takes (SPEC.md 96.4.5's note nibble, and
# V1_DEVIATIONS 1 and 2) and runs it in Unicorn's 16-bit x86.  --faithful-v1
# leaves the deviations out, which is how RV1.RAD is shown to exercise them.
V1_WORD_VARS = {"NoteFreq", "Effects", "AdlibPort", "InstPtrs", "ModSeg",
                "OrderSize", "OrderList", "OrderPos", "PatternList", "PatternPos"}


def _v1_player_nasm(src, deviations=("nibble", "last", "seek")):
    lines = src.replace("\r", "").split("\n")
    lines = lines[[i for i, l in enumerate(lines) if l.startswith("cmPortamentoUp")][0]:]
    out = ["cpu 286", "bits 16", "org 100h",
           "call InitPlayer", "nop", "call PlayMusic", "nop", "call EndPlayer", "nop",
           "LogPtr: dw 0", "LastLine: db 0"]
    in_fx, in_adlib = False, False

    def csvar(m):
        pre, name, off, idx = m.group(1) or "", m.group(2), m.group(3) or "", m.group(4)
        size = "" if pre else ("word " if name in V1_WORD_VARS else "byte ")
        return pre + size + "[cs:" + name + off + ("+" + idx if idx else "") + "]"

    for l in lines:
        s = l.partition(";")[0].rstrip()
        if not s.strip():
            continue
        if s.startswith("Adlib:"):
            in_adlib = True
            out += ["Adlib: push di", "push es", "push ax", "mov di,5000h", "mov es,di",
                    "mov di,[cs:LogPtr]", "cld", "xchg ah,al", "stosw",
                    "mov [cs:LogPtr],di", "pop ax", "pop es", "pop di", "ret"]
            continue
        if in_adlib:
            if not s.startswith("AdlibPort"):
                continue
            in_adlib = False
        s = re.sub(r"^(\w+)\s*=\s*(.*)$", r"\1 equ \2", s)
        s = s.replace("@@", ".")
        s = re.sub(r"\b(\w+)\s+dup\s*\(\?\)", r"\1 dup (0)", s)
        m = re.match(r"^(\w+)\s+(db|dw)\s+(\d+)\s+dup\s*\((\d+)\)", s)
        if m:
            s = "%s: times %s %s %s" % (m.group(1), m.group(3), m.group(2), m.group(4))
        m = re.match(r"^(\w+)\s+(db|dw)\s+\?", s)
        if m:
            s = "%s: %s 0" % (m.group(1), m.group(2))
        s = re.sub(r"^\s*(locals|jumps)\s*$", "", s)
        s = s.replace(" shl ", " << ").replace(" not ", " ~")
        s = re.sub(r"rept\s+(\d+)", r"%rep \1", s).replace("endm", "%endrep")
        m = re.match(r"^(\s*(?:\.?\w+:)?\s*)(push|pop)\s+(\w+(?:\s+\w+)+)\s*$", s)
        if m:
            regs = m.group(3).split()
            s = "\n".join([m.group(1) + m.group(2) + " " + regs[0]] +
                          ["\t%s %s" % (m.group(2), r) for r in regs[1:]])
        s = re.sub(r"(byte|word) ptr (\w\w):\[([^\]]*)\]", r"\1 [\2:\3]", s)
        s = re.sub(r"(byte|word) ptr cs:", r"\1 cs:", s)
        s = re.sub(r"cs:(\w+)-(\d+)\[(\w+)\]",
                   lambda m: ("word " if m.group(1) in V1_WORD_VARS else "byte ") +
                   "[cs:%s-%s+%s]" % (m.group(1), m.group(2), m.group(3)), s)
        s = re.sub(r"(byte|word) ptr \[", r"\1 [", s)
        s = re.sub(r"\b(\d+)\[(\w+)\]", r"[\2+\1]", s)
        s = re.sub(r"((?:byte|word) )?(?<!\[)cs:([A-Za-z]\w*)(-\d+)?(?:\[(\w+)\])?", csvar, s)
        s = s.replace("[byte [cs:si-1]]", "[cs:si-1]")
        s = s.replace("'AR'", "'RA'").replace("' D'", "'D '")
        if s.startswith("Effects"):
            in_fx = True
        if in_fx and re.match(r"^(Effects)?\s+dw\s+\.", s):
            s = re.sub(r"\.(\w+)", r"PlayNote.\1", s)
        if s.startswith("NoteFreq"):
            in_fx = False
        s = re.sub(r"^(\w+)\s+(db|dw)\b", r"\1: \2", s)
        out.append(s)
    text = "\n".join(out) + "\n"

    def sub(old, new):
        assert text.count(old) >= 1, old
        return text.replace(old, new, 1)
    if "nibble" in deviations:
        text = sub("or\tal,al\n\t\tjz\t.lb\n", "test\tal,15\n\t\tjz\t.lb\n")
    if "seek" in deviations:
        text = sub("test\tbyte [cs:si-1],15", "test\tbyte [si-1],15")
    if "last" in deviations:
        old = "\t\tmov\tword [cs:PatternPos],si"
        text = sub(old, "\t\ttest\tbyte [cs:LastLine],1\n\t\tjnz\t.keepz\n" + old + "\n\t.keepz:")
        text = sub("\t\tmov\tword [cs:PatternPos],0",
                   "\t\tmov\tword [cs:PatternPos],0\n\t\tmov\tbyte [cs:LastLine],1")
        text = sub("\t.lc:\tinc\tsi",
                   "\t\tjmp\t.lcc\n\t.lc:\tmov\tbyte [cs:LastLine],0\n\t.lcc:\tinc\tsi")
    return text


def _v1_run_asm(code, tune, frames):
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
    from unicorn.x86_const import (UC_X86_REG_CS, UC_X86_REG_ES, UC_X86_REG_DS,
                                   UC_X86_REG_SS, UC_X86_REG_SP)
    mu = Uc(UC_ARCH_X86, UC_MODE_16)
    mu.mem_map(0, 0x100000)
    mu.mem_write(0x10100, code)
    mu.mem_write(0x20000, tune)
    mu.reg_write(UC_X86_REG_SS, 0x3000)
    mu.reg_write(UC_X86_REG_SP, 0xFFFE)
    mu.reg_write(UC_X86_REG_DS, 0x1000)

    def call(at):
        mu.mem_write(0x1010C, b"\0\0")
        mu.reg_write(UC_X86_REG_CS, 0x1000)
        mu.reg_write(UC_X86_REG_ES, 0x2000)
        mu.emu_start(0x10000 + at, 0x10000 + at + 3, count=10000000)
        n = struct.unpack("<H", bytes(mu.mem_read(0x1010C, 2)))[0]
        data = bytes(mu.mem_read(0x50000, n))
        return [(data[i], data[i + 1]) for i in range(0, n, 2)]
    out = call(0x100) + [(LOG_START_END, 0)]
    for _ in range(frames):
        out += call(0x104) + [(LOG_FRAME_END, 0)]
    return out + [(LOG_STOP, 0)] + call(0x108)


def crosscheck_v1(ref_dir, frames=2000, faithful=False):
    """0 pass or skip, 1 a mismatch.  `ref_dir` holds RAD V1.1a's PLAYER.ASM."""
    asm = os.path.join(ref_dir or "", "PLAYER.ASM")
    if not ref_dir or not os.path.isfile(asm):
        print("radsim crosscheck 1.0: SKIP - no RAD V1.1a archive (pass --ref1 DIR or "
              "set RADSIM_REF1 to the directory holding PLAYER.ASM and TUNES.ZIP)")
        return 0
    nasm = shutil.which("nasm")
    try:
        import unicorn  # noqa: F401
    except ImportError:
        print("radsim crosscheck 1.0: SKIP - no Python `unicorn` module (pip install "
              "unicorn, into a scratch venv)")
        return 0
    if not nasm:
        print("radsim crosscheck 1.0: SKIP - no nasm")
        return 0
    tmp = tempfile.mkdtemp(prefix="radsim1-")
    try:
        src = open(asm, "rb").read().decode("latin-1")
        with open(os.path.join(tmp, "p.asm"), "w") as f:
            f.write(_v1_player_nasm(src, () if faithful else ("nibble", "last", "seek")))
        r = subprocess.run([nasm, "-f", "bin", "-o", os.path.join(tmp, "p.bin"),
                            os.path.join(tmp, "p.asm")], capture_output=True, text=True)
        if r.returncode:
            print("radsim crosscheck 1.0: FAIL - the translated PLAYER.ASM did not "
                  "assemble:\n" + r.stderr[-800:])
            return 1
        code = open(os.path.join(tmp, "p.bin"), "rb").read()
        tunes = [(n, fn()) for n, fn in FIXTURES if fn()[0x10] == 0x10]
        zpath = os.path.join(ref_dir, "TUNES.ZIP")
        if os.path.isfile(zpath):
            import zipfile
            with zipfile.ZipFile(zpath) as z:
                for zn in sorted(z.namelist()):
                    if zn.upper().endswith(".RAD"):
                        tunes.append(("TUNES.ZIP/" + zn, z.read(zn)))
        bad = 0
        for name, data in tunes:
            why = check(data)
            if why is not None:
                print("  %-40s REFUSED %r" % (name, why))
                bad += 1
                continue
            ours = reference_stream(data, frames)
            theirs = _v1_run_asm(code, data, frames)
            if ours == theirs:
                sent = sent_stream(ours)
                print("  %-40s %7d records identical over %d frames (sent %.0f%%)"
                      % (name, len(ours), frames, 100.0 * len(sent) / len(ours)))
            else:
                i = next((k for k in range(min(len(ours), len(theirs))) if ours[k] != theirs[k]),
                         min(len(ours), len(theirs)))
                frame = sum(1 for r_, _ in ours[:i] if r_ == LOG_FRAME_END)
                print("  %-40s DIFFER at record %d (frame %d): ours %r theirs %r"
                      % (name, i, frame, ours[i:i + 3], theirs[i:i + 3]))
                bad += 1
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# =============================================================================
# CLI
# =============================================================================
def dump(stream, out=sys.stdout):
    frame = 0
    for r, v in stream:
        if r == LOG_START_END:
            out.write("-- start sequence ends; frame 1 follows\n")
        elif r == LOG_FRAME_END:
            frame += 1
            out.write("-- end of frame %d\n" % frame)
        elif r == LOG_STOP:
            out.write("-- stop sequence\n")
        else:
            out.write("%03Xh <- %02Xh\n" % (r, v))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("tune", nargs="*")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--make", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--crosscheck", action="store_true")
    ap.add_argument("--ref", default=os.environ.get("RADSIM_REF"),
                    help="the unzipped RAD V2.0a archive (Source/player20.cpp)")
    ap.add_argument("--ref1", default=os.environ.get("RADSIM_REF1"),
                    help="RAD V1.1a's directory (PLAYER.ASM, TUNES.ZIP)")
    ap.add_argument("--faithful-v1", action="store_true",
                    help="run PLAYER.ASM without radsim's deviations")
    ap.add_argument("--frames", type=int)
    ap.add_argument("--log")
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--sent", action="store_true", help="the sent stream (34.12.6)")
    ap.add_argument("--opl3", action="store_true", help="a 1.0 tune on an OPL3")
    ap.add_argument("--opl2", action="store_true", help="validate as on an OPL2")
    a = ap.parse_args(argv)

    if a.selfcheck:
        fails = selfcheck(a.verbose)
        for f in fails:
            print("radsim selfcheck: FAIL " + f)
        if fails:
            return 1
        print("radsim selfcheck: ok (%d fixtures, %d frames each)"
              % (len(FIXTURES), SELFCHECK_FRAMES))
        return 0
    if a.make:
        return 0 if make_fixtures(check_only=a.check) else 1
    if a.crosscheck:
        rc2 = 0 if a.faithful_v1 else crosscheck(a.ref, frames=a.frames or 3000)
        rc1 = crosscheck_v1(a.ref1, frames=a.frames or 2000, faithful=a.faithful_v1)
        return rc1 or rc2
    if not a.tune:
        ap.print_help()
        return 2
    rc = 0
    for path in a.tune:
        data = open(path, "rb").read()
        why = check(data, opl3=not a.opl2)
        if why is not None:
            code, detail, off = why
            name = [k for k, v in RADE.items() if v == code][0]
            if code == RADE["CORRUPT"]:
                print("%s: RADE_CORRUPT - %s at %04Xh" % (path, RADC_REASON[detail], off))
            else:
                print("%s: RADE_%s" % (path, name))
            rc = 1
            continue
        t = rate_tenths(data)
        print("%s: RAD %d.%d, %d.%d Hz, %d bytes, \"%s\""
              % (path, data[0x10] >> 4, data[0x10] & 15, t // 10, t % 10, len(data),
                 title_line(data)))
        if a.log or a.dump:
            stream = reference_stream(data, a.frames or 1000, opl3=a.opl3)
            if a.sent:
                stream = sent_stream(stream)
            if a.log:
                with open(a.log, "wb") as f:
                    f.write(encode_rlg(stream))
            if a.dump:
                dump(stream)
    return rc


if __name__ == "__main__":
    sys.exit(main())
