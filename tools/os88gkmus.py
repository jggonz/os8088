#!/usr/bin/env python3
"""os88gkmus.py - the game kit's music and effects compiler (SPEC.md 133.8).

A SONG BANK and an EFFECTS BANK are art records that apps/cc/gkmus.inc plays:
three FM voices through SOUND.DRV, or the melody alone on the PC speaker, and
effects through the tone tier. The SOURCE is a readable text file; this tool
turns it into the bytes and can play them back on the host.

    compile_music(path, symbols) -> bytes      the song bank   (D1, pinned)
    compile_sfx(path, symbols)   -> bytes      the effects bank (D1, pinned)

    os88gkmus.py --selfcheck                       the built-in fixtures
    os88gkmus.py info MUSIC.TXT [SFX.TXT]          songs, effects, sizes, loops
    os88gkmus.py render MUSIC.TXT --song N|NAME --wav OUT.WAV [--speaker] [--secs S]
    os88gkmus.py render-sfx SFX.TXT --wav OUT.WAV [--speaker]
    os88gkmus.py part -o OUT.BIN MUSIC.TXT SFX.TXT  a two-record art part (gkmusdemo)

THE NOTATION (music.txt). A '#' that starts a word starts a comment (G#4 is a
pitch); blank lines are ignored.

    patch NAME                      an OPL2 instrument: 11 register bytes
      mod 20=01 40=1A 60=F2 80=54 E0=00     modulator (hex reg=value; absent = 0)
      car 20=01 40=08 60=F3 80=65 E0=01     carrier
      c0  0C                                feedback / connection (C0h)
    end

    pattern NAME                    a phrase any voice may call; no nesting
      C3:8 G2 C3 G2 | ...
    end

    song NAME
      beat 8                        OS ticks per quarter note (8 = 136.5 BPM)
      time 4/4                      for the `|` bar checks
      voice 1 lead                  voice 1 is the melody (the speaker plays it)
      voice 2 bass
      voice 3 pad
      1: E5:8 G5 C6:4 | r:2 ...     a voice's notes; lines append
      2: @bassA*4 % @bassB*2        @NAME*N calls a pattern N times
      3: ...
    end

A NOTE is PITCH[:DUR]. PITCH is a letter, an optional # or b, an octave
(C4 = middle C), or `r` for a rest. DUR is 1 2 4 8 16 32 (or any N: 1/N of a
whole note, so 12 is an eighth triplet), with `.` dots and `+` ties
(`:4.`, `:2+8`); without one, the previous duration holds. Every duration
must be a whole number of ticks at the song's `beat`, so loops are exact.
`|` checks the voice is on a bar line. `%` is the loop point: either every
voice has one at the same tick, or none does (the song loops from the start).
Every voice must be the same length in ticks.

THE EFFECTS (sfx.txt): `sfx NAME [duck]` ... `end`, a list of steps in OS
ticks: PITCH:T or HZ:T (a number is Hz), `r:T` a rest, and `slide A B N`
(N one-tick steps from A to B, geometric). `duck` keys the FM music off
while the effect plays. The order of the file is the effect numbers.

THE BYTES (all little endian; offsets are from the record's first byte):

  song bank   'G' 'M' 01 nsongs  hz_off:w  patch_off:w
              nsongs x 18: per voice (start:w loop:w patch:b 0); start 0 = silent
              ...voice streams and patterns...  hz:w[nhz]  patch:11b[npatch]
  stream      n         0..7C: note index (0 rest, n -> hz[n-1]), same duration
              80|n d    a note with a new duration d ticks (1..255)
              7E o:w c  call the pattern at o, c times (one level)
              7D        return (the end of a pattern)
              7F        the end of the voice: loop, or fall silent
  fx bank     'G' 'X' 01 nfx  off:w[nfx]  then per effect: flags (1 = duck),
              steps (t, hz:w)..., 00

Only build, info and --selfcheck are reached by make; render needs numpy.
"""
import argparse, math, os, re, struct, sys
from fractions import Fraction

TICK_HZ = 1193182 / 65536          # the OS tick, 18.2065 Hz
END, CALL, RET = 0x7F, 0x7E, 0x7D
SONGREC = 18                       # a song's record: 3 x (start, loop, patch, 0)
MAXNOTE = 0x7C                     # hz table entries 1..124
HZ_LO, HZ_HI = 19, 6208            # OSAPI_SND_FM's note-on range (SPEC.md 34.2)
NAMES = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


class GkmError(Exception):
    pass


def fail(msg):
    raise GkmError(msg)


def note_midi(tok, where):
    m = re.fullmatch(r"([A-Ga-g])([#b]?)(-?\d)", tok)
    if not m:
        fail(f"{where}: {tok!r} is not a pitch (C4 is middle C, r a rest)")
    n = NAMES[m.group(1).lower()] + {"": 0, "#": 1, "b": -1}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + n


def midi_hz(m):
    return int(round(440.0 * 2 ** ((m - 69) / 12.0)))


def pitch_hz(tok, where):
    """A pitch token or a plain number to Hz; 0 for a rest."""
    if tok.lower() == "r":
        return 0
    if tok.isdigit():
        hz = int(tok)
    else:
        hz = midi_hz(note_midi(tok, where))
    if not HZ_LO <= hz <= HZ_HI:
        fail(f"{where}: {tok} is {hz} Hz, outside {HZ_LO}..{HZ_HI}")
    return hz


def parse_dur(text, where):
    """'4.' '2+8' '12' -> a Fraction of a whole note."""
    total = Fraction(0)
    for part in text.split("+"):
        m = re.fullmatch(r"(\d+)(\.*)", part)
        if not m or int(m.group(1)) == 0:
            fail(f"{where}: {text!r} is not a duration")
        d = Fraction(1, int(m.group(1)))
        add = d
        for _ in m.group(2):
            add /= 2
            d += add
        total += d
    return total


def strip(line):
    """A comment is a '#' that starts a word, so G#4 is a pitch."""
    return re.split(r"(?:^|\s)#", line, maxsplit=1)[0].strip()


def blocks(path):
    """The file as (kind, name, args, [(lineno, text)], lineno) blocks."""
    out, cur = [], None
    for no, raw in enumerate(open(path).read().split("\n"), 1):
        line = strip(raw)
        if not line:
            continue
        where = f"{os.path.basename(path)}:{no}"
        w = line.split()
        if cur is None:
            if w[0] not in ("patch", "pattern", "song", "sfx") or len(w) < 2:
                fail(f"{where}: expected patch/pattern/song/sfx NAME, got {line!r}")
            if not re.fullmatch(r"[a-z][a-z0-9_]*", w[1]):
                fail(f"{where}: name {w[1]!r}: lower case letters, digits, _")
            cur = (w[0], w[1], w[2:], [], where)
        elif line == "end":
            out.append(cur)
            cur = None
        else:
            cur[3].append((where, line))
    if cur:
        fail(f"{cur[4]}: {cur[0]} {cur[1]} has no `end`")
    return out


# ---- patches ----------------------------------------------------------------

OPREGS = (0x20, 0x40, 0x60, 0x80, 0xE0)


def parse_patch(name, lines):
    mod = car = None
    c0 = None
    for where, line in lines:
        w = line.split()
        if w[0] in ("mod", "car"):
            regs = dict.fromkeys(OPREGS, 0)
            for kv in w[1:]:
                m = re.fullmatch(r"([0-9A-Fa-f]{2})=([0-9A-Fa-f]{1,2})", kv)
                if not m or int(m.group(1), 16) not in OPREGS:
                    fail(f"{where}: {kv!r}: want REG=VAL, REG one of 20 40 60 80 E0")
                regs[int(m.group(1), 16)] = int(m.group(2), 16)
            if regs[0xE0] > 3:
                fail(f"{where}: E0 (waveform) is 0..3 on an OPL2")
            vals = [regs[r] for r in OPREGS]
            if w[0] == "mod":
                mod = vals
            else:
                car = vals
        elif w[0] == "c0" and len(w) == 2:
            c0 = int(w[1], 16)
            if c0 > 0x0F:
                fail(f"{where}: c0 is 0..0F (feedback << 1 | connection)")
        else:
            fail(f"{where}: in a patch: mod / car / c0, got {line!r}")
    if mod is None or car is None or c0 is None:
        fail(f"patch {name}: needs mod, car and c0")
    return bytes(mod + car + [c0])


# ---- voices -----------------------------------------------------------------

class Voice:
    """One voice's (or pattern's) events in ticks."""

    def __init__(self, where):
        self.where = where
        self.ev = []            # ('n', hz, ticks) | ('call', name, count, ticks_total)
        self.ticks = 0
        self.pos = Fraction(0)  # whole notes
        self.loop = None        # (event index, tick)
        self.dur = None


def parse_notes(v, text, where, wpt, bar, patterns, in_pattern):
    """Append one line's tokens to voice v. wpt = ticks per whole note."""
    for tok in text.split():
        if tok == "|":
            if bar and v.pos % bar:
                fail(f"{where}: `|` at {v.pos} whole notes, not on a bar of {bar}")
            continue
        if tok == "%":
            if in_pattern:
                fail(f"{where}: a loop point inside a pattern")
            if v.loop is not None:
                fail(f"{where}: a second loop point")
            v.loop = (len(v.ev), v.ticks)
            continue
        if tok.startswith("@"):
            if in_pattern:
                fail(f"{where}: a pattern may not call a pattern")
            m = re.fullmatch(r"@([a-z][a-z0-9_]*)(?:\*(\d+))?", tok)
            if not m or m.group(1) not in patterns:
                fail(f"{where}: {tok!r}: no such pattern (define it above the song)")
            n = int(m.group(2) or 1)
            if not 1 <= n <= 255:
                fail(f"{where}: {tok}: a pattern repeats 1..255 times")
            p = patterns[m.group(1)]
            v.ev.append(("call", m.group(1), n, p.ticks))
            v.ticks += p.ticks * n
            v.pos += p.pos * n
            continue
        if ":" in tok:
            p, d = tok.split(":", 1)
            v.dur = parse_dur(d, where)
        else:
            p = tok
            if v.dur is None:
                fail(f"{where}: {tok!r} has no duration and none came before it")
        t = v.dur * wpt
        if t.denominator != 1 or t < 1:
            fail(f"{where}: {tok!r} lasts {float(t):.2f} ticks; every note must be a "
                 f"whole number of ticks (change `beat` or the duration)")
        hz = pitch_hz(p, where)
        t = int(t)
        if t > 255:
            fail(f"{where}: {tok!r} lasts {t} ticks, at most 255")
        v.ev.append(("n", hz, t))
        v.ticks += int(v.dur * wpt)
        v.pos += v.dur


def encode_stream(events, hzidx, call_off, explicit_first=True):
    """Events -> stream bytes (without the terminator). call_off(name) gives
    the pattern's offset, filled in later (a placeholder list is returned)."""
    out = bytearray()
    fix = []                     # (position in out, pattern name)
    cur = None if explicit_first else -1
    for e in events:
        if e[0] == "call":
            out += bytes([CALL, 0, 0, e[2]])
            fix.append((len(out) - 3, e[1]))
            cur = None           # the duration after a return is unknown
            continue
        _, hz, t = e
        n = hzidx[hz] if hz else 0
        if t != cur:
            out += bytes([0x80 | n, t])
            cur = t
        else:
            out.append(n)
    return out, fix


def compile_music_text(path):
    pats, songs, patches = {}, [], {}
    porder = []
    for kind, name, args, lines, where in blocks(path):
        if kind == "sfx":
            fail(f"{where}: an sfx block in a music file")
        if kind == "patch":
            if name in patches:
                fail(f"{where}: patch {name} twice")
            patches[name] = parse_patch(name, lines)
            porder.append(name)
        elif kind == "pattern":
            if name in pats:
                fail(f"{where}: pattern {name} twice")
            # a pattern borrows its tick scale from `beat N` in its args
            beat = None
            if len(args) == 2 and args[0] == "beat":
                beat = int(args[1])
            if not beat:
                fail(f"{where}: `pattern {name} beat N` (ticks per quarter)")
            v = Voice(where)
            for w2, line in lines:
                parse_notes(v, line, w2, beat * 4, None, pats, True)
            if not v.ev:
                fail(f"{where}: pattern {name} is empty")
            v.beat = beat
            pats[name] = v
        else:
            songs.append(parse_song(name, args, lines, where, pats, patches))
    if not songs:
        fail(f"{path}: no songs")
    if len(songs) > 255:
        fail(f"{path}: more than 255 songs")
    return songs, pats, patches, porder


def parse_song(name, args, lines, where, pats, patches):
    beat, bar, vp = None, None, {}
    voices = {}
    for w2, line in lines:
        w = line.split()
        if w[0] == "beat" and len(w) == 2:
            beat = int(w[1])
            if not 1 <= beat <= 60:
                fail(f"{w2}: beat is 1..60 ticks a quarter note")
        elif w[0] == "time" and len(w) == 2 and re.fullmatch(r"\d+/\d+", w[1]):
            a, b = w[1].split("/")
            bar = Fraction(int(a), int(b))
        elif w[0] == "voice" and len(w) == 3 and w[1] in "123":
            if w[2] not in patches:
                fail(f"{w2}: no patch {w[2]!r} (define it above the song)")
            vp[int(w[1])] = w[2]
        elif re.fullmatch(r"[123]:", w[0]):
            if beat is None:
                fail(f"{w2}: `beat` must come before the notes")
            k = int(w[0][0])
            v = voices.setdefault(k, Voice(w2))
            for tok in w[1:]:
                if tok.startswith("@"):
                    pn = tok[1:].split("*")[0]
                    if pn in pats and pats[pn].beat != beat:
                        fail(f"{w2}: pattern {pn} is beat {pats[pn].beat}, the song beat {beat}")
            parse_notes(v, " ".join(w[1:]), w2, beat * 4, bar, pats, False)
        else:
            fail(f"{w2}: in a song: beat / time / voice N PATCH / N: notes, got {line!r}")
    if 1 not in voices:
        fail(f"{where}: song {name} has no voice 1 (the melody)")
    lens = {k: v.ticks for k, v in voices.items()}
    if len(set(lens.values())) != 1:
        fail(f"{where}: song {name}: voices differ in length (ticks {lens}); pad with rests")
    loops = {k: (v.loop[1] if v.loop else None) for k, v in voices.items()}
    if len(set(loops.values())) != 1:
        fail(f"{where}: song {name}: loop points differ (ticks {loops}); `%` in every voice or none")
    for k in voices:
        if k not in vp:
            fail(f"{where}: song {name}: voice {k} has notes but no `voice {k} PATCH`")
    lt = loops[1] or 0
    if lens[1] - lt < 1:
        fail(f"{where}: song {name}: the loop is empty")
    return {"name": name, "beat": beat, "bar": bar, "voices": voices, "patch": vp,
            "ticks": lens[1], "loop": lt, "where": where}


def compile_music(path, symbols=None):
    """music.txt -> the song bank bytes (SPEC.md 133.8). symbols: unused,
    taken for D1's signature (a future bank may name art IDs)."""
    songs, pats, patches, porder = compile_music_text(path)
    used_p = []
    for s in songs:
        for k in (1, 2, 3):
            pn = s["patch"].get(k)
            if pn and pn not in used_p:
                used_p.append(pn)
    hzs = set()
    for s in songs:
        for v in s["voices"].values():
            hzs.update(e[1] for e in v.ev if e[0] == "n" and e[1] > 0)
    used_pats = []
    for s in songs:
        for v in s["voices"].values():
            for e in v.ev:
                if e[0] == "call" and e[1] not in used_pats:
                    used_pats.append(e[1])
    for p in used_pats:
        hzs.update(e[1] for e in pats[p].ev if e[0] == "n" and e[1] > 0)
    hzl = sorted(hzs)
    if len(hzl) > MAXNOTE:
        fail(f"{path}: {len(hzl)} distinct pitches, at most {MAXNOTE}")
    hzidx = {hz: i + 1 for i, hz in enumerate(hzl)}

    head = 8 + SONGREC * len(songs)
    body = bytearray()
    fixes = []
    srec = []
    for s in songs:
        starts, loops = [0, 0, 0], [0, 0, 0]
        for k in (1, 2, 3):
            v = s["voices"].get(k)
            if not v:
                continue
            li = v.loop[0] if v.loop else 0
            a, fa = encode_stream(v.ev[:li], hzidx, None)
            b, fb = encode_stream(v.ev[li:], hzidx, None)   # the loop starts explicit
            starts[k - 1] = head + len(body)
            loops[k - 1] = head + len(body) + len(a)
            base = head + len(body)
            fixes += [(base + p, n) for p, n in fa]
            fixes += [(base + len(a) + p, n) for p, n in fb]
            body += a + b + bytes([END])
        pidx = [used_p.index(s["patch"][k]) if k in s["patch"] else 0 for k in (1, 2, 3)]
        srec.append((starts, loops, pidx))
    patoff = {}
    for p in used_pats:
        patoff[p] = head + len(body)
        a, f = encode_stream(pats[p].ev, hzidx, None)
        if f:
            fail(f"pattern {p}: calls a pattern")
        body += a + bytes([RET])
    hz_off = head + len(body)
    for hz in hzl:
        body += struct.pack("<H", hz)
    patch_off = head + len(body)
    for p in used_p:
        body += patches[p]
    rec = bytearray(b"GM" + bytes([1, len(songs)]) + struct.pack("<HH", hz_off, patch_off))
    for starts, loops, pidx in srec:
        for k in range(3):
            rec += struct.pack("<HHBB", starts[k], loops[k], pidx[k], 0)
    rec += body
    for pos, name in fixes:
        struct.pack_into("<H", rec, pos, patoff[name])
    if len(rec) > 0xFFFF:
        fail(f"{path}: the bank is {len(rec)} bytes")
    return bytes(rec)


# ---- effects ----------------------------------------------------------------

def parse_sfx(path):
    fx = []
    for kind, name, args, lines, where in blocks(path):
        if kind != "sfx":
            fail(f"{where}: only sfx blocks in an effects file")
        if any(f["name"] == name for f in fx):
            fail(f"{where}: sfx {name} twice")
        flags = 0
        for a in args:
            if a == "duck":
                flags |= 1
            else:
                fail(f"{where}: sfx {name}: unknown flag {a!r} (duck)")
        steps = []
        for w2, line in lines:
            w = line.split()
            if w[0] == "slide":
                if len(w) != 4:
                    fail(f"{w2}: slide FROM TO STEPS")
                a, b, n = pitch_hz(w[1], w2), pitch_hz(w[2], w2), int(w[3])
                if not 2 <= n <= 64 or not a or not b:
                    fail(f"{w2}: slide needs two pitches and 2..64 steps")
                for i in range(n):
                    steps.append((int(round(a * (b / a) ** (i / (n - 1)))), 1))
                continue
            for tok in w:
                if ":" not in tok:
                    fail(f"{w2}: {tok!r}: an effect step is PITCH:TICKS")
                p, t = tok.split(":", 1)
                if not t.isdigit() or not 1 <= int(t) <= 255:
                    fail(f"{w2}: {tok!r}: ticks are 1..255")
                steps.append((pitch_hz(p, w2), int(t)))
        if not steps:
            fail(f"{where}: sfx {name} is empty")
        fx.append({"name": name, "flags": flags, "steps": steps, "where": where})
    if not fx:
        fail(f"{path}: no effects")
    return fx


def compile_sfx(path, symbols=None):
    """sfx.txt -> the effects bank bytes (SPEC.md 133.8)."""
    fx = parse_sfx(path)
    if len(fx) > 255:
        fail(f"{path}: more than 255 effects")
    head = 4 + 2 * len(fx)
    rec = bytearray(b"GX" + bytes([1, len(fx)]) + bytes(2 * len(fx)))
    for i, f in enumerate(fx):
        struct.pack_into("<H", rec, 4 + 2 * i, len(rec))
        rec.append(f["flags"])
        for hz, t in f["steps"]:
            rec += bytes([t]) + struct.pack("<H", hz)
        rec.append(0)
    assert len(rec) >= head
    return bytes(rec)


# ---- the player, in Python: the reference for gkmus.inc ---------------------

class Bank:
    def __init__(self, rec):
        if rec[:3] != b"GM\x01":
            fail("not a song bank")
        self.rec = rec
        self.n = rec[3]
        self.hz_off, self.patch_off = struct.unpack_from("<HH", rec, 4)

    def song(self, i):
        v = [struct.unpack_from("<HHBB", self.rec, 8 + SONGREC * i + 6 * k) for k in range(3)]
        return [x[0] for x in v], [x[1] for x in v], [x[2] for x in v]

    def hz(self, n):
        return struct.unpack_from("<H", self.rec, self.hz_off + 2 * (n - 1))[0]

    def patch(self, i):
        return self.rec[self.patch_off + 11 * i: self.patch_off + 11 * i + 11]


def play(bank, song, loop, ticks):
    """Run gkmus's sequencer for `ticks` ticks (elapsed 1 each, the first at
    0 like gkm_play): a list per voice of (tick, hz or 0 for key-off, rem).
    This mirrors the asm decision for decision."""
    starts, loops, _ = bank.song(song)
    r = bank.rec
    V = []
    for k in range(3):
        V.append({"ptr": starts[k], "rem": 0, "loop": loops[k], "ret": 0, "pst": 0,
                  "cnt": 0, "dur": 1})
    out = [[], [], []]
    for tick in range(ticks + 1):
        el = 0 if tick == 0 else 1
        for k, v in enumerate(V):
            if not v["ptr"]:
                continue
            v["rem"] -= el
            pend = None
            guard = 0
            while v["rem"] <= 0:
                si = v["ptr"]
                while True:
                    guard += 1
                    if guard > 10000:
                        fail("the stream spins (an empty loop?)")
                    b = r[si]
                    si += 1
                    op = b & 0x7F
                    if op == END:
                        if not loop:
                            si = 0
                            break
                        si = v["loop"]
                        continue
                    if op == CALL:
                        v["pst"] = struct.unpack_from("<H", r, si)[0]
                        v["cnt"] = r[si + 2]
                        v["ret"] = si + 3
                        si = v["pst"]
                        continue
                    if op == RET:
                        v["cnt"] -= 1
                        si = v["pst"] if v["cnt"] else v["ret"]
                        continue
                    if b & 0x80:
                        v["dur"] = r[si]
                        si += 1
                    pend = op
                    v["rem"] += v["dur"]
                    break
                v["ptr"] = si
                if not si:
                    pend = 0
                    break
            if pend is not None:
                out[k].append((tick, bank.hz(pend) if pend else 0, v["rem"]))
        if all(not v["ptr"] for v in V):
            break
    return out


def fx_steps(rec, i):
    if rec[:3] != b"GX\x01" or i >= rec[3]:
        fail("no such effect")
    o = struct.unpack_from("<H", rec, 4 + 2 * i)[0]
    flags = rec[o]
    o += 1
    steps = []
    while rec[o]:
        steps.append((struct.unpack_from("<H", rec, o + 1)[0], rec[o]))
        o += 3
    return flags, steps


# ---- the host preview: a rough OPL2 and a rough speaker ---------------------

MULT = [0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15]
FBK = [0, 1 / 16, 1 / 8, 1 / 4, 1 / 2, 1, 2, 4]


def _env(np, n, rate, ar, dr, sl, rr, egt, keyoff):
    """An OPL-like envelope as linear amplitude, n samples, key-off at sample
    keyoff. Attack in amplitude, decay/release in dB."""
    t = np.arange(n) / rate
    att_t = 0 if ar >= 15 else (2.826 / 2 ** (ar - 1) if ar else 1e9)
    dec_rate = 0 if not dr else 96.0 / (39.28 / 2 ** (dr - 1))       # dB/s
    rel_rate = 0 if not rr else 96.0 / (39.28 / 2 ** (rr - 1))
    sl_db = 93.0 if sl == 15 else sl * 3.0
    db = np.zeros(n)
    after = t - att_t
    if dec_rate:
        d = np.clip(after, 0, None) * dec_rate
        if egt:
            d = np.minimum(d, sl_db)
        else:
            reach = sl_db / dec_rate
            d = np.where(after < reach, d,
                         sl_db + np.clip(after - reach, 0, None) * rel_rate)
        db = d
    amp = 10 ** (-np.minimum(db, 96) / 20)
    if att_t > 0:
        amp = np.where(t < att_t, t / att_t, amp)
    if keyoff < n:
        k = keyoff
        a0 = amp[k] if k < n else 0
        tr = (np.arange(n - k)) / rate
        amp[k:] = a0 * 10 ** (-np.minimum(tr * (rel_rate or 1e-3), 96) / 20)
    return amp


def _wave(np, ph, w):
    s = np.sin(ph)
    if w == 1:
        return np.maximum(s, 0)
    if w == 2:
        return np.abs(s)
    if w == 3:
        return np.where((ph % (2 * np.pi)) < np.pi / 2, np.abs(s), 0)
    return s


def fm_note(np, patch, hz, n, keyoff, rate):
    def op(regs):
        c, l, ad, sr, wv = regs
        return {"mult": MULT[c & 15], "egt": bool(c & 0x20), "tl": l & 0x3F,
                "ar": ad >> 4, "dr": ad & 15, "sl": sr >> 4, "rr": sr & 15, "w": wv & 3}
    m, c = op(patch[0:5]), op(patch[5:10])
    fb, conn = (patch[10] >> 1) & 7, patch[10] & 1
    t = np.arange(n) / rate
    em = _env(np, n, rate, m["ar"], m["dr"], m["sl"], m["rr"], m["egt"], keyoff)
    ec = _env(np, n, rate, c["ar"], c["dr"], c["sl"], c["rr"], c["egt"], keyoff)
    am = em * 10 ** (-m["tl"] * 0.75 / 20)
    ac = ec * 10 ** (-c["tl"] * 0.75 / 20)
    pm = 2 * np.pi * hz * m["mult"] * t
    # feedback, approximated in one pass: a self-modulated sine
    mo = _wave(np, pm + FBK[fb] * np.pi * np.sin(pm) * (1 if fb else 0), m["w"]) * am
    pc = 2 * np.pi * hz * c["mult"] * t
    if conn:
        return mo + _wave(np, pc, c["w"]) * ac
    return _wave(np, pc + 4 * np.pi * mo, c["w"]) * ac


def render_song(music, song, secs, speaker, rate=22050, loop=True):
    import numpy as np
    bank = Bank(music)
    starts, loops, pidx = bank.song(song)
    ticks = int(secs * TICK_HZ) + 1
    ev = play(bank, song, loop, ticks)
    spt = rate / TICK_HZ
    total = int((ticks + 4) * spt)
    mix = np.zeros(total)
    for k in range(3):
        if speaker and k:
            break
        evs = ev[k]
        for i, (tk, hz, rem) in enumerate(evs):
            if not hz:
                continue
            s0 = int(tk * spt)
            if speaker:
                d = rem if rem <= 2 else rem - 1
                n = int(d * spt)
                ph = np.arange(n) * hz / rate
                mix[s0:s0 + n] += 0.25 * np.where((ph % 1) < 0.5, 1.0, -1.0)[:max(0, total - s0)]
                continue
            nxt = evs[i + 1][0] if i + 1 < len(evs) else ticks
            keyoff = int((nxt - tk) * spt)
            n = min(keyoff + int(0.4 * rate), total - s0)
            if n <= 0:
                continue
            mix[s0:s0 + n] += 0.3 * fm_note(np, bank.patch(pidx[k]), hz, n, keyoff, rate)
    return mix, rate


def render_fx(sfx, speaker, rate=22050, gap=0.5):
    import numpy as np
    spt = rate / TICK_HZ
    parts = []
    for i in range(sfx[3]):
        flags, steps = fx_steps(sfx, i)
        n = int(sum(t for _, t in steps) * spt)
        buf = np.zeros(n + int(gap * rate))
        s = 0
        for hz, t in steps:
            k = int(t * spt)
            if hz:
                ph = np.arange(k) * hz / rate
                if speaker:
                    w = np.where((ph % 1) < 0.5, 1.0, -1.0)
                else:       # SOUND.DRV's tone patch: a half-sine carrier, lightly driven
                    w = np.maximum(np.sin(2 * np.pi * ph + 0.8 * np.sin(2 * np.pi * ph)), 0) * 2 - 0.6
                buf[s:s + k] += 0.3 * w
            s += k
        parts.append(buf)
    return np.concatenate(parts), rate


def write_wav(path, samples, rate):
    import numpy as np
    peak = max(1e-9, float(np.max(np.abs(samples))))
    pcm = (samples / peak * 0.9 * 32767).astype("<i2").tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16) +
                b"data" + struct.pack("<I", len(pcm)) + pcm)


# ---- the art part for the demo package --------------------------------------

def art_part(records):
    """{id: bytes} -> an art part the band core reads (bx_art_at): two magic
    bytes, a word count, a word offset per ID, the records word aligned."""
    n = max(records) + 1
    head = bytearray(b"GK" + struct.pack("<H", n) + bytes(2 * n))
    body = bytearray()
    for i in range(n):
        if i not in records:
            continue
        off = len(head) + len(body)
        if off & 1:
            body.append(0)
            off += 1
        struct.pack_into("<H", head, 4 + 2 * i, off)
        body += records[i]
    return bytes(head + body)


# ---- info and selfcheck -----------------------------------------------------

def info(music_path, sfx_path=None):
    songs, pats, patches, porder = compile_music_text(music_path)
    rec = compile_music(music_path)
    hzn = (Bank(rec).patch_off - Bank(rec).hz_off) // 2
    print(f"{music_path}: {len(rec)} bytes, {len(songs)} songs, {hzn} pitches, "
          f"{len(patches)} patches, {len(pats)} patterns")
    for i, s in enumerate(songs):
        bpm = 60 * TICK_HZ / s["beat"]
        bars = Fraction(s["ticks"], s["beat"] * 4) / (s["bar"] or 1)
        print(f"  {i} {s['name']:10s} beat {s['beat']} ({bpm:.1f} BPM) {s['ticks']} ticks "
              f"= {s['ticks'] / TICK_HZ:.1f} s, {float(bars):g} bars, loop at tick {s['loop']} "
              f"({s['loop'] / TICK_HZ:.1f} s)")
    if sfx_path:
        fx = parse_sfx(sfx_path)
        srec = compile_sfx(sfx_path)
        print(f"{sfx_path}: {len(srec)} bytes, {len(fx)} effects")
        for i, f in enumerate(fx):
            t = sum(s[1] for s in f["steps"])
            print(f"  {i:2d} {f['name']:10s} {t:3d} ticks ({t / TICK_HZ:.2f} s)"
                  f"{' duck' if f['flags'] & 1 else ''}")


SELF_MUSIC = """
patch p
  mod 20=21 40=10 60=F2 80=54 E0=00
  car 20=01 40=00 60=F3 80=65 E0=01
  c0 0C
end
pattern bass beat 4
  C3:8 G2
end
song a
  beat 4
  time 2/4
  voice 1 p
  voice 2 p
  1: C4:8 E4 G4 C5 | % C5:4 r:8 G4 |
  2: @bass*2 | % @bass*2 |
end
"""
SELF_SFX = """
sfx ping duck
  C6:2 r:1 1000:3
end
sfx up
  slide 200 800 4
end
"""


def selfcheck():
    import tempfile
    d = tempfile.mkdtemp()
    mp, sp = os.path.join(d, "m.txt"), os.path.join(d, "s.txt")
    open(mp, "w").write(SELF_MUSIC)
    open(sp, "w").write(SELF_SFX)
    m = compile_music(mp, {})
    b = Bank(m)
    assert b.n == 1 and m[:4] == b"GM\x01\x01", m[:4]
    starts, loops, pidx = b.song(0)
    assert starts[2] == 0 and pidx == [0, 0, 0]
    # beat 4: an eighth is 2 ticks, a quarter 4. Voice 1: C4 E4 G4 C5 (2 each),
    # then the loop: C5 4, rest 2, G4 2.
    ev = play(b, 0, True, 40)
    v1 = [(t, h) for t, h, _ in ev[0]]
    assert v1[:7] == [(0, 262), (2, 330), (4, 392), (6, 523), (8, 523), (12, 0), (14, 392)], v1[:7]
    assert v1[7] == (16, 523), v1[7]           # looped back to the % point
    v2 = [(t, h) for t, h, _ in ev[1]]
    assert v2[:5] == [(0, 131), (2, 98), (4, 131), (6, 98), (8, 131)], v2[:5]
    assert v2[8] == (16, 131), v2[8]
    # once: falls silent at tick 16
    ev = play(b, 0, False, 40)
    assert ev[0][-1] == (16, 0, 0) or ev[0][-1][1] == 0, ev[0][-1]
    assert b.patch(0) == bytes([0x21, 0x10, 0xF2, 0x54, 0, 0x01, 0, 0xF3, 0x65, 1, 0x0C])
    s = compile_sfx(sp, {})
    assert s[:4] == b"GX\x01\x02"
    f, st = fx_steps(s, 0)
    assert f == 1 and st == [(1047, 2), (0, 1), (1000, 3)], st
    f, st = fx_steps(s, 1)
    assert f == 0 and st == [(200, 1), (317, 1), (504, 1), (800, 1)], st
    part = art_part({0: m, 1: s})
    o0, o1 = struct.unpack_from("<HH", part, 4)
    assert part[o0:o0 + 2] == b"GM" and part[o1:o1 + 2] == b"GX" and not o1 & 1
    # the refusals
    for bad, why in ((SELF_MUSIC.replace("2: @bass*2 | % @bass*2 |", "2: @bass*2 | % @bass*1 |"), "length"),
                     (SELF_MUSIC.replace("C4:8 E4", "C4:16 E4:8."), "bar"),
                     (SELF_MUSIC.replace("beat 4\n  time", "beat 3\n  time"), "ticks"),
                     (SELF_MUSIC.replace("% C5:4", "C5:4"), "loop")):
        open(mp, "w").write(bad)
        try:
            compile_music(mp, {})
        except GkmError:
            continue
        raise AssertionError(f"selfcheck: a {why} error was accepted")
    print("os88gkmus: selfcheck OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", nargs="?", choices=["info", "render", "render-sfx", "part", "build"])
    ap.add_argument("files", nargs="*")
    ap.add_argument("-o", "--out")
    ap.add_argument("--wav")
    ap.add_argument("--song", default="0")
    ap.add_argument("--secs", type=float, default=0)
    ap.add_argument("--speaker", action="store_true")
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    try:
        if a.selfcheck:
            selfcheck()
        if a.cmd == "info":
            info(*a.files)
        elif a.cmd == "build":
            data = compile_music(a.files[0]) if "sfx" not in os.path.basename(a.files[0]) \
                else compile_sfx(a.files[0])
            open(a.out, "wb").write(data)
        elif a.cmd == "part":
            data = art_part({0: compile_music(a.files[0]), 1: compile_sfx(a.files[1])})
            open(a.out, "wb").write(data)
            print(f"os88gkmus: {a.out}: {len(data)} bytes")
        elif a.cmd == "render":
            songs = compile_music_text(a.files[0])[0]
            names = [s["name"] for s in songs]
            i = names.index(a.song) if a.song in names else int(a.song)
            secs = a.secs or (songs[i]["ticks"] / TICK_HZ + (0 if a.once else songs[i]["ticks"]
                                                            - songs[i]["loop"]) / TICK_HZ)
            mix, rate = render_song(compile_music(a.files[0]), i, secs, a.speaker,
                                    loop=not a.once)
            write_wav(a.wav, mix, rate)
            print(f"os88gkmus: {a.wav}: {names[i]}, {secs:.1f} s")
        elif a.cmd == "render-sfx":
            mix, rate = render_fx(compile_sfx(a.files[0]), a.speaker)
            write_wav(a.wav, mix, rate)
            print(f"os88gkmus: {a.wav}")
        elif not a.selfcheck:
            ap.print_help()
    except GkmError as e:
        sys.exit(f"os88gkmus: {e}")


if __name__ == "__main__":
    main()
