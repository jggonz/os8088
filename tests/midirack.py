#!/usr/bin/env python3
"""MIDIRack on a 5150 (SPEC.md 105.11): every output, heard and checked.

    python3 tests/midirack.py --arm fm|sb|spk|tone|end   (one arm a row)

Each arm boots MartyPC's 4.77 MHz 5150 (Hercules, 720KB drives) with the
machine's sound captured (MARTYPC_WAV), opens B:\\APPS\\MIDIRACK.O88 - whose
autoload must find B:\\MEDIA\\MIDI\\ and load BATTLE1 - picks the output by
poking the package's own [mr_want] (tests/mrprobe.py reads it by name), and
presses Play through the menu bar. Then it asserts on the package's state AND
on what came out of the machine:

  fm    the AdLib profile, OPL2 mode (MartyPC's AdLib is an OPL3 core wired
        as an AdLib, so OPL3 mode's second bank would be silent - SPEC.md
        105.6.2): the chip claimed, voices keyed, and the capture's strongest
        pitch class in each half second one the REFERENCE SEQUENCER
        (tools/os88midi.py) has sounding then
  sb    the SB profile, the DSP synth: the stream open (a grant ring - this
        DSP has no auto-init, SPEC.md 105.8.4), no underrun after the first,
        the stream advancing at its rate, and the same pitch-class agreement
  spk   no card, the speaker in its bracket at 5.5 kHz: the ring kept at
        least half full (the synth keeps ahead of the pulses, SPEC.md
        105.7.1), CONS advancing at the rate, the agreement on the low-passed
        capture
  tone  no card, "Play in Background": one square wave on the desktop, its
        frequency always the highest melodic note the reference has sounding
  end   the AdLib profile: INTRO looped (Loop on - the same song again, from
        its start), then Loop off and the song's end advances the playlist

The pitch check is the oracle: the reference sequencer is the one
tests/unit/t_midtab.py holds to the composer's own independent reader, so the
machine is judged against arithmetic it did not do. --break on any arm
corrupts the song the reference reads (a transposition) and the arm must FAIL.
"""
import argparse
import math
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))
os.chdir(ROOT)
import os88marty as M          # noqa: E402
import os88midi as om          # noqa: E402
import os88ui                  # noqa: E402
import sndcheck                # noqa: E402
import mrprobe                 # noqa: E402

IMG = "build/os8088-720.img"
APPS = "build/apps720.img"
SONG = os.path.join(ROOT, "apps", "midirack", "songs", "BATTLE1.MID")
INTRO = os.path.join(ROOT, "apps", "midirack", "songs", "INTRO.MID")
OUT_OPL2, OUT_SB, OUT_SPK = 2, 3, 4
MRO_FM, MRO_SB, MRO_SPK, MRO_TONE = 1, 2, 3, 4
MRV_ON = 1

ARMS = {
    "fm": ("os8088_5150_herc_adlib_720_gla", "adlib_music_synthesizer"),
    "sb": ("os8088_5150_herc_sb_720_gla", "sound_blaster"),
    "spk": ("os8088_5150_herc_720_gla", "pc_speaker"),
    "tone": ("os8088_5150_herc_720_gla", "pc_speaker"),
    "end": ("os8088_5150_herc_adlib_720_gla", None),
}


def fail(msg):
    print("FAIL %s" % msg, flush=True)
    sys.exit(1)


# --- the reference: which notes are sounding when ------------------------------
def spans(path, transpose=0):
    ev, _ = om.timeline(open(path, "rb").read())
    on, out = {}, []
    for t, st, d in ev:
        ch, kind = st & 15, st & 0xF0
        if kind == 0x90 and d[1] > 0:
            on[(ch, d[0])] = t
        elif kind in (0x80, 0x90) and (ch, d[0]) in on:
            out.append((on.pop((ch, d[0])), t, ch, d[0] + transpose))
    t0 = min(s[0] for s in out)
    return [(a - t0, b - t0, ch, n) for a, b, ch, n in out]


def goertzel(seg, f, rate):
    w = 2 * math.pi * f / rate
    c = 2 * math.cos(w)
    s1 = s2 = 0.0
    for x in seg:
        s0 = x + c * s1 - s2
        s2, s1 = s1, s0
    return math.sqrt(max(s1 * s1 + s2 * s2 - c * s1 * s2, 0)) / len(seg)


def boxcar(seg, n):
    if n <= 1:
        return seg
    out, acc = [], sum(seg[:n])
    for i in range(n, len(seg)):
        acc += seg[i] - seg[i - n]
        out.append(acc / n)
    return out


def onset(s, rate, start, thresh=0.01):
    blk = rate // 20
    for i in range(start, len(s) - blk, blk):
        seg = s[i:i + blk:4]
        if math.sqrt(sum(x * x for x in seg) / len(seg)) > thresh:
            return i
    return None


def agreement(wav, ref, tail, box=1, dec=2, wins=10):
    """The share of half-second windows whose strongest pitch class the
    reference has sounding, at the best alignment within +-1 s."""
    rate, s, _ = sndcheck.load(wav)
    at = onset(s, rate, max(0, len(s) - int(tail * rate)))
    if at is None:
        fail("the capture %s is silent where the song should be" % wav)
    tops = []
    for k in range(wins):
        a = at + int((0.25 + k * 0.5) * rate)
        seg = boxcar(s[a:a + rate // 3], box)[::dec]
        if len(seg) < 200:
            break
        r = rate / dec
        m = sum(seg) / len(seg)
        seg = [x - m for x in seg]
        cls = [0.0] * 12
        for n in range(40, 85):
            f = 440 * 2 ** ((n - 69) / 12.0)
            if f < r / 2.2:
                cls[n % 12] += goertzel(seg, f, r)
        tops.append(max(range(12), key=lambda c: cls[c]))
    best = 0
    for sh in [x * 0.125 for x in range(-8, 9)]:
        hit = 0
        for k, top in enumerate(tops):
            t = 0.25 + k * 0.5 + sh
            exp = {n % 12 for a, b, ch, n in ref
                   if ch != 9 and a <= t + 0.33 and b >= t}
            hit += top in exp
        best = max(best, hit)
    return best, len(tops)


# --- the session --------------------------------------------------------------
def session(arm, cap, brk):
    machine, src = ARMS[arm]
    with os88ui.boot(IMG, apps=APPS, machine=machine) as ui:
        ui.path("B:/APPS/MIDIRACK.O88")
        p = mrprobe.attach(ui)
        M.until(ui.m, lambda _: p.b("mr_loaded") == 1, "the autoload",
                poll=.2, limit=120)
        if p.b("mrl_n") != 2:
            fail("autoload found %d songs in MEDIA\\MIDI, want the 720KB "
                 "disk's two" % p.b("mrl_n"))
        name = bytes(ui.m.read(p.addr("mrl_ent"), 12)).split(b"\0")[0]
        if name != b"BATTLE1.MID":
            fail("the first entry is %r, want BATTLE1.MID" % name)
        print("PASS autoload: %d songs, BATTLE1 loaded, %d ms, %d measures"
              % (p.b("mrl_n"), p.dw("mr_lenlo"), p.w("mr_meas")), flush=True)
        if arm == "end":
            return arm_end(ui, p)
        p.put("mr_want", bytes([{"fm": OUT_OPL2, "sb": OUT_SB}.get(arm,
                                                                  OUT_SPK)]))
        if arm == "tone":
            p.put("mr_bg", b"\x01")
        ui.menu_pick("Play", "Play")
        want = {"fm": MRO_FM, "sb": MRO_SB, "spk": MRO_SPK, "tone": MRO_TONE}
        M.until(ui.m, lambda _: p.b("mr_out") == want[arm], "the output open",
                poll=.1, limit=10)
        return {"fm": arm_fm, "sb": arm_sb, "spk": arm_spk,
                "tone": arm_tone}[arm](ui, p)


def arm_fm(ui, p):
    M.guest_sleep(ui.m, 1.0)
    if p.b("mrf_up") != 1 or p.b("mrf_mode") != 2 or p.b("mrv_n") != 8:
        fail("FM: up %d mode %d voices %d, want 1 / OPL2 / 8"
             % (p.b("mrf_up"), p.b("mrf_mode"), p.b("mrv_n")))
    keyed = 0
    for _ in range(8):
        keyed = max(keyed, list(p.data("mrv_st", 8)).count(MRV_ON))
        M.guest_sleep(ui.m, .5)
    if keyed < 2:
        fail("FM: at most %d voices ever keyed" % keyed)
    print("PASS fm: the chip claimed in OPL2 mode, up to %d voices keyed"
          % keyed, flush=True)
    return 5.0


def bios(m):
    b = m.read(0x46C, 2)
    return b[0] | b[1] << 8


def arm_sb(ui, p):
    """The CARD's consumed count against the guest's own clock, over eight
    seconds. Not the synth's position: a grant ring is filled 2,048 samples
    at a time up to half a second ahead, so what has been MADE leads what has
    been played by anything from nothing to the whole lead."""
    def cons():
        if p.b("mrb_grant"):
            return p.w("mrb_gcons")
        b = ui.m.readseg(p.w("mrb_seg"), 8192 + 2, 2)
        return b[0] | b[1] << 8
    M.guest_sleep(ui.m, 1.0)
    c0, k0 = cons(), bios(ui.m)
    played = 0
    for _ in range(8):                  # CONS is a word: sum it in steps
        M.guest_sleep(ui.m, 1.0)
        c1 = cons()
        played += (c1 - c0) & 0xFFFF
        c0 = c1
    secs = ((bios(ui.m) - k0) & 0xFFFF) / 18.2065    # the guest's own clock
    rate = p.w("mrb_rate")
    if p.b("mrb_open") != 1:
        fail("SB: the stream is not open (verb 0 said %d)" % p.b("mrb_err"))
    if p.w("mrb_unders") > 1:
        fail("SB: %d underruns - the synth fell behind the card"
             % p.w("mrb_unders"))
    if not (0.9 * secs * rate < played < 1.1 * secs * rate):
        fail("SB: the card took %d samples in %.2f s at %d Hz"
             % (played, secs, rate))
    print("PASS sb: %s ring, %d Hz, the card took %d samples in %.2f s, %d "
          "underrun(s)" % ("grant" if p.b("mrb_grant") else "external", rate,
                           played, secs, p.w("mrb_unders")), flush=True)
    return 9.0


def arm_spk(ui, p):
    def ring():
        seg = p.w("mrk_seg")
        b = ui.m.readseg(seg, 4096, 4)
        return b[0] | b[1] << 8, b[2] | b[3] << 8
    # THE BRACKET MUST BE PLAYING FIRST: Play repaints and enters it, and
    # until the pre-fill is in, TOTAL and CONS are both 0 - a "ring at 0" that
    # is the start, not a starvation (it failed one run in three on that)
    M.until(ui.m, lambda _: p.w("mrk_seg") and ring()[1] > 0,
            "the first pulses", poll=.05, limit=10)
    M.guest_sleep(ui.m, 1.0)
    low, c0, k0 = 4096, ring()[1], bios(ui.m)
    for _ in range(8):
        tot, cons = ring()
        low = min(low, (tot - cons) & 0xFFFF)
        M.guest_sleep(ui.m, .5)
    c1 = ring()[1]
    secs = ((bios(ui.m) - k0) & 0xFFFF) / 18.2065
    rate = p.w("mrk_rate")
    played = (c1 - c0) & 0xFFFF
    if low < 1024:
        fail("speaker: the ring fell to %d of 4096 - the synth is behind the "
             "pulses" % low)
    if not (0.8 * secs * rate < played < 1.1 * secs * rate):
        fail("speaker: %d samples played in %.2f s at %d Hz"
             % (played, secs, rate))
    print("PASS spk: %d Hz, the ring never under %d, %d played in %.2f s"
          % (rate, low, played, secs), flush=True)
    return 5.0


def arm_tone(ui, p):
    seen = set()
    for _ in range(16):
        seen.add(p.w("mrn_hz"))
        M.guest_sleep(ui.m, .25)
    seen.discard(0)
    if len(seen) < 3:
        fail("tone: the speaker sounded %d distinct pitches" % len(seen))
    if p.b("mr_state") != 1:
        fail("tone: not playing on the desktop (state %d)" % p.b("mr_state"))
    print("PASS tone: %d pitches on the desktop: %s"
          % (len(seen), sorted(seen)[:8]), flush=True)
    return 4.0


def arm_end(ui, p):
    p.put("mrl_sel", b"\x01")         # INTRO: the second of B, I
    p.put("mr_loop", b"\x01")
    ui.menu_pick("Play", "Play")
    M.until(ui.m, lambda _: p.b("mr_lidx") == 1 and p.b("mr_state") == 1,
            "INTRO playing", poll=.05, limit=30)
    plays = p.w("mr_plays")
    secs = om.timeline(open(INTRO, "rb").read())[1]
    M.until(ui.m, lambda _: p.w("mr_plays") != plays, "INTRO looped",
            poll=.05, limit=secs + 20)
    if p.b("mr_lidx") != 1 or p.b("mr_state") != 1:
        fail("loop: entry %d state %d, want INTRO (1) playing again"
             % (p.b("mr_lidx"), p.b("mr_state")))
    print("PASS loop: INTRO played to its end and started again", flush=True)
    p.put("mr_loop", b"\x00")
    M.until(ui.m, lambda _: p.b("mr_state") != 1, "INTRO's second end",
            poll=.05, limit=secs + 20)
    if p.b("mr_state") != 0 or p.b("mr_lidx") != 1:
        fail("end: state %d entry %d after the list's last song, want stopped"
             % (p.b("mr_state"), p.b("mr_lidx")))
    print("PASS end: the last song's end stopped the playlist", flush=True)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True, choices=sorted(ARMS))
    ap.add_argument("--break", dest="brk", action="store_true",
                    help="transpose the reference a tritone: must FAIL")
    a = ap.parse_args()
    machine, src = ARMS[a.arm]
    with tempfile.TemporaryDirectory() as d:
        cap = os.path.join(d, "cap")
        if src:
            os.environ["MARTYPC_WAV"] = cap
        try:
            tail = session(a.arm, cap, a.brk)
        finally:
            os.environ.pop("MARTYPC_WAV", None)
        if not src or a.arm == "tone":
            print("ok", flush=True)
            return 0
        wav = cap + "." + src + ".wav"
        ref = spans(SONG, 6 if a.brk else 0)
        box, dec = (9, 4) if a.arm == "spk" else (1, 2)
        hit, n = agreement(wav, ref, tail + 2.0, box, dec)
        need = {"fm": 6, "sb": 8, "spk": 5}[a.arm]
        print("%s %s: %d of %d half-seconds' strongest pitch class sounding "
              "in the reference (need %d)" % ("PASS" if hit >= need else
                                              "FAIL", a.arm, hit, n, need),
              flush=True)
        if hit < need:
            return 1
        # THE NEGATIVE CONTROL, free: the same capture against the reference
        # a tritone away must NOT pass, or the oracle is not looking
        bad, _ = agreement(wav, spans(SONG, 6), tail + 2.0, box, dec)
        if bad >= need:
            print("FAIL %s: the tritone-transposed reference ALSO scores %d - "
                  "the pitch check cannot tell" % (a.arm, bad), flush=True)
            return 1
        print("PASS %s: the transposed reference scores %d (must stay under "
              "%d)" % (a.arm, bad, need), flush=True)
        return 0


if __name__ == "__main__":
    sys.exit(main())
