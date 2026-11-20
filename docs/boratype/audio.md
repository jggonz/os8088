# Boratype visual reactions, music and effects

The current user-selected scope is original music, sound effects and visual
messages. There are no sound clips, voice-pack import tools, speech controls,
PCM playback paths or application audio heap claims. The BTT1 save format keeps
its old voice byte as an unused compatibility field; it cannot enable sound.

`bt_audio.c` consumes engine events and never changes scoring, lesson
randomness, focus or prompts. Its 63 original messages include 40 roasts, tagged
by measured events and severity. Gentle permits severity 1, Classic 1–2 and
Extra 1–3. The director detects three consecutive errors, accuracy below 85%
after at least 20 attempts, eight Backspaces, a ten-key recovery, real streak
milestones, completed results and personal bests. Idle messages require 30
seconds without typing and have a separate 60-second rate limit. There are no
jokes based solely on a beginner's speed.

Selection skips the previous four caption IDs. Ordinary reactions have a
219-tick gap (12.03 s); a new record or no-input result may replace the caption immediately.
An empty run selects neutral instruction tag11 before record/poor/success
classification, with the short poor-result stinger rather than a success or
achievement cue. Its completed guard prevents repeated finish reactions.
There is no reaction queue. The latest four captions are available through
`bt_reaction_history(0..3)` for results. Expressions remain active for eight
seconds; captions stay until another reaction. Welcome is selected on the
first focused tick with its actual guest timestamp. Muting sound or holding a
readonly instance never suppresses visual reactions or focused idle messages.

## Integration

Include `bt_audio.c` in the single C translation unit after `bt.h`; `bt_audio.h`
declares reaction tags, history and the music-pause bridge. Shim order is
`breadxt/draw.inc` with `BX_ART2`, `cc/gkmus.inc`, then
`boratype/bt_music.inc`. The small final include preserves the kit's sequence
positions across pause/focus loss while releasing and reacquiring FM channels.
It contains no PCM, file or private-driver code.

The normal art converter combines music/effect banks with the font/blink art
in the second part. Music ID 256 and effects ID 257 contain 1,205 bytes before
those drawing records are added. Call `bx_setart2` before audio initialization;
the kit resolves records afresh, so compaction leaves no cached sound segment.
The standalone sound-bank conversion remains:

```
python3 tools/os88gkmus.py part -o build/btsound.bin \
  apps/boratype/art/music.txt apps/boratype/art/sfx.txt
```

Initialize once, call `bt_audio_tick(now, focused)` with the actual focus state,
consume engine events with `bt_audio_event`, call `bt_audio_state` at screen
transitions, and call `bt_audio_done` at close/failed launch. The director
excludes readonly instances from sound ownership itself; the caller must not
hide their visual focus. Pass committed result fields before the finish event.
`bt_audio_test(void)` auditions the original menu music only.

## Sound behavior and resources

| Piece | Arrangement | Length |
| --- | --- | --- |
| Letter Passport / menu | D major, three FM voices or speaker melody | 35.2 s loop |
| Home Row Tea / lesson | G major, quieter glass/pad voices | 49.2 s loop |
| Deadline Dash / challenge | E minor, energetic bright lead | 31.6 s loop |
| Success / poor result / record | Three distinct original stingers | 2.6 / 4.4 / 2.6 s |

Eleven original effects cover selection, start, optional correct-key tick,
wrong key, streak, pause, resume, completion, poor result, record and visual
reaction. Correct-key ticks default off; repeated wrong-key bursts are limited
to one per three ticks. Music and Effects have independent enable settings,
and master sound applies before repaint. No volume slider is fabricated:
actual OS selection and the driver's mixer own volume.

The speaker plays the melody voice; effects temporarily preempt it at the
kit's higher priority. FM uses three claimed channels with the tone-tier
effects on the kit's separate route. Pause/focus loss snapshots sequence
positions/durations and releases FM channels. Resume asks the current OS
route again, reacquires patches, restores positions and retains the new
channel IDs. Ordinary same-route resume therefore keeps phase, while an OS
speaker selection or SOUND detach can resume through the speaker. Closing
releases every app-owned audio resource; there is no sound-file I/O or PCM
staging in any callback.

## Verification

`python3 tests/boratype_audio_host.py` compiles the production director with
address/undefined sanitizers. It checks message count/uniqueness, severity,
threshold/sample floors, correction/recovery, cooldown/tick wrap, anti-repeat,
idle pacing, music selection, mute, key ticks and readonly behavior. Muted and
readonly visual reactions are checked against a byte-for-byte unchanged
session. The wrapper no longer invokes a clip checker.

`tests/boratype_audio_target.py` drives the installed package through actual OS
keyboard/menu events on MartyPC's 4.77 MHz XT. It checks exact mapped package,
compressed boot-kernel and sound-driver identity. It captures FM/speaker title,
lesson, error and result music; observes the real error-effect call; exercises
master mute and verifies all app-owned heap claims are freed at close. The
optional OS route probe pauses a lesson, changes the real speaker route,
resumes normally, detaches SOUND, resumes through the speaker, reattaches,
and resumes FM. Each route must produce a nonzero captured waveform after
ordinary Resume; route/status bytes alone cannot pass. It does not inject
score state or simulate hardware removal.

```
make boratype mixertest build/os8088-360.img
python3 tests/boratype_audio_target.py --evidence /tmp/bt-audio-fm
python3 tests/boratype_audio_target.py --backend speaker --evidence /tmp/bt-audio-speaker
python3 tests/boratype_audio_target.py --route-probe --evidence /tmp/bt-audio-routes
```

The default fixture is just the installed package and its save directory;
there are no local audio packs, synthetic clips, downloads or network needs.
`--negative` disables Music through the actual Settings UI and must fail the
ordinary title-waveform assertion.

The final no-clip package from source `743da2621` was captured on both XT
machines. Its SHA256 is
`946461362cc2597703aa19904574634065fe560817a40dd2d27f128ee360c360`;
the matching `kernel.bin` is
`9a88e1725f4378664e1852d662276221bbe928280ac34863ba49765518081b1f`.

| Technical run | Measured host seconds | Title / lesson / error / result RMS | Mute RMS |
| --- | ---: | --- | ---: |
| FM, real OS route and driver changes | 36.601 | .010397 / .005813 / .019814 / .010507 | .000048565 |
| Speaker, no SOUND driver | 19.495 | .650591 / .689712 / .316454 / .608120 | 0 |
| Music disabled through Settings | 11.693 | Expected title assertion failure | — |

The route run additionally measured .670909 on the selected speaker,
.475606 after actual SOUND detach, and .006029 after reattaching and
resuming FM. Both positive runs observed real START then WRONG effect calls,
three-error captions, committed results, and an empty app-owned heap ledger
after Close. OPL key-on bits were clear and the no-driver speaker gate was
off at exit. JSON, PNG and WAV evidence is outside the repository in
`boratype-logs/audio-effects-routes-serviced` and
`boratype-logs/audio-effects-pass-speaker-serviced`.

The initial full PLAY paint and each full Resume paint prevented sequencer
service for 132–136 OS ticks (about 7.3 seconds) in these captures. Short
speaker leases therefore expire during that draw. The harness records this
delay and starts steady-music waveform windows after a real sequencer tick;
it does not claim continuous music across full paints. FM can sustain an
already-keyed note during that interval. Ordinary incremental typing and
the final renderer latency measurements are separate integration evidence.

All captures remain technical evidence. The audio helper explicitly reported
that this environment does not support audio input. Subjective listening and
music-quality audition are therefore unverified; API success, RMS and stored
WAV files are not labelled as having been heard. The final integration pass
owns adapter/latency checks and the deduplicated suite run. This lane does not
run soak or test-full independently.
