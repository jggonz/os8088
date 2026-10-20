# The speaker's leveller: what is left, and how to go after it

**OPEN.** Written 2026-09-29, after five rounds of field listening, when the
owner asked for the remaining ideas to be written down before moving on - and
then, the same day, picked candidate 1 below as the next experiment. SPEC.md
34.11.9 is the contract for what ships; this is what might come after it, with
the measurements the choices were made on.

## 1. Where it stands

`apps/os88spkfx.inc` turns 8-bit PCM into pulse widths for the PC speaker, and
its leveller decides how loud each 256-sample span plays (512 above 11 kHz).
Four rounds of field listening on the owner's Toshiba T1100 Plus and 5150
shaped it:

| round | what was heard | what changed |
|---|---|---|
| 1 | "one volume, then super soft, then back a third of a second later" | a span after SILENCE takes its own level at once |
| 2 | ~50 ms "microdropouts" | the peak held over three spans |
| 3 | the steady low part moving with the punctuating hits | a HELD, DECAYING peak (re-armed within ~2.5 dB, 16 spans, then 1/16 a span) and a row that GLIDES a 2 dB step every 16 samples; `ZT` 2.0 -> 2.5 |
| 4 | "still some 'a loud thing happens, the soft thing goes away and fades back in', but cleaner" | chosen for the release |
| 5 | a second listener, side by side: round 3's fades out and back in are MORE obvious for being slower | round 3 REVERTED (a46deec); round 2's three-span hold ships |
| 6 | candidate 1 as a RATCHET: "basically completely fixes it - no more weird warbles, no more fades in after dropping out" | Tracker plays ONE level a song (324267f, SPEC.md 34.11.9.1) |
| 7 | a listening build with a hand on the level: 5 is the clearest on the T1100, up to 7 only louder, 10 blurs; on the 5150, 7 to 10 weakens the carrier's whine | the level SHIPS as Tracker's volume bar, and the rate as a menu (3b502c1, SPEC.md 45.25.3); the T1100 confirmed working well on the release build |
| 8 | the missing bass, squared up: "It's audible! ... good enough to show off" | SHIPS (336aa39, SPEC.md 45.25.4) |

Measured on 40 s captures (section 3), "wander" being the level's spread within
each second:

| song | shaper | wander | mean gain | at the curve's end |
|---|---|---|---|---|
| ELYSIUM.MOD | round 2 | 1.87 dB | 10.8 dB | 3.6% |
| | round 3 (reverted) | 0.65 dB | 10.4 dB | 3.5% |
| BEVERLY.MOD | round 2 | 2.21 dB | 10.3 dB | 2.9% |
| | round 3 (reverted) | 0.84 dB | 8.9 dB | 1.8% |

## 2. What is left, and why it is structural

**The gain is one number for the whole mix.** When a loud, infrequent part
arrives, the level has to come down or the part clips, and everything else in
the mix comes down with it. Round 3 made that happen less often and more
slowly, and round 5 is the lesson from it: **slower is not better, only
different.** One listener heard fewer, gentler movements; another heard the
same movements as longer, more obvious fades. Both were describing the same
defect - parts that should not change, changing - so the target is not a
better-shaped movement but NO movement where the music has none. A drum hit
still takes the bass line down with it. Every idea below either splits the
mix so the parts are levelled separately, or knows about the loud part before
it arrives - and candidate 1 does not move at all.

## 3. The instrument, so nobody re-derives it

Every candidate here was judged on the same two captures. That method is what
made four rounds possible without guessing, so it is worth keeping runnable:

- **Capture.** On MartyPC's 7.16 MHz XT (`os8088_xt_vga_hdd`, `--turbo`, which
  takes the 8,000 Hz rung), open Tracker on a module and set a breakpoint just
  after `tsp_fill`'s last `os88spkfx_emit`, at `tsp_fill.one + 3`, the
  `add [es:TSP_RL], bx`. At each stop read `mp_outbuf` (the mixed span, the
  shaper's input) and the span just written into the ring at TOTAL (its
  output). 40 s is 1,253 spans. `tests/trkspk.py`'s `Trk` class and
  `os88marty.bp_trace(..., on_hit=...)` are all it takes, and the dry-grant
  counter at `os88spk_grant.dry` rides along, so a capture also says the ring
  never starved.
- **Replay.** `tools/os88spkfx.py`'s `Shaper` reproduces the machine's output
  EXACTLY (all but the first span, whose state the capture joins mid-song).
  So a variant is a subclass of `Shaper` run over the captured inputs, no
  assembly needed, and only the winner is written in asm.
- **Metrics.** Wander (above); mean gain; the share of samples at the curve's
  end (hard-clipped); level travel in dB/s; V-dips (spans 4 dB or more below
  the level on both sides). The first two are loudness and steadiness, the
  third is what the hits cost, and the last two are what "hitch" meant.

**Both halves are in the tree**:

    python3 tools/os88spkcap.py apps/tracker/beverly.mod --secs 40
    python3 tools/os88spklev.py build/beverly.pkl [more.pkl] --fixed 3,5,7

`os88spkcap.py` takes any module and writes a `.pkl` of spans (build/ by
default). `os88spklev.py` first replays the capture through the model and
says whether it is still EXACT against the machine - seeded from the state
after the capture's first span, since the capture joins the song mid-way -
and then prints the table below for each variant: today's ratchet, the
per-span leveller it replaced, and any `--fixed` levels. A candidate is a
Shaper subclass added to its `VARIANTS`. Measured when they were committed
(2026-09-29, a 20 s BEVERLY.MOD capture on the current build): EXACT over
628 spans, and 494 of them DIFFER with the model's `RTOL` off by one, so the
first line does fail when the model drifts. A capture from an older build
will not replay EXACT - the family table has changed under it - so capture
again rather than trusting an old `.pkl`. The Elysium capture needs the
owner's `ELYSIUM.MOD`, which is not in the tree; BEVERLY.MOD is.

**Cost is the other half of every row.** `tests/spkfx.py` prints the shaper's
cycles a sample. Audio's live 8,000 Hz leg on a 5150 (`tests/apspk.py`,
`pcm8`) is the tightest budget in the family. A first cut of round 3 that
scanned a 16-byte window every span cost it 13 more dry grants in 5 s, and the
constant-work hold that replaced it cost none, so read that leg's dry count
for any change here.

## 4. The candidates, most promising first

1. **A static gain for Tracker, from a pre-pass at load. THE NEXT
   EXPERIMENT** (the owner, 2026-09-29: "the problem are all the changes and
   the fading of things that shouldn't change, so this seems like it has
   promise").

   **SHIPPED, as a RATCHET rather than a pre-pass, and with the user's hand
   on it** (SPEC.md 34.11.9.1 and 45.25.3). The volume bar, `+` and `-` are
   the level with no card: the ratchet picks it and the bar shows it, and
   the first move makes it the user's for the session.

   **The pre-pass is NOT TAKEN** (the owner, 2026-09-29: on the songs tested
   the quiet intro is not a real problem, and a quiet-then-loud song is
   probably fine as it is). The owner's bar was ~300 ms, and it was priced
   against it, ESTIMATED at 150-200 cycles a cell on an 8088 and never
   written: BEVERLY.MOD walked in play order is 21,248 cells, ~0.7-0.8 s on
   a 5150, on top of the ~2.8 s `tsp_natural` already spends there; each
   pattern once is 8,448 cells, ~0.3 s, but loses the sample and volume
   carried in from the pattern before; ELYSIUM.MOD is 7,424 cells in order,
   ~0.25-0.3 s. The bigger doubt was accuracy - a sum of sample peaks times
   volumes is a proxy for the level the ratchet settles on, and a wrong one
   plays the whole song too hot or too quiet, which is worse than one step
   down. If it is ever reopened, check the estimate against the ratchet on
   the host with section 3's replay before writing the loop.

   The first cut (`TSP_RATCHET`): the level starts at `TSP_LSTART` = 8 and only
   ever steps down, for a span that overdrives it by more than 3 levels, so
   it finds the song's loud parts in its first seconds and then holds -
   ELYSIUM settles on 6 in 1.5 s, BEVERLY on 5 in 2.4 s. No pattern walk is
   needed, and a song whose loudest part comes late steps down once when it
   arrives. The pre-pass below is still the way to remove even that one step
   and the too-loud opening a quiet intro gets at 8; it is the next cut if the
   ear asks for it. `TSP_RATCHET=0` builds the per-span leveller back for the
   A/B. Tracker is the one
   player that knows its whole piece in advance: the patterns say which
   sample plays at which volume, and every sample's peak is known once the
   module is loaded (`tsp_natural` already walks them). A pass over the
   pattern data, with no mixing, can bound the song's loud passages and pick
   ONE level for the whole song, so nothing pumps because nothing moves, and
   the soft clip takes the rare hit above it. Captured Elysium at a fixed
   level 5 clips 25% of samples, far too hot, so the level has to come from
   the pre-pass (aim for 3-5% at the curve's end), not from a constant. Cost:
   load time (a pattern walk is fast next to `tsp_natural`'s filter) and no
   cycle in the play loop at all, since the level scan could be skipped.
   Risk: a song with a quiet intro and a loud chorus gets the chorus's level
   throughout; a slow leveller on top (hold of seconds, not spans) would
   cover that.
2. **Two bands.** Split the mix at ~300 Hz with a one-pole filter, level
   each band on its own, and sum. A hit in the highs no longer takes the bass
   down. Cost is per sample, not per span: a one-pole split plus a second
   table lookup is ~25-40 cycles on an 8088, which Audio's 8 kHz leg on a
   5150 cannot afford. It is a V20/T1100/286 rung, chosen by the tier the way
   Tracker's rates are. It changes the family table's shape (two families,
   or one family indexed twice), so it wants the model first.
3. **Look-ahead by delay: DROPPED** (the owner, 2026-09-29: one level a
   song, the ratchet, leaves nothing for it to do - Tracker's level no longer
   moves on a hit, so there is no pre-duck to cure). The idea, for the
   record: Tracker already runs a ring seconds deep, so
   delaying the shaper by one span would let the level fall exactly AT the hit
   instead of at the top of the span holding it. Finer detection costs
   per-sub-block peaks: reading 1 sample in 8 instead of 1 in 32, a few more
   cycles a sample. Addresses the pre-duck; does nothing for the fade-back
   after.
4. **Host-side quality for pre-shaped files.** A `.WAV` the encoder shapes
   for the speaker (`tools/os88venc.py`'s WAV target, SPEC.md 98.2.8) is
   played as counts: the machine does no levelling at all. So the host can
   run a proper leveller (true look-ahead, RMS detection, two or three bands,
   soft knee) at no cost to any 8088. Today it runs the machine's model, so a
   file sounds exactly like live playback; it need not. Counts files (kind 2)
   need no change on the machine to benefit.
5. **Tuning the shipped shape: the finer level steps are DROPPED** (the
   owner, 2026-09-29, for the ratchet's reason: a level that does not glide
   needs no finer glide). The rest, for the record: ratio 3 -> 2 moves the
   level less and leaves
   the lows quieter. 1 dB rows instead of 2 give a finer glide but a family
   of 21 rows, 5.4 KB of every carrier's bss against 2.8 KB. An RMS detector
   in place of the sparse peak is less spiky, but per-span squares need a
   table. Each is a column in the section 3 table before it is a build.

## 5. How to decide

The owner's ear decides, on the owner's machines, and what worked in round 4
should be kept: **two Trackers side by side**, the shipped build and the
candidate, the same module, switching between them. The numbers in section 3
say which candidate is worth an ear; they have never been the verdict. Keep
each candidate one self-contained commit, as round 3 was, so the one that
loses is a `git revert`.

## 6. Also open, found on the way

- **The 286 and Auto: NOT a defect, closed.** One session on the owner's
  16 MHz 286 opened at 16,000 Hz predicting 75% - 22,050 at 101.5%, just
  over `TSP_PCTMAX` - and this file said the mixer term over-counted and
  needed a per-tier constant. The owner then found build 07bc18e opening at
  22,050 predicting 93% and holding it with the spectrum on, the same across
  two reboots and a dozen opens of both modules, and called it right. The
  prediction is `R x (44/Nm + TSP_CS/Ne) + 5`, both benches taken once a
  session in the first play's first ~0.7 s, so a machine that close to the
  line opens a rung apart on a 9% difference. WHY that session read 9% slow
  is not known (XT mode is not it: the owner never plays a 286 in it). If a
  build opens at 16,000 again, note the percentage the status line gives,
  the module played first that session, and anything the machine was doing
  in that first second - the bench shares it with whatever else is running.
- **The carrier's whine against the level: MEASURED and CLOSED** (the
  owner, 2026-09-29: the user has the volume bar, and nothing else here
  worked out). **The resting point has nothing left to give.** `python3 tools/os88spklev.py --carrier CAP.pkl`
  replays a capture at every fixed level and splits what the pulse train
  puts at the pulse rate into the steady TONE (each pulse's
  (1 - e^-i2piD)/i2pi averaged over 20 ms) and the rest, beside the MUSIC
  (D's spread in the same windows). On the owner's card-less 5150 at
  4,800 Hz (30 s captures, EXACT against the machine), the tone falls as the
  level RISES - BEVERLY.MOD -3.9 dB at level 5 to -6.9 at 10, ELYSIUM.MOD
  -3.4 to -9.6 - while the music rises, so music over tone gains ~1.5-2 dB
  a level from 7 up: the owner's ear, in numbers. The mechanism is the
  width's SPREAD - a wide swing puts each pulse's contribution at a
  different phase and they cancel - and not where the carrier rests: the
  shaper already rests the waveform with its lowest pulse ~8 counts of 248
  off the floor, and an oracle that put it exactly there in every window
  buys 0.4-0.7 dB. A per-pulse dither does not help either: it cuts the
  tone by sinc(2k/n), 0.06 dB at +-8 counts, and the +-55 it takes for 3 dB
  is loud noise in the music, the width being the sound. What a higher level
  costs is clipping: at 4,800 Hz level 5 clips ~3% of samples, 7 ~11-12%, 10
  30-40% (the "blurred" the owner heard at 10). So the lever is the LEVEL, a
  trade the volume bar already gives the user, so a higher starting level
  for a 4,800-5,512 Hz machine was NOT taken. (On that capture ELYSIUM.MOD's
  ratchet settles on 7 at 4,800 Hz, against 6 at 8,000.)
- **BEVERLY.MOD's low notes: SHIPPED (SPEC.md 45.25.4), and CLOSED.** The
  owner, on round 2's build: *"It's audible! The speaker is still terrible
  at this, but this is good enough to show off."* What follows is the
  record of how it got there; the listening knobs it names (`-DTSP_BASS=k`,
  `TSP_BASSFULL`, `TSP_BASSKICK`, and `tracker_bass` in the model) became
  the shipped constants `TSP_BSHIFT`/`TSP_BKICK` and
  `tools/os88spkfx.py`'s `tracker_natural`, with `-DTSP_NOBASS` the A/B.
  **Round 3, shipping it, found the cost that matters**: summing |x| and
  |y| inside the load filter's own loop took BEVERLY.MOD's load from 2.86 s
  to 6.16 s on a 5150, ~170 cycles a byte of instruction FETCH for an
  8088; sparse sums over one byte in eight, with the cut moved to 5/7 to
  keep its margin, read 4.01 s, and `soak -k trkload` holds it under 4.4.
  First measured 2026-09-29. The bass is sample 2, `digdug`, a slow,
  nearly pure wave played at 4,390-13,964 Hz - a fundamental of ~50-160 Hz,
  all of it under the speaker - and `tsp_natural`'s load filter takes 5.4 dB
  of it on the way. Weighted by the notes the song plays and resampled as
  the mixer does, the part at 400 Hz and up (where a PC speaker starts to
  work) is **-16.5 dB against the melody's -1.3**: 15 dB down, which is
  "gone". The idea is the ear's missing fundamental: square the wave up, so
  its harmonics land where the speaker plays and the note is heard from
  them, pitch kept (moving the NOTE up an octave or two was the other
  idea, and it puts the bass line into the melody's register). The build
  squares any sample the load filter took three quarters of (sum |y| under
  sum |x| / 4: `digdug` 0.236 and `bassdrum2` 0.185, the next `dxtom` at
  0.31, the melody ~0.45) by `y << k` clamped at its own peak, then runs the
  filter again to take back the fundamental the squaring grew (without it
  the sample's whole energy rose 5-6 dB for nothing the speaker plays).
  `tools/os88spkfx.py`'s `tracker_bass` is it exactly, and the machine's
  bytes after load equal it for both k on four samples. At 4,800 Hz:

  | 400 Hz and up | shipped | k = 3 (x8) | k = 4 (x16) |
  |---|---|---|---|
  | digdug | -16.5 dB | -10.6 (+5.9) | -8.2 (+8.3) |
  | bassdrum2 | -10.8 dB | -0.5 (+10.3) | +3.2 (+14.0) |
  | hallbrass (melody) | -1.3 dB | unchanged | unchanged |

  The ratchet still settles on 5 in all three builds. The listening build's
  load is ESTIMATED ~1 s slower on a 5150, two extra passes over every
  sample; a shipping version folds both sums into the filter pass.

  **Round 1, the owner's ear:** at x16 the kick at 0:30-0:35 is more audible
  and fine on the 5150, but on the 286 it drowns the high notes; the bass
  is "barely audible" where it was missing, at 0:30-0:35 and at 1:20-1:29,
  where the bass IS the melody. Measured across those sections of the whole
  song (95 s captures on the 5150 at 4,800 Hz, energy at 400 Hz and up at
  the level played), x16 bought the bass section 0.8 dB: barely audible, in
  numbers.

  **Round 2, what limits it and what does not:**
  - The clamp is not the limit: squaring at the ORIGINAL peak or at +-127
    instead of 2P buys 1-2 dB. A square's own shape is: at a 60-130 Hz
    fundamental ~80% of its energy is the fundamental and third harmonic,
    still under 400 Hz.
  - A narrow PULSE spreads more of its energy into the harmonics, but
    carries too little of it under the 8-bit peak, and digdug DECAYS, so a
    threshold on its one peak catches its first cycles only.
  - Squared into the whole stored range (the load filter stores every
    sample at half scale, so there are 6 dB unused) with NO second filter
    gives the bass the melody's audible level on its own - and CLIPS 38% OF
    THE SONG, the level falling to 4: the square's fundamental, which the
    speaker cannot play, takes the headroom. REFUSED.
  - `-DTSP_BASSFULL` is what works: the whole stored range, then TWO passes
    of a steeper high-pass (y - y/4 + (x - x_prev), out clamped to +-127),
    which take the fundamental and keep the harmonics.
  - `-DTSP_BASSKICK=j` separates the drum: after the load filter the bass
    crosses zero ~24 times in 1,000 samples and the kick ~250, so a selected
    sample with crossings under len / 16 is a bass and takes the full route,
    and anything else takes the gentle first route at j - the owner asked
    for the kick at x4.

  | whole song, 5150, 4,800 Hz | level | clip | 0:30-0:35 | 1:20-1:29 |
  |---|---|---|---|---|
  | shipped | 5 | 2.9% | 27.5 dB | 25.8 dB |
  | x16, round 1 | 5 | 5.8% | 29.4 dB | 26.6 dB |
  | bass and kick full | 5 | 7.6% | 33.1 dB | 28.9 dB |
  | bass full, kick x4 | 5 | 5.4% | 29.8 dB | 29.0 dB |

  The last is `TSP_BASS=4 TSP_BASSFULL TSP_BASSKICK=2`, the round 2 build;
  its bytes after load equal `tracker_bass(..., full=True, kick=2)` on four
  samples. `tools/os88spkcap.py --pkg ... --define ...` captures a listening
  build.
- **The 86 against 108.** SPKBENCH's shaper loop runs at ~86 cycles a sample
  on a 5150 where Tracker's calibration of the same call reads ~108 (Ne =
  9,728 in four ticks); neither the source data nor the pre-emphasis explains
  it (SPEC.md 45.25.1). `TSP_CS` is expressed against Tracker's figure, so
  this does not move the constant, but it is an unexplained 25%.
- **The 286's ISR is a quarter dearer** against its shaper than an 8088's
  (SPEC.md 45.25.1's ratio column), so Tracker under-predicts a 286 by ~7
  points at 22,050 Hz. The 16 MHz machine holds it; a 10-12 MHz one is the
  field test to ask for. A per-tier `TSP_CS` is the fix if it fails.
