# DrMarco feature plan

Updated: 2026-09-28. Product name: **DrMarco**.

This is the roadmap for bringing the native os8088 puzzle game up to the
feature coverage of the supplied NES Dr. Mario reference, using original
DrMarco screen art and characters. Work is incremental; an unchecked item is
planned, not implemented. The present task covers branding and a first static
artwork pass, not the complete roadmap.

## Baseline and reference

- Implementation: `apps/drmario/{drmario.asm,game.inc,video.inc}`.
- Asset compiler: `tools/drmario_assets.py`; guest gate: `tests/drmario.py`.
- Contract: `SPEC.md` section 99; build: `make drmarco drmarcodisk`.
- Reference: `../NES-Games-Disassembly/Dr. Mario/bank_FF.asm`,
  `bank_ram.inc`, `CHR_ROM.chr`, and annotated disassemblies in `_misc/`.
- Already implemented: 8x16 bottle, three colors, matches of four or more,
  connected halves, detachment, gravity, cascades, rotation and left kick,
  next capsule, level/speed selection, periodic speed increases, scoring,
  pause, blocked-spawn game over, and progression through level 20.
- Original screen art does not remove the current dependency on NES capsule
  and bottle-virus tiles or gameplay tables. Keep the application `local`
  until those assets have their own replacement and distribution review.

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
- [ ] Animate the three large virus characters, including reactions to clears
  and disappearance when their color is exhausted. Track per-color counts.
- [ ] Add animated bottle viruses, clear effects, victory and loss reactions,
  and illustrated stage-clear/game-over panels.
- [ ] Match pause presentation by hiding the playfield and restoring it on
  resume. Keep desktop exit/resume behavior intact.
- [ ] Audit lock, clear, gravity, spawn and held-key delays against NES frame
  counts. Convert deliberately to the 54.6 Hz clock; document rounded cases.
- [ ] Verify animation boundaries, pause during transitions, dirty-region
  repaint equivalence and capsule/control timing on a 4.77 MHz 8088.

### 4. Title, options and attract mode

- [ ] Build a DrMarco title screen, original logo/lettering, animated characters,
  and fullscreen options with player count, levels, speeds and music selection.
- [ ] Preserve the desktop launcher as the os8088 entry point.
- [ ] Add an idle-triggered scripted demonstration using deterministic board,
  capsule sequence and input playback; any start action must exit cleanly
  without overwriting the user's chosen settings or TOP score.
- [ ] Verify demo completion, interruption and settings restoration.

### 5. Audio

- [ ] Implement FEVER / CHILL / OFF selection and gameplay arrangements through
  the OS sound service. Decide and document whether music uses local reference
  data or original DrMarco compositions before bundling any music assets.
- [ ] Add title/options, stage-clear, game-over, versus and ending music.
- [ ] Add cursor, move, rotate, land, falling fragments, clear, speed-up, pause
  and attack cues. Define music/effect priority and audio ownership.
- [ ] Synchronize virus dance frames with music where practical; provide a
  deterministic beat when music is off.
- [ ] Verify sound on the speaker, clean stop on fullscreen exit, pause/resume,
  channel arbitration, and bounded CPU cost without blocking gameplay.

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
- [ ] Complete original title/options/intermission artwork and lettering.
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
