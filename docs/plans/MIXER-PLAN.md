# Mixer — delivery plan

A speaker in the menu bar opens an immediate master-volume control when a
hardware mixer is present. An optional native Mixer adds individual source
levels, stereo balance and the card's tone controls. SPEC.md §34.15 is the
contract. [Build diary](../HOW-MIXER-WAS-BUILT.md).

## Stories and waves

- [x] **Foundation:** isolate the worktree, map existing APIs, pin contracts,
  stories and verification, and open one draft PR.
- [x] **Hardware:** as a sound-card owner, I get controls only for the mixer
  actually detected. Queries reflect hardware, unsupported writes refuse,
  stereo settings stay independent, and detach restores borrowed state.
- [x] **Quick volume:** as a desktop user, I see a speaker beside the clock
  and can click, drag or use keys to change volume without opening a window.
  The recessed slider, engraved ticks and ridged thumb look intentional on
  VGA, CGA and Hercules. Dismissal restores the exact covered pixels.
- [x] **Full Mixer:** as an installed-system user, I can open Mixer from the
  popup when SYSTEM contains MIXER.O88 and adjust source levels and balance.
  Removing that file removes the command on the next opening; the 360KB
  floppy keeps the quick control without carrying the optional package.
- [x] **Acceptance:** prove hardware refusals and quantization, popup input,
  package discovery/presence/absence, adapter faces, background playback,
  cleanup and disk/memory budgets; run the affected soak union once and the
  full gate after the last implementation batch.

## Boundaries and verification

Use SOUND.DRV's package verbs, OSAPI_GFX_SAVE/REST, live video geometry,
OSAPI_FILE_FIND/GOTO_Q and OSAPI_PKG_START. The popup belongs to the already
loaded driver; only capability/hit-test/cache glue belongs to the kernel.
No mixer polling in IRQ0 and no new kernel API slots. Shared slider source
belongs to apps/os88mixui.inc; no copied renderer between driver and package.

Hardware matrices: CT1335 (where modeled), SB Pro/CT1345, SB16/CT1745,
SB1/2 without mixer, AdLib only, absent driver, and kern_small. MartyPC is
the XT/adapter instrument; QEMU supplies SB16 behavior where MartyPC's
Sound Blaster model has no mixer. Physical-card audio/analog balance remain
a field-verification limit, not a claim from screenshots.

Final affected union: `mixerhost`, `mixercga`, `mixerherc`, `mixervga`,
`mixersb16`, `hmadrv`, `hmahiber`, `sndmove`, `fmrefuse`, `stikmenu` and
`toastbar`, plus `fpgcold`/`fpgnotch` for the boot-progress interaction,
followed by `test-full`. Host builds and a visual boot were the
per-wave checks. The final union ran after the implementation batch; failed
UI rows were repaired and rerun individually. The SB16 row also checks a
fully staged background stream while the popup stays open.

Result: all 13 affected soak rows and all 63 full-tier rows passed. The full
pass's only initial failure was a missing 128KB VGA machine definition in
the reused emulator assets; that row passed after installing the repository's
definition into a private configuration. The three kernel budgets pass.
The diary records screenshots, resource costs and the physical listening limit.

## Open follow-ups (found at the main -> elendilon merge, 2026-10-22)

The stories above shipped. These four were found reading the merged code
for the owner's question *"does the Mixer cost an 8088 anything, and can it
upset a Video Player play that is right on the line?"* - nothing below was
measured on a machine, and each line says what would measure it. The owner
has deferred the first two to a later round; the second two are recorded
so they are not rediscovered.

- [ ] **The bar text is lettered twice after every bar repair, on every
  `kern_big` machine.** `menu_furniture` (`kernel/menu.inc`) stores
  `[menu_mixcap] = 0FFFFh` as a "redraw the icon" sentinel - and so does
  `fpg_finish` (`kernel/fprog.inc`) - so the next `menu_sound` always takes
  `.changed`, which calls `menu_relayout` (every cell forgotten) and
  `menu_bar_text` again, after `menu_draw_bar` has already lettered the bar
  once. The second pass is opaque `font_run` over the same glyphs, so the
  glass shows nothing - but it is ~60 cells at ~0.9 ms, **~50 ms ESTIMATED**
  a repair: leaving fullscreen (a video returning to its window pays it
  once), leaving the screen saver, a dropped menu whose save-under was
  refused. `OS88_SNDCARD` is every `kern_big` build's, so it is paid with no
  `SOUND.DRV` and no mixer at all. The fix's shape: invalidate only the icon
  (compare against the old capability, or call `SNDV_MIXICON` without the
  relayout) and leave the cells alone when the clock origin did not move.
  Measure it with a counter in `font_run` across one fullscreen exit, with
  and without the fix (CLAUDE.md, Performance rule 4).
- [ ] **`SOUND.DRV`'s claim grew 8 KB -> 12 KB** (`DRVM_IMG_SND` in
  `kernel/driver.inc`; the image 7,283 -> 11,602 bytes with `-DPICOMEM`,
  assembled at `cd38ec7d` and its parent). That is 4 KB of heap off every
  machine that mounts the driver, mixer chip or not - and the Video Player
  sizes its ring from the free heap at Play, so a borderline video loses
  ~1% of its cushion on a 640 KB machine. **1,056 bytes of it is
  `volume_band`** (`drivers/sound/volume.inc`, `times 176*48/8 db 0`), the
  popup's slider band, which is held for the driver's whole life to be used
  only while the popup is up: claiming it with the popup's save-under at
  open, or moving the popup into an on-demand part of the driver, is where
  to start. `tools/os88drv.py`'s image line is the measurement.
- [ ] **The volume popup holds the gfx lock for as long as it is open**
  (`OSAPI_GFX_LOCK` at `volume_popup`'s entry, `OSAPI_GFX_UNLOCK` at
  dismissal), and its idle loop yields rather than sleeps. A windowed Video
  Player takes the lock every tick to decode and to top up a Live play's
  Sound Blaster ring (`apps/video/video.asm`, `vp_lstep`/`vp_lsnd`), and its
  chunk copy runs on the UI task, which is inside the popup's modal loop - so
  **while the popup is up the picture freezes and Live's sound runs dry**,
  however light the video is. The player recovers on dismissal (the card is
  the clock). SPEC.md 34.15's "background sound continues" holds for streams
  `SOUND.DRV` feeds itself - which is what `tests/mixer.py`'s background
  stream checks - and not for the player's. Fullscreen play cannot reach the
  popup. No row covers it: a MartyPC Live play on an SB machine
  (`tests/vidsound.py`'s setup) with the popup held open ~2 s, reading the
  player's frame counter and, on a `VPDIAG=1` build, the info card's `Dry`
  line, would.
- [ ] **Every popup open searches the boot volume for `SYSTEM\MIXER.O88`**
  (`volume_discover`), through `OSAPI_FILE_FIND`, which re-walks the
  directory per entry (docs/plans/DISK-CPU-PLAN.md). Warm, that is CPU;
  cold, it is `int 13h` at ~400 ms a call before the popup appears. A
  cached answer, re-asked when `[dsk_lstale]` says the volume changed, is the
  obvious shape.
