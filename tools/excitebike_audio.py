#!/usr/bin/env python3
"""EXCITEBIKE sound compiler (SPEC.md 102.5, docs/plans/EXCITEBIKE-PLAN.md 11).

    python3 tools/excitebike_audio.py [--src apps/excitebike/audio] [-o OUTDIR]
    python3 tools/excitebike_audio.py --selfcheck

Compiles the committed, original sound sources into ONE binary, EXB.SND, and a
generated include, exbsnd.inc.  Python standard library only, deterministic.
Nothing here reads anything outside `--src`: no ROM, no disassembly, no
recording (the art and audio policy, plan section 0).

SOURCES (all in apps/excitebike/audio/, all composed for this project)

  sfx.txt     one effect a line:  name  priority  (frames hz) (frames hz) ...
              frames are 60.0988 Hz game steps, hz 0 = a rest.
  *.mml       one song a file, in a small text notation:

                  title  Any words             (documentation only)
                  tempo  138                   quarter notes a minute
                  loop   0                     bar the song returns to, or `once`
                  v1 o5 a8 a8 r8 a8 g8 e8 a4 | ...     voice 1 (the lead)
                  v2 ...                                voice 2 (harmony)
                  v3 ...                                voice 3 (bass)

              A voice may continue on later lines (one phrase a line).

              Notes: a-g, `+`/`#` sharp, `-` flat, then a length (a divisor of
              a whole note: 4 = quarter, 8 = eighth; default `lN`), then dots.
              `r` a rest, `oN` the octave (o4 c = middle C), `<` `>` an octave
              down/up, `lN` the default length, `&` ties a note to the next of
              the same pitch, `|` a bar line (checked: it must fall on a multiple
              of four beats, and every voice must have the same length).

EXB.SND layout (little endian):
    +0   'EXBS'
    +4   byte  format 2
    +5   byte  effect count E
    +6   byte  song count S
    +7   byte  note table entries (72: index 1 = C2 ... 72 = B7)
    +8   word  total length
    +10  word  offset of the engine table (64 words)
    +12  word  offset of the note-Hz table (72 words, index n at +2*(n-1))
    +14  E x word  offset of each effect record
    then S x 12-byte song records: 3 x word stream offset, 3 x word loop offset
                   (0 = the song plays once)
    effect record: byte priority, byte note count N, then N x (byte frames,
                   word hz)            (hz 0 = rest)
    voice stream:  events of (byte pitch, byte steps), pitch 0 = rest, 1..72 =
                   a note, 255 = the end; steps 1..255
    engine table:  64 words, hz[k] = round(base * 2 ** (k / 24)), base 175
    note table:    72 words (offset in the header)
    speed table:   76 bytes straight after the note table: for the speed / 16, the steps
                   of k the rider's speed adds to the idle step, ((16 i + 8) x 51) >> 10
                   (the guest indexes it: no multiply in the frame)
"""
import argparse
import os
import struct
import sys
from fractions import Fraction

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "apps", "excitebike", "audio")
SND_BUDGET = 3072
ENGINE_BASE = 175.0
HZ_MIN, HZ_MAX = 19, 6208
STEPS_HZ = 60.0988            # the game's steps a second (SPEC.md 102.1)
FORMAT = 2
NNOTES = 72                   # C2 .. B7
KTAB_N = 76                   # speed / 16 up to 1,200 (the guest's XU_SPD_CAP)
MIDI_C2 = 36
SONG_REC = 12
END = 255
NVOICES = 3
NAMES = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def parse_sfx(text):
    fx = []
    for n, line in enumerate(text.splitlines(), 1):
        s = line.split("#", 1)[0].strip()
        if not s:
            continue
        head, _, rest = s.partition("(")
        f = head.split()
        if len(f) != 2:
            raise SystemExit("sfx.txt:%d: expected `name priority (frames hz)...`" % n)
        name, pri = f[0], int(f[1])
        notes = []
        for grp in ("(" + rest).replace(")", ") ").split(")"):
            grp = grp.strip().lstrip("(").strip()
            if not grp:
                continue
            fr, hz = (int(v) for v in grp.split())
            if not 1 <= fr <= 255:
                raise SystemExit("sfx.txt:%d: frames %d out of 1..255" % (n, fr))
            if hz and not HZ_MIN <= hz <= HZ_MAX:
                raise SystemExit("sfx.txt:%d: %d Hz outside %d..%d" % (n, hz, HZ_MIN, HZ_MAX))
            notes.append((fr, hz))
        if not notes or not 1 <= pri <= 15 or len(notes) > 255:
            raise SystemExit("sfx.txt:%d: bad effect %s" % (n, name))
        if name in [e[0] for e in fx]:
            raise SystemExit("sfx.txt:%d: duplicate effect %s" % (n, name))
        fx.append((name, pri, notes))
    if not fx:
        raise SystemExit("sfx.txt: no effects")
    return fx


def engine_table():
    return [int(round(ENGINE_BASE * 2 ** (k / 24.0))) for k in range(64)]


def note_hz(midi):
    return int(round(440.0 * 2 ** ((midi - 69) / 12.0)))


def note_table():
    return [note_hz(MIDI_C2 + i) for i in range(NNOTES)]


def speed_table():
    """the engine's target step for a speed: idle + this, from speed / 16 (a bin's centre x 51 / 1024)"""
    return [((16 * i + 8) * 51) >> 10 for i in range(KTAB_N)]


# ---- the score notation ---------------------------------------------------------
def steps_at(beats, tempo):
    """the game step a point `beats` quarter notes into a song falls on.  Every event's
    duration is the difference of two of these, so rounding never drifts and every voice
    of a song lands on the same grid."""
    return int(round(float(beats) * 60.0 / tempo * STEPS_HZ))


def parse_voice(src, where):
    """-> [(start_beat, beats, midi or 0)] with ties merged; and the bar lines' beats."""
    octv, dflt = 4, 4
    pos = Fraction(0)
    events = []
    bars = []
    tie = False
    for tok in src.split():
        if tok == "|":
            bars.append(pos)
            continue
        if tok[0] == "&":
            tie = True
            tok = tok[1:]
            if not tok:
                continue
        c = tok[0].lower()
        rest = tok[1:]
        if c == "o" and rest.isdigit():
            octv = int(rest)
            continue
        if c == "l" and rest.isdigit():
            dflt = int(rest)
            if dflt not in (1, 2, 4, 8, 16, 32):
                raise SystemExit("%s: bad default length %s" % (where, tok))
            continue
        if tok == "<":
            octv -= 1
            continue
        if tok == ">":
            octv += 1
            continue
        if c not in NAMES and c != "r":
            raise SystemExit("%s: unknown token %r" % (where, tok))
        semi = 0
        if c != "r":
            while rest and rest[0] in "+#-":
                semi += -1 if rest[0] == "-" else 1
                rest = rest[1:]
        digits = ""
        while rest and rest[0].isdigit():
            digits += rest[0]
            rest = rest[1:]
        div = int(digits) if digits else dflt
        if div not in (1, 2, 4, 8, 16, 32):
            raise SystemExit("%s: bad length in %r" % (where, tok))
        length = Fraction(4, div)
        add = length / 2
        while rest.startswith("."):
            length += add
            add /= 2
            rest = rest[1:]
        if rest.endswith("&"):
            rest = rest[:-1]
            trailing_tie = True
        else:
            trailing_tie = False
        if rest:
            raise SystemExit("%s: trailing junk in %r" % (where, tok))
        midi = 0 if c == "r" else 12 * (octv + 1) + NAMES[c] + semi
        if midi and not MIDI_C2 <= midi < MIDI_C2 + NNOTES:
            raise SystemExit("%s: %r is outside C2..B7" % (where, tok))
        if tie and events and events[-1][2] == midi and midi:
            s, l, m = events[-1]
            events[-1] = (s, l + length, m)
        else:
            if tie:
                raise SystemExit("%s: `&` needs the same pitch on both sides (%r)" % (where, tok))
            events.append((pos, length, midi))
        pos += length
        tie = trailing_tie
    if tie:
        raise SystemExit("%s: a tie into nothing" % where)
    for b in bars:
        if b % 4:
            raise SystemExit("%s: a bar line falls at beat %s, not a multiple of 4" % (where, b))
    return events, pos


def parse_song(text, name):
    tempo, loop, title = None, None, name
    voices = {}
    for n, line in enumerate(text.splitlines(), 1):
        s = line.split("#", 1)[0].strip()
        if not s:
            continue
        key, _, rest = s.partition(" ")
        rest = rest.strip()
        where = "%s:%d" % (name, n)
        if key == "title":
            title = rest
        elif key == "tempo":
            tempo = int(rest)
            if not 40 <= tempo <= 240:
                raise SystemExit("%s: tempo %d out of 40..240" % (where, tempo))
        elif key == "loop":
            loop = None if rest == "once" else int(rest)
        elif key in ("v1", "v2", "v3"):
            i = int(key[1]) - 1                     # a voice may continue on later lines
            voices[i] = ((voices[i][0] + " " + rest, voices[i][1]) if i in voices else (rest, where))
        else:
            raise SystemExit("%s: unknown line %r" % (where, key))
    if tempo is None or loop == "" or sorted(voices) != [0, 1, 2]:
        raise SystemExit("%s: needs tempo, loop and voices v1 v2 v3" % name)
    parsed = [parse_voice(voices[i][0], voices[i][1]) for i in range(NVOICES)]
    total = parsed[0][1]
    for i in range(1, NVOICES):
        if parsed[i][1] != total:
            raise SystemExit("%s: voice %d is %s beats, voice 1 is %s" % (name, i + 1, parsed[i][1], total))
    if total % 4:
        raise SystemExit("%s: %s beats is not a whole number of 4/4 bars" % (name, total))
    return {"name": name, "title": title, "tempo": tempo, "loop": loop,
            "bars": int(total // 4), "voices": [p[0] for p in parsed]}


def encode_song(song, base):
    """-> (stream bytes per voice, loop offsets relative to each stream, total steps, loop step)"""
    tempo, loop = song["tempo"], song["loop"]
    if loop is not None and not 0 <= loop < song["bars"]:
        raise SystemExit("%s: loop bar %d is outside 0..%d" % (song["name"], loop, song["bars"] - 1))
    loop_beat = None if loop is None else 4 * loop
    total_steps = steps_at(song["bars"] * 4, tempo)
    streams, loops = [], []
    for vi, events in enumerate(song["voices"]):
        out = bytearray()
        loop_off = None
        prev = 0
        for start, beats, midi in events:
            s0 = steps_at(start, tempo)
            s1 = steps_at(start + beats, tempo)
            assert s0 == prev
            dur = s1 - s0
            if not 1 <= dur <= 255:
                raise SystemExit("%s voice %d: a note of %d steps (1..255) at beat %s"
                                 % (song["name"], vi + 1, dur, start))
            if loop_beat is not None and start == loop_beat:
                loop_off = len(out)
            out += bytes([0 if not midi else midi - MIDI_C2 + 1, dur])
            prev = s1
        if prev != total_steps:
            raise SystemExit("%s voice %d: %d steps, expected %d" % (song["name"], vi + 1, prev, total_steps))
        if loop_beat is not None and loop_off is None:
            raise SystemExit("%s voice %d: no note begins at loop bar %d" % (song["name"], vi + 1, loop))
        out += bytes([END, 0])
        streams.append(bytes(out))
        loops.append(loop_off)
    return streams, loops, total_steps, (None if loop_beat is None else steps_at(loop_beat, tempo))


def load_songs(src):
    songs = []
    for f in sorted(os.listdir(src)):
        if f.endswith(".mml"):
            songs.append(parse_song(open(os.path.join(src, f)).read(), f[:-4]))
    return songs


# ---- the blob ---------------------------------------------------------------------
def compile_audio(src=SRC):
    """-> (EXB.SND bytes, exbsnd.inc text, report lines)."""
    fx = parse_sfx(open(os.path.join(src, "sfx.txt")).read())
    songs = load_songs(src)
    eng = engine_table()
    nt = note_table()
    E, S = len(fx), len(songs)
    fxtab = 14
    songtab = fxtab + 2 * E
    head = songtab + SONG_REC * S
    body = bytearray()

    def here():
        return head + len(body)

    fxoffs = []
    for name, pri, notes in fx:
        fxoffs.append(here())
        body += bytes([pri, len(notes)]) + b"".join(struct.pack("<BH", f, h) for f, h in notes)
    recs, srep = [], []
    for song in songs:
        streams, loops, total, loop_step = encode_song(song, 0)
        offs, lo = [], []
        for st, lp in zip(streams, loops):
            offs.append(here())
            lo.append(0 if lp is None else here() + lp)
            body += st
        recs.append(struct.pack("<6H", *offs, *lo))
        events = sum(len(s) // 2 - 1 for s in streams)
        srep.append("  song %-9s tempo %3d, %2d bars, %d voices, %5d steps (%5.1f s), loop %s, %d events"
                    % (song["name"], song["tempo"], song["bars"], NVOICES, total, total / STEPS_HZ,
                       "once" if loop_step is None else "bar %d at step %d" % (song["loop"], loop_step),
                       events))
    eng_off = here()
    body += b"".join(struct.pack("<H", v) for v in eng)
    note_off = here()
    body += b"".join(struct.pack("<H", v) for v in nt)
    ktab_off = here()
    body += bytes(speed_table())
    total = head + len(body)
    out = (b"EXBS" + bytes([FORMAT, E, S, NNOTES]) + struct.pack("<HHH", total, eng_off, note_off) +
           b"".join(struct.pack("<H", o) for o in fxoffs) + b"".join(recs) + bytes(body))
    assert len(out) == total
    if total > SND_BUDGET:
        raise SystemExit("EXB.SND is %d bytes, over its %d budget (plan 12.5)" % (total, SND_BUDGET))
    inc = ["; generated by tools/excitebike_audio.py - do not edit",
           "EXB_SND_SIZE equ %d" % total, "EXB_SND_EFFECTS equ %d" % E,
           "EXB_SND_SONGS equ %d" % S, "EXB_SND_FXTAB equ %d" % fxtab,
           "EXB_SND_SONGTAB equ %d" % songtab, "EXB_SND_ENGINE equ %d" % eng_off,
           "EXB_SND_NOTES equ %d" % note_off, "EXB_SND_KTAB equ %d" % ktab_off,
           "EXB_KTAB_N equ %d" % KTAB_N, "EXB_SONG_REC equ %d" % SONG_REC]
    for i, (name, pri, notes) in enumerate(fx):
        inc.append("EXBFX_%s equ %d" % (name.upper(), i))
        inc.append("EXBFXP_%s equ %d" % (name.upper(), pri))
    for i, song in enumerate(songs):
        inc.append("EXBSONG_%s equ %d" % (song["name"].upper(), i))
    report = ["EXB.SND: %d bytes of %d budget, %d effects, %d songs, engine table %d words, note table %d words, "
              "speed table %d bytes" % (total, SND_BUDGET, E, S, len(eng), len(nt), KTAB_N)]
    for name, pri, notes in fx:
        report.append("  effect %-12s priority %d, %d notes, %d frames"
                      % (name, pri, len(notes), sum(f for f, _ in notes)))
    report += srep
    return out, "\n".join(inc) + "\n", report


def decode(blob):
    """The compiler's own reader of what it wrote (the test has an independent one):
    -> {"effects": [(pri, [(frames, hz)])], "songs": [{"voices": [[(pitch, steps)]], "loops": [event index or None]}],
        "engine": [hz], "notes": [hz]}"""
    assert blob[:4] == b"EXBS" and blob[4] == FORMAT
    E, S, NN = blob[5], blob[6], blob[7]
    total, eng_off, note_off = struct.unpack_from("<HHH", blob, 8)
    assert total == len(blob)
    fx = []
    for i in range(E):
        o = struct.unpack_from("<H", blob, 14 + 2 * i)[0]
        pri, n = blob[o], blob[o + 1]
        fx.append((pri, [(blob[o + 2 + 3 * j], struct.unpack_from("<H", blob, o + 3 + 3 * j)[0])
                         for j in range(n)]))
    songs = []
    for i in range(S):
        rec = struct.unpack_from("<6H", blob, 14 + 2 * E + SONG_REC * i)
        voices, loops = [], []
        for v in range(NVOICES):
            o = rec[v]
            evs, idx = [], None
            while blob[o] != END:
                if rec[3 + v] == o:
                    idx = len(evs)
                evs.append((blob[o], blob[o + 1]))
                o += 2
            voices.append(evs)
            loops.append(idx)
        songs.append({"voices": voices, "loops": loops, "loop_off": rec[3:]})
    eng = list(struct.unpack_from("<64H", blob, eng_off))
    notes = list(struct.unpack_from("<%dH" % NN, blob, note_off))
    ktab = list(blob[note_off + 2 * NN:note_off + 2 * NN + KTAB_N])
    return {"effects": fx, "songs": songs, "engine": eng, "notes": notes, "ktab": ktab}


def selfcheck():
    a = compile_audio()
    b = compile_audio()
    assert a[0] == b[0] and a[1] == b[1], "audio compiler is not deterministic"
    eng = engine_table()
    assert eng == sorted(eng) and eng[0] == 175 and HZ_MIN <= eng[0] and eng[-1] <= HZ_MAX
    # the engine doubles every 24 steps
    assert abs(eng[24] - 2 * eng[0]) <= 1 and abs(eng[48] - 4 * eng[0]) <= 2
    nt = note_table()
    assert nt == sorted(nt) and nt[0] == 65                                  # C2 = 65 Hz
    assert nt[24] == note_hz(60) and abs(nt[33] - 440) <= 1                   # C4 = index 24, A4 = 33
    d = decode(a[0])
    assert d["ktab"] == speed_table() and d["ktab"] == sorted(d["ktab"]) and d["ktab"][0] == 0
    assert d["ktab"][-1] == 60 and len(d["ktab"]) == KTAB_N       # (the guest caps 8 + this at k = 63)
    songs = load_songs(SRC)
    assert len(songs) == len(d["songs"]) >= 5, "the five songs: title select results finish gameover"
    for sd, song in zip(d["songs"], songs):
        exp = steps_at(song["bars"] * 4, song["tempo"])
        for v, evs in enumerate(sd["voices"]):
            assert sum(s for _, s in evs) == exp, (song["name"], v)
            assert all(0 <= p <= NNOTES and 1 <= s <= 255 for p, s in evs)
        if song["loop"] is None:
            assert sd["loop_off"] == (0, 0, 0)
        else:
            lstep = steps_at(4 * song["loop"], song["tempo"])
            for v, evs in enumerate(sd["voices"]):
                i = sd["loops"][v]
                assert i is not None and sum(s for _, s in evs[:i]) == lstep, (song["name"], v)
    fx = parse_sfx(open(os.path.join(SRC, "sfx.txt")).read())
    assert len(fx) == 9 and fx[0][0] == "crash"
    assert d["effects"] == [(p, n) for _, p, n in fx]
    # rejection controls: a score that is wrong must be refused, not compiled
    for bad in ("tempo 120\nloop 0\nv1 o4 c4 d4 e4 f4\nv2 o4 c4 d4 e4\nv3 o4 c1\n",       # voice lengths differ
                "tempo 120\nloop 0\nv1 o4 c4 d4 e4 f4 | c8\nv2 o4 c1 c8\nv3 o4 c1 c8\n",  # not whole bars
                "tempo 120\nloop 3\nv1 o4 c1\nv2 o4 c1\nv3 o4 c1\n",                      # loop outside
                "tempo 120\nloop 0\nv1 o8 c1\nv2 o4 c1\nv3 o4 c1\n",                      # note out of range
                "tempo 120\nloop 0\nv1 o4 c2 & d2\nv2 o4 c1\nv3 o4 c1\n"):                # tie across pitches
        try:
            encode_song(parse_song(bad, "bad"), 0)
        except SystemExit:
            continue
        raise AssertionError("a bad score compiled: %r" % bad)
    for line in a[2]:
        print(line)
    print("excitebike_audio selfcheck: ok")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=SRC)
    ap.add_argument("-o", "--out")
    ap.add_argument("--selfcheck", action="store_true")
    a = ap.parse_args()
    if a.selfcheck:
        return selfcheck()
    snd, inc, rep = compile_audio(a.src)
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        open(os.path.join(a.out, "EXB.SND"), "wb").write(snd)
        open(os.path.join(a.out, "exbsnd.inc"), "w").write(inc)
    print("\n".join(rep))
    return 0


if __name__ == "__main__":
    sys.exit(main())
