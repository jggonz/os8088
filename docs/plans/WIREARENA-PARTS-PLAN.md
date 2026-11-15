# WireArena in parts: what goes in which segment

**STATUS: BUILT** (section 8, 2026-11-13; reconciled 2026-11-15): the
renderer is part 0, the figures' cold keyframes a part of their own (8.6),
and each arena a lazy part (7.5). Sections 1 to 7 are the plan as it was
argued. This document answers the owner's note of 2026-11-13:

> *"I really doubt this entire game will fit in the remaining few kb. We can
> optimize for size later too, but almost certainly we are going to need
> more than one part."*

WireArena is `apps/wirearena/`, the text-mode arena shooter of
[MULTIPLAYER-FPS-PLAN](MULTIPLAYER-FPS-PLAN.md). This plan was written on
branch `parts-plan`, cut from `multiplayer-fps` at `d30f8d30` (the `P` play
bench, MULTIPLAYER-FPS-PLAN §16.7). Five agents are tuning `wirearena.asm`
and `wrender.inc` in parallel, so the migration in section 4 starts with a
step that cannot conflict with their work.

Claims are marked as in the parent plan:

- **Measured**: a number was taken, and the method is given.
- **Derived**: arithmetic from SPEC.md or PERFORMANCE.md unit costs. Nothing
  ran it.
- **Forecast**: a size range for code nobody has written yet, set against
  shipped code of the same kind.

**The verdict, short.** The owner is right. One package segment holds
65,024 bytes (SPEC.md §20.1.1). WireArena uses 59,004 of them today. The
remaining features are forecast at **+35 to +80 KB**, so the game needs
more than one segment.

The proposed split follows the one boundary the code already has:

- **The renderer** becomes a far-called code part with its own data segment
  (part R). It carries the renderer's 36 KB of tables and buffers with it.
- **The game** stays in the primary segment (G): input, simulation,
  network, AI, weapons, sound, HUD and lobby.
- **The animation keyframes** become an asset part.
- **Each arena** becomes a lazy asset part, fetched when a match starts.

G calls R twice a frame. That costs about 0.2% of a frame (derived). Copying
the world state across the boundary costs another 0.5-1%, and section 3.4
gives a way to cut it.

The parts standard already provides every mechanism the split itself needs,
and XTMP3's FRONT part is a working example of the exact shape (section
3.2). Section 3.5 lists what the standard does not cover: two network
questions for later waves, and none that blocks the split.

---

## 1. Today's byte budget (measured)

**Re-measured at `7232b890`** (performance pass 4 merged,
MULTIPLAYER-FPS-PLAN §16.8), which is the tree the split starts from. The
first edition of this section measured `d30f8d30` (59,004 bytes); pass 4
took 7,680 bytes of BSS out of the presenter (`wa_vis` gone, `wa_fmask`
folded into `wa_stamp`'s odd bytes) and spent ~1 KB of code.

**How it was measured.** `make wirearena`. `build/wirearena.bin` is 23,226
bytes of image, and the header declares 29,383 bytes of BSS: **52,609 bytes**
against `APP_MAX_SIZE` = 65,024 (`tools/os88pkg.py:61`, SPEC.md §20.1.1).
That leaves **12,415 bytes**. The packed `WARENA.O88` is 16,238 bytes (LZB).

The code was split by area using a NASM symbol map of a wrapper that
`%include`s `wirearena.asm` unchanged. The wrapper's output was
`cmp`-identical to `build/wirearena.bin`. The span between consecutive
top-level labels was charged to the file that defines the first label.

The BSS was split by area by expanding the `WA_VAR` chain with a `%warning`
that prints each variable's offset and size. The sizes sum to the header's
29,383 exactly.

### 1.1 Image: 23,226 bytes

| area | file | bytes |
|---|---|---:|
| header | `OS88_HEADER` | 32 |
| shell: entry, window paint, menu command | `wirearena.asm` | 147 |
| the bracket loop `wa_main` | `wirearena.asm` | 257 |
| mode set, FOV, arena load (`wa_setmode`, `wa_setfov`, `wa_loadarena`) | `wirearena.asm` | 393 |
| reset, spawn, animation start | `wirearena.asm` | 157 |
| input (`wa_frame_input`, `wa_keys`, `wa_kmark`) | `wirearena.asm` | 440 |
| simulation (`wa_tick`, `wa_collide`, `wa_sincos`, `wa_figtick`, `wa_fire`) | `wirearena.asm` | 888 |
| bench `B` and play bench `P` | `wirearena.asm` | 600 |
| HUD meter and digit output | `wirearena.asm` | 412 |
| **game and shell code, total** | | **3,294** |
| **renderer code, total** (`wrender.inc`: geometry, the box order cache, spans and the flat passes, floor grid, figures, crosshair, `wa_present`) | `wrender.inc` | **8,203** |
| `pit_now` | `os88pit.inc` | 61 |
| sine table `wa_sin` (1,024 Q14 words) | `build/watab.inc` | 2,048 |
| two arenas: coordinates, vertices, corners, BSP, boxes, colours, directory | `build/watab.inc` | 719 |
| three patrol paths | `build/watab.inc` | 124 |
| **stick-figure keyframes `wa_pose`** (23 frames × 16 facings × 11 joints × 2 bytes) | `build/watab.inc` | **8,096** |
| window template, menus, help text (391), HUD template | `wirearena.asm` | 573 |
| renderer glyph and attribute tables (`wa_hgl`, `wa_fglyph`, `wa_attrc`, `wa_attrm`) | `wirearena.asm` | 76 |

### 1.2 BSS: 29,383 bytes

| area | owner | bytes |
|---|---|---:|
| four cell buffers, 1,920 cells × 2 each: `wa_stamp` (the figure mask in its odd bytes), `wa_cur`, `wa_lista`, `wa_listb` | renderer and present | **15,360** |
| `wa_rcp`, the 1/z reciprocal table (MULTIPLAYER-FPS-PLAN §16.6) | renderer | **5,764** |
| `wa_fbuf`, the figures' cell-mask scratch | renderer | **3,840** |
| per-column arrays (`wa_colz` … `wa_chb`, `wa_fvis`) | renderer | 1,444 |
| vertex caches, per-frame products, the box order (`wa_lord`) | renderer | 1,052 |
| span table | renderer | 768 |
| arena working tables (`wa_xv`, `wa_yv`, `wa_vtx`, `wa_corn`, `wa_boxes`, `wa_bcol`) | renderer and collision | 255 |
| row tables (`wa_rowoff`, `wa_rowo2`, `wa_hroff`) | renderer and HUD | 196 |
| floor-grid products | renderer | 144 |
| figure records `wa_figs` (3 × 40) | sim and renderer | 120 |
| typed-key queue `wa_kq` | game | 32 |
| other renderer scalars | renderer | 267 |
| other game and shell scalars | game | 141 |

**The renderer owns ~29 KB of the 29.4 KB of BSS, and the game about
0.3 KB.** Three items dominate:

- the changed-cell presenter's buffers (15,360);
- the reciprocal table (5,764);
- the figure scratch (3,840).

MULTIPLAYER-FPS-PLAN §11 predicted "shadows 8 KB" and concluded "the code
fits one segment". That prediction did not foresee two things: the
stamp/mask/list scheme behind the measured frame rates, and pass 3's
reciprocal table. **They are why the segment is nearly full, not the code.**
Game and shell code is 3.3 KB. The renderer is 8.2 KB of code over ~29 KB
of data, plus 8.1 KB of keyframes it reads.

---

## 2. Size forecast for the remaining features

**How the ranges were set.** Each range is set against shipped code of the
same kind, measured the same way as section 1 from this tree's sources:

| shipped comparator | what it is | bytes |
|---|---|---:|
| WireBall `wblink.inc` | UART hook, queue and framing; the ownership handoff (SPEC.md §109) | 1,887 |
| WireFleet `wflink.inc` + `wfchat.inc` | a SPEC.md §115 lease protocol: CRC frames, hello, sync, acknowledgements, heartbeat, chat | 4,443 + 537 |
| WireFleet `wfsave.inc` | state saved to `SYSTEM/APPDATA` | 1,900 |
| WireBall `wbsound.inc` / WireFleet `wfsound.inc` | tone effects / Sound Blaster samples | 382 / 1,367 |
| WireBall `wbfront.inc`, code only | desktop front-end controls (the file is 28,684; 27,040 of that is art) | ~1,640 |
| Pixelstein `pxact.inc` | actors: states, movement, line of sight (`px_act_los` 369), shooting | 1,854 |
| TANK `tk_tankai` + aim | Tank's opponent steering | ~370 |
| Pixelstein `pxhud.inc` + `pxhuda.inc` | a game HUD | 3,401 + 608 |

Where each feature lands follows the split recommended in section 4:

- **G** is the primary: the game.
- **R** is the renderer part.
- **POSE** and **ARENA** are asset parts.

| feature | where | forecast | reasoning |
|---|---|---:|---|
| **serial link** (SPEC.md §115 lease, owner-authority snapshots at 10-12 Hz, events, the shooter's `HIT`, discovery) | G | 2.5-5 KB | between `wblink` and `wflink`+`wfchat`; framing and CRC can be shared as SOURCE with WireFleet |
| **Ethernet over raw frames** (`NETV_RAW` claim and release, `HELLO` discovery, peer table, frame build and parse over the same snapshot layer) | G | 1.5-3 KB | no package consumer to compare with yet (DOS's packet driver is a different shape). The two 1,514-byte frame buffers are NOT in G: `NETV_RAWTX`/`RAWRX` take a segment in DX (SPEC.md §72.22.4), so they go in a claim |
| snapshot interpolation, event queues | G | 1-2 KB code, 1-2 KB BSS | per remote player: two snapshots and a lerp |
| **lobby and menus** (players, arena, mode, bots, difficulty, link choice; text screens) | G | 2.5-6 KB | WireBall's front-end code is 1.6 KB of desktop buttons; a text lobby with its strings is larger |
| **computer opponents** (four states, waypoint graph, line of sight through the boxes, aim with reaction delay and error, weapon choice, a table of grenade arcs) | G (code); ARENA (nav graph) | 2-4 KB code; 0.3-1 KB nav data per arena | Pixelstein's actors are 1.9 KB without pathing; a host-computed next-hop table makes pathing a lookup |
| **weapons and projectiles**: simulation (bounce, fuse, splash, spawn events) | G | 1-2.5 KB | five weapons from MULTIPLAYER-FPS-PLAN §6 |
| projectiles and trails: drawing | R | 0.5-1.5 KB | one transform, one projection and one glyph each (MULTIPLAYER-FPS-PLAN §6) |
| **hit and death animation tables** | POSE | +6-12 KB of data | today 23 frames = 8,096. The MULTIPLAYER-FPS-PLAN §5 set is ~40 frames = 14,080; with the current per-animation counts and strafe, jump, shoot, back flinch, crumple and body added it is ~44 frames = 15.5 KB. A weapon joint or 32 facings would double it |
| **sound** (tone effects; FM where a card exists) | G | 0.4-1.5 KB | `wbsound` / `wfsound` |
| **more arenas, layers, jumping**: arena data | ARENA, one per arena | 1.5-5 KB each | today's two simple arenas are 719 bytes together. Sectors, steps, spawns, pickups and the nav graph grow it |
| layers: collision with steps, tiers, jump pads, teleporters | G | 0.8-2.3 KB | |
| layers: risers, per-column floor clip, step edges | R | 2-5.5 KB (code and BSS) | plus larger `WA_M*` maxima: ~18 bytes of BSS per vertex, so 40 → 120 vertices is +1.5 KB |
| **HUD and scoreboard** (health, ammunition, weapon, radar strip, scores, kill feed) | G | 1-2.5 KB | today's meter is 412 |
| **VGA specifics**: custom text-font glyphs (MULTIPLAYER-FPS-PLAN §4.4) | R | 0.3-0.6 KB code + 0.5-2 KB glyphs | the glyphs could be an asset part |
| **CGA**: a vertical-blank-limited present option | R | 0.3-1 KB | MULTIPLAYER-FPS-PLAN §4.5 option (c) |
| page flipping on VGA and CGA, with damage per page | R | 0 or 4-8 KB | a second `wa_vis` (3,840) and list per page. Pass 3's reviewer advised against flipping for speed (MULTIPLAYER-FPS-PLAN §16.6), so this may be zero |
| Hercules | none | 0 | the primary adapter already works |
| **kern_mega arm** | G | 0-0.5 KB | `op_cseg` is the standard's own (section 3.5). No `PKG_MEGA` code is expected unless `FSXF_REAL` is adopted |
| **kern_small** | G | 0.1 KB refusal, or 0.5-1.5 KB reduced build | owner's call (section 5) |
| settings and records in `SYSTEM/APPDATA` | G | 0.5-2 KB | `wfsave.inc` |
| more player and bot slots, projectiles, AI state | G | 1-2 KB of BSS | 8 × 64 + 16 × 16 + 8 × 32 |

### 2.1 Totals

| | today | forecast |
|---|---:|---:|
| G (game, after the split in section 4) | ~9.0 KB | **24-44 KB** |
| R (renderer part) | ~46.7 KB | **51-61 KB** without page flipping, up to ~69 KB with it |
| POSE | 8.1 KB | **14-20 KB** |
| ARENA (one in memory at a time) | 0.7 KB (both) | **1.5-5 KB** (4-8 arenas on disk: 6-40 KB) |
| **whole game, image + BSS** | **59.0 KB** | **~95-135 KB in memory during a match** (+35 to +80 KB) |

**Two consequences.**

- **G is comfortable.** At 24-44 KB it is a third to two-thirds of a
  segment.
- **R is the segment to watch.** Its low end leaves 4 KB of headroom and its
  high end does not fit. Section 4 step 6 lists the levers that keep it
  inside 65,024, with their sizes.

A single segment cannot hold R and POSE together: today that is 54.8 KB,
and the forecast is 65-81 KB.

On disk: code and tables compress (today 22,201 → 15,419 with LZB), and
`OP_ZFILL` ships BSS zeros as nothing (SPEC.md §20.12.7.5). The file is
forecast at ~35-80 KB. That fits any of the four geometries, the 360 KB
disk included.

---

## 3. What the parts facility offers, and the candidate splits

### 3.1 The facility, as it applies here

**A part is the package's own business** (SPEC.md §20.12). The parts table
lives in the image. The parts standard (`apps/os88parts.inc`, its code in
`apps/os88partsbody.inc`) is code assembled into the package. The kernel
learns three things and parses none of the parts (SPEC.md §20.12.1). What
this design relies on:

**The kinds of part.**

- `OP_SEG` is code reached by far call.
- `OP_ASSET` is data reached through a segment.
- `OP_ZERO` is zero-filled scratch with no file bytes.
- `OP_LAZY` parts are fetched by `op_fetch` and given back by `op_drop`.
- `OP_COMP` parts are compressed on disk.

Sources: SPEC.md §20.12.4, `apps/os88parts.inc:104-123`.

**`OP_ZFILL`.** On an eager `OP_COMP` row, the zeros are neither shipped
nor decoded (SPEC.md §20.12.7.5, `apps/os88parts.inc:117`). This is how a
part carries a BSS for free.

**Identity stays in the primary (binding).** The entry proc, every window,
every callback and the worker live in the PRIMARY. A part does not call the
ES-fenced slots: `OSAPI_MEM_CLAIM` and its relatives, `OSAPI_MEM_FREE`,
`OSAPI_TASK_SPAWN` and `OSAPI_FSX_ENTER` (SPEC.md §20.12.5,
`apps/os88parts.inc:59-65`).

Two consequences follow for WireArena:

- `wa_main`, the procedure passed to `OSAPI_FSX_RUN`, is a callback, so it
  stays in G.
- A part may keep DS = the primary's (the overlay model), or switch DS to
  its own base for the body of a call and restore it (the data-carrying
  model). It may not near-call an address in another segment.

**One claim, carved.** Every eager part shares one claim, because
`MEM_OWNER_MAX` is 8 claims per owner (SPEC.md:102439,
`apps/os88parts.inc:82-85`). A lazy part takes one claim of its own while it
is fetched (SPEC.md §20.12.4).

**A part is at most 65,024 bytes** (`OP_PARTMAX`, SPEC.md:51143), the same
ceiling as the primary. The carve as a whole may pass 64 KB (SPEC.md
§20.12.11).

**A compressed lazy part can be fetched again after a drop.** The table
keeps a "shadow" word per row (SPEC.md §20.12.7.4.1). That makes per-match
arena loading sound.

**kern_mega.** `op_seg` answers a selector there. `op_cseg` answers the
CODE selector for a far call, behind `%define OP_WANT_CSEG`
(`apps/os88partsbody.inc:1645`, `OSAPI_MEM_CODESEL` at SPEC.md:87348). A
part must read and write nothing through CS, because CS is an execute-only
alias there (`apps/xtmp3/x3front.asm:13-17`).

**Claims may MOVE, on request.** `%define OP_ONMOVE` plus `op_movable`
declares the carve or a fetched part movable. The hook re-reads every
segment the package banked. Code you far-call may be declared movable only
if nothing under that far call ever claims memory (SPEC.md §20.12.12,
`apps/os88partsbody.inc:2411-2425`).

**What the standard costs the primary.** SPEC.md §20.12.9 gives the
measured ladder: 800 bytes for one plain asset, 1,203 with compression,
2,581 with every feature. For this plan's table it was measured by
assembling a probe package against the tree's standard:

- With R (`OP_SEG`, `OP_COMP | OP_ZFILL`), POSE (`OP_ASSET`, `OP_COMP`),
  `OP_WANT_CSEG` and `OP_ONMOVE`, all rows eager: **1,750 bytes** of image.
- With the arena rows lazy and compressed (`op_fetch`, `op_drop`,
  `op_lazyok` reachable): **2,413 bytes**.
- Plus `OP_BSS` = 105 bytes of BSS either way.

**Re-homing** (`OSAPI_PKG_REHOME`, SPEC.md §20.12.10). A loader image hands
its identity to a part and is freed. This is the shape used by Pixelstein,
Clear Skies, Scribe, Word and DOS. It saves the standard's bytes in the
program's segment. It is not assembled on kern_small (SPEC.md:50748).

### 3.2 Precedents in the tree, and what each shows

| package | shape | what it shows for WireArena |
|---|---|---|
| **XTMP3** (`apps/xtmp3/`, SPEC.md §121) | an eager `OP_SEG` FRONT part, assembled at org 0 from its own source: a header of near verb offsets, a FIXED data block the primary stages into, then code and tables, all reached through DS = the part's data segment (`x3front.asm:1-35`). The primary banks FRONT's data and code selectors (`op_seg`/`op_cseg`, `OP_WANT_CSEG`, `xtmp3.asm:66-67`) and enters it with a hand-built far call that reads nothing through CS (`x3p_fcall`, `x3pars.inc:66-88`). It is MOVABLE, with a hook that re-reads the banked words (`x3_partsmv`, `xtmp3.asm:176-209`), and runs on kern_mega (`tests/x3mega.py`) | **This is R's shape exactly**: a data-carrying code part with a staged input block, called once per unit of work (a granule there, a frame here) |
| **Pixelstein** (`apps/pixelstein/`, SPEC.md §97) | a re-homing loader (`pxstein.asm:353-359`); the program is part 0. Scaler scratch is an `OP_ZERO|OP_OPT` `OP_SEG` part. The hot path far-calls generated code once per column (`call far [px_qcur]`, `pxcast.inc:469`). Levels and art are lazy `OP_COMP` assets | a 3D game already makes a far call per COLUMN and lives. Keyframe-like data (art masters) is an asset |
| **RealDMG** (SPEC.md §20.12.12's audit) | the core is far-called code in the carve, with a 64 KB `OP_ZERO` view | a performance-bound dispatch loop living in a part |
| **WireFleet** (SPEC.md §114.11) | lazy `OP_COMP` assets (stamp bank, samples), movable through `OP_ONMOVE` | the first opted-in lazy part; `pkgmove-wireflet` is its row |
| **Scribe, Word** | a re-homing loader; part 1 assembled at the top of the program's own segment, so calls into it are near (`apps/scribe/scribe.asm:20381-20403`) | **no help here.** The carve is also capped at 65,024 (`SC_P1ORG + part 1 <= 65,024`), the same ceiling `APP_MAX_SIZE` now is (SPEC.md §20.1.1) |
| **Sheet, Pixel, Redline, Loom, WIN16** | lazy `OP_SEG` modules for rarely used commands | the lever for a lobby, if G ever needs it (section 4 step 6) |
| **DrMarco, Excitebike, 1942, ARCADE** | lazy `OP_COMP` assets per adapter or per level | the shape for arenas |

### 3.3 The candidate splits

What the renderer and the game exchange today was found by a host scan:

- `wrender.inc` reads about 40 symbols that `wirearena.asm` defines:
  - the camera (`wa_px`, `wa_py`, `wa_ang`, `wa_pitch`, `wa_eye`);
  - the figure records;
  - the arena working tables;
  - the mode and FOV constants (`wa_w`, `wa_k16`, `wa_c16`, `wa_kyq`,
    `wa_kyh`, `wa_wf`, `wa_vseg`, `wa_mono`, the detail toggles);
  - the cell buffers declared in `wirearena.asm`'s chain;
  - `wa_sincos`.
- `wirearena.asm` uses nine renderer symbols: `wa_render`, `wa_present`,
  `wa_geominit`, `wa_wallz`, `wa_colz`, `wa_hroff`, `wa_vst`, `wa_nwr` and
  `WR_BSS`.
- **`wrender.inc` makes no `OSAPI_*` call and reads nothing through CS**
  (grep of the file). So it can become a part with no change to its logic,
  and its claim can be declared movable (SPEC.md §20.12.12's condition).

#### A. Data out only: keyframes to an asset, arenas to lazy assets; renderer and game stay together

| | |
|---|---|
| moves | POSE (8,096), arenas (719), help text (391) |
| primary gains | ~9.2 KB, then pays ~1.75-2.4 KB for the standard: **net +6.8 to +7.4 KB**, about 13 KB of headroom |
| crossings | the figure pass reads two keyframes through a segment register: 22 bytes a keyframe at one facing, 44 reads a figure. With `es:` overrides that is +3.78 clk each (PERFORMANCE.md Part 2), **~170 clk a figure** (derived). It could be a 44-byte copy instead (~650 clk). Arenas cost nothing per frame (copied at match start, as `wa_loadarena` does today) |
| verdict | **necessary but not sufficient.** It absorbs the animation growth but not the forecast's +15-35 KB of game code. Keep it as part of the recommendation |

#### B. Renderer in its own part (R), game in the primary (G): recommended

| | |
|---|---|
| R | `wrender.inc` (7,319), the glyph and attribute tables (76), a copy of `wa_sin`/`wa_sincos` (2,074), every renderer BSS item in section 1.2 (~36.5 KB, shipped as nothing through `OP_ZFILL`), a header of verb offsets and a fixed frame block. **~46.7 KB**, forecast 51-61 KB |
| G | everything else: entry, window, menu, `wa_main`, input, simulation, bench, meter, the HUD (it writes row 24 straight to VRAM, `wa_putc`), the parts standard, its own `wa_sin`. **~9.0 KB**, forecast 24-44 KB |
| R's verbs | `INIT` (the renderer half of `wa_setmode`: row tables, `wa_hroff`, clearing `wa_vis`/`wa_stamp`/`wa_vst`); `ARENA` (copy an arena's working tables in, plus `wa_geominit`); `RENDER`; `PRESENT`; `TRACE` (the half of `wa_fire` that reads `wa_wallz`, `wa_colz` and the figures' last projection, answering which figure the crosshair hit; G applies the damage) |
| crossings | two far calls a frame (RENDER, PRESENT), one a shot (TRACE), one a mode change (INIT), one a match (ARENA), plus the frame block (section 3.4) |
| verdict | **recommended.** The boundary is the one `wrender.inc` already draws: pure data in, cells out. The growth goes to G, which has the room. Every API call, the bracket, the network and the callbacks stay in G with DS = G, where SPEC.md §20.12.5 wants them |

#### B'. The mirror image: game logic as the part, renderer in the primary

The game is the API-heavy half: `OSAPI_DRV_CALL`, the serial lease,
`OSAPI_SND_TONE`, `OSAPI_KEY_DOWN`, `OSAPI_MOUSE_REL` and `op_fetch`. From
a data-carrying part, every X-stub call stamps ES from the part's DS. For
`OSAPI_DRV_CALL` that names the buffer segment (SPEC.md §72.22.4). Each
call would have to swap DS back to the primary.

**Rejected:** it puts the most API calls on the side SPEC.md §20.12.5
constrains.

#### C. Lobby and menus swapped out during play

Pixel's and Sheet's shape: a lazy `OP_SEG` module in the overlay model (DS
= G). It buys the lobby's 3-8 KB. It costs one disk read every time the
player returns to the lobby: one or two `int 13h` calls at ~400 ms each on
the target (PERFORMANCE.md Part 2).

**Not now.** Under B, G is forecast at 24-44 KB of its 65,024. This is a
lever held in reserve (section 4 step 6).

#### D. Renderer BSS only, as an `OP_ZERO` asset reached through ES

This buys ~27 KB (the six cell buffers and `wa_fbuf`). But every plot,
stamp test and present comparison gains a segment override. That is ~2-3
accesses a cell × 200-400 cells × 3.78 clk ≈ **2-4.5k clk a frame,
~1-1.5%** (derived), plus a register for the segment inside loops already
hand-scheduled to the fetch floor (MULTIPLAYER-FPS-PLAN §16.6).

**Rejected:** it buys less than B, costs more per frame, and edits the
hottest code in the package.

#### E. A re-homing loader (Pixelstein's shape) on top of B

This would move the standard's ~2.4 KB out of G. G does not need the room.
The program would still carry the standard to fetch arenas per match. The
mechanism is absent on kern_small (SPEC.md:50748).

**Rejected for now.** Revisit only if G ever approaches its ceiling.

### 3.4 The cost of each crossing in B, per frame

The figures below price the play bench `P` (MULTIPLAYER-FPS-PLAN §16.7:
40 columns, **62.8 ms a frame**, ~300k clk) and bench `B` (MULTIPLAYER-FPS-PLAN §16.6: 28.1 fps,
**35.6 ms**).

| crossing | how often | cost | share of `P` / `B` |
|---|---|---|---|
| a far call into R and back, `x3p_fcall` shape | RENDER + PRESENT: 2 a frame | **~280 clk each** (derived: 8086 book timings plus the 8088's extra byte transfers; the inbound far call alone is MEASURED at 54 cycles, PERFORMANCE.md Set 109, line 10313; a near pair is 52 clk, Part 2). A plain `call far [mem]` with DS loaded first is ~150 | **~0.2% / ~0.3%** |
| TRACE | per shot (≤ 4 a second) | ~280 clk + the work `wa_fire` already does | ~0 |
| INIT / ARENA | per mode change / per match | ~280 clk + a copy of a few hundred bytes | ~0 |
| keyframes read through a segment (`es:` override) | 44 reads a drawn figure | ~170 clk a figure (derived, PERFORMANCE.md Part 2's +3.78) | 3 figures: ~0.2% / ~0.3% |
| **frame block, version 1: a `rep movsw` copy** of camera, per-frame toggles and figure records, G → R | 1 a frame | today ~67 words: 9 + 25n ≈ **1.7k clk** (derived: the 8086 book's 17 clk a word plus 8 for the 8088's two extra byte transfers; PERFORMANCE.md Part 2 MEASURED the store-only `rep stosw` at 1.76 µs a byte, 16.8 clk a word, which agrees). At 8 figures and 16 projectiles of whole records, ~230 words ≈ 5.8k clk | **today ~0.6% / ~1.0%**; at full scale ~1.9% / ~3.4% |
| **frame block, version 2: draw records written in place** by G through ES into R's block (12 bytes a figure, 6 a projectile, 14 of camera) | 1 a frame | ~30-110 words × ~37 clk ≈ 1.1-4k clk (derived) | ~0.4-1.3% / ~0.7-2.3% |

**What the table says.** The calls themselves are free at frame rate. The
cost is moving the world across the boundary.

**Version 1 is right for the FIRST split.** It is mechanical: G keeps
writing its own variables and R reads its own copies, so neither half's
logic changes. At today's three figures it costs ~0.6% of a play frame.

**It does not scale to eight players, though.** Section 4 step 4 replaces
it with compact draw records when the figure count grows. The other option
is the renderer reading G's records through a passed segment register,
which costs +3.78 clk an access and no copy at all.

**The one design number to take once (CLAUDE.md, Testing).** Which version
ships is decided by measuring version 1 on bench `P` in step 3. The
implementer takes that number and records it in the step's report; nobody
re-takes it.

### 3.5 What the facility cannot do, stated rather than invented around

1. **No shared data between two segments.** The standard gives each part a
   segment and nothing else. Sharing (the frame block) is the package's
   convention.

   This is not a missing kernel facility. XTMP3's fixed data block
   (`x3front.asm:30-35`, `x3gran.inc` section 5) is the precedent, and R
   follows it. Both halves are assembled from one tree into one file, so
   the layout can be asserted at build time with `%if` on a shared include.

2. **`wa_sin` cannot be shared across segments.** G and R each assemble a
   copy (2 KB in each).

   Shared constant data needs either a duplicate or an access through ES.
   No facility exists for it, and none is proposed: 2 KB in G is cheap
   against its forecast.

   **Resolved after the split (section 8.5):** R needs only the camera's
   sin and cos, once a frame, so G hands them over in FRAME and R carries
   no table. G's own is a quarter wave.

3. **`OSAPI_DRV_CALL` from a part is not addressed by SPEC.md §20.12.5.**
   The X stub stamps ES from the caller's DS (SPEC.md §72.22.4). Every
   `DRV_CALL` site therefore stays in G with DS = G, which is design B.

   Separately, SPEC.md §53.7 still does not name `OSAPI_DRV_CALL` as legal
   in a bracket. MULTIPLAYER-FPS-PLAN §8.2 already raised this; it is not
   new here.

4. **Raw frame buffers in a claim on kern_mega.** The DX-segment form of
   `NETV_RAWTX`/`NETV_RAWRX` is specified (SPEC.md §72.22.4). Nothing in
   this plan established how it behaves when DX is a kern_mega SELECTOR.

   This must be checked before the Ethernet wave. If it fails, the fallback
   is 3 KB of G's own BSS (DX = 0).

---

## 4. Recommendation and migration

**Adopt design B, with A's data parts.** (As built: section 7 is the
contract, and it supersedes this table - the keyframes ride in row 0.) The
parts table as first proposed:

| row | kind | flags | contents |
|---|---|---|---|
| 0 R | `OP_SEG` | `OP_COMP \| OP_ZFILL` | `build/wrpart.bin`: header and verb offsets, frame block, `wrender.inc`, tables, `wa_sin`, the renderer's BSS as zeros |
| 1 POSE | `OP_ASSET` | `OP_COMP` | `build/wapose.bin` from `tools/arenagen.py` |
| 2.. ARENA *n* | `OP_ASSET` | `OP_LAZY \| OP_COMP` | one per arena: walk tables (BSP links relative to the arena's start), collision boxes, spawns, nav graph |

G declares `OS88_F_PARTS`, `%define OP_WANT_CSEG` and `%define OP_ONMOVE`.
`wa_entry` calls `op_load` first (`apps/os88parts.inc:43-57`). The region
stays `OS88_REGION_MOVABLE`. G declares the carve movable and banks four
words, which its hook re-reads:

- R's data segment;
- R's code selector;
- POSE's segment;
- the current arena's segment.

### 4.1 The steps, in order

**Step 0: now, and nothing in `apps/` changes.**

- Land the five performance branches first. **No step below edits
  `wirearena.asm` or `wrender.inc` until they have merged.**
- Pin the interface before any code (CLAUDE.md, *Building an app or game in
  waves*, step 3). WireArena has no SPEC.md section yet. Open one, or a
  section of the parent plan if the owner prefers, stating:
  - R's verb table;
  - the frame block's layout;
  - the parts table;
  - the arena asset format.
- Verification: `python3 tools/checkdocs.py`.

**Step 1: byte-identical extraction, one commit, after the merges.**

- Move into includes, without reordering, so that `build/wirearena.bin` is
  unchanged:
  - the constants and record layouts (`WA_*`, `F_*`, `A_*`) into
    `apps/wirearena/waconst.inc`;
  - the `WA_VAR` chain into `wavars.inc`, labelled as three groups: game
    state, frame block and renderer state;
  - `wa_sincos` and the sine table's include into `wasin.inc`;
  - `wa_hgl`, `wa_fglyph`, `wa_attrc` and `wa_attrm` into `wrtab.inc`.
- Verification: `make wirearena`, then `cmp build/wirearena.bin` against
  the build from before the commit. `OS88_HEADER` writes no build number
  into a package, so the comparison holds across the commit. A difference
  means a variable moved.
- Then run `make` (the fast tier).

**Step 2: POSE becomes a part (design A's keyframe half).**

- `tools/arenagen.py` writes the keyframes as a binary.
- The Makefile packs `WARENA.O88` with `--part` (as
  `$(BUILD)/pxstein.o88` does, `Makefile:9482-9485`).
- G gains the standard.
- `wa_figure`/`wa_fjl` read the two keyframes through a segment: `es:`
  overrides, or a 44-byte copy. The implementer measures both once on bench
  `P` and records the choice.
- Size: −8,096 + ~1,300 (one compressed asset row plus the move hook) ≈
  **6.8 KB back**, so headroom goes from 6.0 to ~12.8 KB.
- Verification:
  - `make`;
  - a posed text-VRAM comparison against step 1's build: the one pass 3
    used, byte-identical over the same poses at 40 and 80 columns, **with
    figures in view**. `tools/wacheck.py` skips figure columns, so it does
    not cover this step;
  - a launch with too little memory, which must refuse with the standard's
    own toast (SPEC.md §20.12.4).
- Rows to name: `partoff`, `stkapps`, `pins` (full tier: the new carve
  claim either declares movable or gets a `tests/pins.txt` line).

**Step 3: R becomes a part (design B).**

- New `apps/wirearena/wrpart.asm`, org 0, in XTMP3 FRONT's layout:
  - a magic word and verb offsets;
  - the fixed frame block;
  - `%include "wrender.inc"`, unchanged. The performance work keeps landing
    in one file that both builds share as SOURCE (WEAVE-SPEC §1.2's rule);
  - `wrtab.inc`, `wasin.inc` and the renderer group of `wavars.inc`;
  - the BSS emitted as zeros, so `OP_ZFILL` drops them.
- Each verb is a near-called body behind a `retf` wrapper. R's internal
  calls stay near.
- G stops including `wrender.inc` and gains:
  - the far-call glue: `x3p_fcall`'s shape, with `STKBALANCE-OK` notes for
    `tests/unit/t_stkapps.py`;
  - frame-block version 1;
  - `wa_fire` split into TRACE plus damage.
- A `%if` asserts that the frame block's layout is the same in both
  assemblies.
- Size: G ~9.0 KB, R ~46.7 KB.
- Verification:
  - `tools/wacheck.py`. It keeps working: the camera stays in G's BSS and
    is copied to R each frame, and it reads VRAM;
  - the posed VRAM comparison of step 2;
  - benches `B` and `P` on MartyPC `os8088_5150_cga`, within noise of
    step 2. **This is the one deciding number** of section 3.4;
  - a kern_mega launch through `op_cseg` (docs/BOCHS.md is kern_mega's
    instrument; `tests/x3mega.py` is the pattern for a row).
- Rows to name: `partoff`, `stkapps`, `megapkg`, `pins`.

**Step 4: arenas become lazy parts, and the frame block goes to version 2
if step 3's number says so.**

- `ARENA` copies the walk tables into R.
- G reads collision and nav data from the arena's segment.
- The fetch happens from the Game menu command, before `OSAPI_FSX_RUN`, so
  the disk read sits in a loading moment and not inside a frame. Mem and
  file slots are legal in a bracket anyway (SPEC.md §53.7).
- The arena is dropped when the match ends; the shadow makes a re-fetch
  sound (SPEC.md §20.12.7.4.1).
- `tools/arenagen.py` keeps its host checks: the BSP order check, and the
  refusal of a start or patrol inside a wall.
- Verification: `wacheck` per arena, and the posed VRAM comparison.

**Step 5: the feature waves land on this shape.**

- MULTIPLAYER-FPS-PLAN §15's Waves 1-7 go to G (link, Ethernet, AI,
  weapons, lobby, sound) and to R (layers, projectiles, the VGA font, the
  CGA option).
- **Each wave's report states G's, R's, POSE's and an arena's bytes**, in
  the units of section 1, so a ceiling is seen coming rather than hit.

**Step 6: levers held in reserve, each with its size, taken only when a
measured total needs it.**

- **R**, in increasing cost to the hot path:
  - VGA glyph data out to an asset (0.5-2 KB);
  - page-flip damage buffers to an `OP_ZERO` asset, touched only by
    PRESENT (4-8 KB);
  - `wa_rcp` to an asset (5,764; one segment override per projection);
  - capping `wa_lista`/`wa_listb` below 1,920 entries, with a
    present-everything fallback (up to ~5 KB).
- **G**: the lobby as a lazy `OP_SEG` module dropped during play (design
  C).
- The owner's "optimize for size later" belongs here. **Measure the byte
  before taking any of these.**

### 4.2 Verification across all steps

Per the Testing rules in CLAUDE.md:

- Each step verifies on the host: `make`, `make wirearena`, the `cmp` of
  step 1, `tools/checkdocs.py`.
- Each step names its rows and **runs no soak row and no `test-full` per
  step.**
- The bench numbers belong to the final pass, except step 3's one deciding
  number.

`tests/suite.py` has **no WireArena row today**. WireArena is an on-demand
`instrument` (`apps/RETIRED.txt:85`). Once it ships, docs/WRITING-TESTS.md
applies, and these are the rows the split itself makes worth having:

- **a launch row**: `op_load`, R and POSE present, banked segments
  non-zero;
- **a refusal row**: too little memory, its own toast;
- **a move row**: `tests/pkgmove.py`'s pattern, the carve compacted and the
  game still drawing;
- **a kern_mega row**: `tests/x3mega.py`'s pattern.

### 4.3 Risks

- **The parallel performance work.** Mitigated by order: step 1 waits for
  the merges, and step 3 includes `wrender.inc` unchanged so later tuning
  keeps landing.
  - The remaining hazard is a symbol defined in BOTH assemblies with
    different meanings. A missing one is caught by the assembler: `wrpart.asm`
    will not assemble with an undefined `wa_*`.
  - The frame block must be the ONLY way state crosses, and the `%if`
    layout assertion guards it.
- **Movability (`MC_RLOC`, SPEC.md §20.12.12).** Both claims are born
  pinned. G declares the carve with `op_movable` and re-reads four banked
  words in the `OP_ONMOVE` hook. The far-called R may be declared because
  it calls no kernel slot, so nothing claims memory under the far call.
  - What breaks it: an `op_fetch` inside the bracket compacts the heap
    while `wa_main` holds a stale copy in a register. Every banked word is
    re-read from memory after any `op_fetch`.
  - Without the declaration the carve is a pinned wall in the heap and must
    have a `tests/pins.txt` line (the `pins` row, full tier).
- **Worker rules.** WireArena has no worker. `op_load` and `op_fetch` are
  UI-task only (`apps/os88parts.inc:67-69`). If the network ever wants a
  worker: no file and no claim from it (SPEC.md §20.6 rule 7), and
  `OS88_WORKER_RESTARTABLE`.
- **ES on entry to callbacks** (ES = `KERNEL_SEG`). `op_load` clobbers ES,
  so `wa_entry` banks it if it ever calls `OSAPI_ARG_FILE`
  (`apps/os88parts.inc:48-57`). R clobbers ES. G code after a verb must not
  assume ES; `wa_main` already loads ES itself. No window callback calls R.
- **kern_small.**
  - **Memory.** G + R + POSE is ~63 KB today and 90-130 KB later, in
    claims of ~46-61 KB. kern_small resides in 64 KB, so on its 128 KB
    machine WireArena cannot run. `op_load`'s arithmetic refuses that
    instantly and in its own words (SPEC.md §20.12.4).
  - **Features.** There is also no `OSAPI_MOUSE_REL` there
    (MULTIPLAYER-FPS-PLAN status line), no `FSXF_RATE` and no NIC.
  - Re-homing, which this plan does not use, is absent there
    (SPEC.md:50748). The owner decides (section 5).
- **kern_mega selectors.**
  - Every segment comes from `op_seg`/`op_cseg`, and nothing adds to one
    (`OP_SUBV_RAW` is only for layout checks, `apps/os88parts.inc:434-449`).
  - R reads and writes nothing through CS. That is true of `wrender.inc`
    today, and the reviewer's rejection of self-modifying loops keeps it
    true (MULTIPLAYER-FPS-PLAN §16.6).
  - If `FSXF_REAL` is ever adopted, the four banked words join an
    `OS88REAL_LIST` and must be pinned from `os88real_prep` until
    `OSAPI_FSX_RUN` returns (`apps/os88real.inc:39-43`).
- **Contiguity.** R + POSE is one carve: 55 KB today, 65-81 KB forecast,
  contiguous. A fragmented heap can refuse that where two claims would fit.
  The alternative is POSE as a lazy row fetched at entry with
  `OP_WANT_ASSETHI` (SPEC.md §20.12.4.1). It costs one more claim, and it
  is the data shape that section exists for.
- **R's ceiling.** The forecast's high end exceeds 65,024. Step 5's
  per-wave byte line is what sees it coming; step 6 is what answers it.

---

## 5. Open questions for the owner

1. **The shape.** Renderer as the part and game as the primary (design B),
   with keyframes and arenas as data parts? Section 3.3 gives the
   alternatives and why each was set aside.
2. **Arenas: lazy per match or eager.**
   - Lazy means a short floppy read when a match starts (one or two
     `int 13h` calls, ~0.4-0.8 s on a 5150) and one arena in RAM.
   - Eager means no pause, and every arena resident.
3. **POSE in the carve (one claim, contiguous with R) or lazy at the
   ceiling (`OP_WANT_ASSETHI`, its own claim)?**
4. **Page flipping on VGA and CGA.** Is it wanted at all? With damage per
   page it costs R 4-8 KB, and pass 3's reviewer advised against it for
   speed.
5. **kern_small: a clean refusal, or a reduced build?** A reduced build
   would be for a kern_small machine with ≥ 256 KB, keyboard-aimed, with
   bots or serial play. The 128 KB machine cannot hold it.
6. **The minimum RAM to state for kern_big.** 90-135 KB of claims during a
   match is comfortable on the 640 KB machines in docs/FIELD-MACHINES.md
   and marginal on 256 KB, where kern_big's 100 KB resident leaves ~150 KB.
7. **Where the lobby lives.** Text screens inside the bracket, or the
   desktop window before it? It decides whether the lobby uses the
   desktop's drawing API, and whether design C could ever apply.
8. **Frame-block version 1 first, or straight to version 2?** Version 1
   costs ~0.6% of a play frame today, measured in step 3 and replaced in
   step 4 if it grows.

## 6. The owner's answers (2026-11-13)

These answers bind the plan. Where one contradicts a section above, it
overrides that section, and that section is to be rewritten when step 0
lands.

- **At most three figures, ever.** Every "8 figures" scale point above (3.4's
  frame-block cost, the forecast's figure records) is replaced by 3. Copying
  the world each frame stays near today's ~0.6% of a play frame plus the
  projectiles.
- **A heap claim is fine wherever it beats a part.** Anything that need not
  be a part (a buffer, a table built at start, a page) may be a claim of its
  own, if that is faster, smaller on disk or otherwise better. A part is the
  answer for CODE and for data that ships; a claim is the answer for what is
  built or zeroed at run time. Section 3.3's D was set aside for its hot-loop
  cost, not because a claim was ruled out.
- **Q1, the shape:** approved — design B.
- **Q2, arenas:** loaded from disk when a match starts.
- **Q3, keyframes:** they share the renderer's claim if the two fit together
  and that is faster; otherwise POSE gets its own. Decide at step 2 from the
  measured sizes at that point.
- **Q4, page flipping:** not on the 8088. Page flipping is usually slower,
  and there is no room for slower. It may come back for a 286, or anywhere a
  second page makes the game FASTER (some scheme that does). A second page
  would be a heap claim, not a part.
- **Q5, kern_small:** refuse. WireArena is kern_big and kern_mega only.
- **Q6, memory:** design for 640 KB. The end product has ETHER.DRV, likely a
  sound driver and a hard-disk driver loaded, plus the renderer, arena and
  animations. Running on 256 KB is welcome, not required.
- **Q7, the lobby:** a windowed desktop interface, in a part of its own.
- **Q8, the frame block:** whichever is fastest. Measure version 1 in step 3
  and take version 2 if it wins.

**Since this plan was written:** the round-4 present work (perf-present)
removed `wa_vis` and folded `wa_fmask` into `wa_stamp`'s odd bytes, which
cuts the renderer's BSS by 7,680 bytes. Section 1 is re-measured at that
tree (`7232b890`).

---

## 7. The contract (pinned before step 3)

WireArena has no SPEC.md section yet, so the interface between its two
segments is pinned here, as step 0 allowed. Nothing in it is a kernel or SDK
interface: the parts standard is used as SPEC.md §20.12 already pins it, and
`apps/os88api.inc` does not move. `apps/wirearena/wafb.inc` is the code form
of 7.2 and 7.3, included by both halves.

### 7.1 The part table

| row | kind | flags | file | contents |
|---|---|---|---|---|
| 0 | `OP_SEG` | `OP_COMP \| OP_ZFILL` | `build/wrpart.bin` from `apps/wirearena/wrpart.asm` | the header, the frame block, `wrender.inc`, the glyph and attribute tables (`wrtab.inc`), `wa_sin` and `wa_sincos`, the keyframes `wa_pose`, and the renderer's BSS as zeros (the arenas too, in step 3 only) |
| 1.. | `OP_ASSET` | `OP_LAZY \| OP_COMP` | `build/waa<n>.bin`, one per arena, from `tools/arenagen.py --arena N` | 7.5 |

**The keyframes ride in row 0** (the owner's Q3): measured at step 2, the
renderer's image is ~19.2 KB and its BSS ~28.9 KB, ~48 KB of the 65,024 a
part may hold, so the keyframes share the renderer's claim and its DS. No
segment override reaches `wa_figure` or `wa_fjl`.

The primary declares `OS88_F_PARTS`, `OP_WANT_CSEG` and `OP_ONMOVE
wa_partsmv`; `wa_entry` calls `op_load` before anything that could disturb
SI, banks the part's data segment (`op_seg`) and code selector (`op_cseg`)
and the verbs' offsets out of its header, then declares the carve movable
(`op_movable`, `OP_CARVE`). The hook re-reads the two segment words. That is
sound because the part calls no kernel slot (SPEC.md §20.12.12's condition):
nothing claims while this task stands in it.

### 7.2 The part's header

| offset | size | |
|---|---|---|
| 0 | 6 | `'WRPART'` |
| 6 | 1 | `WR_ABI` (1) |
| 7 | 1 | `WR_NVERB` (6) |
| 8 | 2 × 6 | each verb's offset in the part, in the order below |
| 32 | — | `WR_FB`: the frame block (7.3) |

The primary refuses to start, with a toast, if the magic, the ABI byte or the
verb count differ from the ones it was built with.

**The verbs.** The primary enters one through `wa_rcall` (BX = the verb x 2):
a far call built as push/push/`retf` with DS = the part's data, so no vector
is read through CS (XTMP3's `x3p_fcall`; under kern_mega a part's CS is an
execute-only alias). The verb returns with `retf`; DS is put back by the
glue, and every other register, ES included, is the verb's to clobber.

| verb | when | in | out |
|---|---|---|---|
| `WRV_INIT` 0 | after a mode set | the CONFIG section | the row tables, the stamps, the vertex cache and the lists started again |
| `WRV_GEOM` 1 | after `WRV_ARENA` | | `wa_geominit`: the reciprocal table and the coordinate steps |
| `WRV_ARENA` 2 | a match starts | DX = the arena part's segment (step 3: AL = the arena) | the working tables, the tree rebased into `wa_bsp`, and the ARENA section |
| `WRV_RENDER` 3 | every frame | the FRAME section and the figures | AL = the frame number |
| `WRV_PRESENT` 4 | every frame | | AX = the text cells written |
| `WRV_TRACE` 5 | a shot | the figures' heads | AX = the hit figure's offset in `wa_figs`, or -1 |

### 7.3 The frame block

One layout in both segments: `WA_FBLOCK` expands to the same variables in the
same order in the primary's BSS and at `WR_FB` in the part, so a variable's
address in the part is `x - wa_fb_cfg + WR_FB` (`WA_RAT`).

| section | variables | written by | copied |
|---|---|---|---|
| CONFIG | `wa_vseg`, `wa_w`, `wa_w2`, `wa_k16`, `wa_c16`, `wa_kyq`, `wa_kyh`, `wa_wf`, `wa_mono` | the primary (`wa_setmode`, `wa_setfov`) | primary → part by `wa_rsync`, at a mode set and a field-of-view change |
| FRAME | `wa_px`, `wa_py`, `wa_ang`, `wa_pitch`, `wa_eye`, `wa_fov`, `wa_hyst`, `wa_flod`, `wa_grid`, `wa_sparse`, `wa_mflash` | the primary | primary → part every frame (`wa_rframe`) |
| FIGS | `wa_figs`, 3 × 40 | the primary writes bytes 0..17 (position, facing, animation, flash, dead); the part writes the projection (`F_PZ` … `F_PWH`) and keeps it | bytes 0..17 primary → part every frame and before TRACE; never back |
| ARENA | `wa_sx`, `wa_sy`, `wa_sa`, `wa_nbox`, `wa_boxes` | the part (`WRV_ARENA`) | part → primary after `WRV_ARENA` |

**A renderer toggle is a FRAME variable.** Anything the renderer reads that
the primary writes must be in CONFIG or FRAME, or the part never sees it
change. That includes a new key-toggled flag: it goes in FRAME, in
`wafb.inc`, not in the primary's own BSS.

The primary's own `F_PZ` means only *drawn since it last spawned*: the render
glue sets it to 1 for every figure after each frame, and `wa_spawn` zeroes
it as before. Before TRACE, a 0 there zeroes the part's `F_PZ`, so a figure
respawned since the frame is not hit where its body lay, exactly as when the
pistol read the primary's own projection.

### 7.4 Who calls what

The primary keeps the names it always called: `wa_render`, `wa_present`,
`wa_geominit` and `wa_loadarena` are its glue, so `wa_main` and the two
benches are unchanged. `wa_fire` keeps the hit (sound, flash, flinch, death)
and asks the part for the ray.

### 7.5 An arena part (step 4)

One `OP_ASSET` row per arena (`OP_LAZY | OP_COMP`), written by
`tools/arenagen.py --arena N` as NASM source and assembled at org 0. The Game
menu's command fetches it before the bracket (`wa_loadarena`, `op_fetch`),
hands its segment to `WRV_ARENA`, copies the ARENA section back and drops it
again (`op_drop`): both halves keep what they need, so no claim outlives
the call, and a compressed lazy row can be fetched again after a drop
(SPEC.md §20.12.7.4.1).

| offset | | |
|---|---|---|
| 0 | 15 words | `wa_arenas`' descriptor row - xv, its count, yv, its count, vertices, their count, corners, boxes, their count, the tree, the start x, y and angle, the colours - with every table offset relative to the part, and the tree's NODE COUNT appended |
| 30 | 4 words | the heights (WIREARENA-HEIGHTS-PLAN 7.2.1): the box records' offset, the room's height, the arena's tallest height, the lift table's offset |
| 38 | 2 words | its own patrols (WIREARENA-HEIGHTS-PLAN 7.4.5): their block's offset and its bytes (0, 0: the primary's) |
| 42 | 1 word | its PVS's offset, 0 for none (WIREARENA-HEIGHTS-PLAN 9.8): a byte the grid's shift, a byte its cells a column, a word the masks' bytes, then the masks, `WA_PVSB` bytes a cell (a bit a box), column by column. `WRV_ARENA` copies it into `wa_pvs` |
| 44 | | the tables it names, as `arena_tables` writes them for `watab.inc` |

`WRV_ARENA` copies the tables into the working ones exactly as the in-image
`wa_loadarena` did, and the tree - 12 bytes a node - into `wa_bsp`
(`WA_MNODE` nodes, the largest arena's), adding `wa_bsp - (the part's tree)`
to every non-zero link; the root is the first node. The tree is then walked
there, by `wrender.inc` unchanged.

---

## 8. As built (2026-11-13, branch `parts-impl` from `7232b890`)

Steps 1-4 landed as five commits: step 1 (constants, `wa_sincos` and the
renderer's tables into includes, `wirearena.bin` cmp-identical), step 2
(`build/watab.inc`'s data blocks on request, cmp-identical; the keyframes'
home decided), section 7 (this contract, before any interface moved),
step 3 (the renderer part) and step 4 (the arena parts).

### 8.1 Bytes, measured

| | image | BSS | segment |
|---|---:|---:|---:|
| before: `WARENA`, one segment | 23,226 | 29,383 | **52,609** |
| the primary (game): `wirearena.asm` | 8,669 | 534 | **9,203** |
| part 0, the renderer: `wrpart.asm` - header and frame block 266, verbs and the arena loader 386, `wrender.inc` 8,203, `wa_sincos` 26, tables 76, `wa_sin` 2,048, keyframes 8,096 | 19,101 | 29,181 | **48,282** |
| parts 1 and 2, the arenas | 488 and 235 | | |

`WARENA.O88` on the disk: 16,238 → 20,738 bytes. Two streams compress worse
than one, `wa_sin` is in both halves (2 KB that compresses poorly), and the
parts standard is ~2.4 KB of the primary (its whole lazy arm, measured in
section 3.1). The renderer's BSS costs the disk nothing (`OP_ZFILL`: 18,845
bytes stored of 48,282, LZB to ~13 KB).

The game segment has **55.8 KB** free for the features in section 2, and the
renderer segment **16.7 KB**.

### 8.2 Speed, measured (MartyPC `os8088_5150_cga`, the same system disk)

| | `7232b890` | split |
|---|---|---|
| **P bench, 40 columns** | 19.1 fps (MS 52.2, R 44.8, P 5.3) | **18.9 fps (MS 52.7, R 45.2, P 5.4)**: +0.96% |
| **P bench, 80 columns** | 15.8 (MS 63.0, R 51.5, P 9.4) | **15.7 (MS 63.3, R 51.7, P 9.5)**: +0.5% |
| B bench, 40 columns, full / sparse | 33.7 / 37.0 (MS 29.6 / 27.0) | 33.2 / 36.4 (MS 30.1 / 27.4) |
| B bench, 80 columns, full / sparse | 26.8 / 31.0 (MS 37.2 / 32.2) | 26.5 / 30.6 (MS 37.6 / 32.6) |

**The glue, by guest cycles a frame** (breakpoints at each boundary, 25
frames of play): **1,422** from the primary's `wa_render` to the
renderer's (copying FRAME and the three figures' heads, 35 words, and
entering), **246** back (the frame number, the drawn flags), **282 + 149**
round `PRESENT`: ~2,100 cycles, ~0.44 ms. That is section 3.4's version 1.
Version 2 (the sim writing the renderer's copy through ES) was not built:
its derived saving is ~600 cycles a frame (~0.25%), against rewriting every
figure access in the sim, which other branches are editing.

### 8.3 Correctness

- **Posed text VRAM and the figures' projection fields** against the
  `7232b890` build, seven sets on MartyPC: 40 and 80 columns with
  sparse/hold/grid mixed (120 poses each), FOV 60 at 40 and 90 at 80 (100
  each), far figures (100), the open arena (60), Hercules at 80 (60).
  After step 3, all seven identical. After step 4, six identical; in the
  40-column mixed set two poses differ in one or two top-edge cells with
  held edges on - and **the base build makes exactly the same picture when
  its frame counter is advanced by one before the dump** (the same md5 over
  100 poses). So those two poses depend on the renderer's frame-counter
  phase, which the dump's warm-up fixes by timing: a property of the
  renderer before the split (held edges and the stamps' lap), not of the
  split. It is worth a look by whoever owns `EDGE`.
- **The pistol**: a dummy held 5 m ahead and shot six times, hits, flinches,
  the kill and the misses after it identical shot for shot.
- **Arenas in turn**: five matches alternating the full and open arenas at
  both widths, each fetched, drawn and dropped; no toast.
- `tools/wacheck.py --poses 30`: 6 poses with an edge off, as the base.
- **kern_mega** (Bochs, 386, 16 MB): the region above 1MB (`OS88_HIGH`), the
  part a conventional selector pair through `op_seg`/`op_cseg`, the arena
  fetched as a selector, the bracket in protected mode (no `FSXF_REAL`),
  the full arena drawn with its figures, Tab to 80 columns, Esc back; no
  fault and no `kern_mega CLOSED`. kern_mega WORKS; no `PKG_MEGA` arm.
- **kern_small**: refused at entry, before a sector of the parts is read,
  with the toast *Needs kern_big or mega* (`OSAPI_MOUSE_REL`'s refusal,
  SPEC.md §9.14, is the test).

### 8.4 Left over

- The frame-phase dependence above, for the renderer's owners.
- The game is an `instrument` (`apps/RETIRED.txt`) with no suite row. Once
  it ships, the rows section 4.2 names; `pins`, `megapkg`, `partoff`,
  `stkapps` and `pkgdeps` already cover what this branch touched.
- Section 3.4's version 2, if a later measurement wants the ~0.25%.

### 8.5 The sine table: one quarter wave, in the game only (2026-11-13)

The owner asked whether the duplicate `wa_sin` (2,048 bytes in each half) was
there for speed. It was not: R looked it up once a frame, for the camera, and
G a few times a tick. The owner: parts should be able to talk to each other,
so passing the angle is fine, and so is a quarter table, as long as neither
costs speed.

- **R has no table.** G's `wa_rframe` puts the camera's sin and cos (Q14, at
  `wa_ang >> 6`) into FRAME as `wa_fsn` and `wa_fcs`, 4 bytes more of the
  per-frame copy. R's camera setup reads them where it called `wa_sincos`.
  `WR_ABI` is now 2, because FRAME's layout moved.
- **G's table is a quarter wave**, sin(0..256), 257 words. `wa_sincos`
  (`wasin.inc`) mirrors it into the other three quadrants. A host check over
  all 1,024 angles gives exactly the old table's entries.
- **Bytes:** G's image 8,594 → 7,115. Part 0 48,282 → 46,214 in memory, and
  13,055 → 11,100 bytes packed on disk.
- **Speed:** P bench 18.9 fps at 40 columns and 15.7 at 80, unchanged.
- **Picture:** six of section 8.3's seven posed sets (620 frames: 80
  columns mixed, all three FOVs, far figures, the open arena and Hercules)
  are identical to `a4edcb7f`, VRAM and trigger fields. The seventh, 40
  columns with sparse/hysteresis/grid mixed, differed in ONE top-edge cell
  of 120 (`─` against `_`). A rerun moved it to 2-3 other frames. Every
  differing frame had held edges (H) on.
  - With H forced off in that set's 120 frames, the two builds give the
    same md5.
  - So H's output carries state from earlier frames and is not repeatable
    under the dump. The old build happened to repeat itself (its runs at
    frame offsets 0, 1 and 2 agreed); the new one did not.
  - The commit message's "740 posed frames identical" overstated it; this
    is the accurate account.
  - H made no visible difference in the owner's field run (16.4), and it is
    a candidate when the toggles are tidied.

### 8.6 The cold keyframes, a part of their own (2026-11-14)

The owner, after the railgun's death left the renderer part 1,115 bytes
under its 65,024: *"Might be time to look at the part layout and see what
would be performant to move into a different part. Else pretty much
anything is going to overrun it."*

**What the renderer part held** (`build/wrpart.bin`, mapped):

| | bytes | touched |
|---|---:|---|
| code and small tables (`wrender.inc`, `wrlow.inc`, `wrtab.inc`, header, frame block) | ~18,300 | every frame |
| the figures' keyframes (`wa_pose`), 40 of 352 bytes | 14,080 | two facing rows a near figure a frame |
| their bounds (`wa_ktop`, `wa_krch`) and offsets (`wa_kfo`) | ~240 | a near figure a frame, before its joints |
| bss: five screen-sized cell buffers (`wa_lista`, `wa_listb`, `wa_fbuf`, `wa_cur`, `wa_fmask`) | 19,199 | every cell, every frame |
| bss: the reciprocal table (`wa_rcp`) | 5,764 | every column |
| bss: the rest | ~6,300 | |

**The candidates, by what a move costs a frame.** Everything in bss and
the code is on the per-cell or per-column path: moving any of it puts a
segment switch where the renderer spends its time. The cold code (the
arena loader, the init and geometry verbs) is ~1-2 KB - not enough to
matter. **The keyframes are the one large block read rarely**: 22 bytes
twice per near figure per frame, by two pointers (`wa_pa`, `wa_pb`) that
`wa_figure` sets and the joint loops (`WA_FJ`, `WA_FJN`) follow. Those
loops read with `lodsw` and write the joints with `stosw`, so ES must be
DS there and the keyframes cannot simply be read through ES: the rows
have to be in DS when the loops run.

**As built**:

- **The hot keyframes stay**: IDLE and RUN, 12 of the 40 (4,224 bytes),
  where a figure is nearly all the time - read in place as before.
- **The cold ones move** - a flinch and the five deaths, 28 keyframes
  (9,856 bytes) - to `apps/wirearena/wapose.asm`, part row 1 (`OP_ASSET`,
  `OP_COMP`, eager: the carve's rows come before every lazy row, so the
  arenas moved to rows 2..). `tools/arenagen.py` writes the two halves as
  `WA_TAB_POSE0` and `WA_TAB_POSE1` (`HOTANIMS`, which must lead
  `ANIMS`), and `wa_kfo` marks a cold keyframe's offset with bit 15.
- **`wa_kfrows`** (`wrender.inc`): a hot keyframe returns at once. A cold
  one's two rows are copied into DS, into rows of the figure's own (two
  per figure, either one A or B), keyed in two words of the PART's copy
  of the figure record (`WR_FKA`, `WR_FKB`, bytes 36-39, which the game
  never sends - `wrpart.asm` asserts it). So a figure lying dead copies
  nothing, and an animation stepping on - its new A its old B - copies one
  row. The joint loops are unchanged.
- The pose part's segment is in the frame block's CONFIG (`wa_posg`);
  `wa_partsmv` re-reads it when the carve moves and writes the part's copy
  at once.

**Measured** (MartyPC `os8088_5150_cga`, against the commit before):

- **Pictures**: `tools/waposes.py`, 60 heights poses at 40 columns and 40
  full-arena poses at 80, figures in every animation: **all 100 identical**,
  glyph and attribute.
- **Render cycles** (the same sets): **+0.25% and +0.28%** a pose.
- **The play bench** (`tools/wabench.py`): **P40 26.1 fps, MS 38.2, R 31.9;
  P80 22.2 fps, MS 44.9, R 35.4 - the same as before to the tenth.**
- On the way: all 40 keyframes in the pose part cost **+1.2% to +1.5%** of
  render and 0.2 fps on the play bench - every running figure copying -
  which is why the hot ones stayed.

**Bytes**: the renderer part **63,909 -> 54,354, 10,670 under its cap**; the
pose part 9,856; `wirearena.bin` +56 (a row of the table and its glue).

**Not done, and the next lever if it fills again**: moving the hot keyframes
too gives back 4 KB more for ~1% of render (measured above). Past that, the
bss is the renderer's working set, and the next real room is a renderer
built for it (a smaller `wa_rcp`, cell lists sized to the width in use).

**The tools**: `waposes`, `wabench`, `waprof`, `wacheck` and `watrace`
started their matches from the Game menu's Play items, which went in
NET-PLAN 9.4; they start from the setup now (`walift.start`, or
`walift.start_keys` by its keys for a tree whose map they do not build).
