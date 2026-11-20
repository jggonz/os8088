# Boratype reactions and audio

`bt_audio.c` consumes engine events and never changes scores, lesson randomness,
focus or prompts. Its60original lines include 40 roasts, all tagged with an
actual event and severity. Gentle permits severity 1, Classic 1–2, Extra 1–3.
The director detects 3 consecutive errors, below 85% accuracy after 20 attempts,
8 Backspaces, a 10-key recovery, engine 25/50 streak events, poor results and
personal bests. Idle jokes require 30 seconds without a typing event and have
a separate 60-second rate limit. There are no jokes based on beginner speed.
No queue accumulates keystroke reactions. Selection skips the previous 4 caption
IDs, and ordinary reactions/captions have a 219-tick minimum interval (12.03s).
A new record can replace a caption immediately; **voice still obeys 219 ticks**.
The latest 4 captions are available through`bt_reaction_history(0..3)` for results.
Expressions stay active 8 seconds; the caption remains until another reaction.

## Integration

Include`bt_audio.c` in the single C translation unit after`bt.h`. Include
`bt_audio.h` where settings/results need the getters. The package shim includes
`cc/gkmus.inc` after`breadxt/draw.inc` with`BX_ART2`, then
`boratype/bt_voice.inc`. The sound part is:

```
python3 tools/os88gkmus.py part -o build/btsound.bin \
  apps/boratype/art/music.txt apps/boratype/art/sfx.txt
```

It contains music ID 256 and effects ID 257 and is 1205 bytes uncompressed. Set
`bx_setart2` to that part before`bt_audio_init`. The kit resolves the art claim
on every bank use, so compaction never leaves a cached sound segment.

Call init once at entry, tick(now,focused)at most once per OS tick, event(flags,
now)after the engine, state(screen)on screen transitions, and done at close or
failed launch. The caller should present committed results before its finish
event so the director sees new-record and achievement fields. A readonly
instance never preloads or plays audio. Sound enable flags remain unmodified.

Music 0/1/2 is menu/lesson/challenge. Music 3/4/5 is success/poor/record, once,
then a menu or lesson loop. Pausing, focus loss and speech snapshot the kit's
stream positions/durations, release its FM channels, and reacquire patches at
resume. The restored positions retain their phase and new channel IDs. Music
is not restarted on every timer. Music/Effects/Voice and key ticks are separate.
No volume slider is fabricated: OS selection and the real driver's mixer own
volume. The speaker uses the melody voice; FM has 3 voices, with effects on
the kit's separate tone channel. Effects preempt a speaker note; melody resumes
at the sequencer's next note. Error bursts are limited to one per 3 ticks.

## Optional voice

No fetched Borat recording ships in this repository. See
[audio-sources.md](audio-sources.md)for downloaded candidates and rights.
For local import use:

```
python3 tools/os88btvoice.py build /path/to/local/manifest.json -o /path/to/BORAT.VPK
python3 tools/os88btvoice.py check /path/to/BORAT.VPK --wav-dir /path/to/audition
```

Copy`BORAT.VPK` beside the installed`BORATYPE.O88`, then reopen the app. A UI
onwake import can call`bt_audio_reload(path)`; it must never run in paint,
key, tick, or a foreign display bracket. The helper accepts only local files,
requires source/attribution/rights metadata and writes a`.sources.json` sidecar.
It does not treat a download button as permission. `--redistributable` requires
an explicit permission record and redistribution=true for every source.
A source manifest is, for example:

```json
{"rate":8000,"clips":[{"id":0,"tag":"welcome","caption":"Your spoken words",
 "file":"your-recording.wav","priority":1,"expression":0,
 "source_url":"local:your-recording","attribution":"Your name",
 "rights":"Your recording; permission you actually hold"}]}
```

The helper uses ffmpeg to decode/resample, removes DC and exterior silence,
retains 10 ms guards, limits gain to 2x and peaks to 80%, and pads final silence to
one 2048-byte driver block. Oversize clips/packs fail without speech truncation.
8kHzwas the first local measurement; an explicitly chosen6kHzworking set fits
more reactions at lower fidelity. Rates 4000–11025 are permitted.

BTV1 header 16 bytes: magic BTV1, count:u8, format:u8=0, rate:u16,
total:u16, checksum:u16, reserved:u32=0. Checksum is the sum of every byte
except the checksum word, modulo 65536. Each 56-byte directory row contains
id:u8(0..11unique), tag:u8(1..10), priority:u8(0..3), expression:u8(0..6),
offset:u16, length:u16, caption: 48 bytes printable ASCII, NUL terminated.
Offsets are contiguous and ordered after the directory; lengths2048..16384,
multiples2048. No trailing bytes are accepted. The total pack is at most
28672bytes, at most 12 clips. Tags are welcome,error,low,poor,back,idle,recovery,
streak,success,record in that order. The current player chooses the first
matching tag; priority is validated metadata, not a second queue. User packs
should provide at most one clip per tag. Missing tags use the visual reaction
and brief speaker/FM effect. Imported spoken captions are separate from the
original joke, available as`bt_voice_caption()`; label them"Voice reaction:"
so a generic clip is never presented as speech of the original joke.

Preload owns one28KB source claim, pinned during its file read and movable
after validation. A stream owns a4KB driver grant, making
32 KB of source+staging, plus SOUND.DRV's own accounting/DMA fallback if needed.
The relocation callback updates both`bta_pack`and the bridge's`btv_seg`;
playback offsets and captions remain intact. The driver retains only copied
staging bytes. No selector arithmetic is used. All disk transfers use the
claim's aligned base.
The real stream bridge uses verbs7grant,6stage,0open-ring,3status,1feed,2close
and7release; it never uses blocking`SND_PLAY`/`os88pcm_play`. Startup stages at
most4KB, each tick at most 2 KB; total is published after the bytes. The card
consumes the ring asynchronously while the UI returns to typing. At the padded
end, focus loss, mute, close, refusal or driver loss the stream and grant are
released. There is no PCM fallback on a speaker-only system.

`bt_voice_status()`distinguishes missing/invalid pack, memory refusal, mute,
readonly instance, PCM unavailable, ready and playing. A Sound Blaster-capable
SOUND.DRVmust be attached in the OS Drivers control panel and its output route
selected; initialization reads caps without attaching a driver or changing
OS settings. Merely having SOUND.DRVon disk is insufficient. Later driver
attachment is noticed at the next reaction. Device refusal never retries on
every tick and cannot hang the lesson.

## Verification and limits

Host production-C checks: `cc -std=c99 -Wall -Wextra -Werror
 tests/boratype_audio_host.c -o /tmp/bt-audio && /tmp/bt-audio`.
`python3 tests/boratype_audio_check.py` executes the actual 8086 bridge under
Unicorn with a mock stream sink: validates copied PCM bytes, ring wrap/feed,
EOF, busy coalescing, grant/stage/open/status failure cleanup, DS/ES/BP
preservation, and 19 malformed packs through **both** host and 8086 readers.
The mock is not an audible hardware test. The native SmallerC gate passes,
maximum local frame 10 bytes. C code/data is 5232 bytes (2510 text + 360 data + 2362
rodata), BSS 48; voice/pause bridge 958 bytes, BSS 115. Shared gkmus adds its own
code/state. Source pack and driver staging are optional as above.

Affected existing rows are`gkmus`,`sbtest` and the C compiler gates; the root
integration adds the Boratype host gate and runs the deduplicated final suite.
No soak or test-full was run by this lane. Actual guest capture results are
below. Subjective listening and the final 4.77 MHz key-to-visible-feedback
measurement remain open. Host ffprobe/PCM validation and afplay exit 0 do not
establish subjective intelligibility: this agent has no audio-input tool.


## Real guest transport checks

`tests/boratype_audio_target.py` drives the installed package through real OS
keyboard/menu events on the 4.77 MHz MartyPC XT. It proves the exact package,
compressed boot kernel and sound driver match the mapped build. It does not
inject scoring state or call application functions from the debugger. All
waits follow guest state/ticks. The source and staged software disk are private
to each run.

The default generates an original, deterministic 8 kHz unsigned mono PCM
transport fixture using only the Python standard library. Its three captions
say `Test tone: welcome`, `Test tone: error` and `Test tone: success`. It is a
24,760-byte optional pack on the scratch disk, never part of the application.
No network, ffmpeg, restricted recording or separately supplied file is needed
for this routine row. **A synthetic fixture does not pass US10's character
voice/audition requirement.** The optional `--voice-pack` uses a real local
pack instead and requires an evidence destination outside the repository.

```
make boratype mixertest build/os8088-360.img
python3 tests/boratype_audio_target.py --evidence /tmp/bt-audio-sb
python3 tests/boratype_audio_target.py --backend speaker --evidence /tmp/bt-audio-speaker
python3 tests/boratype_audio_target.py --voice-pack /local/BORAT.VPK   --route-probe --evidence /external/bt-local-audio
```

The SB machine is `os8088_xt_vga_sb`, with the mixer test's SOUND-wanted boot
configuration; the speaker machine is `os8088_xt_vga`, with no attached sound
driver. MartyPC records separate OPL, DAC and speaker WAVs. The test checks
menu/lesson/result-stinger waveform levels, written/spoken reaction metadata,
actual source bytes in the driver's staging grant, native PCM completion,
master mute during speech, close during speech and every app-owned heap claim
being freed. The greeting starts automatically on the first focused enabled
tick using that tick's actual timestamp, once; init at tick zero cannot start
it. Master mute, readonly mode and close cancel its pending flag.

With `--route-probe`, actual Control Panel geometry and assembled equates
select the speaker route, detach the actual SOUND driver, reattach it, and
restore the Sound Blaster route. Opening the OS panel first takes application
focus, and the app releases its stream/FM channels before reconfiguration.
This is a supported user route/detach test, **not a hot-unplug or physical
hardware-loss test**. Driver rows are located by their class fields, rather
than treating a class number as a table-row index.

Measured before the renderer's final repair, on source containing platform
sync fix `f23097186`, package SHA-256
`6b38fb45c011b13212ce43c2e727b855c79e3c66c4254ccc97c50323b6fa04c2`:

| Claim | Result | Evidence |
| --- | --- | --- |
| Speaker menu/lesson/error/record stinger | PASS, earlier `cde9eb842` build | RMS 0.654 / 0.623 / 0.278 / 0.611 |
| Speaker mute and close | PASS | zero mute waveform, gate off, no owned heap claims |
| OPL menu/lesson/record stinger | PASS | RMS approximately 0.0104 / 0.0027–0.0029 / 0.0105 |
| Automatic and manual PCM greeting, error reaction | PASS transport/content | 4,096 staged bytes match source per reaction; real captured waveform |
| Captured PCM source identity | PASS | correlation 0.983 for synthetic 8 kHz, 0.938 for local 6 kHz greeting |
| Master mute during live PCM | PASS after platform sync fix | stream/grant cleared before repaint; OPL residual RMS 0.000049 |
| Real OS speaker route and detach/reattach | PASS | speaker RMS 0.660; restored FM; no hardware-loss claim |
| Close during PCM and heap cleanup | PASS | no stream/key-on, no owned heap claims |
| Uninterrupted speech | **FAIL before final renderer repair** | real driver pauses at 4,096 consumed bytes before clip end |
| Subjective listening / intelligibility | **BLOCKED** | no audio-input/listening tool; never inferred from API success or RMS |

The strict continuity assertion checks `sbl_paused` and `sbl_consumed` while a
clip is live. A pause before consumed reaches the clip length fails the row.
This matters because the DAC capture only contains active samples and can
retain a high source correlation while the guest pauses between refills.
The 4 KB ring supplies 512 ms at 8 kHz, about 680 ms at 6 kHz; a longer render
burst can starve it. Both local and synthetic captures exposed that defect:
13 paused observations during the manual greeting and five during the error
reaction, at consumed = total = 4,096. Final evidence must rerun this assertion
after the rendering repair; waveform activity alone is insufficient.

The positive transport/cleanup runs took 104.0 s (default synthetic SB),
117.7 s (local pack plus OS reconfiguration), and 37.7 s (speaker). Those are
measured historical times, not final row budgets. `--negative` disables Music
through the actual Settings UI and fails the ordinary title-waveform assertion.
`--validate-report PATH` reruns signal/continuity assertions over recorded
telemetry; the captured underrun record demonstrably fails it.
