# VBATTLE-BUILDER — the clip-graph builder, with its window from the start

**Status: all five waves BUILT, and the title and ending loops (6) (2026-11-04): the model, the analysis, the
command line, the window's Battle, Idle, Clips, Openings, Actions,
Characters & menus, Enemy and Build tabs, and the engine playing section
11's rules (10.5). The owner's later rounds (2026-11-05/06: Hercules
fixes, the RAM plan, fit and clip, the document, the embedded game, the
title picture) were built on top of it, and the plan moved to completed/ at
the merge to the integration branch; SPEC.md 126 and the builder are the
authority now.** `make
vbtbundle` zips the builder for a machine with no os8088 tree. It turns a battle's footage into a `.VBT` bundle (SPEC.md
§126) through a window. Section 9 has the questions and their answers,
section 10 what the answers changed, and section 11 the proposal for what an
action DOES.

## 1. What it is for

The owner's ask, in order:
- an interface like the video encoder's, built as one from the first line
  and not added on later;
- an idle animation in, and a recommended loop point that can be checked
  and then encoded;
- any number of menu actions, built in a menu editor;
- a top-level item that is either an action or a SUB-MENU of actions. Up
  and Down pick a top-level item; Left opens a third, optional menu;
- a background image for the menu area, and one for each sub-menu, with
  the text placed inside it at a starting point;
- several clips for one action, one chosen at random when it is taken;
- the ATTACHMENT POINTS of each clip found and recommended, with frames
  added or removed by picking them visually, and a toggle that shows the
  difference from the idle frame.

## 2. The lesson from the encoder's window, and the rule that follows

`os88vencgui.py` was added after `os88venc.py` existed. Its form is derived
from the encoder's argument parser, which is a good rule. But a parser
describes a command, not a project, and every later need had to be pushed
through it:
- a choice that implies other choices (98.2.10);
- groups greyed out when they cannot apply;
- a cancel that kills the work (98.2.11.1);
- the defaults loaded back from a file (98.2.17).

Each of those cost a revision.

**The rule here: the PROJECT is the product, and the window and the command
line are two faces of it.**

| layer | file | what it is | needs Tk |
|---|---|---|---|
| model | `tools/vbtproj.py` | the project: sources, the idle loop, clips, anchors, menus, rules. Load, save (JSON), validate, undo. Every edit is a method | no |
| analysis | `tools/vbtana.py` | frame similarity, loop candidates, attachment scores, difference masks, size and memory budgets; results cached by source hash | no |
| build | `tools/os88vbt.py build PROJECT` | encode, decode, join, write the `.VBT`; progress as JSON lines on stdout | no |
| window | `tools/os88vbtgui.py` | a view of the model, calling only model methods and running analysis and build as subprocesses | yes |

The consequences:
- **Nothing the window can do is out of a script's or a test's reach.** A
  test drives `vbtproj` exactly as the window does.
- **Long work is a process from day one.** Analysis and build report
  progress and can be cancelled by killing the process. That is the
  encoder's 98.2.11.1, learned the hard way.
- **Every derived state is computed, never stored.** "Greyed out" and
  "recommended" come from the model, the way `group_state` does in the
  encoder's window.
- **The project file has a version** and an upgrader from its first day.

## 3. From footage to exact joins

A battle's clips are real footage, so they are encoded LOSSILY by
`os88venc` (budgeted for the target, 98.2). A lossy encoder's frame *n* is a
particular decoded screen. A join is exact only if it is made between the
screens the decoder will really hold (126.2.3). So the build does this:

1. **Encode every clip with `os88venc`**, from its source between its in
   and out points. The options are fixed by the project, not per clip:
   - CGA4, one palette for the whole battle (the band shares it);
   - the canvas's rows;
   - 15 fps, no sound;
   - none of the options a join cannot follow: no flip, no layer, no
     page-ahead;
   - a profile (`5150-st225` by default).
2. **Decode each `.V88`** (`os88vid.Reader`) into its exact screens.
3. **Take the clip's own frame records as they are.** They are already the
   ten lists, from a budgeted encoder.
4. **Make every JOIN from decoded screens** with `seam_ops`:
   - the idle loop's seam: its last frame onto its first;
   - each entry: idle frame *k* to the clip's first frame;
   - each exit: the clip's last frame onto idle frame *j*.

   A join can be big. Its size is shown, and warned about when it is over
   one frame's budget.

The demo's lossless path (`tools/vbtscene.py`, frames drawn rather than
encoded) stays: it is a project whose sources are already CGA4.

## 4. The window

One window, five tabs. A project is open, or the window offers New, Open
or one of the recent ones.

### 4.1 Battle: the frame of the whole thing

- **The target:**
  - the profile (the disk it will stream from);
  - the palette (one of CGA4's, chosen with swatches, as in the encoder's
    window);
  - the canvas's rows (142 leaves the band 58).
- **The music:** any file ffmpeg reads, its loop point, and its level.
- **The intro clip, the victory clip and the defeat clip.**
- **The party and the enemy** (section 9, question 2).

### 4.2 Idle: the loop point

- **A source and a range** (in and out), scrubbed over its frames.
- **RECOMMEND** computes a frame-to-frame similarity matrix over the range,
  downscaled and in the battle's palette, so that dither noise does not
  hide a match. It lists the best (start, end) pairs: a length at least the
  minimum and an end frame that is closest to the start frame. Each is
  scored by how far the two frames differ and by the seam's size in bytes.
  On the owner's own capture this finds the 24.5-frame period that was
  measured by hand (VIDEO-BATTLE-PLAN 2).
- **The seam previewed**: the loop played around its join, over and over,
  with the frame difference shown at the cut.
- **ENCODE** encodes the chosen range, then decodes it and reports the
  seam's real bytes and the loop's KB.
- **The anchors**: none are set here. They belong to each clip (4.3).

### 4.3 Clips: the attachment points

A clip is a source with in and out points, and the effects it carries. An
action owns one or more clips.

For each clip:
- **ENTRY RECOMMEND** scores the clip's first frame against every frame of
  the idle loop. It shows the score as a strip under the loop's filmstrip
  and marks the local minima within a margin of the best as recommended
  anchors. There may be one, or several: the idle loop's own period means
  a good match recurs.
- **EXIT RECOMMEND** does the same with the clip's last frame. Only one
  exit is needed, since the clip ends in one state, and the best is chosen.
- **The filmstrip** of the idle loop's frames, each with its score. Click a
  frame to add it as an anchor, or click an anchor to remove it. A
  **DIFFERENCE** toggle draws the pixels that differ from the clip's frame
  over the idle frame, beside the entry's size in bytes.
- **Trimming in and out** moves the clip's first or last frame. Scores
  update as it moves, since a frame later may match better.
- **The wait**: the longest a command can wait for this clip, from its
  anchors (126.3). Anchors spread out bring it down.

### 4.4 Menus: the editor

A tree, with the band's picture beside it:
- **the TOP MENU**: items in order. Each is an ACTION or opens a SUB-MENU;
- **a SUB-MENU**: actions only (the owner's: one level);
- **the SIDE MENU**, which Left opens: optional, actions only (question 1).

Each menu has:
- a background image, quantized to the battle's palette (as the encoder
  quantizes, so it looks on the band as it will on the glass), or none;
- the text's STARTING POINT (x, y), dragged on the picture or typed;
- the row spacing, the ink, and the cursor's style;
- what Esc or Right does from it (back to the menu above).

Each action has:
- a label;
- one or more clips, chosen at random;
- its MP cost and what it does (question 2).

**The band's picture is the engine's, drawn in Python from the same rules**
(126.5): the font is `OSAPI_FONT_GLYPHS`'s 8x8 table, the colours the
palette's. A menu that runs off its background, or text that overlaps it
badly, is visible here before any build.

### 4.5 Build: the budget, the bundle, a disk

- **Per clip**:
  - its bytes and KB/s against the profile's disk;
  - its entries' and its exit's bytes;
  - its longest wait.
- **Memory on a 640 KB machine** (126.4):
  - the resident claim;
  - the ring;
  - the clip cache, and whether every clip has a place or slots are used.
- **The checks**, each one an error until fixed: a clip with no anchor, a
  menu item with no clip, a join over a frame's budget, music missing.
- **BUILD** writes `BATTLE.VBT` and runs `os88vbt.py verify` on it.
- **"...and make a disk of it"**, the encoder's own (98.2.12.1): a floppy
  of any size, or a hard disk image, with `VBATTLE.O88` beside the bundle.

## 5. What the engine must learn for this

The current engine (SPEC.md §126) plays the demo's shape. A captured battle
needs these changes. Each is the bundle's format version 3, with its own
SPEC section first:

1. **Clips bigger than 60 KB.** The owner's 2-Open is about 180 KB of
   video for 4.9 s. A clip cannot be one cached region, so a playing clip
   must STREAM through a ring as the music does. Read-ahead then reads a
   clip's HEAD, its first ~1.5 s, rather than the whole clip.
2. **Anchors per clip**, not per bundle. Each clip has its own set and its
   own exit frame.
3. **Several clips per action, one chosen at random.** The roll is made
   when the cursor lands on the action, not when it is taken, so the
   read-ahead knows which clip's head to read. A new roll happens each
   time the cursor lands.
4. **Menus as a tree**: top, sub and side menus. Each has its own
   background and starting point.
5. **Backgrounds**: a 320 x 58 CGA4 picture is 4,640 bytes, held resident.
   The band is drawn from it, then the text over it in one pass a cell:
   ground from the background, glyph in ink. That is 6.1's rule, one write
   a byte.

## 6. Sound from captured footage

A captured clip's soundtrack has the game's music in it, so its effects
cannot be cut out cleanly. The design takes effects as their own files, each
placed on a clip's frame on the Clips tab. It offers to take a clip's own
soundtrack as its effect anyway, for footage that has no music under it.
The music is its own file (4.1). This is question 4.

## 7. Waves

1. **The model and the command line.**
   - `vbtproj`, `vbtana`, `os88vbt build PROJECT`;
   - the demo re-expressed as a project;
   - `tests/` for the model (a host check);
   - the bundle format version 3, and the engine's per-clip anchors.
2. **The window's shell, and the Idle tab**: New, Open, Save, the
   subprocess runner with Cancel, and the loop's recommendation and encode.
3. **The Clips tab**: entries and exits, the filmstrip, the difference view.
4. **The Menus tab**, and the engine's menus and backgrounds.
5. **The engine's streamed clips** (5.1), and the random choice made when
   the cursor lands (5.3).
6. **The Build tab**: the budget and the disk.

Each wave is checked on the host, and the engine's waves on MartyPC
(`os8088_xt_hdd_sb`), with the owner's own footage outside the tree and the
demo inside it.

## 8. What this does not do

- It does not play the battle with sound in the window. Tk has no audio in
  the standard library. A **Test** button builds the bundle and boots it
  under MartyPC instead, and the window plays the joins and the menus
  silently.
- It does not edit footage. Masking a marker out of a clip is not built
  here.

## 9. Questions for the owner

1. **"Left selects a third, optional menu."** Is that a SIDE menu that Left
   opens from the top menu (as some battle systems put Defend or Change on
   Left and Right)? Or a third LEVEL under a sub-menu, which Left goes back
   out of?
2. **Whose menu, and what does an action do?**
   - Does each party member have a menu of their own, chosen by whose turn
     it is (as now)?
   - Should the builder edit the rules: HP, MP, damage, healing, the
     enemy's turn? Or is the first cut only menus to clips, with the rules
     kept as simple as they are now?
3. **What comes in: footage, or encoded `.V88`?** The design takes FOOTAGE
   and runs the encoder itself, so every clip shares one palette and one
   set of options. Taking finished `.V88` files instead means checking
   both of those and refusing the ones that differ.
4. **Sound effects**: separate files (the design's default), or a clip's
   own soundtrack?
5. **The band's height**: fixed at 58 rows, or a project setting? Your
   footage is 142 rows high, which leaves 58.

## Answers (the owner, 2026-11-04)

1. **The side menu**: yes. Left and Right open a menu of their own, of the
   Defend / Charge Limit / Change Character kind.
2. **Characters**: each has their own menu, since each can be given
   different abilities. The builder needs a CHARACTER EDITOR, from 1 to 4
   characters. The rules are in section 11, which is the proposal for this.
3. **Footage, encoded by the builder itself.** Nobody manages hundreds of
   `.V88` files outside it.
4. **Effects come as separate files.** Recognising the music in a captured
   soundtrack and muting only that would be the easier path if it could be
   done automatically. It is not attempted.
5. **The band's size is a project setting**, and so is the video's.

## 10. What the answers changed

### 10.1 Clip STREAMING is a wave of its own, and comes first

It is core to the project, and the builder would otherwise be designed
against an engine that cannot play its output. It is the next wave, before
any of the builder's (section 7's order moves). Its contents:

- **The bundle's format version 3**:
  - every clip's region is a STREAM of records of any length;
  - each record carries its frame's sound;
  - the attachment points are per clip;
  - the canvas and the band are rectangles in the header.
- **Two clip rings.** A clip plays from one while the next one's head is
  read into the other, so two clips can follow each other with no idle
  frame between them (the openings, 10.3).
- **A clip's SOUND streams with its frames**, as a V88's does: each
  record's own bytes, for that frame. A clip's sound is either MIXED over
  the music (an action's effects, placed in time by the builder and baked
  into its sound) or the clip's OWN, played instead of the music (the
  openings). The engine then needs no effect queue: where an effect sounds
  is the builder's decision, made once.
- **The music JOINS at a chosen offset** when a clip that replaced it ends.

### 10.2 Sound in the builder

- **The music track**, with a LOOP-POINT finder of the idle loop's kind:
  - candidates from the audio's own similarity (the onset envelope and the
    spectrum, frame by frame);
  - each candidate played around its join;
  - the loop set by start and end.

  All music loops.
- **A clip's effects**: files placed on the clip's timeline, by frame or
  dragged, each with a level. They are mixed into the clip's sound at
  build time.
- **The menu's sounds**, resident:
  - the selection moved;
  - a sub-menu opened;
  - a sub-menu closed;
  - an action taken;
  - refused (not enough MP, and the like).
- **Listening.** The window plays the music around a loop point, a clip
  with its effects, and the join from an opening into the music, through
  the platform's own player: `winsound` on Windows (standard library),
  `afplay` on macOS, `aplay` or `paplay` on Linux. The window builds the
  WAV and hands it over; it plays nothing itself.

### 10.3 The openings

- **Opening 1** (optional, full screen or not, any number of them in turn):
  a clip played whole, with its OWN sound.
- **The SCENE opening**, the last: the menu is up. It plays with its own
  sound and then joins the idle loop through an exit, as an action does.
  The music comes in at a JOIN OFFSET. The builder suggests that offset by
  comparing the end of the opening's sound with the music (a
  cross-correlation of their envelopes), so the music continues what the
  opening's sound was doing. The user can move it. The join is the only
  timing involved: nothing is waiting on a key.

### 10.4 Sizes are the project's

- **The video's rectangle and the band's** are set by the project, inside
  the adapter mode's screen, and must not overlap: half and half, a short
  band, or a canvas less than full width. The records address the screen
  as the adapter's memory image, so the canvas's position is the build's
  and costs the engine nothing. The band's rectangle is in the header and
  the engine draws inside it.
- **Hercules (720 x 348) and VGA come later, and they are encodings**:
  another rendition of the same project, with its own rectangles. A 600 x
  200 video over a 600 x 100 band on Hercules is a project setting, not a
  new engine.

### 10.5 The waves, again

0. **Clip streaming**, in the engine and the bundle (10.1). The demo grows a
   clip far larger than memory to prove it. **DONE 2026-11-04** (SPEC.md
   125, format version 3): two 48 KB clip rings, each record's sound, per
   clip anchors, the openings chained, the music joining at an offset. The
   250 KB Limit streamed in real time on MartyPC's XT with 0 late frames.
1. **The model and the command line**. **DONE 2026-11-04**:
   - [x] `tools/vbtproj.py`: the project file (version 1, an upgrader that
     refuses what it cannot read), every edit a method through one door
     with undo and redo, `check()` with ERROR / WARN / INFO (INFO: what
     the engine of today does not play yet), and `engine_script()`, the
     subset the engine plays now;
   - [x] `tools/vbtana.py`: a source's DECODED screens (os88venc for the
     target, cached by the source's content hash beside the project),
     sounds (a file, a scene's, or a range of a source's own soundtrack),
     loop candidates, attachment (in, anchors, out, exit, each scored),
     the longest wait, the budget against the profile's disk;
   - [x] `os88vbt.py build PROJECT [--json]`, `demo-project`, `recommend`;
   - [x] the demo re-expressed as a project, building the shipped bundle
     byte for byte; the owner's H3 footage as a project, its points from
     `recommend`, played to victory on 86Box's ST11M with 0 stalls;
   - [x] characters (1 to 4), menus (top, sub, side), actions with
     section 11's verbs and pools, the enemy's stances and moves, in the
     model and checked;
   - [x] **the ENTRIES as a region of their own** (bundle version 4,
     SPEC.md 126.2), for the owner's "more than one set of join clips":
     a paragraph number names each entry, so they take what memory
     there is rather than sharing 60 KB with the idle loop;
   - [x] `tests/vbtbuild.py` (soak).
   Not in it, and why: the engine still plays the subset `engine_script()`
   makes (two characters, a flat menu of four, DAMAGE and HEAL, the first
   HIT clip, the enemy's first move). `check()` says so as INFO on each
   project. The rules are wave 4.
2. **The window's shell; the Battle tab (sizes, palette, music and its
   loop) and the Idle tab.** **DONE 2026-11-04** (`tools/os88vbtgui.py`):
   - [x] the shell: New, Open, Open recent, Save, Save as, Undo and Redo
     (the model's), the title's unsaved mark, and a JOB runner: every long
     piece of work is a `vbtana` or `os88vbt` process with JSON lines on
     stdout, a progress bar, and a Cancel that kills its process group;
   - [x] the Battle tab: the name, the profile it is made for (with the
     encoder's own description), the CGA4 palette (set, bright,
     background, four swatches), the video's rows over the band's, and
     the MUSIC: a sound, Add a file, Listen, FIND LOOPS (`music_loops`:
     the spectra's similarity over a 2 s window, each end then placed to
     the sample by the waveforms), Use, Listen to its jump (a WAV of 3 s
     either side, through the platform's player), the loop and the join
     typed in seconds;
   - [x] the Idle tab: a source (Add a video), its frames loaded (encoded
     in a job the first time), a scrubber, the loop's frames, RECOMMEND,
     Use, PLAY ACROSS THE SEAM, DIFFERENCE AT THE SEAM (in the colour
     furthest from the palette's four), COST IT (the records the engine
     will hold);
   - [x] the model: the music's LOOP, which the build makes the music's
     region (the join counted from its start);
   - [x] `tests/vbtguitest.py` (soak): the window without a window.
   Seen under Xvfb on the owner's H3 project; the screenshots went to the
   owner. Not seen on Windows or macOS.
3. **The Clips tab**: attachments, effects on the timeline, listening.
   **DONE 2026-11-04**:
   - [x] the clips, New clip (a name and a source, the whole of it to
     start) and Delete (refused while an action, opening or ending uses
     it); the source, In and Out (its cues move with the in point and
     those off its end go; its own soundtrack follows), and ENDING (the
     battle ends on it: held, no exit - the wave 2 lesson);
   - [x] the idle loop's FILMSTRIP: every frame with the clip's in frame's
     score against it, the ENTRY's real bytes, a bar, the out frame's
     score, the anchors boxed, the exit dashed, the recommended ones'
     bars green. Click selects, double-click toggles an anchor,
     right-click makes the exit;
   - [x] DIFFERENCE from the selected idle frame, the longest wait, and
     RECOMMEND / USE over the clip's whole source;
   - [x] EFFECTS on the clip's own timeline: a sound at the frame under
     the scrubber, dragged to move, a level, removed; a sound file, or the
     clip's OWN SOUNDTRACK; LISTEN, which mixes the cues with the build's
     own call, so what is heard is what is baked (`tests/vbtguitest.py`
     holds them equal frame for frame).
4. **The Menus and Characters tabs**, and the engine's menu tree,
   backgrounds and rules (11). **DONE 2026-11-04**:
   - [x] the RULES as bundle data (version 5, SPEC.md 126.2.4): characters,
     actions of up to 4 effects with every verb of 11.2, 11.2.1 and
     11.2.2, clip pools per outcome and per target, menus (top, sub, side)
     with their looks, stances (modifiers, weighted moves at five target
     rules, responses, an automatic change, a timed return), items, band
     pictures;
   - [x] `tools/vbtrules.py`: the compiler, the reader and the REFERENCE
     (`Sim`), and the engine (`apps/vbattle/vbrules.inc`, `vbgame.inc`)
     routine for routine beside it: the same generator, drawn in the same
     order (SPEC.md 126.6.3), the same capped arithmetic;
   - [x] the engine's menu tree and targeting, up to 4 characters, the band
     laid out from the header, pictures under the band and any menu, the
     text over them, a rebuilt band composed off the screen;
   - [x] the Actions tab (the verbs as forms, the pools general and per
     character, the reference's estimate), the Characters & menus tab (the
     stats, the menu tree, each menu's look, the band pictures and items,
     the band as the engine draws it, `tools/vbtband.py`), the Enemy tab
     (stances, moves, responses, the change and the return);
   - [x] `tests/vbtrules.py` (the reference by hand) and
     `tests/vbtengine.py` (the engine against it in lockstep on MartyPC,
     43 turns), both soak.
   Not in it: a stance's own idle loop (it shares the battle's; `check`
   says so); a time gauge, several enemies.
   - [x] **the owner's first feedback** (2026-11-04): drag and drop (a
     project anywhere; videos into clips, or the idle's source on the Idle
     tab; sounds into the sounds, the music or a clip's cues), Add videos
     on the Clips tab, previews of what is CLICKED rather than what is set,
     "Play the loop" whole (the seam preview's own restart had looked like
     a jump), Recommend's best loop previewed at once, a progress bar for
     reading a video, its own soundtrack over frames x to y and cues that
     stop, the endings on the Battle tab.
5. **The openings** in the window (10.3), and **the Build tab**.
   **DONE 2026-11-04**:
   - [x] the Openings tab: the openings in turn (add, remove, earlier,
     later; at most 8, the engine's), each one's clip and sound (any sound,
     its own soundtrack, or silent), the scene opening's join onto the idle
     loop with a recommended frame (its seam's fewest pixels), the openings
     scrubbed as whole screens, and PLAY THE START: the openings, then the
     idle loop from the join, with the sound the engine makes;
   - [x] the music's JOIN (10.3): suggested by the loudness envelopes
     (`vbtana.music_joins`, `joins`), listened to (`joinwav`, mixed at the
     engine's levels), used - the candidate clicked is the one heard;
   - [x] the Build tab: every check (double-click goes to its tab), BUILD
     (the bundle, verified), the BUDGET per clip (KB, seconds, KB/s, the
     clip and the music against the profile's disk, red over it, the
     head's read, whether it fits a ring whole, anchors, the longest
     wait) and the engine's MEMORY on a 640 KB machine (126.4's carving,
     its free figure measured on MartyPC's XT);
   - [x] "...and make a disk of it": the encoder's seven disks, shared
     through `tools/os88mkdisk.py`, `os88vbt.py disk`, a floppy too small
     refused with its sizes; the builder's zip carries the disk makers and
     boot/, so its hard disks boot;
   - [x] `tests/vbtguitest.py` and `tests/vbtbundle.py`.
   Not in it: an opening that is not full screen; the clip cache of 4.5
   (the engine has two rings and no cache).
6. **The title loop, the fade and the endings** (the owner, 2026-11-04:
   "a looped opening and ending. The intent here is fade transitions, but
   the user could provide clips that join in other ways too... The victory
   would fade to the end clip, which would loop and wait for a key - or to
   the opening clip, letting the user play in a loop either way").
   **DONE 2026-11-04** (bundle version 6, SPEC.md 126.3.1):
   - [x] a TITLE loop before the openings, full screen, round until Enter
     or Space, its loop recommended as the idle loop's is; left by a FADE
     the engine draws (a 4 x 4 ordered dissolve to colour 0, the sound a
     shift lower each quarter) or by a CLIP entered at the title's anchors;
   - [x] each ending HELD, FADED to its END loop (in the canvas, the band
     up under it), JOINED to it (its last frame onto the loop's first,
     exactly), or faded into playing AGAIN; Enter on an end loop plays
     again, from the title; Esc leaves from anywhere;
   - [x] the Title & endings tab, the demo's title and endings, verify's
     laps and joins, and `tests/vbtloops.py` (MartyPC) for both ways in
     and out.
   Not in it: a loop bigger than a ring (it is refused, and the budget says
   so first); a fade INTO a clip from black at the title (the openings
   start from black anyway).

## 11. What an action does: a small, fixed set of verbs

**Agreed by the owner (2026-11-04), with the enemy's stances and responses
added (11.2.1).** **No action is hardcoded, and no action is a script.** An
action is DATA made from a short list of verbs that every battle of this
kind uses. The engine interprets them from the bundle's tables, and the
builder edits them as forms.

### 11.1 Characters (1 to 4) and the enemy

- **A character**:
  - a name;
  - HP, MP, ATK, MAG, DEF, MDEF;
  - a LIMIT gauge (0 to 100), which damage taken fills;
  - their own menu tree (the top menu, its sub-menus, and the side menu).
- **The enemy** (one, for now):
  - a name and the same stats;
  - its ATTACKS: a weighted list, each an action as below, with a rule
    for its target.

### 11.2 An action

| part | choices |
|---|---|
| **target** | self; one ally (the player chooses); all allies; the enemy |
| **cost** | MP *n*; an item (the party's count of it, *n* to start); the limit gauge full; none |
| **effects**, in order | DAMAGE (physical or magical, a power, hits 1 to 4, a variance); HEAL (a power, or a % of the most HP); RESTORE MP; REVIVE (to a % of HP); GUARD (damage taken halved until this character's next turn); CHARGE (the next DAMAGE doubled); BOOST or LOWER a stat (by a step, for *n* turns); PASS (this turn goes to the next character: Change); ESCAPE (the battle ends, a clip of its own); STANCE *s* (the enemy's moves only: it goes into stance *s*, 11.2.1) |
| **accuracy** | % to hit; a miss plays the MISS pool |
| **clips** | POOLS of clips, chosen at random: HIT, MISS, CRITICAL, and FINISHING (it brings the enemy to 0). A pool may be keyed by TARGET, so that an attack on character 2 plays character 2's footage. An empty pool falls back: CRITICAL to HIT, a target's pool to the general one |

### 11.2.1 The enemy's STANCES and RESPONSES (the owner, 2026-11-04)

The verbs and target rules above are agreed. The enemy gets two more things:

- **STANCES.** The enemy is always in one stance: normal, defensive, an
  attack posture, and so on, as many as a battle defines. Each stance has:
  - its own weighted POOL of moves;
  - its own idle (an idle loop of its own, with its own clips' anchors
    against it, since the enemy LOOKS different in it), or it shares the
    battle's;
  - optionally, modifiers: DEF/MDEF up in a defensive stance, ATK up in an
    attack posture.

  A move can CHANGE the stance (a new verb, STANCE *s*). The clip that
  changes it plays from the old stance's idle and seams onto the new
  stance's idle, which is an exit like any other, onto another loop. A
  stance may also change on its own: a rule of the form "when HP falls
  under *n* %", or "after *n* turns in it".
- **RESPONSES.** A stance may define a RESPONSE: a move the enemy makes
  AFTER a player's action, outside its own turn. Each response has:
  - which actions trigger it: all, physical, magical, or named ones;
  - a chance;
  - its own pool of clips, played after the player's clip has seamed back
    onto the idle loop.

  Responses are optional, and each stance has its own or none. A response
  can change the stance too: a counter that puts the enemy into its attack
  posture.

What this costs the engine:
- a stance byte;
- per stance, a pool table, a response table and an idle;
- one more check after each player action.

The bundle grows by a few hundred bytes a stance, plus each stance's idle
loop if it has one. Several idle loops means the resident block holds
several loops, so a battle with many stances can be refused by the
builder's memory budget (4.5) before it is built.

### 11.2.2 Misses, defence, and moves that are not attacks (the owner, 2026-11-04)

Five more, with the owner's first three enemy clips (the scorpion on the
gunner: HIT, MISS, DEFENDED). Where 11.2 already had a piece, this says
what is added to it.

- **MISSES.** Every action that targets has a MISS CHANCE (11.2's
  accuracy, now on the enemy's moves as well as the party's). A miss plays
  the action's MISS pool: "the scorpion strikes the gunner and misses" is
  its own footage, keyed by target like HIT. A character may also have an
  EVADE stat, added to the attacker's miss chance (0 for none). An empty
  MISS pool plays HIT with no damage and "Miss" on the band.
- **DEFEND has clips of its own, both sides of it.** GUARD (11.2) is
  the defend verb; what was missing is footage:
  - the guard ACTION's own pool, played when a character chooses Defend
    (the gunner raising their guard);
  - a DEFENDED pool on every attack, played instead of HIT when the target
    is guarding ("the scorpion strikes the gunner, who blocks it"), keyed
    by target. An empty DEFENDED pool falls back to HIT; the halved damage
    is the same either way.
  So an enemy attack has up to five pools: HIT, MISS, DEFENDED, CRITICAL,
  FINISHING, each optionally per target. Which one plays is decided by the
  roll BEFORE the clip is chosen, so the footage always agrees with the
  number.
- **A STANCE CHANGE IS AN ACTION, and it lasts *n* turns.** STANCE *s*
  (11.2.1) becomes a move in its own right in the enemy's pool, with a
  DURATION: *n* of the enemy's turns, after which it returns to the stance
  it came from (or *n* = 0, until another move or rule changes it). It
  deals no damage and has no target; its clip is the change (old idle onto
  the new stance's idle), and the return is a clip of its own, chosen
  when the duration runs out, before the enemy's next move.
- **A move with NO TARGET.** Target "none": the enemy roars, circles,
  wipes its blade. No damage, no roll; it plays its pool and says its
  line. It may carry the non-damage verbs (BOOST or LOWER a stat, STANCE),
  so "it roars, and its ATK rises" is one of these.
- **An AREA attack.** Target "all characters" for the enemy, as "all
  allies" already is for the party. Each character standing is rolled for
  on its own (miss, guard, damage), and the band shows each result. The
  CLIP is one footage of the whole party being hit, so its pools are not
  per target: HIT when at least one is hit, MISS when all miss. DEFENDED
  is used only when every character hit was guarding.

The target rules for the enemy's single-target moves, so that a move can
say who it goes for: random among the standing, the lowest HP, the one
who hit it last, alternating (what the engine does today), or a named
character.

What it costs on top of 11.4: a target kind (4 values) and a duration
byte a move, three more pool slots an action, an EVADE byte a character,
and the per-target rolls of an area attack - about 300 bytes of engine,
and nothing per frame.

**Found on the way: the resident block is the budget that runs out
first.** Every clip's ENTRY (its first frame, as a difference from the idle
frame of each anchor) is held resident beside the idle loop, so that a
chosen action starts on the very next frame. With the owner's footage
that is about 2.2 KB a clip on top of a 48.7 KB idle loop, and eight clips
(knight 2, gunner 2, scorpion 3, victory) came to 65,365 bytes, over the
block's 60 KB. A battle with pools of clips per target will have dozens.
So, before the pools: an entry for a clip with ONE anchor moves into the
clip's own stream (its head is read before the clip may start anyway,
and 40 KB of it is far more than the entry), and only clips with several
anchors keep resident entries. The builder's budget (4.5) shows the
block's bytes per clip either way.

### 11.3 The arithmetic, one formula each

So that the builder can show "about 95 damage" beside an action:
- physical damage = power × ATK / 16 × (256 − DEF) / 256;
- magical damage is the same with MAG and MDEF;
- each times a random 90 to 110 %;
- a critical (1 in 16, physical only) doubles it;
- GUARD halves what the guard takes; CHARGE doubles what it gives.

All of it is integer arithmetic on an 8088, at a few hundred cycles an
action.

### 11.4 What it costs

- the bundle: a few hundred bytes of tables;
- the engine: an interpreter for nine verbs, perhaps 2 KB of code, and
  about 300 bytes more for 11.2.2.

### 11.5 What it does not do

Turn order is round robin: the characters in order, then the enemy. A
timed gauge (ATB) is a later wave. Status ailments beyond GUARD and CHARGE,
several enemies, and items as a menu of their own are left until a battle
needs them. The verbs are numbered in the bundle, so a later verb adds to
the list and does not change the existing ones.
