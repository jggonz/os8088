# Handoff: the shared wins the apps size pass could not take

**PLAN, nothing here is built by this document.** The first apps size pass
(2026-10-10) ran one agent per package with a brief that kept every agent
inside its own `apps/<pkg>/` directory: shared includes, the kernel, drivers,
tools and the Makefile were off-limits because three sessions worked the
alphabet in parallel. Each agent wrote down what it could NOT take. This is
those lists compiled, de-duplicated and ordered, for picking and implementing
separately.

Sources, and they are the authority for the detail behind each row:

- **Set 1** (`1942` … `calc`): docs/reports/APPS-SIZE-P1-2026-10-10.md,
  branch `apps-size-1`, merged to `elendilon`. Its "missing component
  collection" is rows A-E there; every row is folded in below (2-6).
- **Set 2** (`cc` … `excitebike`): docs/reports/APPS-SIZE-P1-APPS9-16-2026-10-10.md,
  branch `apps-size-2`, merged to `elendilon`. Its "missing kernel or library
  components" and "shared-include wins" lists are folded in below. It **built**
  three of the C-toolchain rows (A.5, A.6, A.10 in 4), so those are on
  `elendilon` now.
- **Set 3** (`fontview` … `imgtest`): docs/reports/APPS-SIZE-P1-SET3-2026-10-10.md,
  branch `apps-size-3`, lists (a)-(d).
- **Set 4** (`loom`, `midirack`, `mines`, `missile`, `notepad`, `paccman` -
  `modplug` and `pacman` are retired and were skipped):
  docs/reports/APPS-SIZE-P1-SET4-2026-10-11.md, branch `apps-size-4`. Its rows
  are in 0.2 below.

Every figure is the reporting agent's ESTIMATE unless it says measured, and it
is per includer unless it says otherwise. Re-measure before quoting one —
`tools/incsize.py` for an include's cost in a package (and see 7.3 about its
`/tmp` paths before running two at once).

## 0. What the pass found, in one paragraph

Packages stop shrinking at the same wall the system-side pass hit one level
in: **what is left is shared.** `os88type.inc` is 65% of Font Viewer's image,
`os88img.inc` 63% of IMGTEST's, `os88fp.inc` ~70% of FPTEST's, `os88ui.inc`
1-2.3 KB of every UI package; a generated table whose generator is a gate is
42% of Gorillas (`grart.inc`); and the C toolchain's code shape is the single
largest term in every C package. The rows below are where the next 10-20%
lives, and almost none of them costs a resident kernel byte. The exception
that proves it is the games: DrMarco (-32.9%) and Excitebike (-23.5%) had
their largest wins in their OWN asset encodings, which no brief kept them
from - so a package whose image is mostly content is still worth a pass of
its own, and a package whose image is mostly code is not, until the rows
below land.

## 0.1 Shared round 1 (`apps-shared-1`, 2026-10-11) — what is BUILT

The A and B lists of the round were taken by seven agents on branch
`apps-shared-1`; every figure below is the agent's MEASUREMENT on that branch.
The rows that follow are left as they were written, so read this first: a row
named here is DONE and the rest of the document is what is still open.

| row | built | measured |
|---|---|---|
| pick 1, 5.1, A.10, A.11 | PKGZ in every shipped `.o88` rule (FROTZ, FPTEST, IMGTEST, LOOM, CHELLO, COVL, and two the gate found: SCRIBE, `kdos/DOS.O88`); every C `.OVL` ships `'CZ'` through `CC_PACKAGE`'s `CC_OVLZ_RULE`; fast row `pkgzrules` (`tests/unit/t_pkgzrules.py`) fails any rule that does not pack (SPEC.md 20.13.5, 73.14.1) | LOOM.O88 52,760 → 28,534, LOOM.OVL 43,702 → 17,929, WEAVE.OVL 21,198 → 9,480, FROTZ 22,578 → 19,362, SCRIBE 43,938 → 34,350 on disk. CHELLO and DOS.O88 stay plain on `--compress-if`'s own refusals |
| pick 6, A.1-A.3 | `tools/ccpeep.py` (was `apps/cword/cwpeep.py`) runs as `cc8086.py`'s last step for EVERY C package, ON by default, `make CCPEEP=0` the knob; an exact 8086 size model for jump retargeting; the constant-shift rewrite for runs of 4+ (runs of 3 are slower on an 8088 - `SHIFT_CL_MIN`); `tools/ccverify.py` is the differential verifier (on demand, 1-5 min a package, `--self-test` plants five wrong rewrites) (SPEC.md 73.15) | -38,158 B image and -19,982 B overlay across the C packages, EQUIVALENT on every trial in all ten; CWORD 59,350 → 54,924, WEAVE 59,106 → 52,878, LOOM 58,958 → 51,804. c64 took only 19 setcc rewrites - its compare shapes do not match the pattern, NOT investigated |
| pick 3, 5, 12, 3.1 | `OS88_POPLADDER`/`OS88_PUSHER`, the named-`ret` `OS88_REGION_MOVABLE` as the documented default (Gorillas adopted it), `OS88_BSS_ALIGNED` (Cyclone byte-identical on it), `OS88_ADJACENT` (SPEC.md 20.5). NO package moved onto the ladder: the next size pass adopts it | 0 resident |
| 3.6 `os88ui.inc` | `os88ui_about_d`'s exit is short (-3 B in every About carrier), `OS88UI_ABDONLY` (MIDIRack -16, Arkanoid -8), `os88ui_abdismiss` (opt-in; MEASURED not a byte saving - Hello only); `OS88UI_NOGRADIO` verified to remove 126 B; the NOGLYPH audit found NO includer carrying it for nothing. The `abx/aby/abw/abh` words had already gone | |
| 2 (document) | register contracts of `MEM_CLAIM` (all but DX), `MEM_FREE` (all), `WM_GROW`, `WM_WAKE` (all), the zero-capacity `FILE_READ` probe and `MEM_REGROW` shrink-to-fit, in the SDK and SPEC.md 50.3, 50.3.1, 18.4.1 | |
| 7.3 | `tools/incsize.py` uses a private temp dir, and no longer drops an include's last routine | |
| pick 4, 10, 3.3 | `ty_open` claims the face's own size by the zero-capacity probe; the pre-shift cache is sized from the face height and re-claimed for a taller face; `TY_STRIDE`; `OS88TY_NO*` opt-outs in all five includers; exact `TY_BSS_SIZE` (SPEC.md 6.4, 6.5) | every shipped face 8 → 2 KB; Font Viewer heap with Charter **27 → 17 KB** (MartyPC); Word/Scribe/CWORD predicted, not measured |
| pick 11, 3.6 `os88con.inc` | the CP437 table is a 256-glyph image table the ROM fills 32..127 of at launch; the 2 KB bss copy is gone; `con_band` deliberately NOT shared (SPEC.md 70.8.6.1) | DOS 49,596 → 48,286, Telnet 31,872 → 30,562; cell draw identical instruction for instruction |
| pick 9, 3.6 `os88spkfx.inc` | the level family lives in the speaker ring claim, read `es xlatb` (SPEC.md 34.11.9) | Audio, Tracker, Video -2.8 KB each on a card machine; the speaker ring +3 KB (net +256 B on a speaker machine); emit cycles unchanged, `spkfx` EXACT |
| 6.2, 6.3 | `tools/os88drvsym.py` resolves driver symbols from make's own recipe and refuses unless byte-identical; seven tests use it | `1942sound` card legs and `drmarioaudio`'s AdLib arm green. `trackmove`'s pool arm still SKIPS - no registered machine loads SOUND.DRV for it |
| 6.1 | `ui.inc` saves SI around `ui_bill` (SPEC.md 12.2) | +2 `.text` on all three kernels, no rung; `mouseup` red with `--break-si` |
| 6.4 | soak rows for the browser's `br*` scripts, `brrace`, FPTEST (+18 cases: `fp_i2a`, `fp_a2i`, `fp_cmpab`), IMGTEST | each broken on purpose once |
| 6.5 | Calc's sqrt (a wrapped seed, a wrong `0`, not an INT 0 - its divide is shift-and-subtract); the browser's navigation-during-parse (`brrace`: 250 bytes of the IVT written on the base); `brlink` (it was the TEST); Arkanoid's rails and About clip; the stale DOS/DrMarco/Cyclone/proxy text and the dotdel harness trap | |

B7 and 6.5's Cyclone row landed last: Artful's VGA code-span line measures
161,914 cycles against the old strip's 706,403 (4.4x, SPEC.md 46.4.12) and the
new `atblitvga` row is the one that can see the run split (CGA never takes
it); `cychs` saves, relaunches and reads Cyclone's table back (SPEC.md 77.31.1).

Coverage on the merged tree (`apps-shared-1`): `make` fast tier 62/62, and the union of
every agent's emulator rows - 74 of them - through `tools/os88soak.py`: 69 ok in one
run, and the five that failed passed on a re-run alone (`arkpuwipe` and `dispapps`
read the live source, which a commit had moved under the frozen tree; `brscroll`
was a settle under load; `fptest` and `imgtest` were missing a `wants=`, since fixed).
The kernel change's `make test-full` (67/67) was run by its agent.

Still open after the round: 2 (kernel
features), 3.1's callbacks-from-one-table, 3.2's new includes, 3.4 `os88fp.inc`,
3.5 `os88img.inc`, A.4 and A.7-A.9, 5.2 Gorillas' generators, 5.4. And three
found on the way: Word's region is fixed at `WD_P1ORG`, so it gains only from
the face and table claims; a window paint can lay out a browser page the worker
is mid-parse on (stays inside its buffers - not a memory-safety defect);
Arkanoid's bricks still draw over the ground fill (the mortar is that ground).

## 0.2 Set 4's rows (added after shared round 1)

| what | where | estimate | raised by |
|---|---|---|---|
| **DEFECT in round 1's macro**: `tools/stkbalance.py` (fast-tier `stkapps`) cannot see the labels `OS88_POPLADDER` builds with `%+`, so every adopter fails `stkapps`; and an unreferenced rung label reads as an entry point. Teach the gate the macro, or have the macro emit visible labels | `tools/stkbalance.py` / `apps/os88api.inc` | blocks every future adopter | notepad, missile (both hand-wrote the ladder) |
| `OS88_POPLADDER` costs disk under LZ4: Note Pad image -50 / file +20..30, Missile -42 / +34 (with Cyclone's -22 / +23, three of three). Say so at the macro: it is a RAM-for-disk trade | `apps/os88api.inc` doc | - | notepad, missile |
| Literal pooling in `cc8086.py` (A.4): `apps/loom/lmpool.py` is a working prototype | `tools/cc8086.py` | LOOM -2,795 (taken), CWORD 444, WEAVE 313, APPLE2 198, PACCMAN 121, RUNCPM 103, C64 15 | loom |
| `ccpeep`: fold `mov ax,sym / add ax,imm` into `mov ax,sym+imm` (needs flags liveness) - makes C union/struct members cost nothing | `tools/ccpeep.py` | a union cost LOOM +521..+1,202 | loom |
| `ccpeep`: a direct store to a constant address (`mov ax,_sym / push / ... / pop bx / mov [bx],ax` -> `mov [sym+k],ax`) | `tools/ccpeep.py` | uncounted, every C package | paccman |
| cc8086's indexed table access is dearer than a compare chain (a table-driven rewrite saved 242 B and cost +21% cycles) | `tools/cc8086.py` | - | paccman |
| A form for trailing data in a package's FILE that is never loaded (licences) | SDK / `os88pkg.py` | 1,072 B resident in PACCMAN | paccman |
| `OSAPI_FERR_TEXT`: the kernel already holds the `FERR_*` strings (`fm_s_e*`, `kernel/files.inc` ~2144) | kernel slot | ~125 B in Note Pad | notepad, word, scribe |
| Document `os88ui_btninit` as keeping every register; W_PAINT's clobber contract | `apps/os88ui.inc`, SPEC.md | ~10 B a caller | midirack, mines |
| A `READ_SEQ` door accepting a sector-multiple LAST chunk (an exact-sized `os88_rseq` claim on a large-cluster FAT16 disk takes a refused call and a re-walk) | kernel | a far call | midirack |
| `WEAVE.WSM` ships unpacked; `CC_OVLZ_RULE` likely applies | Makefile | disk | loom |
| 3.6's grid hit-test row: **Mines has no rect table** (its hit test is ~30 B of arithmetic) - struck as a carrier | - | 0 | mines |

## 0.3 Set 5's rows (paint, piano, pixel, pixelstein, recorder, redline, runcpm, scribe)

| what | where | estimate | raised by |
|---|---|---|---|
| `OSAPI_FERR_TEXT` (0.2's row): **four carriers now** | kernel slot | ~125-190 B each (paint ~190) | notepad, word, scribe, paint |
| `OS88_POPLADDER`'s disk cost is now a RULE, measured in Paint: a ladder of **6+ pops pays** (image -125 / file +54), **4-5 pops loses on disk** (-34 / +43). Scribe is the fourth data point (-98 / +74). Put the rule at the macro | `apps/os88api.inc` doc | - | paint, scribe |
| `os88pkg.py --compress-if` bounds a packed file against image+bss rounded to **512**, but the loader claims **whole KB** (`mem_bytes_kb_x`); bounding against the KB lets packages near the line compress for free | `tools/os88pkg.py` | disk; Recorder forced `RC_CHUNK` 250 to fit | recorder |
| `os88pcm.inc`'s 256-byte rescale table lives in the includer's image and is rebuilt each play; take a caller-supplied buffer | `apps/os88pcm.inc` | -256 B per includer (recorder, tests/sndplay) | recorder |
| `op_fetch` (`os88partsbody.inc`): an UNCOMPRESSED lazy part keeps up to a cluster of head slack in its whole-cluster claim for the session; move down and shrink as the compressed path does. C `os88_part_fetch` inherits it | `apps/os88partsbody.inc` | ~20 B/includer; up to ~31 KB heap per part on a 32 KB-cluster disk | pixelstein |
| `benchlib.inc`: `BL_MAXROWS` (380) not overridable (1,520 B/includer); `BL_BSS` should take the save buffer's base separately; `bl_save` composes the whole file - 512-byte chunked WRITE/WRITE_AT would make `bl_out` 512 B in every bench (sysbench ~17 KB); `BL_BSS_SIZE`'s +96 scalar slack against 70 used | `tests/benchlib.inc` | ~900 B redline; ~17 KB sysbench; 26 B/includer | redline |
| `ccpeep` A: fold `mov ax,_sym / mov bx,ax / [bx+k]` -> `[_sym+k]` | `tools/ccpeep.py` | RUNCPM 117, WEAVE 231, LOOM 92, CWORD 84, C64 59, PACCMAN 56, APPLE2 44, LMPVMOD 40 sites, ~4 B each, **~3 KB total** | runcpm |
| `ccpeep` B: member store through a pointer local | `tools/ccpeep.py` | LOOM 109, WEAVE 74, CWORD 40, LMPVMOD 17, C64 9, RUNCPM 6 sites | runcpm |
| C booleans still materialised then branched (`setcc;cbw;test;jcc`), and returned/assigned `&&`/`||` | `tools/ccpeep.py` / `cc8086.py` | LOOM 57, CWORD 39, WEAVE 33, APPLE2 23, PACCMAN 19, RUNCPM 18 sites | runcpm |
| `and ax,-1` from `& 0xFFFF`: drop in cc8086 when flags are dead, or an SDK `W16()` | `tools/cc8086.py` | RUNCPM 32 (fixed locally), C64 7, LOOM 5, APPLE2 1 | runcpm |
| Document `os88_memcpy` as safe for an overlapping move to a LOWER address | `apps/cc/os88.h` doc | - | runcpm |
| Content-relative drawing (2's row) measured: ~180 B in Piano; Paint carries `pt_cfill/pt_cframe/pt_ctext/pt_cwell`. And `os88ui_btn` taking content-relative rects (a record flag) | kernel / `apps/os88ui.inc` | ~180 B piano, ~45 B btn rects (piano, calc, ...) | piano, paint |
| The ROM-font RAM copy (C.3): Paint (`pt_font_init`) and PIXELSTEIN carry one | kernel | small | paint, pixelstein |
| `OSAPI_ICON_DRAW` with an implied solid mask: every `OS88UI_IMG` record carries 32 B of `0xFFFF` | kernel / `apps/os88ui.inc` | PiXEL 512 B + 256 B of half-height copies; every OS88UI_IMG carrier | pixel |
| **Kernel claim policy**: `mem_claim`'s refusal path COMPACTS BEFORE IT SHEDS - on PiXEL's BIG.BMP layout a 32 KB ring moved 332 KB (1.2 s on an 8088) where shedding a 30 KB WSAVE cache would have made room; and there is no query for "is there a run of N WITHOUT compacting?", so a package cannot prefer its smaller fallback over a costly move. Any package-size change can flip a benchmark this way - layout, not code | kernel (SPEC.md 66.4) | design | pixel |
| A shared PiXEL mixing-plans part (~3.2 KB unpacked in each of five decoder parts) needs a worker allowed to fetch | SPEC.md 20.12 | ~13 KB disk | pixel |
| A module stamped to its package's build: a stale `SCRIBE.OVL` reaches the package's bss by offset and reads an empty document; `make scribe` does not refresh `build/ovlz/` (only `scribedisk` does) | SDK / Makefile | a handful of bytes in `sc_ovload` | scribe |
| `os88type`'s `ty_bandbuf` (1,472 B of bss) is Scribe's largest item; `TY_STRIDE` exists | `apps/os88type.inc` | up to 1,472 B | scribe |
| Under `OS88UI_NOGEST` the package owns `OS88UI_BT_DOWN` (Word, Piano rely on it) - say so in the SDK; and the W_PAINT clobber contract (0.2's row, raised again) | SDK doc | - | piano, recorder |
| SPEC.md 86's "verb-6 stage buffer must be in the package segment" is stricter than the mechanism (it is DS at the call) | SPEC.md | - | recorder |
| Grid hit-test helper (3.6): **Piano struck as a carrier**, like Mines | - | 0 | piano |
| **Harness**: `pxpartemu`-based rows (`pxanimemu`, `pxextraemu`, `pxsave`'s host leg) report `ok` in 0.3 s when Unicorn is not installed - a FALSE GREEN, on the base too; `tests/pxfsx.py` ~484 has an `until()` race (breakpoint hit between the condition and the stopped-machine check); `os88marty.type_text` delivers keys out of order (`tye pinfo.txt`) | `tests/`, `tools/os88marty.py` | - | pixel, runcpm |
| **`pxdrawvga` is intermittent on the base** (`b13cfe60` 3/4 failed): pressing R for the Rotate tool while BOUNCE.GIF plays, `ui_done` never sees `ui_idle` hold in 180 guest seconds. Either the wait wants the animation stopped (or a state read, not `ui_done`), or the frame job starves the UI task on VGA - classify before blaming either | `tests/pxdraw.py` ~476 / `apps/pixel/` | - | set 5 coverage run |
| `tools/pxsart.py`'s host models differ from the machine builds for the C160 and CGA4 sprites, and no row compares them | `tools/pxsart.py` | - | pixelstein |

## 0.4 Set 6's rows (skies, solitaire, tamegram, tank, telnet, texpad, tracker, video)

| what | where | estimate | raised by |
|---|---|---|---|
| **Implied-solid-mask picture form**: `OS88UI_IMG`/BIMG records carry a mask of all `0xFFFF` (0.3's PiXEL row, now three carriers) | `apps/os88ui.inc` / `OSAPI_ICON_DRAW` | PiXEL 512 B, Tracker ~160 B, Video ~160 B | tracker, video |
| The 8.3 filename mangle (SPEC.md 77.20) exists twice; `telzm` polices drift by test | new `apps/os88m83.inc` | ~300 B, and no drift | telnet |
| `OSAPI_FERR_TEXT` (0.2/0.3): two more carriers | kernel slot | Telnet ~90 B, FTPD 63 B + strings | telnet |
| Decimal with thousands separators (`tz_num` + `tz_div10`) | shared include | 85 B a carrier | telnet |
| Content-relative drawing (2's row): TameGram carries 154 B of clamp-and-offset wrappers | kernel / `apps/os88ui.inc` | ~120 B here | tamegram |
| One slot answering content origin AND size - packages call `WM_CONTENT` and `WM_GEOM` as a pair | kernel slot | a far call a frame, ~6 B a package | tamegram |
| No-preserve forms of `OS88_REGION_MOVABLE` / `OS88_WORKER_RESTARTABLE` (TameGram wrote raw calls) | `apps/os88api.inc` | 6 + 4 B a package | tamegram |
| `os88ui_about`: a line-pitch ladder, left-aligned lines and a card-only include; `os88ui_about_d` is 175 B in every carrier | `apps/os88ui.inc` | TameGram's private ~220 B panel; every carrier | tamegram, tank |
| A draw/erase polarity for `os88gfx.inc` walk blocks - Tank's `tk_eflip` patches 27-28 bytes to turn a draw walk into its erase | `apps/os88gfx.inc` | Tank's SMC; Wire and Skies could share | tank |
| A shared "update the scroll bar only if total/fit changed" beside `os88ui_sbar` | `apps/os88ui.inc` | ~60 B per two-bar package | texpad |
| `os88partsbody.inc` opt-out for a lazy part the package reads itself (the fetch code is ~650 B) | `apps/os88partsbody.inc` | Skies' loader -512 B, one cluster | skies |
| ~~The kernel re-home (`mem_reown_x`, loader step 8a) trims the carve's HEAD but not its TAIL; Skies regrows itself~~ **DONE** (kernel round D3, SPEC.md 20.12.10.5): `OSAPI_PKG_REHOME`'s KEEP form (`AX = 0`, `CX` = paragraphs kept), computed by `OS88_REHOME` in `os88parts.inc`; DOS.O88 -1,360 at 360KB, Skies -783 (its regrow removed), Word -1,392; kern_big +41, kern_small -131 | kernel | every re-homed program (DOS.O88, tests/rehome) | skies |
| A purgeable claim for packages (Skies could keep its title art across a flight, -485 ms of exit) | kernel (SPEC.md 50.6) | design | skies |
| `os88con.inc` is over a third of what is left of Telnet (~4.8 KB bss + 2 KB face + ~2 KB code); `os88ui` 510 B for one button, `os88line` 986 B for one field | shared includes | - | telnet |
| Tracker: `tsp_mend` draws twice; the text screen's message check compares the `[tui_msgp]` POINTER, so a new message composed into the same buffer is missed; `tools/trkface.py`'s format blocks a 4-byte `tpl_tiles` encoding (~40 B) | `apps/tracker/`, `tools/trkface.py` | ~40 B | tracker |
| **Tooling: a per-routine build diff** (relocations masked) **and an address-parity report** - NASM relaxes branches differently after a cold edit and silently moves hot code by an odd amount (Video wrote five branches back by hand) | `tools/` | makes a hot-path-safe pass repeatable | video |
| `tools/stkbalance.py` reported ONE of two seeded broken jumps into a shared epilogue (with 0.2's POPLADDER blindness) | `tools/stkbalance.py` | - | video |
| `tools/os88rate.py` `scan()` returns the first TRACKER header, which during a load is the compressed FILE above the region - fixed in `trkclick`, LATENT in `trkrate`, `trklcd`, `trktxsurf`, `trkscrl` | `tools/os88rate.py` | - | tracker |
| Harness writes to `/tmp` (`dispapps._map`, `csworlds.world_map`) - breaks the worktree-only rule for parallel agents; honour `TMPDIR` or `build/` | `tests/`, `tools/` | - | tank, skies |
| MartyPC's debug server cannot set IP (a restart test has to patch code); no general fixture forces a named package's region to move | `tools/martypc/`, `tests/` | - | tamegram |
| `tpdraw` compares incremental against full repaints and is blind to a consistent mis-draw; a golden-frame row would have caught TeXpad's bar buttons | `tests/` | - | texpad |
| `os88ui.menu_pick` cannot reach a menu's items 11+ on the VGA XT | `tools/os88ui.py` | - | texpad |
| **Flaky on the base**: `vidsndmute` (`tests/vidsound.py --button` reads `vp_done` before the press edge, ~1 in 7); `pxdrawvga` (0.3) | `tests/` | - | video |
| `vidbench` reports `ok` in 0.1 s without the owner's XDC samples | `tests/vidbench.py` | - | coverage run |

## 0.5 Shared round 2, list A (`apps-shared-2`, 2026-10-12) — what is BUILT

The tooling and test rows of 0.2-0.4, taken by five agents. Nothing here
moved a shipped byte except SCRIBE (A7).

| row | built |
|---|---|
| A1 | `tools/stkbalance.py` expands `OS88_POPLADDER`/`OS88_PUSHER` (one-line invocation, registers spelled out) and reports EVERY broken jump into a shared epilogue - findings were de-duplicated by line text, which folded two broken jumps into one. The measured LZ4 rule is at the macro (SPEC.md 20.5). The hand-written ladders' comments now say the gate COULD not see the macro; a new ladder should be the macro |
| A2 | `tools/os88rate.py`'s `scan(m, apps)` accepts a TRACKER header only when its region holds the IMAGE, read off the floppy (`image_ref`); trkclick, trkrate, trklcd, trktxsurf and trkscrl all pass their disk |
| A3 | `unicorn` and `xdc` capabilities: `pxanimemu`, `pxextraemu`, new `pxsavehost`/`pxedithost` (the host legs, split out of `pxsave`/`pxedit`) and `vidbench` report SKIP instead of ok; `vidhdmake` FAILS on a missing boot file instead of skipping |
| A4 | `vidsndmute`: bound the restart by the frame the player acted on (`[vp_kat_t]`), 1/8 -> 0/24. `pxfsx`: `os88marty.until()` looks once more at a guest seen stopped. `pxdrawvga`: wait on `[px_tool]` and not on an idle UI, which a playing GIF only gives by luck. All three were the harness's |
| A5 | every hard-coded `/tmp` in `tests/` and `tools/` (39 files, ~90 sites) goes through `tempfile.gettempdir()` - set `TMPDIR` and nothing leaves the tree. The runner does not set it yet |
| A6 | `tools/os88rdiff.py`: per-routine diff of two builds with relocations resolved to labels, and `--hot`/`--hot-file` asserting identical code at an even shift; `apps/video/rdiffhot.txt` is Video's 336-routine play path (PERFORMANCE.md Part 7 item 7) |
| A7 | `SCRIBE.OVL` carries its package's build stamp and a mismatch is refused ("SCRIBE.OVL: wrong build"); `make scribe` brings the packed module (SPEC.md 95.8.7). +28 image |
| found on the way | `skiesticks` typed during Skies' longer exit (set 6) - `land()` waits for `[fsx_task]` |

Coverage on the merged tree: 157 rows through `tools/os88soak.py`, 149 ok and
5 SKIP by capability; `skiesticks` (fixed above) and the four Unicorn rows
then passed with Unicorn installed; `lzcomp` is intermittent on the base too
(1/4 at `2edb3b11`, 0/4 merged) - a CALC.O88 the Disk window's listing does
not yet show after the compress, not investigated; `pxdecode` timed out opening JEXBIG.JPG once in that run (1,800 guest seconds) and passed re-run alone (1,018 s); PIXEL.O88 is unchanged since set 5, where it also passed - intermittent, not rated on the base.

## 0.6 Shared round 2, lists B and C and A's leftovers (`apps-shared-2`) — what is BUILT

Seven agents; every figure is the agent's measurement on the branch, image
bytes unless it says otherwise.

| row | built | measured |
|---|---|---|
| B1 | **`OS88UI_CREL` was BUILT, MEASURED and NOT SHIPPED** (SPEC.md 20.5.1.5): an include can only share what a package's own wrappers DUPLICATE, and near-identical wrappers are what LZ4 compresses best | Piano −13 image / +37 file (and a cluster on the full 360 KB apps disk), TameGram +20, Arkanoid +10. **The win is a kernel one** - content origin as state the primitives read, or D5's origin-and-size slot |
| B4 | `os88ui_about_d` options `OS88UI_ABFIT` (pitch steps 12 -> 8 to fit), `OS88UI_ABLEFT`, `OS88UI_ABPACK`; TameGram's private panel gone | TameGram 3,943 -> 3,935, file +1; `os88ui_about_d`'s own 175 B found no saving; every other carrier byte-identical |
| B8a | `OS88UI_SBUPD`: `os88ui_sbupd` moves only the thumb when total/fit are what was drawn (SPEC.md 13.10.3.2) | TeXpad −53 image / −62 file; single-bar packages do not pay for its 49 B |
| B2 | `apps/os88m83.inc`, one 8.3 mangle (SPEC.md 77.20.1); row `m83` | the two copies had DRIFTED three ways (trailing dot, `\`, FTPD mangling a 12-byte-cut leaf); FTPD −110 image (one 512 bss step), Telnet −55 |
| B3 | `apps/os88dec.inc` (`DEC_U16/S16/U32/SEP/FLD`, SPEC.md 20.5.2); row `decfmt` | Sheet −184, Chart −125, Telnet −70, Task Manager −56 (its small arm **+15**), Dot Delirium −28, Scribe −27, Tank −7. Word, MIDIRack and others measured and refused (SPEC.md 20.5.2) |
| B5 | a plain lazy part moves onto its claim's base and the claim shrinks (SPEC.md 20.12.7.4.4, row `msegplslack`); `OP_SELF` and an arm per kind of lazy row (20.12.9.1) | 5 KB back on the 8 KB-cluster fixture (up to ~31 KB a part at 32 KB clusters, arithmetic); Skies' loader 2,102 -> 1,456, SKIES.O88 42,497 -> 41,473; pxstein, dosload, pixel −70 each |
| B6 | `os88fp.inc`'s four uncalled routines deleted, `FP_NO_FTOA/SQRT/FLOOR/I2X/SUB`, `CH_NO_BMPWRITE` (SPEC.md 84.8) | Chart image 11,547 -> 10,512 (with B3: image+bss 12,078 -> 10,866), Sheet −118, FPTEST −118 |
| B7 | `os88img.inc` trimmed, `img_pix`'s jump off the +127 edge, the caller's block 76 -> 64 (SPEC.md 94.4) | IMGTEST 2,851 -> 2,384, SCRIBE.OVL 10,123 -> 9,659; **decode 45% fewer cycles** (1,448 -> 798 a pixel over IMGTEST's 27 cases) |
| B8b/c | `OS88_REGION_MOVABLE_NOKEEP`/`OS88_WORKER_RESTARTABLE_NOKEEP` (5 packages, 4-10 B each); `os88pcm_play` takes the caller's 256-byte table; the W_PAINT, NOGEST and verb-6 documentation rows | Recorder 3,621 -> 3,373 image+bss |
| C | `tools/ccpeep.py`: row A (immcopy/direct), member `addimm`, `pushst` (row B and the constant-address store), four boolean rules, `andid`, and literal pooling in the toolchain (SPEC.md 73.15.2); RunCPM's `RC_W` reverted | **−6,588 image and −2,084 overlay** across the C packages, every rewrite ccverify-EQUIVALENT, `--self-test` 14/14 |
| 5.2 Gorillas | the generators emit SOURCES and the package derives its tables: the live surface's ink tables into one 1,536-byte area on a display change, the intro poses through them into the existing cache, the marquee drawn cross by cross, the sun two words a row, the right arm mirrored, BGM 3 bytes a row and reactions 1 byte a note (SPEC.md 99.5); new rows `gorillascity`, `gorillasmenu`, `gorillasmusic`, `gorillasreactions`; a latent CGA-capture seed flake fixed | **image+bss 56,327 -> 46,031 (-18.3%)**, file 15,205 -> 12,798; every scene and VRAM hash identical on 3 adapters x windowed/fullscreen, no frame slower (flight -0.01..-0.19%), 7/7 gorillas rows on the merged tree |
| A leftovers | the runner defaults `TMPDIR` to `build/tmp`; `scratch_drop` and a sweep for `.args`; `lzcomp` read the listing mid-re-list (reproduced on demand - the kernel paints after the sort and is right); `pxsdisk` through `os88build.at()`; `pxdecode`'s wait split so a stall names its half; `type_text` out of order NOT reproduced (FIFO at every stage); `t_docglyph` left a /tmp dir per `make` | - |

Coverage on the merged tree: 163 rows through `tools/os88soak.py`, **162 ok**;
`pxs256` failed once and is intermittent on the BASE (2/6 at `7e51ee9f`, 0/6
merged) - a dog-candidate assertion in PIXELSTEIN, not investigated. `make
rczex` 67/67 (its first run double-counted one OCR'd line - a harness flake,
not fixed).

Open from this round: `fp_i2a` folding into `dec_s16`; Task Manager's small
arm +15 from B3; a returned `&&`/`||` still builds a 0/1 (a code-generator
change); one pushed store in five refused by ccverify's limits; `OP_SPENT`
looks dead; `tests/ftpd.py` dies at "drive B: has no desktop zone" before
FTPD launches (not checked on the base), so FTPD's new mangle path is proven
only by `m83` and reading; `rczex`'s double-counted line.

## 1. The pick list — highest value per byte of risk first

| # | change | lands in | resident | buys | §  |
|---|---|---|---|---|---|
| 1 | Pass `$(PKGZARG)` in the FROTZ, FPTEST, IMGTEST, LOOM, CHELLO and COVL `.o88` rules, and ship every C `.OVL` as a `'CZ'` file | Makefile, `apps/cc/Makefile.inc` | 0 | disk: LOOM.O88 52,760 → 28,534 and LOOM.OVL 43.7 → ~22 KB, FROTZ 22,578 → ~18,700 (both ship), FPTEST -801, IMGTEST -538 | 5.1, 4 A.11 |
| 2 | Delete `os88fp.inc`'s four uncalled routines, then gate the rest (`FP_NO_FTOA`/`_SQRT`/`_FLOOR`/`_I2X`) | `apps/os88fp.inc` | 0 | 118 B × 3 includers, then ~900 B more in Chart | 3.4 |
| 3 | `OS88_REGION_MOVABLE` form that takes an existing `ret` | `apps/os88api.inc` | 0 | 3 B × ~60 packages; four agents did it by hand | 3.1 |
| 4 | Size `os88type.inc`'s face claim from the FILE | `apps/os88type.inc` | 0 | **~6 KB of heap per open face** (8 KB claimed, faces unpack to 1.1-1.7 KB); up to 3 faces in Word/CWORD | 3.3 |
| 5 | Shared save/restore ladder (`pop N; ret` tails) | `apps/os88api.inc` | 0 | ~4 B a site, ~11 a cold routine; raised by 8 agents across all three sets | 3.1 |
| 6 | C peephole: verify `apps/cword/cwpeep.py` (built, committed OFF) and move it into `tools/cc8086.py` | `tools/cc8086.py` | 0 | **-8,589 B measured in CWORD** (~14% of compiled code, never slower); the compare-as-branch case alone ~3.4 KB loom, ~2.8 KB weave, ~3 KB c64, ~1.7 KB apple2 | 4 A.1, A.3 |
| 7 | Gorillas' generators emit derivable / packed tables | `tools/gorillas_*.py` | 0 | **~9.5 KB of RAM (17% of the app)**, one package | 5.2 |
| 8 | FTPD's 32 KB STOR stage claimed per STOR | kernel (a worker-safe claim or a UI-task request) | small | 32 KB of heap whenever FTPD sits idle | 2 |
| 9 | `os88spkfx.inc`'s 2,816-byte level family out of the package segment | `apps/os88spkfx.inc` | 0 | ~2.8 KB bss per includer on card machines (Audio, Tracker, Video) | 3.6 |
| 10 | Size `os88type.inc`'s 12 KB pre-shift cache from the face height | `apps/os88type.inc` | 0 | 1-6 KB of heap per includer | 3.3 |
| 11 | `os88con.inc` renders from the packed CP437 face (or the ROM font where the glyphs agree) instead of a 2 KB bss copy | `apps/os88con.inc` | 0 | ~2 KB of RAM in DOS and Telnet | 3.6 |
| 12 | `OS88_BSS_ALIGNED`: Cyclone's gap-filling 512-aligned bss layout as a macro | `apps/os88api.inc` | 0 | up to 511 B in any package with an int-13h buffer in its own segment (FTPD's `fd_stage`) | 3.1 |

Rows 1-3 are mechanical and can land together. Rows 4, 8 and 10 change WHEN
or HOW MUCH heap is held, so they want a row that measures a claim
(`tools/heapmap.py`), not just a byte count.

## 2. Kernel

| what | resident | buys | raised by |
|---|---|---|---|
| A worker-safe `mem_claim`, or a UI-task request type, so a worker-fed transfer can claim per operation (SPEC.md 20.6 rule 7 forbids it today) | small | FTPD's 32 KB STOR stage held only during a STOR | ftpd |
| Window-content coordinates: an origin set once per callback, or content-relative entries for fill/frame/text | small, or 0 as an include | ~60-90 B a package (Font Viewer wrote `fv_abs`, Fractal `fr_textxy`) | arkanoid, calc, cyclone, fontview, fractal, gorillas, mines, missile, paint, piano, recorder, sheet, solitaire, tamegram, texpad, thewire |
| `OSAPI_FMT_DEC` decimal formatter (or 0-resident as an include, 3.2) | ~30 + 8-byte cell | ~25-60 B a package | arkanoid, calc, chart, fptest, fractal, notepad, scribe, sheet, word |
| kern_small `gfx_blit1` head mask for any x, as kern_big has (SPEC.md 5.4.2.8) | kern_small only | ~96 B in Artful | artful |
| `OSAPI_GFX_BLIT1` with a per-8px-column pen | small | ~60 B and N-1 call arrivals a line | artful, os88con, word, cword |
| A directly indexable RAM copy of the 8x8 glyph table through `OSAPI_FONT_GLYPHS` | ? | 760 B bss in Artful | paint, word, scribe, gorillas, missile, arkanoid, pixelstein, video, pxfull |
| Scaled 1bpp blit / text in a bitmap font | ? | ~156 B in 1942 | 1942 |
| Planar → packed conversion inside `OSAPI_GFX_BLITP` as its own fallback | ? | 3,010 B of 1942's packed image | 1942, paint, pixel |
| **Document** the register contracts of `OSAPI_MEM_CLAIM`, `MEM_FREE`, `WM_GROW`, `WM_WAKE` ("keeps all but the outputs") | 0 | a few B a call site, every package | set 1 |
| **Document** two patterns set 2 found already work: the zero-capacity `OSAPI_FILE_READ` probe (`FERR_BIG` with DX = the KB needed, from the directory entry, no I/O) as THE way to size a buffer for a file, and `OSAPI_MEM_REGROW` shrink-to-fit for surface-sized claims | 0 | Chart dropped a fixed 32 KB staging claim with the first; Dot Delirium (11 → 4 KB) and Excitebike (sprites 28 → 21 KB) the second - other apps hold fixed or grow-only claims | chart, dotdel, excitebike |
| 512-aligned scratch from the OS, or `OSAPI_FILE_READ`/`WRITE` bouncing a sub-sector buffer on DRIVER volumes too (only BIOS volumes bounce, SPEC.md 18.91.4) | ? | every small save file drops its alignment slack (row 12 is the 0-resident half) | cyclone |
| A transparent `OSAPI_FILE_READ` into a buffer that is not at offset 0 of a segment | small | Excitebike claims 1 KB only to read `EXBTRACK.DAT` and copy it into bss | excitebike |
| Save SI in the click dispatch, or tighten SPEC.md 12.2 — see 6.1 | ~2 | a latent defect | hello |

## 3. Shared assembly includes

### 3.1 `apps/os88api.inc`

- **Save/restore ladder** — a label pair or macro (`push`-all helper and
  `jmp pop_N` tails). Hand-written this pass as 1942's `n_pushall`, Audio's
  `ap_ret6`, DrMarco/Excitebike's `SAVE`/`RESTORE`, Frotz's `zf_pop3..7`,
  FTPD's ladder, Font Viewer's `fv_pop4/6`, Fractal's `fr_pop7`, Gorillas'
  shared epilogue, and in drivers as `wr_sv`/`hd_ent`/`tm_pop_*`/`ep_*`/
  `kentm_*`. A near `jmp` into the tail costs no stack (HANDOFF-KERNEL-SIZE
  passes); a `call`ed push-helper costs ~100 cycles, so cold paths only. Set
  2 adds the DOS box's host code (~35 routines end in one 6-register
  epilogue, ~250-400 B) and Dot Delirium's eight-register callback wrappers
  (~10 B a site) — and one MEASURED warning: Cyclone built a pop ladder and
  it was image -22 but **file +23**, because a repeated epilogue is exactly
  what LZ4 compresses. Price a ladder on both numbers.
- **`OS88_BSS_ALIGNED`** (pick list 12): Cyclone's high-score table needed a
  512-aligned buffer and reserved 511 bytes of slack for it; it now places the
  table at the first 512 boundary past the image and fills the gap in front
  with the rest of its bss, in list order, by assembly-time expressions
  (`CY_PAD`/`CY_FIT` in `apps/cyclone/cyclone.asm`) — at most ~20 bytes lost.
  As a macro, any package with an int-13h buffer in its own segment gets it.
- **`OS88_REGION_MOVABLE`** emits a private `ret` and a `jmp short` over it,
  19 bytes inline; a form naming an existing `ret` saves 3 in every package.
  FPTEST, IMGTEST, Fractal and Gorillas pointed it at their own by hand.
- **Register several window callbacks from one table** instead of four
  `mov ax,cb / call OSAPI_WM_ON*` pairs: ~32 B, most packages.
- **Adjacency assert macro** (two variables written as one word): 0 B, makes
  layout rules self-checking (arkanoid, calc, fptest, fontview all rely on
  one).

### 3.2 New includes (0 resident)

- **Number → decimal text, plus a space-padded fixed field** for one opaque
  `font_run`: ~40-60 B (the kernel slot in 2 is the other spelling).
- **Note-stream sequencer** over `OSAPI_SND_TONE`/`OSAPI_SND_FM` — 1942's
  one-byte-indexed format is reusable: ~150-250 B a game (1942, DrMarco,
  Excitebike; Gorillas' `grmusic` is the same shape, 5.2).
- **Regrowing arena** (`MEM_REGROW` in quanta, shrink when empty): ~100 B;
  any package with a big fixed undo/scratch claim (Artful built its own).
- **"Draw the moved rect, erase only the uncovered part of the old"**: ~170 B
  (arkanoid, cyclone).

### 3.3 `apps/os88type.inc` (Font Viewer, Word, Scribe, CWORD via `cwtype.inc`, `tests/facetest`)

- **Face claim from the file size**: `ty_open` claims 8 KB for every face;
  the shipped faces unpack to 1,118-1,688 bytes. One `FILE_STAT` or the CZ
  size hint, whole KB, keeping the 512-byte alignment assert: ~6 KB of heap
  per open face.
- **Pre-shift cache from the face height**: `ty_cache` is 12 KB for 16 rows;
  12-row faces need 9 KB, 8-row 6 KB. Needs a re-claim when a taller face is
  cached.
- **`%ifndef TY_STRIDE`**: the band is 92 × 16 = 1,472 bss for a 720 px line;
  Font Viewer composes 480 px (480 B back), and each includer can set its own.
- **Opt-out for unreached routines** (`ty_hit`, `ty_width`, `ty_widthn`,
  `ty_put`, `ty_advof`, `ty_famn`, `ty_getasc`, `ty_getlead`): 183 B in Font
  Viewer, which with `TY_STRIDE` takes it to a 6 KB claim.
- **Exact `TY_BSS_SIZE`** with an assertion, instead of `+48` slack now partly
  spent by `ty_fresh`/`ty_fclus`/`ty_fdrv`.

### 3.4 `apps/os88fp.inc` (Sheet, Chart, FPTEST)

- `fp_div32_10e5`, `fp_udiv32_10e5`, `fp_umod32_10e5` (109 B) and
  `fp_tv_to_b` (9 B) have **no caller in any includer** — apparently what
  SPEC.md 84.5 says was deleted. 118 B a copy, ~354 across three.
- `fp_i2a` (59 B) is Sheet's alone — but the better fix is a gate case, below.
- **No opt-out gates at all**: ~960 B of it is unreachable from Chart and
  assembled anyway — `fp_ftoa` 454, the `10e5` divides 109, `fp_sqrt` 140,
  `fp_i2a`/`fp_i2b` 118, `fp_floor` 39, the `fp_sub`/`fps_sub`/`fpx_sub`/tv
  helpers ~80. `FP_NO_*` gates are ~900 B of Chart (set 2), and the deletions
  above are their first step.
- **`apps/os88chart.inc`** (Sheet, Chart): `ch_bmp_write` (92 B) is unused by
  Chart since set 2's export writes the canvas in place (`ct_flip`) — a
  `CH_NO_BMPWRITE` gate, or make it flip in place too so no caller needs a
  19 KB scratch segment.
- **Coverage gap, not a size item**: FPTEST has no case for `fp_i2a`,
  `fp_a2i` or `fp_cmpab`/`fpx_cmpab`, and Sheet and Chart call all three.

### 3.5 `apps/os88img.inc` (IMGTEST; Scribe through `SCRIBE.OVL`)

1,800 of IMGTEST's 2,851 bytes, ~150-250 a copy, 0 resident:
caller-saves for the internal helpers (126 single-byte push/pop; `img_load`
already saves AX-ES) ~70-90; `img_palmap` stepping DI by 3 and looping the
three channels, RGB/BGR copy arms merged, ~70-80 (`img_palident` ~10 more);
`img_load`'s redundant W/H/STRIDE zeroing -15; byte stores for `IMG_ERR`
-14 and one block-zero for the other constants ~-10; one byte-copy helper for
`img_pix`/`img_bmp` ~-10. **`img_pix`'s `jc .out` sits at exactly +127** and
NASM sizes it short or near by the includer's layout — move the target and it
is deterministic. Do not use `aam 16`/`aad 16`: a V20 ignores the immediate.

### 3.6 `apps/os88ui.inc`, `apps/os88spkfx.inc`, `apps/os88data.inc`, `apps/os88sock.inc`, `apps/os88line.inc`

- **`os88ui.inc`**:
  - About card: `os88ui_abx/aby/abw/abh` are `dw 0` in the image (8 file
    bytes in ~25 packages; bss would do); an `os88ui_abdismiss` helper for the
    set/dismiss/repaint every user writes (~35 B in ~28); `os88ui_about`'s
    handler entry optional for a painter-only user (8 B); `os88ui_about_d`'s
    `jc .out` is 147 bytes away and assembles near (~3 B in 29 packages).
  - `OS88UI_NOGLYPH` is opt-in: audit every includer that never calls
    `os88ui_glyph`/`gring`/`gdot` (~321 B each found). Verify
    `OS88UI_NOGRADIO` really removes all ~145 B of ring/dot — FTPD sets it
    and was reported still carrying it (unverified).
  - Single-button entry `os88ui_btn1(AL=flags, SI=label, BX=rect)` ~20 B;
    a grid hit-test helper (rows × cols, pitch, gap → index) or `os88ui_bfind`
    over a rect generator (Calc's table was 232 B of bss; mines, solitaire,
    piano likely).
  - The scroll bar is ~600 B of Frotz and ~560 of TaskMgr — the largest single
    region; worth its own look.
- **`os88spkfx.inc`**: the 2,816-byte level family is reached `cs xlatb`, so
  it is bss in every instance even on a card machine. A segment variable, or
  the family inside the speaker ring claim (Audio `aps_fam`, Tracker
  `tsp_fam`, Video).
- **`os88data.inc`**: `od_find` carries an inline find buffer (24 B, or the
  30 zero bytes of `times OSAPI_FIND_SZ db 0` as set 2 measured it) in the
  IMAGE; bss or a caller-supplied one saves it in every includer. And a
  **read-into-place helper** - read the file straight into the live table,
  else put the defaults back - removes the staging buffer every includer keeps
  (Cyclone did it by hand this pass; Dot Delirium drafted it). The include is
  ~370 B of every includer.
- **`os88con.inc`** (DOS, Telnet; pick list 11): `con_glyf` is a 2,048-byte
  bss copy of the CP437 face built at launch from `os88cp437.inc` (1,280 B of
  image). Reading the packed form, or the kernel's ROM font where the glyphs
  agree, is ~2 KB of RAM in each; `con_band` (640 B bss) might share the
  carrier's scratch.
- **`os88sock.inc`**: `net_call` taking only the verb (`NET_CLASS` is a memory
  operand) ~3 B a call (browser, telnet, ftpd, thewire); and `net_find` could
  return the `NETV_STATE` socket check (~15 B a package, system-side pass).
- **`os88line.inc`**: a "draw this block, keep all registers" entry, ~5 B a
  call (browser, telnet).

## 4. The C toolchain (`tools/cc8086.py`, `apps/cc/`) — every C package

cword, runcpm, c64, apple2, paccman, weave, loom.

| # | what | estimate | status |
|---|---|---|---|
| A.1 | Compare-as-branch peephole: a compare feeding `&&`/`\|\|`/`?:` is materialised 0/1 and tested again (`mov al,1 / jcc / mov al,0 / cbw / test ax,ax / jz`, ~9 B a site) | set 2 counted loom 375 sites (~3.4 KB), weave 313 (~2.8 KB), cword 220, apple2 186, c64 168, runcpm 146, paccman 110; set 1 read c64 at 318; faster | open — apple2 left ~210 sites for it rather than hand-edit; `cwpeep.py`'s `setcc` rewrite is this |
| A.2 | Shift by constant: `mov cl,N / shl` for N ≥ 3, a byte move for 8 | apple2 ~690, c64 ~640 | open |
| A.3 | General peephole (dead `mov ah,0`, unused post-increment values) | hundreds a large package; **-8,589 B measured in CWORD** (resident ~4,540, overlay ~4,050; CWORD.OVL claim 19 → 15 KB) | **built as `apps/cword/cwpeep.py`, committed OFF** (`CWPEEP ?= --off`; `make cword CWPEEP=`). Liveness-based, 16 rewrites, reuses `cc8086.def_use`. Its first combined build MISCOMPILED (thread and jccjmp rewriting one branch in one sweep; fixed by per-sweep node ownership) and the full differential session did not finish — so it wants a differential verifier (a function-level interpreter for SmallerC's subset, or the scratch `cwdiff.py` session) before it ships, and a size-aware jump retargeter (retargeting can push a short jump out of range) |
| A.4 | Merge identical string literals; fold `label+const` | tens-hundreds | open |
| A.5 | Per-thunk gate in `os88thunk.asm` | apple2 ~1,340 | **BUILT** (set 2, `b78e6dba`, SPEC.md 73.9.1): `cc8086.py` emits `CC_U_<name>` per extern; -1.6..2.3 KB RAM a C package. `apps/loom/lmpvmod.asm` still includes the thunks ungated (`CC_THUNK_ALL`) - included after the C with `CC_THUNK_GATED`, `LOOM.WPV` would carry only its own |
| A.6 | `os88_memset`/`os88_memcpy` as `rep stosb`/`movsb` | small, all | **BUILT** (set 2, with strlen/strcpy/utoa and the thunk tightening in SPEC.md 73.9.1) |
| A.7 | `os88_peek16(seg, off)` | apple2 hand-rolled one | open |
| A.8 | `os88_part_shrink(i, keep)` — give back a part needed only at load | apple2 3 KB (done by aliasing `op_base`); c64 chargen | open |
| A.9 | Drop an overlay once loaded (`cc_ovneed` holds it for the session) | 4-18 KB (`CWORD.OVL` 18,565) | open — c64 alternative is folding C64.OVL back in, owner's call |
| A.10 | `CC_PACKAGE` passes `$(PKGZARG)` | c64 image 40,244 → 23,275 on disk | **BUILT** (set 2, `72d6086e`): cword 36,514 → 20,351, weave 51,700 → 30,041, paccman 42,172 → 19,427, runcpm 39,772 → 21,523 on disk. LOOM, CHELLO and COVL are open-coded in the top Makefile and NOT reached by it (pick list 1). `os88pkg.py` still refuses `--compress` on a package with parts, so c64 and apple2 stay plain |
| A.11 | Ship every C `.OVL` as a `'CZ'` file | LOOM.OVL 43.7 → ~22 KB, WEAVE.OVL 21 KB, CWORD.OVL 18,565 → 8,851 | **done for CWORD only** (set 2, `build/cwordz/`); `cc_ovneed`'s transparent read expands it into the claim it already sizes from the image, so no runtime change. One risk to weigh: a host copy that drops the directory-entry size hint makes the overlay refuse with "Old X.OVL" - clean, but a regression for files copied from a PC. Belongs in `CC_PACKAGE`, after which CWORD's own rule goes |

## 5. Tools, generators and the Makefile

### 5.1 Makefile

- `$(PKGZARG)` / `$(PKGZSTAMP)` are missing from the **FROTZ, FPTEST and
  IMGTEST** `.o88` rules, and from the open-coded **LOOM, CHELLO and COVL**
  ones (set 2 fixed the C family through `CC_PACKAGE`, A.10). FROTZ ships on a floppy: 22,578 → ~18,700. FPTEST 5,813 →
  5,012, IMGTEST 2,851 → 2,313 — gate disks only. 20.13.5.2's defect, three
  more rules along; worth a gate that walks every `.o88` rule.

### 5.2 Gorillas' generators (`tools/gorillas_art.py`, `_bgm.py`, `_music.py`)

The largest single item, ~9.5 KB of RAM, one package, no resident cost. The
tests run the generators in `--check` mode against the committed output, so
the format is theirs to change, together with the asm that reads it:

- `grart.inc` (11,169 B): rebuild only the CURRENT adapter's colour
  conversion tables (`gr_winmap`, `gr_winx2`, `gr_planebits`, `gr_winplanes`,
  `gr_winplanesx2`, `gr_monopairs`, `gr_monopairsx2`, `gr_cgapairs` — 4,608 B)
  into one 1,536-byte bss area on a display change (~-3.0 KB, ~30 ms once a
  change, never a frame); derive the front sprite variants from
  `gr_apeleft/right` (~-2.3 KB, ~640 bss for the clipped fallback); generate
  the marquee masks and runs straight into the light cache (~-2.2 KB);
  bit-pack the sun (~-440); derive the raised-arm poses from `gr_ape` (~-320).
- `grbgmdata.inc` (2,697 B): byte note indices into a frequency table instead
  of Hz words, ~-1.1 KB.
- `grmusic.inc` (728 B): byte note + byte ticks, ~-350.

### 5.3 Seen in packages outside a set's own

- **PiXEL `px_dmaclaim`** (`apps/pixel/pxpump.inc`) claims one KB more than
  it needs so it can 512-align the buffer by hand; guard 6b already aligns
  every claim base (`os88type.inc` learned this). ~1 KB of heap per PiXEL
  open, 0 resident - the size pass that reaches `pixel` should take it (set 1,
  from Audio).
- **Private About cards**: SPEC.md 20.5.1.1 listed 17 packages with their own.
  Set 1 converted arkanoid, browser and calc to `os88ui_about` (the shared card
  is not always smaller - Audio's own measured smaller and stayed); the rest
  of that list is unchecked. Browser's private card also drew without a clip
  region.

### 5.4 `tools/os88imgcase.py`

IMGTEST's record format (16 B × 27 cases + 244 B of names): drop the stride
(always `(w+1)/2`), byte-size picno and err, names inline — ~100-160 B, with
the harness changed in the same commit; the `imgcases --check` row follows.

## 6. Cross-cutting defects found on the way, not fixed

### 6.1 SPEC.md 12.2 against `kernel/ui.inc`

The spec lets a window click handler clobber SI; `ui.inc` stores SI into
`[ui_armw]` right after the handler returns (~line 488, after `ui_bill`), and
neither `ui_bill` nor `wm_pkgcall` saves it. A handler that clobbers SI as
permitted arms the button release on a garbage window pointer. Save SI in the
dispatch (~2 resident bytes) or tighten the spec.

### 6.2 Tests that re-assemble `SOUND.DRV` without the build's defines

`tests/gorillasmusic.py` assembled the driver without `-DPICOMEM`, so every
symbol read 341 bytes out of place and the row failed on the base (fixed on
`apps-size-3`, `21224596`). **`tests/trackmove.py` (~line 267) does the same
and reads `sbl_poolseg`** — very likely the same defect, unchecked. The
general fix is a helper that derives the defines from the Makefile, the way
`os88sym`/`os88build.py` do for the kernel.

### 6.3 `1942sound`'s card legs, and probably DrMarco's audio test

`1942sound --backend adlib` and `--backend sb` fail on the base at
`assert driver('sbl_up')[0]==(backend=='sb')`, before any 1942 audio runs.
The plumbing is shared - `tests/drmario_audio.py`'s `driver_symbols()` /
`drv_fseg` resolution - and **set 2 confirmed DrMarco's fails the same way**:
`drmarioaudio`'s AdLib arm fails at that assertion with the unmodified base
package, while its speaker arm passes. It looks like 6.2's shape (symbols read off a driver assembled with the
wrong defines) and wants that same helper. Until it is fixed, 1942's FM path
has no gate; set 1 ran the speaker leg only.

### 6.4 Packages with no emulator row

IMGTEST (the picture decoders' only gate), FPTEST (the float library's) and
FROTZ (whose gates are `make zcheck`/`zgfx`, and need stories a container
without `ifarchive.org` cannot fetch) have no registered soak row; the
browser's `br*` scripts are unregistered and four had rotted (set 1). Each
was driven by hand this pass.

### 6.5 Package defects found and left open (set 1)

Not shared, but found by this pass and not fixed by it, so they are listed
where somebody picking work will see them:

- **Calculator**: `cal_isqrt` can seed `1<<32` when the scaled radicand
  reaches bit length 63; DX:AX wraps to 0 and `cq_divmod` divides by zero.
  Predicted for `sqrt(720575940)`, not driven on the machine.
- **Browser**: a navigation started while the worker is still parsing the
  previous page lets the parse write into a just-freed buffer. `brlink` fails
  three assertions on the base (a stale toolbar layout in the test), and
  `tools/os88proxy.py` still names the removed `br_lmap` in a comment.
- **Arkanoid**: a full repaint fills the side rails and draws them again
  (written twice, PERFORMANCE.md rule 2); the About and credits paths draw the
  board without a clip region.
- **ArtfulType**: the VGA code-line path SPEC.md 46.4.12 introduced has no
  cycle count and no break-on-purpose run yet.

And set 2's:

- **Cyclone**: `cy_pn_dismiss` says it preserves everything and goes through
  `cy_full_repaint`, which never did. And set 2 changed the high-score
  load/save path (read and written in place, format unchanged) and **no row
  exercises it** - it was not driven.
- **DOS box**: `apps/dos/doscents.inc` names a `tests/unit/t_doscore.py` that
  does not exist; `kerndos/kdmouse.inc`'s comment still says `KD_IMG_KB` 35
  (it is 33); `dos_k_copy` is a back-end door nothing calls, kept because the
  slot numbers are an ABI.
- **DrMarco**: the README's timing table predates set 2's renderer changes.
- **Dot Delirium harness trap**: the dotdel rows read package offsets using
  the image size in `build/dotdel.bin`; a stale one makes `ddsmall` report
  `content 0x0`, which reads exactly like a broken package.
- **Chart has no row at all**: it has no association, so nothing opens it;
  set 2 checked it with a scratch MartyPC differential (61/63 steps, the two
  being its Export fix), CGA only.

## 7. Process notes for the next pass

1. **Agent worktrees were cut from `main` (`95f7e971`, 247 commits behind),
   not the session's branch** — in sets 1 and 3 (set 2 does not say); in
   set 1, seven of eight agents moved themselves and Audio measured on the
   stale tree and had to rebase; in set 2, seven of eight worktrees were cut
   there, six agents reset themselves and the coordinator caught the seventh
   (DOS) from `git worktree list` before it had measured anything. On it `make` fails in
   `apps/pixel/pxfull.asm`, which is how most noticed. Brief: *"your first
   command is `git reset --hard <branch>`"*, and the coordinator checks every
   worktree's base (`git merge-base --is-ancestor <base> HEAD`) before
   accepting a single number.
2. **The base moves.** `elendilon` took four FTPD commits mid-pass; the FTPD
   agent had to merge and re-baseline. Fetch the base when merging each
   agent, not only at the end.
3. **`tools/incsize.py` writes `/tmp/attrib.lst` and `/tmp/attrib.bin` at
   fixed paths**, and agents sharing a scratchpad clobbered each other's
   listings. Per-process paths in the tool, and a per-agent scratch directory
   in the brief.
4. **`pkill -f "make -j2"` matches every agent's build** (set 1). Forbid
   pattern kills of shared command lines; kill by PID.
5. **Writes outside the worktree** (symlinked `build/martypc`, `build/cc`, the
   session scratchpad) caused permission prompts mid-run. Say in the brief
   that the links are read-only.
6. **A deadline mid-run works**: set 3's agents were told "~30 minutes,
   commit a clean state, report" and every one landed a committed result with
   its rows run. So did set 2's eight.
7. **Eight agents' emulators on four cores make rows fail that are not
   failing** (set 2): `cycplay` died with "guest at under a tenth of real
   time" and `dotdelpen` lost a leg waiting 8 host seconds on a breakpoint;
   both passed on the merged tree with the box quiet. Re-run a load-shaped
   failure alone before reading it, and have the coordinator run each item's
   emulator rows again on the merged tree.
8. **Two items can own one mechanism** (set 2): the C SDK agent made
   `CC_PACKAGE` pack every C package while the CWORD agent packed CWORD
   through its own rule; both were right alone, and the merge had to delete
   the second. When a brief hands one agent a SHARED file (`apps/cc/`), tell
   the agents whose packages consume it.
9. **Do not wrap `os88soak.py start` in a `timeout`**: it does its prewarm
   in the foreground, and a timeout that fires there leaves an empty
   `build/soak/<run>/` and no run. `os88test.py soak -k ...` in a background
   task is the simpler shape for ten rows.
