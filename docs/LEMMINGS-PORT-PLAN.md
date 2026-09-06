# LEMMINGS port plan — Lemmings (DMA Design / Psygnosis, 1991) as an os8088 C package

The design record for SPEC.md §92, produced by `.claude/skills/port-to-os8088`'s
scouting workflow — three source scouts, three tree scouts, one planner, three
adversarial reviewers and a reconciler — on 2026-09-06, with the decisions below
folded in. `workflows/implement.js` reads this file one wave at a time, and the
implementers build from the wave sections, which are reproduced here in full and
not compressed.

The reference sources live outside this repository and nothing from them is
vendored (CONTRIBUTING.md §6). The 26 data files are fetched at build time,
pinned by SHA-256, and never committed.

## Summary

Lemmings (DMA Design / Psygnosis, 1991) as an os8088 C package: `apps/lemmings`, package name LEMMINGS, one translation unit of ~7,100 lines split into ten `#include`d `.c` files plus three hand-written `.inc` rasters, an overlay (LEMMINGS.OVL) that is much larger than the draft planned, and a host-side converter (`tools/os88lem.py`) that turns the fetched 26-file DOS data set into a machine-ready format so the 8088 does no bit-stream decoding at any point.

THE SHAPE. The desktop window is a LAUNCHER only: the four ratings, the level names of the chosen rating PAGED to the live window height, the original's preview fields beside them, a Play action, four state rows (Mode / Music / Level Code / Save Progress) that exist so SPEC.md 47's greying has something to sit on, and the About card. Play stacks SPEC.md 11.2's fullscreen latch on SPEC.md 53's exclusive bracket (Missile Command's pattern, `apps/missile/missile.asm:1186-1227`), and everything the player recognises as Lemmings happens inside that bracket.

WHAT THE REVIEWS CHANGED. Five things, each an arithmetic error in the draft rather than a matter of taste. (1) THE IMAGE BUDGET WAS 25% LIGHT: the draft charged resident lines at 3.66 bytes/line, which is cword's resident bytes over cword's TOTAL (resident + overlay) line count. Measured here: `wc -l apps/cword/*.c *.h` = 9,102 lines producing 35,886 + 18,564 = 54,450 bytes = 5.98 bytes a line, 5.7 after the runtime the plan counts separately. At the right rate the draft did not fit, so the split the draft deferred to "the next wave" is done in WAVE 1 and the string literals move out of the image into a resource band of the converted data. (2) THE FRAME BUDGET'S CENTRAL CONSTANT WAS OFF 16x: 260,000 is the CYCLE count of a 55 ms tick, used as an instruction count. PERFORMANCE.md's own anchor is ~16.4 clocks an instruction, so a tick is ~16,000 instructions, not 260,000, and the honest figure is ~2-5 fps against 17, not 5-8. That is now a GO/NO-GO with a number and a designed fallback, and the pointer is decoupled from the game frame so input latency stays one tick whatever the world costs. (3) THE PREVIEW AND POSTVIEW SCREENS ARE 640x350 IN THE ORIGINAL (`GameScreen.Preview.pas:114` `target.SetSize(640, 350); // the original dos-screen size`) and the draft drew them in a 320x200 mode where 40 characters of a 16x16 font need 640 pixels and the last line sits at y=322. They now switch mode inside the bracket: FSXM_VGA12 on VGA holds the original layout unscaled, FSXM_HERC (720x348) holds it with 10 rows to spare, and CGA/EGA get FSXM_CGA640 with the blank-line spacing compressed out and that stated as a fact. (4) THE OVERLAY'S REFUSAL PATH TOASTED THROUGH THE KERNEL IN A FOREIGN MODE; the module is now forced resident from the Play handler, outside the bracket, and the launch refuses in a window if it will not load. (5) `os88_file_read_at()` IS DS-RELATIVE and every destination in this program is a claim, so the draft's whole cluster-window paragraph was unbuildable; the converter now emits one 512-padded BAND FILE per bank and `os88_file_read_seg()` reads it straight into a claim, which needs no SDK addition at all.

THE RASTER, three backends over one game. VGA gets FSXM_VGA0D (mode 0Dh, 320x200x16 planar): the CRTC offset register goes to 100 words so VRAM is a 1600-pixel-wide virtual screen, the whole 1584x160 level terrain lives in it at offset 8,000 (32,000 bytes a plane of the 65,536 available), the 320x40 skill panel lives at offset 0 and is shown unscrolled below a CRTC line compare at scanline 320 with the Attribute Controller's Pixel Panning Mode bit set, and scrolling is the start address plus pel panning — so terrain costs ZERO drawing per frame and only destruction writes it. Lemmings and objects are save-under sprites saved and restored with VGA write mode 1, drawn with write mode 2. CGA gets FSXM_CGA320 and Hercules FSXM_HERC with the game in a 640x200 box centred at (40,74) — both are Tank's shadow-plus-dirty-span backend (SPEC.md 85.3). EGA is NOT a fourth backend: `kernel/fsx.inc:117` gives EGA `0x000F`, the CGA-compatible modes only, so an EGA machine runs the CGA raster and the Mode row greys 0Dh with fsx_caps' own bit.

THE ARITHMETIC, said once and honestly. A 4.77 MHz 8088 executes ~16,000 instructions in one 55 ms tick. Sixty live lemmings at a realistic 800-2,000 instructions each — the probe calls alone are 150-250 of that before a handler evaluates a condition — is 165-410 ms of mechanics, plus 30-60 ms of VGA sprite work: an XT frame of 200-470 ms, or 2-5 fps against the original's 17. Every clock in Lemmings counts game frames, so the release rate, the spawn interval and the time limit all slow together and no level becomes unwinnable — but at 2-5 fps that fairness argument does not carry the POINTER, so the cursor and the mouse are repainted every FSXW_TICK whether or not a game frame completed. Wave 2 measures this on MartyPC and question 7 is the threshold below which the XT is greyed with the measured fact and the 386 is the target machine.

## Decisions (taken by the orchestrating session at the user's instruction "make as many decisions as you can on your own"; binding for every wave)

These answer the seven questions the plan came back with, plus one working rule.
Where a decision contradicts a paragraph reproduced below, **the decision wins**
and the contradiction is called out in "Where this file overrides the plan".

1. **Oh No! More Lemmings: NOT in this port.** Original only — 120 levels, 5
   graphic sets, 4 specials. The converter keeps the door open (a second data
   set is a second manifest) but nothing builds it, and no implementer should.
2. **Where LEMMINGS lives:** its own on-demand images only — `make lemmingsdisk`,
   four geometries (1.44MB, 720KB, 360KB, 1.2MB) — which is the RUNCPM / C64 /
   Weave shape; **plus The Wire** (the web repo, a `.WPK` archive, done at the
   end by the orchestrator). A folder on `build/apps-all.img` only if the
   cluster arithmetic still fits with everything else on it, decided in wave 6
   by the numbers and not required.
3. **Small disks:** the 720KB disk carries everything except the four VGASPEC
   pictures; the 360KB disk carries two graphic sets and only the levels that
   use them. The converter PRINTS the manifest in clusters and bytes on every
   run, a `LEVELS.TXT` on each disk says what it carries, and a level whose
   files are not on the disk is present and greyed with the fact — the file
   name plus the cluster arithmetic.
4. **Licence posture:** lift the original's on-screen strings verbatim (result
   texts, preview and postview wording, the status template, level names) with
   attribution in every file header carrying derived material, in `README.TXT`,
   and named in the About box. That is CWORD's posture. Nothing GPL or LGPL is
   copied. The 26 data files are fetched by `tools/getlemmings.py` (pinned URL
   plus SHA-256, into `build/`) and NEVER committed.
5. **The explosion's 80-particle spray:** fetch Lemmix's
   `src/Data/Particles/Particles.dat` as a second pinned file in
   `tools/getlemmings.py` and draw the spray — but as **wave 6 polish, only if
   the resident size line still has 1,500 bytes or more spare after wave 5**.
   Otherwise ship without it, stated as absent in `README.TXT` and SPEC.md §92.
6. **SPEC.md section number: LEMMINGS is §92.** main's §90 is FONT VIEWER;
   PACCMAN and PHOTOSHOP, in flight in sibling worktrees, hold the numbers 90
   and 91 (cited without section marks here because 91 is not a heading in this
   tree yet, and the doc gate is right to say so). The
   section and a regenerated `docs/INDEX.md` land in the same commit as this
   file, before any code.
7. **The XT: SHIP ON THE XT AT WHATEVER RATE WAVE 2 MEASURES.** The user's own
   words: *"If it can't run on XTs reliably, try your best, and that's good
   enough."* Every game clock counts game frames, so nothing becomes
   unwinnable; the pointer is repainted every `FSXW_TICK` regardless. The
   measured MartyPC / 86Box figure — fps on a busy level, on a 4.77 MHz 8088,
   per adapter — is recorded in SPEC.md §92, PERFORMANCE.md and `README.TXT`,
   and never in the About box. **`vm/xt-lemmings` IS created.** There is **no
   cap on live lemmings**.
8. **Working rule:** the skill files under `.claude/` in this worktree carry
   session-local edits (embedded workflow arguments); **never stage or commit
   anything under `.claude/`**. Never stage `vm/386dx/86box.cfg` or
   `vm/xt-wire/86box.cfg` — they are dirty user files. Always `git add` explicit
   paths, never `-A` or `-u`.
9. **THE WIRE'S LIMITS SHAPE THE CONVERTER'S OUTPUT** — scouted from SPEC.md
   §88.13/§88.14 and the site's `tools/wire.py`; the recipe is in
   `<scratchpad>/wire-recipe.md`, and the limits are pinned in SPEC.md §92.3.2.
   Five parts, and the converter refuses on each rather than discovering it at
   publication:
   - **(a)** No band file may exceed **64,512 bytes** (`WIRE_FILEMAX`; and
     `RD_FILEMAX` = 65,024 one device along, SPEC.md §62.9.10). The converter
     REFUSES to write a larger one, and a bank that would exceed it — a 4bpp
     VGASPEC picture at 76,800, or any future style — is written as **numbered
     parts** the package reads in sequence into **ONE claim**, one
     `os88_file_read_seg` per part at successive 512-aligned offsets of that
     claim. Each part is 512-padded, so the next part's base is aligned by
     construction. **The claim is still sized for the whole bank; it is the
     single READ that is bounded.**
   - **(b)** The folder should stay **under about 90 files** (`RD_MAXENT` = 96
     directory rows on the RAM disk). Group the 80 level records **eight to a
     file**, the way the DOS files do, and keep one band per style bank, so the
     folder is about 30 to 40 files.
   - **(c)** The whole data set plus package plus overlay must stay under
     **1,048,575 bytes** as one `.WPK` stream (`WIRE_ARCMAX`), which ~836KB
     plus overhead does. The converter prints the archive total beside its
     per-geometry cluster manifests.
   - **(d)** On a 640KB machine The Wire's **Load Program (run from RAM) will
     grey** with the RAM-disk figure and **Add to Disk is the path**. That is by
     design, not a defect.
   - **(e)** The Wire record is **kind 1 (game)**, **tier 0** if wave 2's XT
     measurement is playable and **tier 1** otherwise; every file name is
     uppercase 8.3 in `[A-Z0-9_-]`.

### Where this file overrides the plan

Five paragraphs below were written before the decisions and are superseded:

- Anything reading "pending question 1" or "pending question 5" in **Scope →
  Absent**: decision 1 settles Oh No! (out), decision 5 settles the particle
  spray (conditional wave-6 polish at a stated size threshold, absent
  otherwise).
- **Wave 2's `done_when`** treats the measured frame rate as a GO/NO-GO gate on
  wave 3. Decision 7 removes the gate: the measurement is still taken at the end
  of wave 2, before wave 3 builds on the raster, and it still sizes
  `LEM_STEP_MAX` — but it decides nothing about whether the XT ships, and no
  live-lemming cap is available as a lever.
- **Wave 6's** "`vm/xt-lemmings` … ONLY IF wave 2's measurement cleared question
  7's threshold". Decision 7: it is created unconditionally.
- The plan's distribution paragraph says on-demand images only; decision 2 adds
  **The Wire** as a `.WPK`, and leaves `apps-all.img` to wave 6's arithmetic.
- **Wave 1's `tools/os88lem.py` feature and the budget's "one bank claim …
  sized once at the largest" line** both predate decision 9. The band writer
  additionally refuses any file over 64,512 bytes and splits a larger bank into
  numbered parts; the level records are grouped eight to a file; and the bank
  claim is still sized at 76,800 while no single read into it exceeds 64,512.

### What the three adversarial reviews changed

Five things, each an arithmetic error in the draft rather than a matter of
taste, and all five are already folded into the paragraphs below:

1. **The image budget was 25% light.** The draft charged resident lines at 3.66
   bytes/line, which is cword's resident bytes over cword's TOTAL (resident +
   overlay) line count. Measured: `wc -l apps/cword/*.c *.h` = 9,102 lines
   producing 35,886 + 18,564 = 54,450 bytes = **5.98 bytes a line**, ~5.7 after
   the runtime this budget counts separately. At the right rate the draft did
   not fit, so **the overlay split moves to wave 1** and the string literals
   move out of the image into a resource band of the converted data.
2. **The frame budget's central constant was off by 16×.** 260,000 is the CYCLE
   count of a 55 ms tick, used as an instruction count. PERFORMANCE.md's own
   anchor is ~16.4 clocks an instruction, so a tick is **~16,000 instructions**
   and the honest figure is ~2-5 fps against 17, not 5-8. The pointer is now
   decoupled from the game frame so input latency stays one tick whatever the
   world costs.
3. **The preview and postview screens are 640x350 in the original** (Lemmix
   `GameScreen.Preview.pas:114`), and the draft drew them in a 320x200 mode
   where 40 characters of a 16x16 font need 640 pixels and the last line sits at
   y = 322. They now **switch mode inside the bracket**, per adapter.
4. **The overlay's refusal path toasted through the kernel in a foreign mode.**
   The module is now forced resident from the Play handler, outside the bracket,
   and the launch refuses in a window if it will not load.
5. **`os88_file_read_at()` is DS-relative** and every destination in this
   program is a claim, so the draft's whole cluster-window paragraph was
   unbuildable. The converter now emits **one 512-padded band file per bank** and
   `os88_file_read_seg()` reads it straight into a claim, which needs no SDK
   addition at all.

## Authority table (surface → the reference file that defines it)

- **Skill panel bitmap (320x40, 4bpp) - the whole bottom bar including the minimap frame and the status strip** — MAIN.DAT section 6 offset 0, per lemmings_3ds/doc/data/lemmings_main_dat_file_format.txt (sections 5 and 9); confirmed by Lemmix src/Game.SkillPanel.pas ReadBitmapFromStyle. Section 2's copy is the PS/2 variant and is dropped - the doc warns the in-level palette is only accurate for section 6
- **Skill panel button order, count and hit boxes** — Lemmix src/Dos.Consts.pas:104-118 (the enum: Slower, Faster, Climber, Umbrella, Explode, Blocker, Builder, Basher, Miner, Digger, Pause, Nuke) and src/Game.SkillPanel.pas:487 SetButtonRects (first button Rect(1,16,15,38), each next +16 in x - VERIFIED at :495-499, `R.Offset(16, 0)` in a loop from Succ(Low) to High). Cross-checked against lemmings_3ds src/control.c:441-500, which agrees cell for cell
- **Skill-count digits on the buttons (two 4-pixel halves OR'd to make two digits; a zero count is a solid block)** — MAIN.DAT section 2 offset 0x1900, twenty 8x8 1bpp glyphs in right/left pairs, per lemmings_main_dat_file_format.txt section 5; drawing rule from lemmings_3ds src/draw.c draw_toolbar and Lemmix Game.SkillPanel.pas DrawSkillCount (:295+)
- **In-game status line: the 40-character template and its five field WRITE offsets** — Lemmix src/Base.Strings.pas:351 SGame_ToolBar_TextTemplate = '..............OUT_.....IN_.....TIME_.-..' for the template, and THE FIVE SETTERS at src/Game.SkillPanel.pas:501-563 for the offsets - NOT the comment block at :250-255, which gives label-inclusive spans (1/14, 15/23, 24/31, 32/40) that are not write offsets. VERIFIED in the source: SetInfoCursorLemming writes fNewDrawStr[i] i=1..14 (PadRight 14); SetInfoLemmingsOut writes [18+i] i=1..5 = 19..23 (PadRight 5); SetInfoLemmingsSaved writes [26+i] i=1..5 = 27..31 (PadRight 5); SetInfoMinutes [35+i] i=1..2 = 36..37 (PadLeft 2); SetInfoSeconds [38+i] i=1..2 = 39..40 (PadLeft 2, '0'). THE DRAFT PLAN SAID 26-30 FOR THE IN FIELD AND THAT IS ONE COLUMN LEFT OF THE TRUTH - column 26 is the template's separator and column 31 is the field's last cell
- **How a status-line character becomes a glyph, and what an unmappable one draws** — Lemmix src/Game.SkillPanel.pas:250-280 DrawNewStr, read in full. THREE RULES the draft missed: (a) both the old and the new character are UpCase'd before the compare, so the whole line is uppercase on the glass whatever case the string is stored in; (b) the index is a four-arm case - '%' -> 0, '0'..'9' -> ord-ord('0')+1, '-' -> 11, 'A'..'Z' -> ord-ord('A')+12, giving 0..37; (c) ANYTHING ELSE, the space and the template's own '.' included, falls to FillRectS(x, y, x+8, y+16, 0) - an 8x16 black cell, not a glyph. Only changed cells are drawn, which is the delta rule this port copies
- **The word shown for the lemming under the cursor, and the ORDER the tests run in** — Lemmix src/Base.Strings.pas for the words, and src/Game.pas:3645-3653 for the order, VERIFIED: IsClimber AND IsFloater -> SAthlete; IsClimber -> SClimber; IsFloater -> SFloater; else LemmingActionStrings[Action]. LEMMIX WINS OVER lemmings_3ds src/lemming.c get_lemming_description here, by this plan's own tie-break rule (Lemmix is the authority on the original's SCREENS and this string is a screen): 3ds returns BUILDER for a shrugging lemming before it tests the abilities, so the two disagree on a climber who has just finished twelve bricks, which is on the glass constantly. THE WORDS ARE STORED UPPERCASE ('WALKER', 'ATHLETE', 'BOMBER') because MAIN.DAT section 6's font has no lowercase glyph and Lemmix's mixed case is Lemmix's presentation, undone by its own UpCase at draw time
- **In-level status font (green 8x16, 38 glyphs) and its character set** — MAIN.DAT section 6 offset 0x1900, 3bpp, order '%' 0-9 '-' A-Z, per lemmings_main_dat_file_format.txt sections 5 and 9 - VERIFIED at lines 206 and 299 of that file, which print the set literally: `% 0 1 2 3 4 5 6 7 8 9 - A B C D E F G H I J K L M N O P Q R S T U V W X Y Z`. Uppercase only, no lowercase, no space glyph. Enumerated identically in Lemmings.ts src/game/resources/skill-panel-sprites.ts
- **Minimap: position, size and the solidity rule** — Lemmix src/Dos.Consts.pas:35-53 (DOS_MINIMAP_WIDTH 104, HEIGHT 20, DosMiniMapCorners (208,18)-(311,37)) for the geometry; lemmings_3ds/doc/data/minimap.txt for the rule (sample rows 16,24,...,152 and 16-pixel groups; 9 of 16 non-zero lights a cell)
- **Lemming animations: 28 registrations with size, bpp, hotspot and frame count, in file order** — Lemmings.ts src/game/resources/lemmings-sprite.ts (the sequential registration list IS the file layout), cross-checked against Lemmix src/Styles.Base.pas TLemmingAnimationSet.InitMetadata and against the offset table in lemmings_main_dat_file_format.txt section 3, which lemtool/lemdat.py:selfcheck() proves is the running sum of frames*w*h*bpp/8 (so no row padding)
- **Terrain-destruction masks (bash x4+4, mine x2+2, explosion) and the 8x8 countdown digits over a lemming's head** — MAIN.DAT section 1, per lemmings_main_dat_file_format.txt section 4; the semantics (a SET bit means leave alone) from Lemmings.ts src/game/resources/mask-provider.ts Mask.at()
- **Mouse cursor: 14x14 cross and its boxed highlight form, hotspot (7,7)** — GEOMETRY ONLY, from Lemmix src/GameScreen.Player.pas (CursorDefault/CursorHighlight, hotspot 7,7). The bitmaps are REDRAWN here: lemmings_3ds src/data/cursor.c is named in that project's LICENCE.txt as third-party and excluded from its public-domain dedication, so it is cited and not copied
- **THE PREVIEW AND POSTVIEW SCREENS' SURFACE - 640x350, which decides which foreign mode each adapter sets** — Lemmix src/GameScreen.Preview.pas:64 InitializeImageSizeAndPosition(640, 350) and :114 `target.SetSize(640, 350); // the original dos-screen size`; src/GameScreen.Postview.pas:55 and :59, the same two calls. VERIFIED IN THE SOURCE, and it is the fact the draft plan was missing: 40 characters of the 16x16 purple font is exactly 640 pixels wide (which is WHY the original screen is 640 wide), the preview's last line sits at y = 82 + 15*16 = 322, and the postview forces its footer ~19 lines down from y=16. None of that fits a 320x200 mode in either axis. So the bracket SWITCHES MODE for these two screens, per adapter, and the choice is pinned in api_gaps and in wave 5
- **Level preview screen: the seven lines, their wording, their indent and their per-line colours** — Lemmix src/GameScreen.Preview.pas GetScreenLinesAndColors (16 lines 16px apart from y=82, ten-space indent on the value rows, line 15 centred) and src/Base.Strings.pas:411-418 for the format strings ('Level %s %s', 'Number of Lemmings %s', '%s To Be Saved', 'Release Rate %s', 'Time %s Minutes', 'Rating %s', 'Style %s', 'Press mouse button to continue'). lemmings_3ds has no preview screen at all
- **Postview (results) screen: layout, the nine result texts, the header and footer lines, and how the footer is positioned** — Lemmix src/GameScreen.Postview.pas GetScreenText (all lines centred, first at y=16, 16px pitch) and src/Base.Strings.pas:423-460 (SPostviewScreen_Result0..8 verbatim, YourTimeIsUp, AllLemmingsAccountedFor, YouRescued_s, 'You needed  %s' with its two spaces, the mouse-button footers). THE FOOTER IS FORCE-POSITIONED: :189 `AddLineFeed(18 - Result.CountChar(CR))` pads to a fixed row whatever came above, so dropping the access-code lines does NOT move it. What the port must do instead is take the FAILURE branch's AddLineFeed(5) on both branches (:178-185 emits the code line plus AddLineFeed(3) on success, AddLineFeed(5) otherwise), so the two branches are identical once the code line is gone
- **Result tier selection (which of the nine texts a percentage earns)** — Lemmix src/GameScreen.Postview.pas GetResultText - Done=100 -> 8; Done=0 -> 0; <Target div 2 -> 1; <Target-5 -> 2; <Target-1 -> 3; =Target-1 -> 4; =Target -> 5; <Target+20 -> 6; else 7. Lemmix wins over lemmings_3ds src/ingame.c show_result, whose thresholds differ, because Lemmix is the authority on the original's screens
- **Mayhem 30 congratulation** — Lemmix src/Base.Strings.pas SPostviewScreen_CongratulationOrig ('Congratulations!' ... 'Everybody here at DMA Design salutes you' ... 'Now hold your breath for the data disk'). Note the middle line is exactly 40 characters = 640 pixels of the purple font, which is the second confirmation of the 640x350 surface
- **Rating names and the 120-level play order (which file, which section, whether ODDTABLE overrides)** — Lemmix src/Styles.Dos.pas TDosOrigLevelSystem SectionTable (4x30 bytes, decoded Dec(B); IsOddTable := Odd(B); B := B div 2; file = B div 8; section = B mod 8) as the authority, verified entry-for-entry against lemmings_3ds src/import/gamespecific.c:172 position_of_classic_level[] and Lemmings.ts public/data/config.json level.order in lemtool/lemdat.py:selfcheck() - all three agree on all 120
- **Level names and per-level header (release rate, lemming count, save count, time, eight skill counts, start x, graphic set)** — The 2048-byte record at the offsets in lemmings_3ds/doc/data/lemmings_lvl_file_format.txt (rt), with the bit unpacking taken from Lemmix src/Level.Loader.pas TranslateLevel; ODDTABLE.DAT overwrites the first 24 bytes and the 32-byte name at 0x07E0 (lemmings_3ds src/import/import_level.c:429-431, indexed file*8+section)
- **Keyboard map in the play screen** — Lemmix src/GameScreen.Player.pas:519-531 (F1 slower, F2 faster, F3 climber, F4 umbrella, F5 exploder, F6 blocker, F7 builder, F8 basher, F9 miner, F10 digger, F11/Pause pause, F12 nuke), :465 (Esc finishes the level) and :1290-1291 (Ctrl+F1 minimum release rate, Ctrl+F2 maximum). lemmings_3ds has no keyboard at all. THE PLATFORM BENDS TWO OF THESE and SPEC.md 11.2.1 adds one: 'f' and 'F' are bound as the fullscreen door in both directions (binding, VERIFIED at SPEC.md:12161 - 'An app that reserves letters for gameplay is not an exception'), Esc keeps the original's cancel-first meaning (finish the level while one is running, leave the bracket from the postview where there is nothing left to finish), and F11/F12/Pause are unreachable on the target machine - see the greyed list
- **Preview/postview background and the 16x16 purple font they are lettered in** — MAIN.DAT section 3 offset 0 (320x104 2bpp brown tile, Lemmix src/Dos.MainDat.pas ExtractBrownBackGround) and section 4 offset 0x69B0 (94 glyphs, ASCII 0x21-0x7E, 16x16 3bpp, Lemmix src/GameScreen.Base.pas ExtractPurpleFont / DrawPurpleText). 94 glyphs INCLUDES lowercase, unlike the status font, which is why the preview lines keep their mixed case and the status line does not
- **Rating signs (72x27) shown beside the rating in the launcher** — MAIN.DAT section 4 at 0x5A80 Mayhem, 0x5E4C Taxing, 0x6218 Tricky, 0x65E4 Fun - REVERSE of the name order, per lemmings_main_dat_file_format.txt section 7 and Lemmix src/GameScreen.Menu.pas PaintCurrentSection
- **In-level 16-colour palette (0-7 the lemmings' and the panel's, 8-15 the level's)** — Lemmings.ts src/game/resources/lemmings/color-palette.ts setMainColors for 0-7 and the note that 8-15 come from the style; the style half from GROUNDxO.DAT's VGA custom palette (lemmings_3ds src/import/import_ground.c, with entry 7 set to a copy of entry 8). This is what makes plane 3 mean 'terrain' and planes 0-2 'sprite', which the VGA raster leans on
- **Frame clock, the opening sequence and the spawn interval** — Lemmix src/Game.pas IncrementIteration:3498 (17 frames to the second; iteration 15 'let's go', 34 entrance sound, 35 entrances open, 55 music) and CheckSpawnLemming:4001 (first countdown 20, interval (99-RR) div 2 + 4, spawn at entrance.Left+24, Top+14); agrees with lemmings_3ds src/ingame.c level_step
- **Cursor hit test, skill priority and the right-click behaviour** — Lemmix src/Game.pas PrioritizedHitTest:3595 - a 13x13 box hung off the animation's foot offsets, prioritised actions Blocking/Building/Shrugging/Bashing/Mining/Digging/Ohnoing, last prioritised wins unless the right button is held. Carried as the original's behaviour, not switchable
- **Interactive-object metadata: trigger effect ids, trigger rectangle at x4 resolution, frame counts, trap sound ids** — lemmings_3ds/doc/data/lemmings_vgagrx_dat_groundxo_dat_file_format.txt (28-byte OBJECT_INFO; left*4, top*4-4, w/h*4 with a stored 0 meaning 256; effect 0 none, 1 exit, 4 trap, 5 drown, 6 disintegrate, 7 one-way left, 8 one-way right, 9 steel), read by lemmings_3ds src/import/import_ground.c
- **World size, the terrain-list terminator and the steel bit layout** — lemtool/REPORT.md sections 4 and 'Where the readers disagree', measured over all 120 levels: the world is 1584x160 (C and Lemmix; TS's 1600 is wrong); a terrain slot is SKIPPED not BROKEN at 0xFFFF (TS and Lemmix; C's break renders Taxing 27 'Call in the bomb squad' with 68 pieces instead of 395); steel x is the high 9 bits with the width in the high nibble (C and Lemmix and the LVL doc; TS is wrong)
- **Sound EVENTS (let's go, oh no, explosion, door open, yippee, splat, exit, trap)** — Lemmix src/Dos.Consts.pas TSoundEffect and src/Meta.Structures.pas ose_* trap ids for the event list and where each fires. THE TONES THEMSELVES ARE THIS PORT'S OWN: the original's are FM patches inside ADLIB.DAT, which is not in the 26-file set at all. That fact is stated in SPEC.md, in README.TXT and beside the greyed Music row - NOT in the About box, which per LESSONS.md 8 carries the product, the version, what this port is and the attribution, and nothing about how the build renders or what it synthesises
- **About box attribution and the original credits** — Lemmix src/Base.Strings.pas SCredits 'Original credits...' block (Lemmings By DMA Design / Programming By Russell Kay / Animation By Gary Timmons / Graphics By Scott Johnston / Music By Brian Johnston & Tim Wright / PC Music By Tony Williams / Copyright 1991 Psygnosis Ltd.) - EIGHT lines. Against LESSONS.md 8's twelve-row ceiling (a 640x200 number; a 19-row About box put its OK button on the DESKTOP on CGA and Hercules) that block plus product, version and a port line is already twelve, so the FULL attribution list lives in README.TXT beside the package and in every file header carrying derived material, and the About card names the two principals and points at the README
- **Kernel-side surfaces the port must not guess at** — Read from this tree, not from memory: kernel/fsx.inc:20-28 (the nine FSXM ids), :117 fsx_capstab (VGA 0x01EF, HERC 0x0011, CGA 0x000F, EGA 0x000F - and the EGA row's own comment, 'mode 0Dh (native, planar) is a safe later addition, not pass 1'); kernel/kernel.asm:38-39 (MBAR_H 20, TITLE_H 18); kernel/dock.inc:57 (DOCK_H 24); kernel/memory.inc:34/45 (MEM_MAX 20 on kern_small, 32 on kern_big); tools/os88disk.py:124 GEOMETRY (spc 2 on BOTH 360KB and 720KB, so every file on those two rounds up to 1,024 bytes; 354 and 713 data clusters); SPEC.md 53.1 (the file API is legal mid-bracket, and every sound grant inside the bracket is billed to the instance - so the draft's doubt about os88_snd_tone is answered YES by the contract); SPEC.md 11.2.1 (f/F binding, Esc as the escape hatch that keeps its cancel meaning first)

## Scope

### Ships

- All 120 original levels in the four ratings Fun / Tricky / Taxing / Mayhem, in the original's order, including the 40 that take their header and name from ODDTABLE.DAT - on the 1.44MB and 1.2MB disks; a stated subset on 720KB and 360KB (question 3)
- All five graphic sets (dirt, fire, squasher, pillar, crystal): 273 terrain pieces and 54 animated objects with their trigger rectangles and effects
- The four VGASPEC special-graphics levels (A Beast of a level, MENACING !!, What an AWESOME level, A BeastII of a level) - 1.44MB and 1.2MB disks
- Full colour on VGA (mode 0Dh, the original's own 16-colour in-level palette), 4 colours on CGA and EGA, and monochrome on Hercules by colour CLASS (solid terrain / dithered terrain / black), so terrain, lemmings, objects and the cursor stay distinguishable (SPEC.md 39.4, the Pac-Man precedent at SPEC.md 89.2)
- The eight assignable skills and the whole 18-action lemming state machine: walk, jump, fall, float, climb, hoist, dig, bash, mine, build, block, shrug, splat, drown, exit, fry, ohno, explode
- Terrain destruction by mask, the builder's bricks, the digger's rows, the blocker's field and its restore, and the 4x4-resolution trigger map with exits, traps, water, fire and one-way walls
- The DOS skill panel: twelve buttons with the original's hit boxes, two-digit skill counts, the selection frame, the 40-character green status line delta-drawn at the FIVE SETTERS' write offsets (1-14 / 19-23 / 27-31 / 36-37 / 39-40), the minimap with its sampled solidity and its view rectangle, and the pause and nuke behaviour - both reachable by their panel buttons, which is the original's own primary route
- The original's keyboard map where the machine has the keys (F1-F10, Esc, Ctrl+F1/F2), plus SPEC.md 11.2.1's f/F fullscreen door, and its mouse model, with the app drawing its own 14x14 cursor in its two forms - REPAINTED EVERY FSXW_TICK whether or not a game frame completed, so pointer latency is one tick even when the world is running at 300 ms a frame
- The level preview screen and the postview results screen with the nine original result texts and the Mayhem 30 congratulation, drawn in MAIN.DAT's own purple font over its own brown background, inside the bracket, in a SECOND foreign mode chosen per adapter to hold the original's 640x350 layout
- A desktop launcher window: rating chooser, the level names PAGED to the live window height, the preview fields beside them, Play, four state rows (Mode / Music / Level Code / Save Progress) that give SPEC.md 47's greying somewhere to live, and the standard About card
- PC-speaker tones for the original's events through os88_snd_tone - legal inside the bracket by SPEC.md 53.1's own words ('every sound grant the app takes inside the bracket is billed to its instance')
- Per-rating progress saved in SYSTEM/APPDATA/LEMMINGS.SAV
- A README.TXT beside the package naming the fetch, the full attribution list, the synthesised tones and what this build does and does not carry

### Present and greyed (SPEC.md §47 — the fact that greys it)

- **Music (a Music row in the launcher and a line in README.TXT)** — ADLIB.DAT is a compiled x86 sound driver plus its data, not a score - the game installs an interrupt vector at its offset 0 and calls into it - and there is no FM score path in this OS. It is also not in the 26-file data set this port fetches at all. The event tones this build plays are this port's own.
- **Level Code... (a Level Code row in the launcher)** — The original's access-code algorithm lives in the game's executable, not in any of its data files. Lemmix's ten-letter code is Lemmix's own (an MD5 of the 2048-byte record) and is not the code a 1991 player knows. Progress is kept in SYSTEM/APPDATA/LEMMINGS.SAV instead. The postview's two access-code lines are omitted with it, and both branches take the failure branch's AddLineFeed(5) so the force-positioned footer is unaffected.
- **The 320x200x16 mode (a Mode row in the launcher naming the adapter and the mode in use)** — kernel/fsx.inc:117 fsx_capstab gives an EGA 0x000F - the CGA-compatible modes only - so mode 0Dh has no FSXM id on that row and a sixteen-colour card gets four colours. fsx_caps is asked with OUR window and the same bit that greys the row is the one fsx_mode would refuse on (SPEC.md 47 rule 4). Adding FSXM_VGA0D to the EGA row is a KERNEL change and is outside this port's scope.
- **F11 (pause), F12 (nuke) and the Pause key (named on the Mode row's second line, beside the keyboard the machine reports)** — An 83-key XT keyboard - vm/xt-lemmings, the machine this port is about - has no F11 or F12 key, and its Pause is Ctrl-NumLock, which the BIOS spins on internally and never returns. This build reads keys with int 16h AH=11h/10h where an enhanced BIOS answers and falls back to AH=01/00 where it does not, so F11 and F12 work on an AT-class machine and are absent on an XT. Pause and Nuke are the panel's own buttons 11 and 12 on every machine.
- **The original's preview and postview LAYOUT on CGA and EGA** — The original's preview and postview screens are 640x350 (Lemmix GameScreen.Preview.pas:114). VGA sets FSXM_VGA12 (640x480) and Hercules FSXM_HERC (720x348) and both hold that layout; a CGA and an EGA top out at FSXM_CGA640, 640x200, which holds the 40-character lines but 12 of the ~22 rows - so the blank-line spacing is compressed out and the content lines are drawn at the original's 16px pitch.
- **A level row in the chooser whose graphic set or special picture is not on this disk** — Names the missing file and the arithmetic in CLUSTERS as well as bytes, e.g. 'LEMSPC2.LEM is not on a 720KB disk: the four special pictures are 307,200 bytes = 300 clusters of this disk's 713, with 517 already spent.'
- **Saving progress on a read-only medium** — The live CD cannot be written (SPEC.md 80.3); the session is played and the result is shown, and nothing is recorded.
- **Sound on a machine whose speaker path refuses** — os88_snd_caps() answered no tone capability; the same predicate greys the row and is what os88_snd_tone would refuse on.

### Absent

- The original's MAIN MENU screen - the 632x94 logo, the six 120x61 F-key signs, the blinking eyes, the two working-lemming scrollers and the credits reel. 98,048 bytes of MAIN.DAT sections 3 and 4 for a screen os8088 already provides: the launcher window IS the menu (SPEC.md 11, 12). The brown background tile, the purple font and the four rating signs ARE carried, because the preview and postview screens are drawn in them.
- The explosion's 80-particle spray. MOVED HERE FROM 'greyed' ON REVIEW: there is no control a player could click, so SPEC.md 47's present-and-greyed has nothing to attach to, and a fact with no surface belongs in README.TXT and SPEC. The 51 x 80 signed-byte trajectory table lives in the DOS executable, not in the 26 data files this port fetches; Lemmix ships it as its own extracted resource and lemmings_3ds's copy is named in that project's LICENCE.txt as third-party. The explosion ANIMATION and the terrain crater - what the player is actually reading - are both carried. Pending question 5.
- Two-player over local wireless - 3,730 lines of lemmings_3ds and a 3DS radio; nothing in the original DOS release.
- The 3DS port's settings screen and its per-glitch toggles. The glitches the original DOS release actually has (ABBA entrance order, the right-click priority glitch) are carried as behaviour, unswitchable, because they are the original's behaviour and levels are solved with them.
- Lemmix's replay recorder, hyperspeed rewind and save states - a Lemmix addition, not the 1991 game. Lemmix's own 'F' (skip one minute) goes with them, which is what leaves the letter free for SPEC.md 11.2.1's fullscreen door.
- Oh No! More Lemmings (five more ratings, 100 levels, four more graphic sets). Pending question 1: the data is beside the original set in the scratchpad and the converter is written so it can be added, but nothing in this plan builds it and no implementer should until the question is answered.
- Custom / Lemmini level loading, the level finder, and any level editor.

> Decisions 1 and 5 settle the two "pending question" entries above: Oh No!
> More Lemmings is out of this port, and the particle spray is conditional
> wave-6 polish at a stated size threshold.

## Files

| file | holds | resident |
|---|---|---|
| `apps/lemmings/lemmings.c` | The translation unit's root: prototypes for everything (clang in the host harness is stricter than SmallerC about them), os88_main / os88_paint / os88_onkey / os88_onclick / os88_onmouseup / os88_oncmd / os88_about / os88_onwake, the empty menu set whose AM_NAME is 'Lemmings' (apps/word's idiom - COUNT 0, so it names the kernel bar and carries no items; every greyed item lives on a launcher ROW instead), the claim table and its sizing-and-refusal arithmetic INCLUDING a count against MEM_MAX (32 on kern_big, 20 on kern_small - kernel/memory.inc:34/45 - which nothing in the draft counted), the Play handler that forces the overlay resident BEFORE fsx_run, the fsx entry os88_fsx_main() and the session state machine (preview -> play -> postview -> next/retry/menu), and the #includes of the files below in dependency order | yes |
| `apps/lemmings/lemtab.c` | Const tables only, nothing executable: the 4x30 level order with its odd-table flags, the four rating names, the 28-entry animation metadata (frames, w, h, bpp, hotspot, loop), the 16-entry float table, the action dispatch indices and their 18 draw-offset pairs, the trigger-effect ids, the twelve button hit boxes, the FIVE status-line write offsets with their pad rules, the 38-entry status-font index map (with the 'anything else is an 8x16 black fill' arm), the colour-class map per adapter, and the nine result-tier thresholds. A struct table indexed, never copied (LESSONS.md section 4) | yes |
| `apps/lemmings/lemgame.c` | The per-tick step: the 55-tick opening, entrance opening and the ABBA rotation, spawning on the (99-RR)/2+4 countdown, release-rate change with its hold-repeat, the nuke's one-lemming-a-tick timer walk, the game clock at 17 frames a second, object animation advance, the end-of-level verdict, AND the mechanics budget: a round-robin cursor so a frame steps at most LEM_STEP_MAX lemmings and the rest wait one frame, which is what turns the frame-budget risk into a knob rather than a rescue | yes |
| `apps/lemmings/lemact.c` | The eighteen action handlers and their constants (walk probes 8 up and 4 down; fall splats above 60; bash acts on frame mod 16 with the 8-ahead 6-up exit test; mine's mod-24 fall-through; dig's every-eighth-frame row; build's six bricks at frame 9 and the twelve-brick shrug; climb's mod 8 hoist), Transition/TurnAround, the eight skill assignments with their refusals, and the cursor's prioritised picker with Lemmix's climber/floater-first description order (Game.pas:3645-3653). The hottest C in the program and the largest file; every terrain probe it makes is one near call into lemmask.inc, and the probe COUNT per lemming per tick is a number the cost table prints, because at 16,000 instructions a tick it is the budget | yes |
| `apps/lemmings/lemobj.c` | The object model: the 32 instances, their trigger rectangles painted into a per-32-pixel-column bucket index (the reference walks every trigger for every lemming every tick - ~2,000 rectangle tests a tick, which at the CORRECTED instruction budget is the whole frame twice over), the effects (exit, drown, fire, triggered trap, one-way left/right, steel), the blocker's two fields and its 3x3 save-under, and the sound event each raises | yes |
| `apps/lemmings/lemdraw.c` | The frame: the visible-sprite list and its save-under directory, the restore-in-reverse then draw order, the panel's delta-draw (only the status-line cells and skill digits that changed, the selection frame when it moves), the minimap's changed cells against its own 260-byte shadow, the view rectangle, the cursor's own save-under AND the tick-paced cursor repaint that runs whether or not a game frame completed, and the per-frame cost counters the host harness reads. It decides; lemblit.inc touches the pixels | yes |
| `apps/lemmings/lemload.c` | RESIDENT HALF ONLY (the composition driver moved to lemovl.c in wave 1): the manifest reader, the level name table, the decoded 2048-byte header, claim acquisition with the refusal text and its arithmetic, and the band reader. THE BAND READER IS os88_file_read_seg(), NOT os88_file_read_at(): every destination here is a heap claim and os88_file_read_at takes a DS-relative `void *buf` (apps/cc/os88.h:833), so the draft's cluster-window design could not have been built. The converter emits one 512-padded file per band and the whole SPEC.md 20.12.2 paragraph disappears; os88_file_read_at survives only for reading a manifest header into a DS-relative static | yes |
| `apps/lemmings/lemui.c` | RESIDENT HALF ONLY: the launcher's hit testing, the rating tabs, the level list PAGED from os88_wm_geom()'s live content height (never from 30 - LESSONS.md 8's Font-list defect, and on CGA the content box is 200 - 20 - 24 - 18 - 1 = 137 px, seventeen 8px rows), the four state rows, the cell shadow sized from that same paged row count with a compile-time cap, and the damage-only repaint. The rating-sign BITMAPS are not modelled by a character shadow and carry an explicit invalidate flag | yes |
| `apps/lemmings/lemtext.c` | The string ACCESSOR and its formatting, not the strings: a lem_str(id) that reads a 512-padded RESOURCE BAND of the converted data file into a small bss scratch. The literals themselves - the preview's eight formats, the postview's header, nine result texts and footers, the congratulation, the status template, the cursor-lemming words (stored UPPERCASE, because MAIN.DAT section 6's font has no lowercase glyph and Lemmix's UpCase at draw time is what hides that), the rating names and every greying fact - are written by tools/os88lem.py from a source table that carries the reference file for each in a comment. This is the change that took ~2,000 bytes of literals out of the image, and it is the reason the corrected budget closes | yes |
| `apps/lemmings/lemovl.c` | Everything ovl_*, and it is much larger than the draft planned because the corrected budget forces the split into wave 1: ovl_preview() and ovl_postview() (the two original screens, in the SECOND foreign mode chosen per adapter), ovl_about() (the About card - product, version, one line of what this port is, and the two principals, TWELVE ROWS, with the full attribution in README.TXT), ovl_greyfact() (the dialog behind a greyed row), ovl_progress_read()/ovl_progress_write(), ovl_compose() (the terrain-list walk and its four flag bits - once per level, never during play), ovl_chrome() (the launcher's non-hit-test drawing) and ovl_fmt() (the preview-field and status formatting). Split by FREQUENCY: nothing here runs during play. THE MODULE IS FORCED RESIDENT FROM THE PLAY HANDLER, outside fsx_run - cc_ovneed's refusal toasts through the kernel, and a kernel toast in a foreign mode writes desktop-geometry pixels into the game's framebuffer | no |
| `apps/lemmings/lemblit.inc` | Hand-written assembly, three backends behind five entries (lem_r_setup, lem_r_mode, lem_r_scroll, lem_r_sprite, lem_r_present). VGA: the CRTC offset/start-address/line-compare/pel-panning programming, write-mode-1 latch save-under and restore, write-mode-2 per-colour sprite draw with a runtime shift into the byte phase, and the terrain writes destruction makes. CGA/HERC: Tank's shadow plus per-row dirty spans (SPEC.md 85.3.1), one stride of 80 bytes on both, blitting the union of two span sets. lem_r_mode is the mid-bracket mode change the 640x350 screens need. C calls each once per sprite or once per frame, never per pixel (SPEC.md 73.11) | yes |
| `apps/lemmings/lemmask.inc` | Hand-written assembly over the 1bpp solid mask claim: lem_has_pixel(x,y), lem_clear_pixel, lem_set_pixel, lem_apply_mask (a 16x10..16x22 stencil), lem_dig_row, and the terrain composer's per-piece inner loop. lem_has_pixel is the hottest call in the game - 4 to 8 times per lemming per tick - and at ~16,000 instructions a tick the CALL OVERHEAD is the thing to price: a SmallerC cdecl call plus prologue and epilogue is 20-30 instructions, so a batched probe entry taking four offsets at once is the first optimisation the bench will justify | yes |
| `apps/lemmings/lemfont.inc` | Hand-written assembly for text in a foreign mode (SPEC.md 85.7's precedent): the 8x16 green status glyphs (38 of them, with the 'anything else fills an 8x16 black cell' arm the reference has), and the 16x16 purple glyphs, each composed into a band and written once. No kernel font slot is legal inside the bracket | yes |
| `apps/lemmings/lemmings.asm` | The shim: CC_PKG_NAME 'LEMMINGS', CC_HAS_ONKEY / ONCLICK / ONMOUSEUP / ONWAKE / MENUS / ABOUT / FSX, CC_HAS_OVL, CC_STACK_CLASS, CC_ICON, then %include cc/crt0.asm, %include lemmings.gen.asm, the three %included .inc files, CC_IMAGE_END | yes |
| `apps/lemmings/icon.inc` | The 16x16 1bpp package icon, drawn for this port (a walking lemming silhouette). No association block: this package has no user-openable document type | yes |
| `apps/lemmings/hosttest/os88.h` | The stub API, ahead of apps/cc on the include path: a model of the glass (character per cell, overstrike ink, line ink), a model of the fsx surface (a byte-per-pixel frame with a call counter and a written-twice counter) that MODELS the raster rather than refusing - LESSONS.md 7's 'a stub that always refuses measures the fallback path' - one named stub per assembly entry (lem_r_setup/mode/scroll/sprite/present, lem_has_pixel/clear/set, lem_apply_mask, lem_dig_row), stub file reads off the converter's real output, and stub claims that count against a modelled MEM_MAX. It is a second copy of an interface and it WILL drift - and when it does the harness fails to COMPILE, which is the failure you want | no |
| `apps/lemmings/hosttest/lemtest.c` | The harness: #includes the whole program against that stub, drives the launcher like a user and rebuilds what the list ought to show from an independently written PAGED layout after every keystroke (at three modelled window heights, 137 px among them), asserts the 40-cell status string cell by cell against an independent rebuild so a one-column field drift fails the host build, asserts every character of every status string maps into the 38-glyph set, runs the mechanics HEADLESS for N ticks on real levels and diffs lemming positions and states against a recorded trace, audits the shadow against the modelled glass, asserts no pixel is written twice in a frame, and prints the cost table on every build | no |
| `apps/lemmings/hosttest/fixture/` | A tiny SYNTHETIC data set in the converted format - two levels, one style, the resource band - written by tools/os88lem.py --fixture and COMMITTED, carrying no bytes derived from the original. It is what lets apps/lemmings/build.sh run the harness with no network fetch, which is what makes question 2's 'make lemmings needs no fetch' true and keeps the real-data reader out of the 30-second fast tier | no |
| `apps/lemmings/build.sh` | The host gate, apps/runcpm's pattern: four checks, every one stops the build and a failure leaves no stamp so the 8086 compile never runs. (1) the converter's --selfcheck, (2) hosttest/lemtest.c against the committed fixture, (3) the raw-x86 harness for lemmask.inc and lemblit.inc under SS != DS, (4) the cost table printed and compared against a recorded ceiling | no |
| `tools/getlemmings.py` | The fetcher, in tools/getstories.py's shape: one pinned URL (Lemmix at commit 40e9bc34451f0e9127fd53b290a3877e700d143d, src/Data/Styles/Orig/orig.zip), one SHA-256 (bf1f2dbd11cd20df9f748047f8c4a32e7033d3ac7bb4b77058c3ecf27c0a9d6d, 336,839 bytes), unpacked into build/lemdata/ behind $(BUILD)/lemdata.stamp. Nothing is committed, and the stamp gates the DISK target only, never `make lemmings` | no |
| `tools/os88lem.py` | The converter: the DAT container walk and its backward bit-stream decompressor, the 2048-byte record reader with Lemmix's bit unpacking, GROUNDxO, VGAGRx, VGASPECx's second-level RLE, MAIN.DAT's seven sections, the ODDTABLE overlay, the 120-level order - then the machine-native writer, whose OUTPUT SHAPE IS ONE 512-PADDED BAND FILE PER BANK (one per style, one per rating, one resource band of strings, one per special picture) so os88_file_read_seg reads a band straight into a claim and cluster slack is bounded by the number of BANDS rather than by the number of levels. The per-geometry manifest prints its arithmetic in CLUSTERS as well as bytes, because 360KB and 720KB both have spc = 2 (tools/os88disk.py:124) and 120 level files would have cost 120 clusters for 84,000 bytes. --fixture writes the committed synthetic set. Deterministic: the same input rebuilds the same bytes. DECISION 9 bounds the output: no file over 64,512 bytes (a larger bank becomes numbered 512-padded parts), level records grouped eight to a file to keep the folder under ~90 entries, uppercase 8.3 names, and the archive total printed against 1,048,575 - SPEC.md §92.3.2 | no |
| `tests/unit/t_lemdat.py` | The independent second reader, written from Lemmings.ts's semantics rather than from os88lem.py: 101 sections' XOR checksums and lengths, the mask = plane 3 identity over all 273 terrain pieces, the four VGASPEC pictures at exactly 4 chunks of 14,400, the 120-level order against BOTH reference tables, and every converted band's header against what the converter claims. Registered in tests/suite.py's FAST tier with its MEASURED seconds and a SKIP when $(BUILD)/lemdata.stamp is absent (the `lmpack` row's precedent) - the fast tier's 30-second wall clock is enforced and this row must never depend on a network fetch to pass | no |
| `tests/cfsx/cfsx.c` | The capability gate for the new C fsx surface, tests/covl's shape: puts NUMBERS on the glass - the caps mask, the FSI block's seven fields, a 16-colour ramp in mode 0Dh, the panel drawn below a line compare while the top half scrolls by pel panning, a MID-BRACKET MODE CHANGE from 0Dh to 12h and back (which the 640x350 screens need and nothing in this tree has ever done), an ovl_* called from INSIDE the bracket, the same call with the .OVL deleted, and a counter only the bracket can bump. Built before the port needs the mechanism, not after | no |

## Budget

| | bytes |
|---|---:|
| resident image (estimate) | 36,000 |
| resident bss (estimate) | 14,000 |
| `LEMMINGS.OVL` (estimate) | 14,000 |
| **resident total against `APP_MAX_SIZE` 61,440** | **50,000** |

### The basis

RE-DERIVED AT CWORD'S MEASURED RATE, because the draft's 3.66 bytes/line was cword's RESIDENT bytes over cword's TOTAL (resident + overlay) line count, then applied to resident lines only while the overlay's lines were charged a second, higher rate on top. MEASURED HERE: `wc -l apps/cword/*.c apps/cword/*.h` = 9,102 lines producing 35,886 resident + 18,564 overlay = 54,450 bytes of compiled output = 5.98 bytes a line whole-program, or ~5.7 after subtracting the ~3,000 of crt0, thunks and shared .inc that this budget counts separately. The draft's blended rate was 4.47 and at 5.7 the draft did not fit, which is why the split moves to wave 1.

IMAGE, at 5.7 bytes a line of resident C after the wave-1 split: lemmings.c 900, lemtab.c 600, lemgame.c 500, lemact.c 1,600, lemobj.c 500, lemdraw.c 700, lemload.c's resident half 250 = 5,050 lines -> 28,800; plus the three .inc files at ~1,400 lines of nasm at ~2.7 a line -> 3,800; plus crt0 and the thunks the package names (the six FSX thunks are %ifdef CC_HAS_FSX, so no other C package pays for them) -> ~3,000. IMAGE ~35,600, called 36,000.

OVERLAY: lemovl.c's own 1,100 lines plus what wave 1 moves out - ovl_compose 350, ovl_chrome 400, ovl_fmt 200 = 2,050 lines -> ~11,700, called 14,000 with slack. The draft's 8.8 bytes/line for an overlay is not used: it was derived from the same mis-split ratio.

BSS, buffers listed with sizes: lemming pool 100 x 20 = 2,000; object instances 32 x 20 = 640; terrain entry list 400 x 5 = 2,000; steel list 32 x 6 = 192; decoded level header + name 128; one rating's name table 30 x 33 = 990; launcher glass shadow now sized from the LIVE window height with a compile-time cap of 46 cols x 17 rows x 2 = 1,564 (the draft's fixed 22 rows was 2,024 and was LESSONS.md 8's Font-list defect frozen into a constant - a CGA content box is 200 - MBAR_H 20 - DOCK_H 24 - TITLE_H 18 - 1 = 137 px, seventeen 8px rows); status-line and digit shadow 64; MINIMAP SHADOW 104 x 20 bits = 260 (absent from the draft, and delta-drawing needs it); sprite save-under directory 100 x 8 = 800; dirty-span arrays 2 sets x 200 rows x 2 = 800; trigger column buckets 100 x 2 + a 64-entry pool x 10 = 840; the string staging scratch the resource band reads into 700; the claim-record table and manifest 300; the FSI block, name scratch and per-frame counters ~1,200 = 12,478, called 14,000 with slack.

TOTAL RESIDENT 50,000 of 61,440 = 81%, 11,440 spare - and LESSONS.md 5 says to expect the first full build to overshoot (cword's did, by 2,514, fixed by halving one buffer). The os88pkg size line goes in the WAVE 1 done_when, not wave 6, and 55,000 is the trigger for the next move-out (lemdraw.c's panel composition and lemtab.c's colour-class maps).

CLAIM RECORDS ARE A BUDGET TOO, and nothing in the draft counted them: kernel/memory.inc gives MEM_MAX = 32 on kern_big and 20 on kern_small, system-wide, shared with every Disk window and driver already open. This program's claim list is merged down to FOUR: the solid mask 32,000 (1600x160 bits, all adapters); ONE bank claim reused in sequence for the terrain piece bank (up to 42,200), the object bank (up to 53,580) and then the derived sprite bank (~48,000 on VGA) or a VGASPEC picture (76,800), sized once at the largest - and DECISION 9 bounds the READ rather than the claim, so that 76,800-byte picture arrives as numbered parts of at most 64,512 bytes read at successive 512-aligned offsets of this one claim (SPEC.md §92.3.2); the adapter terrain copy (CGA 2bpp 63,360, Hercules 1bpp 31,680, VGA none - it lives in VRAM); and the 80-byte-stride screen shadow 16,000 on the two 1bpp adapters. Four records plus the package's own region plus the overlay module's is six, which fits 20 with room and is stated in the refusal text. Worst case ~255KB on CGA, comfortably inside the 532KB a 640KB machine has free after boot (docs/KERNEL-MEMORY.md), and refused with that arithmetic on the glass on a 256KB machine (WEAVE-SPEC 1.4's precedent).

## API gaps, and what each one costs

**Need.** Enter the SPEC.md 53 exclusive bracket from C - the whole game lives inside it

- *Slot:* OSAPI_FSX_RUN (0x02C8) exists; no C thunk, and os88.h's header comment names OSAPI_FSX_* as deliberately unwrapped
- *Action:* Add a thunk to os88thunk.asm + os88.h, GATED %ifdef CC_HAS_FSX. NOT a raw C function pointer: add %define CC_HAS_FSX and a crt0.asm trampoline cc_fsxentry that near-calls a declared callback void os88_fsx_main(void *win), exactly as every other callback is declared, so the shim and the C cannot drift. The thunk is int os88_fsx_run(void *win, int flags) with AX = cc_fsxentry, BX = win, CX = flags, CF -> 0/-1. Stack it on os88_fullscreen(win,1) the way apps/missile/missile.asm:1186-1227 does. THE GATE IS NOT OPTIONAL: crt0.asm %includes os88thunk.asm WHOLE and only ~13 of its 1,968 lines are behind %ifdef, so six ungated thunks land in the image of every existing C package - cword ships with 1,043 bytes spare. CC_HAS_FDLG and CC_HAS_PARTS are the precedent

---

**Need.** Ask which foreign modes this window's display can set, so the mode choice and its greying are one predicate (SPEC.md 47 rule 4)

- *Slot:* OSAPI_FSX_CAPS (0x02C0), no C thunk
- *Action:* Add int os88_fsx_caps(void *win, unsigned char *kind) behind CC_HAS_FSX - AX is the bitmask, DL the VID_* kind through the out-parameter (a static, per rule 1). Pass OUR window, and ask again where the answer is used: kernel/fsx.inc's own comment records Missile Command reading HERC's caps because it asked before its window existed

---

**Need.** Set mode 0Dh / mode 4 / Hercules graphics for the game raster AND CHANGE MODE MID-BRACKET for the 640x350 preview and postview screens, reading back the framebuffer segment, stride, banks and pages each time

- *Slot:* OSAPI_FSX_MODE (0x02D0), no C thunk
- *Action:* Add int os88_fsx_mode(int id, struct os88_fsi *fsi) behind CC_HAS_FSX, with ES = DS set inside the thunk, plus struct os88_fsi mirroring FSI_SEG/W/H/STRIDE/FLAGS/BPP/BANKS/PAGES/BSTEP/MODE/RSVD as plain unsigned and unsigned char fields (no bit-fields), a compile-time sizeof(struct os88_fsi)==16 self-check in os88.h, and the FSXM_*/FSIF_* constants. THE MODE-PER-SCREEN TABLE IS PINNED HERE, because the original's preview and postview are 640x350 and no 320x200 mode holds them: VGA plays in FSXM_VGA0D (5) and shows the two screens in FSXM_VGA12 (7, 640x480x16 - the layout unscaled and in colour, centred with 65 rows of margin); HERCULES plays in FSXM_HERC (4) and shows them in the SAME mode, because 720x348 holds a 640x350 layout whose lowest glyph row is 338 - it is letterboxed at x=40 and needs no mode change at all; CGA and EGA play in FSXM_CGA320 (2) and show them in FSXM_CGA640 (3, 640x200x2), which holds the 40-character lines at their true width but 12 rows of the ~22, so the original's blank-line spacing is compressed out and that is a stated fact. Every one of those ids is in fsx_capstab's mask for its adapter (VGA 0x01EF, HERC 0x0011, CGA 0x000F, EGA 0x000F), so nothing here needs a kernel change

---

**Need.** Pace the frame at the game's own 17 fps without task_sleep (nothing else is eligible inside the bracket, so a sleep returns at once)

- *Slot:* OSAPI_FSX_WAIT (0x02D8), no C thunk
- *Action:* Add int os88_fsx_wait(int kind) behind CC_HAS_FSX with OS88_FSXW_TICK/VSYNC/FRAME. TICK is the clock this game runs on - and the loop is structured so the CURSOR and the mouse poll run once per TICK unconditionally while a game step may take several, which is what keeps pointer latency at 55 ms when the world is at 300

---

**Need.** Read the keyboard inside the bracket - no event is dispatched there, and the port needs PRESSES (a tap of F3 selects a skill) which os88_key_down cannot give, being a LEVEL poll that is 'advice, not an oracle' and arms itself on the first call

- *Slot:* NONE EXISTS, and none should: the bracket's own contract says 'the app polls the keyboard directly' (SPEC.md 53.1, verified), which apps/missile/missile.asm:1066 does in assembly
- *Action:* Add int os88_fsx_key(int wait) to os88thunk.asm BEHIND CC_HAS_FSX - not a free-floating 'runtime helper' the way the draft had it, which would have cost every C package bytes. It does int 16h AH=11h (enhanced peek) falling back to AH=01, and AH=10h falling back to AH=00 when waiting, answering (scan<<8)|ascii or 0. THE FALLBACK IS THE POINT: the non-extended pair filters the 0x85/0x86 codes, so F11 and F12 - the original's pause and nuke - are unreachable through AH=00/01 on an AT and physically absent on the 83-key XT this port targets. With AH=11h they work where the keyboard has them, and the Mode row states the fact where it does not. Pause and Nuke remain the panel's own buttons 11 and 12 everywhere, which is the original's own primary route

---

**Need.** The rect the bracket owns on a two-display machine

- *Slot:* OSAPI_FSX_SURF (0x03F8), no C thunk
- *Action:* Add int os88_fsx_surf(struct os88_rect *r) behind CC_HAS_FSX, using the existing os88_rect. Costs a single-display machine one call and stops the fullscreen landing on the monitor we are not on

---

**Need.** Flip the Hercules page so the shadow blit is never seen half-done

- *Slot:* OSAPI_FSX_PAGE (0x04E8), no C thunk
- *Action:* Add int os88_fsx_page(int page) behind CC_HAS_FSX. SPEC.md 53.10 is the contract and it is richer than the draft assumed: the routine writes the register AND waits for the retrace, and it REFUSES the Hercules second page on a machine that also has a colour card, because that page lives at B8000 where a CGA lives - the predicate is vid_dual_ok. SPEC.md 85.2 records that Tank deliberately does not use it. Wave 2 measures whether the flip beats the span blit here and the answer is a decision, not an assumption; a refusal on a dual-card XT is a normal path

---

**Need.** Reprogram the VGA CRTC offset, start address, line compare and the Attribute Controller's pel panning and Pixel Panning Mode bit; VGA write modes 1 and 2 and the bit-mask register

- *Slot:* NONE EXISTS AND NONE IS WANTED - SPEC.md 53.1 grants the card to the bracket outright, excepting PIT channel 0, the sound ports and int 10h mode sets
- *Action:* Write it in apps/lemmings/lemblit.inc. Nothing in this tree drives split screen or pel panning today, so tests/cfsx is the gate that proves the three emulators and the field machine all honour it - and honour a mid-bracket mode change back and forth - before wave 2 builds on it

---

**Need.** Read one style's bank, larger than a segment, into a heap claim

- *Slot:* os88_file_read_seg() (apps/cc/os88.h:807) - THE DRAFT NAMED THE WRONG ONE AND ITS ANSWER OF 'no API change' WAS RIGHT FOR THE WRONG REASON
- *Action:* NO API CHANGE, but the CONVERTER'S OUTPUT FORMAT CHANGES AND THAT IS A WAVE 1 DELIVERABLE. os88_file_read_at() takes a DS-relative `void *buf` (os88.h:833) and every destination here is a claim of 30-77KB, so the draft's SPEC.md 20.12.2 cluster-window design could not have been built at all - it would have surfaced in wave 2 with the converter's file layout already written, which is LESSONS.md 4's 'the build tells you three steps late' with a format change attached. Instead the converter emits ONE 512-PADDED BAND FILE PER BANK and os88_file_read_seg(name, seg, cap) reads it straight in; a _seg base must be 512-aligned (os88.h:795-805, SPEC.md 2.1.1) and a claim's own base is, so the rule is met by construction. os88_file_read_at survives only for reading a manifest header into a DS-relative static, where its whole-cluster rule is trivially satisfied by a cluster-sized read at offset 0. OS88_PART (SPEC.md 20.12) was considered and rejected: parts ride INSIDE the package file and this payload is ~836KB that differs per geometry

---

**Need.** Tell the player a fullscreen refusal or a long level load is happening while a WF_FULL window covers the menu bar - and never let the KERNEL draw inside the bracket

- *Slot:* OSAPI_TOAST is wrapped as os88_toast()
- *Action:* No API change, and two rules rather than the draft's one. (1) The RunCPM lesson: a toast under a fullscreen window is invisible, so every refusal goes through one lem_say() that toasts AND, once the bracket owns the screen, prints the line in the game's own font. (2) THE ONE THE DRAFT MISSED: an overlay call site carries `call cc_ovneed` / `jc`, and cc_ovneed on refusal TOASTS THE REASON ITSELF - a kernel drawing call the C author does not write and cannot intercept, made with a foreign mode up while [vid_stride]/[vid_w] still describe the desktop (kernel/fsx.inc's own header says the [vid_*] live block is never touched). So the module is FORCED RESIDENT from the Play handler, outside fsx_run, by calling one cheap ovl_* and refusing the launch with a windowed toast if it answers 0. cc_ovneed is a four-byte no-op once the module is in, so nothing inside the bracket can reach that toast. RUNCPM's own lesson is the same shape one step earlier: the .OVL cannot be loaded from os88_main at all

---

## The six waves

### Wave 1 — The ground: the SPEC number, the fetch, the converter's band format, the gated C fsx surface, and a launcher that fits every adapter

**Features**

- SPEC.md's section number CLAIMED FIRST, as a wave-0 one-liner and not a wave-6 task (question 6): a stub section landing the surface->reference table, with docs/INDEX.md regenerated in the SAME commit. tools/os88index.py:220 regexes CC_PACKAGE out of the Makefile and the `docindex` fast row runs --check on every make, so the Makefile edit below fails every build until the index is regenerated; and checkdocs.py, also fast-tier, rejects a citation to a heading that does not exist, which lemblit.inc, lemmask.inc, lemfont.inc, lemmings.asm, tools/os88lem.py and tests/unit/t_lemdat.py all want to make
- tools/getlemmings.py: the pinned Lemmix commit, the pinned SHA-256, into build/lemdata/ behind $(BUILD)/lemdata.stamp - which gates the DISK target only, never `make lemmings` (apps/runcpm's runcpm-src.stamp is the shape, Makefile:4627/4698)
- tools/os88lem.py: every reader (DAT bit stream, 2048-byte record with Lemmix's bit unpacking, GROUNDxO, VGAGRx, VGASPECx's RLE, MAIN's seven sections, ODDTABLE) and the BAND writer - one 512-padded file per bank, so os88_file_read_seg reads a band into a claim and no cluster-window arithmetic is needed anywhere in the package. The resource band of strings is written here too. Plus --fixture (the committed synthetic set) and the per-geometry manifest printed in CLUSTERS as well as bytes. **DECISION 9, and it is part of this wave's deliverable, not a later tightening**: the writer REFUSES any file over 64,512 bytes (`WIRE_FILEMAX`) and writes a bank that would exceed it - a 4bpp VGASPEC picture at 76,800, or any future style - as NUMBERED PARTS, each 512-padded, that the package reads in sequence into one claim at successive 512-aligned offsets; the 80 level records are grouped EIGHT TO A FILE so the folder stays under ~90 entries (`RD_MAXENT` = 96); every name is uppercase 8.3 in `[A-Z0-9_-]`; and the run prints the whole-archive total against `WIRE_ARCMAX` (1,048,575) beside the per-geometry cluster manifests. SPEC.md §92.3.2 is the pinned form of all four
- tests/unit/t_lemdat.py: the independent second reader, registered in tests/suite.py's fast tier with MEASURED seconds and a SKIP when the stamp is absent
- apps/cc: the seven FSX thunks (run, caps, mode, wait, page, surf, key) ALL BEHIND %ifdef CC_HAS_FSX, CC_HAS_FSX + the cc_fsxentry trampoline, and struct os88_fsi with its sizeof self-check - the whole SDK change, in one edit, with the host stubs added in the same edit (LESSONS.md 4)
- tests/cfsx: the capability gate. Numbers on the glass, tests/covl's shape - the caps mask, the seven FSI fields, a 16-colour ramp, a hardware scroll by pel panning, the split-screen panel standing still, a MID-BRACKET mode change 0Dh -> 12h -> 0Dh, an ovl_* called from inside the bracket, and the same call with the .OVL deleted
- The launcher window: rating tabs and the four 72x27 rating signs, the level names read from the converted manifest, the original's preview fields beside the selected row, the four state rows (Mode / Music / Level Code / Save Progress) that give every greyed item a home, the level list PAGED from os88_wm_geom()'s live content height with the shadow sized from the same number, the empty menu set naming 'Lemmings', ovl_about(), and greyed rows for any style not on this disk
- THE SPLIT, in wave 1 rather than 'the next wave': lemovl.c exists from the start and carries ovl_about, ovl_greyfact, ovl_compose, ovl_chrome, ovl_fmt and the progress pair; the string literals move out of the image into the converter's resource band behind lem_str(); and the Play handler forces the module resident before any bracket can be entered
- apps/lemmings/hosttest: the stub os88.h with its glass model, lemtest.c driving the PAGED launcher at three modelled window heights, and the committed fixture
- apps/lemmings/build.sh running the host checks BEFORE anything is built for the 8086 - against the FIXTURE, so it needs no fetch - and the Makefile stamp that wires them into make (apps/runcpm's pattern: a failing check leaves no stamp and the compile does not run)

**Files**

- `SPEC.md`
- `docs/INDEX.md`
- `tools/getlemmings.py`
- `tools/os88lem.py`
- `tests/unit/t_lemdat.py`
- `tests/suite.py`
- `tests/cfsx/cfsx.c`
- `tests/cfsx/cfsx.asm`
- `apps/cc/os88.h`
- `apps/cc/os88thunk.asm`
- `apps/cc/crt0.asm`
- `apps/lemmings/lemmings.c`
- `apps/lemmings/lemtab.c`
- `apps/lemmings/lemui.c`
- `apps/lemmings/lemtext.c`
- `apps/lemmings/lemload.c`
- `apps/lemmings/lemovl.c`
- `apps/lemmings/lemmings.asm`
- `apps/lemmings/hosttest/os88.h`
- `apps/lemmings/hosttest/lemtest.c`
- `apps/lemmings/hosttest/fixture/`
- `apps/lemmings/build.sh`
- `Makefile`

**Done when**

make cfsx boots and a screendump shows a 16-colour ramp in mode 0Dh with the FSI fields printed over it, the bottom 40 rows standing still while the top scrolls a pixel at a time, then the same run switching to mode 12h and back; and its two overlay rows pass, including the one with LEMMINGS.OVL deleted, with NO kernel toast painted into the foreign mode. make lemmings && make test TESTAPPS=build/lemmings.img shows the launcher with real level names off the converted bands - 'Just dig!', 'Only floaters can survive this' - and the preview fields beside them, cropped and zoomed ON VGA AND ON VIDEO=cga --screen 640x200, where the list must PAGE inside a 137px content box and no row may fall past the dock. python3 tests/unit/t_lemdat.py passes 101 section checksums, 273 mask identities and 120 order entries against both reference tables, and SKIPS cleanly with the stamp removed. `make test-fast` is green and still inside 30 seconds. THE SIZE LINE IS QUOTED HERE, not in wave 6: os88pkg's image + bss + overlay for lemmings, plus a rebuild of cword, runcpm, c64, weave and loom with their size lines before and after, proving %ifdef CC_HAS_FSX cost them nothing. The harness prints its first cost table: a full launcher repaint and a selection move, each in calls and cells, at all three modelled window heights.

### Wave 2 — The raster, and THE MEASUREMENT THAT DECIDES THE PROJECT

**Features**

- lemblit.inc's VGA backend: the CRTC offset to 100 words, terrain at VRAM offset 8,000, panel at 0, line compare at scanline 320 with the Pixel Panning Mode bit, scroll by start address plus pel panning, write-mode-1 latch save-under into VRAM scratch at 40,000, write-mode-2 per-colour sprite draw with a runtime byte-phase shift, and lem_r_mode for the mid-bracket change
- lemblit.inc's shadow backend, shared by CGA and Hercules: an 80-byte stride on both, two per-row dirty-span sets, blit the union (SPEC.md 85.3.1), the 640x200 box at (40,74) on Hercules with horizontal pixel doubling
- lemmask.inc: the solid mask primitives and the terrain composer's per-piece inner loop, one pass writing the adapter's target form and the mask bit together with the four flag bits honoured - AND a batched probe entry taking four offsets in one call, because a SmallerC cdecl call plus prologue and epilogue is 20-30 instructions of a 16,000-instruction tick
- lemload.c's band reader through os88_file_read_seg, the four-claim table with its MEM_MAX count and its sizing refusal with the arithmetic, and ovl_compose() building a level from its terrain list (or from a VGASPEC picture at x=304)
- The panel drawn from its own bitmap, the minimap sampled by minimap.txt's rule against its 260-byte shadow, the view rectangle, and scrolling from the screen edge and from a minimap click
- The mouse mapped from desktop coordinates into the game raster per adapter, the app's own 14x14 cursor with its save-under, and THE TICK-PACED INPUT PATH: the cursor and the mouse poll run once per FSXW_TICK unconditionally, so pointer latency is 55 ms whatever a game step costs
- tests/lemband: the bench - microseconds per sprite saved, per sprite drawn, per span blitted, per composed terrain byte, per batched probe, on each backend
- hosttest grown with the raster: one named stub per assembly entry and a byte-per-pixel frame model that MAKES the written-twice counter real

**Files**

- `apps/lemmings/lemblit.inc`
- `apps/lemmings/lemmask.inc`
- `apps/lemmings/lemfont.inc`
- `apps/lemmings/lemload.c`
- `apps/lemmings/lemovl.c`
- `apps/lemmings/lemdraw.c`
- `apps/lemmings/lemmings.c`
- `apps/lemmings/hosttest/os88.h`
- `apps/lemmings/hosttest/lemtest.c`
- `tests/lemband/lemband.asm`
- `tools/os88lem.py`

**Done when**

Three screendumps of Fun 1 'Just dig!': mode 0Dh in colour with the panel and minimap below a line compare and the terrain scrolled to x=508 by pel panning; the same level on VIDEO=cga at --screen 640x200 with every second row taken (QEMU double-scans mode 6); the same on VIDEO=herc HERCSEG=0x7000 through tools/hercshot.py, letterboxed in its 640x200 box. tests/lemband prints microseconds per sprite, per span and per batched probe on each backend. The harness models the raster (not a refusing stub) and its written-twice counter reads 0. AND THE GATE: the MartyPC cycle count around one game tick with 60 live lemmings and one displayed frame, on each backend, turned into a frame time and an fps - taken HERE, before wave 3 builds a game on top of it, and measured against question 7's threshold. Below it, the plan does not proceed to wave 3 unchanged: LEM_STEP_MAX comes down, or the XT greys with the measured fact and vm/386-lemmings becomes the target machine.

### Wave 3 — The game: mechanics, skills, the panel and the clock

**Features**

- The eighteen action handlers with their constants transliterated from Lemmix Game.pas and lemmings_3ds src/lemming.c, every terrain probe through lemmask.inc's batched entry
- Skill assignment, its eight refusals, the prioritised cursor pick and the right-button behaviour, and the cursor-lemming word in LEMMIX'S test order (Game.pas:3645-3653: climber+floater -> ATHLETE, climber -> CLIMBER, floater -> FLOATER, else the action word) stored UPPERCASE
- Objects and triggers through the column bucket: exit, drown, fire, triggered traps with their sound ids, one-way walls, steel; the blocker's fields and its restore
- Terrain destruction: bash and mine masks, the explosion mask, the digger's rows, the builder's bricks - each a double write, to the picture and to the solid mask, plus the minimap's one-pixel clear against its shadow
- The per-tick step: the opening at 15/34/35, spawning, release-rate change with hold-repeat, the nuke walk, the clock at 17 frames a second, the end verdict, and the round-robin mechanics budget wave 2's measurement sized
- The panel live: twelve buttons with the original's hit boxes, two-digit counts, the selection frame, the 40-character status line delta-drawn AT THE FIVE SETTERS' OFFSETS (1-14 / 19-23 / 27-31 / 36-37 / 39-40) with their pad rules, everything UpCase'd and anything outside the 38-glyph set drawn as an 8x16 black cell, the minimap's lemmings, pause and the double-click nuke
- The keyboard: F1-F10 and Ctrl+F1/F2 from Lemmix GameScreen.Player.pas:519-531; SPEC.md 11.2.1's 'f' and 'F' as the fullscreen door in both directions; Esc cancel-first (finish the level while one runs, leave the bracket from the postview); F11/F12 through int 16h AH=11h where the BIOS answers, and the Mode row stating their absence where it does not
- PC-speaker tones on the original's events through os88_snd_tone - legal inside the bracket by SPEC.md 53.1's own words, so no verification detour is needed, with the caps predicate greying them where there is no path
- The harness's headless mechanics driver: replay a recorded command script tick by tick and diff every lemming's position, action and frame against an expected trace; and the status-string audit, cell by cell, against an independent rebuild

**Files**

- `apps/lemmings/lemact.c`
- `apps/lemmings/lemobj.c`
- `apps/lemmings/lemgame.c`
- `apps/lemmings/lemdraw.c`
- `apps/lemmings/lemtab.c`
- `apps/lemmings/hosttest/lemtest.c`

**Done when**

Fun 1 is completable over QMP: a screendump after 100 ticks shows lemmings walking out of the entrance, the status line reading OUT and IN and TIME with the right values IN THE RIGHT COLUMNS (crop and zoom the 40 cells; the IN field's last cell is column 31 and the template's stale '.' must be gone from it), a digger assigned by clicking the panel then the lemming, a hole appearing in the terrain and the minimap losing the pixel under it, and the level ending with all ten saved. Pressing 'f' leaves the bracket and pressing it again re-enters; Esc finishes the level to the postview and Esc from the postview leaves. The headless harness replays a fixture for 2,000 ticks on three levels with zero divergence, and its status-string audit passes. The cost table prints calls, VRAM bytes, sprites and MECHANICS INSTRUCTIONS per frame, and wave 2's microseconds turn them into a measured XT frame time to compare against wave 2's prediction.

### Wave 4 — The whole level set, and what each disk can hold

**Features**

- All 120 levels in the four ratings, in Lemmix Styles.Dos.pas SectionTable's order, with the 40 ODDTABLE overrides applied by the converter so the machine never sees that file
- All five graphic sets, and the four VGASPEC levels reading their 960x160 picture instead of composing a terrain list
- The superlemming flag at LVL 0x001E
- Per-geometry manifests: the converter decides what fits each of the four disks and PRINTS the arithmetic in CLUSTERS as well as bytes (360KB and 720KB both round every file to 1,024, tools/os88disk.py:124; 354 and 713 data clusters), and writes a LEVELS.TXT saying which levels this disk carries and why the others are not here
- Every level whose style or special picture is absent is greyed in the chooser with the fact, naming the missing file and the cluster arithmetic (SPEC.md 47 rule 5: a fact, never a guess)
- Progress in SYSTEM/APPDATA/LEMMINGS.SAV through ovl_progress_read/write, greyed on a read-only medium. NOT .PRG: apps/c64/c64cmd.c:251 opens the Standard File dialog on '*.PRG' and C64-SPEC 11.3 is Smart attach, so a Lemmings progress blob would be offerable to a 6510 (LESSONS.md 1)
- The four disk geometries built and os88disk.py --verify'ed

**Files**

- `tools/os88lem.py`
- `apps/lemmings/lemload.c`
- `apps/lemmings/lemui.c`
- `apps/lemmings/lemovl.c`
- `apps/lemmings/lemtext.c`
- `Makefile`

**Done when**

make lemmingsdisk builds four images and each verifies. A screendump of the chooser on the 720KB disk shows the four special levels greyed with their fact and its cluster arithmetic; the 360KB disk shows the styles it does not carry greyed with theirs. Taxing 27 'Call in the bomb squad' renders with 395 terrain pieces, not 68 - the one level where the reference readers disagree. A screendump of Fun 22 'A Beast of a level' shows the VGASPEC picture at x=304 with 304 pixels of empty level to its left. Progress survives a relaunch, verified from the host by walking the FAT12 directory entry to its cluster and not by grepping the image.

### Wave 5 — The original's two screens, in a second foreign mode

**Features**

- ovl_preview(): the level preview inside the bracket, after lem_r_mode has set the adapter's screen mode - FSXM_VGA12 on VGA, FSXM_HERC unchanged on Hercules, FSXM_CGA640 on CGA and EGA - the brown background tiled, the seven lines in Lemmix's own wording and per-line colours at y=82 and 16px pitch where the surface holds it, the shrunk level picture above them
- ovl_postview(): the results screen, the nine result texts, the tier rule from GetResultText, the header and footer lines, and the Mayhem 30 congratulation. THE ACCESS-CODE LINES ARE OMITTED and both branches take the failure branch's AddLineFeed(5), so the two are identical; the footer does not move either way because :189's AddLineFeed(18 - CountChar(CR)) force-positions it
- The CGA/EGA re-layout, and the greying fact that names it: the original's screens are 640x350 and a 640x200 mode holds 12 rows, so the blank-line spacing is compressed out and the content lines keep their 16px pitch and their true 640px width
- The session state machine: preview -> play -> postview -> next level / retry / menu on the mouse buttons the original uses, with the mode set and restored around each screen and lem_r_mode's failure a normal path
- ovl_about(): the About card, TWELVE ROWS - product, version, one line of what this port is, and the two principals (DMA Design / Psygnosis for Lemmings 1991 and its data; Eric Langedijk / Lemmix, zlib, for the screens, strings, level order and format work), pointing at README.TXT for the rest. NOTHING about how the build renders: the frame rate goes to PERFORMANCE.md and SPEC.md, the synthesised tones to the greyed Music row and the README (LESSONS.md 8)
- ovl_greyfact(): the dialog behind a greyed row, naming the fact
- The greyed rows wired to their predicates: Music, Level Code, Mode (0Dh on an EGA, and the F11/F12/Pause keyboard fact), Save Progress on a read-only medium, sound with no path

**Files**

- `apps/lemmings/lemovl.c`
- `apps/lemmings/lemtext.c`
- `apps/lemmings/lemfont.inc`
- `apps/lemmings/lemblit.inc`
- `apps/lemmings/lemmings.c`
- `tools/os88lem.py`

**Done when**

Screendumps of the preview screen for Fun 1 and of the postview after saving 10 of 10 (result text 8) and 4 of 10 (text 1), IN ALL THREE MODES: mode 12h on VGA with the 640x350 layout unscaled and centred; mode 7 on Hercules with the same layout letterboxed at x=40 and its lowest glyph row at 338 of 348; mode 6 on CGA with the content lines compressed and no line past row 200. 'Everybody here at DMA Design salutes you' measures exactly 640 pixels in the purple font on the first two. The About card cropped and zoomed on VIDEO=cga shows twelve rows and its OK button INSIDE the panel, not on the desktop, and carries no frame-rate and no synthesis note. Every greyed row, clicked, names its fact - looked at on VIDEO=cga, where grey rounds to black.

### Wave 6 — Polish, disks, machines and the record

**Features**

- The 16x16 icon, README.TXT beside the package carrying the FULL attribution list (DMA Design and Psygnosis; Eric Langedijk / Lemmix, zlib; Matthias / esoteric-programmer, lemmings_3ds, public domain; Thomas Zeugner / Lemmings.ts, MIT; ccexplore, Mindless, rt, Simon and Volker Oth for the format documents), the fetch, the synthesised tones, the absent particle spray and the LEVELS.TXT per geometry
- The four floppy geometries and their folder layout - package, overlay and every band file in ONE folder, because the .OVL is resolved in the launching instance's directory (SPEC.md 19.2.1)
- vm/xt-lemmings (4.77 MHz XT, VGA, 640KB) and vm/386-lemmings, each a copy of a machine that has booted with only the B: image and the uuid changed - and the XT one ONLY IF wave 2's measurement cleared question 7's threshold, because an XT target the port does not run on is a claim rather than a machine
- The MartyPC run repeated on the final build: frame time on the target, per level class, per backend, into PERFORMANCE.md as a field set and quoted in SPEC.md - and NOT in the About box
- SPEC.md's section filled out from the wave-1 stub and re-measured against the last build: the surface->reference table, the three rasters, the mode-per-screen table, the corrected frame arithmetic, the greying list with its facts, and the disk manifests
- The closing pass LESSONS.md 11 requires: every number in SPEC, the source headers, the README and the About box re-measured against the final build
- Attribution in the header of every file carrying derived material; nothing GPL or LGPL copied at all
- tests/suite.py rows: t_lemdat in fast (measured seconds, SKIP without the stamp), a lemmings screendump row in soak

**Files**

- `apps/lemmings/icon.inc`
- `apps/lemmings/README.TXT`
- `vm/xt-lemmings/86box.cfg`
- `vm/386-lemmings/86box.cfg`
- `SPEC.md`
- `README.md`
- `PERFORMANCE.md`
- `tests/suite.py`
- `Makefile`
- `docs/INDEX.md`

**Done when**

make lemmingsdisk builds and verifies four images; make 386-lemmings boots to the launcher and plays a level, and make xt-lemmings does too or the machine was never created and the fact is on the Mode row; the MartyPC number is in PERFORMANCE.md and quoted in SPEC and NOWHERE in the About box; make test-full is green including the two new rows; tools/checkdocs.py and tools/os88index.py --check pass; and os88pkg's size line in the commit message matches what SPEC says.

## Verification

- HOST, before anything is built for the 8086 - apps/lemmings/build.sh runs four checks against the COMMITTED SYNTHETIC FIXTURE so it needs no network fetch, and every one stops the build, wired into make by a stamp (apps/runcpm's pattern, not cword's): (1) the converter's --selfcheck, which re-reads what it wrote and compares it to what it decoded; (2) hosttest/lemtest.c, which includes the whole program against a stub os88.h, drives the PAGED launcher at three modelled window heights, replays mechanics fixtures headless against recorded traces, audits the shadow against the modelled glass, audits the 40-cell status string against an independent rebuild, asserts every status character maps into the 38-glyph set, and asserts no pixel is written twice; (3) a raw-x86 harness for lemmask.inc's probes and lemblit.inc's movers, run in QEMU with SS != DS and with negative controls, because that is the one place ES is loaded (apps/cword/hosttest/cwmovetest.asm's shape); (4) the cost table against a recorded ceiling. python3 tests/unit/t_lemdat.py - the independent second reader against the REAL fetched data - is a suite row rather than a build.sh check, and SKIPs when build/lemdata.stamp is absent.

- THE COST TABLE, printed on every build by lemtest.c, priced by PERFORMANCE.md's constants AND BY ITS ICOUNT ANCHOR (one -icount shift=3 PIT count = 0.359 ms of real XT = ~105 guest instructions, ~16.4 clocks an instruction, PERFORMANCE.md line 1144-1147 - so a 55 ms tick is ~16,000 instructions and NOT the 260,000 the draft assumed): per frame, sprites saved / restored / drawn, VRAM byte-plane writes, span bytes blitted, panel cells redrawn, minimap cells, and MECHANICS INSTRUCTIONS EXECUTED and PROBE CALLS MADE, both per lemming and in total; per level load, int 13h calls (not sectors) and composed terrain pixels; per launcher keystroke, calls and cells. This is how the three emulator-invisible defects - a visible redraw, a double-draw flash, input overrun - are seen at all, and it is how the frame budget is checked against arithmetic rather than against a feeling.

- tests/lemband, a bench package in tests/ (nothing under tests/ ships), measuring microseconds per sprite save, per sprite draw, per span blit, per composed terrain byte AND PER BATCHED TERRAIN PROBE on each of the three backends. The RunCPM lesson is binding: that project's row composer measured 306 us a cell against a model's 40, and the whole pacing was re-sized on the measurement rather than on the guess.

- MartyPC for the number that decides whether this is playable, TAKEN AT THE END OF WAVE 2 AS A GATE and again at the end of wave 3: docs/MARTYPC-DEBUG.md's cycle counter around one game tick and one displayed frame, on a busy level (60 live lemmings) and a quiet one, on each backend. Wave 3 does not start on an unmeasured raster. The figure goes into PERFORMANCE.md as a field set and is quoted in SPEC - and in neither the About box nor anywhere else user-facing, per LESSONS.md 8.

- tests/cfsx is the capability gate for the whole display design and it runs BEFORE wave 2, with SIX rows and not the draft's four: mode 0Dh set from C, the FSI block's seven fields printed, a 16-colour ramp, a scroll by pel panning, a split-screen panel that does not move, A MID-BRACKET MODE CHANGE 0Dh -> 12h -> 0Dh (which the 640x350 preview and postview screens need and nothing in this tree has ever done), an ovl_* called from INSIDE the bracket, and the same call with the .OVL DELETED - which must refuse without a kernel toast reaching the foreign-mode framebuffer. Nothing here drives split screen or pel panning today, so QEMU, 86Box and MartyPC are each asked separately, and a field run on the 5150 is asked for before the design is called safe (docs/FIELD-MACHINES.md).

- QMP screendumps, cropped and zoomed - a small change is easy to misread as nothing happening. `make test TESTAPPS=build/lemmings.img`, then tools/mouse.py and tools/shot.py --crop --zoom 8 on: the status line's FIVE fields with their exact columns (the IN field ends at 31, not 30 - a one-column drift is the defect the reviewers found in the draft's own authority row), the skill-count digits, the selection frame, the minimap's view rectangle, a greyed row, the paged level list at the bottom of the window, and the About card's OK button. Kill any previous QEMU with `pkill -f "[q]emu-system"` in a command that does not itself mention QEMU, and check its start time against build/kernel.bin's mtime before believing a dump.

- Per adapter, and --screen must match or clicks land nowhere: VGA at 640x480; VIDEO=cga with `tools/mouse.py --screen 640x200` and every second row taken from the dump because QEMU double-scans mode 6; VIDEO=herc HERCSEG=0x7000 with tools/hercshot.py build/qmp.sock 0x70000, because a plain screendump there is a black image rather than an error (docs/HERCULES-TESTING.md); and VIDEO=ega, where fsx_caps refuses mode 0Dh and the CGA raster must come up with the Mode row greyed. THE CGA CROP IS A WAVE 1 REQUIREMENT, not a wave 5 one, because the launcher's list is a wave 1 file and 200 rows is where it breaks: MBAR_H 20 + DOCK_H 24 + TITLE_H 18 + 1 leaves a 137px content box. Build knob kernels into their own BUILD= directory so a concurrent session is not booting our CGA kernel.

- The greying pass on 1bpp before any drawing change is called done (SPEC.md 39.4, 47.2): grey rounds to black there, so a disabled level row is a checkerboard and the About card's disabled ring is dotted. Look at it on VIDEO=cga.

- 86Box at the end: vm/386-lemmings always, and vm/xt-lemmings only if wave 2's measurement cleared question 7's threshold. Each cfg is a copy of one that has booted with only the B: image and the uuid changed and nothing else, because 86Box substitutes a default speed for an unrecognised cpu_family and rewrites the config on exit; `git checkout` the cfg before committing and never commit nvr/.

- Suite rows, registered in tests/suite.py: t_lemdat in the FAST tier with its MEASURED seconds and needs/SKIP wiring (the tier's 30-second wall clock is ENFORCED and already carries 45 rows, so a Python pass over 101 DAT sections, 273 pieces and 120 levels must be timed before it is registered), and a lemmings boot-and-screendump row in SOAK. The C toolchain row in FULL already asserts that a call to an os88_* with no thunk stops the build naming the function, which is what will catch a missing FSX thunk - but it builds only cc-smoke, chello, covl and cword, so the wave-1 rebuild of runcpm, c64, weave and loom with their size lines quoted is a manual step and belongs in the done_when.

- os88disk.py --verify on all four geometries in the recipe (a standalone invocation taking no other arguments), and the 360KB and 720KB CLUSTER arithmetic re-checked by hand: spc = 2 on both (tools/os88disk.py:124), so every band file rounds to 1,024 bytes and the manifest's claim is a cluster claim, not a byte claim. 354 clusters at 360KB and 713 at 720KB are the numbers LEVELS.TXT has to survive.

- A saved progress file verified from the host by walking the FAT12 directory entry to its cluster, not by grepping the image for a magic - the first grep finds the package's own string literals.

## Risks

- THE FRAME BUDGET IS THE PROJECT'S CENTRAL RISK AND THE DRAFT'S ARITHMETIC WAS WRONG BY 16x. A 4.77 MHz 8088 runs ~262,000 CYCLES in a 55 ms tick, which at PERFORMANCE.md's own measured ~16.4 clocks an instruction is ~16,000 INSTRUCTIONS, not the 260,000 the draft used. Sixty live lemmings at a realistic 800-2,000 instructions each - and 4-8 probe calls a lemming at 20-30 instructions of cdecl overhead each is 150-250 of that before a handler evaluates a condition - is 165-410 ms of mechanics, plus 30-60 ms of VGA sprite work: 200-470 ms a frame, or 2-5 fps against 17. The game-clock fairness argument survives (every clock in Lemmings counts game frames, so nothing becomes unwinnable) but it does NOT cover the pointer, which is why the cursor is repainted every FSXW_TICK regardless. The MITIGATIONS are all designed rather than hoped for: the batched probe entry, LEM_STEP_MAX's round-robin mechanics budget, and question 7's stated GO/NO-GO threshold measured on MartyPC at the END OF WAVE 2 as a gate on wave 3.

- Nothing in this tree has ever driven the VGA CRTC's line compare, start address or the Attribute Controller's pel panning, AND NOTHING HAS EVER CHANGED MODE MID-BRACKET, which the 640x350 preview and postview screens now require on VGA and on CGA/EGA. All three emulators may model any of it differently and 86Box's plain renderers have already produced one field artifact nobody could reproduce here (SPEC.md 79.5.9). A wrong line compare shows as the skill panel scrolling with the terrain; a missing Pixel Panning Mode bit shows as the panel jittering; a mode change that does not restore shows as a dead screen on return. tests/cfsx is the gate and it must be looked at on QEMU, 86Box, MartyPC and, if a field run can be had, a real 5150.

- THE RESIDENT BUDGET IS TIGHTER THAN THE DRAFT BELIEVED and the corrected estimate (50,000 of 61,440) already assumes the wave-1 split AND the string literals moved into the converter's resource band. LESSONS.md 5 says to expect the first full build to overshoot anyway - cword's did by 2,514. If the resource band or the split does not land as planned there is no second lever of that size, and the next moves out (lemdraw.c's panel composition, lemtab.c's per-adapter colour maps) are smaller. The size line is quoted from wave 1 for exactly this reason.

- An EGA machine gets four colours, not sixteen, and its preview and postview screens lose the original's spacing - two knowable disappointments on a capable card. kernel/fsx.inc:117 gives the EGA row 0x000F and its own comment says mode 0Dh there is 'a safe later addition, not pass 1'. Adding it is a KERNEL change and is deliberately outside this port's scope; both facts sit on the Mode row.

- The converter is the only thing between copyrighted data and the disk, and a bug in it is invisible until a level looks wrong six waves later. It now also owns the STRING RESOURCE BAND, so a converter bug can corrupt the on-screen wording as well as the pictures. The mitigation is tests/unit/t_lemdat.py written from Lemmings.ts's semantics rather than from the converter, the harness's per-string audit against the 38-glyph set, and the report's own cross-checks.

- The 720KB and 360KB subsets change what ships and are not the porter's to choose alone (question 3). They are also a moving target: the converter decides them, so a change to any encoding silently changes which levels a small disk carries - and the unit is a 1,024-byte CLUSTER on both geometries, so the byte arithmetic the draft reasoned in was the wrong currency. The converter PRINTS the manifest in clusters and bytes on every run, and LEVELS.TXT says the same thing to the player.

- F11, F12 and Pause are unreachable on the 4.77 MHz 83-key XT this port is nominally about, and F11/F12 are also unreachable through int 16h AH=00/01 on an AT-class BIOS. The build reads AH=11h/10h with a fallback and states the fact on the Mode row; Pause and Nuke stay reachable as panel buttons everywhere, which is the original's own primary route. THIS IS NOT FULLY RESOLVED: no machine in this tree can be booted with an 83-key keyboard to prove the fallback path, so it is verified by reading the BIOS call and by an 86Box AT run only.

- os88_snd_tone inside the bracket is ANSWERED, not a risk - SPEC.md 53.1 says in so many words that 'every sound grant the app takes inside the bracket is billed to its instance', so the draft's doubt is withdrawn and no verification detour is scheduled. What remains is only os88_snd_caps' ordinary refusal on a machine with no path, which greys the row.

- A worker may not touch a file (SPEC.md 20.6 rule 7) and an ovl_* may never be called from one (the gate is decided from SP). This program has no worker, which is deliberate; if one is ever added for a background level load, both rules bite at once and the shape is the OSAPI_WM_ONWAKE handshake, not a lock.

- The .OVL is resolved in the LAUNCHING instance's current directory, and this package's band files must be in that same folder. A disk that carries the package without its overlay is now WORSE than politely refusing, because the launcher forces the module resident before entering the bracket - so the Play action refuses in a window, which is the correct behaviour but means the disk is unplayable rather than partially playable. One folder, on every disk, checked in the recipe.

- Attribution is not optional and is easy to lose in a refactor. Lemmix's licence clause 4 says the notice may not be removed from any source distribution; every file carrying a derived table, string or offset names DMA Design/Psygnosis, Lemmix/Eric Langedijk, lemmings_3ds, Lemmings.ts and the format authors in its header, README.TXT carries the full list, and the twelve-row About card names the two principals and points at the README. Nothing GPL or LGPL may be copied at all - not adlib.cpp, not dbopl, not the OPL3 TypeScript, and not text lifted from lemmings_3ds/doc/mechanics/, whose own README says the licence is unknown and may be GPL.

- THE SPEC SECTION NUMBER IS CONTENDED (question 6). checkdocs.py's own header documents this exact failure - two authors appending 'the next free number' in parallel, a duplicate heading, every citation of that number ambiguous, three releases broken - and this port is one of three running at once. It must be claimed as a wave-0 one-liner and the stub section landed with docs/INDEX.md regenerated in the same commit, because the docindex fast row runs os88index.py --check on every make and the wave-1 Makefile edit fails every build until it is.

- LESSONS.md 10's standing traps will each cost an hour if forgotten: a stale QEMU answering on build/qmp.sock with the previous image; --screen not matching the adapter; a double-click needing one process, not two mouse.py invocations; VIDEO= restamping build/kernel.bin under a concurrent session; and make not seeing through #include or %include, so every included .c and .inc is a declared prerequisite or an edit leaves a stale .o88 that reads exactly like a change that did nothing.

## The reference sources

None of these is in this repository and none is vendored. They live in the
session scratchpad and are **re-cloneable** — the two git checkouts by URL and
commit, the data set by the pinned fetch `tools/getlemmings.py` performs.

| what | where it was read | how to get it again |
|---|---|---|
| **Lemmix** (Eric Langedijk, zlib) — the authority on the original's SCREENS, strings, level order and the LVL bit unpacking | `<scratchpad>/Lemmix` | `https://github.com/ericlangedijk/Lemmix` at commit `40e9bc34451f0e9127fd53b290a3877e700d143d` |
| **lemmings_3ds** (Matthias / esoteric-programmer, public domain with four named third-party exclusions) — the authority on the FORMAT DOCUMENTS under `doc/data/` and on the mechanics reading | `<scratchpad>/lemmings_3ds` | `https://github.com/esoteric-programmer/lemmings_3ds` at commit `7625315ef42d28d5d584eed9012a2cebe5695474` |
| **Lemmings.ts** (Thomas Zeugner, MIT) — the authority on the animation registration list and the mask semantics, and the independent second reader `tests/unit/t_lemdat.py` is written from | `<scratchpad>/Lemmings.ts` | `https://github.com/tomsoftware/Lemmings.ts` at commit `10c7c8b9167258503d02589cf048e2d83a43dd0a` |
| **the 26 DOS data files** — 358,154 bytes shipped, 956,492 decompressed | `<scratchpad>/lemdata/orig` | Lemmix `src/Data/Styles/Orig/orig.zip` at the pinned commit above, SHA-256 `bf1f2dbd11cd20df9f748047f8c4a32e7033d3ac7bb4b77058c3ecf27c0a9d6d`, 336,839 bytes — which is exactly what `tools/getlemmings.py` fetches |
| **the measurement** — `lemtool/lemdat.py` (the reader) and `lemtool/REPORT.md` (its output: every file, section, size, the 120-level order, the five ground sets, the four VGASPEC pictures, and the nine places the readers disagree) | `<scratchpad>/lemtool` | regenerate by running `lemdat.py` over the fetched set; every number in SPEC.md §92 that is not marked *(measured in wave N)* comes from it |

`<scratchpad>` is this session's scratchpad directory. Nothing there is
tracked, and nothing there is required to build: `make lemmings` runs against
the committed synthetic fixture, and only `make lemmingsdisk` needs the fetch.
