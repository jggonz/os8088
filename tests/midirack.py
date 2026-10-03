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
  sb    the SB profile, the DSP synth (--rate N: at the rate Settings would
        choose, an index into mr_rates - the row takes 11,025 Hz): the stream open (a grant ring - this
        DSP has no auto-init, SPEC.md 105.8.4), no underrun after the first,
        the stream advancing at its rate, and the same pitch-class agreement
  spk   no card, the speaker in its bracket at 5.5 kHz: the ring kept at
        least half full (the synth keeps ahead of the pulses, SPEC.md
        105.7.1), CONS advancing at the rate, the agreement on the low-passed
        capture
  covox no card, a Covox on LPT2 (MartyPC's Covox machine, `make
        covoxtest`'s disk - SPEC.md 34.14, 105.8.7): spk's checks on the
        ring, the IRQ0 vector the Covox's ISR and the ring's table the
        identity, then the agreement on the DAC's OWN capture - and the
        speaker's capture silent, so the play went to the ladder and not
        the cone
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
    "mpu": ("os8088_5150_herc_mpu_720_gla", None),
    "wt": ("os8088_5150_herc_sb_720_gla", "sound_blaster"),
    "covox": ("os8088_5150_herc_covox_720_gla", "covox"),
}
OUT_MIDI, MRO_MIDI = 5, 5
OUT_WT, MRO_WT = 6, 6
OUT_LPT, MRO_LPT = 7, 7


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
    # the MPU-only machine boots `make miditest`'s disk: SOUND.DRV wanted by
    # its SYSTEM.CFG, since the kernel's boot sniff finds no FM chip there
    img = {"mpu": "build/midisys720.img",
           "covox": "build/covoxsys720.img"}.get(arm, IMG)
    # ...and the wavetable's, `make mrwttest`'s apps disk: the synthetic bank
    # beside the package
    apps = "build/mrwt720.img" if arm == "wt" else APPS
    with os88ui.boot(img, apps=apps, machine=machine) as ui:
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
        if arm == "mpu":
            return arm_mpu(ui, p, brk)
        if arm == "wt" and p.b("mr_hasbank") != 1:
            fail("wt: MIDIRack found no MIDIRACK.BNK beside it")
        p.put("mr_want", bytes([{"fm": OUT_OPL2, "sb": OUT_SB,
                                 "wt": OUT_WT,
                                 "covox": OUT_LPT}.get(arm, OUT_SPK)]))
        if arm == "tone":
            p.put("mr_bg", b"\x01")
        if RATE:
            p.put("mr_rate", bytes([RATE]))     # Settings' rate, by index
        ui.menu_pick("Play", "Play")
        want = {"fm": MRO_FM, "sb": MRO_SB, "spk": MRO_SPK, "tone": MRO_TONE,
                "wt": MRO_WT, "covox": MRO_LPT}
        M.until(ui.m, lambda _: p.b("mr_out") == want[arm], "the output open",
                poll=.1, limit=10)
        return {"fm": arm_fm, "sb": arm_sb, "spk": arm_spk,
                "tone": arm_tone, "wt": arm_wt,
                "covox": arm_covox}[arm](ui, p)


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
        b = ui.m.readseg(p.w("mrb_seg"), 16384 + 2, 2)
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
    if RATE and rate != RATES[RATE]:
        fail("SB: the card was opened at %d Hz, Settings chose %d"
             % (rate, RATES[RATE]))
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


def arm_wt(ui, p):
    """THE WAVETABLE (SPEC.md 105.8.6): the bank read, the samples mixed into
    the card's ring at the rate, and the card's own consumption - arm_sb's
    test, since the card and the ring are the synth's exactly. From the
    stream's OPENING: the bank is read off the floppy first, and the seconds
    that takes are not the mixer's."""
    M.until(ui.m, lambda _: p.b("mrb_open") == 1, "the wavetable's stream",
            poll=.2, limit=60)
    tail = arm_sb(ui, p)
    if p.w("mwt_bseg") == 0:
        fail("wt: the bank was never read")
    if p.w("mwt_xseg") == 0:
        fail("wt: no mix claim")
    print("PASS wt: the bank in a claim at %04X, %d voices" %
          (p.w("mwt_bseg"), p.b("mrv_n")), flush=True)
    return tail


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


def arm_covox(ui, p):
    """THE COVOX (SPEC.md 105.8.7, 34.14): the speaker's bracket and ring,
    with the port named - so the ring's table is the identity and IRQ0 is
    os88spk_isrd - and then arm_spk's own checks on the ring."""
    if p.b("mr_haslpt") != 1 or p.w("mr_lptport") != 0x378:
        fail("covox: MIDIRack has no Covox (haslpt %d, port %03X) - the "
             "Sound page's tier on this disk is LPT2 at 378h"
             % (p.b("mr_haslpt"), p.w("mr_lptport")))
    M.until(ui.m, lambda _: p.w("mrk_seg"), "the ring", poll=.05, limit=10)
    tab = bytes(ui.m.readseg(p.w("mrk_seg"), 4096 + 16, 256))
    if tab != bytes(range(256)):
        fail("covox: the ring's table is not the identity - os88spk built "
             "the speaker's counts")
    tail = arm_spk(ui, p)
    vec = ui.m.read(0x20, 4)
    off, seg = vec[0] | vec[1] << 8, vec[2] | vec[3] << 8
    if (seg, off) != (p.seg, p.s["os88spk_isrd"]):
        fail("covox: IRQ0 is %04X:%04X, not os88spk_isrd at %04X:%04X"
             % (seg, off, p.seg, p.s["os88spk_isrd"]))
    print("PASS covox: the identity table, IRQ0 at os88spk_isrd, the DAC at "
          "378h", flush=True)
    return tail


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


def midi_msgs(hexs, us):
    """The MPU's log as (seconds, message) - full status every time, which
    is what MIDIRack sends, and a SysEx whole."""
    b = bytes.fromhex(hexs)
    out, i = [], 0
    while i < len(b):
        s = b[i]
        if s == 0xF0:
            j = b.index(0xF7, i)
            out.append((us[i] / 1e6, b[i:j + 1]))
            i = j + 1
            continue
        if s < 0x80:
            fail("MIDI out: a data byte 0x%02x where a status was due, at "
                 "byte %d" % (s, i))
        n = 2 if (s & 0xF0) in (0xC0, 0xD0) else 3
        out.append((us[i] / 1e6, b[i:i + n]))
        i += n
    return out


def arm_mpu(ui, p, brk):
    """MIDI OUT (SPEC.md 105.8.5): the song's own events, byte for byte, at
    the song's own times, to the recording MPU-401 - with SOUND.DRV attached
    on the MPU alone, since this machine has no other card."""
    if p.b("mr_hasmpu") != 1:
        fail("mpu: MIDIRack found no MPU-401 - SOUND.DRV must attach on it "
             "alone (SPEC.md 34.13)")
    m = ui.m
    m.cmd(cmd="midi", reset=True)
    p.put("mr_want", bytes([OUT_MIDI]))
    ui.menu_pick("Play", "Play")
    M.until(m, lambda _: p.b("mr_out") == MRO_MIDI, "MIDI out open",
            poll=.1, limit=10)
    M.guest_sleep(m, 8.0)
    r = m.cmd(cmd="midi", reset=True)
    if not r["uart"]:
        fail("mpu: the interface was never put in UART mode")
    msgs = midi_msgs(r["bytes"], r["us"])
    if not msgs or bytes(msgs[0][1]) != bytes([0xF0, 0x7E, 0x7F, 0x09, 0x01,
                                               0xF7]):
        fail("mpu: the first thing sent was %r, want GM ON"
             % (msgs[0][1].hex() if msgs else None))
    chan = [(t, bytes(x)) for t, x in msgs if x[0] < 0xF0]
    ev, _ = om.timeline(open(SONG, "rb").read())
    ref = []
    for t, st, d in ev:
        d = bytes(d)
        if brk and st & 0xF0 in (0x80, 0x90):
            d = bytes([(d[0] + 6) & 0x7F]) + d[1:]
        ref.append((t, bytes([st]) + d))
    if len(chan) < 100:
        fail("mpu: only %d channel messages in 8 s of BATTLE1" % len(chan))
    # CONTENT: what the module was sent is the reference's own events. Events
    # on one tick in different tracks may legitimately come out in another
    # order, so the comparison is of MULTISETS over the first n, with a margin
    # at the edge of the window for a tick's worth of difference
    n = len(chan) - 20
    from collections import Counter
    sent = Counter(x for _, x in chan)
    want = Counter(x for _, x in ref[:n])
    missing = want - sent
    if missing:
        fail("mpu: %d of the reference's first %d messages were never sent, "
             "e.g. %s" % (sum(missing.values()), n,
                          [x.hex() for x in list(missing)[:4]]))
    # TIME: the k-th note-on of each (channel, key) against the reference's,
    # with the first note aligning the two clocks
    def ons(lst):
        seen, out = {}, {}
        for t, x in lst:
            if x[0] & 0xF0 == 0x90 and len(x) > 2 and x[2]:
                k = (x[0], x[1], seen.get((x[0], x[1]), 0))
                seen[(x[0], x[1])] = k[2] + 1
                out[k] = t
        return out
    a, b = ons(chan), ons(ref)
    common = [k for k in a if k in b]
    t0a = min(a[k] for k in common)
    t0b = min(b[k] for k in common)
    late = [abs((a[k] - t0a) - (b[k] - t0b)) for k in common]
    good = sum(1 for d in late if d <= 0.1)
    if good < 0.95 * len(common):
        fail("mpu: %d of %d note-ons within 0.1 s of the song's time (worst "
             "%.3f s)" % (good, len(common), max(late)))
    print("PASS mpu: GM ON first, %d channel messages, the reference's first "
          "%d all sent, %d of %d note-ons on time (worst %.3f s)"
          % (len(chan), n, good, len(common), max(late)), flush=True)
    # PAUSE silences every channel; RESUME tells each its program again
    m.key("Space")
    M.until(m, lambda _: p.b("mr_state") == 2, "paused", poll=.1, limit=10)
    M.guest_sleep(m, 0.5)
    r = m.cmd(cmd="midi", reset=True)
    got = {bytes(x) for _, x in midi_msgs(r["bytes"], r["us"])}
    off = [c for c in range(16) if bytes([0xB0 | c, 123, 0]) not in got]
    if off:
        fail("mpu: the pause sent no All Notes Off on channels %s" % off)
    m.key("Space")
    M.until(m, lambda _: p.b("mr_state") == 1, "resumed", poll=.1, limit=10)
    M.guest_sleep(m, 0.5)
    r = m.cmd(cmd="midi", reset=True)
    progs = {x[0] & 15 for _, x in midi_msgs(r["bytes"], r["us"])
             if x[0] & 0xF0 == 0xC0}
    if len(progs) < 16:
        fail("mpu: the resume told only channels %s their program"
             % sorted(progs))
    print("PASS mpu: a pause silences all sixteen channels and a resume "
          "tells each its program", flush=True)
    return None


RATES = [0, 8000, 11025, 16000, 22050, 32000, 44100]   # mr_rates (mrout.inc)
RATE = 0


def main():
    global RATE
    ap = argparse.ArgumentParser()
    ap.add_argument("--arm", required=True, choices=sorted(ARMS))
    ap.add_argument("--rate", type=int, default=0,
                    help="Settings' rate, an index into mr_rates (sb arm)")
    ap.add_argument("--break", dest="brk", action="store_true",
                    help="transpose the reference a tritone: must FAIL")
    a = ap.parse_args()
    RATE = a.rate
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
        need = {"fm": 6, "sb": 8, "spk": 5, "wt": 7, "covox": 7}[a.arm]
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
        if a.arm == "covox":
            # ...AND THE CONE SILENT: the door turned the speaker on for a
            # PWM nobody writes, and os88spk_go turns it off again
            rate, s, _ = sndcheck.load(cap + ".pc_speaker.wav")
            seg = s[-int((tail + 2.0) * rate):]
            m = sum(seg) / max(len(seg), 1)
            rms = math.sqrt(sum((x - m) ** 2 for x in seg) / max(len(seg), 1))
            if rms > 0.01:
                print("FAIL covox: the PC speaker sounded (RMS %.3f) while "
                      "the Covox played" % rms, flush=True)
                return 1
            print("PASS covox: the speaker silent (RMS %.4f) - the play went "
                  "to the ladder and not the cone" % rms, flush=True)
        return 0


if __name__ == "__main__":
    sys.exit(main())
