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
        choose, an index into mr_rates - the row takes 11,025 Hz): the stream open (the external ring
        this DSP 2.1 plays in place, SPEC.md 105.8.4), no underrun after the first,
        the stream advancing at its rate, and the same pitch-class agreement
  spk   no card, the speaker in its bracket at 5.5 kHz: the ring kept at
        least half full (the synth keeps ahead of the pulses, SPEC.md
        105.7.1), CONS advancing at the rate, the agreement on the low-passed
        capture
  covox no card, a Covox on LPT2 (MartyPC's Covox machine, `make
        covoxtest`'s disk - SPEC.md 34.14, 105.8.7), Automatic: the ISR's
        road at 6,214 Hz on this 5150 (34.14.3) - os88spk_isrd, the ring's
        table the identity, spk's checks on the ring - then the agreement on
        the DAC's OWN capture, and the speaker's capture silent
  covoxpoll  the same with a rate chosen in Settings, which on an XT asks for
        the POLLED play (SPEC.md 34.14.3): no ISR, IRQ0 the owed-tick
        counter os88spk_ptick, 6,520 Hz, the polled ring never starved and
        played at the rate by the GUEST'S CYCLES (the BIOS tick is owed
        while it plays, so it cannot be the clock); then a pause, which must
        PAY the ticks - the BIOS clock caught up with the cycles to within a
        tick a second - and a resume; then the same agreement
  tone  no card, "Play in Background": one square wave on the desktop, its
        frequency always the highest melodic note the reference has sounding
  end   the AdLib profile: INTRO looped (Loop on - the same song again, from
        its start), then Loop off and the song's end advances the playlist
  sbmem the SB profile, THE RINGS' LIFETIMES (SPEC.md 105.8.4, 105.8.3,
        KERN-MEGA-PLAN 14.59): the external ring is held while a song plays
        and is GONE from the claim map at Stop; the speaker's ring is held
        through a pause and gone at the Stop after it, gone at Esc in the
        bracket, and not held while the card plays; then tests/filler takes
        the heap until no claim of the external ring's size can be funded,
        and the card plays on the GRANT ring instead; the filler's T takes
        the rest, and the card's Play and the speaker's are REFUSED -
        nothing open, nothing held, each with its own message - and with the
        filler closed both play again. Its apps disk is its own
        720KB one (mrwt720.img's shape): MIDIRack and tests/filler in APPS,
        the two songs in MEDIA

The pitch check is the oracle: the reference sequencer is the one
tests/unit/t_midtab.py holds to the composer's own independent reader, so the
machine is judged against arithmetic it did not do. --break on any arm
corrupts the song the reference reads (a transposition) and the arm must FAIL.
"""
import argparse
import math
import os
import subprocess
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
    "covoxpoll": ("os8088_5150_herc_covox_720_gla", "covox"),
    "sbmem": ("os8088_5150_herc_sb_720_gla", None),
}
OUT_MIDI, MRO_MIDI = 5, 5
OUT_WT, MRO_WT = 6, 6
OUT_LPT, MRO_LPT = 7, 7
FROM_START = {"covoxpoll"}      # arms whose pitch check is the FIRST play (main)


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
    reference has sounding, at the best alignment within +-1 s. The play is
    the first onset in the last `tail` seconds, or in the whole capture for
    tail None."""
    rate, s, _ = sndcheck.load(wav)
    at = onset(s, rate, 0 if tail is None else max(0, len(s) - int(tail * rate)))
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
           "covox": "build/covoxsys720.img",
           "covoxpoll": "build/covoxsys720.img"}.get(arm, IMG)
    # ...and the wavetable's, `make mrwttest`'s apps disk: the synthetic bank
    # beside the package
    apps = "build/mrwt720.img" if arm == "wt" else APPS
    if arm == "sbmem":                  # ...and the filler beside it: the
        apps = os.path.join(os.path.dirname(cap), "apps.img")   # mrwt shape
        subprocess.run(["python3", "tools/os88disk.py", "-o", apps,
                        "--size", "720", "APPS:build/midirack.o88",
                        "APPS:build/filler.o88",
                        "MEDIA/MIDI:" + SONG, "MEDIA/MIDI:" + INTRO,
                        "--folder", "SYSTEM/APPDATA"], check=True,
                       stdout=subprocess.DEVNULL)
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
                                 "sbmem": OUT_SB, "wt": OUT_WT,
                                 "covox": OUT_LPT,
                                 "covoxpoll": OUT_LPT}.get(arm, OUT_SPK)]))
        if arm == "covoxpoll":
            p.put("mr_rate", bytes([1]))        # 8,000 chosen: "the most"
        if arm == "tone":
            p.put("mr_bg", b"\x01")
        if RATE:
            p.put("mr_rate", bytes([RATE]))     # Settings' rate, by index
        ui.menu_pick("Play", "Play")
        want = {"fm": MRO_FM, "sb": MRO_SB, "spk": MRO_SPK, "tone": MRO_TONE,
                "wt": MRO_WT, "covox": MRO_LPT, "covoxpoll": MRO_LPT,
                "sbmem": MRO_SB}
        M.until(ui.m, lambda _: p.b("mr_out") == want[arm], "the output open",
                poll=.1, limit=10)
        return {"fm": arm_fm, "sb": arm_sb, "spk": arm_spk,
                "tone": arm_tone, "wt": arm_wt, "sbmem": arm_sbmem,
                "covox": arm_covox, "covoxpoll": arm_covoxpoll}[arm](ui, p)


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


MR_ST_STOP = 0
MRB_KB = 17                         # (MRB_RL + 16 + 1023) / 1024, mrout.inc


def heap(m):
    """(the claims as (base, paragraphs, owner), the KB a claim could reach):
    every hole in the arena plus every PURGEABLE claim, since a refused claim
    sheds those and asks again (SPEC.md 50.6.2). Under MRB_KB, no claim of the
    ring's size can be funded however the kernel compacts or sheds."""
    import os88geom as g
    u = lambda b, i: b[i] | b[i + 1] << 8               # noqa: E731
    base = u(m.read(m.sym("mem_base"), 2), 0)
    top = u(m.read(m.sym("mem_top"), 2), 0)
    raw = m.read(m.sym("mem_tab"), g.MEM_MAX * g.MC_SIZE)
    rows = [(u(raw, i * g.MC_SIZE), u(raw, i * g.MC_SIZE + 2),
             u(raw, i * g.MC_SIZE + 4)) for i in range(g.MEM_MAX)
            if u(raw, i * g.MC_SIZE)]
    held = sum(n for b, n, o in rows
               if base <= b < top and not 0xFB <= o >> 8 <= 0xFE)
    return rows, (top - base - held) // 64


def arm_sbmem(ui, p):
    """THE TWO RINGS LIVE ONLY WHILE THEY PLAY, AND A REFUSAL IS A MESSAGE
    (SPEC.md 105.8.4, 105.8.3; KERN-MEGA-PLAN 14.55, 14.59): the card's
    external ring and the speaker's, each in the claim map while its output
    plays and gone at every stop; a heap that cannot fund the external ring
    plays on the GRANT ring instead; one that cannot fund either refuses the
    card's Play and the speaker's, each in words; and the memory back, both
    play again."""
    m = ui.m
    M.until(m, lambda _: p.b("mrb_open") == 1, "the card's stream", poll=.1,
            limit=10)                   # ([mr_out] is set before the open)
    seg = p.w("mrb_seg")
    if p.b("mrb_open") != 1 or p.b("mrb_grant") or not seg:
        fail("sbmem: want the EXTERNAL ring open (open %d grant %d seg %04X)"
             % (p.b("mrb_open"), p.b("mrb_grant"), seg))
    if not any(b == seg for b, _, _ in heap(m)[0]):
        fail("sbmem: the ring at %04X is not in the claim map" % seg)
    stop(ui, p)
    if p.w("mrb_seg") or any(b == seg for b, _, _ in heap(m)[0]):
        fail("sbmem: the ring is still held after Stop ([mrb_seg] %04X)"
             % p.w("mrb_seg"))
    print("PASS sbmem: the ring at %04X held while it played, freed at Stop"
          % seg, flush=True)
    # THE SPEAKER'S RING: claimed in its bracket, kept through a pause (the
    # queue is the resume), freed at the Stop after it and at Esc inside it
    spk_play(ui, p)
    ks = p.w("mrk_seg")
    m.type_text(" ")                    # to the desktop, paused
    M.until(m, lambda _: p.b("mr_state") == MR_ST_PAUSE, "the pause",
            poll=.1, limit=30)
    if p.w("mrk_seg") != ks:
        fail("sbmem: a PAUSED speaker play gave its ring up - the resume "
             "needs it")
    stop(ui, p)
    spk_gone(m, p, ks, "Stop after a pause")
    spk_play(ui, p)
    ks = p.w("mrk_seg")
    m.key("Escape")                     # Esc: a stop INSIDE the bracket
    M.until(m, lambda _: p.b("mr_state") == MR_ST_STOP, "the bracket's stop",
            poll=.1, limit=30)
    spk_gone(m, p, ks, "Esc in the bracket")
    # ...AND NOT IN SOUND BLASTER MODE: the card plays with no speaker ring
    card_play(ui, p, "the card after the speaker")
    if p.w("mrk_seg"):
        fail("sbmem: the speaker's ring %04X is held while the card plays"
             % p.w("mrk_seg"))
    print("PASS sbmem: the card plays with no speaker ring held", flush=True)
    stop(ui, p)
    # THE HEAP TAKEN: each key is one more fill and an ask (tests/filler),
    # until nothing of the ring's size could be funded at all
    fl = ui.path("FILLER.O88")         # the acting Disk window, B:/APPS
    for _ in range(8):
        if heap(m)[1] < MRB_KB:
            break
        ui.raise_window(fl)
        m.key("KeyA")
        M.guest_sleep(m, 1.0)
    reach = heap(m)[1]
    if reach >= MRB_KB:
        fail("sbmem: the filler left %d KB reachable - the refusal cannot be "
             "forced on this machine" % reach)
    # NO EXTERNAL RING TO BE HAD: the grant ring plays instead (14.59)
    ui.raise_window(ui.window("MIDIRack"))
    ui.menu_pick("Play", "Play")
    M.until(m, lambda _: p.b("mrb_open") == 1 or not p.b("mr_out") and
            p.w("mr_msg") in (p.s["mr_s_ringmem"], p.s["mr_s_sberr"]),
            "the card's answer", poll=.1, limit=10)
    if (p.b("mrb_open") != 1 or p.b("mrb_grant") != 1 or p.w("mrb_seg")
            or p.b("mr_out") != MRO_SB):
        msg = bytes(m.read(p.w("mr_msg") + p.seg * 16, 64)).split(b"\0")[0]
        fail("sbmem: with %d KB reachable, want the GRANT ring (open %d "
             "grant %d seg %04X out %d, %r)" % (reach, p.b("mrb_open"),
                                                p.b("mrb_grant"),
                                                p.w("mrb_seg"), p.b("mr_out"),
                                                msg))
    g0 = p.w("mrb_gcons")
    M.guest_sleep(m, 1.0)
    if p.w("mrb_gcons") == g0:
        fail("sbmem: the grant ring opened but the card consumed nothing")
    print("PASS sbmem: with %d KB reachable the external ring was refused "
          "and the GRANT ring (%d bytes) plays" % (reach, p.w("mrb_grl")),
          flush=True)
    stop(ui, p)
    # ...AND THE REST TAKEN ('T'): neither ring, so both Plays refuse in words
    ui.raise_window(fl)
    m.key("KeyT")
    M.guest_sleep(m, 1.0)
    reach = heap(m)[1]
    if reach >= 4:
        fail("sbmem: 'T' left %d KB reachable - a 4 KB grant can still be "
             "funded" % reach)
    ui.raise_window(ui.window("MIDIRack"))
    refused(ui, p, OUT_SB, "mr_s_ringmem", reach)
    refused(ui, p, OUT_SPK, "mr_s_spkmem", reach)
    # ...AND THE MEMORY BACK: the next Play works, on both
    ui.close(fl)
    ui.raise_window(ui.window("MIDIRack"))
    card_play(ui, p, "the card with the filler closed")
    c0 = bytes(m.readseg(p.w("mrb_seg"), 16384 + 2, 2))
    M.guest_sleep(m, 1.0)
    c1 = bytes(m.readseg(p.w("mrb_seg"), 16384 + 2, 2))
    if c0 == c1:
        fail("sbmem: the card opened again but consumed nothing")
    print("PASS sbmem: with the filler closed, the next Play opens the card "
          "(ring at %04X) and it plays" % p.w("mrb_seg"), flush=True)
    stop(ui, p)
    spk_play(ui, p)
    ks = p.w("mrk_seg")
    m.key("Escape")
    M.until(m, lambda _: p.b("mr_state") == MR_ST_STOP, "the bracket's stop",
            poll=.1, limit=30)
    spk_gone(m, p, ks, "Esc, with the memory back")
    return 0.0


MR_ST_PAUSE = 2


def stop(ui, p):
    ui.raise_window(ui.window("MIDIRack"))
    ui.menu_pick("Play", "Stop")
    M.until(ui.m, lambda _: p.b("mr_state") == MR_ST_STOP and not
            p.b("mr_out"), "the stop", poll=.1, limit=10)


def card_play(ui, p, what):
    """mr_want the DSP synth, Play, the EXTERNAL ring open."""
    p.put("mr_want", bytes([OUT_SB]))
    ui.raise_window(ui.window("MIDIRack"))
    ui.menu_pick("Play", "Play")
    M.until(ui.m, lambda _: p.b("mr_out") == MRO_SB and p.b("mrb_open") == 1,
            what, poll=.1, limit=10)
    if p.b("mrb_grant") or not p.w("mrb_seg"):
        fail("sbmem: %s is not on the external ring (grant %d seg %04X)"
             % (what, p.b("mrb_grant"), p.w("mrb_seg")))


def spk_play(ui, p):
    """mr_want the speaker, Play: the bracket up, its ring claimed and in the
    claim map, the pulses consuming it."""
    m = ui.m
    p.put("mr_want", bytes([OUT_SPK]))
    ui.raise_window(ui.window("MIDIRack"))
    ui.menu_pick("Play", "Play")

    def cons():
        ks = p.w("mrk_seg")
        if not ks:
            return 0
        b = m.readseg(ks, 4096 + 2, 2)
        return b[0] | b[1] << 8
    M.until(m, lambda _: p.b("mr_out") == MRO_SPK and cons() > 0,
            "the speaker's first pulses", poll=.05, limit=20)
    ks = p.w("mrk_seg")
    if not any(b == ks for b, _, _ in heap(m)[0]):
        fail("sbmem: the speaker's ring at %04X is not in the claim map" % ks)
    print("PASS sbmem: the speaker plays out of its ring at %04X" % ks,
          flush=True)


def spk_gone(m, p, ks, how):
    if p.w("mrk_seg") or any(b == ks for b, _, _ in heap(m)[0]):
        fail("sbmem: the speaker's ring is still held after %s ([mrk_seg] "
             "%04X, was %04X)" % (how, p.w("mrk_seg"), ks))
    print("PASS sbmem: the speaker's ring at %04X freed at %s" % (ks, how),
          flush=True)


def refused(ui, p, out, msg, reach):
    m = ui.m
    p.put("mr_want", bytes([out]))
    p.put("mr_msg", b"\0\0")
    ui.menu_pick("Play", "Play")
    M.until(m, lambda _: p.w("mr_msg") == p.s[msg], "the refusal's message",
            poll=.1, limit=10)
    if (p.b("mr_out") or p.b("mr_state") != MR_ST_STOP or p.w("mrb_seg")
            or p.b("mrb_open") or p.b("mrb_grant") or p.w("mrk_seg")):
        fail("sbmem: refused, but out %d state %d seg %04X open %d grant %d "
             "speaker %04X" % (p.b("mr_out"), p.b("mr_state"),
                               p.w("mrb_seg"), p.b("mrb_open"),
                               p.b("mrb_grant"), p.w("mrk_seg")))
    text = bytes(m.read(p.addr(msg), 64)).split(b"\0")[0]
    print("PASS sbmem: with %d KB reachable Play is refused: %r, nothing "
          "open or held" % (reach, text.decode()), flush=True)


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


GUEST_HZ = 4772727.0


def cycles(m):
    return m.status()["cycles"]


def arm_covoxpoll(ui, p):
    """THE POLLED COVOX (SPEC.md 34.14.3, 105.8.7): no ISR a sample - IRQ0 is
    os88spk_ptick, owing the kernel's tick - so the clock is the guest's
    cycle counter, and the BIOS tick is checked only after a pause has paid
    it"""
    if p.b("mr_haslpt") != 1 or p.w("mr_lptport") != 0x378:
        fail("covox: MIDIRack has no Covox (haslpt %d, port %03X) - the "
             "Sound page's tier on this disk is LPT2 at 378h"
             % (p.b("mr_haslpt"), p.w("mr_lptport")))
    M.until(ui.m, lambda _: p.b("os88spk_pon") == 1
            and p.w("os88spk_pcons") > 0, "the polled play", poll=.05,
            limit=30)
    if p.b("mrk_pol") != 1:
        fail("covox: the play is not the polled one on a 5150")
    rate = p.w("mrk_rate")
    if rate != 6520:
        fail("covox: the polled play is at %d Hz, want 6,520 (N = 183)"
             % rate)
    vec = ui.m.read(0x20, 4)
    off, seg = vec[0] | vec[1] << 8, vec[2] | vec[3] << 8
    if (seg, off) != (p.seg, p.s["os88spk_ptick"]):
        fail("covox: IRQ0 is %04X:%04X, not os88spk_ptick at %04X:%04X"
             % (seg, off, p.seg, p.s["os88spk_ptick"]))

    def q():
        return (p.w("os88spk_ptot") - p.w("os88spk_pcons")) & 0xFFFF
    M.guest_sleep(ui.m, 1.0)
    c0, y0, k0 = p.w("os88spk_pcons"), cycles(ui.m), bios(ui.m)
    owed0 = p.w("os88spk_powed")        # ticks owed since the play began
    low, played = 2048, 0
    prev = c0
    for _ in range(10):
        low = min(low, q())
        M.guest_sleep(ui.m, .5)
        c = p.w("os88spk_pcons")
        played += (c - prev) & 0xFFFF
        prev = c
    secs = (cycles(ui.m) - y0) / GUEST_HZ
    if low < 128:
        fail("covox: the polled ring fell to %d - the synth is behind the "
             "DAC" % low)
    if not (0.95 * secs * rate < played < 1.02 * secs * rate):
        fail("covox: %d samples played in %.2f s of guest cycles at %d Hz "
             "(%.1f%%)" % (played, secs, rate, 100.0 * played / (secs * rate)))
    if bios(ui.m) != k0:
        fail("covox: the BIOS tick moved while the polled play held IRQ0 - "
             "the tick is not being owed")
    print("PASS covox: polled, %d Hz, IRQ0 at os88spk_ptick, the ring never "
          "under %d, %d played in %.2f s (%.1f%% of the rate), the BIOS "
          "tick held" % (rate, low, played, secs,
                         100.0 * played / (secs * rate)), flush=True)
    # A PAUSE PAYS THE TICKS: Space, and the BIOS clock must have moved by
    # what the play owed when k0 was read plus the guest's own cycles since,
    # to within one tick a second
    ui.m.type_text(" ")
    M.until(ui.m, lambda _: p.b("mr_state") == 2, "the pause", poll=.1,
            limit=30)
    secs = (cycles(ui.m) - y0) / GUEST_HZ
    ticks = (bios(ui.m) - k0) & 0xFFFF
    want = owed0 + secs * 18.2065
    if abs(ticks - want) > max(2.0, secs):
        fail("covox: after the pause the BIOS tick moved %d in %.2f s (want "
             "%.0f) - the owed ticks were not paid" % (ticks, secs, want))
    if p.b("os88spk_pon") != 0:
        fail("covox: the polled play is still open after the pause")
    print("PASS covox: the pause paid the ticks - %d in %.2f s of cycles "
          "(%.1f expected)" % (ticks, secs, want), flush=True)
    ui.m.type_text(" ")                         # ...and a resume plays on
    M.until(ui.m, lambda _: p.b("os88spk_pon") == 1, "the resume",
            poll=.05, limit=30)
    print("PASS covox: Space resumed the polled play", flush=True)
    M.guest_sleep(ui.m, 5.0)
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
    if p.w("mrk_rate") != 6214:
        fail("covox: Automatic on an XT plays %d Hz, want the ISR's 6,214"
             % p.w("mrk_rate"))
    print("PASS covox: the identity table, IRQ0 at os88spk_isrd, the DAC at "
          "378h", flush=True)
    return tail


def arm_tone(ui, p):
    # WAIT FOR THE PITCHES, not for a duration. This sampled `mrn_hz` sixteen
    # times a quarter of a guest second apart from wherever Play happened to
    # land, and BATTLE1's opening gives about three pitches in those four
    # seconds - so a pass saw exactly 3 and one run in three saw 2, a melody
    # note falling between samples. Every read now counts, at the finest poll
    # the harness gives, until three distinct pitches have SOUNDED; a tone
    # stuck on one pitch, or trilling between two, still fails - after a
    # budget in guest seconds that a loaded box cannot shorten.
    seen = set()

    def three(_):
        seen.add(p.w("mrn_hz"))
        seen.discard(0)
        return len(seen) >= 3
    try:
        M.until(ui.m, three, "three distinct pitches on the speaker",
                poll=.02, guest=30.0)
    except M.MartyError as e:
        fail("tone: the speaker sounded %d distinct pitches %s - %s"
             % (len(seen), sorted(seen), e))
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
        # THE REFERENCE IS THE SONG FROM ITS START, so the window must be a
        # play from its start. An arm's tail is that play everywhere but
        # covoxpoll, whose last stretch is the RESUME - the song from where
        # the pause landed - and whose capture ends where the HOST closed the
        # machine: anchored to the end it read 6, 7 or 8 of 10 on one build,
        # as those two moved. Its first polled play is the song from 0 and
        # reads 10 of 10 every run, so that is the play it is judged on
        span = None if a.arm in FROM_START else tail + 2.0
        hit, n = agreement(wav, ref, span, box, dec)
        need = {"fm": 6, "sb": 8, "spk": 5, "wt": 7, "covox": 7,
                "covoxpoll": 7}[a.arm]
        print("%s %s: %d of %d half-seconds' strongest pitch class sounding "
              "in the reference (need %d)" % ("PASS" if hit >= need else
                                              "FAIL", a.arm, hit, n, need),
              flush=True)
        if hit < need:
            return 1
        # THE NEGATIVE CONTROL, free: the same capture against the reference
        # a tritone away must NOT pass, or the oracle is not looking
        bad, _ = agreement(wav, spans(SONG, 6), span, box, dec)
        if bad >= need:
            print("FAIL %s: the tritone-transposed reference ALSO scores %d - "
                  "the pitch check cannot tell" % (a.arm, bad), flush=True)
            return 1
        print("PASS %s: the transposed reference scores %d (must stay under "
              "%d)" % (a.arm, bad, need), flush=True)
        if a.arm.startswith("covox"):
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
