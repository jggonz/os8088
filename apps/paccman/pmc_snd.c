/* ============================================================================
 * os8088 - apps/paccman/pmc_snd.c    three arcade voices into one speaker
 *
 * DERIVED MATERIAL. Part of PACCMAN, a reimplementation of Andre Weissflog's
 * pacman.c (https://github.com/floooh/pacman.c), MIT, (c) 2020 Andre
 * Weissflog, at commit 0f5ec5a; this file carries its sound machinery
 * (pacman.c 634-673 and 3192-3380). The two register dumps the players read
 * were captured from an arcade emulator and are decoded on the host into
 * pmc_rom.c. See apps/paccman/README.md.
 *
 * #included by apps/paccman/paccman.c (one translation unit, SPEC.md 73.1).
 *
 * ---------------------------------------------------------------------------
 * THREE VOICES, ONE SPEAKER, AND THE REDUCTION IS STATED RATHER THAN TUNED
 * ---------------------------------------------------------------------------
 * The Namco board has three wavetable voices, each with its own waveform and
 * its own 4-bit volume, and they play at once. The PC speaker is ONE square
 * wave with neither (os88_snd_tone(hz, ticks, prio), SPEC.md 34). So the
 * arcade's mixer cannot be carried and is not faked: the three voice registers
 * below are kept exactly as the reference keeps them - every effect writes the
 * register it writes there, at the tick it writes it - and once per OS tick
 * pmc_snd_frame() picks ONE of them for the speaker by priority:
 *
 *      voice 2  the effects       (a dot, a pill, a ghost, the fruit, death)
 *      voice 1  the tune          (the siren, the frightened warble, and the
 *                                  prelude's MELODY)
 *      voice 0  the prelude BASS
 *
 * so an effect always interrupts the tune, and the prelude's melody always
 * outranks its own bass. Which voice carries which is not a choice made here:
 * the prelude's dump is `.voice = { true, true, false }` at pacman.c 638-642
 * and the melody is the SECOND word of each pair, which is why pmc_rom.c
 * carries both columns and tests/unit/t_paccman.py asserts the melody's first
 * notes (539 then 1078 Hz) by name. Wave 1's first cut had the two swapped and
 * played the bass.
 *
 * WHAT ELSE THE ONE VOICE COSTS, both stated in SPEC.md 91 and the README and
 * neither of them on the About card (LESSONS.md 8: those are facts about the
 * BUILD):
 *
 *   - no waveform and no volume. `waveform` is read off the register and
 *     dropped; `volume` is kept only as the "is this voice silent" test, which
 *     is what the arcade's own envelopes need it for - the prelude's bass
 *     decays to volume 0 fifteen ticks into every phrase, and a sampler that
 *     ignored volume would hold that note through the rest.
 *   - the speaker is sampled ONCE PER OS TICK and the game runs at 60 Hz, so
 *     about 3.3 game ticks pass between two samples. An effect shorter than
 *     that can fall between them: the eat-dot crunch is 5 ticks long, so it
 *     lands as at most two tones and sometimes one.
 *
 * NOTHING HERE TAKES THE ADDRESS OF A FUNCTION. The reference dispatches its
 * six procedural effects through a `void (*func)(int slot)` in the sound
 * descriptor; a function pointer in a package is an offset into the package's
 * own segment and the C here has no way to say so safely (SPEC.md 73.5), so a
 * slot holds a KIND and pmc_snd_tick() is a switch on it.
 * ==========================================================================*/

/* --- the sound slots (pacman.c 165: NUM_SOUNDS = 3) ----------------------- */
#define PMC_SK_NONE      0
#define PMC_SK_PRELUDE   1      /* the register dump, voices 0 AND 1        */
#define PMC_SK_DEAD      2      /* ...and the death dump, voice 2           */
#define PMC_SK_EATDOT1   3
#define PMC_SK_EATDOT2   4
#define PMC_SK_EATGHOST  5
#define PMC_SK_EATFRUIT  6
#define PMC_SK_WEEOOH    7
#define PMC_SK_FRIGHT    8

/* WHICH VOICES A KIND OWNS, as a bit mask - the reference's `.voice[3]`
 * (pacman.c 638-679) turned into three bits, because snd_stop has to silence
 * exactly the voices its slot was driving and no others. */
static const unsigned char pmc_sk_voices[9] = {
    0,          /* none      */
    1 | 2,      /* prelude   - voice 0 bass and voice 1 melody */
    4,          /* dead      - voice 2 */
    4, 4, 4, 4, /* eatdot1, eatdot2, eatghost, eatfruit - voice 2 */
    2, 2        /* weeooh, frightened                   - voice 1 */
};

static unsigned char pmc_sk[3];         /* the kind in each slot            */
static unsigned      pmc_sct[3];        /* ...and its cur_tick              */

/* The three voice registers. `pmc_v_f` is the RAW register the six procedural
 * effects step by their own deltas; `pmc_v_hz` is what the speaker is asked
 * for. The two dumps write pmc_v_hz directly, because tools/paccman_assets.py
 * did that conversion on the host at full precision. */
static unsigned      pmc_v_f[3];
static unsigned      pmc_v_hz[3];
static unsigned char pmc_v_vol[3];

/* The weeooh siren's phase, 0..23. The reference tests `cur_tick % 24` and a
 * modulo of a non-power-of-two is a `div` here; a counter that wraps at 24 is
 * the same sequence for nothing. It cannot be `cur_tick & 31` either - the
 * siren never stops, so cur_tick wraps at 65,536, which is a multiple of 8
 * (so the frightened warble's `& 7` below stays right for ever) and NOT a
 * multiple of 24. */
static unsigned char pmc_wph;

/* Whether Game > Sound is on. It is a plain toggle and is NEVER greyed:
 * kernel/snd.inc's osapi_snd_caps answers the constant SND_CAP_TONE |
 * SND_CAP_PCM_EXCL with no kern_small arm, so every machine this OS boots on
 * has a speaker and the MACHINE is never a reason to refuse (SPEC.md 91). */
static int pmc_snd_on = 1;

/* The Hz the speaker was last asked for, so silence is not re-sent every OS
 * tick. A tone is asked for with a DURATION of two ticks and renewed by the
 * next frame, so it stops itself if the game does (a paused or covered window
 * goes quiet without a single call). */
static unsigned pmc_snd_last;

#define PMC_SND_PRIO  0x40      /* the default priority (apps/cc/os88.h)    */
#define PMC_SND_HOLD  2         /* ticks: renewed every frame, ~55 ms apart */

/* pmc_hz_of - the arcade's 20-bit frequency register in Hz.
 *
 * The WSG steps a 20-bit accumulator by the register value at 96 kHz, so the
 * tone is f * 96000 / 2^20 = f * 375 / 4096. That numerator overflows 16 bits
 * at f = 175 and the effects reach f = 0x1600, so the fraction is scaled down
 * to 47/512: `(f >> 5) * 47 >> 4` is within 0.3% of the exact value over the
 * whole range the six effects use and cannot overflow below f = 0x5800.
 *
 * The DUMPS do not come through here - pmc_rom.c holds their frequencies in Hz
 * already, converted on the host in full precision - so the only values this
 * rounds are the ones the effects compute. */
static unsigned pmc_hz_of(unsigned f)
{
    return ((f >> 5) * 47) >> 4;
}

/* pmc_snd_voice - write one voice register, the way an effect does. */
static void pmc_snd_voice(int v, unsigned f, int vol)
{
    pmc_v_f[v] = f;
    pmc_v_hz[v] = pmc_hz_of(f);
    pmc_v_vol[v] = (unsigned char) vol;
}

/* snd_stop, pacman.c 3262-3275: silence the slot's own voices and clear it. */
static void pmc_snd_stop(int slot)
{
    int m, v;

    m = pmc_sk_voices[pmc_sk[slot]];
    for (v = 0; v < 3; v++) {
        if (m & (1 << v)) {
            pmc_v_f[v] = 0;
            pmc_v_hz[v] = 0;
            pmc_v_vol[v] = 0;
        }
    }
    pmc_sk[slot] = PMC_SK_NONE;
    pmc_sct[slot] = 0;
}

/* snd_start, pacman.c 3234-3260. */
static void pmc_snd_start(int slot, int kind)
{
    pmc_sk[slot] = (unsigned char) kind;
    pmc_sct[slot] = 0;
}

/* snd_clear, pacman.c 3227-3231: every voice and every slot. */
static void pmc_snd_clear(void)
{
    int i;

    for (i = 0; i < 3; i++) {
        pmc_sk[i] = PMC_SK_NONE;
        pmc_sct[i] = 0;
        pmc_v_f[i] = 0;
        pmc_v_hz[i] = 0;
        pmc_v_vol[i] = 0;
    }
}

/* --- the six procedural effects, pacman.c 3277-3380 -----------------------
 * Every register, every delta and every stop tick is the reference's. They are
 * inlined into the switch below rather than written as six functions, because
 * six calls one level deeper would each be on the worker's tick path and the
 * package declares OS88_STACK_256 (SPEC.md 91). */

/* pmc_snd_tick - once per GAME tick, before the state's own tick (pacman.c
 * 744-780's frame() calls snd_tick() first). */
static void pmc_snd_tick(void)
{
    int s;
    unsigned t;

    for (s = 0; s < 3; s++) {
        t = pmc_sct[s];
        switch (pmc_sk[s]) {
        case PMC_SK_NONE:
            continue;                   /* no cur_tick to advance either */

        case PMC_SK_PRELUDE:
            if (t == PMC_SND_PRELUDE_TICKS) {
                pmc_snd_stop(s);
                continue;
            }
            pmc_v_hz[0]  = pmc_snd_prelude_hz0[t];
            pmc_v_vol[0] = pmc_snd_prelude_vol0[t];
            pmc_v_hz[1]  = pmc_snd_prelude_hz1[t];
            pmc_v_vol[1] = pmc_snd_prelude_vol1[t];
            break;

        case PMC_SK_DEAD:
            if (t == PMC_SND_DEAD_TICKS) {
                pmc_snd_stop(s);
                continue;
            }
            pmc_v_hz[2]  = pmc_snd_dead_hz[t];
            pmc_v_vol[2] = pmc_snd_dead_vol[t];
            break;

        case PMC_SK_EATDOT1:            /* pacman.c 3277-3291 */
            if (t == 0)
                pmc_snd_voice(2, 0x1500, 12);
            else if (t == 5) {
                pmc_snd_stop(s);
                continue;
            } else
                pmc_snd_voice(2, pmc_v_f[2] - 0x0300, 12);
            break;

        case PMC_SK_EATDOT2:            /* pacman.c 3293-3307 */
            if (t == 0)
                pmc_snd_voice(2, 0x0700, 12);
            else if (t == 5) {
                pmc_snd_stop(s);
                continue;
            } else
                pmc_snd_voice(2, pmc_v_f[2] + 0x0300, 12);
            break;

        case PMC_SK_EATGHOST:           /* pacman.c 3309-3323 */
            if (t == 0)
                pmc_snd_voice(2, 0, 12);
            else if (t == 32) {
                pmc_snd_stop(s);
                continue;
            } else
                pmc_snd_voice(2, pmc_v_f[2] + 0x20, 12);
            break;

        case PMC_SK_EATFRUIT:           /* pacman.c 3325-3341 */
            if (t == 0)
                pmc_snd_voice(2, 0x1600, 15);
            else if (t == 23) {
                pmc_snd_stop(s);
                continue;
            } else if (t < 11)
                pmc_snd_voice(2, pmc_v_f[2] - 0x0200, 15);
            else
                pmc_snd_voice(2, pmc_v_f[2] + 0x0200, 15);
            break;

        case PMC_SK_WEEOOH:             /* pacman.c 3343-3357 - never stops */
            if (t == 0) {
                pmc_snd_voice(1, 0x1000, 6);
                pmc_wph = 0;
            } else {
                pmc_wph++;
                if (pmc_wph >= 24)
                    pmc_wph = 0;
                if (pmc_wph < 12)
                    pmc_snd_voice(1, pmc_v_f[1] + 0x0200, 6);
                else
                    pmc_snd_voice(1, pmc_v_f[1] - 0x0200, 6);
            }
            break;

        default:                        /* PMC_SK_FRIGHT, pacman.c 3359-3374 */
            if (t == 0)
                pmc_snd_voice(1, 0x0180, 10);
            else if ((t & 7) == 0)
                pmc_snd_voice(1, 0x0180, 10);
            else
                pmc_snd_voice(1, pmc_v_f[1] + 0x0180, 10);
            break;
        }
        pmc_sct[s] = t + 1;
    }
}

/* pmc_snd_hush - stop the speaker NOW, for Game > Sound turned off. Without
 * it a tone already granted plays on for its two ticks after the user asked
 * for silence, which is the one case the self-timing duration gets wrong. */
static void pmc_snd_hush(void)
{
    if (pmc_snd_last != 0) {
        os88_snd_tone(0, 0, PMC_SND_PRIO);
        pmc_snd_last = 0;
    }
}

/* pmc_snd_frame - ONCE PER OS TICK, from the worker's frame. The one place
 * three voices become one, by the priority at the top of this file.
 *
 * A voice is silent when its VOLUME is zero as well as when its frequency is:
 * the prelude's bass decays through volume 14..0 over the fifteen ticks of
 * every phrase while its frequency stands still, so a sampler reading only the
 * frequency would hold that bass note for ever and never let the melody
 * through.
 *
 * Silence is not re-sent. A granted tone lasts PMC_SND_HOLD ticks and every
 * frame renews it, so a window that stops running frames - paused, behind
 * another window, or with the About card up - falls quiet by itself and costs
 * no call to do it. */
static void pmc_snd_frame(void)
{
    int v;
    unsigned hz;

    hz = 0;
    if (pmc_snd_on) {
        for (v = 2; v >= 0; v--) {
            if (pmc_v_vol[v] != 0 && pmc_v_hz[v] != 0) {
                hz = pmc_v_hz[v];
                break;
            }
        }
    }
    if (hz == 0 && pmc_snd_last == 0)
        return;
    pmc_snd_last = hz;
    os88_snd_tone((int) hz, PMC_SND_HOLD, PMC_SND_PRIO);
}
