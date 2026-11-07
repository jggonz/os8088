# ksp18 agent "draw" - the drawing, cursor, mouse and video files

Kernel size pass 18 (`docs/plans/completed/ksp18/BRIEF.md`), section 2's `draw` row: a
general finder over `vga12.inc`, `softgfx.inc`, `font.inc`, `mouse.inc` (the
cursor code with it), `mouproto.inc`, `vmmouse.inc`, `moudiag.inc`,
`viddet.inc`, `vidsel.inc`, `vidext.inc`, `blank.inc`, `fsx.inc`, `band.inc`,
except the routines `fresh-routines.md` gives `hot`, `dir` or `main`. Branch
`ksp18-draw`, base `c5a7b0f9` (the brief). Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on kern_mega). Per-routine figures
are `tools/os88rdiff.py kernel c5a7b0f9 .` spans.

`moudiag.inc` and `band.inc` are knob-only (`MOUDIAG=1`, `BAND=1`) and
`vmmouse.inc` is kern_emu's alone, so none of the three is resident on
kern_big; vmmouse.inc changed only to follow `mou_lockon`'s new contract.

## WHAT THE FINDER LOOKED FOR

Passes 16 and 17 had taken the single-caller routines and the obvious banks
in these files, so this pass read the resident routines in full (every one in
the files above that the fresh table does not assign, ~15 KB) and ran four
censuses over them as scratch scripts, then read every hit by hand:

* **a reload or a bank whose value is already there** on every path in
  (gfx_points' `.out`, vid_ctx_act's pointer recovery, mou_lockon's bank);
* **a conditional that is arithmetic** - `test / jz / or` and `test / jz /
  mov` pairs whose two answers differ by a bit pattern (the serial and PS/2
  decoders, sw_ink's AA/55);
* **two immediate stores where one register store does** (fsx_mode,
  kbm_move, osapi_gfx_fill_pat, thm_pen_dis);
* **constant data read only by code that is not resident** - which found
  `vid_tab`: its four constant rows are read by `vid_apply` alone, and
  `vid_apply` has no resident copy (it is expanded into the blob, CTRL.DRV and
  EXTD.DRV).

## BASE AND TIP

| kernel | base `c5a7b0f9` | tip | delta |
|---|---:|---:|---:|
| **kern_big** resident | 91,767 | **91,723** | **-44** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` / `.vga` | 40,633 / 5,376 / 38,034 / 5,358 / 336 / 2,030 | 40,589 / 5,376 / 38,034 / 5,358 / 336 / 2,030 | -44 / 0 / 0 / 0 / 0 / 0 |
| kern_big `KERN_SIZE` / `.boot2` / `.ovl` | 97,280 / 6,456 / 5,117 | 97,280 / 6,456 / 5,117 | 0 (no rung) / 0 / 0 |
| **kern_small** resident | 57,333 | **57,265** | **-68** (`.text` 30,232 -> 30,164) |
| kern_small `.boot2` / `KERN_SIZE` | 4,742 / 59,392 | 4,782 / 59,392 | +40 (the blob's copy of two `vid_tab` rows; 11 sectors, room) / 0 |
| **kern_emu** resident | 92,011 | **91,895** | **-116** (`.text` 40,877 -> 40,761) |
| kern_emu `.boot2` | 6,571 | 6,656 | +85 (the blob's copy of four rows; its 14 sectors have room) |
| **kern_mega** resident (with `.mega`) | 127,276 | **127,230** | **-46** (`.text` 46,307 -> 46,261; `.mega` 23,072 unchanged) |
| kern_mega conventional / `KERN_SIZE` at boot | 63,488 / 135,168 | **62,976** / 134,656 | -512 / -512 (a step each) |
| kern_dos (`build/kerndos.bin`) | 32,924 | 32,920 | -4 (MOU_DECODE_MS is shared source) |
| CTRL.DRV / EXTD.DRV, kern_big | 15,361 / 1,872 | 15,361 / 1,872 | 0 / 0 |
| CTRL.DRV, kern_small | 5,652 | 5,692 | +40: 12 sectors and its 6 KB claim unchanged |
| CTRL.DRV / EXTD.DRV, kern_emu | 15,379 / 1,872 | 15,462 / 1,955 | +83 / +83: 31 and 4 sectors, 16 KB and 2 KB claims, unchanged |

`make`, `make small`, `make emu` and `make mega` build at the tip; the fast
tier passes at every commit (`hotpar` green on kern_big and kern_small,
`stkbalance`, `cswrite`, `megaseg` among it). `os88megaseg`: kern_mega carries
3,680 bytes beyond kern_big in the segment (3,682 at the base). The rdiff's
HOT verdict at the tip: **all 18 hot routines identical, at an even shift**.
**No new parity pad**; two parity CHOICES between equivalent forms (below).

## TAKEN

kern_big bytes from the routine spans; kern_emu and kern_mega take every
kern_big item (kern_mega's gfx_points item is -6: its `KSEGR` is a 4-byte
load); kern_small the ones it assembles.

| # | item | kern_big | kern_small | kern_emu | hotness, cycles |
|---:|---|---:|---:|---:|---|
| 1 | **gfx_points: `.out`'s second `KSEGR ax / mov ds, ax`.** Every way into `.out` already has DS back: `.done` reloads it before any jump there and `.slow` never gives it away (gfx_pixel is called with the kernel's). The header's "takes it back by RELOAD at `.out`" described `.done` | -4 | -4 | -4 | H2 (once a call): two instructions fewer, ~16 clocks of fetch |
| 2 | **vid_ctx_act: the record addressed off SI as `rep movsw` leaves it.** SI ends exactly `VID_CTX_W*2` past the record, which IS `VID_CTX_VX`, so `sub si, 32` / `mov di, si` went and `[si]` is a byte under `[di+32]` | -6 | - | -6 | H2 on an extended desktop only (one display: the first compare answers). Faster: 5 bytes and ~10 clocks of EA less |
| 3 | **mou_lockon: no bank, `ret` for `jmp kret_cx`.** Both callers own AX..CX: mou_hotplug's `.have` banks AX..DX and leaves by `kret_dx`; kern_emu's `vmm_boot_x` banks all eight and now stores `[vmm_on]` as an immediate rather than from AL (`.boot2`, +2, given back). This was the khp1 size agent's HELD-TO-PAIR item; it is paired | -5 | -5 | -5 | once a session |
| 4 | osapi_mouse's re-hook arm: `inc di` x2 for `mov di, 2` (the khp1 size agent's other held item) - item 3's pair on kern_big | -1 | - | -1 | the FSX re-hook arm only; equal on an 8088 (4 clocks, a byte less fetch), +2 clocks on a 286 |
| 5 | mou_hotplug's settled exit is mou_newround's `ret` on kern_small too (kern_big has had it since pass 17) - item 3's pair on kern_small | - | -1 | - | one compare a UI pass, unchanged |
| 6 | **kbm_btn, kbm_rclick: no AX bank.** A CF = 1 key already spends AX (kbm_move never banked it), ui_task dispatches nothing after a CF = 1 key, and kbm_poll0 and kbm_p5spend bank AX themselves. kbm_key's banner now says so (DEFECTS FOUND) | -4 | -4 | -4 | H3 (a keyboard-mouse key) |
| 7 | **kbm_poll/kbm_pollm: the push first and the flag stored from AL** (`kbm_poll0`'s `push ax` becomes both entries' own, `mov byte [kbm_eat], n` becomes `mov [kbm_eat], al`) | -2 | -2 | -2 | H3; a register store for an immediate one |
| 8 | **vid_reboot: no AX bank.** Its callers are the reboot path's (ui_cmd_reboot, which never comes back; HIBER.DRV's hbm_bye and hbm_down through cw_mem_disp, neither of which reads AX before writing it). kern_small's body is a bare `jmp near vid_text` (item 16) | -2 | -3 | -2 | once |
| 9 | **fsx_mode: the two page-count stores as one through AH** (`mov ah, 1 / jmp .fp` and `mov ah, 4`, one `mov [es:di+..], ah`) | -2 | -2 | -2 | once a mode set |
| 10 | gfx_blit_run: `mov [gfx_color], ah` for `mov al, ah / mov [gfx_color], al` | -1 | 0 | -1 | H2 (a run on the gfx_fill path): 8088 max(clocks, 4 x bytes) 16 against 20, -4 clocks a run; kern_small keeps the old form for font_run's parity |
| 11 | thm_pen_dis: `thm_inkc` and `thm_disc` as one word load (`%if`-pinned adjacency) | -1 | - | -1 | H3 (a disabled chrome item); -4 clocks |
| 12 | **MOU_DECODE_MS's buttons on one jump** (mouproto.inc, shared with kerndos/kdmouse.inc): `shr dh, 4 / mov cl, dh / and cl, 3 / jpe / xor cl, 3` - the pair shifted down is the right order's MIRROR, which differs from it only when the bits differ, i.e. on odd parity. See *Hot paths* | -4 | -4 | -4 | **H2, per serial packet**: never slower, see below |
| 13 | **vid_tab's constant rows into the images that read them.** Each `VID_APPLY` expansion carries its own copy (`VID_TAB_ROWS` at the macro's foot, read through CS with DS following it for the one `rep movsw` - not `cs rep movsw`, which an 8086 interrupt can strand), and the kernel keeps only `vid_vxrow`, the VESA row its driver rewrites (`VXB_TABROW` and `ctrl.inc`'s two reads repointed). **kern_big keeps the table resident** (`VID_TAB_RES`) because its blob is full to the byte - WANTS THE OWNER 1 - and kern_mega because its switch rewrites the segment column to selectors. SPEC.md 39.2 and 120.4 say so; `tests/cswrites.txt` needs no line (the arm is lexically `%ifndef KERN_MEGA`) | 0 (WANTS THE OWNER: -72) | **-36** | **-72** | once per adapter apply (boot, the Display page, EXTD.DRV's restore) |
| 14 | **mou_p2_byte: each axis's sign by `cbw`.** Byte 0's bit shifted to bit 7 and `cbw`, the magnitude loaded over AL after - no `test / jz / dec ah` on either axis. See *Hot paths* | -4 | - | -4 | **H2, per PS/2 packet**: never slower |
| 15 | gfx_disp_enter: its nested exit is gfx_disp_enter_n's `ret` | -1 | - | -1 | nested whole-shape hook only |
| 16 | **vid_bk: 3BFh loaded above the direction test** (`mov` writes no flag), so the shared tail is `mov dl, 0B8h` | -1 | -1 | -1 | per blank / unblank. kern_small's vid_reboot is `jmp near` rather than short to keep its hot parity against this byte - a parity CHOICE |
| 17 | **sw_ink.dither: AA/55 without a branch** - `(y & 1)` negated and XORed into AAh | -2 | -2 | -2 | H2 (every 1bpp gray fill's set-up): ~21 clocks against 25-41 |
| 18 | kbm_move: the step and the direction in one word store (`mov dh, cl / mov [kbm_step], dx`; adjacency `%if`-pinned) | -2 | -2 | -2 | H3 |
| 19 | osapi_gfx_fill_pat: `[gfx_pat]` published from DI before the copy, not as an immediate after it (the lock is held, so nothing reads it in between) | -2 | -2 | -2 | per patterned fill call |
| | **total** | **-44** | **-68** | **-116** | |

Files reached that are no `draw` file, both renames only: `kernel/ctrl.inc`
(two reads of the VESA row, now `[vid_vxrow + 14/16]`; the file is `wmui`'s
resident half and this is its CTRL.DRV half) and `SPEC.md`.

### Hot paths

Nothing on the brief's H1/H2 list got slower, and khp1's picks are untouched
(no `fresh-routines.md` routine was edited). The two decoders are the ones
that needed counting:

* **MOU_DECODE_MS (item 12), per serial packet, inside mou_byte.** 8088 terms,
  `max(EU clocks, 4 x bytes fetched)` per straight run plus a queue flush per
  taken jump. Old: 18 bytes, two `test / jz` arms, so two flushes when no
  button is down (the common motion packet), ~48-72 clocks by button state.
  New: 14 bytes, ONE conditional, so one flush at most: `shr dh, cl` (24
  clocks) overlaps the fetch of the next three instructions, ~50-58 clocks by
  state. No state is slower and the drag states (one button down) are ~7
  clocks faster. **Equivalence**: both forms assembled alone and run under
  unicorn over all 256 header bytes x three CH values: CX identical in 768 of
  768; `jpo` for `jpe` breaks all 768.
* **mou_p2_byte (item 14), per PS/2 packet.** dy: 9 bytes / ~10 clocks against
  11 bytes / 15-25 (the old `jz` taken on a positive delta); dx: 10 bytes /
  ~38 clocks against 12 / 23-33 plus a flush - by the same measure 40 against
  48+. **Equivalence**: unicorn, 12,288 (byte 0, Y magnitude, X magnitude)
  cases, AX/BX/CX/DX/SI/DI identical in all; a wrong shift count breaks 6,144.
* gfx_points (1), gfx_blit_run (10), sw_ink (17), vid_ctx_act (2): fewer
  instructions or fewer bytes on the same path, counted in the table.
* `hotpar`: green at every commit on both gated kernels with no pad. Parity
  choices made instead of pads, which pin a layout the way a pad does: item
  10's kern_small form, item 16's kern_small `jmp near`, item 5 (kern_small's
  shared `ret`), item 4 (the odd byte for item 3 on kern_big).
* No change touches a disk path: no `int 13h` call added anywhere. The vid_tab
  copy in kern_small's and kern_emu's blob fits their existing sectors, so
  stage 1 reads what it read before.

## REFUSED

* **sw_fill / sw_fill_gray / sw_xor_fill setting `[sw_mode]` through AL**
  (-4): AX..DX are the rect, so no register is free at the entries.
* **cursor_hide/show passing the direction in CF** (-5): their contract keeps
  the caller's flags (`popf`), and CF would come back changed.
* **cur_move_multi banking `[vid_cur]` with `push word`** (-6, and
  `cur_dprev`'s `.bss` byte): it adds a word to the mouse ISR's private stack
  on the extended-desktop path; pass 14's rule is not to spend that stack on an
  estimate.
* **gfx_disp_run activating each display before asking its rect** (-4): it
  would leave a different display current after a primitive whose last
  display did not intersect, which SPEC.md 39.14.3's "restores none" makes
  observable to the next primitive.
* **gfx_fill_gray_raw_v's AA/55 the sw_ink way**: it is BL there, and the
  non-accumulator forms make it +1.
* **kbm_rclick's event code by `and ax, 2`**: EVT_RDOWN is 3, not 2.
* **vid_tab compressed instead of moved**: every encoding found costs blob
  code the full blob cannot take.
* Pass 17's refusals (gfx_sub_arm on kent_bp, the VGA save/restore and fill
  pop tails, cur_saveu/restoreu sharing, gfx_scroll_v's `cld`, kent_bp on the
  1bpp primitives) still hold and were not re-proposed.

## WANTS THE OWNER

1. **kern_big's `vid_tab` move: -72 resident, for a FOURTEENTH `.boot2`
   sector on kern_big.** The code is in this branch and running on kern_small
   and kern_emu (item 13). kern_big keeps the table resident only because its
   blob is full TO THE BYTE: `boot2_end` = `BOOT2_PAD` = 6,656 at thirteen
   sectors on the base (the base map's own symbol; `kernsize`'s `boot2` figure
   of 6,456 does not count the whole loader - DEFECTS FOUND 3), and the copy
   wants 72 bytes of rows and 11 of code there. `.ovl` cannot take it either
   (5,117 of its 5,120). Taking it is one line: drop kern_big from
   `VID_TAB_RES` in `viddet.inc` and add the sector the way kern_emu's is added
   (`BOOT2_KNOBSEC`). kern_emu already runs at fourteen; what the sector costs
   kern_big is what `tests/unit/t_blobruns.py` and `t_canary.py` say about
   stage 1's read and `KSIG_OFF`'s band at fourteen, which this agent did not
   run. Equally, any 83 bytes freed in the blob (it holds splash.inc, which no
   row owns, and font_init, vx_boot_x and the VSW_* expansions, which are this
   row's) take it with no sector at all.

No other item changes a picture, a result, a refusal or a transient.

## WANTS AN ABI BREAK

None found. `OSAPI_FSX_CAPS` answering `VID_VESA` in DL (pass 17's, -7) is
still priced not worth it.

## CROSS-FILE

* `kernel/ctrl.inc` (`wmui`'s file; the edit is in its CTRL.DRV half): the two
  VESA-row reads renamed to `vid_vxrow`, no byte.
* `kernel/vmmouse.inc` (kern_emu): `[vmm_on]` stored as an immediate after
  `mou_lockon`, `.boot2`, +2 given back.
* For `hot` (font_run_x, gfx_blit4 are theirs): nothing found worth taking in
  the parts read on the way.
* `kernel/vidsel.inc` around `vid_ctx_act`: a `%ifdef KERN_SMALL` PARITY `nop`
  sits inside the file's `%ifdef KERN_BIG` block and can never assemble - dead
  text, no byte (left).

## ROWS RUN

One `python3 tools/os88test.py soak -k ... --marty-jobs 1` after the last code
commit (`585c3c1a`), nothing edited while it ran (MartyPC on the GLaBIOS
twins, QEMU for the PS/2 and VMware rows): **40 / 40 ok, 0 failed, 0
skipped** in 863 s. No row was broken on purpose; the two decoder rewrites
were each shown to go red under their unicorn harness instead (items 12, 14).

| rows | what of this pass they reach |
|---|---|
| `small128`, `gfxptsmall` | kern_small booting on 128KB with the blob's own `vid_tab` rows (13); gfx_points there (1) |
| `vmmouse` | kern_emu: its blob's row copy and VESA arm (13), `vmm_boot_x`'s `[vmm_on]` store and `mou_lockon` (3) |
| `serlease`, `mouerrq`, `mouwheel`, `mouseup`, `curshape`, `mouresume`, `wirefleetnomouse` | the serial decode (12), the contest and `mou_lockon` (3, 5), the keyboard mouse before a mouse settles (6, 7, 18) |
| `kbd4f`, `kbdpreempt` | the keyboard ISR and the keyboard mouse's poll (6, 7) |
| `ps2mouse`, `wheelps2`, `p2restore` | the PS/2 decode (14) |
| `dosmouse`, `dosmouevt` | kern_dos's INT 33h, the same MOU_DECODE_MS (12) |
| `gfxpoints`, `ptsext` | gfx_points (1), the extended-desktop enter (15) |
| `dispstrad`, `dispnp`, `dispdepth` | vid_ctx_act (2), gfx_disp_enter (15) |
| `dispreboot` | vid_reboot (8) |
| `dispfsx`, `dispfsxcga`, `dispfsxherc`, `dispmodex`, `vidmodex` | fsx_mode (9), osapi_mouse's re-hook arm (4) |
| `saver`, `savervga`, `dispsaver` | vid_bk (16) |
| `thm95`, `dispthm`, `uidrdis` | thm_pen_dis (11) |
| `fillpat` | the patterned fill (19) |
| `paint1blit`, `b4walk` | gfx_blit_run (10) |
| `deskflash`, `win1bpp`, `su1bpp` | sw_ink's dither (17), 1bpp fills |

## DEFECTS FOUND

1. **kbm_key's banner said "all registers preserved"** on both answers, but a
   CF = 1 key through kbm_move has always spent AX. Harmless (ui_task
   dispatches nothing after one; kbm_poll0 banks AX); the banner now says
   AX is spent on CF = 1, which is also what items 6 rest on.
2. **gfx_points' header said DS comes back "by RELOAD at `.out`"** - it came
   back at `.done`, and `.out` reloaded it a second time (item 1).
3. **`tools/kernsize.py`'s `boot2` figure is not the blob guard's
   `BOOT2_SIZE`.** On the base kern_big it prints 6,456 while `boot2_end` is
   6,656 - exactly `BOOT2_PAD` at thirteen sectors, i.e. zero spare (kern_emu
   6,571 against 6,771 of 7,168; kern_small 4,742 against 4,953 of 5,632). A
   reader of `kernsize` would think kern_big's blob has 200 bytes of room;
   this agent's first build of item 13 on kern_big tripped the guard on that
   misreading. Not fixed (a tool outside this row): the coordinator may want
   kernsize to print `BOOT2_SIZE` against `BOOT2_PAD`.
4. vmmouse.inc's `vmm_boot_x` comment ("AL survives the thunk: mou_lockon
   preserves every register") is rewritten with item 3.
5. A `%error` message that NAMES a routine makes `tools/stkbalance.py` treat
   it as an address-taken ENTRY and walk it from depth 0 (`kbm_move: ret at
   depth -3` until the message was reworded). Worth knowing when writing an
   adjacency assertion; not fixed in the tool.
