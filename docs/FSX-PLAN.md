# Fullscreen exclusive — the plan (fsx)

Status: **IMPLEMENTED** through phase 3. SPEC.md §53 is the binding version
(written first, per the §1 rule); `kernel/fsx.inc` is the module,
`tests/fsxtest` the gate, and Missile Command's Mode X path the shipped
reference consumer — all verified under QEMU on VGA, forced-CGA and
forced-Hercules, restore-equality and both sound directions included.
Phase 4 (Tracker + `FSXF_KEEPWORKER`'s implementation — the contract and
the whitelist hook are already in) is next. This document stays as the
design record; where it and SPEC.md §53 disagree, §53 wins.

## 0. What this is

§11.2's fullscreen surface is a real window: the desktop's mode, the kernel's
primitives, the cursor, the event ladder — all still live, which is exactly
right for Tracker and ArtfulType. This plan is the other thing: **the app
borrows the machine**. Multitasking suspended, every kernel drawer parked,
and — the part §11.2 cannot offer — **the video mode is the app's to change**:
all four CGA modes, both Hercules modes, four standard VGA modes. Mode 13h's
320x200x256 is the headline; a real text mode for a roguelike is the sleeper.

The two coexist, and the first two consumers are committed: **Missile
Command** (the reference — Mode X at the arcade's own raster, section 9 item 3) and
**Tracker immediately after**, whose worker-fed audio ring is what §7's
`FSXF_KEEPWORKER` opt-in and §6's present clause exist for — both designed
in from the start because that adoption is a certainty, not a maybe. §11.2
stays the right answer for apps that want the desktop's mode with the
desktop alive underneath — ArtfulType stays put.

## 1. The shape: a bracket, not a latch (the load-bearing decision)

`OSAPI_FSX_RUN` is called from a window callback — UI-task context, gfx lock
held, the `wm_fullscreen` contract — and **does not return until the app is
done being fullscreen**. The kernel suspends the world, far-calls the app's
"exclusive main" through the window's own `W_DISP`/`W_SEG` dispatcher (the
`wm_pkgcall` idiom — the proc is an ordinary near proc, no `retf` for the
author to forget), and when that proc returns, restores everything in one
place and returns to the callback.

Why not an enter/exit pair like `wm_fullscreen`? Because a latch that
survives across callback returns means the UI task's event ladder runs
between callbacks **with the screen in a foreign mode**, and then every
invariant in §12/§13 needs an fsx gate: the menu-bar clock, `fdlg_reap`, the
event ladder's four branches, every background painter's visibility recheck,
the cursor. §11.2 needed exactly two such gates because it keeps the mode and
the primitives; a foreign mode needs dozens, each one a silent-corruption bug
when missed. The bracket makes the only rule that matters — **the kernel
never runs while the mode is foreign** — true by construction, because the
kernel is parked on the stack underneath the app. Precedent: `osapi_snd_play`
blocks; the §38 dialog is modal; both are cheap for the same reason.

Consequences, all free:

- No second input model. The bracket runs on the UI task, the one context
  allowed int 16h (§7's BIOS rule), so the app polls the keyboard directly.
  The mouse ISR keeps `mouse_x/y/btn` fresh throughout (it never draws — see
  §3), so `OSAPI_MOUSE` answers live; the app edge-detects buttons itself.
  No events are dispatched — there is no ladder running to dispatch them.
- The file API is legal mid-bracket (it is UI-task-context-only, and this IS
  the UI task) — a game can load level data between modes. It takes
  `sch_lock` around int 13h exactly as it always does; the tick still chains.
- Task 0 cannot `task_exit`, so the "worker dies inside the bracket and
  strands the machine" class of bug is structurally unreachable.
- Stack: the whole session lives in nested frames on STK0. A few dozen bytes
  deeper than a long callback today; bounded and measured at implementation.

## 2. The freeze: a scheduler whitelist, not `sch_lock`

`sch_lock` is the wrong tool, and sched.inc already documents why: it stops
the *voluntary* path too (`task_yield` self-resumes under it), and — the
sharper edge — it would starve a loaded sound driver's refill worker, the
exact hazard `snd.inc` and `sb.inc` each call out by name. A fullscreen game
with Sound Blaster music is precisely the customer here.

Instead:

- **`T_FLAGS`** — the task record's existing padding byte at offset 7 becomes
  a flags byte. `T_SIZE` stays 8, the CL-shift indexing is untouched, no
  layout moves. Bit 0 = `TF_SERVICE`: set on tasks spawned through
  `osapi_drv_task` (cell 0x0248 — the sound driver's refill/drain workers)
  and nowhere else. Everything `task_spawn` creates defaults to 0.
- **`[fsx_task]`** — one `.bss` byte, 0xFF = off, else the slot number of the
  exclusive task. Armed and cleared under `pushf`/`cli`.
- **One test in `sch_switch`'s scan**: a task is eligible iff `T_STATE` = 1
  AND (fsx off, OR slot = `[fsx_task]`, OR `TF_SERVICE`). ~10 bytes, on the
  switch path only.

What keeps running, by construction: the int 08h chain to the BIOS (floppy
motor), `sch_account`, `snd_tick` inside IRQ0 (tones and FM sustain — an
OPL2 note needs no task at all), the wake scan (sleepers mark ready and
simply wait unpicked; no new state, they run normally the moment the bracket
ends), and the driver's `TF_SERVICE` workers — so a Sound Blaster stream
keeps playing across a mode switch. What freezes: every Clock, Bounce,
sampler, other packages' workers, and the caller's own worker (but see the
§7 opt-in). The cooperative watchdog needs no special case: when it forces a
switch, the whitelist means it picks a service task or resumes the exclusive
task — harmless either way.

One behavioural note to document in the SDK: inside the bracket,
`task_sleep` on the UI task degenerates (nothing else eligible → `sch_switch`
resumes the outgoing task immediately, the "nothing ready" path that today
only task-0-never-sleeps makes unreachable). Pacing inside the bracket is
`OSAPI_FSX_WAIT` (§6) or polling `[ticks]`, and the SDK says so.

### 2.1 The freeze silences what it strands

"Sound keeps running" is only half a rule, and the missing half is a bug
with three faces. Finite-duration tones are fine — `snd_tick` still runs
and expires them. But a **duration-0 tone, a keyed FM note or a stream
owned by a frozen instance** has an owner whose "off" call can never run:
the note sustains for the whole session. Second face: that frozen owner
still holds the tone channel **at its priority** in the §34.3 router, so
the fullscreen app's own `snd_tone` is refused (CF=1) for the entire
bracket by an owner that can never release — a permanent refusal coded
like a transient one, the exact failure §48 already names. Third face: a
foreign stream's feeding worker freezes, the SB driver underruns and parks
the DMA armed-but-paused until the bracket ends.

All three have one fix, and it already exists: **entry walks `inst_tab`
and calls `snd_release_inst` for every instance except the caller's** —
the same routine both §29.4 teardown paths call. It routes the driver's
`DSV_RELINST` verb first (FM key-off, stream teardown), flags down a
running clip, silences the tone channel via `snd_town_off`, and bumps
`[snd_gen]` — which is what makes the thaw safe: when a frozen owner wakes
after the bracket and finally issues its "off", the generation mismatch
means the stale call cannot kill a note the fullscreen app is playing by
then. The router's own comment states the principle — "the release
ignores priority: the owner is gone" — and frozen is the same condition
with a delay: an owner that cannot act must not keep grants it cannot
manage.

The release runs **after** the freeze is armed, so no foreign task can
take a fresh grant between the two. Exemptions, both deliberate: kernel
grants (0xFF — finite beeps, `snd_tick` expires them) and the caller's
own (it keeps running and manages its own sound — which is exactly what
lets a `FSXF_KEEPWORKER` game carry its stream into the bracket). There is
no symmetric release on exit: the caller is alive and responsible
afterward, the same rule a windowed app lives under, and §29.4 teardown
still catches whatever it leaks at close.

## 3. The gfx lock stays held for the whole bracket

The caller enters holding it (callback contract); `fsx_run` never releases
it. Three things fall out, none of them new mechanism:

- **The cursor never draws.** The mouse ISR's first gate is
  `gfx_lock_flag`; it updates coordinates and sets `cur_dirty`, nothing
  more. At exit, the callback's ordinary epilogue `gfx_unlock` runs
  `cursor_show`, which takes a fresh save-under from the freshly restored
  VRAM — §7's existing contract, zero new code.
- **The lock is the fence.** Anything that somehow runs and tries to draw
  (a `TF_SERVICE` worker gone wrong, a §7 opt-in worker) parks in
  `gfx_lock`'s yield-retry loop until the bracket ends. Self-serializing,
  not corrupting.
- **`wm_clip_set` state, menu save-under, drag outlines** — none can exist,
  because all of them live inside single lock holds owned by code that is
  not running.

## 4. Mode switching — `OSAPI_FSX_MODE`

Mode ids (`FSXM_*`), availability decided by `[vid_kind]` alone — a fact,
not a guess, per §47:

| id | mode                    | set via                     | VGA | CGA | HERC |
|----|-------------------------|-----------------------------|-----|-----|------|
| 0  | 80x25 text              | int 10h AX=0003 / mode 7 on Herc (`vid_text` body) | Y | Y | Y |
| 1  | 40x25 text              | int 10h AX=0001             | Y | Y | – |
| 2  | 320x200x4 (CGA)         | int 10h AX=0004             | Y | Y | – |
| 3  | 640x200x2 (CGA)         | int 10h AX=0006             | Y | Y | – |
| 4  | 720x348 mono (Herc gfx) | 6845 direct (`vid_setmode`'s Herc body) | – | – | Y |
| 5  | 320x200x16 planar (0Dh) | int 10h AX=000D             | Y | – | – |
| 6  | 320x200x256 (13h)       | int 10h AX=0013             | Y | – | – |
| 7  | 640x480x16 (12h)        | int 10h AX=0012             | Y | – | – |
| 8  | 320x240x256 "Mode X"    | int 10h AX=0013 + unchain pokes | Y | – | – |

Ids 0–3 are "all four modes CGA supports" (the colour/BW pairs 0/1, 2/3,
4/5 collapse; we set the colour variant). Ids 0 and 4 are Hercules text and
graphics. Ids 5–8 are the VGA four, **chosen**: 320x200 and 640x480, each
in 16 and 256 colours — with one substitution forced by the hardware.
**640x480x256 is not a VGA mode**: 307,200 bytes exceeds both the 64KB
real-mode window and what standard VGA addressing can display; it is SVGA,
bank-switched behind vendor registers with no portable interface until
VESA VBE — nothing in this repo's machine matrix can set it. What that
wish was reaching for — more 256-colour capability — is **Mode X**:
mode 13h plus a short standard-register unchain sequence (~30 bytes in the
mode table), running on every real VGA and faithfully emulated by QEMU and
86Box. It buys square pixels at 4:3 and, because unchaining frees the
card's full 256KB, **three display pages** — page-flipping against
`FSX_WAIT`'s vsync, which single-page 13h cannot do. The cost is
planar-style addressing (map-mask plane select), which the info block
already describes; 13h stays in the set as the dead-simple linear model.

The enum is open-ended; appending costs one table row. 10h (640x350x16)
or 11h (640x480x2) land that way if ever wanted, and an SVGA/VBE mode —
if such machines ever become a target — arrives as a *loadable video
driver* the way sound and hard disks did, not as a change to this table.
One footnote for later: `vid_detect` step 2 admits EGA-class cards as
`VID_VGA`, and an EGA sets 0Dh but not 12h/13h; if those machines ever
matter, `FSX_CAPS` is where the finer probe lives — the desktop's
detection does not change.

- **`OSAPI_FSX_CAPS`** — out AX = bitmask of settable ids for the live
  adapter (bit n = id n), DL = `vid_kind`. Callable from any context, lock
  held or not (the `OSAPI_VIDEO` precedent), so an app can grey its own
  mode menu per §47 *before* entering — one predicate, shared by the greying
  and the refusal.
- **`OSAPI_FSX_MODE`** — legal only inside the bracket, from the exclusive
  task. In AL = id, ES:DI = a 16-byte info block the kernel fills:
  framebuffer segment, width, height, stride, flags (text / planar /
  banked), bpp, bank count, bank step, display pages. Exact offsets pinned
  in SPEC.md at implementation. Out CF=1 = not on this adapter (the caps
  bit again). The mode set clears the screen (BIOS does; the Hercules body
  keeps its load-bearing 32KB `rep stosw`).

Rules that hold it up:

- **The kernel's `[vid_*]` live block is never touched.** It keeps
  describing the desktop mode; the info block is the app's only description
  of the foreign one. Nothing kernel-side needs undoing at exit, and nothing
  kernel-side can draw meanwhile because nothing kernel-side runs.
- **After the first `FSX_MODE` call, every drawing slot is off-limits**
  (`gfx_*`, `font_*`, `wm_*`, blit, scroll — they'd render desktop geometry
  into a foreign framebuffer). Before any `FSX_MODE` call the screen is
  still the desktop's, so a bracket that never switches modes may keep using
  them — "exclusive but same mode" is a legitimate use (a game loop that
  wants zero jitter) and costs nothing to allow. Author rule, SDK-documented,
  same enforcement class as "workers never call the file API".
- **What stays legal throughout**: file slots, `OSAPI_MOUSE`, ticks, snd,
  mem/xm, `cpu_info`, `fsx_*`, and `osapi_font_glyphs` — the glyph *data*,
  which is how an app letters mode 13h without a renderer of its own.
- Mode-set bodies reuse `vid_setmode`'s Hercules body and `vid_text` as
  callable leaves rather than duplicating the 6845 table. **The Hercules
  rows take their segment from the same table `vid_seg` was initialized
  from**, so a `HERCSEG=` test build flows through to the info block for
  free — without that, `hercshot.py` can never see an fsx frame.
- Hercules graphics reports **2 display pages** (`out 3BFh, 3` already
  allows the B800 page; 3B8h bit 7 flips) — hardware double buffering on a
  1983 card, for one word in the info block.

## 5. Restore — one path, ordered

When the app's proc returns (the only exit):

1. `vid_setmode` — the desktop adapter mode back, idempotent; the Hercules
   body clears its 32KB. BIOS mode set restores the default palette, which
   is the desktop's.
2. Drain the BIOS key buffer and the event queue (`evq`) — keys typed and
   clicks queued at the game must not land in the desktop ladder; the
   `snd.inc` click-abort drain is the precedent.
3. Disarm: `[fsx_task]` = 0xFF under `cli`.
4. Repaint under the still-held lock: `wm_paint_all` with `[menu_bdirty]`
   forced (the save-under-overdraw precedent).
5. Return to the callback; its dispatch epilogue's `gfx_unlock` brings the
   cursor back with a fresh save-under.

Entry saves nothing — a BIOS mode set trashes VRAM regardless, so the
restore is a repaint by design. The §9 build-out makes it instant on
machines with XMS.

## 6. `OSAPI_FSX_WAIT` — the frame clock, and the present

In AL: 0 = next tick (`hlt` loop on `[ticks]`); 1 = vertical retrace —
3DAh bit 3 for the VGA/CGA family, 3BAh bit 7 for Hercules, chosen by the
*current fsx mode*. Bracket-only. Every wait is **bounded by a `[ticks]`
delta** — §37.90's rule: the one way to hang is to wait forever for a bit
that never changes on a machine where every read is 0FFh. Retrace is what
makes palette animation and tear-free page flips possible, and — on a real
CGA in 80-column text — it is the snow-avoidance window.

**It was also the present, and that half has been removed.** The bracket
never calls `gfx_unlock`, and the unlock is where §32's back-buffer flush
lived — so `FSX_WAIT` ran `gfx_flush` before waiting whenever a buffer was
armed and the mode unswitched, and `FSX_MODE` refused while one was, the
buffer describing desktop geometry and nothing else. SPEC.md §32 removed the
buffer; `FSX_WAIT` is pure clock on every path now, and Tracker's Smooth
(§45.11) went with it.

## 7. Lifecycle, refusals, and the forbidden list

- Refusals (CF=1, one predicate each): already in a bracket; the file dialog
  is up (`[fdlg_win]`); the caller is not task 0 with the lock held (the
  verifiable half of the context contract; the rest is an author rule like
  the file slots'). The entry offset is fenced inside the owning package's
  region — the `inst_pkg_spawn` ownership test, reused.
- The app's window is untouched: its rect, its `WF_FULL` state (§11.2 and
  fsx are orthogonal), its menus — all exactly as the app left them.
- An app that hangs in the bracket hangs the machine — the same truth as any
  runaway callback today, documented rather than defended; an 8086 has no
  protection ring and pretending otherwise is inventing a clock.
- **`FSXF_KEEPWORKER`** (flag bit in `FSX_RUN`'s AL, opt-in): the caller's
  own worker stays eligible. **Designed complete now, because its consumer
  is committed**: Tracker adopts fsx immediately after Missile Command, and
  its worker-fed audio ring is exactly this flag. The kept-worker contract
  is binding and has teeth beyond "it parks safely": a worker that touches
  the gfx lock parks on it without corrupting anything (§3) — but for a
  feeder, parking is death by another name, its slices burned in the
  retry loop while the ring drains and the music stops. So: **a kept
  worker feeds data — the ring, a shared word — and never takes the lock
  or a drawing slot.** Tracker's worker already complies. The flag's
  implementation may land with Tracker's adoption; the SPEC contract, the
  whitelist bit and the entry-release exemption that carries the caller's
  stream in are all phase-1 shape, so nothing gets revisited.
- **The §38 file dialog is unreachable inside a bracket, by design**: its
  answer arrives through a completion callback dispatched by the event
  ladder, which is parked under the app on the stack. An app that wants
  Open/Save exits the bracket first — Tracker's Load flow already lives in
  its windowed splash, so its adoption needs no contortion, and `FSX_RUN`
  already refuses while a dialog is up.
- Forbidden inside the bracket, binding: reprogramming PIT channel 0 (the
  tick feeds the floppy motor, `[ticks]` and `snd_tick`); touching channel 2
  or any §34.1-owned sound port directly (the sound API is the route);
  `int 10h` mode sets outside `FSX_MODE` (the kernel must know which mode it
  is restoring *from* — Hercules needs the non-BIOS path).

## 8. API slots, SDK, budget

Four slots, appended per §20.8 (invisible to built packages): `FSX_CAPS`,
`FSX_RUN`, `FSX_MODE`, `FSX_WAIT`. Numbers are **the next free block at
landing time**: 0x0270 on today's `main`, 0x0298 once the §52 hard-disk
block (0x0270–0x0290, in flight on `elendilon`) merges — whichever lands
second takes the higher block; nothing renumbers. The SDK gets `OSAPI_FSX_*`,
`FSXM_*`, `FSXF_*`, and the info-block offsets.

Estimated cost: ~0.6–0.8KB `.text` (bracket + mode table + caps + wait;
the mode-set bodies are mostly reuse), ~6 bytes `.bss`, ~10 bytes on the
switch path. Measured against guard 1 at implementation like everything
else; if it does not fit, that is a decision with whoever asked, not a
build fix.

## 9. What else to build around it (the answer to "what else")

1. **`tests/fsxtest`** — the gate package, phase 1. A mode menu built from
   `FSX_CAPS` (greyed per §47); each mode draws an identifying pattern
   (mode id, border, colour bars), waits for a key, cycles; Esc exits. The
   strong acceptance is **restore equality**: screendump before entry,
   cycle every mode, screendump after — byte-identical, scripted over QMP.
2. **Sound continuity test, both directions** — `make test ADLIB=1`. One:
   the fsxtest app holds an FM note of its own across three mode switches;
   the wav shows one unbroken tone (the freeze-whitelist's proof). Two: a
   *second* instance keys a duration-0 note, fsxtest enters the bracket,
   and the wav shows that note **stop at entry** (§2.1's proof — without
   the release it sustains for the whole capture). Three: after the
   bracket, the second instance's stale "off" call must not kill a note
   fsxtest is then playing (the generation check, observable as the tone
   surviving in the wav).
3. **The reference consumer — DECIDED: Missile Command.** Mode X's
   320x240 holds the arcade's native 256x231 playfield 1:1 with square
   pixels — no scaling, the 6502 sources' own coordinates — and §48.6's
   palette cycling becomes real DAC writes instead of a reduced-palette
   approximation. The fsx path is a menu item gated on `FSX_CAPS` per §47
   (greyed on CGA/Hercules with the mode named); windowed play stays
   exactly as shipped. Its trail-erase, wave and refusal lessons (§48)
   carry over untouched — the game logic does not know the mode changed.
   **Tracker follows immediately** as the second consumer and the
   `FSXF_KEEPWORKER` + present-semantics proof (§6, §7).
4. ~~**XMS desktop stash**~~ (§41) — **BUILT, then REMOVED.** On 286+ VGA,
   `xm_copy` the four planes out at entry and back at exit — instant restore,
   no repaint. It shipped and came out again: see phase 4's entry below, and
   SPEC.md §53.6.1, which is now the record of why. The short version is that
   a bracket takes real TIME, so the desktop behind it is live state and not
   an image, and a snapshot restores a screen that has moved on.
5. ~~**Task Manager service badge**~~ — **DROPPED, not deferred.** The
   idea was to surface `TF_SERVICE` as "kernel plumbing vs app work" in the
   task list. But `TF_SERVICE` has exactly one reader (`sch_switch`'s
   whitelist) and exactly one effect: keeping a driver service task runnable
   *while a bracket is up*. A bracket means the app owns the whole screen —
   so the Task Manager can never be visible when the flag is doing anything,
   and whenever the Task Manager IS visible (`[fsx_task]` = 0xFF) the flag is
   never consulted at all. The badge would draw a distinction with no
   observable consequence at the only moment it could be drawn. It is not a
   cost/model question (though the Task Manager also lists instances, not
   tasks, and a service worker has no instance): it is that there is nothing
   worth showing.
6. **SDK recipe: text in any mode** — `osapi_font_glyphs` + the info block
   is a complete "letter mode 13h yourself" story; write it down once.
7. **Port ownership doc** — inside a foreign mode the 6845/sequencer/GC/
   attribute/DAC ports are the app's; a table in SPEC §53 saying exactly
   which, so the §34.1 precedent has a video twin.

Deliberately not built: kernel-side blit/palette helpers for foreign modes
(the whole point is that the screen is the app's; the kernel stays out), and
an int 09h escape hatch (the kernel doesn't hook the keyboard today, and a
half-working panic key is worse than a documented absence).

## 10. Testing beyond the gate

- QEMU exercises ids 0–3 and 5–8 on VGA (SeaVGABIOS sets ids 0–7; id 8 is
  13h plus register pokes QEMU's VGA honours, and `screendump` follows the
  CRTC so it shows every one, Mode X included), ids 0–3 gating under `VIDEO=cga`,
  and id 4's *rendering* under `VIDEO=herc` + `hercshot.py` (the 6845 pokes
  go into the void there — harmless; the port writes themselves are 86Box's
  to verify).
- 86Box: `make xt-cga` (real CGA text↔graphics), `make xt-hercules` (the
  real 6845 flip — THE thing QEMU cannot test), `286`/`386` for VGA.
  §17 gets a per-adapter acceptance row.
- docs/TESTING.md gains the fsx rows, including the `--screen` implications
  for `mouse.py` (none: mouse coordinates stay in desktop space — the app
  scales, which is one shift for the half-width modes; SDK-documented).

## 11. Phasing

1. **Bracket + freeze**: `T_FLAGS`, the whitelist test, `FSX_RUN` with no
   mode switching, the §2.1 entry release (`snd_release_inst` walk — it is
   part of the freeze, not a sound feature, and ships with it), fsxtest
   skeleton proving exclusive-same-mode, restore path minus mode set.
   **SPEC §53 first, and written COMPLETE** — the `FSXF_KEEPWORKER`
   contract and `FSX_WAIT`'s present clause included, so the committed
   consumers (Missile Command, then Tracker) are designed for from the
   first line even where implementation lands later.
2. **Modes**: the table, `FSX_MODE` + info block, `FSX_CAPS`, `vid_setmode`
   /`vid_text` factored into leaves, full fsxtest, restore-equality script.
3. **Frame clock + the reference consumer**: `FSX_WAIT` (clock + present),
   Missile Command's Mode X path, sound-continuity tests, 86Box sweep.
4. **Tracker + polish**: Tracker's fsx adoption with `FSXF_KEEPWORKER`
   (DONE — §45's fullscreen is the bracket now, verified with a real
   Sound Blaster wav produced entirely inside it, and the one bug it found
   — `[trk_fs]` must clear before `fsx_restore`'s repaint — fixed), docs
   (DONE). The two brainstormed polish items did not ship, for different
   reasons:
   - **Task Manager service badge — DROPPED** (was item 5 of section 9).
     `TF_SERVICE` only ever
     does anything inside a bracket, and a bracket owns the whole screen, so
     the Task Manager can never be on screen when the flag matters; when the
     Task Manager IS on screen the flag is never even read. There is nothing
     to show at the moment you could show it. (The instance-vs-task model and
     the `SYS_SNAPSHOT` ABI cost are real too, but secondary to that.)
   - **XMS desktop stash — SHIPPED, then REMOVED** (SPEC.md §53.6.1). The
     four desktop planes (150KB) went to the §41 store at bracket entry and
     back at exit — an instant restore instead of `wm_paint_all` — on a
     286+/VGA machine with a store, with the 8086 target, a mono adapter, an
     armed back buffer or a refused claim all falling through to the repaint.
     Built in **`.cold`** at the user's direction so it cost guard 1 (the
     budget), not guard 2 (measured: 63 bytes of `.text` glue, ~250 in cold).
     It verified cleanly under QEMU — `fsx_stashed`=1 with the block at linear
     0x110000, byte-identical restore for a same-mode bracket AND a Mode X
     bracket, and `-m 1M` falling through — and **byte-identical restore was
     never the question it needed to answer.** A bracket takes real TIME. What
     is behind it is not pixels but live state: the menu bar's clock, the
     Timer, a Bounce, any background task with a window — and the exclusive
     app's own window, whose content is usually the thing the full-screen
     session was spent changing. The stash restored a photograph of a desktop
     that had moved on. It had even conceded the point in miniature and nobody
     read it as general: the stash path redrew the menu bar afterwards
     *because the clock was stale*. Removed; step 4 is `wm_paint_all` on every
     machine, which is what tier 0 always did. What survives it is the side
     quest, which was right on its own merits: `xm_copy` under the gfx lock is
     legal (§41.8), the "never under the gfx lock" restriction having been
     unenforced conservatism whose stated 286-CPU-reset reason does not
     survive scrutiny. The §41 slots are untouched — a published package ABI
     with its own consumers, of which this was one.

## 12. Decisions — all four made, recorded here with their reasoning

1. **Which four VGA modes? — DECIDED**: 320x200 and 640x480, each in 16
   and 256 colours, with Mode X (320x240x256) standing in for the
   hardware-impossible 640x480x256 (§4). The one open sliver: confirm the
   Mode X substitution is wanted, or ship three VGA modes and leave id 8
   unassigned.
2. **The reference consumer — DECIDED: Missile Command** (section 9, item 3 of this plan), with
   Tracker committed immediately after.
3. **`FSXF_KEEPWORKER` — DECIDED: designed complete in phase 1, ships with
   Tracker's adoption in phase 4** (§7). The freeze, the whitelist and the
   entry-release exemption are built keepworker-shaped from the start;
   only the flag's few bytes wait.
4. **Text-mode contract depth — DECIDED: the bare contract.** A text mode
   brings three pieces of hardware state a graphics mode does not have:
   the CRTC's own blinking cursor (parked at 0,0 by the mode set — writing
   B800 never moves it), the 4–8 display pages, and the attribute
   blink/bright-background toggle. All three belong to the **app**,
   through the BIOS it is already allowed to call (the bracket IS the UI
   task) or the CRTC ports directly: for text, the ROM BIOS is a complete
   portable driver (AH=02h cursor move, AH=01h shape/hide, AH=05h page
   flip, AH=0Eh teletype, AX=1003h blink-off), and nothing the app pokes
   outlives the exit mode set. The performance framing, honestly: the hot
   path — characters into B800 — was direct VRAM under any contract; bare
   wins by putting nothing between the app and the hardware for the rest
   (a kernel slot would wrap the same operations behind a far call, and
   would have to *forbid* direct CRTC pokes to stay coherent), and by
   spending zero kernel bytes. The SDK recipe lists the incantations,
   including the per-adapter blink difference (int 10h on EGA/VGA, port
   3D8h bit 5 on a real CGA). A helper slot remains one §20.8 append away
   if a real text app ever proves the recipe annoying.

**All four decisions are now made.** The plan is complete and phase 1
(SPEC §53, written in full, then the bracket and the freeze) can start.
