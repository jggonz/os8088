# ksp17 agent "draw" - the drawing, cursor, mouse and video files

Kernel size pass 17 (`docs/plans/completed/ksp17/BRIEF.md`), section 1's
`draw` row: a general finder over `vga12.inc`, `softgfx.inc`, `font.inc`,
`mouse.inc` (the cursor code with it), `viddet.inc`, `vidsel.inc`,
`vidext.inc`, `blank.inc`, `fsx.inc`, `band.inc`. Branch `ksp17-draw`, base
`00348bdc` (the brief, on `af5579d2`). Figures are ASSEMBLED bytes,
`tools/kernsize.py --json` absolutes; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga` (+ `.mega` on kern_mega). Per-routine figures
are `tools/os88rdiff.py kernel 00348bdc HEAD` spans.

## WHAT THE FINDER LOOKED FOR

Pass 16's gui agent had read these files for single-caller routines and
compares made twice, so this pass ran the censuses it did not, each as a
scratch script over the source and then read by hand:

* **banks nobody needs** - a routine's head pushes whose register its own body
  never writes (callees checked by reading: `vid_disp_planes`, `gfx_ls_box`);
* **flags already set** - an `or r,r` / `cmp r,0` whose flags the last
  arithmetic on `r` already left, with only `mov`/`push` between
  (`gfx_rect_setup`, kern_mega's `fsx_rkeepm` walk);
* **a register already zero** - `cmp byte [m], 0` where an `xor` a few
  instructions up still holds 0 (`gfx_unlock`, `gfx_blit1_x`);
* **`cld` under SPEC.md 1's DF = 0 promise** - in kernel routines that no ISR
  enters without its own gate `cld` (and every drawing ISR has one);
* **single callers whose call site has dead registers** - the shape pass 16
  found cheapest, this time reaching the ones it left: `kbm_accel`,
  `mou_claim`, `gfx_pt_resolve`, `gfx_1bpp_off`, `sw_rect_pl`, `blk_one`;
* **refusal trampolines** (`jmp short .tests / .fail: stc / jmp .out`) that a
  stub just outside the routine replaces.

`band.inc` is a knob (`BAND=1`) and was not touched: no shipped kernel
assembles it.

## BASE AND TIP

| kernel | base `00348bdc` | tip | delta |
|---|---:|---:|---:|
| **kern_big** resident | 91,879 | **91,797** | **-82** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` / `.vga` | 40,925 / 5,435 / 37,793 / 5,358 / 336 / 2,032 | 40,847 / 5,435 / 37,789 / 5,358 / 336 / 2,032 | -78 / 0 / -4 / 0 / 0 / 0 |
| kern_big `KERN_SIZE` / `.boot2` / `.ovl` | 97,280 / 6,455 / 5,116 | 97,280 / 6,455 / 5,116 | 0 (no rung) / 0 / 0 |
| **kern_small** resident | 57,718 | **57,667** | **-51** (`.text` 30,494 -> 30,443) |
| **kern_emu** resident | 92,133 | **92,051** | **-82** (`.text` 41,179 -> 41,101, `.cold` -4) |
| **kern_mega** resident (with `.mega`) | 125,183 | **125,085** | **-98** (`.text` 46,167 -> 46,089, `.cold` -4, `.mega` 21,712 -> 21,696) |

No module image, driver, `.boot2` or `.ovl` byte moved. `make`, `make small`,
`make emu` and `make mega` build at the tip; the fast tier passes at every
commit (hotpar and stkbalance among it); `os88megaseg` is 3,248 within the
3,250 `tests/megaseg.txt` allows (unchanged). kern_mega's `.mega` -16 is the
fsx walk's -2 falling through an `align 16`.

## TAKEN

kern_big bytes from routine spans (kern_emu and kern_mega take every kern_big
item; kern_small the ones it assembles, column 3).

| # | item | kern_big | kern_small | what went |
|---:|---|---:|---:|---|
| 1 | **`vid_text` onto the ladder, past the splash window** | -7 (75 -> 68) and `vid_pop8`'s label | -4 | It sat INSIDE `spw_resident_end`'s window sharing `vid_pop8` with `vid_setmode`, and `tests/unit/t_resident.py` rightly refuses a ladder jump there. But no splash path reaches it - its callers are `fsx_mode` and `vid_reboot` - so it moved to the end of `viddet.inc`, past the window, and banks with `kent_bp` / leaves by `kret_es` (-4). Its mode 3 is loaded once for both arms so the mono arm needs only `mov al, 7` (-1), and the VGA machine's mono arm is in line under `jz .go` instead of behind `jnz .mono / jmp short .go` (-2). `vid_setmode` falls into its own pops; the `vid_pop8` label went (stkbalance walks a label nothing reaches as an entry). SPEC.md 15.1.2's account is amended. |
| 2 | `vid_disp_planes`: no DI bank | -2 | - | `vid_disp_of` (via `vid_disp_find`) and `vid_kind_of` both keep DI |
| 3 | `gfx_ls_box`: no DI bank | -2 | -2 | nothing in it writes DI, and `gfx_clip_query` keeps every register |
| 4 | `sw_patcol` counts in BP | -2 | -2 | SI's push/pop pair: the caller writes BP only after both calls |
| 5 | `gfx_xor_rect_d.pops` -> `pop bp / jmp kret_dx` | -2 | -2 | five pops and a `ret`; `sw_xfer` leaves through the same label |
| 6 | **`fsx_mode`'s refusal and `fsx_run`'s** as stubs outside the routines | -3, -2 | -3, -2 | `fsx_mfail: stc / jmp short fsx_run.out` after `fsx_run`, `fsx_rfail: stc / jmp kret_di` before it; each routine's `jmp short .tests` trampoline went, the tests reach the stub backward in two bytes |
| 7 | **`fsx_mode`'s Mode X CRTC unlock** as `fsx_xcrtc`'s first word | -8 | - | `mov al, 11h / out / inc dx / in / and al, 7Fh / out / dec dx` became `dw 0x0E11` at the head of the table the loop already walks (count 10 -> 11), and the `cld` before it went. See *The CR11 word* below. |
| 8 | `fsx_mode .fill`'s `cld` | -1 | - | DF = 0 on entry (SPEC.md 1); every call between is the kernel's or an `int 10h`, whose `iret` restores DF |
| 9 | `vid_ctx_act`'s `cld`, `vid_text`'s mono clear's `cld` | -1, -1 | - | DF = 0 (SPEC.md 1); every drawing ISR clears it at its gate |
| 10 | `thm_set`: one `mov bl, al` below the 1bpp Windows/Color fork | -2 (`.cold`) | - | AH and the flags at `.apply` are what they were on both arms |
| 11 | `vgas_bankcopy`'s two `cld` (one in `.gen`'s per-row loop) | -2 | -2 | DF = 0 (SPEC.md 1) |
| 12 | **`blk_one` gone**: `blk_set` chooses once | -2 | -4 | kern_big: `SI = vid_unblank_kind / vid_blank_kind` from AL (which `ssf_set` keeps), `call si` in the display walk; kern_small: a branch at its one call |
| 13 | **`gfx_scroll`**: the direction off SI, the step built in AX | -8 | -6 | `cmp word [vgas_sdy], 0 / jl` is `or si, si / jns` (SI is still dy), and the two arms' four stores are one ydst, one conditional ydst and one step store; kern_big's VID_EXT parity pad in the same routine went with the odd total. kern_small spells the test `cmp si, 0` (3 bytes) so its total is even - a parity choice between equivalent forms. |
| 14 | `mou_pall`: two ports, two calls | -2 | -2 | the two-iteration loop written out; an `%error` holds MOU_PEND to the two rows named |
| 15 | `vid_depth_set`: planar is one unsigned compare after `dec` | -2 | - | `vid_bk`'s map (VGA FF, HERC 0, CGA 1, EGA 2); VESA is answered above it |
| 16 | **`kbm_accel` written into `kbm_move`** | -12 | -12 | AX and CX are dead there: three banks, the call, the ladder jump, and the tick bank as one `xchg dx, [kbm_last]` |
| 17 | **`mou_claim` written into `mou_byte`'s `.pkt`** | -4 | -4 | the call and the `ret`; see *Hot paths*. On kern_big `mou_hotplug`'s state-2 exit lands on `mou_newround`'s `ret` (`.ret`), -1 that evens the odd 49-byte move for the ISRs in between; kern_small's claim was an even 62 and keeps its own `ret` |
| 18 | `gfx_pt_resolve` written into `gfx_points`' `.pass` | -6 | - | AX and BX are dead there: its AX bank, call and ret |
| 19 | `gfx_1bpp_off` written at `gfx_sr_tail` | -4 | - | its one caller stores `VGA_PN_FOUR` as `cur_rect` already did |
| 20 | `sw_rect_pl` written into `sw_rect` | -2 | -2 | its call, with its tail `jmp sw_plane_op` now a `call` returning to the ladder |
| 21 | `gfx_rect_setup`: the span test is the `sub`'s ZF | -2 | -2 | an `or dx, dx` after a `mov` |
| 22 | `gfx_lock`: flag and owner in one word store | -1 | -1 | `mov ah, al / mov al, 1 / mov [gfx_lock_flag], ax`, inside the same `cli` window |
| 23 | `gfx_unlock`: `cmp [cur_lazy], ah` | -1 | -1 | AH is still the `xor ax, ax`'s 0: every call since keeps AX |
| 24 | `gfx_blit1_x`: two compares against a register already zero | -2 (`.cold`) | 0 | the pen test against the BL its `xor` cleared, and (planar kernels only) r0 zero-extended by `xor ax, ax / or al, [wm_clip_r0]`; the pair keeps the row loops' parity, kern_small keeps the old three |
| 25 | kern_mega's `fsx_rkeepm` walk: `or bx, bx` after a `sub` | (`.mega`) | - | only `mov`s between |

Plus `disk.inc`'s `align 2` after `dsk_img16`: at the first commit it
absorbed one byte of an odd shift, and gave it back when the later items made
the shift even again, so the span sum and `kernsize` agree at the tip.

### The CR11 word (item 7)

The read-modify-write of CRTC register 11h existed to clear bit 7 (protect
CR0-7) while keeping the BIOS's low bits. The table writes CR11 = ACh four
words later, so those low bits live for four `out`s of a mode set; `0Eh` is
the value IBM's mode 13h leaves there (`8Eh & 7Fh`), so on an IBM-compatible
VGA the transient is identical and on any card the final state is identical.
It is listed here and not in WANTS THE OWNER for that reason; if the
coordinator would rather not carry even a transient difference on a clone
BIOS, the revert is item 7 alone (+7, the `cld` stays gone).

### Hot paths

Nothing on the brief's hot list got slower; `hotpar` is green on kern_big and
kern_small at every commit and **no new parity pad was added** (one was
removed, item 13). Cycle notes:

* **The serial mouse ISR got faster** (item 17): a settled port - every packet
  of the session after the contest - now skips the claim arm on one compare
  and a taken jump, where it paid `call` + `push ax` + compare + taken `jne` +
  `pop ax` + `ret` and then the same compare again (~95 clocks a packet less
  on an 8088), and the private ISR stack carries one return address less.
* `gfx_points` (item 18): one `call`/`ret` and an AX bank less per pass (not
  per point); `gfx_ls_box` (item 3) two stack moves less per resolve.
* 1bpp fills (items 20, 21): one `call`/`jmp` pair and one instruction less
  per rect.
* `gfx_lock` / `gfx_unlock` (22, 23): a word store for two byte stores, a
  register compare for an immediate one.
* `gfx_blit1_x` (24): two register compares for immediate ones, in the
  per-call setup; the row loops moved by -2 (even) and are byte-identical.
* `vid_ctx_act`, `vgas_bankcopy` (9, 11): a `cld` less per display swap, per
  scroll and per bank-copy row.
* **What got slower, all off the hot list**: `vid_text` (item 1) pays
  `kent_bp`'s ~95 clocks once per text-mode switch (an fsx bracket's
  FSXM_TEXT80 or the reboot); `gfx_xor_rect_d.pops` (item 5) a ~15-clock jump
  once per outline, save or restore on 1bpp.

Parity choices made instead of pads: item 13's kern_small `cmp si, 0`; item
17's kern_big `mou_newround.ret`; item 24's planar-only r0 form; item 1's
mono arm leaving by `jmp short .out` rather than straight to the ladder. **For
the coordinator's pad pairing**, none of these is a pad, but each pins a
layout the way one does.

## REFUSED

* **`gfx_sub_arm` on `kent_bp`** (-4): it is called once per save-under
  fragment on the damage-repaint path - DECLINED 3's reason (priced per
  window, not per call). Its SI bank is unneeded but the ladder has no rung
  that skips SI, so dropping it costs a byte.
* **`vga_save_vram_v` / `vga_restore_vram_v` pop tails onto `kret_dx`** (-2
  each): `cur_saveu` / `cur_restoreu` reach them from inside the mouse ISR's
  `cur_move` - a hot path, +15 clocks a move.
* **`vga_solid_rect_v` / `gfx_fill_gray_raw_v` pop tails onto `kret_di`** (-4
  each): every VGA fill; the gray body's own teardown was measured at +0.29%
  and kept (size pass 10).
* **`cur_saveu` / `cur_restoreu` sharing one epilogue** (-4): the jump would
  land in `cur_move`, which is hot.
* **`gfx_scroll_v`'s `cld`** (-1, `.vga`): `cur_draw` follows it in `.vga`,
  and an odd shift there moves the cursor's draw loop.
* **`sw_rect`, `sw_fill_pat` and `gfx_spans` on `kent_bp`** (-4 each): a 1bpp
  fill, a pattern fill and a span list are primitives (pass 15's refusal of
  `kent_*` on drawing primitives).
* **`thm_set`'s `cld`** (-1, `.cold`): built, and it put `dsk_xfer`,
  `dsk_runcap` and `dsk_next_clus_x` at an odd shift; item 10's -2 was taken
  instead.
* **`gfx_blit1_x`'s two `push cx / push dx / add / dec / ... / pop` corner
  blocks** as a shared helper: -1 at best once the helper's far return is
  counted.
* **`kbd_ovspend` / `kbm_p5spend` as one body**: both are 16 bytes, and a
  shared body needs a pointer and an indirect call per entry.
* **`vid_bk .planar`'s `neg/sbb/and`** and **`.mono`'s DX loads**: no shorter
  equivalent found.
* **`fsx_caps`'s VESA -> VGA map** (7 bytes): DL is an output - see WANTS AN
  ABI BREAK.

## WANTS THE OWNER

None. Every item is byte-identical behaviour (item 7's transient is argued
above and is the coordinator's to revert if wanted).

## WANTS AN ABI BREAK

Priced, not worth taking:

* **`OSAPI_FSX_CAPS` answering `VID_VESA` (4) in DL** instead of mapping it to
  `VID_VGA` - 7 bytes of `fsx_caps`. At least nine packages compare DL with
  `VID_VGA` (Tracker, DrMarco, 1942, Excitebike, PiXEL, Pixelstein, Missile,
  Skies, DOS.O88's `con_tkind` in three places), every one of which would
  need a VESA arm. Not worth seven bytes.

## CROSS-FILE

* `SPEC.md` 15.1.2 (`vid_pop8`, item 1) and 9.6.3 (`kbm_accel`, item 16) are
  amended where they named what moved. Comments elsewhere (SPEC.md 9.5, and
  `hiber.inc`, `tests/mouwheel.py`) still say `mou_claim` / `gfx_pt_resolve`
  for the logic; they read true of the arm that now holds it, and no gate or
  breakpoint names either symbol (grepped).
* `hiber.inc`'s `mov bp, mou_pall` (kern_dos) is unchanged: `mou_pall` keeps
  its contract.
* None of the other six agents' rows names a routine this pass edited.

## ROWS RUN

All one `python3 tools/os88test.py soak -k ... --marty-jobs 1` after the last
code commit (`b9c006c3`), MartyPC on the GLaBIOS twins, nothing edited while
they ran: **41 / 41 ok, 0 failed, 0 skipped** in 1,510 s. No row was broken on
purpose this pass (each was already registered against the behaviour it
asserts; `resident` is the gate item 1 had to clear and was read green).

| rows | what of this pass they reach |
|---|---|
| `dispreboot`, `disptext`, `vidtext`, `vidtextherc`, `vidtextvga` | `vid_text` (item 1), the reboot path |
| `dispmodex`, `vidmodex`, `skiesvga`, `wireballvideo`, `dispfsx`, `dispfsxcga`, `dispfsxherc`, `fsxclip` | `fsx_mode`, `fsx_run`, Mode X's CRTC (items 6-8) |
| `dispsaver`, `saver`, `savervga` | `blk_set` (item 12) |
| `dispthm`, `thm95` | `thm_set` (item 10) |
| `gfxpoints`, `ptsmix` | `gfx_points`, `gfx_ls_box` (items 3, 18) |
| `win1bpp`, `su1bpp`, `tmload`, `paint1blit` | 1bpp fills, pattern fills, save-under, `gfx_blit1_x` (4, 19-21, 24) |
| `dispdepth`, `dispstrad`, `dispseam`, `dispnp` | `vid_disp_planes`, `vid_ctx_act`, `vid_depth_set` (2, 9, 15) |
| `dispdrag`, `dragui` | `gfx_xor_rect_d` (5) |
| `npscroll`, `wdscroll` | `gfx_scroll`, `vgas_bankcopy` (11, 13) |
| `mouwheel`, `mouerrq`, `mouresume`, `mouseup`, `curshape`, `serlease`, `wirefleetnomouse` | `mou_byte`'s claim, `mou_hotplug`, `mou_pall`, `kbm_move` (14, 16, 17) |
| `gfxlk` | `gfx_lock` / `gfx_unlock` (22, 23) |
| `resident` | item 1: no ladder jump inside the splash window (host-side) |

## DEFECTS FOUND

* `vid_pop8`'s header and `t_resident.py`'s docstring said `vid_text` runs
  from the loading screen; nothing on any splash path calls it (its callers
  are `fsx_mode` and `vid_reboot`, and `vid_reboot`'s are `ui_cmd_reboot`
  and HIBER.DRV's). Corrected in `viddet.inc` and SPEC.md 15.1.2;
  `t_resident.py`'s history paragraph is left as the record it is.
* `gfx_ls_box` banked DI "to preserve every register" while writing BX, which
  its banner does not list; the one caller already reloads BX (unchanged,
  noted).
