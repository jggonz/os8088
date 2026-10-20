#!/usr/bin/env python3
"""isoTron live: the sound, the keys and the pace on the real 8088 (SPEC.md 108.14).

    make isotrondisk && python3 tests/isotronlive.py --sound
    python3 tests/isotronlive.py --input
    python3 tests/isotronlive.py --pace

All three on MartyPC's 5150 with a CGA (os8088_5150_cga_gla, the machine the
pace is fitted to), ISOTRON.O88 off drive B:, every wait in GUEST time, every
key a real one through the 8255 and int 09h.

SOUND (isotronsound). The speaker is CAPTURED (MARTYPC_WAV, the vidspk and
excitebike_audio precedent) and every OSAPI_SND_TONE the package makes is
traced at its one call site (the it_tonecall mark, AX / CX / DL read off the
registers). `1` starts a seeded game with sound and nobody steering: READY's
three beeps are 440, 440 and 880 Hz at 50h on ticks 0, 3 and 6 with their
offs at 60h; the wall ticks are 110 Hz at 40h with a one-tick lease - 165 Hz
once F10 has boosted the player - one every 4th player cell and never two in
a wake; the crash is 15 entries falling x 15/16 from 1,400 Hz a tick apart at
60h, then its off; and ON THE CAPTURE the crash's dominant frequency, tick by
tick (a Goertzel scan, tools/sndcheck.py), falls monotonically within 3% of
the table. Then M mutes - the next whole round, its sweep, READY, play and
crash, makes NO call and the capture is silent - and M again unmutes; a
SILENT tier (`A`) is silent the same way. Last the gate: port 61h's bits 0
and 1 are clear, and nothing more sounds, after P pauses a round with the
wall ticking, after Esc in the middle of a crash's sweep, after F enters
full screen in the middle of one and after F leaves it in the middle of
another - each of those a sweep that would otherwise have run 12 more ticks.
In full screen a wall tick is followed by its explicit off a frame later.

INPUT (isotroninput). A held F10 RAMPS the speed: sampled at every wake, p
goes 2 -> 9 by its typematic repeats, through at least five values. A HELD
turn key is ONE turn: Del held 1.5 s with the game held still posts exactly
one turn, and the filter counts the repeats it dropped (at least five - the
negative control, so the keyboard really did repeat). A DOUBLE TAP is two
turns, a U-turn: the player reverses one row over. With NumLock on, keypad
Ins (AL '0', AH 52h) turns. Up posts F10's boosts and Down F9's normal. A
toggle resets the turn keys' tracking.

PACE (isotronpace). A seeded harness round (NOFLOW, paced, nobody steering,
p = 2) at Rookie h1, Rookie h2 and Lunatic h1, windowed and then in full
screen: iterations per GUEST second (MartyPC's cycle counter, between two
wake ends - the worker's it_wake1, the bracket's it_fs_frame - where the debt
is in (-cost, 0], so the count is good to one iteration) within 2% of SPEC.md
108.4.1's model, with every head alive and p = 2 throughout; and the clock
itself: the u the governor credited over the window (its iterations' costs
plus the debt's change) equal to 768 a BIOS tick, plus 256 a phase in full
screen, to within one tick. Then THE STALL: it_tstall makes one paced
wake / frame wait 10 real ticks, and what that wake credits is at most two
ticks (its own and one owed: DEBT_CAP, SPEC.md 108.4.2) - and at least one
and a half, so the stall's owed tick was not dropped too. And V = now at a
RESUME (P, a guest second, P: the first wake back credits at most a tick)
and at a TOGGLE (the first iterating frame in full screen, and the first
wake back in the window, at most a tick each).
"""
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import os88marty as M                               # noqa: E402
import os88ui                                       # noqa: E402
import isotronlib as L                              # noqa: E402
import isotronsim as S                              # noqa: E402
import isotron as T                                 # noqa: E402
import sndcheck                                     # noqa: E402

TAG = "cga"                                         # the 5150 the pace is fitted to
TICK_CYC = M.GUEST_HZ * 65536.0 / 1193182.0         # 262,144: the PIT is the CPU / 4
SEED = 11                                           # every head lives through each window (the model says, isotronsim)
P_NORMAL = L.P_NORMAL
H_ALIVE = L.H_ALIVE
OUT = os.path.join(ROOT, "build", "isotron-live")


def _e(name):
    return L._E[name]


CRASH = [round(1400 * (15.0 / 16.0) ** k) for k in range(15)]
PRI_TICK, PRI_PHR, PRI_HIGH = _e("IT_SPRI_TICK"), _e("IT_SPRI_PHR"), _e("IT_SPRI_HIGH")
TICK_HZ_N, TICK_HZ_B = _e("IT_SND_TICK"), _e("IT_SND_TICKB")


# =============================================================================
# shared
# =============================================================================

def boot():
    system, apps, machine = T.boot_for(TAG)
    return os88ui.boot(system, apps=apps, machine=machine)


def open_game(ui):
    g = L.open_game(ui)
    M.until(ui.m, lambda _: g.b("it_spawned") == 1, "the worker to start", poll=0.2, guest=20)
    T.presented(ui, g)
    return g


def player(g):
    return g.a("it_heads") + L.MAXHEADS * L.H_SIZE


def ph(g, off):
    return g.m.read(player(g) + off, 1)[0]


def fifo(g):
    n = (g.b("it_fhead") - g.b("it_ftail")) & 255
    t = g.b("it_ftail")
    return [g.m.read(g.a("it_fifo") + ((t + k) & 3), 1)[0] for k in range(n)]


def hold(g, on):
    """The harness's freeze of the game (it_tpause): it_commands runs - so a
    key's post is read - and no iteration does."""
    g.put("it_tpause", 1 if on else 0)


def until(g, cond, what, guest=20):
    M.until(g.m, lambda _: cond(), what, poll=0.05, guest=guest)


class Bss:
    """The whole bss read in ONE round trip at a stop: a hit that needs a dozen
    variables pays one socket call, not twelve."""

    def __init__(self, g):
        self.g = g
        offs = list(g.bss.values())
        self.lo, self.hi = min(offs), max(offs) + 64

    def snap(self, mm):
        raw = bytes(mm.read(self.g.base + self.lo, self.hi - self.lo))
        return Snap(self.g, self.lo, raw)


class Snap:
    def __init__(self, g, lo, raw):
        self.g, self.lo, self.raw = g, lo, raw

    def b(self, name, k=0):
        return self.raw[self.g.bss[name] - self.lo + k]

    def w(self, name, k=0):
        return struct.unpack_from("<H", self.raw, self.g.bss[name] - self.lo + k)[0]

    def sw(self, name):
        return struct.unpack_from("<h", self.raw, self.g.bss[name] - self.lo)[0]


def ticks(mm):
    return struct.unpack("<I", bytes(mm.read(0x46C, 4)))[0]


def gate(m):
    return m.inb(0x61) & 3


def press(g, name, cond, what, guest=20):
    """A keystroke that must change something: tapped, and AGAIN if nothing
    happened in a guest second and a half - a key sent while a bp_trace pump
    had the machine stopped was lost outright once in three runs (Esc, the
    gate leg), the release-lost case's sibling. Then the plain wait."""
    for _ in range(3):
        tap(g.m, name)
        try:
            M.until(g.m, lambda _: cond(), what, poll=0.02, guest=1.5)
            return
        except M.MartyError:
            pass
    until(g, cond, what, guest=guest)


def tap(m, name, secs=0.04):
    """One keystroke as a finger makes it: the press, a few guest ms, the
    release - as TWO commands. One `key` command carrying both, sent while a
    bp_trace pump has the machine stopped, lost the release: F then stayed
    down in the kernel's key map and the bracket's entry, which waits for F's
    release, never ended."""
    m.key(name, down=True, up=False)
    try:
        M.guest_sleep(m, secs)
    finally:
        m.key(name, down=False, up=True)


def tap_exact(m, name, secs=0.04):
    """One keystroke held EXACTLY `secs` of guest time: paused, the press,
    `advance` by the cycles, the release, run. tap()'s hold is a host-timed
    guest_sleep, and on MartyPC that polls by host round trips while the
    guest runs ~7-8x real time - a "40 ms" tap measured 0.40-0.55 guest s,
    which straddles the keyboard's ~500 ms typematic delay, so one Up in
    three or four posted a REPEAT and the package rightly counted it as a
    second boost (SPEC.md 108.4.1: F10/Up repeats ramp the speed). Only
    OUTSIDE a bp_trace: a pause from this thread can land on a stop a pump
    has not recorded (tests/isotron.py's roundstart says why)."""
    m.pause()
    try:
        m.key(name, down=True, up=False)
        m.advance(cycles=int(secs * M.GUEST_HZ))
        m.key(name, down=False, up=True)
    finally:
        m.run()


# =============================================================================
# SOUND
# =============================================================================

class Tones:
    """Every OSAPI_SND_TONE the package makes, at its one call site."""

    def __init__(self, g):
        self.g = g
        self.at = g.base + g.code["it_tonecall"]
        self.at1 = g.base + g.code["it_tonecall1"]
        self.log = []
        self.armed = None               # (crash entries to wait for, a key)
        self.pressed = None             # ...and the gate when it went down
        self._n = 0

    def onhit(self, mm, rec):
        if rec["addr"] == self.at1:
            # THE DRIVER'S ANSWER: CF = 1 refused. Read with the kernel's own
            # owner record, so a refusal says why
            if self.log:
                e = self.log[-1]
                e["cf"] = rec["regs"]["flags"] & 1
                if e["cf"]:
                    S_ = self.g.ui.sym if hasattr(self.g.ui, "sym") else None
                    e["why"] = dict((n, bytes(mm.read(mm.sym(n), 2)).hex()) for n in
                                    ("snd_town_act", "snd_town_pri", "snd_ch2mode", "snd_live"))
            return None
        if rec["addr"] != self.at:
            return None
        r = rec["regs"]
        st = mm.read(self.g.a("it_state"), 1)[0]
        fsm = mm.read(self.g.a("it_fsm"), 1)[0]
        e = dict(cyc=rec["cycles"], hz=r["ax"], cx=r["cx"], pri=r["dx"] & 0xFF, st=st, fsm=fsm)
        self.log.append(e)
        if self.armed and e["hz"] in CRASH and e["pri"] == PRI_HIGH:
            self._n += 1
            if self._n >= self.armed[0]:
                # SEEN AT THIS STOP: a crash's sweep is 0.8 s, and a host poll
                # for CRASH missed all of it under load. The key itself goes
                # from the main thread (a press sent from a stop arrived ~9
                # ticks late, MEASURED), polled at 10 ms
                self.pressed = dict(gate=mm.inb(0x61) & 3, key=self.armed[1], k=len(self.log))
                self.armed = None
        return e

    def arm(self, key, n=1):
        """`key` to go down at the n-th entry of the NEXT crash's sweep, from
        the stop itself. Arm it before anything can crash."""
        self.pressed, self._n, self.armed = None, 0, (n, key)

    def press_in_crash(self):
        """Wait for the armed key to have gone down, release it a few guest
        ms on. Answers the log's length at the press."""
        g = self.g
        n = self.armed[0] if self.armed else 0
        try:
            M.until(g.m, lambda _: self.pressed is not None, "a crash's sweep %d entries in" % n,
                    poll=0.01, guest=30)
        except M.MartyError:
            raise AssertionError("no crash's sweep: state %d paused %d snd %d fsm %d iter %d p %d "
                                 "player (%d, %d) alive %d; the last calls %r"
                                 % (g.b("it_state"), g.b("it_paused"), g.b("it_snd"),
                                    g.b("it_fsm"), g.w("it_iter"), g.b("it_pspeed"),
                                    ph(g, L.H_U), ph(g, L.H_D), ph(g, H_ALIVE), self.log[-5:]))
        k = len(self.log)
        self.pressed["gate"] = gate(g.m)        # the sweep sounding as the key goes
        self.pressed["n"] = sum(1 for e in self.log[self.pressed["k"] - 1:]
                                if e["pri"] == PRI_HIGH and e["hz"])
        left = 15 - self.pressed["n"]
        assert left >= 4, "the host was too slow: the key would go with %d of the crash's " \
            "sweep left" % left
        tap(g.m, self.pressed["key"])
        self.pressed["k"] = k
        assert self.pressed["gate"] == 3, \
            "the crash's sweep is sounding and 61h & 3 = %d" % self.pressed["gate"]
        return self.pressed["k"]

    def trace(self, *more):
        return M.bp_trace(self.g.m, self.at, self.at1, *more, regs=True, on_hit=self.onhit,
                          cap=50000)

    def since(self, k):
        return self.log[k:]


def wav_load(path):
    rate, mono, dur = sndcheck.load(path)
    return rate, mono


def wav_slice(w, t0, t1):
    rate, mono = w
    return mono[max(0, int(t0 * rate)):max(0, int(t1 * rate))]


def rms(x):
    if not x:
        return 0.0
    mean = sum(x) / len(x)
    return (sum((v - mean) ** 2 for v in x) / len(x)) ** .5


def crash_calls(log):
    """The crash phrases in a tone log: runs of the 15 entries."""
    out, k = [], 0
    while k < len(log):
        if log[k]["hz"] == CRASH[0] and log[k]["pri"] == PRI_HIGH:
            out.append(log[k:k + 16])
            k += 16
        else:
            k += 1
    return out


def go_round(g, keys=()):
    """From the title or WAIT: the keys, then wait for PLAY."""
    for k in keys:
        tap(g.m, k)
    until(g, lambda: g.b("it_state") == L.ST_PLAY, "a round in play", guest=20)


def boost(g, n):
    for _ in range(n):
        tap(g.m, "F10")
        M.guest_sleep(g.m, 0.03)


def sound_leg():
    os.makedirs(OUT, exist_ok=True)
    cap = os.path.join(OUT, "snd")
    for f in os.listdir(OUT):
        if f.startswith("snd"):
            os.unlink(os.path.join(OUT, f))
    os.environ["MARTYPC_WAV"] = cap
    try:
        ctx = boot()
        ui = ctx.__enter__()
    finally:
        os.environ.pop("MARTYPC_WAV", None)
    try:
        _sound(ui)
    finally:
        ctx.__exit__(None, None, None)
    print("isotronsound: ok")


def _sound(ui):
    g = open_game(ui)
    m = ui.m
    tones = Tones(g)
    W1 = g.base + g.code["it_wake1"]
    m.pause()
    g.put("it_tseed", SEED, 2)
    g.put("it_tflags", L.TF_SEED)          # the stored seed through the real title
    m.run()
    # --- A: a round with sound, nobody steering ------------------------------
    with tones.trace():
        tap(m, "Digit1")
        until(g, lambda: g.b("it_state") == L.ST_PLAY, "the round's start", guest=20)
        assert g.b("it_snd") == 1, "1 started a game without sound"
        M.guest_sleep(m, 1.0)
        c_boost = m.status()["cycles"]
        tap(m, "F10")                           # boosted from here: 165 Hz
        until(g, lambda: g.b("it_state") == L.ST_WAIT, "the round to end", guest=30)
        M.guest_sleep(m, 0.3)
    log = list(tones.log)
    calls = [(e["hz"], e["pri"]) for e in log[:6]]
    want = [(440, PRI_PHR), (0, PRI_HIGH), (440, PRI_PHR), (0, PRI_HIGH), (880, PRI_PHR),
            (0, PRI_HIGH)]
    assert calls == want, "READY's calls: %r, not %r" % (calls, want)
    dt = [round((e["cyc"] - log[0]["cyc"]) / TICK_CYC) for e in log[:6]]
    raw = ["%.2f/%d/%d" % ((e["cyc"] - log[0]["cyc"]) / TICK_CYC, e["hz"], e["st"]) for e in log[:8]]
    assert dt == [0, 2, 3, 5, 6, 8], "%r " % raw + \
        "READY's beeps on ticks %r, not 0 2 3 5 6 8" % dt
    assert all(e["cx"] == 3 for e in log[:6] if e["hz"]), "READY's leases %r" % [e["cx"] for e in log[:6]]
    crashes = crash_calls(log)
    assert len(crashes) == 1, "%d crash sweeps in one round" % len(crashes)
    cr = crashes[0]
    k_cr = log.index(cr[0])
    play = log[6:k_cr]
    assert play, "no wall tick in a whole round"
    for e in play:
        assert e["pri"] == PRI_TICK and e["cx"] == 1 and e["hz"] in (TICK_HZ_N, TICK_HZ_B), \
            "a call in play that is not a wall tick: %r" % e
    pre = [e for e in play if e["cyc"] < c_boost]
    post = [e for e in play if e["cyc"] > c_boost + 2 * TICK_CYC]
    assert pre and all(e["hz"] == TICK_HZ_N for e in pre), "unboosted ticks %r" % \
        sorted(set(e["hz"] for e in pre))
    assert post and all(e["hz"] == TICK_HZ_B for e in post), "boosted ticks %r" % \
        sorted(set(e["hz"] for e in post))
    gaps = [b["cyc"] - a["cyc"] for a, b in zip(play, play[1:])]
    assert min(gaps) > TICK_CYC / 2, "two wall ticks %d cycles apart - one a wake" % min(gaps)
    cells = abs(ph(g, L.H_D) - L.P_D0) + abs(ph(g, L.H_U) - 108)
    assert abs(len(play) - cells // 4) <= 1, \
        "%d wall ticks for %d player cells - one every 4th" % (len(play), cells)
    refused = [e for e in log if e.get("cf")]
    assert not refused, "the driver REFUSED %r" % refused
    hz = [e["hz"] for e in cr]
    assert hz[:15] == CRASH and hz[15] == 0 and cr[15]["pri"] == PRI_HIGH, \
        "the crash's calls %r" % hz
    assert all(e["pri"] == PRI_HIGH and e["cx"] == 2 for e in cr[:15]), "the crash's priorities"
    cdt = [(b["cyc"] - a["cyc"]) / TICK_CYC for a, b in zip(cr, cr[1:])]
    assert all(0.5 < x < 1.5 for x in cdt), "the crash's entries %r ticks apart" % cdt
    assert len(log) == k_cr + 16, "calls after the crash's off: %r" % log[k_cr + 16:]
    print("  sound: READY 440/440/880 at 50h on ticks %r, offs at 60h; %d wall ticks for %d "
          "player cells (%d at 110 Hz, %d at 165 boosted), one a wake at most; the crash's 15 "
          "entries 1400 -> 567 Hz a tick apart at 60h, then off" %
          (dt, len(play), cells, len(pre), len(post)), flush=True)
    seg = dict(crash=[e["cyc"] for e in cr], ready=(log[0]["cyc"], log[5]["cyc"]))
    # --- B: M mutes a whole round, and unmutes ---------------------------------
    n0 = g.w("it_ntone")
    press(g, "KeyM", lambda: g.b("it_snd") == 0, "M to mute")
    c_m0 = m.status()["cycles"]
    press(g, "Space", lambda: g.b("it_state") == L.ST_PLAY, "the muted round", guest=20)
    boost(g, 4)
    until(g, lambda: g.b("it_state") == L.ST_WAIT, "the muted round to end", guest=30)
    M.guest_sleep(m, 0.3)
    c_m1 = m.status()["cycles"]
    assert g.w("it_ntone") == n0, "muted, the round made %d calls" % (g.w("it_ntone") - n0)
    press(g, "KeyM", lambda: g.b("it_snd") == 1, "M to unmute")
    seg["muted"] = (c_m0, c_m1)
    print("  sound: M mutes - a whole round (sweep, READY, play, crash) made no call; M again "
          "unmutes", flush=True)
    # --- C: a silent tier ------------------------------------------------------
    press(g, "Escape", lambda: g.b("it_state") == L.ST_TITLE, "Esc to the title")
    n0 = g.w("it_ntone")
    c_s0 = m.status()["cycles"]
    go_round(g, ["KeyA"])
    assert g.b("it_snd") == 0 and g.b("it_tier") == 1, "A is not a silent Rookie game"
    boost(g, 4)
    until(g, lambda: g.b("it_state") == L.ST_WAIT, "the silent round to end", guest=30)
    M.guest_sleep(m, 0.3)
    c_s1 = m.status()["cycles"]
    assert g.w("it_ntone") == n0, "a silent tier made %d calls" % (g.w("it_ntone") - n0)
    seg["silent"] = (c_s0, c_s1)
    press(g, "Escape", lambda: g.b("it_state") == L.ST_TITLE, "Esc to the title")
    print("  sound: A - a silent Rookie - made no call through a whole round", flush=True)
    # --- D: the tower's chime and GAME OVER's phrase -----------------------------
    tower_leg(g, tones)
    # --- E: the gate, after pause, Esc and both toggles -------------------------
    gate_leg(g, ui, tones)
    # --- F: what the sound costs ------------------------------------------------
    cost_leg(g, ui)
    # --- E: the capture ----------------------------------------------------------
    m.pause()
    wavs = [os.path.join(OUT, f) for f in sorted(os.listdir(OUT))
            if f.startswith("snd") and "speaker" in f]
    assert wavs, "no speaker capture in %s: %r" % (OUT, os.listdir(OUT))
    w = wav_load(wavs[0])
    m.run()
    capture(w, seg)


PH_TOWER = [(0, 523), (3, 659), (6, 784), (9, 0)]
PH_OVER = [(0, 523), (4, 392), (8, 330), (12, 262), (16, 0)]


def phrase_at(log, k, want, pri, what):
    """log[k:] begins with the phrase `want` ((tick, Hz) rows): its Hz, its
    priority (offs at 60h), its rows on their ticks."""
    got = log[k:k + len(want)]
    refused = [e for e in got if e.get("cf")]
    assert not refused, "%s: the driver REFUSED %r" % (what, refused)
    assert [e["hz"] for e in got] == [hz for _, hz in want], \
        "%s: %r, not %r" % (what, [e["hz"] for e in got], [hz for _, hz in want])
    assert all(e["pri"] == (pri if e["hz"] else PRI_HIGH) for e in got), \
        "%s: priorities %r" % (what, [e["pri"] for e in got])
    dt = [round((e["cyc"] - got[0]["cyc"]) / TICK_CYC) for e in got]
    assert dt == [t for t, _ in want], "%s on ticks %r, not %r" % (what, dt, [t for t, _ in want])
    return dt


def tower_leg(g, tones):
    """A stored game played FAST by the harness's bot through the real title
    (isotronflow's recipe): every RAISE has its chime while the new level
    sweeps in, a reset to 1 and a round at the same height have none, and
    the third loss's GAME OVER has its phrase under the line."""
    m = g.m
    seed, tier, _, _ = next(x for x in S.STORED if x[1] == 1)
    want = S.Game(seed, tier).play()
    m.pause()
    g.put("it_tseed", seed, 2)
    g.put("it_tflags", L.TF_FAST | L.TF_BOT | L.TF_SEED)
    m.run()
    k0 = len(tones.log)
    with tones.trace():
        tap(m, "Digit1")
        for r in range(len(want) - 1):
            until(g, lambda: g.b("it_state") == L.ST_WAIT and g.w("it_round") == r + 1,
                  "round %d's WAIT" % (r + 1), guest=60)
            press(g, "Space", lambda: g.b("it_state") != L.ST_WAIT, "Space to continue")
        until(g, lambda: g.b("it_state") == L.ST_OVER, "GAME OVER", guest=60)
        M.guest_sleep(m, 1.2)
    log = tones.since(k0)
    m.pause()
    g.put("it_tflags", L.TF_SEED)
    m.run()
    press(g, "Space", lambda: g.b("it_state") == L.ST_TITLE, "the title")
    chimes = [k for k, e in enumerate(log) if e["hz"] == 523 and e["st"] == L.ST_SWEEP]
    raises = [k for k in range(1, len(want)) if want[k]["h"] > want[k - 1]["h"]]
    assert len(chimes) == len(raises), "%d chimes for %d raises (heights %r)" \
        % (len(chimes), len(raises), [r["h"] for r in want])
    for k in chimes:
        phrase_at(log, k, PH_TOWER, PRI_PHR, "the tower's chime")
    overs = [k for k, e in enumerate(log) if e["hz"] == 523 and e["st"] == L.ST_OVER]
    assert len(overs) == 1, "%d GAME OVER phrases" % len(overs)
    phrase_at(log, overs[0], PH_OVER, PRI_PHR, "GAME OVER's phrase")
    refused = [e for e in log if e.get("cf")]
    assert not refused, "the driver REFUSED %r (the crash's 60h lease, not freed before GAME " \
        "OVER's 50h?)" % refused
    print("  sound: a stored game (heights %s): %d raises, %d chimes 523/659/784 on ticks 0 3 6 "
          "and off on 9, none on a reset or the same height; GAME OVER 523/392/330/262 on "
          "ticks 0 4 8 12, off on 16" % ("-".join(str(r["h"]) for r in want), len(raises),
                                         len(chimes)), flush=True)


def med(xs):
    xs = sorted(xs)
    return xs[len(xs) // 2] if xs else None


def cost_leg(g, ui):
    """MartyPC's cycle counter on what the sound costs (SPEC.md 108.14.4,
    PERFORMANCE.md Set 157): one OSAPI_SND_TONE, on and off, at its one call
    site (it_tonecall -> it_tonecall1), and a bracket frame's sound
    (it_fs_frame -> it_fssnd1) with no call and with one, over a round in
    full screen, boosted, with the wall ticking."""
    m = g.m
    B = g.base
    TC0, TC1 = B + g.code["it_tonecall"], B + g.code["it_tonecall1"]
    FF, FS1 = B + g.code["it_fs_frame"], B + g.code["it_fssnd1"]
    T.fs_enter(ui, g, "F")
    go_round(g, ["Digit1"])
    with M.bp_trace(m, TC0, TC1, FF, FS1, regs=True, cap=20000) as tr:
        boost(g, 3)
        M.guest_sleep(m, 0.8)
    calls, frames = {"on": [], "off": []}, {"quiet": [], "call": []}
    last = {}
    for h in tr.hits:
        last.setdefault(h["addr"], None)
        if h["addr"] == TC0:
            last["tc"] = h
        elif h["addr"] == TC1 and last.get("tc"):
            kind = "on" if last["tc"]["regs"]["ax"] else "off"
            calls[kind].append(h["cycles"] - last["tc"]["cycles"])
            last["tc"] = None
            last["called"] = True
        elif h["addr"] == FF:
            last["ff"], last["called"] = h, False
        elif h["addr"] == FS1 and last.get("ff"):
            frames["call" if last.get("called") else "quiet"].append(h["cycles"] - last["ff"]["cycles"])
            last["ff"] = None
    out = dict(on=med(calls["on"]), off=med(calls["off"]), quiet=med(frames["quiet"]),
               call=med(frames["call"]), n=dict((k, len(v)) for k, v in list(calls.items())
                                                + list(frames.items())))
    T.fs_leave(ui, g, "F")
    press(g, "Escape", lambda: g.b("it_state") == L.ST_TITLE, "Esc to the title")
    assert out["on"] and out["off"] and out["quiet"] and out["call"], "no sample: %r" % out
    print("  cost: one OSAPI_SND_TONE %d cycles on, %d off (medians of %d and %d); a bracket "
          "frame's sound %d cycles with no call (%d frames), %d with one (%d)"
          % (out["on"], out["off"], out["n"]["on"], out["n"]["off"], out["quiet"],
             out["n"]["quiet"], out["call"], out["n"]["call"]), flush=True)
    return out


def held_gate(g, what, secs=1.0, n=6):
    """The gate over `secs` guest seconds: n samples, all 0."""
    m = g.m
    for _ in range(n):
        v = gate(m)
        assert v == 0, "%s: port 61h & 3 = %d" % (what, v)
        M.guest_sleep(m, secs / n)


def silenced(extra, what):
    """The calls after a key that must silence a crash's sweep: at most
    eight more of its entries (the host's poll, the key's dispatch, a wake),
    then ONE off at 60h, then nothing - where the sweep had at least
    eleven more to run (the key is sent at its first entry's stop)."""
    k = 0
    while k < len(extra) and extra[k]["hz"] in CRASH and extra[k]["pri"] == PRI_HIGH:
        k += 1
    rest = [(e["hz"], e["pri"]) for e in extra[k:]]
    assert k <= 8 and rest == [(0, PRI_HIGH)], \
        "%s: %r - not (at most eight entries and) one off at 60h, then silence" \
        % (what, [(e["hz"], e["pri"]) for e in extra])
    return k


def gate_leg(g, ui, tones):
    m = g.m
    with tones.trace():
        # 1. P in a round with the wall ticking
        go_round(g, ["Digit1"])
        k = len(tones.log)
        M.until(m, lambda _: any(e["pri"] == PRI_TICK for e in tones.since(k)),
                "a wall tick", poll=0.02, guest=5)
        press(g, "KeyP", lambda: g.b("it_paused") == 1, "P to pause")
        k = len(tones.log)
        last = tones.log[-1]
        held_gate(g, "paused")
        extra = tones.since(k)
        assert not extra, "paused, the package called %r" % extra
        press(g, "KeyP", lambda: g.b("it_paused") == 0, "P to resume")
        print("  gate: P - 61h & 3 = 0 and no call for a guest second (the last before it: "
              "%d Hz at %02Xh)" % (last["hz"], last["pri"]), flush=True)
        # 2. Esc in the middle of a crash's sweep (a fresh round, boosted at its
        # start, so the crash is a second or more away when the key is armed)
        press(g, "Escape", lambda: g.b("it_state") == L.ST_TITLE, "Esc to the title")
        go_round(g, ["Digit1"])
        boost(g, 3)
        tones.arm("Escape")
        k = tones.press_in_crash()
        until(g, lambda: g.b("it_state") == L.ST_TITLE, "Esc in CRASH")
        held_gate(g, "after Esc in the crash's sweep")
        n = silenced(tones.since(k), "after Esc")
        print("  gate: Esc in the crash's sweep (%d entries in) - %d more, one off at 60h, then "
              "61h & 3 = 0 and silence" % (tones.pressed["n"], n), flush=True)
        # 3. F into full screen in the middle of one
        go_round(g, ["Digit1"])
        boost(g, 3)
        tones.arm("KeyF")
        k = tones.press_in_crash()
        until(g, lambda: g.b("it_fsm") == 1, "F into full screen", guest=20)
        held_gate(g, "after F into full screen in the crash's sweep", 0.6, 4)
        n = silenced(tones.since(k), "into full screen")
        T.fs_settled(g)
        print("  gate: F in the crash's sweep (%d entries in) - %d more, one off, the bracket "
              "silent" % (tones.pressed["n"], n), flush=True)
        # ...a round in full screen: a wall tick and its explicit off a frame on
        until(g, lambda: g.b("it_state") == L.ST_WAIT, "WAIT", guest=20)
        k = len(tones.log)
        tones.arm("KeyF")                       # (4, below: armed before it can crash)
        press(g, "Space", lambda: g.b("it_state") == L.ST_PLAY, "a round in full screen", guest=20)
        M.guest_sleep(m, 1.0)
        fsl = tones.since(k)
        pairs = [(a, b) for a, b in zip(fsl, fsl[1:]) if a["pri"] == PRI_TICK
                 and b["hz"] == 0 and b["cyc"] - a["cyc"] < TICK_CYC]
        ticks_ = [e for e in fsl if e["pri"] == PRI_TICK]
        assert ticks_ and len(pairs) >= len(ticks_) // 2, \
            "full screen: %d wall ticks, %d followed by their off inside the tick" \
            % (len(ticks_), len(pairs))
        print("  gate: full screen - %d wall ticks, %d with their explicit off a frame on"
              % (len(ticks_), len(pairs)), flush=True)
        # 4. F out of full screen in the middle of a crash's sweep
        n0 = g.w("it_fsout")                   # (no boost: a second in, the round
        k = tones.press_in_crash()              # is near its end at p = 2)
        until(g, lambda: g.b("it_fsm") == 0 and g.w("it_fsout") > n0, "F out of full screen")
        held_gate(g, "after F out of full screen in the crash's sweep", 0.6, 4)
        n = silenced(tones.since(k), "out of full screen")
        T.presented(ui, g)
        press(g, "Escape", lambda: g.b("it_state") == L.ST_TITLE, "Esc to the title")
        print("  gate: F out of full screen in the crash's sweep (%d entries in) - %d more, one "
              "off, then silence" % (tones.pressed["n"], n), flush=True)


def capture(w, seg):
    rate = w[0]
    import json
    json.dump(seg, open(os.path.join(OUT, "seg.json"), "w"))
    # THE CAPTURE'S CLOCK IS THE GUEST'S: READY's 880 Hz beep where the trace
    # says it was asked for (the 6th call is its off; the 5th its start)
    r0, r1 = seg["ready"]

    def at(c):
        return c / M.GUEST_HZ

    f, p = sndcheck.dominant(wav_slice(w, at(r1) - 1.6 * TICK_CYC / M.GUEST_HZ,
                                       at(r1) - 0.4 * TICK_CYC / M.GUEST_HZ), rate, 300, 1200, 5)
    assert abs(f - 880) < 0.03 * 880, "READY's last beep on the capture: %d Hz, not 880" % f
    cr = seg["crash"]
    c1 = cr[15]
    got, drop = [], []
    for k in range(15):
        t0 = at(cr[k]) + 0.006
        x = wav_slice(w, t0, t0 + 0.040)
        if rms(x) < 0.05:
            # A HOLE: seen twice in ~10 runs (wave 5), at 688 Hz both times.
            # Every call reaching here was GRANTED (a refusal fails the call
            # log's check above), so a window the capture shows silent with
            # the guest's grant in hand is the emulated speaker's to explain,
            # not the package's (the wave log says what is known). One is
            # tolerated and named; a second fails.
            drop.append(k)
            got.append(None)
            continue
        f, _ = sndcheck.dominant(x, rate, 400, 1600, 2)
        got.append(f)
    assert len(drop) <= 1, "the crash on the capture has holes at entries %r" % drop
    bad = [(k, g_, w_) for k, (g_, w_) in enumerate(zip(got, CRASH))
           if g_ is not None and abs(g_ - w_) > 0.03 * w_]
    assert not bad, "the crash on the capture %r, the table %r" % (got, CRASH)
    heard = [f for f in got if f is not None]
    assert all(b < a for a, b in zip(heard, heard[1:])), "the crash does not fall: %r" % got
    if drop:
        print("  capture: NOTE a silent window at the crash's entry %d (granted, CF = 0)"
              % drop[0], flush=True)
    tail = wav_slice(w, at(c1) + 0.01, at(c1) + 0.2)
    assert rms(tail) < 0.02, "the capture is not silent after the crash's off (rms %.3f)" % rms(tail)
    for name in ("muted", "silent"):
        a, b = seg[name]
        x = wav_slice(w, at(a), at(b))
        assert x and rms(x) < 0.02, "the %s round is not silent on the capture (rms %.3f, %d " \
            "samples)" % (name, rms(x), len(x))
    print("  capture: %d Hz, READY's 880 where it was asked for; the crash falls %s Hz; "
          "silent after its off and through the muted and the silent rounds"
          % (rate, " ".join("%s" % f for f in got)), flush=True)


# =============================================================================
# INPUT
# =============================================================================

def fresh_round(g):
    """Esc to the title, and `1`: a new seeded round, 10 iterations in."""
    press(g, "Escape", lambda: g.b("it_state") == L.ST_TITLE, "Esc to the title")
    hold(g, False)
    # `1` as press(), not a bare tap: a key the machine lost while a pump had
    # it stopped is sent again, as Esc's is
    press(g, "Digit1", lambda: g.b("it_state") != L.ST_TITLE, "a game started")
    go_round(g)
    until(g, lambda: g.w("it_iter") >= 10, "the round under way")


def input_leg():
    with boot() as ui:
        g = open_game(ui)
        m = ui.m
        bss = Bss(g)
        W1 = g.base + g.code["it_wake1"]
        m.pause()
        g.put("it_tseed", SEED, 2)
        g.put("it_tflags", L.TF_SEED | L.TF_HOLD)
        m.run()
        go_round(g, ["Digit1"])
        assert g.b("it_pspeed") == P_NORMAL
        # --- a held F10 ramps p 2 -> 9 -------------------------------------------
        seen = []

        def onhit(mm, rec):
            s = bss.snap(mm)
            seen.append((s.b("it_pspeed"), s.b("it_state")))
        with M.bp_trace(m, W1, on_hit=onhit, cap=4000):
            M.guest_sleep(m, 0.2)
            m.key("F10", down=True, up=False)
            try:
                M.until(m, lambda _: g.b("it_pspeed") == L._E["P_MAX"], "F10 held to ramp p to 9",
                        poll=0.05, guest=5)
            finally:
                m.key("F10", down=False, up=True)
            M.guest_sleep(m, 0.1)
        k9 = next(k for k, (p, _) in enumerate(seen) if p == L._E["P_MAX"])
        seen = seen[:k9 + 1]                    # (at 68 cells a second the round
        ps = [p for p, st in seen]              # may end soon after: no matter)
        assert all(st == L.ST_PLAY for _, st in seen), "the round ended during the ramp: %r" % seen
        assert ps[0] == P_NORMAL and ps[-1] == L._E["P_MAX"], "the ramp %r" % ps
        assert all(b >= a for a, b in zip(ps, ps[1:])), "p fell while F10 was held: %r" % ps
        vals = sorted(set(ps))
        assert len(vals) >= 6, "p jumped rather than ramped: %r" % vals
        print("  input: F10 held - p ramps %s by its repeats, sampled at every wake"
              % " ".join(str(v) for v in vals), flush=True)
        fresh_round(g)
        # --- a held turn key is one turn (N -> E) ----------------------------------
        hold(g, True)
        M.guest_sleep(m, 0.1)
        nt, nd = g.w("it_nturn"), g.w("it_ndrop")
        hd0 = ph(g, L.H_HD)
        m.key("Delete", down=True, up=False)
        try:
            M.guest_sleep(m, 1.5)
        finally:
            m.key("Delete", down=False, up=True)
        M.guest_sleep(m, 0.1)
        f, dn, dd = fifo(g), g.w("it_nturn") - nt, g.w("it_ndrop") - nd
        assert f == [L.K_RIGHT] and dn == 1, \
            "Del held 1.5 s posted %r (%d taken) - a held key is ONE turn" % (f, dn)
        assert dd >= 5, "only %d repeats dropped - the keyboard did not repeat (the control)" % dd
        hold(g, False)
        until(g, lambda: not fifo(g), "the turn to be taken")
        hd1 = ph(g, L.H_HD)
        assert hd1 == (hd0 + 1) & 3, "heading %d after a right turn from %d" % (hd1, hd0)
        print("  input: Del held 1.5 s - one turn taken, %d repeats dropped" % dd, flush=True)
        # --- NumLock: keypad Ins is still Ins (E -> N) ---------------------------------
        M.guest_sleep(m, 0.3)
        hold(g, True)
        tap_exact(m, "NumLock")
        M.guest_sleep(m, 0.1)
        assert m.read(0x417, 1)[0] & 0x20, "NumLock is not on"
        nt = g.w("it_nturn")
        tap_exact(m, "Numpad0")
        M.guest_sleep(m, 0.1)
        assert fifo(g) == [L.K_LEFT] and g.w("it_nturn") == nt + 1, \
            "keypad Ins with NumLock on posted %r" % fifo(g)
        tap_exact(m, "NumLock")
        M.guest_sleep(m, 0.1)
        assert not m.read(0x417, 1)[0] & 0x20, "NumLock is still on"
        hold(g, False)
        until(g, lambda: not fifo(g), "the keypad turn to be taken")
        assert ph(g, L.H_HD) == hd1 - 1 & 3, "keypad Ins did not turn the player left"
        print("  input: NumLock on, keypad Ins (AL '0', AH 52h) - a left turn", flush=True)
        # --- a double tap is a U-turn (N -> W -> S) -------------------------------------
        M.guest_sleep(m, 0.5)                   # (room: the U-turn runs back beside
        hold(g, True)                           # the trail it just laid)
        M.guest_sleep(m, 0.1)
        nt, nd = g.w("it_nturn"), g.w("it_ndrop")
        u0, d0, hdb = ph(g, L.H_U), ph(g, L.H_D), ph(g, L.H_HD)
        tap_exact(m, "Insert")
        M.guest_sleep(m, 0.08)
        tap_exact(m, "Insert")
        M.guest_sleep(m, 0.1)
        assert fifo(g) == [L.K_LEFT, L.K_LEFT] and g.w("it_nturn") - nt == 2 \
            and g.w("it_ndrop") == nd, "a double tap posted %r" % fifo(g)
        hold(g, False)
        until(g, lambda: not fifo(g), "the two turns to be taken")
        hold(g, True)
        u1, d1, hda = ph(g, L.H_U), ph(g, L.H_D), ph(g, L.H_HD)
        assert hda == (hdb + 2) & 3, "a double tap left the player heading %d from %d" % (hda, hdb)
        print("  input: a double tap of Ins - two turns, a U-turn (heading %d -> %d, from "
              "(%d, %d) to (%d, %d))" % (hdb, hda, u0, d0, u1, d1), flush=True)
        fresh_round(g)
        # --- Up and Down are F10 and F9 --------------------------------------------------
        hold(g, True)
        for _ in range(3):
            tap_exact(m, "ArrowUp")
            M.guest_sleep(m, 0.05)
        M.guest_sleep(m, 0.1)
        assert g.b("it_kboost") == 3, "Up x3 posted %d boosts" % g.b("it_kboost")
        hold(g, False)
        until(g, lambda: g.b("it_pspeed") == P_NORMAL + 3, "Up x3 to make p 5")
        hold(g, True)
        tap_exact(m, "ArrowDown")
        M.guest_sleep(m, 0.1)
        assert g.b("it_kf9") == 1, "Down did not post F9's normal"
        hold(g, False)
        until(g, lambda: g.b("it_pspeed") == P_NORMAL, "Down to make p 2")
        print("  input: Up x3 - three boosts, p 5; Down - F9, p 2", flush=True)
        fresh_round(g)
        # --- a toggle resets the tracking ---------------------------------------------------
        hold(g, True)
        tap_exact(m, "Delete")
        M.guest_sleep(m, 0.1)
        assert g.b("it_tkey") == 0x53, "Del is not the tracked key (%02x)" % g.b("it_tkey")
        T.fs_enter(ui, g, "F")
        assert g.b("it_tkey") == 0, "full screen kept the tracked key"
        tap_exact(m, "Delete")
        M.guest_sleep(m, 0.1)
        assert g.b("it_tkey") == 0x53, "Del in full screen is not tracked"
        T.fs_leave(ui, g, "F")
        assert g.b("it_tkey") == 0, "the way back kept the tracked key"
        hold(g, False)
        T.to_title(g)
        print("  input: a toggle forgets the tracked turn key, both ways", flush=True)
    print("isotroninput: ok")


# =============================================================================
# PACE
# =============================================================================

def pace_window(g, bss, secs, fs):
    """Trace the wake (frame) ends of a round in play, from the first that has
    30 iterations in, for `secs` guest seconds: those hits."""
    m = g.m
    at = g.base + g.code["it_fs_frame" if fs else "it_wake1"]
    hits = []

    def onhit(mm, rec):
        hits.append((rec["cycles"], bss.snap(mm), ticks(mm)))

    def done():
        h = [x for x in hits if x[1].w("it_iter") >= 30]
        return bool(h) and h[-1][0] - h[0][0] >= secs * M.GUEST_HZ
    with M.bp_trace(m, at, on_hit=onhit, cap=20000) as tr:
        tr.until(done, "%.1f guest seconds" % secs, limit=300)
    h = [x for x in hits if x[1].w("it_iter") >= 30]
    k = next(i for i, x in enumerate(h) if x[0] - h[0][0] >= secs * M.GUEST_HZ)
    return h[:k + 1]


def sigma_cost(tier, h, a, b):
    """The u the iterations between snaps a and b were charged (every head alive,
    the player going straight)."""
    cfg = S.TIERS[tier]
    its = b.w("it_iter") - a.w("it_iter")
    pa = (a.b("it_heads", L.MAXHEADS * L.H_SIZE + L.H_U), a.b("it_heads", L.MAXHEADS * L.H_SIZE + L.H_D))
    pb = (b.b("it_heads", L.MAXHEADS * L.H_SIZE + L.H_U), b.b("it_heads", L.MAXHEADS * L.H_SIZE + L.H_D))
    cells = abs(pb[0] - pa[0]) + abs(pb[1] - pa[1])
    return its * (cfg["A"] + cfg["B"] * h) + cfg["C"] * cells


def credited(tier, h, a, b):
    """What the governor credited between two wake ends: the costs it paid
    plus the debt's change (debt = now - V)."""
    return sigma_cost(tier, h, a, b) + b.sw("it_debt") - a.sw("it_debt")


def pace_case(g, bss, name, tier, h, secs, fs):
    hits = pace_window(g, bss, secs, fs)
    (c0, a, t0), (c1, b, t1) = hits[0], hits[-1]
    for _, s, _ in hits:
        assert s.b("it_state") == L.ST_PLAY and s.b("it_live") == h \
            and s.b("it_pspeed") == P_NORMAL and s.b("it_heads", L.MAXHEADS * L.H_SIZE + H_ALIVE), \
            "%s: a head died or p moved in the window (state %d, live %d, p %d, player " \
            "alive %d, iteration %d of the window's %d..%d)" \
            % (name, s.b("it_state"), s.b("it_live"), s.b("it_pspeed"),
               s.b("it_heads", L.MAXHEADS * L.H_SIZE + H_ALIVE), s.w("it_iter"),
               a.w("it_iter"), b.w("it_iter"))
    its = b.w("it_iter") - a.w("it_iter")
    secs_g = (c1 - c0) / M.GUEST_HZ
    rate = its / secs_g
    want = S.Pace.rates(tier, h, P_NORMAL)[0]
    err = (rate - want) / want
    u = credited(tier, h, a, b)
    # the clock: 768 a tick, and in a bracket 256 a phase, to within a tick
    clock = 768 * (t1 - t0)
    if fs:
        clock += 256 * (b.b("it_fsph") - a.b("it_fsph"))
    assert abs(u - clock) <= 768, "%s: the governor credited %d u over %d ticks (%d u)" \
        % (name, u, t1 - t0, clock)
    assert abs(err) <= 0.02, "%s %s: %.2f iterations a guest second, the model %.2f (%+.2f%%)" \
        % ("full screen" if fs else "windowed", name, rate, want, 100 * err)
    print("  pace: %-10s %-11s %4d iterations in %.3f guest s = %6.2f/s, the model %6.2f "
          "(%+.2f%%); %d u credited over %d ticks"
          % ("full screen" if fs else "windowed", name, its, secs_g, rate, want, 100 * err, u,
             t1 - t0), flush=True)
    return dict(mode="fs" if fs else "win", case=name, its=its, secs=secs_g, rate=rate,
                model=want, err=err)


def wake_credits(g, bss, act, n=12, poke=None):
    """Trace the wake and frame ends around `act`, and n more after it - from
    a round well under way (its whole present out, 10 iterations in).
    `poke` = (name, value): written at the first stop after `act` has run,
    FROM the stop - a byte written from the main thread while the pump ran
    the machine was lost (twice in ~12 runs, the stall never served and the
    byte read back 0)."""
    m = g.m
    until(g, lambda: g.b("it_state") == L.ST_PLAY and g.w("it_iter") >= 10, "10 iterations")
    ats = [g.base + g.code["it_wake1"], g.base + g.code["it_fs_frame"]]
    hits = []
    req = dict(go=False, done=False)

    def onhit(mm, rec):
        if poke and req["go"] and not req["done"]:
            mm.write(g.a(poke[0]), bytes([poke[1]]))
            req["done"] = True
        hits.append((rec["cycles"], rec["addr"], bss.snap(mm)))
    with M.bp_trace(m, *ats, on_hit=onhit, cap=20000) as tr:
        act()
        req["go"] = True
        if poke:
            tr.until(lambda: req["done"], "the poke", limit=60)
        k = len(hits)
        tr.until(lambda: len(hits) >= k + n, "%d wake ends" % n, limit=300)
    return hits


def iterating(hits, k0, tier, h, which=None):
    """(credit, index) of the first wake end from k0 on that ran iterations,
    the one before it of the same kind and no whole present between them."""
    for k in range(max(k0, 1), len(hits)):
        a, b = hits[k - 1][2], hits[k][2]
        if which is not None and (hits[k][1] != which or hits[k - 1][1] != which):
            continue
        if b.w("it_iter") == a.w("it_iter"):
            continue
        if b.w("it_fsn") != a.w("it_fsn") or b.w("it_nwhole") != a.w("it_nwhole"):
            continue
        return credited(tier, h, a, b), k
    return None, None


def mode(fs):
    return "full screen" if fs else "windowed"


def stall_case(g, bss, tier, h, fs):
    """A paced wake (frame) made to wait 10 real ticks (it_tstall): what it
    credits is DEBT_CAP's two ticks less the phase the last frame had paid -
    exactly - and never the ten."""
    n0 = g.w("it_nstall")
    hits = wake_credits(g, bss, lambda: None, poke=("it_tstall", 10))
    ks = [k for k in range(1, len(hits))
          if hits[k][2].w("it_nstall") != hits[k - 1][2].w("it_nstall")]
    assert ks and g.w("it_nstall") == n0 + 1, "the forced stall was never served (state %d " \
        "paused %d tpause %d iter %d tstall %d vsync %d full %d flags %02x)" % (
            g.b("it_state"), g.b("it_paused"), g.b("it_tpause"), g.w("it_iter"),
            g.b("it_tstall"), g.b("it_vsync"), g.b("it_full"), g.b("it_tflags"))
    k = ks[0]
    a, b = hits[k - 1], hits[k]
    waited = (b[0] - a[0]) / TICK_CYC
    u = credited(tier, h, a[2], b[2])
    want = 2 * 768 - 256 * a[2].b("it_lastph")
    assert waited >= 9, "the stalled wake took only %.1f ticks" % waited   # (10 boundaries)
    assert u == want, "after a %.1f-tick stall the governor credited %d u, not %d - a stall " \
        "owes at most ONE tick past its own" % (waited, u, want)
    print("  pace: %-11s a forced stall of %.1f ticks credited %d u - its own tick and one "
          "owed, the other %d dropped" % (mode(fs), waited, u, int(waited) - 2), flush=True)
    return dict(waited=waited, u=u)


def resume_case(g, bss, tier, h, fs):
    """V = now at a resume: P, a guest second, P - the first wake that
    iterates after it credits at most a tick."""
    m = g.m

    def act():
        press(g, "KeyP", lambda: g.b("it_paused") == 1, "P to pause")
        M.guest_sleep(m, 1.0)
        press(g, "KeyP", lambda: g.b("it_paused") == 0, "P to resume")
    hits = wake_credits(g, bss, act)
    ks = [k for k in range(1, len(hits))
          if hits[k - 1][2].b("it_paused") and not hits[k][2].b("it_paused")]
    assert ks, "no wake saw the resume"
    u, k = iterating(hits, ks[0], tier, h)
    assert u is not None and u <= 768, "the first wake after a 1 s pause credited %s u" % u
    print("  pace: %-11s after a guest second paused the first wake credits %d u (V = now)"
          % (mode(fs), u), flush=True)
    return u


def toggle_case(g, ui, bss, tier, h):
    """V = now on both sides of a toggle, mid-round: the first full-screen
    frame that iterates after the entry's whole present, and the window's
    first wake after the way back (V taken once OSAPI_FSX_RUN returned)."""
    m = g.m
    W1, FF = g.base + g.code["it_wake1"], g.base + g.code["it_fs_frame"]

    def into():
        tap(m, "KeyF")
        until(g, lambda: g.b("it_fsm") == 1 and not g.fs_owed(), "F into full screen")
    hits = wake_credits(g, bss, into)
    u1, _ = iterating(hits, 1, tier, h, which=FF)
    assert u1 is not None and u1 <= 768, "the first frame in full screen credited %s u" % u1

    def back():
        n0 = g.w("it_fsout")
        tap(m, "KeyF")
        until(g, lambda: g.b("it_fsm") == 0 and g.w("it_fsout") > n0, "F out of full screen")
    hits = wake_credits(g, bss, back)
    k = next(k for k in range(1, len(hits)) if hits[k][1] == W1 and hits[k - 1][1] == FF)
    # the sync on the way back zeroed the debt: what the first wake credited
    # is its iterations' cost plus the debt it left
    u2 = sigma_cost(tier, h, hits[k - 1][2], hits[k][2]) + hits[k][2].sw("it_debt")
    assert u2 <= 768, "the first wake back credited %d u" % u2
    print("  pace: toggled mid-round - the first frame in full screen credits %d u, the "
          "first wake back %d u (V = now on both sides)" % (u1, u2), flush=True)
    T.presented(ui, g)


def held_round(g, tier):
    """A harness round in play (NOFLOW, HOLD, paced, nobody steering)."""
    T.start_game(g, SEED, tier, L.TF_NOFLOW | L.TF_HOLD)
    until(g, lambda: g.b("it_state") == L.ST_PLAY, "a round in play")


def next_round(g, h):
    """The held round's end, then the next at height h (poked)."""
    m = g.m
    until(g, lambda: g.b("it_tpause") == 1, "the round's end", guest=30)
    r = g.w("it_round")
    m.pause()
    g.put("it_h", h)
    g.put("it_tpause", 0)
    m.run()
    until(g, lambda: g.b("it_state") == L.ST_PLAY and g.w("it_round") == r
          and g.b("it_hplayed") == h, "the next round in play")


def pace_leg():
    out = []
    with boot() as ui:
        g = open_game(ui)
        bss = Bss(g)
        cases = (("Rookie h1", 1, 1, 2.0), ("Rookie h2", 1, 2, 3.0), ("Lunatic h1", 3, 1, 1.0))
        for fs in (False, True):
            if fs:
                T.fs_enter(ui, g, "F")
            held_round(g, 1)
            out.append(pace_case(g, bss, *cases[0], fs=fs))
            next_round(g, 2)
            out.append(pace_case(g, bss, *cases[1], fs=fs))
            T.to_title(g)
            held_round(g, 3)
            out.append(pace_case(g, bss, *cases[2], fs=fs))
            T.to_title(g)
            held_round(g, 1)
            stall_case(g, bss, 1, 1, fs)
            next_round(g, 1)
            resume_case(g, bss, 1, 1, fs)
            T.to_title(g)
            if not fs:
                held_round(g, 1)
                toggle_case(g, ui, bss, 1, 1)
                T.to_title(g)
        T.fs_leave(ui, g, "esc")
    worst = max(abs(r["err"]) for r in out)
    print("isotronpace: ok (%d cases, worst %.2f%%)" % (len(out), 100 * worst))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--sound", action="store_true", help="the isotronsound row")
    ap.add_argument("--input", action="store_true", help="the isotroninput row")
    ap.add_argument("--pace", action="store_true", help="the isotronpace row")
    a = ap.parse_args()
    if not (a.sound or a.input or a.pace):
        ap.print_help()
        return 2
    if a.sound:
        sound_leg()
    if a.input:
        input_leg()
    if a.pace:
        pace_leg()
    return 0


if __name__ == "__main__":
    sys.exit(main())
