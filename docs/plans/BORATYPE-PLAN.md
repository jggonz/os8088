# Borat Teaches Typing delivery plan

Base: 528648017d103f335fa6fdbfbb0404ab92fb786f (2026-11-20 main).
Contract: [BORATYPE-SPEC](../BORATYPE-SPEC.md), SPEC.md §134.
Diary: [How it was built](../HOW-BORATYPE-WAS-BUILT.md).

Status: P planned, I implemented/integrated, S seen in the real guest, R removed by owner scope.
No story is complete from a host check alone. The user requested local work;
no PR, push, website publication or release is part of this build.

| Story | Outcome | Status | Verification |
|---|---|---|---|
| US01 | installed launch, profile, menus, About, close | I | fresh guest |
| US02 | 24 declared and validated lessons, 95% mastery | I | host + beginner lesson |
| US03 | strict input, correction, segmentation | I | oracle + injected events |
| US04 | accurate QWERTY, Shift/finger/mistake hints | I | geometry + guest |
| US05 | honest tick-based WPM/accuracy/streak | I | independent arithmetic |
| US06 | reproducible groups/weak keys | I | sample thresholds + guest |
| US07 | 1/3/5 minute tests, unranked pauses | I | no-input deadlines |
| US08 | results, next/retry/practice, achievements | I | full session |
| US09 | 48 reactions, 24 roasts, thresholds/intensity | I | host + guest |
| US10 | recorded voice clips | R | owner: sound effects and visual messages only (2026-11-20) |
| US11 | three composed loops + stingers | I | scores + listened captures |
| US12 | effects, priorities, cleanup | I | listened guest capture |
| US13 | window clipping/focus/resize/instances | I | guest interaction |
| US14 | safe fullscreen transitions | I | native latch + CGA FSX |
| US15 | CGA/EGA/VGA/VESA paths | I | adapter evidence matrix |
| US16 | compact layouts, keyboard controls | I | minimum geometry |
| US17 | four profiles, validated safe persistence | I | failure tests + reboot |
| US18 | XT responsiveness and bounded work | I | MartyPC cycles |
| US19 | working help and settings | I | guest click/key tour |
| US20 | four disk geometries and WPK | I | packaged installation |

Waves: foundation/platform map; independent engine/UI/audio; integrate and
boot to inspect; all adapter/audio/persistence QA; repair; final affected
soak union and test-full once. Shared kit regressions: gkmus, gkdemo, cc8086,
cmemmove, breadxt_target, fullscreen. Exact rows will be checked in suite.py.

## QA and evidence

Native VGA launch and home rendering have been observed in MartyPC and QEMU.
The first integrated QA run failed: an unchanged one-tick paint scan kept the
UI busy, and portrait updates consumed seconds on the XT. The repair round is
in progress. Scoring, transitions, save/reboot, adapter paths and audible audio
remain unverified in the real guest. Art references are design input, not
evidence. The owner removed recorded voice clips from scope during the repair round.
Original composed music, sound effects and visual jokes remain. US11/US12
need actual listening, not successful API returns. I means integrated, not done.
