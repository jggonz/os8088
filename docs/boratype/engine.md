# Typing engine implementation

`apps/boratype/bt_lessons.c`, `bt_core.c`, and `bt_codec.c` are included in the
native package's single C translation unit. The same production sources are
included by `hosttest/engine.c`; only that harness uses wider oracle arithmetic.
The engine calls no OS, file, rendering, audio, heap or worker functions.

The 24 authored lessons declare their own alphabets, newly introduced keys,
instructions and passages. `bt_validate_lessons()` checks printable ASCII,
unique alphabets, gradual introduction, declared drill characters, nonempty
instructions and the 192-character passage bound. Mastery is a complete lesson
with the displayed rounded accuracy of at least `BT_MASTERY` (95).

Session state has one writer, the UI task. Start captures the profile number;
switching the selected profile cannot redirect a running result. Printable
attempts are historical, while net accepted progress can retract within the
current passage. Per-key samples belong to the expected character, including
space and shifted punctuation, rather than the attempted wrong key. They
commit with the frozen result exactly once. Lifetime samples are halved when
merging would overflow, preserving bounded rates; historical errors cannot
exceed historical attempts. Weak-key labels require 20 samples and at least
one error. Their ranking compares exact fractions, with larger samples and
then ASCII order breaking ties.

`bt_start(mode, lesson, duration, difficulty, now)` takes duration indices
0/1/2 for 60/180/300 seconds and difficulty indices 0/1/2/3 for home row,
letters, digits and mixed. Practice with `lesson == -1` builds a varied drill
from the five strongest sampled weaknesses, with the selected group as a
fallback. Its xorshift seed belongs solely to the session. Identical parameters
and start ticks reproduce the same drill; drawing and reactions cannot consume
its randomness. Guided lessons and practice finish after one passage.
Challenges refill passages in memory and retain completed progress across a
refill; Backspace never reaches into that completed passage.

The first printable attempt, including a wrong attempt, starts guided timing
at zero elapsed ticks. Challenge timing starts after a three-second countdown,
even without input. `bt_seconds` implements the exact floor of
`ticks * 65536 / 1193182` using two words. Deadlines use the ceilings 1093,
3278 and 5462 active ticks, respectively. A delayed callback consumes any
countdown overshoot and clamps a finished challenge to its deadline. Unsigned
subtraction handles one wrapping 16-bit clock sample; callers must sample at
least once per clock wrap. Pauses exclude time. Pausing a challenge, including
its ready countdown, makes it unranked. `bt_finish(now)` samples through now
and freezes; a deliberately early challenge finish is unranked. All runs
freeze before a 16-bit count or elapsed accumulator can overflow.
An uncommitted finished result keeps reporting `BT_EV_FINISH` on ticks, so
a deadline discovered by a void pause/focus transition cannot lose its result.

Accuracy is nearest whole percent of K/A; WPM is floor(12*N/active_seconds).
The face shows a dash when A or active seconds is zero. `bt_muldiv` computes a
full two-word product and saturates a quotient beyond 65535. `best_wpm` is
separate from `baseline_wpm`, which smooths measured completed-run speeds for
reaction policy. Duration/difficulty records accept ranked challenges only.
Achievements, lesson progress, four recent results and the next unmastered
recommendation update in `bt_commit`, protected by its committed flag.
`practice_minutes` totals whole active minutes per result.

The platform owns persistence paths, write ownership, replacement, disk errors
and generations. The codec is a fixed 2136-byte canonical image:

| Offset | Field |
|---|---|
| 0 | ASCII `BTT1` |
| 4 | little-endian version word, 1 |
| 6 | little-endian length word, 2136 |
| 8 | generation word; wrapping comparison belongs to platform |
| 10–16 | selected profile, music, effects, voice, key tick, roast, display |
| 17 | reserved zero |
| 18 | Fletcher-16 word, both checksum bytes treated as zero during calculation |
| 20 | four explicit 529-byte profile rows |

Each profile row holds name[13], lesson best[24], lesson done[24], expected-key
attempt words[95], error words[95], record WPM words[3][4], record accuracy
words[3][4], achievement/best-streak/practice-minute/baseline-WPM/best-WPM words,
next-lesson byte, recent WPM/accuracy/seconds words[4] each, recent mode bytes[4]
and next-ring byte. No file field depends on native structure padding.
Names are bounded printable ASCII, NUL-terminated, with zero canonical tails;
empty names remain editable. Boolean and enum ranges, lesson consistency,
error/attempt ordering, achievements, record accuracies and recent results
are validated. Truncation, extra bytes, unsupported version, bad checksum or
bad fields return zero without changing any live profile or session.
Only a fully validated staging store replaces the live store. Codec entry
bodies use the package's embedded eager C overlay; hot scoring stays resident.

Run `python3 tests/boratype_host.py` for the production oracle. It checks all
65536 tick values, 200000 randomized full-width products/divisions, exact weak
ratios, all lessons, strict and corrected input, independent 30 WPM/94-percent
values, pauses/wrap/deadlines, counter limits, profile attribution, one-time
commit/awards, deterministic practice and all save-byte corruptions. Four
disposable mutations must fail: wrong WPM credit, advancing on wrong input,
disabled integrity validation and an invalid early-lesson character.

`hosttest/native.c` and `native.asm` are compile-only fixtures using the actual
runtime and embedded overlay. They do not provide an alternate deliverable.
Guest input, save/reboot, visual output and XT timing remain integration checks;
host success alone does not prove those stories.
