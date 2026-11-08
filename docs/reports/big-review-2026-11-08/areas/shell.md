# Desktop shell: desk, dock, extended desktop, Windows theme, Sticky Apps

Reviewer area key: `shell`. Tree: `elendilon` at `5e4740b`. Read-only review:
no build, no emulator, no row run. Module image sizes below are read off the
existing `build/kernel.map` (`mod?_end`), not re-measured.

## Scope read

Every proc in these files was read, not sampled:

| file | lines | arm(s) |
|---|---:|---|
| `kernel/desk.inc` | 2,395 | both kernels; `.ovl`, `.cold`, `.text` data, `.bss`, `.modu`, kern_small `.boot2` |
| `kernel/deskmod.inc` | 389 | kern_big (`DESK.DRV`, `.modj`) and its resident `.cold` thunks |
| `kernel/desksc.inc` | 2,084 | kern_big (`OS88_SHORTCUTS`): `.cold`, `.text`, `.modc` (CTRL.DRV), `.boot2`, kern_mega `.cold` arm of AUTOSTART |
| `kernel/dock.inc` | 1,327 | both; `DOCK_OPT` (kern_big) vs the kern_small `%else` |
| `kernel/dockmod.inc` | 1,696 | kern_big (`DOCK.DRV`, `.modk`) |
| `kernel/extmod.inc` | 1,614 | kern_big (`EXTD.DRV`, `.modx`), `EXF_SYNC` macro |
| `kernel/winmod.inc` | 1,028 | kern_big (`WIN95.DRV`, `.modw`) and `thv` |
| `kernel/stikmenu.inc` | 2,106 | kern_big (`STIKMENU.DRV`, `.modn`) and its `.cold` thunks |

Also read where the shell's contracts land: `kernel/icons.inc` 86-215
(`icon_draw_x`, `ico_draw_common`), `kernel/loader.inc` 1440-1470 (step 9's
`IF_DESK`), `kernel/instance.inc` 20-50, `kernel/mouse.inc` 3458-3486
(`kbm_pollm`), `kernel/menu.inc` 2573-2581, `kernel/ui.inc` 2460-2480,
`kernel/mega.inc` 14726-14772 (`mega_dklibf`), `kernel/font.inc` 440-460 and
2560-2590 (seam cells), `apps/os88stikc.inc` 140-200, `apps/os88stik.inc`
950-1000 and 1475-1492, `apps/stiklib.asm` 1-40, `apps/win16/w16progm.inc`
680-745, `apps/os88api.inc` 370-385 and 1405-1460 and 5489-5497.
SPEC.md: §26.7 (head), §26.8.6-26.8.7, §26.9 through 26.9.9 (head),
§11.103.9-11.103.10, §30.5, §39.19.6 with 39.19.6.1-2, §76.14.1-76.14.4,
§110.12 (cost and not-done lists), §110.15 (lifetime). Plans:
`docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md`,
`docs/plans/STICKY-APPS-NEXT.md`, `docs/plans/completed/ONDEMAND-PLAN.md` 7,
`docs/plans/HEAP-UNPIN-PLAN.md` piece B, and
`docs/reports/CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (nothing in this area).

## Verdict

The shell is the most carefully engineered part of the tree I have read, and
the engineering is mostly the right kind: one byte per desktop item
(`[desk_zslot]`) and one reconciling routine (`desk_reflow`) that knows only
liveness, a cell drawn exactly once into the damage region, a dock that diffs
tile keys and XORs marks, and four on-demand modules that put the decision
code on disk while every per-primitive path stays resident and near. Hostile
input (SYSTEM.CFG trailer, `.LNK`, a package's link record) is sanitised at
each door. What needs work is mostly *lifetime and cost on the target*, not
correctness of the drawing: the Sticky App library's lifetime is keyed on a
proxy (`IF_DESK`) rather than on who holds pointers into it, which is a latent
use-after-free; every desktop gesture and every Sticky Apps menu press pays a
module load that the shell's own comments call unacceptable elsewhere
("an XT pays 2-3 s to load an image, which is a menu that does not come up",
`desksc.inc:13-15`); the dock painter exists twice in source; and a long tail
of comments and SPEC text still describes the designs before kernel size
passes 12-14 and before §110.15 (claims "held until restart", "a 1 KB claim",
`dkf_apply`, `wm_land_fit`, `desk_svflag`). No ABI-forced rethink is needed;
the one ABI-shaped weakness (`OSAPI_DESK_ITEM` remove keyed by a zone that is
not an identity) has an additive fix.

## Strengths

1. **One table, one reconciler.** `desk_reflow` (`desk.inc:853-948`) is told
   nothing about events; it recomputes from liveness (`desk_live`,
   `desk.inc:690-721`) and posts exactly the cells that changed. "A volume
   came", "the Wire went", "the screen changed shape" and a hostile
   SYSTEM.CFG pin block are all the same call. This is the model to copy for
   any other placement problem (dock packing already half-does it in
   `dkx_map`).
2. **Every pixel once, by construction.** `desk_col`/`desk_cap`
   (`desk.inc:1673-1973`) compose the picture column and caption in RAM and
   put each down with one `gfx_blit1`, the ground going round them
   (`desk_g2`); `desk_zones_r_x` (`desk.inc:2008-2087`) draws cells INTO
   `wm_dmg_gray`'s region with a provable overflow bound instead of an
   optimistic subtraction. PERFORMANCE rule 2 kept in the code, not only in
   prose.
3. **Dock key diff with self-inverse marks** (`dock.inc:660-730`): a focus
   change is one `gfx_xor_rect`, never an erase-and-rebuild; the damage span
   is its own flag with the `07FFFh..08000h` empty sentinel, and the span
   exchange is bracketed `pushf/cli/popf` (`dock.inc:576-580`) so a
   concurrent `db_force` cannot be lost.
4. **Two principled hook shapes, chosen by what "no module" means.** Where the
   absent answer is a kernel body (`dkv`, `dock.inc:366-383`; `thv`,
   `winmod.inc:69-79`) the vector table rests on that body and the module
   writes both halves (`dkx_vset`, `wnx_set`); where the absent answer is a
   refusal (EXTD, DESK, STIKMENU) the call goes straight at the `mod_fp` slot
   resting on `mod_gone`. No dispatcher, no index register, no resident
   second copy. The `[vid_ndisp] > 1 only while EXTD.DRV is mounted`
   invariant (`extmod.inc:22-29`) is what makes the thunk-less form safe and
   it is kept by construction in `EXF_SYNC`.
5. **The hand-built far frame onto `cw_kretf`** (`dockmod.inc:1663-1694`,
   `exk_go` `extmod.inc:1595-1612`): a module reaches any near kernel routine
   with zero resident shim. Mac trap glue and Win16 thunks both needed a
   resident stub per entry; this does not.
6. **Hostile bytes are bounded where they enter.** `ovl_sc_load`
   (`desksc.inc:1752-1872`) terminates every string and turns a bad kind or
   volume into a free row; `sc_m_add` (`desksc.inc:837-904`) re-stamps the
   picture header and forbids a forged Sticky mark; `lnk_open`
   (`desksc.inc:335-403`) bounds the volume, forces the path NUL and bounds
   link chains by remaining stack, not by a counter.
7. **Lifetime care that is easy to get wrong.** `desk_need` saves the outer
   `drv_vol` pair across a nested bracket (`deskmod.inc:89-100`); `desk_drop_x`
   counts unpublished records ("any record, not any live record",
   `deskmod.inc:110-116`); `stik_boot` re-reads `[sc_seg]` after a load may
   compact (`desksc.inc:287`).
8. **Layouts are asserted, not trusted**: `DSV_R_SIZE`, the SC record tiling,
   `ICO_BSTR`, `DKXPAD` pad positions, `stm_ctab`/`stm_atab` order, the
   dword zone mask (`desk.inc:1257-1259`).

## Findings

Ranked most valuable first. Bytes are signed and placed (resident `.text`/
`.cold`/`.bss`, a module image, or `.boot2`).

### shell-1 (hazard, medium, now) — the Sticky App library can be freed under a widget that bound it

`DESK.DRV` is dropped when no `inst_tab` record carries `IF_DESK`
(`deskmod.inc:118-134`). The loader's step 9 clears `IF_DESK` and calls
`desk_drop_x` for any launch whose returned window is not `WF_DESK`
(`loader.inc:1458-1466`). But a `STIK_LIB` widget has ALREADY bound the
library before that: `stik_load` asks LIB and writes the library's segment
into every far pointer of `stik_le` (`apps/os88stikc.inc:152-176`). In the
library build `STIK_DESKONLY` is forced (`apps/stiklib.asm:28`), and under it
`stik_made` issues MAKE and ignores its CF (`apps/os88stik.inc:962-966`: "its
refusals are all a caller's mistakes"). So if MAKE ever refuses - any of
`dkd_verb .make`'s six tests (`deskmod.inc:239-258`) - the window comes back
titled, step 9 drops `DESK.DRV` (it is the only widget), and the next
`call far [stik_le + ...]` jumps into a freed heap claim. Today every MAKE
precondition holds for a correct package, so this is latent; the failure mode
(wild far call) is the worst one the kernel has.

The drop rule uses "has a desk window" as a proxy for "holds pointers into
the library". Recommendation, cheapest first: (a) the library's `stik_made`
refuses the launch when MAKE refuses (CF out, the entry proc already returns
`stik_load`'s CF elsewhere) - module bytes only; or (b) the Amiga shape: the
LIB verb sets an `IF_LIB` bit (I_FLAGS bit 3 is free, `instance.inc:27`) on
`[ld_rec]`, `desk_drop_x`'s scan tests `IF_DESK|IF_LIB`, and only the record's
free clears it (inst_alloc zeroes the record). (b) is the right model -
library lifetime by binder count - and costs the same resident byte count as
today's test (only the immediate changes).
Bytes: (a) +4..+6 DESK.DRV, 0 resident; (b) +0 resident `.cold` (mask), +6
DESK.DRV, plus the teardown sites' existing clears.
Prior art: AmigaOS `OpenLibrary`/`CloseLibrary` and `lib_OpenCnt`; a library
is expunged only at count 0, never on a proxy.

### shell-2 (weakness, medium, now) — module loads on interactive paths the shell itself calls too slow

`desksc.inc:13-15` moved the shortcut popup resident because "an XT pays
2-3 s to load an image, which is a menu that does not come up". The same
shell then (i) loads and drops `MOD_SETS` (2,206 bytes of CTRL.DRV) for EVERY
drag, delete, new shortcut and package link (`sc_modcall`,
`desksc.inc:437-462`), (ii) loads `STIKMENU.DRV` (7,876-byte image,
`modn_end` 0x1EC4) and rescans `STIKAPPS\` on every press of the Sticky Apps
cell, then drops it (`stikmenu.inc:60-88`), and (iii) WIN16's DeleteGroup
probes up to `W16PG_SCN` zones with `OSAPI_DESK_ITEM` remove
(`apps/win16/w16progm.inc:721-734`), each probe a `sc_modcall` - a load and a
drop per miss. §110.12 prices a menu drop at ~2-3 `int 13h` calls, about a
second, on a floppy 5150.

Recommendation: make these images DISCARDABLE rather than dropped - kept as a
low-priority purgeable claim after use and shed by `mem_claim`'s
shed-and-retry. `ONDEMAND-PLAN.md` 7.2 proposed exactly this ("Discard: shed,
do not free"), and HEAP-UNPIN-PLAN piece B priced it at ~215-240 resident
bytes for all modules with a per-span pin. The new argument is that the
shell's three gesture paths above are the strongest beneficiaries and need
the cheapest version: only `MOD_SETS` and `MOD_STIKM`, both of which are
entered only from the UI task with nothing re-entrant, so the per-module
nesting count of ONDEMAND-PLAN 7.1 can be the existing "loaded" row plus one
purge tag. A second visit on a 640 KB machine then costs nothing.
Bytes: estimate +40..+80 resident (`mod_drop`'s discard arm and the purge
tag), 0 module; the full piece B is +215..+240.
Listed as: ONDEMAND-PLAN 7.2; HEAP-UNPIN-PLAN piece B; SPEC §110.12's "No
cache of the scan".
Prior art: Windows 3.0/3.1 DISCARDABLE code segments - reloaded from the
executable only when the segment was actually discarded under pressure; Mac
Resource Manager purgeable resources.

### shell-3 (rethink, medium, now) — `OSAPI_DESK_ITEM`'s remove is keyed by a zone, which the API itself says is not an identity

The remove takes "the link in zone AH, ES:SI = the record it added"
(`apps/os88api.inc:1441-1444`), and `sc_m_pdel` then has to re-verify volume
and path because "a zone is reused, and renumbered at boot"
(`desksc.inc:1449-1468`). A caller that did not keep the zone (every caller
after a reboot) has to PROBE: WIN16 loops zones until one matches
(`w16progm.inc:721-734`), each probe a module load (shell-2). The Sticky App
row already has the right shape - `AH = 0xFF`, find by name
(`sc_m_dfind`, `desksc.inc:935-973`). Recommendation: an additive
`AH = 0FEh` "the row whose volume and path are this record's" that walks rows
with the existing `sc_m_same` (`desksc.inc:978-1002`); WIN16 then makes one
call. At the next flag day, re-contract remove to be by record always and
drop the zone argument.
Bytes: +~12 CTRL.DRV (`.modc`), 0 resident; WIN16 -~12 package bytes.
Flag-day follow-on: -~6 resident `.cold` (`osapi_desk_item_x .pdel`'s zone
test, `desk.inc:2283-2284`).

### shell-4 (consistency, medium, now) — the dock painter is written twice

`db_paint`/`db_key`/`db_tile_rect`/`db_draw_tile`/`db_click`/`db_rclick`
(`dock.inc:499-1271`) and `dkx_paint`/`dkx_key`/`dkx_tile_rect`/
`dkx_draw_tile`/`dkx_click`/`dkx_rclick` (`dockmod.inc:505-1297`) are the same
algorithm hand-copied, including their prose invariants ("EXACTLY ONE EXIT",
the `dock_drew` wrap argument, the `xchg` bracket). They have already drifted
in their comments (both still say "the five pops at the bottom",
`dock.inc:521`, `dockmod.inc:539`, where the exits pop six registers plus the
pen and, in the module, two more words). The tree has an established idiom
for exactly this - a macro assembled per image (`DESK_ROWCALC`,
`DOCK_GEOM`, `DOCK_APPLY`, `EXF_SYNC`). Recommendation: one `DOCK_PAINT`
body parameterised on the geometry source (immediates for the basic strip,
`[cs:dock_a0]`/`[cs:dock_astep]` and `dkx_swap_side` in the module), so a fix
lands once. Bytes: 0 (same expansions); this is source, not size.

### shell-5 (consistency, low, now) — three textual copies of the far-frame helper

`exk_go` (`extmod.inc:1595-1612`), `wnk_go` (`winmod.inc:902-919`) and
`smk_go` (`stikmenu.inc:1973-1990`) are byte-for-byte the same 19
instructions; `dockmod.inc`'s `dkk_*` is the per-routine form of the same
frame. Each image must carry its own (they cannot share code), but they
should share SOURCE: one `%macro MODK_GO name` in `kernel/mod.inc`. 0 bytes.

### shell-6 (hazard, low, now) — `menu_items_file+2` is poked by raw offset from four places

`desk_select` (`desk.inc:1401-1407`), `desk_unsel_x` (`desk.inc:1452`),
`sc_m_del` (`desksc.inc:1492`) and `sc_rclick_x` (`desksc.inc:517, 541`)
address "item 1 of Locator's File menu" as `menu_items_file+2`; the table's
own comment names the wrong writer (`menu.inc:2576`, "swapped by
ui_loc_gate"). Recommendation: `MENU_FILE_REM equ menu_items_file + 2` beside
the table with an `%if MENU_FILE_N != 2` guard, used at all five sites.
0 bytes.

### shell-7 (weakness, low, now) — `stm_a_more` writes the kernel's service record from a module

With no driver registered, "More Sticky Apps..." copies `THEWIRE.O88` and
`The Wire` into the kernel's `desk_svc_file`/`desk_svc_name`
(`stikmenu.inc:1844-1864`) so that `ui_svc_open` can be reused, leaving a
half-written service record (no caption, class or verb) in resident `.bss`
for the rest of the session. It is gated today by `[desk_svc_seg] = 0`, but
it makes `desk_svc_rec` a two-writer structure whose second writer is a
module nobody reading desk.inc would look in. Recommendation: give
`ui_svc_open` the file/name as DS:SI/DS:DI inputs (the zone passes its
`.bss`, the module passes its own strings through a far-copy) or have the
module call the loader by name directly as `stm_a_launch` already does.
Bytes: about 0..+4 resident.

### shell-8 (bug, low, now) — the boot reader keeps up to 1 KB more shortcut claim than the rows need

`ovl_sc_load` claims the WHOLE file (settings head + rows + pins + footer) as
the shortcut table and moves the rows down to its base
(`desksc.inc:1768-1846`), but never shrinks it; `[sc_nrows]` becomes the
file's row count, so the claim stays file-sized until the first add/delete
regrows it (`sc_m_kb`). With `CFG_FBUF` about 140 bytes, 7, 8 or 15 rows put
the file over a KB boundary: a 2 KB claim for 1 KB of rows (3 for 2 at 15).
Movable, so not a wall, but a KB of a 128-640 KB heap for the session.
Recommendation: a `mem_regrow` to `ceil(rows/8)` KB after the row move, in
`.boot2`. Bytes: +~10 `.boot2` (no resident byte; the blob is tight, so this
is the owner's call against the blob budget).

### shell-9 (perf, low, now) — every damage pass recomputes every shown cell's rect with a `div` and two `mul`s

`desk_dmg_zones_x` (`desk.inc:1260-1302`) calls `desk_cell_rect` ->
`desk_cell_xy` (`cbw/cwd/div word`, `mul bx`, `mul word`, `desk.inc:1005-1024`)
and a far `cw_wm_dmg_hit` for every shown zone on every window move, close or
raise: roughly 700 cycles (~150 us) a shown zone, ~0.9 ms for six, before
anything is drawn - and the commonest damage (a window in the left two thirds
of a VGA screen) touches no cell at all. Recommendation: one early-out of the
damage rect against the grid's x extent (the leftmost column's left, derived
once in `DESK_GRID` into a word) before the walk.
Bytes: +2 `.bss`, +~6 per `DESK_GRID` expansion (CTRL.DRV and the blob/`.cfx`),
+~8 `.cold`.

### shell-10 (perf, low, now) — WIN95.DRV shifts button art one bit at a time

`wn_blit` (`winmod.inc:456-507`) phases each 16-bit art row into three bytes
with `shr ax,1 / rcr bp,1 / loop` per bit: up to 7 x ~21 cycles a row, 13 rows,
three buttons and the grip - ~0.5-1 ms of CPU per title bar on the 8088 under
the Windows theme, on top of the eleven primitive calls. `shr ax,cl` for the
two bytes plus `shl` of a copy of the low byte by `8-cl` for the spill is
~2x fewer cycles. Bytes: +~6 WIN95.DRV, 0 resident.

### shell-11 (hazard, low, now) — the basic `dock_geom` only works at boot or with CTRL.DRV loaded

`dkb_geom` (`dock.inc:213-220`) is the boot blob's copy while `[spl_fseg]` is
set and otherwise `call far [CPFP + CPE_DOCK_GEOM*4]`. Every caller today is
the boot, CTRL.DRV's `DOCK_APPLY` and the Display page's `VSW_DISP`
(`vidsel.inc:1047, 2121`), so it holds; but a future caller outside the panel
(an fsx restore, a hot-plugged display) gets `mod_gone`'s CF = 1 and NO
geometry, silently. Recommendation: say it in `dkb_geom`'s banner as the
invariant it is, or fall back to `dkb_band` on CF = 1 (the band words at
least). Bytes: 0 (comment) or +3 resident `.text`.

### shell-12 (consistency, low, now) — a driver's service record is half-sanitised

`osapi_desk_item_x` force-terminates the caption only
(`desk.inc:2247-2251`); the 13-byte launch file and 12-byte header name are
copied as given and read later as NUL strings. Drivers are kernel-trusted, so
this is not a security finding, but the SDK prose says "Both strings are
terminated by the kernel whatever you put in them"
(`apps/os88api.inc:1446-1447`) right under the driver's record. Fix the prose
(doc mismatch below) or terminate both. Bytes: 0, or +10 resident `.cold`.

### shell-13 (perf, low, record) — `desk_reflow` is O(cells x zones) per placement

`desk_free` calls `desk_owner` (a 24-entry scan) for each cell from 0
(`desk.inc:731-773`), and pass 2 calls it per item; a screen-shape reflow
re-places every flowing item. Worst realistic case is tens of ms on the XT,
once, at a display switch. An occupancy bitmap built once per reflow (the
`desk_cdirty` shape, 8 bytes) would make it linear. Not recommended now:
+~30 resident bytes for a path that runs at the rate of a mount.

### shell-14 (weakness, low, record) — kern_small still draws a cell two and three times

kern_small's `desk_draw_zone` (`desk.inc:2090-2137`) fills the whole zone rect
with the ground, then the icon over it, then a white caption rect over the
ground, then an opaque `font_run` over the white: the caption band is written
three times and the picture twice on the 128 KB machine, which is the slowest
one. §26.9.9 records kern_big's fix as kern_big-only by design. A cheaper
kern_small partial exists (ground only above the caption band, plus two
side strips) for about +2 primitive calls (~1.5 ms) and ~+20 resident bytes;
whether the flash or the calls cost more on that machine is a measurement
(`flicker`) the owner may want before deciding.

### shell-15 (weakness, low, record) — the Windows title bar fills its ground under the buttons and caption

`wn_title` fills the whole bar (`winmod.inc:276-281`) and then the button
bands and the opaque caption land on it, so ~3 x 15 x 13 button pixels and the
caption's cells are written twice on every title paint. §76.14.3 records the
decision (five fills would cost more calls than they save). Logged for
completeness against PERFORMANCE rule 2; the composed-bar knob is the
principled way out if the theme becomes a default.

## Rethinks

* **Library lifetime by binder count, not by window kind** (shell-1). The
  STIKLIB design put shared code in a module whose lifetime was chosen for a
  different purpose (the desk layer). Model it as a library: LIB = open
  (count up on the instance), instance free = close, drop at zero. Zero ABI
  break; it is the only change that makes the library's safety a property
  instead of a coincidence.
* **Discardable modules for the interactive ones** (shell-2). The module
  system's only lifetimes are "held while a feature is on" and "freed when
  done". Win16's third lifetime - discardable, reloaded on demand - is the
  one desktop gestures want, and it is already designed in ONDEMAND-PLAN 7.
* **Remove by identity** (shell-3): an additive verb now, the zone argument
  retired at the next flag day.
* **One painter source for the dock** (shell-4) and **one far-frame helper
  source** (shell-5): the house's own macro-per-image idiom, applied where it
  was not.
* Not recommended, recorded so it is not re-derived: **desktop shortcuts as
  `.LNK` files in a `DESKTOP\` folder** (the Win95 model). It would delete the
  SYSTEM.CFG trailer and `ovl_sc_load`, but every icon on the desktop would
  then cost a directory walk and a read per link at boot (~400 ms each on the
  XT) where today's cached 64-byte picture costs none. The current split -
  Progman-style cache for the desktop, `.LNK` for links inside folders - is
  the right one for the target.

## Prior art

* **Mac Finder (System 1-7)**: icon positions live with each file
  (`FInfo.fdLocation`) and icons come from the Desktop file. Icons never
  "flow"; a mounted disk lands wherever its saved location says and can
  overlap. os8088's placed/flowing distinction with a reconciler is better
  for drives that come and go. "Clean Up" is what `stm_a_arr` does for
  widgets.
* **Windows 3.1 Program Manager**: `.GRP` files cache each item's icon bits,
  exactly the choice `SC_R_BODY` makes and for the same reason (no
  per-icon disk read). Windows 95 then moved to `.LNK`-in-a-folder, which
  os8088 also has (§26.10). Both models coexist here, correctly.
* **Windows 3.x discardable segments / Mac purgeable resources**: the
  missing third module lifetime (shell-2).
* **Mac trap patching (`SetTrapAddress`) and AmigaOS `SetFunction`**: the
  `dkv`/`thv` vector tables. os8088's version is single-owner and the owner
  restores the kernel's own entries on the way out, which avoids the
  INIT-ordering and "tail patch" bugs that chained Mac patches suffered.
  Keep it single-owner.
* **Mac WDEF**: per-window chrome as a code resource. `thv` is a system-wide
  WDEF; per-window chrome is not needed and would cost a pointer per window.
* **AmigaOS libraries**: open count governs expunge (shell-1).
* **Mac II Color QuickDraw `GDevice` list (1987)**: each device a rect in
  global coordinates, the menu bar on the main device, drawing split per
  device - the same model as `vid_ctx`/`GFXDISP`/§39.19. os8088 goes further
  in two places the Mac did not: the dead-zone pull (`ext_e_dead`) and the
  per-adapter preferred size on landing (`ui_drag_size`).
* **Mac desk accessories / GEOS desk accessories**: Sticky Apps are DAs that
  live on the desktop layer. System 7's Apple menu cached the Apple Menu
  Items folder and rebuilt only when the folder changed - the cache §110.12
  lists as not done.
* **Windows 95 taskbar auto-hide** is `dkx_pass`'s model; os8088's clip-hole
  approach (no save-under, windows keep drawing round the open strip) is
  better on a machine where a save-under of a strip is a KB.

## Doc/code mismatches

1. `kernel/desk.inc:2374` - "one compare in desk_svflag is the whole of what"
   - there is no `desk_svflag`; the compare is `desk_live`'s `.svc`
   (`desk.inc:699-701`). Proposed: "one compare in desk_live (its `.svc` arm)
   is the whole of what".
2. `SPEC.md:58406` - "`desk_svflag`." - same; proposed: "`desk_live`'s
   service-item arm."
3. `kernel/desk.inc:2387` - "osapi_desk_svc copies DSV_R_SIZE bytes into it"
   - the copier is `osapi_desk_item_x` (`desk.inc:2236-2245`).
4. `kernel/desk.inc:2391` - "BYTE, because DESK_NZ bounds it at 8" -
   kern_big's `DESK_NZ` is 24 (`desk.inc:103-107`); proposed: "a BYTE,
   because DESK_NZ bounds it (24 on kern_big, DVOL_MAX on kern_small)".
5. `kernel/deskmod.inc:50` - "CF = 0 the layer is resident from here until
   restart" - since the drop rule it is held until the last widget goes
   (`desk_drop_x`, `deskmod.inc:118-134`).
6. `kernel/deskmod.inc:73` - "so the 1 KB pinned claim lands ABOVE" - the
   image is 4,520 bytes (`modj_end` = 0x11A8 in `build/kernel.map`) since the
   library rides it; proposed "so the pinned claim (~5 KB with the library)".
7. `kernel/deskmod.inc:18` - "Nothing else loads it" - the loader's step 5
   loads it too (`desk_need`'s second caller, `deskmod.inc:72-77`).
8. `kernel/deskmod.inc:374` - "pinned, held until the restart" - dropped
   when no record carries `IF_DESK` (`deskmod.inc:118-134`).
9. `apps/os88api.inc:378-379` - "holds a 1 KB claim / until restart" -
   proposed: "holds a ~5 KB claim (the desk layer and the Sticky App library)
   until the last desk window's instance goes".
10. `SPEC.md:22610` - "and `DESK.DRV` is now loaded and held until restart"
    and "no heap for its 1 KB claim" - same correction (§11.103.10's own
    lifetime paragraph already says it).
11. `SPEC.md:223672` - "it is pinned and held until the restart" - pinned,
    and held while any widget record exists (`desk_drop_x`).
12. `SPEC.md:221361` - "a 1 KB claim from a Sticky App's first QUERY until
    restart" (§110.6 cost row) - superseded by §110.15 and the drop rule; add
    a pointer rather than rewrite the measurement.
13. `kernel/instance.inc:42-48` - "Set by OSAPI_WM_DESK's MAKE on the
    CALLER's record ... before the loader's step 9 publishes it (which writes
    no I_FLAGS)" - MAKE writes no `IF_DESK` (`deskmod.inc:234-271`);
    `ld_alloc` sets it early (`loader.inc:1232-1233`) and step 9 sets or
    clears it off the returned window (`loader.inc:1458-1466`).
14. `kernel/dock.inc:131` - "One caller, kmain:4525." - the call is
    `kernel/kernel.asm:5890` (`OVLCALL dock_init`); proposed "One caller,
    kmain (`OVLCALL dock_init`)" so it does not go stale again.
15. `kernel/dock.inc:182-183` and `SPEC.md:68612` - "Four operations exist
    only while a module is mounted" - five slots rest on `dkb_ret`/`dkb_clc`:
    DROP, PASS, HOLE_ARM, HOLE_SUB, HOLE_WIN (`dock.inc:376-380`).
16. `kernel/dock.inc:209`, `kernel/dockmod.inc:19`, `kernel/winmod.inc:93`,
    `SPEC.md:80126`, `SPEC.md:135256`, `SPEC.md:135264` - "dkf_apply" - it is
    `dock_apply` (CTRL.DRV) and `bdk_apply` (the blob), the `DOCK_APPLY`
    macro (`dock.inc:303-340`); SPEC.md:68594 already records the rename.
17. `kernel/dock.inc:30`, `kernel/dock.inc:1299`, `SPEC.md:68122`,
    `SPEC.md:68221`, `SPEC.md:68536` - "dock_key" - the routines are `db_key`
    (`dock.inc:865`) and `dkx_key` (`dockmod.inc:886`).
18. `kernel/dock.inc:850-851` and `kernel/dockmod.inc:871` - "I_ICO_PKG
    (loader.inc:265) or 0 (:261)" - the one store is step 9's
    (`loader.inc:1453-1457`, `and ax, 1`); proposed "I_ICO_PKG or 0 (the
    loader's step 9, one `and ax, 1` off header flag bit 0)".
19. `kernel/dock.inc:521` and `kernel/dockmod.inc:539` - "the five pops at
    the bottom" - `db_paint` exits `pop ax` (the pen) then `kret_di` (six
    registers); `dkx_paint` pops the pen, `wm_clip_n`, `gfx_hole`, then six.
    Proposed: "the pen's `pop` and the ladder at the bottom".
20. `kernel/dockmod.inc:495-504` - the banner "dock_apply - the SETTING
    moved: geometry, drive column, windows, strip ... out: nothing" sits over
    `dkx_paint` (it is a stale copy; `DOCK_APPLY` lives in dock.inc and
    answers CF). Replace with a `dkx_paint` banner: "the strip up to date on
    any edge; CF = 1 something drew; an open strip damages nothing".
21. `kernel/extmod.inc:1085` - "which there IS ye." - truncated; the code
    returns DX = VY + CH (`extmod.inc:1098-1101`). Proposed: "which there is
    that display's own last row".
22. `kernel/extmod.inc:1106` - "wm_land_fit asks wm_disp_now ONCE" -
    `wm_land_fit` folded into `ui_drag_size` (`extmod.inc:611, 679-683`).
23. `kernel/extmod.inc:1345-1348` - "`mov al,1` then vid_ctx_ptr, which takes
    its index in AL and does `mul dl`" - the call is `ext_ctx_ptr`, a branch
    with no multiply (`extmod.inc:503-509`).
24. `SPEC.md:80168` - "**What an Extend user pays**: a 1KB heap claim" and
    `SPEC.md:80196` "1,566 -> 1,069 -> 1,023 bytes, a 1 KB claim" - the image
    is 1,955 bytes (`modx_end` = 0x7A3), a 2 KB claim, as §39.19.6's own
    seam-cut paragraph already says ("inside the 2 KB claim").
25. `SPEC.md:80013` - "the landing and the straddle fit (`wm_land_fit` and
    the eleven routines under it" - folded into `ui_drag_size` (the same
    section's 39.19.6.2 says so).
26. `SPEC.md:135291` (and the sentence after the table, "goes home to
    `kret_ret`") - "`wn_title`, home by `jmp KERNEL_SEG:kret_ret`" - it is
    `jmp KERNEL_SEG:kret_es` (`winmod.inc:348`).
27. `kernel/menu.inc:2576` - "item 1, swapped by ui_loc_gate" - item 1 is
    written by `desk_select`, `desk_unsel_x` and `sc_m_del`
    (`desk.inc:1406, 1452`; `desksc.inc:1492`); `ui_loc_gate` writes item 0
    (`ui.inc:2472-2476`).
28. `apps/os88api.inc:1446-1447` - "Both strings are terminated by the kernel
    whatever you put in them." - true of a PACKAGE's record (`desksc.inc:871-
    872`); for a DRIVER's only the caption is (`desk.inc:2247`). Proposed:
    "A package's two strings are terminated by the kernel whatever you put in
    them; a driver's caption is, and its file and name must be NUL-ended."
29. `SPEC.md:59386` - "Delete, right-click, File > Remove Shortcut | removes a
    SHORTCUT | anything else is a no-op (§26.8.5)" - a right-click on a
    mounted IMAGE's drive pops Unmount Image (`desksc.inc:516-555`,
    §18.101.5). Proposed: "... anything else is a no-op, but a right-click on
    a mounted image's drive offers Unmount Image (§18.101.5)".

## Open questions

1. shell-1: is there any MAKE refusal the owner considers reachable in
   practice (a widget relaunched by `stik_boot` while another load is in
   flight, kern_mega selector mismatch on `W_SEG` vs `[ld_base]`)? If yes,
   shell-1 is a bug rather than a hazard.
2. shell-2: is discard-on-pressure acceptable for `MOD_SETS` given that
   `cp_flush_cfg` writes SYSTEM.CFG from inside it (ONDEMAND-PLAN 7.1's
   open-to-close span)? The desktop gesture path runs the whole operation
   under the gfx lock with no re-entry, which suggests yes for the gesture
   path specifically.
3. shell-8: does the blob have the ~10 bytes for the post-load regrow on
   kern_big and kern_emu, or should it ride `.cfx` like the grid copy?
4. shell-14: does the owner want a `flicker` measurement of kern_small's cell
   repaint before deciding between +2 calls and the flash?

## Verification

Adversarial pass, 2026-11-08, against the same tree and `build/kernel.map`;
every bug/hazard and every medium finding re-read with its callers and
build arms. Data: `docs/reports/big-review-2026-11-08/data/shell.json`.

Findings:

- shell-1 - CONFIRMED. `desk_drop_x` keys on `IF_DESK` alone; step 9 drops for
  any non-`WF_DESK` window; `stik_load` has already bound `stik_le`; MAKE's CF
  is ignored under `STIK_DESKONLY`. Not in either plan queue.
- shell-2 - CORRECTED. Real; the settings core is 3,180 bytes (`mods_end` =
  0xC6C), not 2,206, and §110.12's "about a second" prices the menu's load.
- shell-3 - CONFIRMED. `sc_m_dfind` (AH = 0FFh) matches only `SC_STIK` rows,
  so WIN16's group link must probe, one module load per zone.
- shell-4 - CORRECTED. Real duplication, but the copies have diverged beyond
  geometry (the module's hole/region bank, the open-strip CF clear, prologue
  encoding), so one macro needs `%if` arms; severity medium -> low.
- shell-5 - CONFIRMED (17 source lines, not 19 instructions).
- shell-6 - CONFIRMED (five sites: `desk_select`, `desk_unsel_x`,
  `sc_rclick_x` x2, `sc_m_del`).
- shell-7 - CONFIRMED.
- shell-8 - CORRECTED. Behaviour as described; kind bug -> weakness (heap
  held in a movable claim, nothing malfunctions).
- shell-9 - CONFIRMED (cycle figures predicted, not measured).
- shell-10 - CONFIRMED (predicted).
- shell-11 - CONFIRMED.
- shell-12 - CORRECTED. Only the caption is forced (confirmed), but the SDK's
  "Both strings are terminated" closes the PACKAGE record's paragraph and the
  driver's three strings are each documented NUL: no SDK contradiction.
- shell-13 - CONFIRMED (spot-checked).
- shell-14 - CONFIRMED (spot-checked).
- shell-15 - unchecked (low, recorded decision).

Doc mismatches (numbering as in the section above):

- 1, 2, 4-11, 13-23, 24 (its SPEC.md:80168 half), 26, 27, 29 - CONFIRMED,
  replacement text refined in the JSON (`doc_fixes`). Item 9's edit is inside
  `OSAPI_WM_DESK`'s contract, which `tools/abilock.py` fingerprints: apply it
  with `python3 tools/abilock.py --update` (no `PKG_FMT` bump).
- 3 (`desk.inc:2387`, "osapi_desk_svc copies") - REFUTED as a doc fix: the
  stale name is real but sits in an `%error` string, which is code.
- 12 (SPEC.md:221361) - REFUTED: a row of the dated 2026-10-15 measured
  table, accurate for the build it measured.
- 24, its SPEC.md:80196 half (the §39.19.6.2 heading) - REFUTED as proposed;
  replaced by a fix to the summary sentence at SPEC.md:80007-80009, which
  still says the image is 1,023 bytes (it is 1,955, a 2 KB claim).
- 25 (SPEC.md:80013, `wm_land_fit` in "What moved") - REFUTED: the list of
  what went into EXTD.DRV when it landed, which is history and accurate.
- 28 (`os88api.inc:1446`, "Both strings") - REFUTED, as shell-12.
- Added by the verifier: `apps/stiklib.asm:8` ("held from the first QUERY to
  the restart") and SPEC.md:80007-80009 (above).
