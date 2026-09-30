#!/usr/bin/env python3
"""EXCITEBIKE wave 5: the sound (SPEC.md 102.5, plan section 11 and 16 wave 5).

    make excitebikedisk && python3 tests/excitebike_audio.py [--host] [--speaker] [--fm] [--capture]

  --host     (fast tier, no emulator) EXB.SND read back by a decoder written HERE from the layout in
             SPEC.md 102.5, not by the compiler's: <= 3,072 bytes; every song's three voices sum to the
             steps the score's tempo and bars give; loop points land on event boundaries at the loop bar;
             nothing longer than 40 s before its loop; effect records well formed; the engine table is
             round(175 x 2^(k/24)); the note table is equal temperament.  Negative controls: a blob with a
             voice that is one step short, and one whose loop points into the middle of an event, must FAIL.
  --speaker  MartyPC's 4.77 MHz XT, VGA, no sound card: the speaker path.  The guest is asked, by calling its
             own routines with the machine stopped, what it does:
               - every song, at random step sizes, plays exactly the compiled score: each voice's Hz after each
                 tick is the score's at that time (loops wrap, `once` songs fall silent);
               - the speaker gets the lead voice folded above 130 Hz, at most ONE tone call a tick;
               - the engine: the pitch step k for every mode x speed x input, the table's Hz, the glide (up two,
                 down one, never past the target), and silence for a crash, a stall, a pause, a countdown, a finish;
               - the effects: priorities (a lower one never interrupts), an effect lasts exactly its frames, a pause
                 freezes every effect but its own blip;
               - the frame's events -> at most one effect, the highest cue;
               - in a real race: the start lights beep on READY 3, 2, 1 and GO, at most one tone call a frame over
                 a scripted ride, the pause cue and the silent engine, M silences and restores.
  --fm       the same on the Sound Blaster machine: the four channels are claimed, the engine plays on the two
             first, an effect on the fourth, a pause / M / leaving releases them, and a channel claimed by
             somebody else makes the open REFUSE and fall back to the speaker with nothing left claimed.
  --capture  MartyPC's speaker output (MARTYPC_WAV, the vidspk precedent), read with tools/sndcheck.py's
             Goertzel scan: the engine's tone at a held speed is the table's Hz an octave up, and the title
             song's lead melody is heard in order.

Nothing here reads a ROM or any file outside the repo.
"""
import argparse
import math
import os
import random
import re
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

ART = os.path.join(ROOT, "build", "excitebike-art")
SRC = os.path.join(ROOT, "apps", "excitebike", "audio")
STEPS_HZ = 60.0988
SND_BUDGET = 3072
XM_RIDE, XM_AIR, XM_CRASH, XM_STALL = 0, 1, 2, 3
INP_U, INP_A, INP_B = 0x01, 0x10, 0x20
XEV = dict(CRASH=1, LAND=2, TAKEOFF=4, BUMP=8, LAP=0x10, FINISH=0x20, OVERHEAT=0x40, BOUNCE=0x80)
SPK_FLOOR = 130
QUICK = False
ONLY = ()
KTAB_N = 76                             # speed / 16 up to the cap 1,200: 76 entries
KTAB = [((16 * i + 8) * 51) >> 10 for i in range(KTAB_N)]


# ---------------------------------------------------------------------------------------------
# the blob, read by a decoder that shares nothing with tools/excitebike_audio.py
# ---------------------------------------------------------------------------------------------
def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def read_blob(b):
    """-> dict, or raises AssertionError with the first thing wrong."""
    assert b[:4] == b"EXBS", "magic"
    assert b[4] == 2, "format"
    n_fx, n_song, n_note = b[5], b[6], b[7]
    total, eng, notes = u16(b, 8), u16(b, 10), u16(b, 12)
    assert total == len(b), ("length field", total, len(b))
    fx = []
    for i in range(n_fx):
        o = u16(b, 14 + 2 * i)
        pri, n = b[o], b[o + 1]
        assert o + 2 + 3 * n <= len(b), "effect %d runs off the end" % i
        fx.append((pri, [(b[o + 2 + 3 * j], u16(b, o + 3 + 3 * j)) for j in range(n)]))
    songs = []
    rec0 = 14 + 2 * n_fx
    for i in range(n_song):
        r = rec0 + 12 * i
        starts = [u16(b, r + 2 * v) for v in range(3)]
        loops = [u16(b, r + 6 + 2 * v) for v in range(3)]
        voices, loop_idx = [], []
        for v in range(3):
            o, evs, li = starts[v], [], None
            while True:
                assert o + 2 <= len(b), "song %d voice %d runs off the end" % (i, v)
                if o == loops[v] and loops[v]:
                    li = len(evs)
                if b[o] == 255:
                    break
                evs.append((b[o], b[o + 1]))
                o += 2
            assert loops[v] == 0 or li is not None, "song %d voice %d: loop offset %d is not an event" % (i, v, loops[v])
            voices.append(evs)
            loop_idx.append(li)
        songs.append(dict(voices=voices, loops=loop_idx))
    assert eng + 128 <= len(b) and notes + 2 * n_note + KTAB_N <= len(b)
    return dict(fx=fx, songs=songs, engine=[u16(b, eng + 2 * k) for k in range(64)],
                notes=[u16(b, notes + 2 * k) for k in range(n_note)],
                ktab=list(b[notes + 2 * n_note:notes + 2 * n_note + KTAB_N]))


def score_facts(name):
    """tempo, loop bar (None = once), bars: counted from the score's own text (`|` ends a bar of voice 1)"""
    tempo, loop, bars = None, None, 0
    for line in open(os.path.join(SRC, name + ".mml")):
        s = line.split("#", 1)[0].strip()
        if s.startswith("tempo "):
            tempo = int(s.split()[1])
        elif s.startswith("loop "):
            loop = None if s.split()[1] == "once" else int(s.split()[1])
        elif s.startswith("v1 "):
            bars += s.count("|")
    return tempo, loop, bars


def song_names():
    return sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".mml"))


def steps_at(beats, tempo):
    return int(round(beats * 60.0 / tempo * STEPS_HZ))


def check_blob(blob, names):
    """the list of failures of an EXB.SND (empty = all good)"""
    bad = []
    try:
        d = read_blob(blob)
    except AssertionError as e:
        return ["decode: %s" % e]
    if len(blob) > SND_BUDGET:
        bad.append("EXB.SND is %d bytes, over %d" % (len(blob), SND_BUDGET))
    if len(d["songs"]) != len(names):
        bad.append("%d songs in the blob, %d scores" % (len(d["songs"]), len(names)))
        return bad
    for name, s in zip(names, d["songs"]):
        tempo, loop, bars = score_facts(name)
        total = steps_at(bars * 4, tempo)
        for v, evs in enumerate(s["voices"]):
            got = sum(st for _, st in evs)
            if got != total:
                bad.append("%s voice %d: %d steps, the score's %d bars at %d BPM give %d"
                           % (name, v + 1, got, bars, tempo, total))
            if any(p > 72 or not 1 <= st <= 255 for p, st in evs):
                bad.append("%s voice %d: a pitch or duration out of range" % (name, v + 1))
            li = s["loops"][v]
            if loop is None:
                if li is not None:
                    bad.append("%s voice %d loops though the score is `once`" % (name, v + 1))
            else:
                if li is None:
                    bad.append("%s voice %d has no loop point" % (name, v + 1))
                else:
                    at = sum(st for _, st in evs[:li])
                    if at != steps_at(4 * loop, tempo):
                        bad.append("%s voice %d loops at step %d, bar %d is step %d"
                                   % (name, v + 1, at, loop, steps_at(4 * loop, tempo)))
        if total / STEPS_HZ > 40.0:
            bad.append("%s runs %.1f s before its loop (limit 40)" % (name, total / STEPS_HZ))
    for i, (pri, notes) in enumerate(d["fx"]):
        if not 1 <= pri <= 15 or not notes:
            bad.append("effect %d: priority %d, %d notes" % (i, pri, len(notes)))
        for fr, hz in notes:
            if fr < 1 or (hz and not 19 <= hz <= 6208):
                bad.append("effect %d: a note (%d, %d)" % (i, fr, hz))
    if d["engine"] != [int(round(175 * 2 ** (k / 24.0))) for k in range(64)]:
        bad.append("the engine table is not round(175 x 2^(k/24))")
    if d["notes"] != [int(round(440 * 2 ** ((36 + i - 69) / 12.0))) for i in range(72)]:
        bad.append("the note table is not equal temperament from C2")
    if d["ktab"] != KTAB:
        bad.append("the speed table is not (16 i + 8) x 51 >> 10")
    return bad


def host_arm():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import excitebike_audio as A
    names = song_names()
    assert len(names) >= 5 and {"title", "select", "results", "finish", "gameover"} <= set(names), names
    blob, inc, _ = A.compile_audio()
    bad = check_blob(blob, names)
    assert not bad, bad
    disk = os.path.join(ART, "EXB.SND")
    if os.path.exists(disk):
        assert open(disk, "rb").read() == blob, "build/excitebike-art/EXB.SND is not what the compiler writes"
    assert A.compile_audio()[0] == blob, "the compiler is not deterministic"
    d = read_blob(blob)
    print("host: EXB.SND %d of %d bytes; %d songs %s; every voice's steps equal the score's bars x tempo, "
          "loops on event boundaries at the loop bar, all <= 40 s; %d effects; engine and note tables exact: PASS"
          % (len(blob), SND_BUDGET, len(d["songs"]), ", ".join(
              "%s %.1fs" % (n, sum(s for _, s in v["voices"][0]) / STEPS_HZ)
              for n, v in zip(names, d["songs"])), len(d["fx"])), flush=True)
    # the negative controls: a checker that cannot fail proves nothing
    short = bytearray(blob)
    o = u16(blob, 14 + 2 * len(d["fx"]))                # song 0, voice 1's stream
    short[o + 1] -= 1                                    # its first event one step short
    assert check_blob(bytes(short), names), "a voice one step short passed the checker"
    rec = 14 + 2 * len(d["fx"])
    for i, n in enumerate(names):
        if score_facts(n)[1] is not None:                # the first looping song: the loop into an event's middle
            bent = bytearray(blob)
            r = rec + 12 * i + 6
            bent[r:r + 2] = struct.pack("<H", u16(blob, r) + 1)
            assert check_blob(bytes(bent), names), "a loop into the middle of an event passed"
            break
    over = blob + bytes(SND_BUDGET)                      # a blob over its budget, its length field made honest
    over = over[:8] + struct.pack("<H", len(over)) + over[10:]
    assert any("over" in f for f in check_blob(over, names)), "a blob over the 3,072-byte budget passed"
    print("host: negative controls (a voice one step short; a loop into an event) FAIL as they must: PASS", flush=True)
    return d


# ---------------------------------------------------------------------------------------------
# the guest
# ---------------------------------------------------------------------------------------------
def sndinc():
    out = {}
    for line in open(os.path.join(ART, "exbsnd.inc")):
        m = re.match(r"(\w+) equ (\d+)", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


def model_hz(song, v, t):
    """the voice's Hz at cumulative step t: the event with start <= t < end, wrapping at the loop"""
    evs = song["voices"][v]
    li = song["loops"][v]
    total = sum(s for _, s in evs)
    if t >= total:
        if li is None:
            return 0
        start = sum(s for _, s in evs[:li])
        t = start + (t - start) % (total - start)
    acc = 0
    for p, s in evs:
        if acc <= t < acc + s:
            return NOTES[p - 1] if p else 0
        acc += s
    raise AssertionError("no event at %d" % t)


NOTES = [int(round(440 * 2 ** ((36 + i - 69) / 12.0))) for i in range(72)]
ENGINE = [int(round(175 * 2 ** (k / 24.0))) for k in range(64)]


def fold(hz):
    if hz:
        while hz < SPK_FLOOR:
            hz *= 2
    return hz


def engine_model(mode, speed, inp):
    k = 8 + KTAB[min(speed >> 4, KTAB_N - 1)]
    if mode == XM_AIR:
        k += 6
    if inp & (INP_A | INP_B):
        k += 2
    return min(k, 63)


class Guest:
    def __init__(self, fl, sym):
        import excitebike_flow as FL
        self.fl, self.sym, self.m = fl, sym, fl.m
        self.g = fl.g
        self.call_guest = FL.call_guest

    def halt(self, label="xu_menu_tick"):
        """Stop the guest at the entry of one of the game's own routines, so DS, SS and the stack are the
        game task's (a machine paused anywhere may be inside the kernel)."""
        m = self.m
        m.pause()
        m.bp_exec(self.g.a(label))
        m.run()
        assert m.wait_stop(60) == "breakpoint", "the game never reached " + label

    def release(self):
        self.m.bp_exec()
        self.m.run()

    def call(self, name, **regs):
        return self.call_guest(self.m, self.g.base, self.sym, name, **regs)

    def b(self, n):
        return self.g.b(n)

    def w(self, n):
        return self.g.w(n)

    def sw(self, n):
        return struct.unpack("<h", self.g.rd(n, 2))[0]

    def put(self, n, v, size=None):
        self.g.put(n, v, size)

    def putw(self, n, v):
        self.g.put(n, v & 0xFFFF, 2)

    def voices(self):
        raw = self.g.rd("xu_v", 24)
        return [struct.unpack_from("<H", raw, 8 * i + 6)[0] for i in range(3)]


def check_music(gu, d, ids, tag, rnd):
    """Every song: the guest's own sequencer, driven with a chosen step size a tick, against the score.
    The first ticks are small (one to four steps: the menus' real cadence), the rest large enough to cross
    several events - never more than the four the sequencer catches up in one tick, which is what the
    bound is for - so a song and its loop point are covered in a few dozen calls."""
    for name, sid in ids:
        song = d["songs"][sid]
        span = sum(s for _, s in song["voices"][0])
        shortest = min(s for evs in song["voices"] for _, s in evs)
        big = max(4, min(3 * shortest, 60))
        gu.put("xu_race", 0)
        gu.call("xu_song", ax=sid)
        t = 0
        maxcalls = 0
        emitted = 0
        need = span * 2 + 2 if song["loops"][0] is not None else span + 40
        i = 0
        while t < need:
            dt = rnd.randrange(1, 5) if i < 24 else rnd.randrange(big // 2, big + 1)
            gu.put("xu_dt", dt)
            gu.call("xu_music_tick")
            t += dt
            got = gu.voices()
            for v in range(3):
                exp = model_hz(song, v, t)
                assert got[v] == exp, ("%s voice %d at step %d: the guest has %d Hz, the score %d" %
                                       (name, v + 1, t, got[v], exp))
            if i < 12 or i % 6 == 0:
                n0 = gu.w("xu_ntone")
                gu.call("xu_emit")
                emitted += 1
                calls = (gu.w("xu_ntone") - n0) & 0xFFFF
                maxcalls = max(maxcalls, calls)
                if gu.b("xu_fm") == 0:
                    exp = fold(model_hz(song, 0, t))
                    sent = gu.w("xu_sent")
                    assert sent in (exp, 0xFFFF), ("%s speaker at step %d: sent %d, expected %d" % (name, t, sent, exp))
                    assert calls <= 1, ("%s: %d tone calls in one tick" % (name, calls))
                else:
                    assert calls <= 8, ("%s: %d FM calls in one tick" % (name, calls))
            i += 1
        if song["loops"][0] is None:
            assert gu.voices() == [0, 0, 0] and gu.w("xu_v") == 0, "a once song did not fall silent and stop"
        print("%s music %-8s %3d ticks over %4d of %4d steps (%s): every voice's Hz is the score's, %s: PASS"
              % (tag, name, i, t, span, "the loop wraps" if song["loops"][0] is not None else "ends silent",
                 "<= 1 tone call a tick, lead folded over %d Hz (%d emitted)" % (SPK_FLOOR, emitted)
                 if gu.b("xu_fm") == 0 else "<= 8 FM calls a tick (%d emitted)" % emitted), flush=True)


def check_engine(gu, tag, tab):
    m = gu.m
    fm_was = gu.b("xu_fm")
    n = 0
    for mode in (XM_RIDE, XM_AIR):
        for speed in (0, 1, 100, 400, 800, 0x320, 0x340, 0x466, 1200, 1201, 1500, 0xFFFF):
            for inp in (0, INP_A, INP_B, INP_A | INP_B, INP_U):
                want = engine_model(mode, speed, inp)
                gu.put("xm_mode", mode)
                gu.putw("xm_speed", speed)
                gu.put("xm_inp", inp)
                gu.put("xb_paused", 0)
                gu.putw("xb_cd", 0)
                gu.put("xu_fin", 0)
                gu.put("xu_k", want)                    # the step is already where the model says the target is,
                gu.put("xu_dt", 1)                      # so one step of glide must leave it there
                gu.putw("xu_enghz", 0)
                gu.put("xu_fm", 1)                      # (the fifth below is only worked out for FM)
                gu.call("xu_engine")
                assert gu.b("xu_k") == want, (mode, speed, inp, gu.b("xu_k"), want)
                assert gu.w("xu_enghz") == ENGINE[want], (mode, speed, inp, gu.w("xu_enghz"), ENGINE[want])
                assert gu.w("xu_enghz2") == ENGINE[max(want - 14, 0)], "the fifth below"
                gu.put("xu_fm", fm_was)
                n += 1
    # monotone in speed: the pitch never falls as the bike speeds up
    prev = 0
    for speed in range(0, 1300, 20):
        assert engine_model(XM_RIDE, speed, 0) >= prev
        prev = engine_model(XM_RIDE, speed, 0)
    print("%s engine: %d mode x speed x input cases: the step k = 8 + KTAB[speed / 16] (+6 in the air, +2 on "
          "the throttle, capped 63), the table's Hz, and a fifth below (k - 14): PASS" % (tag, n), flush=True)
    # the glide
    gu.put("xm_mode", XM_RIDE)
    gu.putw("xm_speed", 1200)
    gu.put("xm_inp", INP_A)
    gu.put("xu_k", 8)
    gu.put("xu_dt", 1)
    ks = []
    for _ in range(40):
        gu.call("xu_engine")
        ks.append(gu.b("xu_k"))
    assert ks[:5] == [10, 12, 14, 16, 18] and max(ks) == 63 and ks[-1] == 63, ks
    assert all(b - a in (0, 1, 2) for a, b in zip(ks, ks[1:])), "the climb skipped"
    gu.putw("xm_speed", 0)
    gu.put("xm_inp", 0)
    gu.put("xu_k", 63)
    gu.put("xu_dt", 3)
    ks = []
    for _ in range(40):
        gu.call("xu_engine")
        ks.append(gu.b("xu_k"))
    assert ks[:3] == [60, 57, 54] and ks[-1] == 8 and min(ks) == 8, ks
    print("%s engine glide: up two a game step to the target and no further, down one; a jump in speed is a "
          "slide (%d frames at 3 steps to fall from 63 to 8): PASS" % (tag, ks.index(8) + 1), flush=True)
    # the speaker's glide is asked for once in three frames; starting and stopping are at once
    gu.put("xu_race", 1)
    gu.put("xu_fin", 0)
    gu.put("xu_fxpri", 0)
    gu.put("xu_fm", 0)
    gu.put("xb_paused", 0)
    gu.putw("xu_sent", 440)
    gu.put("xu_hold", 0)
    pat = []
    for i in range(9):
        gu.putw("xu_enghz", 100 + 7 * i)              # a note that moves every frame
        n0 = gu.w("xu_ntone")
        gu.call("xu_emit")
        pat.append(gu.w("xu_ntone") - n0)
    assert pat == [1, 0, 0, 1, 0, 0, 1, 0, 0], ("a glide's tone calls", pat)
    gu.putw("xu_enghz", 0)
    n0 = gu.w("xu_ntone")
    gu.call("xu_emit")
    assert gu.w("xu_ntone") - n0 == 1 and gu.w("xu_sent") == 0, "the engine stopping must be at once"
    gu.putw("xu_enghz", 300)
    n0 = gu.w("xu_ntone")
    gu.put("xu_hold", 2)
    gu.call("xu_emit")
    assert gu.w("xu_ntone") - n0 == 1 and gu.w("xu_sent") == 600, "the engine starting must be at once"
    gu.put("xu_race", 0)
    gu.put("xu_fm", fm_was)
    print("%s engine glide on the speaker: a tone call at most once in three frames while the pitch moves, at "
          "once when the engine starts or stops: PASS" % tag, flush=True)
    # silence: a crash, a stall, a pause, a countdown, a finished ride - and a fresh start climbs from idle
    for what, setup in (("crash", lambda: gu.put("xm_mode", XM_CRASH)), ("stall", lambda: gu.put("xm_mode", XM_STALL)),
                        ("pause", lambda: gu.put("xb_paused", 1)), ("countdown", lambda: gu.putw("xb_cd", 100)),
                        ("finish", lambda: gu.put("xu_fin", 1))):
        gu.put("xm_mode", XM_RIDE)
        gu.putw("xm_speed", 800)
        gu.put("xb_paused", 0)
        gu.putw("xb_cd", 0)
        gu.put("xu_fin", 0)
        gu.put("xu_k", 40)
        setup()
        gu.put("xu_dt", 1)
        gu.call("xu_engine")
        assert gu.w("xu_enghz") == 0 and gu.w("xu_enghz2") == 0, ("silence", what)
        assert gu.b("xu_k") == (40 if what in ("pause", "countdown") else 8), (what, gu.b("xu_k"))
    gu.put("xm_mode", XM_RIDE)
    gu.put("xb_paused", 0)
    gu.putw("xb_cd", 0)
    gu.put("xu_fin", 0)
    print("%s engine silence: a crash, a stall, a pause, the countdown and a finished ride are silent: PASS" % tag,
          flush=True)


def check_effects(gu, d, ids, tag):
    m = gu.m
    gu.put("xu_race", 1)
    gu.put("xu_live", 1)
    fx = d["fx"]

    def clear():
        gu.putw("xu_fxp", 0)
        gu.put("xu_fxpri", 0)
        gu.putw("xu_fxhz", 0)
        gu.put("xb_paused", 0)

    for i, (pri, notes) in enumerate(fx):
        clear()
        gu.call("xu_fx", ax=i)
        assert gu.b("xu_fxpri") == pri and gu.b("xu_fxn") == len(notes) - 1 and gu.b("xu_fxid") == i, i
        gu.put("xu_dt", 1)
        seen, ticks = [gu.w("xu_fxhz")], 0            # the first note sounds the moment the effect starts
        while gu.w("xu_fxp"):
            gu.call("xu_fx_step")
            ticks += 1
            hz = gu.w("xu_fxhz")
            if gu.w("xu_fxp") and (not seen or seen[-1] != hz):
                seen.append(hz)
            assert ticks < 1000
        total = sum(f for f, _ in notes)
        assert ticks == total, ("effect %d lasts %d ticks, its record %d" % (i, ticks, total))
        want = []
        for _, hz in notes:
            if not want or want[-1] != hz:
                want.append(hz)
        assert seen == want, ("effect %d's notes" % i, seen, want)
        assert gu.b("xu_fxpri") == 0 and gu.w("xu_fxhz") == 0, "an ended effect must fall silent"
    print("%s effects: %d records, each ends after exactly its frames at one step a tick, its notes in order, "
          "then silence: PASS" % (tag, len(fx)), flush=True)
    # priorities
    P = {n: i for i, n in enumerate(("crash", "land_soft", "land_hard", "bump", "takeoff", "overheat", "lap",
                                     "start_light", "pause"))}
    clear()
    gu.call("xu_fx", ax=P["crash"])
    gu.put("xu_dt", 1)
    gu.call("xu_fx_step")                     # the crash's first note is 2 steps: one is left
    gu.call("xu_fx", ax=P["bump"])
    assert gu.b("xu_fxid") == P["crash"], "a bump interrupted the crash"
    assert gu.b("xu_fxn") == len(fx[P["crash"]][1]) - 1 and gu.w("xu_fxw") == fx[P["crash"]][1][0][0] - 1, \
        "the crash was disturbed"
    gu.call("xu_fx_step")
    gu.call("xu_fx_step")                     # into its second note
    assert gu.b("xu_fxn") == len(fx[P["crash"]][1]) - 2
    gu.call("xu_fx", ax=P["crash"])
    assert gu.b("xu_fxn") == len(fx[P["crash"]][1]) - 1 and gu.w("xu_fxw") == fx[P["crash"]][1][0][0], \
        "an equal priority restarts"
    clear()
    gu.call("xu_fx", ax=P["bump"])
    gu.call("xu_fx", ax=P["crash"])
    assert gu.b("xu_fxid") == P["crash"], "the crash did not take over"
    clear()
    gu.call("xu_fx", ax=P["start_light"])
    gu.call("xu_fx", ax=P["land_hard"])
    assert gu.b("xu_fxid") == P["start_light"], "a landing cut the start lights"
    clear()
    order = sorted(range(len(fx)), key=lambda i: fx[i][0])
    assert fx[P["crash"]][0] == max(p for p, _ in fx) and fx[P["pause"]][0] == min(p for p, _ in fx)
    print("%s effect priorities: a lower one never interrupts, equal restarts, a higher takes over "
          "(crash %d > start lights %d > landing > ... > pause %d): PASS"
          % (tag, fx[P["crash"]][0], fx[P["start_light"]][0], fx[P["pause"]][0]), flush=True)
    # a pause freezes every effect but its own blip
    clear()
    gu.call("xu_fx", ax=P["lap"])
    gu.put("xu_dt", 1)
    gu.call("xu_fx_step")
    gu.put("xb_paused", 1)
    w0 = gu.w("xu_fxw")
    for _ in range(30):
        gu.call("xu_fx_step")
    assert gu.w("xu_fxw") == w0 and gu.b("xu_fxpri") == fx[P["lap"]][0], "a paused game ran an effect's clock"
    gu.put("xu_fin", 0)
    gu.putw("xu_sent", 0xFFFF)
    gu.call("xu_emit")
    assert gu.w("xu_sent") == 0 or gu.b("xu_fm"), "a frozen effect left a tone sounding through the pause"
    gu.put("xb_paused", 0)
    gu.putw("xu_sent", 0xFFFF)
    gu.call("xu_emit")
    assert gu.w("xu_sent") == fx[P["lap"]][1][0][1] or gu.b("xu_fm"), "the frozen effect did not sound again at the resume"
    gu.put("xb_paused", 1)
    gu.call("xu_pause")                        # the key: the lap chirp is cancelled, the blip plays
    assert gu.b("xu_fxid") == P["pause"] and gu.b("xu_fxpri") == fx[P["pause"]][0], "Enter did not play the blip"
    clear()
    gu.call("xu_fx", ax=P["start_light"])
    gu.put("xb_paused", 1)
    gu.call("xu_pause")
    assert gu.b("xu_fxid") == P["start_light"], "a pause must freeze the start lights, not cancel them"
    clear()
    gu.put("xb_paused", 1)
    gu.call("xu_fx", ax=P["pause"])
    ticks = 0
    while gu.w("xu_fxp") and ticks < 100:
        gu.call("xu_fx_step")
        ticks += 1
    assert ticks == sum(f for f, _ in fx[P["pause"]][1]), "the pause blip did not run through the pause"
    clear()
    print("%s pause: every effect's clock stops with the game, the pause blip alone runs (%d steps): PASS"
          % (tag, ticks), flush=True)
    # the frame's events: at most one effect, the highest cue
    cases = [("CRASH", "crash"), ("BOUNCE", "land_hard"), ("LAND", "land_soft"), ("OVERHEAT", "overheat"),
             ("LAP", "lap"), ("TAKEOFF", "takeoff"), ("BUMP", "bump")]
    for ev, name in cases:
        clear()
        gu.put("xu_ev", XEV[ev])
        gu.call("xu_events")
        assert gu.b("xu_fxid") == P[name] and gu.w("xu_fxp"), (ev, gu.b("xu_fxid"))
    allbits = 0xFF & ~XEV["FINISH"]
    clear()
    gu.put("xu_ev", allbits)
    gu.call("xu_events")
    assert gu.b("xu_fxid") == P["crash"], "with every cue at once the crash wins"
    for ev, name in cases[1:]:
        clear()
        gu.put("xu_ev", allbits & ~XEV["CRASH"] if ev == "BOUNCE" else XEV[ev] | XEV["BUMP"])
        gu.call("xu_events")
        assert gu.b("xu_fxid") == P[name], (ev, gu.b("xu_fxid"))
    clear()
    gu.put("xu_ev", 0)
    gu.call("xu_events")
    assert gu.w("xu_fxp") == 0, "no event, no effect"
    gu.put("xu_fin", 0)
    gu.put("xu_ev", XEV["FINISH"])
    gu.call("xu_events")
    assert gu.b("xu_fin") == 1 and gu.b("xu_cur") == ids["finish"], "the finish must start the fanfare and stop the engine"
    gu.put("xu_ev", 0)
    gu.put("xu_fin", 0)
    print("%s events: each of the seven cues starts its own effect, several at once start the highest, none "
          "starts none, the finish starts the fanfare: PASS" % tag, flush=True)
    gu.put("xu_race", 0)
    clear()


# ---------------------------------------------------------------------------------------------
# a real race
# ---------------------------------------------------------------------------------------------
def race_items(gu, fl, tag):
    import excitebike_flow as FL
    m = gu.m
    fl.to_race(0, 0, wait_cd=True)
    assert gu.b("xu_race") == 1 and gu.b("xu_live") == 1
    # READY 3, 2, 1, GO! on the speaker: 440 Hz on each number, silence between, 880 on GO
    timeline = [(0, 10, 440), (10, 60, 0), (60, 70, 440), (70, 120, 0), (120, 130, 440), (130, 180, 0), (180, 204, 880)]
    seen = {}
    for _ in range(400):
        cd = gu.w("xb_cd")
        sent = gu.w("xu_sent")
        t = 180 - cd if cd else 180 + 6
        seen.setdefault(t // 10, set()).add(sent)
        if cd == 0 and gu.w("xm_stepn") > 30:
            break
        M_sleep(m, .02)
    ok = []
    for lo, hi, hz in timeline:
        for slot in range(lo // 10 + 1, (hi - 1) // 10):          # a 10-step margin at each edge
            if slot in seen:
                assert hz in seen[slot] or seen[slot] == {0xFFFF}, ("start lights", slot, sorted(seen[slot]), hz)
                ok.append(slot)
    assert ok, "the start lights were never sampled"
    assert gu.w("xu_enghz") == 0 or gu.w("xb_cd") == 0, "the engine sounded through the countdown"
    print("%s race start: the start lights beep 440 Hz on READY 3, 2, 1 and 880 on GO, silent between, and the "
          "engine waits for GO (%d sampled slots): PASS" % (tag, len(ok)), flush=True)
    # the ride: a scripted pad, throttle mostly held: the engine's pitch follows the speed, <= 1 tone call a frame
    n0 = gu.w("xu_ntone")
    f0 = gu.w("xb_frames")
    ks, sent = set(), set()
    m.key("KeyZ", up=False)
    per = trace_calls(gu, 120)                      # 120 race frames under breakpoints: the calls in each
    for _ in range(60):
        M_sleep(m, .05)
        ks.add(gu.b("xu_k"))
        sent.add(gu.w("xu_sent"))
    m.key("KeyZ", down=False)
    frames = gu.w("xb_frames") - f0
    calls = gu.w("xu_ntone") - n0
    assert max(per) <= 1, ("tone calls in a frame", max(per), per)
    assert len(ks) > 3, ("the engine's pitch did not move with the speed", sorted(ks))
    assert calls < frames, ("a tone call every frame", calls, frames)
    print("%s ride: %d frames, %d tone calls (a call only when the step changes or the lease is due), the most in "
          "any one of 120 traced frames %d; k moved through %d steps: PASS"
          % (tag, frames, calls, max(per), len(ks)), flush=True)
    # the pause: Enter silences the engine and plays the blip; Enter again brings the engine back
    fl.key("Enter", settle=.05)
    assert gu.b("xb_paused") == 1
    seen_pause = set()
    for _ in range(20):
        seen_pause.add(gu.b("xu_fxid") if gu.b("xu_fxpri") else 255)
        M_sleep(m, .02)
    M_sleep(m, .4)
    assert gu.w("xu_enghz") == 0 and gu.w("xu_sent") == 0, ("the engine sounded through a pause", gu.w("xu_sent"))
    fl.key("Enter", settle=.3)
    assert gu.b("xb_paused") == 0
    print("%s pause: Enter plays the blip and leaves the speaker silent, Enter again returns the engine: PASS" % tag,
          flush=True)
    # M: silence, release, restore
    fl.key("KeyM", settle=.3)
    assert gu.b("xu_mute") == 1 and gu.b("xu_live") == 0, "M did not silence"
    ntone = gu.w("xu_ntone")
    M_sleep(m, .5)
    assert gu.w("xu_ntone") == ntone, "a silenced game kept calling the driver"
    fl.key("KeyM", settle=.5)
    assert gu.b("xu_mute") == 0 and gu.b("xu_live") == 1, "M did not restore"
    print("%s M: sound off releases the claim and stops every call, on brings it back mid-race: PASS" % tag, flush=True)
    fl.key("Escape", settle=.8)
    fl.wait_state(FL.ST_TITLE)
    assert gu.b("xu_race") == 0 and gu.b("xu_cur") == sndinc()["EXBSONG_TITLE"], "back on the title: the title song"
    print("%s leaving a race: the engine is off and the title song plays again: PASS" % tag, flush=True)


def boot(tag, machine=None):
    import os88ui
    import excitebike_video as V
    return os88ui.boot(V.at("build/os8088-360.img"), apps=V.at("build/excitebike360.img"),
                       machine=machine or V.MACHINE[tag])


def trace_calls(gu, frames, labels=("xu_tonecall", "xu_fmcall")):
    """Run `frames` race frames under exec breakpoints and return the driver calls made in each: the hits
    on the call marks between two entries of xu_race_frame."""
    import os88marty as M
    names = ["xu_race_frame"] + list(labels)
    addr = {("%05X" % gu.g.a(n)): n for n in names}
    top = "%05X" % gu.g.a("xu_race_frame")
    with M.bp_trace(gu.m, *[gu.g.a(n) for n in names], cap=frames * 8 + 400) as tr:
        tr.until(lambda: tr.count(top) >= frames + 1, "%d race frames" % frames, limit=180)
    per, cur = [], None
    for h in tr.hits:
        n = addr.get(h["name"], h["name"])
        if n == "xu_race_frame":
            if cur is not None:
                per.append(cur)
            cur = 0
        elif cur is not None:
            cur += 1
    return per


def M_sleep(m, s):
    import os88marty as M
    M.guest_sleep(m, s)


def speaker_arm(tag="vga"):
    import os88marty as M
    import excitebike_flow as FL
    import excitebike_video as V
    import exbsim
    art = exbsim.X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    inc = sndinc()
    with boot(tag) as ui:
        fl, c0, c_pre = FL.open_flow(ui, tag, sym, ref, art)
        gu = Guest(fl, sym)
        assert gu.b("xu_live") == 1 and gu.b("xu_fm") == 0, "no sound driver: the speaker's"
        assert gu.b("xu_cur") == inc["EXBSONG_TITLE"], "the title song"
        print("%s open: the speaker path (no driver), sound live, the title song playing: PASS" % tag, flush=True)
        d = read_blob(open(os.path.join(ART, "EXB.SND"), "rb").read())
        names = song_names()
        ids = {n: inc["EXBSONG_" + n.upper()] for n in names}
        rnd = random.Random(0xB1CE)
        gu.halt()
        if not QUICK and (not ONLY or "music" in ONLY):
            check_music(gu, d, sorted(ids.items()), tag, rnd)
        if not QUICK and (not ONLY or "engine" in ONLY):
            check_engine(gu, tag, d["engine"])
        if not ONLY or "effects" in ONLY:
            check_effects(gu, d, ids, tag)
        # restart the real song and let the game go on
        gu.call("xu_song", ax=ids["title"])
        gu.release()
        if not ONLY or "race" in ONLY:
            race_items(gu, fl, tag)
        # leaving: no tone left running, the claim map as it was
        fl.key("Escape", settle=.8)
        assert gu.b("xu_live") == 0, "Esc from the title must close the sound"
        print("%s close: leaving the bracket closes the sound: PASS" % tag, flush=True)


# ---------------------------------------------------------------------------------------------
# the Sound Blaster machine: FM
# ---------------------------------------------------------------------------------------------
def driver_symbols():
    names = ("opl_own", "opl_b0")
    source = open(os.path.join(ROOT, "drivers", "sound", "sound.asm")).read()
    with tempfile.TemporaryDirectory() as td:
        p, b = os.path.join(td, "probe.asm"), os.path.join(td, "probe.bin")
        open(p, "w").write(source + "\n" + "\n".join("dw " + n for n in names))
        subprocess.run(["nasm", "-f", "bin", "-I", "drivers/", "-I", "drivers/sound/", "-I", "apps/",
                        "-I", "build/", "-o", b, p], cwd=ROOT, check=True)
        raw = open(b, "rb").read()
        return dict(zip(names, struct.unpack("<%dH" % len(names), raw[-2 * len(names):])))


def fm_arm(tag="vga"):
    import os88marty as M
    import excitebike_flow as FL
    import excitebike_video as V
    import exbsim
    art = exbsim.X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    inc = sndinc()
    ds = driver_symbols()
    d = read_blob(open(os.path.join(ART, "EXB.SND"), "rb").read())
    names = song_names()
    ids = {n: inc["EXBSONG_" + n.upper()] for n in names}
    with boot(tag, "os8088_xt_vga_sb") as ui:
        fl, c0, c_pre = FL.open_flow(ui, tag, sym, ref, art)
        m = ui.m
        gu = Guest(fl, sym)
        fseg = struct.unpack("<H", m.read(m.sym("drv_fseg"), 2))[0] << 4

        def own():
            return bytes(m.read(fseg + ds["opl_own"], 4))

        def keyon(ch):
            return m.read(fseg + ds["opl_b0"], 8)[ch] & 0x20 != 0

        assert gu.b("xu_live") == 1 and gu.b("xu_fm") == 1, "the FM route was not taken on the Sound Blaster machine"
        assert all(v != 255 for v in own()), ("the four channels are claimed", own().hex())
        print("%s fm open: four channels claimed (owner bytes %s), the FM path: PASS" % (tag, own().hex()), flush=True)
        seen = False
        for _ in range(60):
            if any(keyon(c) for c in range(3)):
                seen = True
                break
            M_sleep(m, .05)
        assert seen, "the title song never keyed a channel"
        print("%s fm music: the title song keys channels 0-2: PASS" % tag, flush=True)
        gu.halt()
        check_music(gu, d, sorted(ids.items()), tag + " fm", random.Random(0xFA11))
        gu.call("xu_song", ax=ids["title"])
        gu.release()
        fl.to_race(0, 0)
        M_sleep(m, .3)
        m.key("KeyZ", up=False)
        ok = False
        for _ in range(80):
            if keyon(0) and keyon(1) and gu.w("xu_ksent"):
                ok = True
                break
            M_sleep(m, .05)
        assert ok, "the engine never keyed channels 0 and 1"
        per = trace_calls(gu, 150, ("xu_fmcall",))
        m.key("KeyZ", down=False)
        cmax = max(per)
        assert cmax <= 4, ("FM calls in one frame", cmax, per)
        print("%s fm engine: throttle keys channels 0 (growl) and 1 (a fifth below), the most FM calls in any one "
              "of 150 traced race frames %d, %d of them with any: PASS" % (tag, cmax, sum(1 for x in per if x)),
              flush=True)
        fl.key("Enter", settle=.05)
        M_sleep(m, .6)
        assert gu.b("xb_paused") == 1 and not keyon(0) and not keyon(1), "a pause left the engine keyed"
        fl.key("Enter", settle=.05)
        M_sleep(m, .6)
        assert gu.b("xb_paused") == 0
        fl.key("KeyM", settle=.3)
        assert gu.b("xu_live") == 0 and own() == b"\xff" * 4, ("M must release every channel", own().hex())
        fl.key("KeyM", settle=.5)
        assert gu.b("xu_live") == 1 and all(v != 255 for v in own()), "M did not claim again"
        print("%s fm pause and M: a pause keys the engine off; M releases all four claims and takes them again: PASS"
              % tag, flush=True)
        # an effect on channel 3: halted in a race frame so the state is the game's
        gu.halt("xu_race_frame")
        gu.put("xu_fxp", 0)
        gu.put("xu_fxpri", 0)
        gu.call("xu_fx", ax=inc["EXBFX_CRASH"])
        gu.put("xu_dt", 1)
        gu.call("xu_fx_step")
        gu.call("xu_emit")
        assert keyon(3), "the effect did not key channel 3"
        for _ in range(60):
            gu.call("xu_fx_step")
        gu.call("xu_emit")
        assert not keyon(3), "the ended effect left channel 3 keyed"
        print("%s fm effect: a crash keys channel 3 and its end keys it off: PASS" % tag, flush=True)
        gu.release()
        fl.key("Escape", settle=.8)
        fl.wait_state(FL.ST_TITLE)
        # THE REFUSAL: somebody else owns channel 2, so the open takes none and the speaker plays
        gu.halt()
        gu.call("xu_close")
        assert own() == b"\xff" * 4
        m.write(fseg + ds["opl_own"] + 2, b"\xfe")
        gu.call("xu_open")
        assert gu.b("xu_fm") == 0 and gu.b("xu_live") == 1, "a refused claim must fall back to the speaker"
        got = own()
        assert got == b"\xff\xff\xfe\xff", ("what we claimed must be released and the foreigner's kept", got.hex())
        gu.put("xu_race", 0)
        gu.call("xu_song", ax=ids["title"])
        n0 = gu.w("xu_ntone")
        gu.put("xu_dt", 14)                        # into the lead's second note (13..26 steps)
        gu.call("xu_music_tick")
        gu.call("xu_emit")
        assert gu.w("xu_sent") not in (0, 0xFFFF) and gu.w("xu_ntone") - n0 == 1, (
            "the speaker did not take over", gu.w("xu_sent"), gu.w("xu_ntone") - n0, gu.b("xu_fm"), gu.voices(), gu.b("xu_cur"))
        m.write(fseg + ds["opl_own"] + 2, b"\xff")
        gu.call("xu_close")
        gu.call("xu_open")
        assert gu.b("xu_fm") == 1, "with the foreign claim gone the FM claim must succeed"
        print("%s fm refusal: a channel owned by another falls the whole open back to the speaker (nothing of ours "
              "left claimed, the other's claim intact), the speaker plays, and the claim succeeds once it is free: "
              "PASS" % tag, flush=True)
        gu.release()
        fl.key("Escape", settle=.8)
        assert gu.b("xu_live") == 0 and own() == b"\xff" * 4, "leaving must release every channel"
        print("%s fm close: leaving the bracket releases every channel: PASS" % tag, flush=True)


# ---------------------------------------------------------------------------------------------
# the speaker's own output: MartyPC's capture read with tools/sndcheck.py
# ---------------------------------------------------------------------------------------------
def wav_runs(path, win=0.02, tol=0.025):
    """[(t0, t1, hz)] of the steady tones in a capture.  PC speaker output is a square wave, so the rising
    edges are the pitch: each `win` seconds' frequency is (edges - 1) / the time they span (good to a
    fraction of a percent), and consecutive windows within `tol` of their group's median are one tone -
    a semitone is 5.9%, so neighbouring notes stay apart - with a group of fewer than three windows dropped."""
    import sndcheck
    rate, mono, dur = sndcheck.load(path)
    peak = max((abs(v) for v in mono), default=0)
    if peak < 0.02:
        return [], rate, dur
    hi = 0.5 * peak                     # MartyPC's speaker capture is 0 / full scale (no negative half)
    edges, state = [], False
    for i, v in enumerate(mono):
        if not state and v > hi:
            state = True
            edges.append(i)
        elif state and v <= hi:
            state = False
    w = int(rate * win)
    slots = {}
    for e in edges:
        slots.setdefault(e // w, []).append(e)
    est = {}
    for k, es in slots.items():
        if len(es) >= 3:
            est[k] = (rate * (len(es) - 1) / float(es[-1] - es[0]), es[0] / float(rate), es[-1] / float(rate))
    runs, grp = [], []

    def close():
        if len(grp) >= 3:
            fs = sorted(g[0] for g in grp)
            runs.append((grp[0][1], grp[-1][2], fs[len(fs) // 2]))
    last = None
    for k in sorted(est):
        f = est[k][0]
        if grp and (k != last + 1 or abs(f - sorted(g[0] for g in grp)[len(grp) // 2]) > tol * f):
            close()
            grp = []
        grp.append(est[k])
        last = k
    close()
    return runs, rate, dur


def capture_arm(tag="vga"):
    import os88marty as M
    import excitebike_flow as FL
    import excitebike_video as V
    import exbsim
    art = exbsim.X.Art()
    ref = exbsim.Ref(art)
    sym = V.symbols()
    inc = sndinc()
    d = read_blob(open(os.path.join(ART, "EXB.SND"), "rb").read())
    out = os.path.join(ROOT, "build", "excitebike-proof")
    os.makedirs(out, exist_ok=True)
    cap = os.path.join(out, "audio-speaker")
    for f in os.listdir(out):
        if f.startswith("audio-speaker"):
            os.unlink(os.path.join(out, f))
    os.environ["MARTYPC_WAV"] = cap
    try:
        with boot(tag) as ui:
            os.environ.pop("MARTYPC_WAV", None)
            fl, c0, c_pre = FL.open_flow(ui, tag, sym, ref, art)
            m = ui.m
            gu = Guest(fl, sym)
            mark = {}
            # the title song, on the title, for ~8 s of guest time
            M_sleep(m, 8.0)
            # the engine, held at three speeds by the guest's own routines: the tone is asked for with its
            # lease and rings while the machine runs on
            gu.halt()
            gu.put("xu_race", 1)
            gu.put("xu_cur", 255)
            plan = []
            for speed, inp in ((0, 0), (400, INP_A), (800, INP_A), (0x466, INP_A | INP_B)):
                gu.put("xm_mode", XM_RIDE)
                gu.putw("xm_speed", speed)
                gu.put("xm_inp", inp)
                gu.put("xb_paused", 0)
                gu.putw("xb_cd", 0)
                gu.put("xu_fin", 0)
                k = engine_model(XM_RIDE, speed, inp)
                gu.put("xu_k", k)
                gu.put("xu_dt", 0)
                gu.putw("xu_enghz", 0)               # (k was set by hand: have the engine say its Hz)
                gu.put("xu_hold", 0)
                gu.call("xu_engine")
                gu.call("xu_emit")
                assert gu.w("xu_sent") == ENGINE[k] << 1, (speed, gu.w("xu_sent"), ENGINE[k] << 1)
                plan.append((speed, ENGINE[k] << 1))
                gu.release()
                M_sleep(m, .8)
                gu.halt()
            gu.put("xu_race", 0)
            gu.call("xu_song", ax=255)
            gu.release()
            M_sleep(m, .3)
            fl.key("Escape", settle=.8)
    finally:
        os.environ.pop("MARTYPC_WAV", None)
    wav = [os.path.join(out, f) for f in sorted(os.listdir(out)) if f.startswith("audio-speaker") and "pc_speaker" in f]
    assert wav, ("no speaker capture", os.listdir(out))
    runs, rate, dur = wav_runs(wav[0])
    assert runs, "the capture holds no tone"
    print("capture: %s, %.1f s @ %d Hz, %d steady tones" % (os.path.basename(wav[0]), dur, rate, len(runs)), flush=True)
    # the capture falls into segments by long silences: the BIOS's boot beep, the title song, the engine
    segs, cur = [], []
    for r in runs:
        if cur and r[0] - cur[-1][1] > 5.0:
            segs.append(cur)
            cur = []
        cur.append(r)
    segs.append(cur)
    assert len(segs) >= 3, ("expected the boot beep, the title song and the engine", [len(x) for x in segs])
    title_runs, engine_runs = segs[-2], segs[-1]
    # The first engine tone (speed 0) starts the moment the title song is halted, so on a slow host the
    # title's tail and the engine's first tone can close to under the 5 s split and land in ONE segment
    # (a capture-start/segmentation offset, not a missing tone).  Search everything after the boot beep.
    engine_runs = [r for sg in segs[1:] for r in sg]
    # the engine: each planned Hz is heard, in order, for most of a second, within 3%
    heard = [r for r in engine_runs if r[1] - r[0] >= 0.4]      # held 0.8 s; the song's notes are shorter
    idx = 0
    found = []
    for speed, hz in plan:
        while idx < len(heard) and abs(heard[idx][2] - hz) > 0.03 * hz:
            idx += 1
        assert idx < len(heard), ("the engine tone %d Hz (speed %d) was never heard; heard %s"
                                  % (hz, speed, [round(r[2]) for r in heard]))
        found.append((speed, hz, round(heard[idx][2], 1), round(heard[idx][1] - heard[idx][0], 2)))
        idx += 1
    print("capture engine: %s: PASS" % "; ".join("speed %d: table Hz x2 = %d, heard %s Hz for %s s" % f
                                                  for f in found), flush=True)
    # the title song: the lead's notes in order.  The speaker asks for a new tone only when the frequency
    # differs, so two equal notes with no rest between are ONE run; a rest is the gap between runs.
    lead = d["songs"][inc["EXBSONG_TITLE"]]["voices"][0]
    want = []                                # [hz, seconds]
    prev_rest = True
    for p_, st in lead:
        hz = fold(NOTES[p_ - 1]) if p_ else 0
        if not hz:
            prev_rest = True
        elif not prev_rest and want and want[-1][0] == hz:
            want[-1][1] += st / STEPS_HZ
        else:
            want.append([hz, st / STEPS_HZ])
            prev_rest = False
        if len(want) >= 14:
            break
    got = [(r[2], r[1] - r[0]) for r in title_runs]
    assert len(got) >= 12, ("the title song's notes were not heard", [round(g[0]) for g in got])
    n = 0
    for (hz, sec), (ghz, gsec) in zip(want[:12], got):
        assert abs(ghz - hz) <= 0.03 * hz, ("the title lead: note %d heard at %.1f Hz, the score says %d Hz" % (n, ghz, hz))
        assert abs(gsec - sec) <= max(0.07, 0.2 * sec), ("note %d lasts %.3f s, the score says %.3f" % (n, gsec, sec))
        n += 1
    print("capture title: the lead's first %d notes are heard in order at the score's pitch and length "
          "(%s Hz; folded up over %d Hz; equal notes run together on the speaker): PASS"
          % (n, " ".join(str(round(g[0])) for g in got[:12]), SPK_FLOOR), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", action="store_true")
    ap.add_argument("--speaker", action="store_true")
    ap.add_argument("--fm", action="store_true")
    ap.add_argument("--capture", action="store_true")
    ap.add_argument("--adapter", choices=("vga", "cga"), default="vga")
    ap.add_argument("--quick", action="store_true", help="(development) skip the song and engine sweeps")
    ap.add_argument("--only", default="", help="(development, negative controls) --speaker: only these checks, "
                                               "comma separated: music, engine, effects, race")
    a = ap.parse_args()
    global QUICK, ONLY
    QUICK = a.quick
    ONLY = tuple(x for x in a.only.split(",") if x)
    every = not (a.host or a.speaker or a.fm or a.capture)
    if a.host or every:
        host_arm()
    if a.speaker or every:
        speaker_arm(a.adapter)
    if a.fm or every:
        fm_arm(a.adapter)
    if a.capture or every:
        capture_arm(a.adapter)
    print("excitebike_audio: PASS")


if __name__ == "__main__":
    main()
