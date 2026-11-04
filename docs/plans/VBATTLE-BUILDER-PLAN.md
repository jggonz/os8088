# VBATTLE-BUILDER — the clip-graph builder, with its window from the start

**Status: DESIGN, for the owner's feedback (2026-11-04).** Nothing here is
built. It turns a battle's footage into a `.VBT` bundle (SPEC.md §125)
through a window. Section 9 lists the questions this design cannot settle
alone.

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
screens the decoder will really hold (125.2.3). So the build does this:

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
  anchors (125.3). Anchors spread out bring it down.

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
(125.5): the font is `OSAPI_FONT_GLYPHS`'s 8x8 table, the colours the
palette's. A menu that runs off its background, or text that overlaps it
badly, is visible here before any build.

### 4.5 Build: the budget, the bundle, a disk

- **Per clip**:
  - its bytes and KB/s against the profile's disk;
  - its entries' and its exit's bytes;
  - its longest wait.
- **Memory on a 640 KB machine** (125.4):
  - the resident claim;
  - the ring;
  - the clip cache, and whether every clip has a place or slots are used.
- **The checks**, each one an error until fixed: a clip with no anchor, a
  menu item with no clip, a join over a frame's budget, music missing.
- **BUILD** writes `BATTLE.VBT` and runs `os88vbt.py verify` on it.
- **"...and make a disk of it"**, the encoder's own (98.2.12.1): a floppy
  of any size, or a hard disk image, with `VBATTLE.O88` beside the bundle.

## 5. What the engine must learn for this

The current engine (SPEC.md §125) plays the demo's shape. A captured battle
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
