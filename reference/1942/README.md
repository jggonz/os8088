# 1942 cartridge pacing reference

Inspected the user's local `1942.nes` with the payload digest accepted by
`tools/1942nes.py`. The ROM and disassembly remain local; these are original
implementation notes. No emulator or third-party package is needed to build
the native game.

- `$DA21..$DA53` reads the four-byte wave records at `$EB19`: aircraft type,
  period, batch size, remaining aircraft. `$DA81..$DB42` decrements each
  timer once, reloads the period, and allocates a batch. The native engine
  previously decremented by three, making the opening much denser.
- `$DAC2..$DACE` searches actor slots `$0E..$15`: eight regular aircraft.
  The secret aircraft uses `$18`. The native storage remains large enough
  for renderer stress fixtures; gameplay allocation observes these limits.
- `$FA32..$FAFF` throttles fighters through a shared opportunity counter
  (`$07C7`), compared with the current stage/page byte shifted right twice.
  After an opportunity it resets to `$FF`, including rejected shots. This
  is not a separate repeating weapon timer on every plane.
- `$FA55..$FA88` calls the relative-coordinate rectangle check `$AA00`
  with bounds `$70/$90` (112/-112), then `$20/$E0` (32/-32). Shots require
  the outer rectangle and reject the inner rectangle. `$FA89..$FABE`
  requires aim sectors 6 through 10 and checks aircraft heading. `$ADC9`
  quantizes relative coordinates using the quadrant lookup at `$B1FB`.
- The entry routines selected through `$EFF2` set bit 1 of `$048E,X` for
  types 9 through 33, excluding them from ordinary fighter fire. This
  includes the orange formations. The secret plane is also unarmed.
- `$FABF..$FACD` searches projectile slots `$03..$0A`: eight enemy bullets.
- `$FB00..$FC52` uses a separate shared counter and timed bursts. The
  records pointed to by `$FD2F` contain count, spacing, and signed aiming
  offsets. `$FF` in the source record means minus one, not a terminator:
  the cartridge adds the base aim before marking consumed shots `$FF`.
  Ordinary bomber patterns have three shots spaced five updates apart or
  five shots spaced two apart. The giant bomber has seven shots spaced
  one update apart. The former native code emitted three shots together.

The full campaign review covers all 32 schedules (851 event records), all
49 wave descriptors, and all 39 path pointers, including shared path suffixes.
Additional findings now implemented in the native engine:

- `$DE42` scrolls by -$00C0, or 3/4 pixel per actor update. Advancing two
  pixels while ticking waves once compressed the route relative to combat.
  Both adapters now use the same logical clock; CGA accumulates paired rows.
- `$071F` is a shared sequential entry index, not a random sample per actor.
  It persists across stages. `$F054` uses an eight-entry mask; its eighth
  entry overlaps `$F14A`. Actor page bits must be masked, as `$B0BC` does.
- `$B1D7` subtracts origin ($80,$70), with 240-line Y pages. Previously all
  imported aircraft paths were shifted upward by sixteen pixels.
- `$AF7A/$B048` multiply the signed 8.8 direction vectors at `$B2FB` by each
  aircraft's speed. Orange formations use speed 21, rotating fighters 10,
  medium aircraft 6, and large bombers 11. The generic two-pixel waypoint
  follower particularly distorted the orange formations.
- `$F176/$F34C` select circular sweep centers from `$FC53`; `$B33B` supplies
  radius vectors. `$F054/$F0CE` reverse aimed divers; `$F42C/$F4BA` turn
  descending attackers into diagonal passes, not horizontal strafing.
- `$F623` only checks waypoint arrival while visible. Outside the viewport,
  planes retain their heading until `$B37B` retires them. Reading onward
  through adjacent path records caused spurious repeated bottom entries.
- `$F6E8` puts large bombers at X=64 or 192 according to the player's half,
  ascending before they join their shared path. Stage one does schedule
  type 48 twice, at native route distances 688 and 1280: these are real
  bottom-entry bombers, distinct from the opening top-entry fighters.
- `$DA14` resets the shared orange group at wave IDs 16,17,18,23,28.
  Staggered formations can occupy five wave descriptors; they now share
  a reward group instead of awarding a POW per plane.
- `$D9CF` compares the scroll register, excluding the HUD offset. Combat
  starts after the 45 pixels scrolled by `$D901` during takeoff.
- `$DB18` releases ordinary wave records after spawning completes, without
  waiting for live aircraft to depart. `$D9C5` skips events from a page that
  has passed while the queue was saturated.
- `$DDA4` starts the giant encounter at page 2, Y<$90, after clearing waves.
  `$DC86/$DCB3` clear final-page actors at Y<=$80, then land at Y<$61.

The game remains a native adaptation: simulation updates are capped at the
18.2 Hz system clock rather than real-time 60 Hz NES frames. Slower machines
can fall below that rate under load; missed updates are not replayed.
Takeoff, landing visuals,
collision boxes, hit points, integer bullets and the giant bomber's single
muzzle remain native. Waypoint steering performs one movement update per
native frame instead of reproducing the cartridge's cooperative coroutine
instruction order; small differences at tight turns and reversals remain.
The proximity rectangle still also gates bomber fire by prior request,
although the original `$FB00` routine does not use the fighter rectangle.

`tests/n1942.py` checks assembled 8088 wave cadence, capacity, full-pool
retries, forbidden/allowed shots, shared counters, aim quadrants, bomber
sequences and reset state on VGA and CGA, alongside rendering and XT
guest-cycle performance. Asset checks compare the imported aiming and
burst tables directly with the cartridge bytes.

As a separate local check, executing `$FA32` in a 6502 interpreter with a
player at (120,208) rejected aircraft at (120,32), (120,190), and (8,140),
and an upward-facing aircraft at (120,140). The downward-facing aircraft
at (120,140) fired. The native regression exercises the same cases. The
interpreter was used only for this investigation and is not a dependency.

Campaign regressions execute all 32 stages through the assembled 8088
scheduler and movement routines, verify all events are consumed and each
stage reaches landing, check every scheduled motion family leaves the
screen, and exercise staggered formation rewards and escape invalidation.
Straight-flight coordinate checkpoints were independently measured by
executing the original 6502 actor routines locally (with collision/fire
calls disabled); the comparison does not add an emulator build dependency.
