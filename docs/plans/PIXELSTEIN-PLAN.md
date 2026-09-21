# PIXELSTEIN 3D — the plan (FINAL, reconciled)

**Package:** `apps/pixelstein/` → `PXSTEIN.O88`, header name `'Pixelstein 3D'`,
label prefix `px_` (loader `pxl_`), SPEC.md **§96** — the heading carries the
literal string `Pixelstein 3D` because `tools/os88index.py` matches the header
name against SPEC headings with `\b…\b` (`os88index.py:499-503`); renumber
before merging `main` if a parallel branch takes 96 first. Tree: `main` @
`2237d1ba`.

**Reconciled from:** the draft (`plan-draft.md`) and three adversarial reviews
(`review-xtfit.md`, `review-tree.md`, `review-process.md`). Every finding is
either folded in below or named in §10 (risks) with why it stays open. `§N`
alone means SPEC.md; **M** = measured in the tree, **D** = derived, arithmetic
shown.

---

## 0. What the reviews changed, in one page

| finding | decision |
|---|---|
| **XT-1 (blocker)** — the frame table omitted the sprite texel loads, the far-call overhead, the `spotvis` clear and the candidate walk; 64×100 misses its own 7.0 gate | Table re-derived from **store + load + overhead COUNTS** (§3). **XT default is 64×80**, 48×80 the reserve rung, 64×100 the 286 setting. The ÷1.2 "believed" fudge is retired — the gate measures nominal. Two structural savings taken: a column-driver loop generated into part 3 (ONE far call a frame, not 64+110), and `spotvis` as a **generation byte** (no per-frame clear at all) |
| **XT-5 (major)** — the DDA cannot hold six words in registers; "≤130 a crossing" needs the steps patched, as Wolf did | **The four quadrant bodies are generated into part 3 and the two step immediates patched once per column** (4 stores ≈ 100 clk against 10 × 24 of memory operand). Two patch sites in the package, both in the pinned part 3, both rewritten by the routine that owns them |
| **XT-2 (major)** — the Hercules flip charged no retrace wait; `OSAPI_FSX_PAGE` waits (os88api.inc:2830); 20 ms quantisation | Priced honestly (§3): 128 ms of draw → 140 ms quantised = **7.1 fps against the shadow arm's 7.7**. **The flip arm is DROPPED from the shipped set**; the banked row table stays a documented generator input in §96.11 with this arithmetic, so a later measurement can bring it back as a Mode item. Wave 5's row-table work, per-page HUD dirty and the weapon-ladder finding (XT-7) go with it |
| **XT-3 (major)** — a flip backend Δ-fills against N frames ago; §88.12 measured Mode X slowest | **Mode X drops to 2 pages** (the third bought nothing under an unconditional wait); a ×2 Δ-fill staleness term is charged; the shadow+copy arm is the named fallback if wave 2's `pxsperf` on `os8088_xt_vga` reads §88.12's fate |
| **XT-4 (major)** — `cs_blit.expand` is ~48 clk/B, not 26 | CGA16 re-priced and given a **narrower view: 48 bytes × 80 rows** (96 logical px, two texels a byte). Still the second Mode item on `VID_CGA` |
| **XT-6 (major)** — the gate's corridor scene is the Δ-fill's best case | **Two pinned scenes** — a corridor and a 90° turn at a doorway — and the promise is made on both: **≥ 7.0 corridor / ≥ 6.0 turning** fullscreen, **≥ 6.0 / ≥ 5.0** windowed |
| **XT-7 (minor)** — a DI bias cannot move a scaler on a banked table | Moot with the flip dropped; the rule stays in §96.3: *every off-centre writer assumes rows are 80 apart, and the generator is the only thing allowed to know otherwise* |
| **XT-8 / tree-3** — "seven claims of MEM_OWNER_MAX 8" is wrong; the parts carve is ONE claim (os88partsbody.inc:628-632) | Claim list rewritten (§4.2): loader region + **one contiguous parts claim** + shadow + two lazy fetches = 5 of 8. The real ask — a ~132 KB contiguous run (147 on Mode X) — is stated, and parts 3/4/5 carry **`OP_OPT`** so a refusal degrades to the Low rung instead of failing the load |
| **XT-9 (minor)** — image estimate below SKIES' comparable, no overflow route | Estimate raised to ~39.5 KB image + 11.5 KB bss = **51,000 of 61,440**; the overflow route (a far-called COLD part for menus/About/cards/scores/settings) is named in §96.9 and its dispatch thunk designed in wave 1, populated only if `os88pkg.py`'s assert trips. **Repriced by wave 0's review**: §96.2 doubled the map and `spotvis` (two layouts, 16 KB of bss), so part 0 is ~60,000 of 61,440 with ~1,400 spare, and the cold part is wave 1's PLANNED shape (§96.9), not a contingency — with it carved out, ~54 KB and ~7 KB spare |
| **XT-10 (minor)** — texture 0 is "open", so fifteen materials, not sixteen | **15 materials**, part 4 = 15 × 2 × 1 KB = 30 KB (45 on Mode X); `t_pxsart` asserts the count against the nibble range |
| **tree-1 (blocker)** — the parts run is bounded at 128 unpacked SECTORS (os88partsbody.inc:430,460); bss ships inside part 0; OP_COMP does not relieve it | **Parts 1 (art) and 2 (levels) are `OP_LAZY`**, fetched by `op_fetch` and expanded by the package through `OSAPI_DECOMP` (skies' `csl_art`, csload.asm:78-99). The run is part 0 alone, ~100 sectors; **the recipe asserts run < 128 at build** through `tools/os88parts.py` |
| **tree-2 (blocker)** — the small arm was sized against free bytes; the binding number is the 17.5–20 KB largest run in a 52.5 KB arena | **The small arm is a §24.5 OMISSION carrying SKIES' ground** (§4.3): the smallest honest arm is ~32 KB image+bss + a 10 KB shadow = 42 KB, against TANK's 36 KB — the largest thing measured to fit — and a 32 KB image may not even load against a 20 KB run. `SMALLOMIT_GAMES += pxstein.o88` with the arithmetic beside it; the door is left open with the measurement that would reopen it |
| **process-2 (major)** — the Low-detail rung lived in part 3, so a build without part 3 had no renderer | **The linear (80-stride) ceiling/floor/flat-column ladders are STATIC, assembled into the image** — `row(r) = r × 80` is known at assembly time — and are the **Low rung the full build exposes as a Detail item and falls back to when part 3/4/5 is refused**. Only textured scalers, the DDA bodies and the column driver are generated. One path, exercised on every machine from wave 1 |
| **tree-4 (major)** — WIN1's 140 content rows do not fit a 640×200 CGA desktop (~137 max after `TITLE_H`) | The windowed Rows rung is a function of the content height `WM_GEOM` returns after `WM_PREFER`; the XT default 80 + 24 + 16 = **120 rows fits everywhere**; Rows 100 is offered only where the desktop returns ≥ 140 |
| **tree-5 / process-1 (major)** — five fast rows against a tier at 31.9 s of a 30 s budget; the registry's own rule (suite.py:344-345) | **One fast row**, `pxs-gen` in `t_paccman`'s mould (0.3 s): regenerate `pxtab.inc`/`pxart.inc`/`pxlev.inc` and hash against the committed copies. The DDA sweep, the reference-renderer frames and the scaler byte image are **soak** (`soak -k 'pxs*'`) with CLEAR SKIES' sentence. `t_pxsmap`'s sweep is sampled (every open cell × 16 headings) and still soak |
| **tree-6 (major)** — `APPS_GAMES` has five consumers; `apps-all.img` has 127 spare clusters (65 KB) | All five named (§4.4). **`PXSTEIN.O88` packed ≤ 56 KB is a hard `$(error)` in its recipe**, so wave 6's real art cannot grow past the cluster arithmetic silently |
| **tree-7/8/9/10 (minor)** | Windowed, the ENTER state and the score write are on the UI task through `W_ONKEY`; the shadow is claimed **at entry, before `WM_CREATE`**, so a refusal is §42.6's sentence in the window; a THIRD staleness fix (`OSAPI_GFX_BLIT1`'s kern_small comment, os88api.inc:1105-1107); two INDEX rows expected; `tests/pxsbench.py` is a named instrument with `wants=("build/pxsbench.o88",)` |
| **process-3 (major)** — the instrument sat inside the wave it prices | **Wave 0** is the instrument, the tables, the level tool and the reference renderer, nothing else; its `done_when` is a table of measured numbers, and wave 1 picks the default rung from them |
| **process-4 (major)** — the questions | Q3 (text-mode flip) settled in §96.11 by its own arithmetic. Added: the **default rung** question (64 at ~7.7 or 48 at ~9.7 — both ship), the **theme** question (before wave 2 fixes the material list), and the **three kernel gaps as one question with prices**. Q4 and Q5 merged into one content-scope question |
| **process-5/6** — file lists; wave 1's menus, parts and HUD band | Fixed: `pxgame.asm` in wave 3; `pxlev.inc` + `t_pxsmap` in every wave that adds a level; wave 1 installs the Mode item only; only parts that exist are declared; wave 1's screendump shows a black HUD band |
| **process-7** — the art pipeline as a process | The recipe and the key rule written into §5 and `--check`; a losable `--preview` criterion; **the 15 wall masters move to wave 2, the guard masters to wave 3** |
| **process-8** — models | The orchestrator's rubric decides (fable on engine/raster/inner-loop waves, opus on mechanical ones), so the reviewer's inversion is not taken; **wave 5 is split** so no one failing subsystem blocks three others |
| **process-9** — the guest-reading mechanism is unnamed | `tests/pxslib.py` is a wave-1 deliverable: `pkg_syms()` (tests/mcperf.py:66), `winptr()` (tools/os88geom.py:782), `tools/os88parts.py`, plus the new part-claim locator (read part 3's segment out of the instance's parts table) |
| **process-10** — launch-path costs unpriced | `pxsbench` row (h): generate once, `bt_build` once. **One byte-texture set resident at a time**; the F toggle rebuilds it (~50 K stores ≈ 0.3 s, behind a mode set the player already sees as black). §96.3 states the strongest claim outright: **the scaler set is identical across WIN1, CGA4, CGA16 and Mode X because all four are an 80-byte stride** |

Decisions **kept** from the draft: raycaster over voxel (C §2.4's 2.5–3.2× per column; a flyover is not a Wolfenstein); one ray per shadow byte; compiled scalers with DS = destination; Δ-fill + row-range present + nothing-dirty predicate; CGA4 shadow default and no text-mode flip; Mode X far-dark third shade; WIN1 as a 1bpp band, WIN4 on 286+ only; DOT DELIRIUM's session; eight-facings-by-mirror; Wolf's door, guard and HUD beats; `games360.img` at 360 KB.

---

## 1. The engine

### 1.1 Fixed point and tables

| quantity | format | note |
|---|---|---|
| map | 64×64 bytes `[y<<6 \| x]`; high nibble = **material 1–15** (0 = open); low nibble bit 0 SOLID, bit 1 DOOR, bit 2 DOOR_EW, bit 3 SPECIAL (elevator switch on a solid cell; secret door on a door cell) | 4 KB bss |
| position | Q8.8 unsigned per axis; tile = high byte | no shift anywhere |
| angle | 12-bit, 4,096/turn, `and ax,0FFFh` | 10.7 units a column at 64 |
| `px_sin` | 1,024-entry quarter table Q14; `cos(a) = sin(a+1024)` | 2 KB image |
| `px_tan` | 1,024-entry quarter table Q8.8, clamped ±127.996 | the DDA's steps; no divide |
| `px_fan[n]` | pixelangle per column for 48/56/64/72/80 and Mode X 160/320 | `tools/pxstab.py` → `pxtab.inc` |
| height | `h = PX_HEIGHTK / nx`, ONE `div` a column (163 clk M) | a `px_h[]` table is the wave-2 micro-option |
| `spotvis` | **a GENERATION byte per cell**: written with the frame's generation `CL` at every crossing (`mov [si+PX_SPOTVIS],cl`, the same 4-byte store), tested by `cmp …,gen`; cleared only when the generation wraps (every 255 frames) | the per-frame clear XT-1 priced at 8,600–34,000 clk is gone |

No `mul`/`div`/`shl reg,cl` in any per-pixel loop.

### 1.2 The trace — four generated quadrant bodies, patched steps

Per column: `angle = heading + px_fan[c]`, quadrant from the top 2 bits, `xstep`/`ystep` from `px_tan`, initial intercepts by 2 `mul` (266 M). Then:

- **The four quadrant bodies live in part 3** (generated at launch from a template in the image, ~40 bytes each) and **the two step magnitudes are patched into their `add reg,imm16` once per column** — 4 byte stores ≈ 100 clk, against 10 crossings × ~24 clk for a memory-resident step. This is what lets the body hold its state in registers: BX/DX the two intercept fractions, SI/DI the two candidates' map indices (the y tile kept pre-shifted as `ytile<<6`, stepped by ±64 immediates fixed per body), CL the generation, AL the map read. ≤ 24 bytes, ≤ 3 memory operands (map read, `spotvis` write, nothing else). **Budget ≤ 130 clk a crossing (D); 155 is the pessimistic anchor and TANK's 159 the ceiling** — `pxsbench` row (c) measures it in wave 0 before anything is shaped around it.
- Doors in the DDA: half-step the intercept (`sar` pair), compare the fraction with `doorpos[door]`; through → continue; struck → hit with `u = frac − doorpos`. ~200 clk. The two cells beside a door show material 15, the jamb.
- Hit: `nx` by two `imul` (MUL14, `tk3d.inc:31`), clamp `MINDIST` 0.09, `h = K/nx`, `u = frac>>3` (32 texels, mirrored per side), side → lit/dark. Outputs `wallh[c]` (the sprite z-buffer), `scaler[c]`, `texcol[c]`, `shade[c]`.
- Cast ≈ 64 × (150 + 266 + 100 + 10 × 130 + 630) ≈ **156,500** at 64 columns (D). *The 630 was below the ~800 floor its own two multiplies, divide and seven stores add to; wave 0 measured the hit whole at 1,294.5 and the setup at 1,094 (§96.1), and the cast is 234,900 at 64 rays.*

### 1.3 The compiled scalers and the static ladders (`pxgen.inc`)

- **Static, in the image (~1.3 KB)**: the linear ceiling ladder, floor ladder and flat-column ladder — Duff ladders of `mov [di+r×80],bl/bh`, entered at index. `row(r) = r × 80` is known at assembly time, so nothing generates them. **These are the Flat rung** (§14's rung 1 — "Low res" is §16's RESOLUTION axis and never a rung's name): the Detail item "Flat" in the full build, the fallback when part 3/4/5 is refused, and what wave 1 draws with.
- **Generated into part 3 (`OP_ZERO | OP_OPT`, ~30 KB, ~150 ms once, row (h) measures it)**: one textured scaler per height (2-row steps to 60, 3-row above, to 1.5·H clipped), body `mov al,[es:si+v]` (23) per texel and `mov [di+r×80],al` (25 RAM / 31 VRAM) per row, a near `ret` (§96.3: the driver is in the part and near-calls every scaler; the `retf` this line first carried would pop a segment); **DS = the destination, ES = the byte-texture set**; `codeofs[33]` per scaler; `px_sctab[h]`; the four DDA bodies; and **the column-driver loop** (~200 bytes) so the frame makes ONE far call into part 3 and every scaler entry is a near call inside it. The part is pinned (`MC_RLOC` 0), never `OSAPI_MEM_MOVABLE`; no register preserved (§85.3.4).
- **The scaler set is IDENTICAL across WIN1, CGA4, CGA16 and Mode X** — all four are an 80-byte row stride — and is regenerated only on a Size/Rows/Detail change. The Hercules banked table is the one input that would differ and it is not shipped (§0). *Scoped by wave 0 (§96.3): the ROW STORE is identical; the texel run's odd-row phase is the backend's (two `ror al,1` on CGA4, one `ror al,cl` on Hercules/WIN1, none on C160/Mode X), so the set is generated for the current backend and a Mode change regenerates it too (130 ms M).*
- **Two patch sites, both in part 3, both owned by one routine each**: the near `ret` (§96.3) patched over the first store of row `v_bottom` for a sprite post or a clipped wall (Wolf's ScaleLine), restored in the same routine before any `jc`; and the DDA step immediates, rewritten at the top of every column. `tests/pxsscale.py` diffs the generated part against `tools/pxsgen.py` byte for byte; the patches are transient and the diff is taken between frames.
- Every off-centre writer (weapon, Tab map, cards, fade) biases DI by `k × 80` and may do so **only because the stride is linear**; the rule is written in §96.3.

### 1.4 Δ-fill, row range, the idle predicate (`pxcomp.inc`)

Per column `top[c]`, `bot[c]` — the union of wall, sprite and weapon extents drawn last frame, **per page on Mode X (2 × 80 × 2 words)**. Ceiling rows `[top, top')` rewritten only if `top' > top`, floor `(bot', bot]` only if `bot' < bot`; nothing written twice. Every other writer widens the extents through `px_touch(col, top, bot)`. The present copies `[min(top, lasttop) .. max(bot, lastbot)]` across all columns. **Nothing dirty → nothing composed** (no move/turn/door/sprite/weapon/HUD/fade): step the sim, `FSXW_TICK` / `TASK_SLEEP 1`; the gate pokes `[px_force]`.

### 1.5 Sprites and the weapon (`pxspr.inc`)

Candidates = actors and statics whose cell's `spotvis` byte equals the generation (~5,000 clk for 128 tests). Transform 4 `imul` + 2 `div` ≈ 1,000 each, ≤ 8 considered, insertion sort far-to-near. Per covered column: one word compare against `wallh[c]` is the z-test; each **post** is one patched-`retf` scaler entry (~350 clk of patch + near call + restore inside the driver). 8 facings from ≤ 5 masters by a mirror table built at load (the mirrored post list is the columns reversed). Bounded by `t_pxsmap` (no cell from which more than two guards reach melee) and by the **8,000-store cap** (past it the farther sprite is drawn at every second column). Weapon: 16 byte-columns × 24 rows, columns 24–39 of the bottom 24 rows, 3 weapons × 3 frames, through `codeofs[0]` of the height-24 scaler, DI biased by row; no depth test; extents through `px_touch`.

### 1.6 Doors, keys, secrets, the exit (`pxgame.inc`)

≤ 64 doors `{cell, axis, pos, state, timer, lock}`; Use opens if unlocked; 16 units a tick (~0.9 s), holds 5 s, closes unless a body is in it; collision passes at `pos ≥ 128`; gold/silver locks; secret doors (bit 3 on a door — the wall's texture, counts in secrets %); the elevator switch (bit 3 on a solid cell, material 14 → 13 on Use, ends the floor). Pushwalls absent.

### 1.7 The clock, the input, the states

`tk_steps` verbatim: sim once per elapsed tick, capped at 3; a step ≤ 1/4 tile with per-axis collision at radius 0.25 (§93.6.1). `OSAPI_KEY_DOWN` (232 µs M) read once at the first owed step and cached; armed by a throwaway call in entry; the tap latch. `int 16h` in the bracket / `W_ONKEY` windowed through one `px_key_common`; `dd_kbdrain`; `dd_focus_ck` sticky pause; `[px_inbr]` set before `OSAPI_FSX_RUN`, cleared INSIDE the proc; restart at bracket entry after a game over. **Windowed, the ENTER state is advanced by `W_ONKEY` on the UI task and the score file is written there** (§20.6 rule 7; ddhs.inc:592-594's sentence); fullscreen the bracket IS the UI task. States: ATTRACT / READY ("Floor N") / PLAY / DIE (red fade = a shade-table swap, 12 ticks) / LEVELDONE (ratios card) / OVER / ENTER. Mouse turn ∝ pointer offset, clamped, off by default.

---

## 2. Adapters

`px_r_setup` (cs_r_setup's shape) binds six indirect words per backend — `setup`, `begin`, `col`, `sprite`, `glyph`, `end` — every branch writing both ways (csraster.inc:225). Menus from `OSAPI_FSX_CAPS` **on this window** (`tank.asm:499-508`), re-asked on `W_ONRESIZE`; a refused item renamed with its reason by rewriting its string pointer (`tank.asm:534-539`, §47). Settings one byte each in `SYSTEM/APPDATA/PXSTEIN.CFG`, clamped on load. Art is authored in **material indices**; `px_bt_build` transposes the masters into **one resident byte-texture set for the current backend** (part 4, byte per texel, column-major, lit + dark; far-dark on Mode X) through an ink table; the F toggle rebuilds it (row (h)); §39.4's UI mono map is never on the art path.

| backend | mode | XT default view | on the glass | HUD | present | ink |
|---|---|---|---|---|---|---|
| **CGA4** (CGA first item; EGA; VGA-as-CGA) | `FSXM_CGA320` | **64 × 80** | 256×80 at (32,28) | 24 rows | 16 KB shadow, row-range `rep movsw` to the 2-bank rows (`cs_devrows`) at 18.6 clk/B; palette 0 via `int 10h AH=0Bh` | 4 solid + 6 2×2 dithers = 10 tones; dark one tone down |
| **CGA16** (second item, `[vid_kind] == VID_CGA` only) | `FSXM_TEXT80` retimed to 160×100×16 (`cs_c160_mode`) | **48 × 80** | 96×80 logical at (32,4) | 20 rows | shadow + `cs_blit.expand` at **~48 clk/B** (csraster.inc:942-954 is a lodsw/stosb pair, not a rep); snow on a genuine CGA accepted (§88.15.4) | attribute nibbles, texels u and u+1 in one byte; dark = §88.15.3's table |
| **HERC** | `FSXM_HERC` 720×348, TANK's box at (40,74) | 64 × 80 | 512×80 at box row 60 | 24 rows | **shadow + 4-bank span copy on every Hercules** (lone or dual); the flip arm is absent, §0 | density 0/25/50/75/100% from a Rec.601 luminance ladder (`pxsart.py`, a2ref's idiom); phase arm chosen by `--preview` in wave 2; one-pixel features solid |
| **MODEX** (VGA) | `FSXM_MODEX` 320×240×256, **2 pages** | 64 × 80 | 256×80 at (32,48) | 24 rows | `OSAPI_FSX_PAGE`, DS = A000, map mask 0Fh; per-page extents; Detail 80/160/320 rays (mask 0Fh / 03h+0Ch / per plane) defaulted by `OSAPI_CPU_INFO`; the retrace wait charged | DAC 0–15 lit, 16–31 dark 60%, 32–47 far-dark beyond 8 tiles, 48–63 sprites/HUD |
| **WIN1** (any desktop; the XT default) | — | 64 × 80 | 512×80 1bpp band + a 16-px `font_run` line; frame 528 × (80+24+16 = **120**) via `WM_PREFER`, 8-aligned by `WM_SNAP`, `WM_OWNBG`, `WM_ONRESIZE` hooked, no `WM_SAVEU`, not resizable | 24 | ONE `OSAPI_GFX_BLIT1` of the row range (ES:SI = shadow+8, BP = 80, CX = 512, DX = rows), `_PEN` light grey on black on VGA; geometry re-asked every frame (`WM_CONTENT`/`WM_GEOM`/`WM_DISPLAY`) | the Hercules byte set |
| **WIN4** (VGA, tier ≥ 286) | — | 64 × 100 | 512×100 4bpp in four 25-row strips | 24 | four `OSAPI_GFX_BLIT4`s from the shadow claim's spare 6,400 B (of 6,464 — said in §96 so nobody adds a HUD row); greyed on `CPU_8086` with the price ("Colour: 1.2 s a frame on this CPU") | the Mode X set through a 32→16 table |

**The "XT default view" column above is the plan's pre-measurement default and wave 0 repriced it** (§96.1, on the user's direction — §15, §16): the XT default is **Size 64 × Rows 80 × Resolution: Low res on every backend of the 8086 tier** — the full view width at 32 rays — Full at 64/56/48 stays on the Size row, offered and reported, and wave 2's measured Textured Low res frame is the fork (under 8.0 on scene A, the rung moves to 48 × 80 Low res).

**The windowed Rows rung is what `WM_GEOM` allows**: a 640×200 CGA desktop returns ~137 content rows (`os88api.inc:2494-2499`, `TITLE_H` 18), so Rows 80 (120 total) is the ceiling there and Rows 100 (140) is offered where the desktop returns ≥ 140. Not offered: `FSXM_CGA640`, `FSXM_VGA13`, 0Dh, a same-mode bracket. Windowed 16 colours on an XT is a kernel change — question 6.

---

## 3. The per-frame arithmetic (D, anchored on M units; re-derived from counts)

Units: store 25 RAM / 31 VRAM; texel load 23; DDA crossing 130; `mul` 132.5 M; `div` 162.9 M; `rep movsw` → VRAM 18.6/B; `KEY_DOWN` ~1,100 M; windowed tax 3.8 ms mono / 6.7 VGA M; `gfx_blit1` 0.40 ms + 3.9 µs/B mono, 2.2 ms + ~1.0 µs/B VGA M; retrace period 20 ms Herc / 16.7 ms CGA-VGA (scout-perf §2.4). 4,772,727 clk/s; 8 fps = 596,600.

**Scene A (corridor), 64 × 80**: mean wall 32 rows, 10 crossings, 3 sprites 25 columns × 24 rows, weapon, Δ-fill ~700 stores, 1.3 ticks of sim.

| stage | counts | clk |
|---|---|---:|
| cast | 64 × (150 + 266 + 100 patch + 10 × 130 + 630 — the hit's floor is ~800, measured 1,294.5) | 156,500 |
| candidate walk | 128 × ~40 | 5,000 |
| stores | walls 2,048 + Δ-fill 700 + sprites 1,800 + weapon 384 = 4,932 × 25 | 123,300 |
| loads | walls 2,048 + sprites 1,800 + weapon 384 = 4,232 × 23 | 97,300 |
| overhead | 64 columns × 240 + 75 posts × 350 + 16 weapon posts × 350 + 8 transforms × 1,000 + one far call | 59,000 |
| HUD amortised | | 5,000 |
| simulation | `dd_step` 6.8–8.7 ms M for five actors, scaled to 1.3 ticks — **wrong, and corrected in wave 0's report and §96.1**: `tk_steps` returns elapsed ticks, so steps a frame = frame ÷ 54.925 ms (2.5 at this frame length) and the table has to be solved as a fixed point, not evaluated once; the 1.3 was DOT DELIRIUM's own frame ÷ tick | 45,000 |
| input + OS | 8 × 1,100 × 0.6 + int 16h + ~10 calls | 8,300 |
| **adapter-independent** | | **499,400** |

| backend, 64 × 80 | present | frame | ms | fps |
|---|---:|---:|---:|---:|
| **CGA4 shadow** | 5,120 × 18.6 + 8,000 = 103,000 | 602,000 | 126 | **7.9** |
| **Hercules shadow** | 103,000 | 602,000 | 126 | **7.9** |
| CGA16 48 × 80 (cast ×0.75, draw ×0.75) | 3,840 × 48 = 184,000 | 583,000 | 122 | 8.2 |
| Mode X 2 pages, XT-VGA | +6 × 4,900 VRAM stores + Δ-fill ×2 (+19,000) + 64 outs + retrace mean 40,000 | 588,000 → quantised to 133 | 133 | 7.5 |
| WIN1 on a 1bpp desktop | 0.40 + 5,120 × 3.9 µs + 3.9 tax = 24.3 ms | 615,000 | 129 | **7.8** |
| WIN1 on an XT-VGA | 2.2 + 5.1 + 6.7 = 14 ms | 566,000 | 119 | 8.4 |
| *(not shipped)* Hercules flip | +29,000 VRAM + 19,000 staleness + 47,700 wait, no copy | 595,000 → quantised to 140 | 140 | 7.1 |

**Scene B (a 90° turn at a doorway)**: the Δ-fill band is tens of rows on the columns crossing an edge — ~2,500 Δ-fill stores instead of 700 (+49,000), present = every row (already). CGA4 ≈ 651,000 = 136 ms = **7.3 fps** (D); windowed mono ≈ 7.2. TANK's +55% turning premium does not apply — it repaints its whole wireframe — but this is the number the gate must measure, not derive.

**Rungs (CGA4, corridor)**: 64 × 100 → 669,000 = 7.1; **64 × 80 → 7.9 (the default)**; 56 × 80 → 8.7; **48 × 80 → ~494,000 = 9.7 (the reserve)**; flat walls (Flat) → +5% only. Worst frames: two guards at melee 10,240 stores → ~6 fps before the cap; a 20-crossing corridor −83,000. **Superseded by wave 0's converged table** (§96.1, `docs/reports/PXS-FRAME-2026-09-13.md`): with the setup and the hit measured at 2.1× their D each, the sim tick measured (4.60 ms at E1M1's counts, 16.98 at the caps) and the frame solved as a fixed point, 64 × 80 Full reads 6.3 with the rotate and 48 × 80 Full 8.2, so **the XT default is Size 64 × Rows 80 × Resolution: Low res — 9.4 / 8.9 — and Full stays on the Size row**; Flat Full 8.1, Flat Low res 12.7, Wire ~12.2.

**The promise** (`tests/pixelstein.py`, cycle-exact on MartyPC, `[px_force]` poked): **≥ 7.0 fps on scene A and ≥ 6.0 on scene B** fullscreen on `os8088_5150_cga_gla` (CGA4) and `os8088_5150_herc_gla`; **≥ 6.0 / ≥ 5.0 windowed** on the Hercules desktop; Mode X and windowed VGA measured on `os8088_xt_vga` and reported. If the default misses, the rung moves (64 → 56 → 48 columns) before the gate is edited. Wave 0 replaces every D unit above with an M one before wave 1 shapes code around it. **Rewritten by wave 0 on the user's direction** (§96.1): the promise is **≥ 8.0 (A) / ≥ 7.0 (B) fullscreen at Size 64 × Rows 80 × Resolution: Low res**, and the fall-back rung is **48 × 80 Low res** — the ray count is what a slow machine gives up, never the view's width.

Against the tree: TANK 72–123 ms (8.1–13.9 fps), CLEAR SKIES 164 ms (6.1), both called playable. A 286-12 is ~5–6× faster and tick-bound at 64 × 100 / 160 rays; a 386 draws 320 rays in Mode X tick-bound.

---

## 4. Memory, the package's shape, the disks

### 4.1 Image + bss (part 0, ≤ 61,440)

| | bytes |
|---|---:|
| code: cast + the DDA template, generator, static ladders (1.3 KB), Δ-fill, sprites, doors, actors, player, HUD, presents + glyph writers, window/worker/bracket, menus, About, settings, scores, loader glue | ~34,000 |
| tables: `pxtab.inc` 5,796 (M: sin 2,050, tan 2,050, eight fans 1,664, `px_fantab` 32), five 32-byte ink tables, strings — SUPERSEDED by SPEC.md 96.1's `px_fan*` row: wave 2's four Low-res fans made it twelve fans, 1,968 bytes of fans and selector, 6,068 for the include | ~6,300 |
| bss: **map 4,096 × 2 and spotvis 4,096 × 2** (§96.2's two layouts — the single-layout 8,192 this row first carried was priced against them and lost), column arrays 80 × 4 words, per-page extents 2 × 80 × 2 words, actors 32 × 24, doors 64 × 6, statics 96 × 4, player/HUD/settings/scores | ~19,700 |
| **image + bss (SKIES' game part is 51,776 for comparison)** | **~60,000 of 61,440 — ~1,400 spare** (this row read ~51,000 before wave 0's review repriced it; the 8,192 is the second map and the second `spotvis`) |

**The overflow route is the planned shape** (repriced by wave 0's review: a 1,400-byte margin against an estimated code line is no margin): a far-called **cold part** (`OP_SEG` — the kind you far-call; `OP_ASSET` is data reached through a segment, and the first cut of this sentence had them the wrong way round — code at org 0 reading its data through DS = part 0) for menus, About, the cards, scores and settings, ~6 KB, entered through a dispatch thunk in part 0 — designed AND populated in wave 1, which takes part 0 to ~54 KB with ~7 KB spare; `os88pkg.py`'s size assert stays the gate. Worker stack `OS88_STACK_256` sized with `tools/stkdepth.py` + 64 + SOUND.DRV's 16.

### 4.2 Heap — five claims, one of them contiguous

| claim | KB | when |
|---|---:|---|
| the loader's own region | 2 | freed after `OSAPI_PKG_REHOME` |
| **the parts claim, ONE contiguous run** (os88partsbody.inc:628): part 0 (~54 with the cold part carved out, 60 without, `OP_SEG \| OP_COMP`) + the cold part (~6, `OP_SEG`) + part 3 scalers/DDA/driver (**~37**: both resolution sets at the MEASURED 16,579 B a set (§96.1 row (h)) + `col2tex` ~2 KB + bodies and driver ~1 KB — §16's "~30 + ~30" was priced before the measurement, `OP_SEG, OP_ZERO \| OP_OPT`) + part 4 byte-textures (**30** — one byte a texel, the odd-row phase being the scaler's rotate as the 1992 engine turns it (§96.3); 45 Mode X; 60 on the dither backends only under the dual-phase word-load FALLBACK; `OP_ASSET, OP_ZERO \| OP_OPT`) + part 5 sprite set (~21, `OP_ASSET, OP_ZERO \| OP_OPT`) | **~148 (163 VGA)** — read ~132 (147) before wave 0's review with a single map layout and one scaler set; 178 with the word-load fallback | `op_load`; **the read run is part 0 alone, ~100 of the 127 sectors the carve allows** (os88partsbody.inc:430,460; §20.12.7 keeps `len` unpacked) |
| shadow 80 × 104 rows in 16 KB (WIN4's strips in the spare 6,400 B) | 16 | **at entry, before `WM_CREATE`** — a refusal is §42.6's sentence in the window, never a black bounce (§24.5's SKIES row) |
| part 1 art masters, `OP_LAZY`, LZ4 stream expanded by the package through `OSAPI_DECOMP` (`csl_art`'s shape; `OP_COMP \| OP_LAZY` is refused by the macro) | 24 | first paint; resident (the F toggle re-transposes from it) |
| part 2 levels, `OP_LAZY`, RLE | ~6 | first paint |
| **total** | **~196 KB (211 VGA)**, 5 of `MEM_OWNER_MAX` 8 (~180/195 before the reprice; 226/211 under the word-load fallback) | a 640 KB XT has ~500 KB free; a second instance is refused on the contiguous run, in the package's words |

**When the scratch parts are refused** (`OP_OPT`, checked through `op_seg` = 0): any of 3/4/5 missing puts the game on the **Flat rung** — static ladders, flat walls, sprites as flat silhouettes through the flat ladder — the same code the Detail item exposes, so it is exercised on every machine.

### 4.3 The 128 KB floor — an OMISSION, with the ground written down

The floor machine's arena is **52.5 KB** and the largest run a claimant can have once the caches are shed is **17.5–20 KB** (§24.5's SKIES row, §85.3.5.1, Makefile:8699-8706). TANK's small arm — image 18 KB + an 18 KB claim = 36 KB — is the largest thing measured to fit. Pixelstein's smallest honest arm (no generator, static ladders, a generic DDA, 3 levels, 16×16 sprites, the same 64×64 map + `spotvis` + column arrays) is ~32 KB image+bss + a 10 KB shadow = **42 KB, and a 32 KB image may not load at all against a 20 KB run**. So `SMALLOMIT_GAMES += $(BUILD)/pxstein.o88` with this arithmetic in §96.9 and a row in §24.5's table (**a REQUIREMENT the machine cannot meet: a 64-tile world plus its visibility plus a frame is more contiguous memory than the arena holds**). The door stays open: a 32×32-level, 48×64 arm measured on `os8088_5150_cga_128k` would be a substitution — nobody has measured it and the plan does not promise it. `soak -k smallreq` reads the built floppy either way.

### 4.4 Disk

Loader 2 + part 0 packed ~33 (11.5 KB of bss zeros pack well) + art ~16 + levels ~5 ≈ **56 KB packed**, and **56 KB is a hard `$(error)` in the recipe** (`stat` after `$(OS88PKG)`), because wave 6's real art lands after the disk arithmetic was checked. `APPS_GAMES += $(BUILD)/pxstein.o88` and **its five consumers** (tree-6): `APPS360` (Makefile:10039) and `APPSARGS360` (:10081) take `APPS_GAMES_360 := $(filter-out …)` with the §24.6.1 sentence; `GAMES360`/`GAMESARGS360` (:10215) KEEP the unfiltered list; `dbg-apps360.img` (:4958,4963, a 360 KB disk off `all`) takes the filtered list; `SMALLGAMES` (:8844) subtracts it through `SMALLOMIT_GAMES`. Measured now: `apps360.img` 313/354 clusters (cannot take it), `games360.img` 120/354, **`apps-all.img` 2,720/2,847 — 127 spare clusters = 65 KB, so the package leaves ~9 KB on the disk `make live` carries**. A soak row asserts `PXSTEIN.O88` is on `games360.img` and `apps.img` and NOT on `apps360.img` or `smallapps360.img`.

---

## 5. Assets and the host tools

- **Masters** in `apps/pixelstein/art/`, 16-colour PNGs in material indices: **15 walls** 32×32 (the theme is question 3; the placeholder list: grey stone ×2, blue stone, wood ×2, brick, banner, portrait, emblem, cell door, cell bars, steel door, elevator 14, elevator-used 13, jamb 15), guard 5 facings × 3 + attack 2 + pain 1 + die 3 + dead 1 = 22 frames, dog 4 × 2 + 3, 6 decorations, 8 pickups 16×16, weapon 3 × 3 at 16×24, face 6, digits 0–9 16×24, icons.
- **The image-model recipe** (process-7): generate **flat, posterised, low-frequency** at 256×256 or 512×512; downsample by an **integer factor with a mode filter** (a box filter returns mush); snap to the 16 RGBs; **the sprite key comes from ALPHA, never from near-magenta** (index 5 is the key; an image model puts near-magenta inside a sprite) — `--check` refuses a master with index 5 inside its alpha. `--preview DIR` renders every asset through all five ink tables at each aspect (VGA 1.00, Hercules 1.55, CGA 2.40); **the losable criterion**: two named materials (brick, stone) must be distinguishable at 64 columns in the CGA4 render at 1:1, the same crop each time, and the guard's front and side facings distinguishable at 12 columns; a wave that fails it re-chooses materials before it is done.
- **`tools/pxsart.py`** (csart.py's shape; stdlib only for anything `make` runs; `--pil` an authoring aid; `--placeholder` procedural art; `--dither` both arms in `--preview`) → `pxart.inc` (offsets, sizes, the five ink tables incl. the Rec.601 ladder, post directories — never pixels) + `build/pxsart.bin` (the LZ4 stream); the 16×20 HUD crops for CGA16; asserts 15 materials.
- **`tools/pxstab.py`** → `pxtab.inc`. **`tools/pxslevel.py`** → `build/pxslev.bin` + `pxlev.inc` from `levels/*.txt` (one character a cell; the legend in the tool), with the checks: reachability, keys before doors, ≤ 64 doors / 32 actors / 96 statics, no axial sight line over 24 cells, the DDA budget (mean ≤ 12, worst ≤ 26 over every open cell × 16 sampled headings), the melee invariant.
- **`tools/pxssim.py`** — the host reference renderer (htmsim/weavesim's shape): the same tables, DDA, hit and Δ-fill byte semantics; renders (position, heading, size, backend) to shadow bytes and a PNG; `--dump` the column arrays.
- **`tools/pxsgen.py`** — the host model of part 3: the byte image of every scaler, the DDA bodies and the driver for (H, columns, backend — the phase instructions are the backend's).
- **`tools/pxsframe.py`** — the frame table as a program (§96.12): the §3 counts scaled to the rung, priced on `pxsbench`'s M units and solved as the fixed point with the measured sim tick; every fps in §96.1 is a line of its output.
- **`tests/pxslib.py`** — the guest-side reader every soak row uses: `pkg_syms()` (tests/mcperf.py:66), `winptr()` (tools/os88geom.py:782), `tools/os88parts.py` for the table, and **the part-claim locator** (part 3's segment out of the instance's parts table) — new work, a wave-1 deliverable.

---

## 6. Package conventions

`pxstein.asm` the loader (csload.asm's shape, first in the rule so `os88index.py` finds it — **expect two INDEX rows**, as SKIES has, docs/INDEX.md:326-327) + `pxgame.asm` part 0; `PXSTEIN_SRC` on dotdel's idiom with skies' `--part` lines, `$(PKGZSTAMP)`, `$(OS88PKG)`. Entry order from tank.asm:407: `op_load` / shadow claim / `OSAPI_VIDEO`, `KEY_DOWN` arm, `SRAND`, `WM_CREATE`, `REGION_MOVABLE`, `PREFER`, `SNAP`, `OWNBG`, `ONRESIZE`, `ONWAKE`, `MENU_SET` (wave 1 installs **Mode only**; Size/Rows/Detail arrive in wave 2, Sound/Mouse in wave 4), `OSAPI_ABOUT_SET` via `os88ui_about`; sound byte 1. `font_run` only. §96 written BEFORE the code with **three** staleness fixes in the same commit: §53.5's `gfx_flush` clause; `os88api.inc:2773`'s `OSAPI_FSX_RUN` CX comment; **`os88api.inc:1105-1107`'s `OSAPI_GFX_BLIT1` "kern_small carries the slot and not the body"** (withdrawn by §5.4.2.5.1). `tools/os88index.py` regenerated by every wave that adds a tool or package file; every generated-and-committed include regenerated by the wave that changes its input. **The index lists TRACKED documents** (`git ls-files`, deliberately — an untracked draft must not fail every `make`), so a wave that adds a `docs/` file regenerates the index in the COMMITTING step, after `git add` and before the commit; wave 0 learned this by widening the listing to untracked files and having that reverted.

---

## 7. Waves

Each wave builds (`make`, fast tier green), boots, and is photographed. The model named implements it (the orchestrator's rubric: fable on engine/raster/inner-loop waves, opus on mechanical ones).

### Wave 0 — the instrument, the tables, the reference renderer — **fable**

Files: `SPEC.md` (§96 drafted with D numbers marked, the three staleness fixes), `apps/os88api.inc` (two of the three staleness fixes live there), `Makefile` (`pxsbench` on the two bench disks through `BENCHIMGPKGS` — never on `BENCHPKGS`, which is also the field and combo disks; `PXSTEIN_SRC` skeleton; `make pxsgen`), `tools/{pxstab.py, pxslevel.py, pxssim.py, pxsframe.py}`, `apps/pixelstein/{pxtab.inc, pxlev.inc}`, `apps/pixelstein/levels/e1m1.txt`, `tests/pxsbench/pxsbench.asm`, `tests/pxsbench.py`, `tests/suite.py` (the instrument row, `wants=("build/bench360.img",)`; the `pxs-gen` fast row; the `pxs-level` soak row), `tests/unit/t_pxsgen.py` (the digest row), `tests/unit/t_pxslevel.py` (the sweep row), `docs/INDEX.md`, `docs/reports/PXS-FRAME-<date>.md`.

Features: `pxsbench` (INSTRUMENT, `make bench`) rows (a) the 4-byte compiled store ladder, 80 rows, to RAM/B800/B000/A000; (b) the static vertical ladder; (c) **the patched-immediate DDA body at 10 and 20 crossings**; (d) a 5,120 B span copy to B800/B000 and a 512-wide `OSAPI_GFX_BLIT1`; (e) `cs_blit.expand` of 3,840 B and the direct attribute-stride store; (f) a two-phase `xlat` texel; (g) `KEY_DOWN` × 8; **(h) one scaler-set generation and one `bt_build` transpose**. `pxstab.py` + `pxslevel.py` + e1m1 (rooms, corridors, a door slab, ≤ 24-tile sight lines) + `pxssim.py` rendering e1m1's spawn and the doorway-turn scene to PNGs.

Done when: `python3 tests/pxsbench.py` prints all eight rows on `_cga_gla` and `_herc_gla` (expect ~25 RAM / ~31 VRAM stores, ~27 ladder, ≤ 155 DDA, ~18.6/B copy, ~48/B expand, the blit1 row term at 512 wide, the generation and transpose in ms); §3's table is re-run with the M units and **the XT default rung is written into §96.1 from measurement** (expected 64 × 80 Full; the setup and the hit each came in at 2.1× their D and the converged table puts 64 × 80 Full at 6.3 with the rotate — so **the default is Size 64 × Rows 80 × Resolution: Low res on the user's direction**, 9.4 / 8.9 on the same units with the sim tick measured, and the promise on it is ≥ 8.0 / ≥ 7.0); `tools/pxssim.py` produces the two scenes' PNGs; the `pxs-gen` fast row is registered and the fast tier's declared total is quoted (**32.2 s of 30 — pre-existing at 31.9, the runner enforces the measured wall, and no PIXELSTEIN row after `pxs-gen` is fast**; §96.10); `make` green.

### Wave 1 — the skeleton, the Flat rung on every adapter, WIN1, the gate — **fable**

Files: `SPEC.md`, `Makefile` (the loader/game rules, `--part`, the 56 KB assert, `APPS_GAMES` + `APPS_GAMES_360` at all five sites, `SMALLOMIT_GAMES`), `apps/pixelstein/{pxstein.asm, pxgame.asm, pxicon.inc, pxcast.inc, pxgen.inc, pxcomp.inc, pxrast.inc, pxwin.inc, pxgame.inc}`, `tools/pxssim.py`, `tests/pxslib.py`, `tests/{pixelstein.py, pxssim.py, pxsdisk.py}`, `tests/suite.py`, `tests/unit/t_registry.py`, `docs/INDEX.md`, `README.md`, `docs/reports/PXS-FRAME-<date>.md`.

Features: the loader + part 0 + the parts table declaring **only what exists** (part 0, the lazy level part; the art part arrives in wave 2); the shadow claimed at entry; the icon; menus with the **Mode item only**; About; the cold-part dispatch thunk designed (not populated). The four-quadrant DDA **as a generated, patched body in part 3** with the generic memory-step body as the image-side fallback; the hit; the column arrays; `spotvis` generations; the **static ladders** (the Flat rung) drawing flat lit/dark walls; the Δ-fill; the row range; the nothing-dirty predicate. All four fullscreen backends' geometry, mode set, palette and present (CGA4 shadow, CGA16 48-wide on `VID_CGA`, HERC shadow, MODEX 2 pages via `fsx_page` with per-page extents); F/f/Esc; `[px_inbr]`. The worker, WIN1 through one `gfx_blit1` (pen on VGA), geometry every frame, the Rows-from-`WM_GEOM` rule, focus, `dd_kbdrain`, the `font_run` line. `tk_steps`, cached `KEY_DOWN`, the tap latch, move/turn/strafe/run, per-axis collision. `tests/pxslib.py` (the guest reader + part-claim locator). `tests/pxssim.py` (soak: guest column arrays and shadow vs host, both scenes). `tests/pixelstein.py`: draws (`px_frames` climbs), advances, no flash (FLOOR 70 over 40 frames), **fps on BOTH pinned scenes** with `[px_force]` poked, on `_cga_gla`, `_herc_gla`, `_xt_vga`, windowed on `_herc_gla`. `tests/pxsdisk.py` (soak: on `games360`/`apps`, off `apps360`/`smallapps360`; the packed size).

Done when: a screendump on each of CGA4, CGA16, Hercules, Mode X and in a window shows the flat-shaded corridor with lit/dark faces, a door slab, the ceiling/floor split **and a black HUD band** (nothing paints it until wave 4), from the same spawn as `pxssim.py`'s PNG; `tests/pxssim.py` 0 differing heights / 0 differing shadow bytes on both scenes; `tests/pixelstein.py` ≥ 8.0 (A) / ≥ 7.0 (B) on CGA4 and Hercules at the default — Flat Low res at 64 × 80 (expect ~12.7) — and **the Flat Full 64 × 80 frame FILED** against §96.1's 8.1 / 7.5 as a calibration of the table (no longer a rung decision), the numbers filed as the wave-1 row; `tools/os88parts.py` reports the run < 128 sectors; `make` and `make test-full` green; `kernsize.py` unchanged.

### Wave 2 — textures, the scalers, the walls — **fable**

Files: `apps/pixelstein/{pxgen.inc, pxrast.inc, pxcomp.inc, pxcast.inc, pxart.inc, pxset.inc, pxgame.asm, pxstein.asm}`, `apps/pixelstein/art/*.png` (**the 15 real wall masters** on question 3's answer; `--placeholder` if it has not come), `tools/{pxsart.py, pxsgen.py, pxssim.py}`, `tests/{pxsscale.py, pxs160.py, pxsfsx.py, pxsperf.py}`, `tests/unit/{t_pxsart.py, t_pxsscale.py}` (`t_pxsgen` is wave 0's fast digest row — the name is taken), `tests/suite.py`, `docs/reports/PXS-FRAME-<date>.md`, `PERFORMANCE.md`, `SPEC.md`, `docs/INDEX.md`.

Features: `pxsart.py` (the pipeline, the recipe, the key rule, `--preview` with the losable criterion, `--check`, the five ink tables, both Hercules phase arms previewed and one chosen — recorded in §96.4) → the lazy art part + `pxart.inc`. `px_bt_build` → part 4 (one resident set; rebuilt on the F toggle; the C160 two-texel byte). The full textured scaler set with `codeofs`, `px_sctab`, the `retf` patch/restore helpers, **the column driver in part 3** (one far call a frame); `pxsgen.py` + `t_pxsscale` (soak) + `pxsscale.py` (soak, byte for byte). Doors in the DDA with the sliding u, jambs, u mirroring. Size/Rows rows + the V key; the Mode X Detail row 80/160/320 by tier; **Detail: Flat** exposing the static ladders; regeneration on change; `PXSTEIN.CFG`. `pxssim.py` gains textures and ink. `pxsperf.py` (INSTRUMENT, skiesperf's shape, `-DPXPROBE` md5 byte-identical); `pxs160.py` (the text backend vs a forced full redraw); `pxsfsx.py` (restore equality after cycling every Mode/Size/Rows/Detail/Resolution, the Flat rung and Low res included).

Done when: screendumps on all five backends show textured walls with jambs beside the door slab and the `--preview` criterion passes on the CGA4 and Hercules renders (brick vs stone at 64 columns); `pxsscale.py` 0 differing bytes; `pxssim.py` 0/0 with textures; `pixelstein.py` ≥ 8.0 / ≥ 7.0 on CGA4 and Hercules at the default — Textured Low res at 64 × 80 (expect ~9.4 / ~8.9; **under 8.0 on scene A the rung moves to 48 × 80 Low res before the wave closes**, §96.1) — with Full 64 × 80 and 48 × 80 reported beside it; Mode X and windowed VGA reported off `_xt_vga` — if Mode X reads under 6.0 the shadow+copy arm is tried before the wave closes; the stage table (cast / walls / present / sim) filed in `docs/reports/` and PERFORMANCE.md Part 5.

### Wave 3 — sprites, guards, doors, combat — **fable**

Files: `apps/pixelstein/{pxspr.inc, pxact.inc, pxgame.inc, pxgame.asm, pxcast.inc, pxcomp.inc, pxart.inc, pxlev.inc}`, `apps/pixelstein/art/*.png` (**the guard's 5 masters**, weapon 3 × 3, 8 pickups, 6 decorations), `apps/pixelstein/levels/e1m1..e1m3.txt`, `tools/{pxsart.py, pxslevel.py}`, `tests/unit/t_pxsmap.py` (soak), `tests/{pixelstein.py, pxsact.py}`, `tests/suite.py`, `SPEC.md`.

Features: sprites — posts, the mirror table, transform, sort, the `wallh` z-test, the patched-`retf` post draw inside the driver, the 8,000-store cap, `px_touch`, part 5. The weapon over the view, hitscan, ammo, knife at 0; health/damage. Doors: slide, timers, blocked-while-occupied, locks, secret doors, the elevator switch. Guards: stand / patrol / alert / chase / attack (Wolf's hit table) / pain / die / dead; LOS = `spotvis` + a ≤ 32-step walk once a tick; the DIE fade. Pickups and statics. Three levels; `t_pxsmap`'s melee invariant and waypoints. `--preview` on the guard decides **8 vs 4 facings** here, with three waves left to act on it. `pxsact.py` (soak): a poked guard sees, turns, shoots, dies; a body keeps a door open; the two-guard frame reported.

Done when: a screendump shows a guard (facings across four headings), the pistol, an open door with jambs, a pickup; `pixelstein.py` still ≥ 7.0 / ≥ 6.0 and the two-at-melee frame reported (expect ~6 before the cap); `pxsact.py` green; `t_pxsmap` refuses three guards at one cell's melee; part 5 measures ~21 KB; `tools/os88parts.py` run < 128.

### Wave 4 — HUD, states, scores, sound, the episode — **opus**

Files: `apps/pixelstein/{pxhud.inc, pxhs.inc, pxgame.inc, pxgame.asm, pxrast.inc, pxart.inc, pxlev.inc}`, `apps/pixelstein/art/*.png` (digits, face, icons), `apps/pixelstein/levels/e1m1..e1m8.txt`, `tools/{pxsart.py, pxslevel.py}`, `tests/unit/t_pxsmap.py`, `tests/{pxsstate.py, pxshud.py}`, `tests/suite.py`, `README.md`, `SPEC.md`.

Features: the status bar (24 rows; 20 on CGA16): floor, score, lives, health, ammo, keys, weapon, 6-frame face, 16×24 digits, `OSAPI_FONT_GLYPHS` labels through per-backend glyph writers; change-only; per-page dirty on Mode X; the windowed `font_run` line. The seven states, READY and LEVELDONE cards, pause, sticky auto-pause, restart at bracket entry; **ENTER on the UI task windowed**. `PXSTEIN.HS` (tkhs.inc's shape, `FILE_GOTO` never `_Q`, magic `'PX8',1`); 4-letter floor passwords. Sound through `OSAPI_SND_TONE` only (the draft's list), Sound/Mouse menu items. Eight levels. The About card's full text. `pxsstate.py` (walks the states in BOTH worlds, reads the score back after a restart); `pxshud.py` (zero HUD rewrites over a quiet second).

Done when: screendumps of the bar on CGA4, Hercules and Mode X; the two cards photographed; `pxsstate.py` green in the bracket and windowed with the file read back; `pxshud.py` green; `t_pxsmap` green on eight levels; `pixelstein.py` unchanged; the run still < 128 and the package < 56 KB.

### Wave 5 — WIN4 and the two-display gates — **fable**

Files: `apps/pixelstein/{pxrast.inc, pxwin.inc, pxcomp.inc, pxgame.asm}`, `tests/{pxswin.py, pxsmd.py}`, `tests/suite.py`, `SPEC.md`.

Features: WIN4 — the Mode X set expanded through a 32→16 table into four 25-row 4bpp strips in the shadow's spare 6,400 B, four `OSAPI_GFX_BLIT4`s, default on `CPU_286`+ with VGA, greyed with the price on an 8088. `pxswin.py` (a window MOVE costs zero repaints; the band lands 8-aligned; pen vs 1bpp identical bits; a seam drag takes the right byte set); `pxsmd.py` (a bracket changes its own card only, QEMU two-card).

Done when: a screendump of WIN4 on the 386 QEMU shows 16-colour walls in a window and the same window on an XT-VGA shows the greyed item with its price; `pxswin.py` and `pxsmd.py` green; `pixelstein.py` unchanged.

### Wave 6 — the dog, the map, the machines, the numbers — **opus**

Files: `apps/pixelstein/{pxact.inc, pxspr.inc, pxgame.inc, pxgame.asm, pxart.inc}`, `apps/pixelstein/art/*.png` (the dog, polish), `tools/pxsart.py`, `vm/xt-pixelstein/86box.cfg`, `vm/xt-pixelstein-herc/86box.cfg`, `Makefile` (the two targets), `README.md`, `SPEC.md` (§96's measured numbers), `PERFORMANCE.md`, `docs/reports/PXS-FRAME-<date>.md`, `docs/INDEX.md`, `tests/{pxsact.py, suite.py}`.

Features: the dog (4 facings × 2 walk + bite + die; melee, fast); the Tab overhead map of seen cells into the view, `px_touch`ed off. `vm/xt-pixelstein` (xt-cga + `games360.img`) and `vm/xt-pixelstein-herc` (xt-hercules + `games360.img`); `make xt-pixelstein` / `xt-pixelstein-herc`; README says which target shows which backend. The final frame table from `pxsperf.py` on all four MartyPC machines into §96, PERFORMANCE.md Part 5 and the report; the About card's numbers; the spotlight capture recipe; a follow-up note for the os8088.com Wire entry. The full gate run.

Done when: screendumps of the same corridor on CGA4, CGA16, Hercules, Mode X, WIN1 and WIN4 beside `--preview`'s renders; `make xt-pixelstein` boots 86Box to the game; the dog bites in `pxsact.py`; the map photographed; `pixelstein.py` green on all four machines with the numbers matching §96 within 5%; `make`, `make test-full`, `soak -k 'pxs*'` green; every shipped image `--verify`'d, `PXSTEIN.O88` on `games360.img` and `apps.img`, not on `apps360.img` or the small disks.

---

## 8. Tests (the registry)

| row | tier | asserts |
|---|---|---|
| `pxs-gen` | **fast, 0.3 s** | regenerated `pxtab.inc` / `pxart.inc` / `pxlev.inc` hash-equal to the committed copies (t_paccman's mould) |
| `pxs-level` (`t_pxslevel`) | soak | every level passes every rule with the DDA sweep in both door states, plus the negative control (a 40 × 40 open hall refused in words) |
| `t_pxsscale`, `t_pxssim`, `t_pxsmap`, `t_pxsart` | soak | the scaler byte image, the reference frames, the sampled DDA sweep + level rules, the material count and key rule — "ONE package, beside a change to it: `soak -k 'pxs*'`" |
| `pixelstein` | soak, MartyPC × 4 | draws / advances / no flash; **fps ≥ 7.0 (A) / ≥ 6.0 (B) fullscreen, ≥ 6.0 / ≥ 5.0 windowed** |
| `pxssim`, `pxsscale` | soak | guest arrays + shadow == host; guest part 3 == `pxsgen.py` |
| `pxs160`, `pxsfsx`, `pxsdisk` | soak | Δ-fill ghost gate; restore equality over every setting incl. Low res; the disk membership and packed size |
| `pxsact`, `pxsstate`, `pxshud`, `pxswin`, `pxsmd` | soak | as above |
| `pxsbench`, `pxsperf` | instruments, `wants=` | the unit costs; the staged frame |

---

## 9. Verification

`make` (the fast tier incl. `pxs-gen`; the four every-shipped-image gates walking `games360.img`); `make test-full` alone; `python3 tools/os88test.py soak -k 'pxs*' --marty-jobs 1`; `make bench && python3 tests/pxsbench.py` on `_cga_gla` / `_herc_gla`; `python3 tests/pixelstein.py --machine … [--windowed] --scene a|b`; `python3 tests/pxsperf.py` per backend; `python3 tools/os88disk.py --verify` on every apps image with cluster counts quoted (`apps-all.img` spare ≥ 15); `python3 tools/os88parts.py build/pxstein.o88` run < 128; `tools/kernsize.py` unchanged; `pxsart.py --preview` LOOKED AT on CGA4 and Hercules; `make xt-pixelstein` / `xt-pixelstein-herc` / `xt-ega` / `386` screenshots per claim; `stkdepth.py` on the worker's deepest chain; `grep -c` two patch sites in part 3's generator, each restored in its owner; `tests/textsites.txt` unchanged; `checkdocs.py` clean; the fast tier's declared total quoted (32.2 s of 30 after wave 0, pre-existing — §96.10).

## 10. Risks

1. The unit costs are DERIVED until wave 0 measures them; a 2× miss on the store is the 48 × 80 rung, a 2× miss on everything is ~5 fps and a redesign of the promise, not the engine.
2. The DDA body's 130 rests on patched immediates in part 3; if row (c) reads 155 the cast is +10,000 and the table says so.
3. Two patch sites in generated code in a pinned claim; a patch left by a fault path is a wrong picture — `pxsscale.py` between frames and the "restore before any `jc`" rule.
4. **The contiguous parts claim** (~148 KB on the dither backends, 163 on VGA, after wave 0's reprice — §4.2; 178 under the word-load fallback): a fragmented heap refuses it whole; `OP_OPT` on 3/4/5 degrades to Low, and a second instance is refused in words.
5. **The 128-sector run**: part 0 at ~100 sectors has 27 spare; the build asserts it, but growth past 127 fails at LAUNCH.
6. The image at ~60,000 of 61,440 without the cold part (§4.1, repriced: the two map layouts), ~54,000 with it; the cold part is therefore wave 1's planned shape and is exercised from the first menu.
7. `apps-all.img` at 127 spare clusters: this package leaves ~9 KB, so the NEXT package is a §19.10 curation decision; the 56 KB assert is what keeps wave 6's art from eating it.
8. Two guards at melee ~6 fps before the cap; the frame to photograph on the 5150.
9. Mode X on an XT-VGA priced at 7.5 (quantised) against §88.12's 4.3 for a run renderer; wave 2 measures and the shadow+copy arm is the fallback.
10. CGA16 snow on a genuine IBM CGA — accepted, second item.
11. Δ-fill ghosts from any writer bypassing `px_touch` — the forced-full-redraw diff.
12. Pixel aspect on CGA4 (9.6:1 per byte-row) — `--preview`'s losable criterion in wave 2, and CGA16 one item away.
13. Two-display desktops — depth from `WM_DISPLAY`, caps on `W_ONRESIZE`; `pxsmd.py`.
14. The art remains the schedule risk, now spread over waves 2, 3 and 6 with the theme asked first.
15. The small-disk omission's ground is a claim about the arena (52.5 KB / 20 KB run); a kernel change to either reopens it (`soak -k smallreq`).
16. The fast tier is at 31.9 s declared of 30; one 0.3 s row is added and the total is quoted in wave 0.
17. §96 is taken mid-wave on a long branch.
18. Eight levels of original content the arithmetic does not cover.
19. The Hercules flip and the small arm are both decided by arithmetic, not measurement; both are written as reopenable with the measurement that would reopen them.

## 11. Questions for the user

1. **Commit the PNG masters** under `apps/pixelstein/art/` (CONTRIBUTING.md §6; the C64-ROM class of decision)? Recommended yes.
2. **The XT default rung**: 64 columns × 80 rows at ~7.9 fps (D), or 48 × 80 at ~9.7? Both ship on the Size row; this is what a new player sees first. Recommended 64 × 80 — Wolfenstein's own 320-ray look at a fifth of the rays still reads as a wall, and 48 is one V press away.
3. **The theme**: what are the walls and who are the guards? The plan ships an original setting ("the Pixelstein" — a castle of stone, wood and iron with helmeted guards and dogs) and nothing of Wolfenstein's own art or iconography. Needed before wave 2 fixes 15 materials.
4. **360 KB placement**: `games360.img` only (recommended, §24.6.1's dated decision) — or displace a package from `apps360.img`.
5. **Content scope**: 8 floors + passwords, guard + dog, no saves, the officer a follow-up (recommended) — or 10 floors + a 6 KB save file and the officer staged in wave 6.
6. **The three kernel gaps, with prices**: (a) a clip-honouring `gfx_blitp` (16 colours in a window on an XT — ~150 bytes of `.text` in `kernel/vga12.inc` and a §5.4.2 rule; §5.4.2.5.1 is the precedent for taking a kernel change for a package), (b) `OSAPI_MOUSE_REL` for mouse-look (a slot, ~60 bytes, and a driver-side delta), (c) nothing for the text-mode flip (settled: 16,000 of 16,384 bytes). Recommended: none in this PR, all three named in §96.11.

---

## 12. Decisions (taken 2026-09-13; the questions above are closed)

1. **PNG masters are committed** under `apps/pixelstein/art/` (the C64-ROM class of decision, CONTRIBUTING.md §6 departure stated in §96).
2. **The XT default rung is 64 × 80** until wave 0's measurement says otherwise; 48 × 80 ships on the Size row and is what the gate falls back to if 64 misses ≥ 7.0 / ≥ 6.0. **Wave 0's measurement said otherwise** (§96.1, 2026-09-13): the column setup and the hit each measured 2.1× their D and 64 × 80 Full converges to 6.3 on scene A with the rotate. **The user's direction is to rely on the lower resolution at the full width**, so the default is **Size 64 × Rows 80 × Resolution: Low res** (9.4 / 8.9 on the measured units with the sim tick measured), the promise on it is **≥ 8.0 / ≥ 7.0**, Full at 64/56/48 is on the Size row offered and reported, wave 1's Flat Full 64 × 80 frame is filed as a calibration, and wave 2's measured Textured Low res is the fork: under 8.0, the rung moves to **48 × 80 Low res**.
3. **The theme is original**: "the Pixelstein" — a castle of grey and blue stone, wood, brick and iron; helmeted guards and dogs; nothing of Wolfenstein's own art, names or iconography. Materials are named in wave 2 from `--preview`.
4. **360 KB placement: `games360.img` only** (§24.6.1's dated decision); `apps360.img` is not reopened.
5. **Content scope: 8 floors + 4-letter floor passwords, guard + dog, no save file**; the officer is a follow-up.
6. **No kernel change in this PR.** The three gaps (clip-honouring `gfx_blitp`, `OSAPI_MOUSE_REL`, the text-mode flip) are named in §96.11 with their prices and not taken.
7. **8087: not taken, by arithmetic.** The frame's only `mul`/`div` are per COLUMN (1 `div` 163 clk + 4 `mul`/`imul` ~530) ≈ 700 clk × 64 columns ≈ 45,000 of a ~600,000-clock frame (7.5%), and per sprite transform (≈ 1,000 × ≤ 8). An 8087 `FMUL` is 130–145 clk and `FDIV` 193–203, before the `FILD`/`FIST(P)` transfers (50–90 each) through memory that every operand and result would pay on an 8-bit bus — so the coprocessor is slower per operation than the integer unit it would replace, and there is nothing in a per-pixel loop for it to touch (no `mul`/`div` there by design). The option would cost a run-time probe, a second code path and a test machine, for a frame that gets no faster. Re-open only if a per-column reciprocal table (`px_h[]`, the wave-2 micro-option) is refused for size and the `div` count rises.
8. **Language: NASM 8086 assembly**, not C (PACCMAN measured 2.18 fps against PACMAN's 4.14 on the XT).
9. **Technique references** (read for technique, never copied — the game's art and code are original): `WolfensteinCGA` (jhhoward; the Wolf3D engine modified to run on 8088 and CGA/mono) and `cubicDoom` (nanochess; an 8086 boot-sector raycaster), both cloned read-only under the session scratchpad; the planner's own research in `/tmp/pxs-reports/research-codex.md` and `scout-research.md`.
10. **Models per wave** as §7 names them (fable on 0/1/2/3/5, opus on 4/6); Codex reviews each wave's diff read-only as a fourth lens.

---

## 13. Grafts from WolfensteinCGA (2026-09-13; the critique is `/tmp/pxs-reports/graft-wolfcga.md`)

James Howard's WolfensteinCGA is Wolf3D's engine converted MECHANICALLY for the 8088 — every `shl r,n` unrolled, one ray per CGA byte, an 80-stride RAM back buffer — with the 286 trace (six shifts a crossing, 16.16 intercepts in memory, a C far call per hit), a `spotvis` clear and a whole-view clear every frame, and a `rep movsb` present. The plan already does better on each of those, and its field figures are the calibration the plan lacks: **~3.6 fps full screen / ~5.8 at a 48×96 view on a 7.16 MHz V20**, "around 2" on a 5150 — which converts to **~2.5–2.9 fps at 90 % of this plan's default cell count on a 4.77 MHz 8088**. The plan promises 7.9 at 64×80, a 2.9× per-cell claim; the renderer-side savings account for ~12 % of Wolf's frame and the rest is either Wolf's C runtime, ISRs and paging (which this package has none of) or a unit-cost shortfall the plan would share. **Nothing in Wolf distinguishes the two; wave 1's flat-wall frame on `_cga_gla` does** (the plan expects ~8.3): **under ~6.5 the rung moves to 48×80 BEFORE wave 2 writes a generator around 64**. That is a fork, not a checkbox. *Re-derived by wave 0 on the converged table* (§96.1): the default is Size 64 × Rows 80 × Resolution: Low res on the user's direction; wave 1's Flat Full 64 × 80 frame is FILED against the table's 8.1 (a calibration of the units, no longer a rung decision), and wave 2's measured Textured Low res frame is the fork — under 8.0 on scene A, the rung moves to 48 × 80 Low res before wave 2 closes.

Taken, in order of value (W: = WolfensteinCGA file:line; the critique has the arithmetic):

1. **(wave 2, generator)** Each texel run is emitted as `mov al,[es:si+v]`, the even-row stores, `ror al,1`×N, then the odd-row stores (W:WL_SCALE.C:294-376) — the odd-row dither phase, without which a "2×2 dither" is vertical stripes. **N is the pixel format's, not a taste** (W:WL_SCALE.C:76-95's `dithershift`): **2 on CGA4** (one 2-bit pixel; the "N = 1" this graft first carried shifts by half a pixel and is a colour fault), **4 on CGA16** (the attribute nibble pair), **3 on HERC/WIN1**, 0 on Mode X/WIN4. Wave 0 measured the rotate **as the source does it**: two `ror al,1` on CGA4 at +18.0 a texel (76,000 a frame at Full 64 × 80, 32,000 at the Low-res default), one `ror al,cl` with CL = 3 on Hercules/WIN1 at +15.2 (64,000 / 27,000), none on C160 (16 solid colours — the "4 on CGA16" this graft first carried was a rotate of nibbles that hold two COLUMNS, not two rows) and none on Mode X — and **the rotate is TAKEN** (§96.3), part 4 staying one byte a texel. The dual-phase word load — `mov ax,[es:si+2v]`, AL the even rows' byte and AH the odd rows', +4.8 a texel for part 4 doubled to 60 KB — is the **named fallback**, one sentence in §96.3 and one in the generator, if wave 2's measured frame wants what it buys back — 56,000 a frame at Full 64 × 80, only ~7,600 at the Low-res default, where the split `AX` costs two duplications.
2. **(wave 2, `pxsgen.py`)** For every scaler height h emit `col2tex[w(h)]` — the source column each of the w(h) = round(h × K_backend) covered screen columns takes, from the aspect-UNcorrected height the way Wolf's `width[]` is built (W:WL_SCALE.C:193-208) — so wave 3's post walk has a table to read (~2 KB of part 3).
3. **(wave 1/2)** Delete the "clipped wall" `retf` patch: the 1.5·H set is generated with off-view texels emitting no load and off-view rows no store (W:WL_SCALE.C:243-260), any taller height clamps to the last scaler (W:WL_DRAW.C:475-477), leaving ONE patch site (sprite posts) in part 3.
4. **(wave 3, `pxspr.inc`)** An ACTOR is a candidate when its own cell OR any of its eight neighbours carries the frame's generation and is open (W:WL_DRAW.C:1193-1208); statics stay own-cell. +6,000 clk.
5. **(wave 2, `pxcast.inc`)** Jambs decided at hit time: on a solid hit, if the cell the ray came FROM (the un-stepped SI/DI) is a DOOR cell, that face takes material 15 (W:WL_DRAW.C:561-568). "The two cells beside a door show material 15" is withdrawn from §1.2 and from `pxslevel.py`.
6. **(wave 2, `pxsart.py`)** Seed the CGA4 ink table from cgaify's 21 four-pixel patterns (W:tools/cgaify.cpp:54-84, matched with gamma 1.5) and the 1bpp ladder from its nine 8-bit levels (0xff,0x7f,0x77,0x57,0x55,0x15,0x11,0x01,0x00); `--preview` culls.
7. **(wave 4, cold part)** A `timedemo`: from ATTRACT, replay the two pinned scenes' input scripts for a fixed frame count unthrottled and print frames / ticks / fps on the card (W:WL_PLAY.C:466-479, 1500-1514), so a 5150 owner can report the About card's number without MartyPC.
8. **(§96.1)** State the ray origin — at the eye, or `FOCALLENGTH` behind it as Wolf (W:WL_MAIN.C:29) — and the sprite forward fudge (`ACTORSIZE`), since both change `MINDIST`'s meaning. Decision: **the eye**, `MINDIST` 0.09 as planned, unless wave 1's photographs show a touched wall blowing the height clamp on every column.
9. **(wave 2)** Scalers step by 6 rows above H instead of 3 (W:WL_SCALE.C:119-127), directory entries aliased: ~6.5 KB off the contiguous claim.
10. **(wave 2 micro-option)** One `imul` by a 2 KB `1/cos` quarter table (cubicDoom/doom.asm:182-191) instead of the two-`imul` rotation: −9,000 clk a frame.
11. **(§96.11, not this PR)** Two numbers beside the withdrawn items: an unwaited Hercules flip (W:ID_VL.C:1611-1642) is 8.7 fps against the shadow's 7.9; a composite-CGA backend (W:ID_VL.C:1505-1512) is CGA16's byte set with CGA4's present and no snow. Both are one kernel row away.
12. **(§96)** One sentence on the sim cap: `tk_steps`' cap of 3 means below ~6 fps the GAME slows rather than the frame growing, so the ≥ 6.0 scene-B promise is exactly where the cap starts to bind; Wolf's timedemo charges a fixed 4 tics a frame, so its field figures are kinder than real play and the plan's `pxsperf` numbers are the stricter ones.

Skipped: cubicDoom's step-marching ray (wrong for a DDA design), its quarter-sine `xlat` (the tree has `tk_pshift` and the quarter tables), Wolf's second Hercules CRTC mode (the OS owns the mode).

---

## 14. The detail ladder — graceful degradation (user decision, 2026-09-13)

The user: *"build graceful degradation into the 3D model so that on slower hardware you can rely more on simple lines and polygons and faster hardware can use more sophisticated graphics."* The plan's Detail/Size/Rows/Mode-X-rays rows are already rungs; this section makes them ONE LADDER with a rung below Flat and an automatic selector, and it binds every wave from 1 on.

| rung | what a column costs | who gets it by default | needs |
|---|---|---|---|
| **0 Wire** — edges only, TANK's vocabulary: the wall's top and bottom edge pixel per column, a vertical line from `top[c-1]` to `top[c]` (and bottom) where the height CHANGES between neighbouring columns (a corner or a material change), the door slab outlined; ceiling and floor untouched (the shadow's flat tones, written once); sprites as outlined boxes with the facing glyph; no shading | ~2 stores + the Δ edge (mean ~8) ≈ 10 stores ≈ 250 clk against Flat's ~32 stores + Δ-fill ≈ 1,000 — **the cast (156,500) dominates, so Wire is ~1.4× Flat's frame, not 4×**; where it wins outright is the PRESENT: rows that hold no edge are not dirty, so the row range collapses (a corridor is a few dozen dirty rows) — on CGA4 the 103,000-clk copy falls to ~30,000 | nothing by default; **what Auto steps down to** on a machine that misses the budget, and the arm the gate runs at 48×80 as the floor number | nothing: image-resident, no part 3/4/5, no art part — it is ALSO what a refused `OP_OPT` part and a refused art part fall to, so it is exercised on every machine, and it is the rung the 128 KB floor would run (§4.3's door) |
| **1 Flat** — solid lit/dark faces through the static ladders (§1.3) | as §3 | Auto's second step down | nothing |
| **2 Textured** — the compiled scalers, 32-texel columns, dither phase (§13, graft 1) | as §3 | **8086 (the XT): 64 × 80 at Resolution: Low res, Auto** (§16, §96.1) | parts 3/4/5 |
| **3 Textured + Rows 100 / Size 72–80** | ×1.25–1.6 | **286**: 64 × 100 | parts |
| **4 Mode X 160 rays** (mask 03h+0Ch, two rays a byte) | ×2 cast | **286/386 VGA** | Mode X |
| **5 Mode X 320 rays** (per plane), WIN4 in a window | ×4 cast | **386+ VGA** | Mode X / VGA desktop |

**Auto** (the default of the Detail row; every rung is also pinnable by name, and a pinned rung never moves): at launch the rung is chosen from `OSAPI_CPU_INFO`'s tier (a fact, §47) — 8086 → 2, 286 → 3, 386 → 4 (5 in Mode X); then the frame is TIMED (`OSAPI_GET_TICKS` across 8 frames; `pit_now` from `apps/os88pit.inc` where sub-tick resolution is wanted) and **steps DOWN one rung when eight consecutive frames miss the budget** (the budget is the rung's promise: 7.0 fps for rung 2 on the XT) and steps back UP only after 64 consecutive frames under 60 % of the budget, so the ladder does not hunt across a doorway. A step is announced once on the HUD line ("Detail: Flat") and never mid-frame: the switch happens between frames, `px_touch`es every column so the Δ-fill repaints, and regenerates nothing (part 3 stays; rungs 0–1 do not use it). What a slower machine relies on is therefore lines, then flat polygons, then textures, exactly in that order, and the user's setting persists in `PXSTEIN.CFG` as the pinned rung or Auto.

**Faster hardware** takes the top rungs, and two follow-ups are named for it in §96.11 rather than built here: 64-texel textures (a second art part, chosen by rung ≥ 4 — the scaler load count doubles on tall walls, which a 386 does not notice) and a second distance shade on Mode X (the DAC has the entries). Floor/ceiling texturing is NOT on the ladder: it is per-pixel work with a divide per row and would be seconds on the XT and slow on a 286; it is a different engine.

**What this changes in the waves.** Wave 1 builds rungs 0 and 1 and the selector (Auto by tier + the timed step-down, the Detail row with Auto/Wire/Flat and the greyed Textured until wave 2), and the gate measures BOTH rungs on `_cga_gla` and `_herc_gla` — Wire is the floor number that goes in §96 beside Flat. Wave 2 adds rung 2 and the step between 1 and 2 (no regeneration; part 3 is resident from launch). Wave 5's WIN4 is rung 5's windowed face. `tests/pxsfsx.py` cycles every rung; a new soak row `pxsauto.py` pokes the frame clock slow and asserts the rung steps down once, announces once, and steps back up after the quiet run. The reference renderer `pxssim.py` renders rung 0 and 1 too (an edge-only PNG is the cheapest column-by-column check of the cast there is, and it is what wave 1 diffs first).

**Wire is off Auto's ladder (the user's decision on wave 1's measurement, 2026-09-14; SPEC.md 96.8).** Auto steps Textured Full → Textured Low res → Flat Full → Flat Low res and stops there; Wire stays rung 0 — a pinnable Detail item, a picture trade of edges only, and the rung a refused part falls to — but never Auto's. Measured on the 5150 (`docs/reports/PXS-FRAME-2026-09-14.md`), Wire's turning draw is **76.4 ms against Flat Low res's 68.7** and its full repaint **106.4 against 85.5** (76.5 against 66.7 and 87.0 after SPEC.md 96.5's two-ends arm, which Wire does not take), and the order is the same windowed, on the C160 retime and on Mode X. The stage split says where: the cast and the present are identical between the two rungs (the same 32 rays, the same 80-row band — the present never collapses on any frame a player sees, SPEC.md 96.5), and Wire's COMPOSE is the dearer one — 24.6 ms against Flat's 18.4 turning, 51.4 against 31.7 on a full repaint. **The premium is a routine, not the picture** (SPEC.md 96.8 has the counts): on the frame measured Wire's drawing is the cheaper half — 77 ladder entries and 309 rows + 20 direct stores against Flat's 53 and 1,082, ~2.5 ms LESS — and the +6.2 ms is ~1,300 clk a column of bookkeeping in `px_wrun`, `px_ground` and the neighbour test, removable without moving a pixel; so the decision is CONTINGENT on that routine's cost, and a cheaper `px_wrun` re-opens the floor. The table's "~1.4× Flat" above was the store arithmetic without the cast's share, and its "where it wins outright is the PRESENT" was wrong for both scenes and for every scene in E1M1. A floor slower than the rung above it — one that could never climb back, 76.4 being over the 62.5 ms step-up line — is a trap, not a rung. The user's intent survives it: the ladder still degrades to simpler pictures, textures first, then flat faces, then fewer rays, and Wire is one menu pick away. This supersedes rung 0's "what Auto steps down to" cell above and §16's order line, and `tests/pxsauto.py`'s step-down leg lands at Flat Low res.

---

## 15. Where the XT is measured (user decision, 2026-09-13)

The user: *"rely on a lower resolution full screen mode to benchmark the XT class PCs. XTs on windowed mode may be asking too much."* So:

- **The XT's PROMISE is fullscreen, in the low-resolution modes**: CGA4 (`FSXM_CGA320`, 320×200×4 — the lowest graphics mode a CGA has) and the Hercules box, at the default rung, on the two pinned scenes: **≥ 7.0 (A) / ≥ 6.0 (B)**, exactly §3's fullscreen line. CGA16 (160×100×16, the lowest-resolution mode of all) is measured and reported as the second CGA item; it is not the gate because of the snow question on a genuine IBM CGA.
- **Windowed on an 8086 is REPORTED, not promised.** `tests/pixelstein.py --windowed` still runs on `_herc_gla` and files its number in §96 and the report, but it is not a pass/fail row on the 8086 tier — the windowed frame carries the kernel's blit, the lock round trip and the arrow, and that tax (3.8 ms mono / 6.7 ms VGA, M) is the OS's, not the game's. The windowed promise moves to the 286 tier (measured on QEMU's 286 profile under `-icount` as a reported number, gated only when a MartyPC 286 exists — there is none, so it is a reported number everywhere).
- **Windowed Auto starts one rung lower on an 8086** (rung 1, Flat) and steps down to Wire freely; the attract page on an 8086 says "Press F for full screen" on its first line, so the player is told where the machine plays best. Fullscreen Auto on an 8086 starts at rung 2 as §14 says.
- The gate's floor numbers (Wire and Flat at 48×80, fullscreen) are what §96 quotes for "the slowest it can go"; a field report from a 5150 is read against the fullscreen CGA4 line first.

---

## 16. Low resolution as a Detail item (user decision, 2026-09-13)

The user: *"rely on lower resolution full screen. Make it an option in the graphics detail menu."* So the Detail menu carries a RESOLUTION axis beside the rung axis, and the two together are the ladder:

- **Detail ▸ Resolution: Full / Low res.** Full = one ray per shadow byte (64 rays across the 256-px CGA4 view; the plan as written). **Low res = one ray per TWO shadow bytes** — 32 rays across the SAME view, every column drawn 8 CGA pixels (16 Hercules dots, 8 Mode X pixels) wide. The scaler set for Low res is generated with a WORD store per row (`mov [di+r×80],ax` after a `mov ah,al`, 28.3 + 9.6 clk M on the 8088 — one instruction where Full uses one, so a Low-res frame's STORE count per screen area is ~58 % of Full's) and the Δ-fill and present are unchanged (the same bytes reach the glass). **Wave 0 wrote this as the XT DEFAULT** (§96.1, the user's direction): Size 64 × Rows 80 × Low res, the promise ≥ 8.0 / ≥ 7.0 on it, 9.4 / 8.9 on the measured units; the name is "Low res" throughout, so that "Low" alone never names a rung. The cast halves (156,500 → ~78,000), the texel loads halve, the sprite posts halve. Rows stay 80 (a Low-rows arm — 40 texel rows doubled — is NOT taken: the scaler already emits one store per screen row and halving the loads saves ~2 % of the frame for a picture that reads as bands).
- **Priced (D, CGA4, scene A)**: Textured Low res ≈ 602,000 − 78,000 − 49,000 − 20,000 (stores at 58 %) ≈ **455,000 = 10.5 fps**; Flat Low res ≈ **11.5**; Wire Low res ≈ 12+ (cast-bound). *Wave 0's converged table (§96.1) reads 9.4 / 8.9 Textured Low res with the rotate and the measured sim, 12.7 Flat Low res.* `tests/pixelstein.py` reports Low res on every machine it runs on (fullscreen; §15).
- **Auto's order** (§14 amended): Textured Full → **Textured Low res** → Flat Full → Flat Low res → Wire — and on the 8086 Auto STARTS at Textured Low res, the default. Halving the rays is the first thing a slow machine gives up, because a wall that is still textured at 32 columns reads better than a flat one at 64 (Wolf's own `viewsize` trade). The pinned rung and resolution persist in `PXSTEIN.CFG`.
- **The menu, in full**: *Mode* (the adapter's fsx items, §2) · *Detail ▸ Auto / Wire / Flat / Textured* (Textured greyed with its fact when part 3/4/5 is refused: "Textured: needs 80 KB free") · *Resolution ▸ Full / Low res* · *Size* (view width 48–80 columns, §2) · *Rows* (80/100, where the surface allows) · *Sound* · *Mouse*. Every item is a fact-greyed §47 control; none is a guess. **Folded into four cells by wave 1's review** (SPEC.md 96.8): `MENU_APPMAX` is 5 and the bar drops cells from the clock band, so the row set is *Game* (Full Screen, Pause, Sound, Mouse) · *Mode* · *Detail* (one pull-down, two axes: Auto / Wire / Flat / Textured and Full res / Low res) · *View* (Size, Rows); the Resolution items live under Detail, and wave 1 ships the first three cells.
- **Part 3 holds BOTH scaler sets** (Full's byte-store set and Low res's word-store set, ~30 + ~30 KB → the contiguous claim grows by ~30 KB, ~162 KB / 177 on VGA — repriced by wave 0 at ~37 KB for both, §96.9) so a resolution switch regenerates nothing and Auto can step between frames; if the claim is refused whole, `OP_OPT` degrades to Flat/Wire as before, where resolution is a store-width flag on the static ladders (a second static ladder set, +1.3 KB image). `pxsgen.py` models both sets; `pxsscale.py` diffs both. Wave 1 builds the Resolution row for the static ladders (Wire and Flat, both widths); wave 2 adds the word-store textured set.
