# Borat Teaches Typing delivery plan

Base: 528648017d103f335fa6fdbfbb0404ab92fb786f (2026-11-20 main).
Contract: [BORATYPE-SPEC](../BORATYPE-SPEC.md), SPEC.md §134.
Diary: [How it was built](../HOW-BORATYPE-WAS-BUILT.md).
Final evidence and limits: [acceptance report](../boratype/EVIDENCE.md).

S means observed in the installed native guest; B is a specific verification
blocker; R is removed by the owner. Host checks supplement guest evidence.
The original implementation was local. The owner requested a pull request
after the VGA redesign and the interactive 86Box run.

| Story | Delivered outcome | Status | Evidence |
| --- | --- | --- | --- |
| US01 | installed launch, profile names, native menus/About/close | S | VGA/CGA guest tour; standard About registration |
| US02 | 24 authored lessons, character validation, 95% mastery, any lesson accessible | S | production oracle + actual beginner/mixed completions |
| US03 | strict input, historical attempts, Backspace/retype, segmented passages | S | native wrong/correct/correction and independent counters |
| US04 | full US QWERTY, punctuation, opposite Shift/finger/target/error hints | S | all 95 printable map checks + shifted guest lesson |
| US05 | exact PIT WPM/accuracy/streak/time and frozen results | S | independent arithmetic + native counters/deadlines |
| US06 | deterministic group and sampled weak-key practice, reasons | S | actual 19-error F diagnosis, recommended drill and digits selection |
| US07 | 1/3/5 minute challenges, ready countdown and unranked pauses | S | 1093/3278/5462 ticks; ranked/unranked tests |
| US08 | results/recommendations, next/retry/weak actions, seven achievements | S | full sessions + actual empty/poor result cards |
| US09 | 65 original captions/40 roasts, intensity/cooldown/anti-repeat, comeback | S | actual three-error/low/poor/comeback; host policy |
| US10 | recorded voice clips | R | owner: effects and visual messages only |
| US11 | three composed loops and stingers | S/B | actual speaker/FM waveform/routes; subjective listening unavailable |
| US12 | responsive effects, priorities, mute, cleanup | S/B | actual sound captures/leases/cleanup; subjective listening unavailable |
| US13 | clipping/focus/minimum resize/instances and writer exclusion | S | native window tour and actual second instance |
| US14 | native fullscreen and CGA foreign-mode transitions | S | midlesson preservation, running fullscreen faces, Ctrl+Q restoration |
| US15 | CGA/EGA/VGA/VESA; high-memory ordinary package on Mega286 | S | actual adapter/mode assertions and inspected captures |
| US16 | compact minimum 480x128 client, keyboard controls | S | actual shrink/resume/type/restore on VGA/CGA; 24 host geometries |
| US17 | four named profiles, validated generations, safe failures/reboot | S | all profiles after cold boot; seven actual volume cases + 35 host faults |
| US18 | bounded changed-region work and measured XT feedback | S | 30 keys within 97.978ms to framebuffer; full-paint exception in evidence |
| US19 | help/settings with real controls and sensible unsupported-state notices | S | native Help/settings/toggle/save; no removed voice controls |
| US20 | self-contained O88, four software disks, existing-format WPK | S | installed360 KB disk tours + deterministic package/disk/archive builds |

## Delivery and QA record

Foundation contracts and story list preceded implementation. Engine, face
and audio lanes worked in isolated worktrees; the lead integrated them before
the combined native QA. Bitmap sources, exact prompts, fonts, scores and
converters are committed; no generation service runs in make or in the guest.
The original checkout and its running machines were left alone.

The first integrated run exposed busy unchanged-paint scans, oversized
portrait work and small typography. Repairs reused clipped native indicator
primitives and cached/sliced drawing. Subsequent failures found CGA full-grid
cost, countdown time spent during initial painting, Help returning to a new
lesson, mute policy applied after drawing, minimum shrink lacking a resize
callback, no-input success praise, recent-four neutral-caption exhaustion,
profile mouse selection retaining name-edit state and a misleading digits
label. These were repaired and the affected cases rechecked. The build diary
records the measured causes and preserves the difference between host and
native evidence.

The final deduplicated 18-row family passed 18/18 without skips. Additional
running-fullscreen captures closed a visual-evidence gap; two emulator boot
hangs were retried alone and passed. The final digits-label correction then
passed focused face/practice/XT performance checks. A final canonical-Marty
run exposed a 121ms caption line; four-row strips brought all 220 periodic
calls below 100ms, with no relaxed threshold. A test breakpoint-cleanup race
was corrected before the timing row passed end to end. Shared renderer,
SDK, kernel and sound-kit sources were reused unchanged; their generic
regressions remain in the repository-wide full gate rather than a repeated
unrelated application soak.

`make test-full` and exact budget status are recorded in the acceptance
report. No unfinished audio audition is marked complete: the audio-input
tool explicitly cannot listen, and actual WAVs remain available. Physical
hardware was not exercised. The final full gate also required the standard HIGH/HIGHMEM declarations;
actual 286 high-region/high-band complete lessons, profiles and cold reboot
passed. Six initially failed repository rows passed on repair; exact budgets
passed for all four kernels. Full native paints can interrupt speaker music
for about 7.3 seconds; Exact reproduction of the mockups and continuous music across
those paints are not claimed.

## Visual revision from the player's first session

- [x] Rebuild the masthead, navigation, bevels and package lettering from the
  supplied VGA references. Keep controls live in windowed and fullscreen play.
- [x] Recompose the lesson and timed challenge around the prompt, typed line,
  statistics, scenic tutor, speech bubble and colored keyboard.
- [x] Give lesson selection, records, achievements, results and settings their
  own illustrations and controls. Drive them with actual selections and scores.
- [x] Measure complete portrait changes on the XT and check hardware-key
  latency during drawing. Stage the large native poses around the lesson
  lifetime so two copies can open on a 640 KB machine.
- [x] Inspect the guest screens, correct defects and launch the writable
  386 VGA/Sound Blaster machine for interactive use.

See the [native VGA captures and animation](../boratype/EVIDENCE.md#vga-redesign).
