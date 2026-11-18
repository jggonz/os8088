#!/usr/bin/env python3
"""SPEC.md 133.8 - the game kit's song and effects banks, read by something
that did not write them, and held to the names the C plays them by.

    python3 tests/unit/t_gkmus.py

tools/os88gkmus.py compiles Byteopoly's art/music.txt and art/sfx.txt into
the two records apps/cc/gkmus.inc plays, and carries its own model of the
player (`play`) - but that is the compiler's own code, so the failure it
cannot see is both halves agreeing on the same wrong thing. This file walks
the BYTES with a decoder written from SPEC.md 133.8 alone (it imports only
the two pinned entry points, compile_music and compile_sfx, for the bytes):

  * every offset lands inside the record; every stream ends (7F) and every
    pattern returns (7D); a call names a pattern, never from inside one;
  * every note index names an entry of the Hz table, every Hz is one
    OSAPI_SND_FM and OSAPI_SND_TONE take (19..6208), every duration >= 1;
  * the first note of a stream, of a loop and of a pattern, and the note
    after a return, carry their own duration (the player's "same duration"
    would otherwise inherit whatever came before);
  * a song's three voices last the same number of ticks from their starts
    AND from their loop points, so a loop is seamless by construction;
  * a patch's waveform is 0..3 and its C0h byte 0..0Fh (OPL2);
  * the two banks fit SPEC.md 133.9's 5 KB;
  * bp_app.c's BP_MUS_* / BP_FX_* name the songs and effects in file order,
    and bp.h's BP_SND_* are the first eight effects (a cue renamed or moved
    in one file and not the others plays the wrong sound, silently).

Break it on purpose: swap two `sfx` blocks in sfx.txt, or delete a note from
one voice of a song - the row goes red.
"""
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from os88gkmus import compile_music, compile_sfx   # noqa: E402 (the pinned pair, D1)

ART = os.path.join(ROOT, "apps", "byteopoly", "art")
APP = os.path.join(ROOT, "apps", "byteopoly", "bp_app.c")
BPH = os.path.join(ROOT, "apps", "byteopoly", "bp.h")
BUDGET = 5 * 1024
SONGREC = 18


def die(msg):
    sys.exit("t_gkmus: " + msg)


def walk(rec, start, hz_n, patterns, where):
    """One voice stream from `start`: its ticks, the tick of each offset
    reached (for the loop point). Calls are followed (one level)."""
    p, ticks, dur, at = start, 0, None, {}
    while True:
        if not 0 <= p < len(rec):
            die(f"{where}: runs off the record at {p}")
        at.setdefault(p, ticks)
        b = rec[p]
        p += 1
        op = b & 0x7F
        if op == 0x7F:
            return ticks, at
        if op == 0x7D:
            die(f"{where}: a return outside a pattern")
        if op == 0x7E:
            if p + 3 > len(rec):
                die(f"{where}: a call runs off the record")
            target, n = struct.unpack_from("<HB", rec, p)
            p += 3
            if n < 1:
                die(f"{where}: a call repeats 0 times")
            ticks += n * pattern(rec, target, hz_n, patterns, where)
            dur = None
            continue
        if op > hz_n:
            die(f"{where}: note {op}, the Hz table has {hz_n}")
        if b & 0x80:
            dur = rec[p]
            p += 1
            if dur < 1:
                die(f"{where}: a duration of 0 ticks")
        elif dur is None:
            die(f"{where}: a note at {p - 1} inherits a duration it cannot know")
        ticks += dur


def pattern(rec, start, hz_n, patterns, where):
    if start in patterns:
        return patterns[start]
    p, ticks, dur = start, 0, None
    while True:
        if not 0 <= p < len(rec):
            die(f"{where}: a pattern at {start} runs off the record")
        b = rec[p]
        p += 1
        op = b & 0x7F
        if op == 0x7D:
            if not ticks:
                die(f"{where}: an empty pattern at {start}")
            patterns[start] = ticks
            return ticks
        if op in (0x7E, 0x7F):
            die(f"{where}: a pattern at {start} calls or ends (one level, then 7D)")
        if op > hz_n:
            die(f"{where}: note {op} in a pattern, the Hz table has {hz_n}")
        if b & 0x80:
            dur = rec[p]
            p += 1
        elif dur is None:
            die(f"{where}: a pattern's note at {p - 1} inherits a duration")
        if not dur:
            die(f"{where}: a 0-tick note in a pattern")
        ticks += dur


def check_music(rec):
    if rec[:3] != b"GM\x01":
        die("music: not a version-1 song bank ('GM' 01)")
    n = rec[3]
    hz_off, patch_off = struct.unpack_from("<HH", rec, 4)
    if not (8 + SONGREC * n <= hz_off <= patch_off <= len(rec)) or (patch_off - hz_off) & 1:
        die(f"music: the Hz table {hz_off} / patches {patch_off} are out of order")
    hz_n = (patch_off - hz_off) // 2
    for i in range(hz_n):
        hz = struct.unpack_from("<H", rec, hz_off + 2 * i)[0]
        if not 19 <= hz <= 6208:
            die(f"music: Hz table entry {i + 1} is {hz}")
    if (len(rec) - patch_off) % 11:
        die("music: the patches are not 11 bytes each")
    npatch = (len(rec) - patch_off) // 11
    for i in range(npatch):
        pb = rec[patch_off + 11 * i: patch_off + 11 * i + 11]
        if pb[4] > 3 or pb[9] > 3 or pb[10] > 0x0F:
            die(f"music: patch {i} is not an OPL2 patch ({pb.hex()})")
    patterns, names = {}, []
    for s in range(n):
        lens, loops = [], []
        for v in range(3):
            start, loop, patch, pad = struct.unpack_from("<HHBB", rec, 8 + SONGREC * s + 6 * v)
            if not start:
                continue
            if patch >= npatch or pad:
                die(f"music: song {s} voice {v + 1}: patch {patch} of {npatch}")
            t, at = walk(rec, start, hz_n, patterns, f"song {s} voice {v + 1}")
            if loop not in at:
                die(f"song {s} voice {v + 1}: the loop offset {loop} is not an event")
            lens.append(t)
            loops.append(t - at[loop])
        if not lens:
            die(f"music: song {s} has no voice")
        if len(set(lens)) != 1 or len(set(loops)) != 1:
            die(f"music: song {s}: voices last {lens} ticks, loops {loops}: not seamless")
        names.append((lens[0], loops[0]))
    return names


def check_sfx(rec):
    if rec[:3] != b"GX\x01":
        die("sfx: not a version-1 effects bank ('GX' 01)")
    n = rec[3]
    for i in range(n):
        o = struct.unpack_from("<H", rec, 4 + 2 * i)[0]
        if not 4 + 2 * n <= o < len(rec):
            die(f"sfx: effect {i} at {o}, outside the record")
        if rec[o] & ~1:
            die(f"sfx: effect {i}: flags {rec[o]:#x} (bit 0 duck, nothing else)")
        o += 1
        steps = 0
        while True:
            if o >= len(rec):
                die(f"sfx: effect {i} runs off the record")
            t = rec[o]
            if not t:
                break
            hz = struct.unpack_from("<H", rec, o + 1)[0]
            if hz and not 19 <= hz <= 6208:
                die(f"sfx: effect {i} step {steps} is {hz} Hz")
            o += 3
            steps += 1
        if not steps:
            die(f"sfx: effect {i} is empty")
    return n


def blocks(path, kind):
    out = []
    for line in open(path):
        w = re.split(r"(?:^|\s)#", line, maxsplit=1)[0].split()
        if len(w) >= 2 and w[0] == kind:
            out.append(w[1])
    return out


def defines(path, prefix):
    return {m.group(1): int(m.group(2)) for m in
            re.finditer(r"#define (%s\w+)\s+(\d+)" % prefix, open(path).read())}


def main():
    import os88gkmus                        # its own fixtures too (--selfcheck)
    os88gkmus.selfcheck()
    mp, sp = os.path.join(ART, "music.txt"), os.path.join(ART, "sfx.txt")
    music, sfx = compile_music(mp, {}), compile_sfx(sp, {})
    songs = check_music(music)
    nfx = check_sfx(sfx)
    if len(music) + len(sfx) > BUDGET:
        die(f"the banks are {len(music)} + {len(sfx)} bytes, over {BUDGET}")
    snames, fnames = blocks(mp, "song"), blocks(sp, "sfx")
    if len(snames) != len(songs) or len(fnames) != nfx:
        die("the bank and its source disagree about how many records")
    mus, fx = defines(APP, "BP_MUS_"), defines(APP, "BP_FX_")
    want_m = {"BP_MUS_" + n.upper(): i for i, n in enumerate(snames)}
    if mus != want_m:
        die(f"bp_app.c BP_MUS_* {mus} != music.txt's order {want_m}")
    alias = {"TURN": "YOURTURN"}           # BP_FX_TURN plays `yourturn`
    got = {alias.get(k[6:], k[6:]).lower(): v for k, v in fx.items()}
    want_f = {n: i for i, n in enumerate(fnames)}
    if got != want_f:
        die(f"bp_app.c BP_FX_* {got} != sfx.txt's order {want_f}")
    snd = defines(BPH, "BP_SND_")
    cue = {"DICE": "dice", "BUY": "buy", "RENT": "rent", "CARD": "floppy", "CRASH": "crash",
           "BANKRUPT": "bankrupt", "WIN": "victory", "ERROR": "error"}
    for k, v in snd.items():
        if want_f.get(cue.get(k[7:], "?")) != v:
            die(f"bp.h {k} = {v}, but sfx.txt's effect {v} is {fnames[v] if v < nfx else '-'}")
    print(f"gkmus: {len(songs)} songs ({', '.join(f'{n} {t}t' for n, (t, _) in zip(snames, songs))}), "
          f"{nfx} effects, {len(music)} + {len(sfx)} of {BUDGET} bytes; names agree")


if __name__ == "__main__":
    main()
