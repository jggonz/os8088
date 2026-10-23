# VIDEO-TICK-PLAN: a steady slower rate, by a finer tick

**Status: PLAN, nothing built** (the owner, 2026-10-19: "steady frame
timing by a higher tick rate - lets plan it", after the first set of
encodes ships).

## 1. The finding

A `.V88` has ONE frame period (`rate / spf`, SPEC.md 98.1), and a frame is
on the screen a whole number of them. So the degrade ladder's `slow:5/6`
(VIDEO-SEEN-PLAN 3a) at 20 fps can only be 1, 1, 1, 1, 2 periods: four
frames for 50 ms and one HELD for 100 ms. `vfr_pattern` already spaces the
holds as evenly as that allows inside a stretch; what the eye reads as a
jerk is the held frame itself, and the rate changing at a stretch's edge
between 20 and 16.7.

The owner's two encodes of Gas Gas Gas are the two ends of it: 24 fps with
`slow:5/6` jerked, and 20 fps for the whole clip is steady but slower
everywhere, including where 24 fitted. A steady 16.7 needs a frame to last
1.2 periods, which one period cannot express.

## 2. The idea

A **finer period**, so a content frame lasts a varying whole number of
TICKS: at a 60 Hz tick, 20 fps is 3 ticks a frame and 16.7 is 3, 4, 3, 4,
4 - an uneven step of 17 ms, against today's 50. The VGA's own refresh is
70 Hz and a flip lands on a retrace (98.3.8), so the glass is already
quantised near there; at that point a slower rate looks like a slower
rate, not a stutter.

## 3. What has to be decided first

1. **How a frame says how long it lasts.** Either (a) empty records at the
   tick - a HELD frame is already one (98.1.3.9), so a 3-tick frame is a
   record and two empties - or (b) a duration in the record header, so a
   frame is one record whatever it lasts. (a) needs no new record kind and
   costs a record's header and the hook's call per tick; (b) is a format
   change (a flag, refused by an older player) and costs nothing per tick.
   Measure (a)'s overhead first: HOOK_CYC per tick at 60 Hz is three
   times today's at 20.
2. **The sound.** A record carries its period's audio (`abytes`): at a 60 Hz
   tick, 22,050 / 60 is not whole (367.5) - the rate's own divisor is
   22,050 / 368 = 59.92 Hz, which is what `spf` already allows.
3. **The budgets.** Every budget is per frame today (the CPU's ceiling,
   the disk bucket's refill, the lag schedule's owed time, 98.2.1.1). A
   frame that lasts 4 ticks has a third more of each than one of 3; the
   model must charge by ticks.
4. **The ladder's `slow` rung** becomes a rate in ticks and is spread
   evenly; and between stretches the rate should RAMP (3, 3, 4, 3, 4, 4)
   rather than step, so the edge is not a jerk of its own.
5. **Page-ahead** (98.2.1.8) builds the hidden page over held periods;
   at a finer tick a hold is a tick, and its arithmetic is per tick too.

## 4. Gate, before any of it ships

A clip of steady motion (a pan) encoded at 16.7 fps by `slow:5/6` today
and by the finer tick: the frames' display times read off MartyPC's VGA
(`m.pace()`, PERFORMANCE.md Part 3.2) - today's must show the 100 ms hold,
the new one no step longer than 67 ms.
