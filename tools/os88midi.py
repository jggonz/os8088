#!/usr/bin/env python3
"""os88midi.py - MIDIRack's generated tables, and its reference sequencer.

MIDIRack (SPEC.md 105, apps/midirack/) plays Standard MIDI Files through an
OPL2/OPL3, a Sound Blaster's DSP or the PC speaker. Everything it needs that is
ARITHMETIC rather than logic is made here, on the host, once:

    python3 tools/os88midi.py gen  [-o apps/midirack/mrtab.inc]
    python3 tools/os88midi.py check                     # the .inc is current
    python3 tools/os88midi.py play FILE.MID [--rate R]  # the reference timeline

`gen` writes apps/midirack/mrtab.inc - committed, the way apps/os88spkfx_t.inc
is, and `check` (a fast-tier row) refuses a stale one:

  mrt_fn     384 words  the OPL F-Number of each 1/32 of a semitone across
                        one octave, at BLOCK = octave - 1 (SPEC.md 105.6.2):
                        f * 2^21 / 49716 for f in MIDI octave 0, so one table
                        serves every octave and the block is the octave
  mrt_fq     384 words  the same pitches as frequency x 2048 (octave 0's
                        Hz, so 16,744..33,487): the synth's half-period is
                        (R << 18) / (F << octave) (SPEC.md 105.7.2)
  mrt_tl     128 bytes  linear MIDI level -> OPL attenuation in 0.75 dB steps,
                        the 40 log10 curve General MIDI specifies
  mrt_amp    128 bytes  linear MIDI level -> the synth's amplitude, 0..255,
                        the same curve as a square law
  mrt_gmname            the 128 General MIDI names, abbreviated to 15
  mrt_fmbank 128 x 12   an ORIGINAL two-operator bank (below), one patch a
                        program: modulator 20h/40h/60h/80h/E0h, carrier the
                        same, C0h, then a signed transpose in semitones
  mrt_fmdrum 47 x 12    GM drum keys 35..81: the same eleven bytes, then the
                        MIDI note the drum is struck at
  mrt_syn    128 bytes  the synth's voicing per program: bits 0-1 the duty
                        (50 / 25 / 12.5 %), bits 2-4 the envelope class,
                        bits 5-6 an octave lift
  mrt_syndr  47 x 4     the synth's drum per key: kind (0 tone / 1 noise),
                        pitch or noise colour, decay step, level
  mrt_env    8 x 4      the synth's envelope classes: attack step, decay
                        step, sustain level, release step - per 16 ms

THE FM BANK IS ORIGINAL. No bank of anyone else's is read, fetched or
transcribed: every patch below is built from a handful of family templates
and parameters written here. It will not sound like a Sound Canvas; it is
meant to sound like a 1990s AdLib game, which is what an OPL is for.
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_OUT = os.path.join(ROOT, 'apps', 'midirack', 'mrtab.inc')

# ---------------------------------------------------------------------------
# pitch
# ---------------------------------------------------------------------------
FINE = 32                                   # steps a semitone


def f0(x):
    """MIDI octave 0's frequency at semitone position x in [0, 12)."""
    return 440.0 * 2.0 ** ((x - 69.0) / 12.0)


def fn_table():
    out = []
    for i in range(12 * FINE):
        fn = int(round(f0(i / FINE) * (1 << 21) / 49716.0))
        assert 345 <= fn <= 690, fn
        out.append(fn)
    return out


def fq_table():
    out = []
    for i in range(12 * FINE):
        fq = int(round(f0(i / FINE) * 2048.0))
        assert 16744 <= fq < 33488, fq
        out.append(fq)
    return out


# ---------------------------------------------------------------------------
# levels
# ---------------------------------------------------------------------------
def tl_table():
    out = []
    for v in range(128):
        if v == 0:
            out.append(63)
            continue
        db = -40.0 * math.log10(v / 127.0)
        out.append(min(63, int(round(db / 0.75))))
    return out


def amp_table():
    return [int(round(255.0 * (v / 127.0) ** 2)) for v in range(128)]


# ---------------------------------------------------------------------------
# names
# ---------------------------------------------------------------------------
GM_NAMES = [
    'Acoustic Grand', 'Bright Piano', 'Electric Grand', 'Honky-tonk',
    'Electric Pno 1', 'Electric Pno 2', 'Harpsichord', 'Clavinet',
    'Celesta', 'Glockenspiel', 'Music Box', 'Vibraphone',
    'Marimba', 'Xylophone', 'Tubular Bells', 'Dulcimer',
    'Drawbar Organ', 'Perc. Organ', 'Rock Organ', 'Church Organ',
    'Reed Organ', 'Accordion', 'Harmonica', 'Tango Accordion',
    'Nylon Guitar', 'Steel Guitar', 'Jazz Guitar', 'Clean Guitar',
    'Muted Guitar', 'Overdrive Gtr', 'Distortion Gtr', 'Gtr Harmonics',
    'Acoustic Bass', 'Electric Bass', 'Picked Bass', 'Fretless Bass',
    'Slap Bass 1', 'Slap Bass 2', 'Synth Bass 1', 'Synth Bass 2',
    'Violin', 'Viola', 'Cello', 'Contrabass',
    'Tremolo Strings', 'Pizzicato', 'Orch. Harp', 'Timpani',
    'String Ensemble', 'Strings 2', 'Synth Strings 1', 'Synth Strings 2',
    'Choir Aahs', 'Voice Oohs', 'Synth Voice', 'Orchestra Hit',
    'Trumpet', 'Trombone', 'Tuba', 'Muted Trumpet',
    'French Horn', 'Brass Section', 'Synth Brass 1', 'Synth Brass 2',
    'Soprano Sax', 'Alto Sax', 'Tenor Sax', 'Baritone Sax',
    'Oboe', 'English Horn', 'Bassoon', 'Clarinet',
    'Piccolo', 'Flute', 'Recorder', 'Pan Flute',
    'Blown Bottle', 'Shakuhachi', 'Whistle', 'Ocarina',
    'Lead 1 (Square)', 'Lead 2 (Saw)', 'Lead 3 (Callio)', 'Lead 4 (Chiff)',
    'Lead 5 (Charang)', 'Lead 6 (Voice)', 'Lead 7 (Fifths)', 'Lead 8 (Bass)',
    'Pad 1 (New Age)', 'Pad 2 (Warm)', 'Pad 3 (Poly)', 'Pad 4 (Choir)',
    'Pad 5 (Bowed)', 'Pad 6 (Metal)', 'Pad 7 (Halo)', 'Pad 8 (Sweep)',
    'FX 1 (Rain)', 'FX 2 (Soundtrk)', 'FX 3 (Crystal)', 'FX 4 (Atmos)',
    'FX 5 (Bright)', 'FX 6 (Goblins)', 'FX 7 (Echoes)', 'FX 8 (Sci-fi)',
    'Sitar', 'Banjo', 'Shamisen', 'Koto',
    'Kalimba', 'Bagpipe', 'Fiddle', 'Shanai',
    'Tinkle Bell', 'Agogo', 'Steel Drums', 'Woodblock',
    'Taiko Drum', 'Melodic Tom', 'Synth Drum', 'Reverse Cymbal',
    'Fret Noise', 'Breath Noise', 'Seashore', 'Bird Tweet',
    'Telephone', 'Helicopter', 'Applause', 'Gunshot',
]
assert len(GM_NAMES) == 128 and max(len(n) for n in GM_NAMES) <= 16

DRUM_LO, DRUM_HI = 35, 81                   # the GM kit's keys, inclusive

# ---------------------------------------------------------------------------
# the FM bank - ORIGINAL, from family templates
# ---------------------------------------------------------------------------


def op(mult=1, tl=0, ar=15, dr=0, sl=0, rr=7, ws=0, egt=1, ksr=0, vib=0,
       am=0, ksl=0):
    """One operator's five register values (20h, 40h, 60h, 80h, E0h)."""
    assert 0 <= mult <= 15 and 0 <= tl <= 63 and 0 <= ws <= 3
    r20 = (am << 7) | (vib << 6) | (egt << 5) | (ksr << 4) | mult
    r40 = (ksl << 6) | tl
    r60 = (ar << 4) | dr
    r80 = (sl << 4) | rr
    return [r20, r40, r60, r80, ws]


def patch(mod, car, fb=0, cnt=0, tr=0):
    assert 0 <= fb <= 7
    return mod + car + [(fb << 1) | cnt, tr & 0xFF]


def fm_bank():
    P = [None] * 128
    # pianos: percussive (EGT 0), a bright attack that decays
    P[0] = patch(op(1, 24, 15, 3, 6, 4, egt=0, ksl=1), op(1, 0, 15, 2, 7, 4, egt=0), 3)
    P[1] = patch(op(1, 19, 15, 3, 6, 4, egt=0, ksl=1), op(1, 0, 15, 2, 7, 4, egt=0), 4)
    P[2] = patch(op(2, 26, 15, 3, 6, 4, egt=0), op(1, 0, 15, 2, 7, 4, egt=0), 3)
    P[3] = patch(op(1, 20, 15, 2, 5, 4, egt=0, vib=1), op(1, 0, 15, 2, 7, 4, egt=0, vib=1), 4)
    P[4] = patch(op(1, 34, 15, 3, 7, 4, egt=0), op(1, 0, 15, 2, 6, 4, egt=0), 1)
    P[5] = patch(op(14, 36, 15, 4, 8, 4, egt=0), op(1, 0, 15, 2, 6, 4, egt=0), 0)
    P[6] = patch(op(4, 22, 15, 4, 8, 3, egt=0), op(1, 0, 15, 3, 9, 3, egt=0), 5)
    P[7] = patch(op(3, 18, 15, 5, 9, 4, egt=0), op(1, 0, 15, 4, 10, 4, egt=0), 6)
    # chromatic percussion: bells - a high-ratio modulator, long decays
    P[8] = patch(op(4, 34, 15, 3, 8, 3, egt=0), op(1, 0, 15, 2, 9, 3, egt=0), 0, tr=0)
    P[9] = patch(op(7, 30, 15, 4, 9, 3, egt=0), op(2, 0, 15, 3, 9, 3, egt=0), 0)
    P[10] = patch(op(5, 32, 15, 4, 9, 3, egt=0), op(1, 0, 15, 3, 10, 3, egt=0), 0)
    P[11] = patch(op(4, 36, 15, 2, 7, 3, egt=0, am=1), op(1, 0, 15, 2, 8, 3, egt=0, am=1), 0)
    P[12] = patch(op(4, 28, 15, 6, 15, 5, egt=0), op(1, 0, 15, 5, 15, 5, egt=0), 0)
    P[13] = patch(op(7, 26, 15, 7, 15, 6, egt=0), op(3, 0, 15, 6, 15, 6, egt=0), 0)
    P[14] = patch(op(3, 26, 15, 2, 6, 2, egt=0), op(1, 0, 15, 2, 6, 2, egt=0), 3)
    P[15] = patch(op(3, 22, 15, 4, 9, 4, egt=0), op(1, 0, 15, 3, 9, 4, egt=0), 4)
    # organs: ADDITIVE (CNT 1), sustaining
    P[16] = patch(op(2, 8, 15, 0, 0, 7), op(1, 2, 15, 0, 0, 7), 0, 1)
    P[17] = patch(op(4, 10, 15, 6, 4, 7), op(1, 2, 15, 0, 0, 7), 0, 1)
    P[18] = patch(op(2, 6, 15, 0, 0, 7, vib=1), op(1, 2, 15, 0, 0, 7, vib=1), 2, 1)
    P[19] = patch(op(2, 10, 11, 0, 0, 5), op(1, 4, 10, 0, 0, 5), 0, 1, tr=-12)
    P[20] = patch(op(2, 14, 13, 0, 0, 6, vib=1), op(1, 4, 12, 0, 0, 6), 1, 1)
    P[21] = patch(op(1, 18, 13, 0, 0, 6, vib=1), op(1, 0, 13, 0, 1, 6), 6)
    P[22] = patch(op(2, 20, 12, 0, 1, 6, vib=1), op(1, 0, 12, 0, 1, 6), 5)
    P[23] = patch(op(1, 16, 13, 0, 0, 6), op(1, 0, 13, 0, 1, 6), 6)
    # guitars: plucked; 29/30 driven with feedback and sustain
    P[24] = patch(op(1, 28, 15, 4, 8, 5, egt=0), op(1, 0, 15, 3, 9, 5, egt=0), 3)
    P[25] = patch(op(3, 26, 15, 4, 8, 5, egt=0), op(1, 0, 15, 3, 9, 5, egt=0), 4)
    P[26] = patch(op(1, 32, 15, 3, 7, 5, egt=0), op(1, 0, 15, 2, 8, 5, egt=0), 2)
    P[27] = patch(op(2, 28, 15, 3, 7, 5, egt=0), op(1, 0, 15, 2, 8, 5, egt=0), 3)
    P[28] = patch(op(1, 24, 15, 7, 15, 8, egt=0), op(1, 0, 15, 6, 15, 8, egt=0), 4)
    P[29] = patch(op(1, 12, 15, 1, 2, 6), op(1, 0, 15, 1, 2, 6), 7)
    P[30] = patch(op(1, 8, 15, 1, 2, 6), op(2, 0, 15, 1, 2, 6), 7)
    P[31] = patch(op(4, 30, 15, 3, 8, 4, egt=0), op(2, 0, 15, 3, 8, 4, egt=0), 0, tr=12)
    # basses: strong fundamental, 37/38 synth
    P[32] = patch(op(1, 22, 15, 4, 8, 6, egt=0), op(1, 0, 15, 3, 7, 6, egt=0), 3, tr=-12)
    P[33] = patch(op(1, 18, 15, 3, 6, 6, egt=0), op(1, 0, 15, 2, 6, 6, egt=0), 4, tr=-12)
    P[34] = patch(op(1, 14, 15, 4, 8, 6, egt=0), op(1, 0, 15, 3, 7, 6, egt=0), 5, tr=-12)
    P[35] = patch(op(1, 26, 13, 2, 5, 6, egt=0), op(1, 0, 14, 2, 5, 6, egt=0), 2, tr=-12)
    P[36] = patch(op(1, 12, 15, 5, 9, 6, egt=0), op(1, 0, 15, 3, 7, 6, egt=0), 6, tr=-12)
    P[37] = patch(op(2, 14, 15, 5, 9, 6, egt=0), op(1, 0, 15, 3, 7, 6, egt=0), 6, tr=-12)
    P[38] = patch(op(1, 10, 15, 2, 4, 7), op(1, 0, 15, 1, 3, 7), 7, tr=-12)
    P[39] = patch(op(2, 16, 15, 3, 5, 7), op(1, 0, 15, 1, 3, 7), 5, tr=-12)
    # strings: slow attack, vibrato, a feedback saw
    P[40] = patch(op(1, 20, 7, 1, 1, 6, vib=1), op(1, 0, 8, 1, 1, 6, vib=1), 6)
    P[41] = patch(op(1, 22, 7, 1, 1, 6, vib=1), op(1, 0, 8, 1, 1, 6, vib=1), 6, tr=-12)
    P[42] = patch(op(1, 22, 7, 1, 1, 5, vib=1), op(1, 0, 7, 1, 1, 5, vib=1), 5, tr=-12)
    P[43] = patch(op(1, 24, 6, 1, 1, 5, vib=1), op(1, 0, 6, 1, 1, 5, vib=1), 5, tr=-24)
    P[44] = patch(op(1, 20, 9, 1, 1, 6, vib=1, am=1), op(1, 0, 9, 1, 1, 6, vib=1, am=1), 6)
    P[45] = patch(op(1, 26, 15, 6, 15, 7, egt=0), op(1, 0, 15, 5, 15, 7, egt=0), 3)
    P[46] = patch(op(2, 30, 15, 3, 9, 4, egt=0), op(1, 0, 15, 2, 9, 4, egt=0), 2)
    P[47] = patch(op(1, 16, 15, 4, 10, 4, egt=0), op(1, 0, 15, 3, 10, 4, egt=0), 6, tr=-12)
    # ensembles and voices
    P[48] = patch(op(1, 22, 6, 1, 1, 5, vib=1), op(1, 0, 6, 1, 1, 5, vib=1), 6)
    P[49] = patch(op(1, 24, 5, 1, 1, 4, vib=1), op(1, 0, 5, 1, 1, 4, vib=1), 5)
    P[50] = patch(op(2, 22, 6, 1, 1, 5, vib=1), op(1, 0, 6, 1, 1, 5), 4)
    P[51] = patch(op(1, 26, 4, 1, 1, 4, vib=1), op(1, 0, 4, 1, 1, 4, vib=1), 3)
    P[52] = patch(op(2, 18, 6, 1, 1, 5, vib=1), op(1, 4, 6, 1, 1, 5, vib=1), 0, 1)
    P[53] = patch(op(1, 30, 7, 1, 1, 5, vib=1), op(1, 0, 7, 1, 1, 5, vib=1), 0)
    P[54] = patch(op(2, 24, 6, 1, 1, 5, vib=1), op(1, 0, 6, 1, 1, 5, vib=1), 2)
    P[55] = patch(op(1, 10, 15, 5, 12, 5, egt=0), op(1, 0, 15, 4, 12, 5, egt=0), 7)
    # brass: a quick attack, the modulator pushing the carrier bright
    P[56] = patch(op(1, 16, 9, 2, 2, 6, vib=1), op(1, 0, 10, 1, 1, 6), 6)
    P[57] = patch(op(1, 18, 8, 2, 2, 6), op(1, 0, 8, 1, 1, 6), 5, tr=-12)
    P[58] = patch(op(1, 18, 8, 2, 2, 6), op(1, 0, 8, 1, 1, 6), 4, tr=-24)
    P[59] = patch(op(1, 24, 10, 2, 3, 6), op(2, 0, 10, 1, 2, 6), 5)
    P[60] = patch(op(1, 24, 7, 1, 2, 5, vib=1), op(1, 0, 7, 1, 1, 5), 3, tr=-12)
    P[61] = patch(op(1, 14, 9, 2, 2, 6, vib=1), op(1, 0, 9, 1, 1, 6), 7)
    P[62] = patch(op(1, 12, 10, 3, 3, 6), op(1, 0, 10, 2, 2, 6), 7)
    P[63] = patch(op(2, 16, 8, 3, 3, 6), op(1, 0, 8, 2, 2, 6), 6)
    # reeds: odd harmonics from a 2:1 or 3:1 pair
    P[64] = patch(op(3, 26, 10, 1, 1, 6, vib=1), op(1, 0, 10, 1, 1, 6), 4)
    P[65] = patch(op(1, 20, 10, 1, 1, 6, vib=1), op(1, 0, 10, 1, 1, 6), 6)
    P[66] = patch(op(1, 18, 10, 1, 1, 6, vib=1), op(1, 0, 10, 1, 1, 6), 6, tr=-12)
    P[67] = patch(op(1, 16, 10, 1, 1, 6), op(1, 0, 10, 1, 1, 6), 6, tr=-12)
    P[68] = patch(op(3, 24, 9, 1, 1, 6, vib=1), op(1, 0, 9, 1, 1, 6), 3)
    P[69] = patch(op(3, 22, 9, 1, 1, 6, vib=1), op(1, 0, 9, 1, 1, 6), 3, tr=-12)
    P[70] = patch(op(3, 20, 9, 1, 1, 6), op(1, 0, 9, 1, 1, 6), 4, tr=-12)
    P[71] = patch(op(2, 22, 10, 1, 1, 6, vib=1), op(1, 0, 10, 1, 1, 6), 2)
    # pipes: near-sine, breathy attack
    P[72] = patch(op(1, 40, 10, 1, 1, 6, vib=1), op(1, 0, 10, 1, 1, 6, vib=1), 1, tr=12)
    P[73] = patch(op(1, 38, 9, 1, 1, 6, vib=1), op(1, 0, 9, 1, 1, 6, vib=1), 1)
    P[74] = patch(op(1, 36, 10, 1, 1, 6), op(1, 0, 10, 1, 1, 6), 2)
    P[75] = patch(op(2, 36, 8, 1, 1, 6, vib=1), op(1, 0, 8, 1, 1, 6, vib=1), 3)
    P[76] = patch(op(1, 34, 9, 1, 1, 6, vib=1), op(1, 0, 9, 1, 1, 6, vib=1), 5)
    P[77] = patch(op(2, 32, 9, 1, 1, 6, vib=1), op(1, 0, 9, 1, 1, 6, vib=1), 3)
    P[78] = patch(op(1, 46, 13, 1, 1, 6, vib=1), op(1, 0, 13, 1, 1, 6, vib=1), 0, tr=12)
    P[79] = patch(op(1, 44, 12, 1, 1, 6, vib=1), op(1, 0, 12, 1, 1, 6), 0)
    # synth leads
    P[80] = patch(op(2, 18, 15, 0, 0, 6), op(1, 0, 15, 0, 0, 6), 7)
    P[81] = patch(op(1, 12, 15, 0, 0, 6), op(1, 0, 15, 0, 0, 6), 7)
    P[82] = patch(op(1, 30, 12, 0, 0, 6, vib=1), op(1, 0, 12, 0, 0, 6, vib=1), 2)
    P[83] = patch(op(4, 26, 15, 5, 3, 6), op(1, 0, 15, 0, 0, 6), 3)
    P[84] = patch(op(1, 10, 15, 1, 2, 6), op(1, 0, 15, 0, 1, 6), 7)
    P[85] = patch(op(1, 26, 11, 1, 1, 6, vib=1), op(1, 0, 11, 0, 1, 6, vib=1), 2)
    P[86] = patch(op(3, 20, 15, 0, 0, 6), op(1, 0, 15, 0, 0, 6), 5)
    P[87] = patch(op(1, 14, 15, 1, 1, 6), op(1, 0, 15, 0, 1, 6), 7, tr=-12)
    # synth pads: slow, additive halos
    P[88] = patch(op(2, 24, 6, 1, 1, 4, vib=1), op(1, 0, 6, 1, 1, 4), 2)
    P[89] = patch(op(1, 26, 5, 1, 1, 4, vib=1), op(1, 0, 5, 1, 1, 4, vib=1), 4)
    P[90] = patch(op(1, 18, 8, 1, 1, 5), op(1, 0, 8, 1, 1, 5), 6)
    P[91] = patch(op(2, 22, 5, 1, 1, 4, vib=1), op(1, 4, 5, 1, 1, 4, vib=1), 0, 1)
    P[92] = patch(op(1, 24, 4, 1, 1, 4, vib=1), op(1, 0, 4, 1, 1, 4, vib=1), 5)
    P[93] = patch(op(3, 26, 5, 1, 1, 4), op(1, 0, 5, 1, 1, 4), 3)
    P[94] = patch(op(4, 30, 4, 1, 1, 4, vib=1), op(1, 0, 4, 1, 1, 4, vib=1), 1)
    P[95] = patch(op(1, 20, 3, 1, 1, 4, vib=1, am=1), op(1, 0, 4, 1, 1, 4), 5)
    # synth effects
    P[96] = patch(op(5, 30, 15, 4, 9, 3, egt=0), op(1, 0, 15, 3, 9, 3, egt=0), 2)
    P[97] = patch(op(2, 24, 5, 1, 1, 4, vib=1), op(1, 0, 5, 1, 1, 4), 4)
    P[98] = patch(op(7, 28, 15, 3, 8, 3, egt=0), op(1, 0, 15, 2, 8, 3, egt=0), 1)
    P[99] = patch(op(3, 26, 6, 1, 1, 4, vib=1), op(1, 0, 6, 1, 1, 4, vib=1), 3)
    P[100] = patch(op(5, 24, 12, 2, 4, 4), op(1, 0, 12, 1, 3, 4), 3)
    P[101] = patch(op(1, 28, 4, 1, 1, 3, vib=1, am=1), op(1, 0, 4, 1, 1, 3), 6)
    P[102] = patch(op(3, 30, 10, 2, 5, 3, egt=0), op(1, 0, 10, 2, 5, 3, egt=0), 3)
    P[103] = patch(op(9, 22, 8, 1, 2, 4, vib=1), op(1, 0, 8, 1, 2, 4), 5)
    # ethnic
    P[104] = patch(op(3, 20, 15, 3, 7, 4, egt=0), op(1, 0, 15, 2, 7, 4, egt=0), 6)
    P[105] = patch(op(3, 24, 15, 5, 10, 5, egt=0), op(1, 0, 15, 4, 10, 5, egt=0), 4)
    P[106] = patch(op(3, 22, 15, 5, 11, 5, egt=0), op(1, 0, 15, 4, 11, 5, egt=0), 5)
    P[107] = patch(op(2, 26, 15, 4, 9, 4, egt=0), op(1, 0, 15, 3, 9, 4, egt=0), 3)
    P[108] = patch(op(4, 30, 15, 5, 11, 4, egt=0), op(1, 0, 15, 4, 11, 4, egt=0), 1)
    P[109] = patch(op(1, 12, 15, 0, 0, 6), op(1, 0, 15, 0, 0, 6), 7)
    P[110] = patch(op(1, 20, 9, 1, 1, 6, vib=1), op(1, 0, 9, 1, 1, 6, vib=1), 6)
    P[111] = patch(op(3, 18, 12, 1, 1, 6, vib=1), op(1, 0, 12, 1, 1, 6), 6)
    # percussive
    P[112] = patch(op(9, 30, 15, 4, 10, 3, egt=0), op(3, 0, 15, 3, 10, 3, egt=0), 0)
    P[113] = patch(op(5, 24, 15, 6, 13, 5, egt=0), op(2, 0, 15, 5, 13, 5, egt=0), 2)
    P[114] = patch(op(3, 28, 15, 4, 10, 4, egt=0), op(1, 0, 15, 3, 10, 4, egt=0), 1)
    P[115] = patch(op(9, 20, 15, 8, 15, 8, egt=0), op(4, 0, 15, 7, 15, 8, egt=0), 3)
    P[116] = patch(op(1, 10, 15, 4, 10, 5, egt=0), op(1, 0, 15, 4, 10, 5, egt=0), 6, tr=-12)
    P[117] = patch(op(1, 14, 15, 4, 10, 5, egt=0), op(1, 0, 15, 4, 10, 5, egt=0), 4)
    P[118] = patch(op(1, 16, 15, 5, 11, 5, egt=0), op(1, 0, 15, 5, 11, 5, egt=0), 5)
    P[119] = patch(op(13, 6, 2, 1, 1, 7), op(1, 0, 3, 1, 1, 7), 7)
    # sound effects
    P[120] = patch(op(13, 20, 15, 7, 15, 8, egt=0), op(1, 0, 15, 7, 15, 8, egt=0), 7)
    P[121] = patch(op(13, 14, 7, 1, 1, 6), op(1, 0, 7, 1, 1, 6), 7)
    P[122] = patch(op(13, 8, 3, 1, 2, 3, am=1), op(1, 0, 3, 1, 2, 3, am=1), 7)
    P[123] = patch(op(4, 30, 15, 2, 4, 5, vib=1), op(2, 0, 15, 2, 4, 5, vib=1), 3, tr=24)
    P[124] = patch(op(6, 24, 15, 0, 0, 6, am=1), op(1, 0, 15, 0, 0, 6, am=1), 2, tr=12)
    P[125] = patch(op(13, 10, 6, 1, 1, 5, am=1), op(1, 0, 6, 1, 1, 5, am=1), 7, tr=-12)
    P[126] = patch(op(15, 8, 4, 1, 1, 4, am=1), op(1, 0, 4, 1, 1, 4), 7)
    P[127] = patch(op(15, 0, 15, 6, 14, 6, egt=0), op(1, 0, 15, 5, 14, 6, egt=0), 7, tr=-24)
    assert all(p is not None for p in P)
    return P


# GM drum keys 35..81 -> (kind, fixed note, template). The FM kit is built
# from five sounds: a kick (a low sine whose decay is the punch), a snare and
# the cymbals (feedback-7 noise at a high ratio), a tom (a pitched sine) and a
# small "tick" for the wood and metal hits.
def _kick(note, tl=0):
    return patch(op(1, 16, 15, 7, 15, 7, egt=0), op(1, tl, 15, 6, 15, 7, egt=0), 5) [:11] + [note]


def _snare(note, tl=0, decay=6):
    return patch(op(14, 0, 15, decay, 15, decay, egt=0), op(1, tl, 15, decay, 15, decay, egt=0), 7)[:11] + [note]


def _hat(note, tl=0, decay=8):
    return patch(op(15, 0, 15, decay, 15, decay, egt=0), op(13, tl, 15, decay, 15, decay, egt=0), 7)[:11] + [note]


def _tom(note, tl=0):
    return patch(op(1, 24, 15, 5, 15, 5, egt=0), op(1, tl, 15, 5, 15, 5, egt=0), 3)[:11] + [note]


def _tick(note, mult=5, tl=0, decay=8):
    return patch(op(mult, 14, 15, decay, 15, decay, egt=0), op(1, tl, 15, decay, 15, decay, egt=0), 4)[:11] + [note]


def fm_drums():
    D = {}
    D[35] = _kick(28)
    D[36] = _kick(32)
    D[37] = _tick(76, 7, 4, 9)              # side stick
    D[38] = _snare(60, 0, 6)
    D[39] = _snare(64, 2, 7)                # clap
    D[40] = _snare(62, 0, 6)
    for k, n in ((41, 41), (43, 44), (45, 47), (47, 51), (48, 54), (50, 58)):
        D[k] = _tom(n)
    D[42] = _hat(90, 4, 9)                  # closed hat
    D[44] = _hat(90, 6, 9)                  # pedal hat
    D[46] = _hat(90, 2, 5)                  # open hat
    D[49] = _hat(80, 0, 3)                  # crash 1
    D[51] = _hat(86, 6, 4)                  # ride 1
    D[52] = _hat(78, 2, 3)                  # chinese
    D[53] = _tick(84, 9, 4, 5)              # ride bell
    D[54] = _hat(94, 6, 7)                  # tambourine
    D[55] = _hat(84, 2, 4)                  # splash
    D[56] = _tick(76, 7, 2, 6)              # cowbell
    D[57] = _hat(82, 0, 3)                  # crash 2
    D[58] = _tick(64, 11, 2, 4)             # vibraslap
    D[59] = _hat(88, 6, 4)                  # ride 2
    for k, n in ((60, 72), (61, 67), (62, 64), (63, 60), (64, 55)):
        D[k] = _tom(n, 4)                   # bongos and congas
    for k, n in ((65, 62), (66, 57)):
        D[k] = _tom(n, 2)                   # timbales
    D[67] = _tick(79, 7, 2, 6)              # agogo
    D[68] = _tick(74, 7, 2, 6)
    D[69] = _hat(96, 8, 8)                  # cabasa
    D[70] = _hat(100, 8, 9)                 # maracas
    D[71] = _tick(96, 2, 6, 3)              # whistles
    D[72] = _tick(91, 2, 6, 2)
    D[73] = _hat(98, 10, 9)                 # guiro
    D[74] = _hat(98, 10, 6)
    D[75] = _tick(84, 9, 4, 9)              # claves
    D[76] = _tick(79, 5, 4, 9)              # wood blocks
    D[77] = _tick(74, 5, 4, 9)
    D[78] = _tick(70, 3, 6, 7)              # cuicas
    D[79] = _tick(66, 3, 6, 7)
    D[80] = _tick(96, 11, 6, 9)             # triangles
    D[81] = _tick(96, 11, 6, 4)
    out = []
    for k in range(DRUM_LO, DRUM_HI + 1):
        assert k in D, k
        assert len(D[k]) == 12
        out.append(D[k])
    return out


# ---------------------------------------------------------------------------
# the synth's voicing
# ---------------------------------------------------------------------------
# envelope classes: (attack step, decay step, sustain level, release step),
# levels 0..255, applied every 16 ms. An attack step of 255 is instant.
ENV = [
    (255, 0, 255, 48),          # 0 organ: on, held, a quick release
    (255, 3, 0, 24),            # 1 piano / pluck: decays to nothing ~1.4 s
    (255, 10, 0, 32),           # 2 short pluck: ~0.4 s
    (14, 0, 220, 10),           # 3 strings / pad: ~0.3 s swell, slow release
    (60, 2, 200, 24),           # 4 brass: ~70 ms attack
    (255, 1, 0, 12),            # 5 bell: a long decay
    (255, 0, 230, 40),          # 6 lead
    (34, 0, 215, 20),           # 7 wind: ~0.12 s breath
]

# per GM family (8 programs each): duty, envelope class, octave lift
SYN_FAMILY = [
    (1, 1, 0),   # piano
    (2, 5, 0),   # chromatic percussion
    (0, 0, 0),   # organ
    (1, 2, 0),   # guitar
    (0, 2, 0),   # bass
    (1, 3, 0),   # strings
    (1, 3, 0),   # ensemble
    (1, 4, 0),   # brass
    (2, 7, 0),   # reed
    (0, 7, 0),   # pipe
    (0, 6, 0),   # synth lead
    (1, 3, 0),   # synth pad
    (2, 3, 0),   # synth effects
    (1, 2, 0),   # ethnic
    (2, 2, 0),   # percussive
    (2, 5, 0),   # sound effects
]


def syn_table():
    out = []
    for p in range(128):
        duty, env, octl = SYN_FAMILY[p >> 3]
        if p in (6, 7):                     # harpsichord, clavinet: thinner
            duty = 2
        if p in (36, 37, 38, 39):           # slap and synth basses: a pulse
            duty = 1
        if p in (80,):                      # Lead 1 (Square) is a square
            duty = 0
        if p in (45, 46):                   # pizzicato, harp: plucked
            env = 2
        if p in (47,):                      # timpani
            env = 1
        if p in (55,):                      # orchestra hit
            env = 2
        out.append(duty | (env << 2) | (octl << 5))
    return out


# GM drum key -> (kind, pitch or colour, decay step, level). kind 0 is a tone
# (pitch = a MIDI note, with a fall: the kick's thump), kind 1 noise (colour =
# the longest run in samples at 8 kHz, 1 = bright hat, 6 = dark snare).
def syn_drums():
    D = {}
    D[35] = (0, 30, 22, 255)
    D[36] = (0, 33, 22, 255)
    D[37] = (1, 1, 60, 160)
    D[38] = (1, 4, 24, 230)
    D[39] = (1, 3, 30, 210)
    D[40] = (1, 4, 24, 230)
    for k, n in ((41, 41), (43, 44), (45, 47), (47, 51), (48, 54), (50, 58)):
        D[k] = (0, n, 16, 220)
    D[42] = (1, 1, 60, 150)
    D[44] = (1, 1, 60, 130)
    D[46] = (1, 1, 14, 150)
    D[49] = (1, 2, 5, 190)
    D[51] = (1, 1, 10, 140)
    D[52] = (1, 2, 5, 190)
    D[53] = (0, 84, 12, 150)
    D[54] = (1, 1, 30, 150)
    D[55] = (1, 2, 8, 170)
    D[56] = (0, 76, 20, 170)
    D[57] = (1, 2, 5, 190)
    D[58] = (1, 3, 10, 150)
    D[59] = (1, 1, 10, 140)
    for k, n in ((60, 72), (61, 67), (62, 64), (63, 60), (64, 55)):
        D[k] = (0, n, 24, 190)
    for k, n in ((65, 62), (66, 57)):
        D[k] = (0, n, 20, 200)
    D[67] = (0, 79, 24, 170)
    D[68] = (0, 74, 24, 170)
    D[69] = (1, 1, 40, 120)
    D[70] = (1, 1, 50, 120)
    D[71] = (0, 96, 14, 140)
    D[72] = (0, 91, 8, 140)
    D[73] = (1, 2, 30, 130)
    D[74] = (1, 2, 16, 130)
    D[75] = (0, 84, 50, 170)
    D[76] = (0, 79, 50, 170)
    D[77] = (0, 74, 50, 170)
    D[78] = (0, 70, 24, 150)
    D[79] = (0, 66, 24, 150)
    D[80] = (0, 96, 40, 130)
    D[81] = (0, 96, 10, 130)
    out = []
    for k in range(DRUM_LO, DRUM_HI + 1):
        out.append(D[k])
    return out


# ---------------------------------------------------------------------------
# emit
# ---------------------------------------------------------------------------
def _rows(label, values, kind='db', per=16, comment=None):
    lines = []
    if comment:
        lines.append('; ' + comment)
    lines.append('%s:' % label)
    for i in range(0, len(values), per):
        chunk = values[i:i + per]
        lines.append('    %s %s' % (kind, ', '.join('0x%02X' % (v & 0xFF) if kind == 'db'
                                                    else '0x%04X' % (v & 0xFFFF) for v in chunk)))
    return lines


def generate():
    L = []
    L.append('; ' + '=' * 77)
    L.append('; os8088 - apps/midirack/mrtab.inc')
    L.append(';')
    L.append('; GENERATED by `python3 tools/os88midi.py gen` - DO NOT EDIT. The tool is')
    L.append('; the reference and `os88midi.py check` (a fast-tier row) refuses a stale')
    L.append('; copy. What each table is, and why it is arithmetic made on the host, is')
    L.append("; the tool's docstring and SPEC.md 105.6/105.7.")
    L.append('; ' + '=' * 77)
    L.append('')
    L.append('MRT_FINE   equ %d' % FINE)
    L.append('MRT_DRLO   equ %d' % DRUM_LO)
    L.append('MRT_DRHI   equ %d' % DRUM_HI)
    L.append('MRT_PATCH  equ 12')
    L.append('')
    L += _rows('mrt_fn', fn_table(), 'dw', 8, 'OPL F-Number at block = octave - 1, 1/32 semitone steps')
    L.append('')
    L += _rows('mrt_fq', fq_table(), 'dw', 8, "octave 0's frequency x 2048, the same steps")
    L.append('')
    L += _rows('mrt_tl', tl_table(), 'db', 16, 'MIDI level -> OPL attenuation (0.75 dB steps), 40 log10')
    L.append('')
    L += _rows('mrt_amp', amp_table(), 'db', 16, "MIDI level -> the synth's amplitude, a square law")
    L.append('')
    L.append("; the General MIDI names, NUL-terminated, in program order (mr_gmname walks)")
    L.append('mrt_gmname:')
    for i, n in enumerate(GM_NAMES):
        L.append("    db '%s', 0%s" % (n.replace("'", "''"), '' if i % 8 else '            ; %d' % i))
    L.append('')
    bank = fm_bank()
    L.append('; the FM bank: mod 20h 40h 60h 80h E0h, car the same, C0h, transpose')
    L.append('mrt_fmbank:')
    for i, p in enumerate(bank):
        L.append('    db %s   ; %3d %s' % (', '.join('0x%02X' % (b & 0xFF) for b in p), i, GM_NAMES[i]))
    L.append('')
    L.append('; the FM kit, keys %d..%d: the same eleven bytes, then the note struck' % (DRUM_LO, DRUM_HI))
    L.append('mrt_fmdrum:')
    for i, p in enumerate(fm_drums()):
        L.append('    db %s   ; key %d' % (', '.join('0x%02X' % (b & 0xFF) for b in p), DRUM_LO + i))
    L.append('')
    L += _rows('mrt_syn', syn_table(), 'db', 16, "the synth's voicing: duty | env << 2 | octave lift << 5")
    L.append('')
    L.append("; the synth's kit: kind, pitch or colour, decay step, level")
    L.append('mrt_syndr:')
    for i, d in enumerate(syn_drums()):
        L.append('    db %d, %d, %d, %d   ; key %d' % (d + (DRUM_LO + i,)))
    L.append('')
    L.append("; the synth's envelope classes: attack, decay, sustain, release (per 16 ms)")
    L.append('mrt_env:')
    for i, e in enumerate(ENV):
        L.append('    db %d, %d, %d, %d   ; class %d' % (e + (i,)))
    L.append('')
    return '\n'.join(L)


# ---------------------------------------------------------------------------
# the reference sequencer: an SMF read into a timeline, for the gates
# ---------------------------------------------------------------------------
def _vlq(data, pos):
    v = 0
    for _ in range(4):
        b = data[pos]
        pos += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, pos
    raise ValueError('VLQ longer than 4 bytes at %d' % pos)


def read_smf(data):
    """-> (format, division, [track event lists]). Each event (tick, kind,
    payload). Refuses what MIDIRack refuses (SPEC.md 105.5.1)."""
    if data[:4] != b'MThd' or int.from_bytes(data[4:8], 'big') != 6:
        raise ValueError('not a Standard MIDI File')
    fmt = int.from_bytes(data[8:10], 'big')
    ntrk = int.from_bytes(data[10:12], 'big')
    div = int.from_bytes(data[12:14], 'big')
    if fmt > 1:
        raise ValueError('format %d' % fmt)
    if div & 0x8000 or div == 0:
        raise ValueError('SMPTE or zero division')
    pos = 14
    tracks = []
    while len(tracks) < ntrk and pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        clen = int.from_bytes(data[pos + 4:pos + 8], 'big')
        body = data[pos + 8:pos + 8 + clen]
        pos += 8 + clen
        if cid != b'MTrk':
            continue
        ev = []
        p = 0
        tick = 0
        rs = 0
        while p < len(body):
            d, p = _vlq(body, p)
            tick += d
            st = body[p]
            if st & 0x80:
                p += 1
            else:
                st = rs
            if st == 0xFF:
                rs = 0                      # meta and SysEx cancel running
                mt = body[p]                # status (the SMF spec; mrq_event)
                ln, p = _vlq(body, p + 1)
                ev.append((tick, 'meta', (mt, bytes(body[p:p + ln]))))
                p += ln
                if mt == 0x2F:
                    break
            elif st in (0xF0, 0xF7):
                rs = 0
                ln, p = _vlq(body, p)
                p += ln
            else:
                rs = st
                n = 1 if (st & 0xF0) in (0xC0, 0xD0) else 2
                ev.append((tick, 'ch', (st, bytes(body[p:p + n]))))
                p += n
        tracks.append(ev)
    return fmt, div, tracks


def timeline(data):
    """-> list of (seconds, status, bytes) in play order, ties by track."""
    fmt, div, tracks = read_smf(data)
    merged = []
    for ti, ev in enumerate(tracks):
        for seq, (tick, kind, pl) in enumerate(ev):
            merged.append((tick, ti, seq, kind, pl))
    merged.sort()
    tempo = 500000
    last_tick = 0
    secs = 0.0
    out = []
    for tick, ti, seq, kind, pl in merged:
        secs += (tick - last_tick) * tempo / (div * 1e6)
        last_tick = tick
        if kind == 'meta' and pl[0] == 0x51 and len(pl[1]) == 3:
            tempo = int.from_bytes(pl[1], 'big')
        if kind == 'ch':
            out.append((secs, pl[0], pl[1]))
    return out, secs


# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    g = sub.add_parser('gen')
    g.add_argument('-o', '--out', default=DEFAULT_OUT)
    c = sub.add_parser('check')
    c.add_argument('-i', '--inc', default=DEFAULT_OUT)
    pl = sub.add_parser('play')
    pl.add_argument('file')
    a = ap.parse_args()
    if a.cmd == 'gen':
        with open(a.out, 'w') as f:
            f.write(generate())
        print('os88midi: wrote %s' % os.path.relpath(a.out, ROOT))
        return 0
    if a.cmd == 'check':
        want = generate()
        try:
            have = open(a.inc).read()
        except OSError:
            have = None
        if have != want:
            print('os88midi: %s is STALE - run `python3 tools/os88midi.py gen`'
                  % os.path.relpath(a.inc, ROOT))
            return 1
        print('os88midi: %s is current' % os.path.relpath(a.inc, ROOT))
        return 0
    if a.cmd == 'play':
        ev, secs = timeline(open(a.file, 'rb').read())
        print('%d channel events, %.2f s' % (len(ev), secs))
        return 0
    return 2


if __name__ == '__main__':
    sys.exit(main())
