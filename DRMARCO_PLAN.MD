# DrMarco feature plan

Updated: 2026-09-28. Product name: **DrMarco**.

This is the roadmap for bringing the native os8088 puzzle game up to the
feature coverage of the supplied NES Dr. Mario reference, using original
DrMarco screen art and characters. Work is incremental; an unchecked item is
planned, not implemented. Branding, the original surround and gameplay character
animation are implemented incrementally; the complete roadmap remains open.

## Baseline and reference

- Implementation: `apps/drmario/{drmario.asm,game.inc,video.inc}`.
- Asset compiler: `tools/drmario_assets.py`; guest gate: `tests/drmario.py`.
- Contract: `SPEC.md` section 99; build: `make drmarco drmarcodisk`.
- Reference: `reference/drmario/bank_FF.asm` and `CHR_ROM.chr`, committed
  and pinned (2026-09-30). `bank_ram.inc` and the annotated disassemblies in
  `_misc/` stay in NES-Games-Disassembly `df2c8e5`; the build reads neither.
- Already implemented: 8x16 bottle, three colors, matches of four or more,
  connected halves, detachment, gravity, cascades, rotation and left kick,
  next capsule, level/speed selection, periodic speed increases, scoring,
  pause, blocked-spawn game over, and progression through level 20.
- The NES capsule and bottle-virus tiles and gameplay tables are still taken
  from the NES reference, now COMMITTED in `reference/drmario/` by the
  owner's decision (2026-09-30), so the application is no longer `local`: a
  plain `make` builds it and it ships in `GAMES/` (SPEC.md section 100).

## Delivery order

### 1. DrMarco identity and original static artwork — complete

- [x] Display DrMarco in the launcher, fullscreen title and About dialog;
  ship `DRMARCO.O88`, with `drmarco`/`drmarcodisk` build targets.
- [x] Retain old build commands as aliases and document stable internal paths.
- [x] Generate and save an original doctor and laboratory bottle surround,
  including the generation prompt and source PNG.
- [x] Convert the art offline for VGA and CGA and integrate it into gameplay.
  Keep the bottle readable, reserve score/preview/state areas and inspect both
  native resolutions. Do not add art work to ordinary movement frames.
- [x] Verify all four disk geometries, gameplay, art survival across HUD
  updates and fullscreen restoration, and XT rendering costs.
- [x] Add an original checker surround and clipboard-style information panels,
  keeping text opaque and the backdrop static across gameplay updates.

### 2. Single-player progression and score tracking

- [ ] Continue through levels 21–24, then repeat level 24. Keep starting-level
  selection at 0–20 and virus count capped at 84. Check the reference's height
  lookup and level bookkeeping before extending the imported 21-entry table.
  Reference: NES `$B2BF`–`$B2C6` level increment and `$8206` setup.
- [ ] Add separate TOP score storage, display and update on game over;
  preserve it through in-app resets. The NES baseline retains it across soft
  reset, not power loss. Disk persistence is a separate optional enhancement.
- [ ] Audit new-game/retry score resets, simultaneous clears, cascade scoring,
  multiplier saturation and score overflow against the reference. Record
  deliberate fixes to original bugs separately from parity requirements.
- [ ] Add fixture-based checks for level 20→21, 23→24, repeat 24, all virus
  counts, and high-score/retry behavior.

### 3. Gameplay presentation and animation

- [ ] Add timed virus placement and the opening ready delay.
- [ ] Animate DrMarco throwing the preview capsule into the bottle; preserve
  preview sequencing and reject control of a capsule before its entry state.
- [x] DrMarco occasionally blinks, independent of capsule timing. Keep the
  portrait still and update only the eyes to avoid head-redraw flicker.
- [x] Animate the three large virus characters, including reactions to clears
  and disappearance when their color is exhausted. Track per-color counts.
- [x] Animate bottle viruses with staggered per-color wiggles.
- [ ] Add further clear effects and illustrated stage-clear/game-over panels.
- [ ] Match pause presentation by hiding the playfield and restoring it on
  resume. Keep desktop exit/resume behavior intact.
- [ ] Audit lock, clear, gravity, spawn and held-key delays against NES frame
  counts. Convert deliberately to the 54.6 Hz clock; document rounded cases.
- [ ] Verify animation boundaries, pause during transitions, dirty-region
  repaint equivalence and capsule/control timing on a 4.77 MHz 8088.

### 4. Title, options and attract mode

- [x] Replace the plain desktop launcher with a DrMarco capsule-logo splash,
  game-style sprites, title music, Enter to play and H for illustrated help.
  Use color on VGA and line drawings on CGA/Hercules, with a window-shade reveal.
- [x] Preserve the desktop launcher as the os8088 entry point.
- [ ] Add animated title characters and fullscreen options with player count,
  levels, speeds and music selection.
- [ ] Add an idle-triggered scripted demonstration using deterministic board,
  capsule sequence and input playback; any start action must exit cleanly
  without overwriting the user's chosen settings or TOP score.
- [ ] Verify demo completion, interruption and settings restoration.

### 5. Audio

- [x] Implement FEVER / CHILL / OFF selection and gameplay arrangements through
  the OS sound service. Music is compiled from the committed NES reference into
  build-only assets; OPL patches and speaker tones are native arrangements.
- [x] Add title/options, stage-clear and game-over music for existing scenes.
- [ ] Add versus and ending music alongside those future game modes/scenes.
- [x] Add cursor, move, rotate, land, falling fragments, clear, speed-up and pause
  cues. Effects override speaker music and have a dedicated FM voice; OS channel
  claims and priorities govern ownership. Sound Blaster uses its OPL FM tier.
- [ ] Add attack cues alongside competitive play.
- [ ] Synchronize virus dance frames with music where practical; provide a
  deterministic beat when music is off.
- [x] Verify speaker, AdLib and Sound Blaster FM, clean stop on fullscreen exit,
  pause/resume, channel arbitration, and bounded CPU cost without blocking
  gameplay in the 4.77 MHz MartyPC model. Physical XT listening remains a
  hardware acceptance check.

### 6. Milestone scenes and endings

- [ ] Add speed-dependent intermissions after the appropriate milestones
  (reference table `tbl_9FF8`; scenes follow cleared levels 5, 10 and 15 where
  enabled by speed) and the level-20 endings for each speed.
- [ ] Create original DrMarco characters, scenery and flying-object art for
  these sequences; include a DrMarco interpretation of the HI-speed UFO finale.
- [ ] Support skipping scenes, then continue to the correct next level with
  score and settings intact. Scenes must leave no lingering audio or palette.
- [ ] Test every speed/milestone combination and skip at each scene state.

### 7. Two-player competitive mode

- [ ] Add two independently controlled bottles and per-player level/speed
  options. Design a readable 320-pixel layout for both adapters first.
- [ ] Refactor one-player state into explicit per-player records while sharing
  gameplay routines. Retain identical opening boards when levels match and
  the reference's shared capsule reserve.
- [ ] Implement combo attack accumulation, colors, 2–4 half-capsule delivery,
  delivery timing, gravity and top-out interactions. Reference:
  `sub_9BE9_calculate_punishment_for_opponent` and `sub_9C01`.
- [ ] Add round wins, loss/draw outcomes, first-to-three match victory, match
  reset and appropriate character/sound feedback.
- [ ] Choose simultaneous keyboard controls and verify key rollover limits;
  document any physical keyboard limitation and an alternative mapping.
- [ ] Test attack fixtures, simultaneous clears/losses, draw adjudication,
  round/match resets, two-player input and worst-case XT frame costs.

### 8. Fidelity audit and final acceptance

- [ ] Compare seeded virus placement with NES RNG/placement behavior, including
  occupied-cell retries and color fallback. Decide whether to retain the
  native retry adaptation or implement exact placement; record the choice.
- [ ] Compare deterministic capsule/movement/rotation/match/score traces with
  the reference. Keep timing approximations and intentional bug fixes explicit.
- [x] Add original capsule-logo title artwork and illustrated desktop help.
- [ ] Complete fullscreen options/intermission artwork and lettering.
  Optionally replace capsule and small virus tiles to remove the CHR dependency;
  this requires replacing the current CHR-based pixel oracle too.
- [ ] Refresh README, SPEC, screenshots, package size, BSS and cycle measurements.

## Engineering and validation requirements

Keep 8086 instructions, VGA Mode X and RGB CGA support. Prefer the existing
OS sound, input and fullscreen services. Use offline graphics conversion,
bounded state machines and dirty rectangles; avoid per-frame full-screen
decoding, a new framebuffer, blocking effects or new kernel services.
Account for image, BSS and stack growth before adding animation or a second
board; retain the package's single-segment constraints.

Each phase must pass relevant deterministic gameplay fixtures, visual checks
and fullscreen exit/reentry checks before its checkbox is marked complete.
Run the existing MartyPC VGA/CGA gate and record movement, idle and new
worst-case costs. Use QEMU only for the documented full Mode X display check;
it supplies no XT timing evidence. Build and verify 360 KB, 720 KB, 1.2 MB and
1.44 MB application disks. Keep generated art provenance and build outputs
separate from user-supplied reference assets.

## Session log

- 2026-09-28: Roadmap created from the source comparison. DrMarco static art
  and branding completed; remaining phases are open. Source PNG and built-in
  imagegen prompt are in `apps/drmario/art/`. All four disk geometries verify.
  MartyPC VGA/CGA gameplay, background decode, portrait/preview/footer pixels,
  input, incremental repaint and desktop restoration checks pass. QEMU confirms
  the complete Mode X display and return to desktop. The new checks caught and
  fixed CGA HUD blanks overwriting the preview and footer.
- Measurement: 36,189 image bytes + 10,194 BSS; compressed package 14,312 bytes.
  VGA/CGA movement rendering: 3.41/1.37 ms; idle: 0.022 ms each. Background
  decode on entry: 331.96/97.33 ms. These are 4.77 MHz MartyPC cycle measurements,
  not physical XT measurements. Proof images and results: `build/drmario-proof/`.
- 2026-09-28: Gameplay character animation completed. DrMarco has a reusable
  bobblehead sprite with idle, clear/victory and loss poses. Three original
  geometric mascots dance, become dizzy on clears, and disappear when their
  color is exhausted. Bottle viruses alternate mirrored poses. The doctor
  advances every eight ticks, each color every 24; alternating bottle rows
  follow one tick apart to bound dense-board copies. No gameplay RNG, capsule
  sequence or control state is consumed by the decorative clock. Throws,
  timed placement and illustrated end panels remain open.
- Validation: MartyPC VGA/CGA gameplay and animation gates pass, including
  all pose transitions, counts, reactions, disappearance, pause between row
  updates, incremental/full repaint equality and desktop reentry. QEMU confirms
  the complete Mode X display and desktop restoration. All four disk geometries
  verify. Animation previews: `build/drmario-proof/{vga,cga}-dance.gif`.
- Initial animation measurements (4.77 MHz MartyPC): movement 3.45/1.41 ms VGA/CGA;
  unchanged renderer 0.038 ms each; doctor update 18.58/9.50 ms; peak sampled
  animation rendering with 84 viruses 27.81/14.71 ms. Image 45,678 bytes +
  10,226 BSS = 55,904 bytes; compressed package 18,118 bytes. Animation adds
  32 BSS bytes and no framebuffer. Full background decode remains entry-only.
- 2026-09-28: Replaced the bobblehead and terminal head motions with a brief
  blink about every 4.8 seconds, following feedback about flicker. The portrait
  is static; tiny opaque eye patches close and restore the eyes without clearing
  the head. Virus animation and pause behavior are preserved.
- Blink validation: VGA/CGA guest suites pass, including blink cadence,
  eye-only pixel changes, exact reopening restoration, pause and mode reentry.
  Blink redraw costs 0.89/0.42 ms VGA/CGA. Image 46,868 bytes + 10,223 BSS =
  57,091 bytes; compressed package 18,409 bytes. All four disk geometries verify.

- 2026-09-28: Audio implemented for current single-player scenes. M selects
  FEVER/CHILL/OFF in either view; the fullscreen HUD shows the selection.
  NES note data is compiled at build time with source hashes, repeats, shared
  tempo/transposition and phrase deduplication. Three OPL music voices plus an
  effect voice serve AdLib and Sound Blaster; the speaker uses the lead voice
  with effect priority. Title/options, clear and loss music and eight gameplay
  cues are connected. Versus/attack/ending scenes remain future work.
- Audio validation: independent execution of the original 6502 sequencer agrees
  with every imported pitch/duration through three loops. XT speaker, AdLib-only
  emulation and SB profiles pass note, effects, priority, OFF, pause,
  exit/reentry, refused claims and channel release tests. Captures contain
  audible speaker/OPL output; SB PCM stays idle. VGA/CGA gameplay, pixel and
  animation gates pass; QEMU confirms complete Mode X display and restoration.
  All four application disk geometries verify.
- Audio costs: speaker averages 0.20/0.18 ms for FEVER/CHILL; FM averages
  0.88/0.60 ms. Peak sampled SB audio is 6.06 ms after limiting steady updates
  to two voices per tick with rotating pending notes. A movement frame including
  input, logic, rendering and peak audio totals 11.64 ms versus an 18.32 ms tick.
  Movement rendering remains 3.45/1.40 ms VGA/CGA. The existing decorative clock
  stays independent to preserve its timing and cost; music synchronization is
  still open. These are emulator results, not physical XT measurements.
- FM music uses hardware attack rate 14 to soften note onsets; effects retain
  rate 15. No extra per-frame work or CPU-specific timing changes.
- Memory: 54,598 image + 6,617 BSS = 61,215 bytes; compressed 23,254 bytes.
  Sharing the VGA/CGA native font cache and storing 63 used glyphs recovers
  3,648 bytes of BSS, rebuilt on mode entry only. No mixer, worker, new IRQ,
  heap allocation or kernel API was added. Proofs: `build/drmario-proof/`.
- 2026-09-28: Added a 24-pixel checker surround, clipped HUD/preview boards
  and matching title, state-prompt and controls plaques, following the layout
  separation in NES `tbl_C198_playfield_1p_mode`. VGA uses navy checks; CGA
  uses alternate red/black scanlines inside lit checks. All lettering retains
  black writing surfaces. The source portrait/bottle PNG is unchanged.
  Unused HUD row 15 no longer erases the clipboard's lower edge.
- Background row packets now support literal bytes and repeated scanlines;
  identical animation streams share storage. Image 54,525 + 6,617 BSS =
  61,142 bytes (73 fewer than before); compressed package 25,536 bytes.
  The backdrop still decodes only on entry/reentry, with no framebuffer or
  extra movement-frame drawing. MartyPC VGA/CGA movement: 3.45/1.40 ms;
  idle: 0.035 ms; background entry decode: 364.55/92.11 ms. VGA/CGA gameplay,
  animation and restoration gates pass; QEMU confirms the full Mode X screen
  and desktop restoration. Native previews: `build/drmario-proof/*-native.png`.
- Focused VGA/CGA pixel checks verify every left HUD glyph and all static
  surround pixels outside the dynamic drawing regions after input, pause,
  clear/restart and reentry. All four application disk geometries verify.
- 2026-09-28: Replaced the text launcher with a capsule-logo title screen
  following the supplied NES screenshot's composition: green checks, a large
  blue/red DrMarco capsule, and a black menu plaque with character sprites.
  H opens illustrated help using the existing gameplay portrait and native
  capsule/virus tiles. Enter starts/resumes; H/Escape returns from help.
  VGA uses color; CGA and Hercules use line drawings. Hercules remains a
  frontend-only adapter. Music and an eight-row window-shade reveal share the
  existing desktop timer; no worker, framebuffer or new kernel API was added.
- Supporting DRMARCO.VGA/CGA/HRC files ship on all four application disks.
  Only one compressed resource is held in an instance-owned movable claim;
  bounded row decoding borrows the idle game queue. Fully drawable VGA bands
  use native planar copies, with a clipped packed-pixel fallback. Window drags
  preserve correct mouse targets, and returning from play preserves the board
  and restores the completed help/splash page.
- A compact eight-ink VGA background codec recovers embedded image space.
  The application is 49,433 image + 6,640 BSS = 56,073 bytes; compressed package
  25,338 bytes. Supporting VGA/CGA/Hercules files are 34,554/5,921/11,754 bytes.
  The generated source and exact built-in imagegen prompt are committed as
  apps/drmario/art/drmarco-splash.png and SPLASH_PROMPT.md. Fullscreen options,
  animated title characters, attract demo and intermissions remain open.
- Validation: VGA/CGA/Hercules frontend gates pass pixel comparisons, music,
  help/settings navigation, moved-window clicks and game return. Maximum
  sampled unobscured splash/help reveal steps are 52.23/45.90 ms VGA,
  24.92/27.35 ms CGA and 24.93/28.00 ms Hercules at 4.77 MHz. The VGA packed
  fallback also matches the uncompressed image. Gameplay/animation and all
  three audio backend suites pass; QEMU confirms Mode X display/restoration.
  Fresh gameplay measurements include interrupt costs: movement 3.45–4.08 ms
  VGA / 1.40–2.04 ms CGA; idle 0.035 ms; entry-only backdrop 505.84/93.54 ms.
  All four disks verify. Guest screenshots: build/drmario-proof/*-splash.png
  and *-help.png; these are emulator checks, not physical XT measurements.
- 2026-09-28: Optimized splash interaction for the 4.77 MHz XT. Setup keys
  update only the settings text row; unused keys and help-page setup changes
  do not repaint artwork. Controls and help text appear before the image,
  with the initial file read deferred until after the first paint. Enter may
  start play before that read; returning schedules the outstanding load.
- Identical resource rows now share offsets (also across splash/help), reducing
  VGA art from 34,554 to 26,947 bytes, CGA to 5,139 and Hercules to 10,112.
  Bounded REP decoding and batched OS blits borrow the existing idle font cache;
  fullscreen entry rebuilds it. No new pixel buffer is allocated. Twelve-row
  reveal steps replace eight-row steps: 22 callbacks instead of 33 on VGA and
  Hercules, 11 instead of 17 on CGA. Visible text is restored only where a
  reveal band crosses it. The complete image remains pixel-identical.
- Settings handlers measured about 7–9 ms; first controls paint 57–85 ms before
  file I/O. Peak splash/help steps: VGA 47.33/38.02 ms, CGA 23.06/28.40 ms,
  Hercules 19.32/22.97 ms. These are emulator CPU measurements, excluding
  application startup and physical floppy latency. Image 49,685 + 6,648 BSS =
  56,333 bytes; compressed package 25,550. Fullscreen gameplay/animation gates
  pass on VGA/CGA; frontend gates check actual key delivery, no artwork decode
  on setup keys, unchanged surrounding pixels, deferred loading and mode return.
