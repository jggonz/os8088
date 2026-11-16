# Kernel size pass 19 - `recon`'s note

Branch `ksp19-recon`, cut from `kernel-size-19` at `174b80bf`. Rows: R2, R12,
R15, C10, R2-4, R2-5, V1, V2 (ABI-BREAK-LIST sections R, 10, 13; long form in
`docs/reports/big-review-2026-11-08/synth/flagday.md`), then a search for more.

All four kernels built (`make`, `make small`, `make emu`, `make mega`), the
fast tier green on kern_big, and the C packages built (`make cword runcpm c64
apple2 paccman lemmings weave loom`) - the C thunk moved. One short row run,
because it is the mechanism C10 changed: `tests/deskitem.py` **PASS** (Add,
the link launching, a Remove no row matches REFUSED, Remove taking it off) -
which also boots and drives a whole session through V2's yield cell.

## Measured, against the brief's 0.2 table (`tools/kernsize.py --json`)

| kernel | `.text` | `.cold` | resident | of which |
|---|---:|---:|---:|---|
| kern_big | 39,976 -> 39,971 (-5) | 37,821 -> 37,820 (-1) | 90,899 -> **90,893 (-6)** | R12 -5; R2 +6, C10 -7 |
| kern_small | 29,680 -> 29,678 (-2) | 21,037 -> 21,043 (+6) | 56,533 -> **56,537 (+4)** | R12 -2; R2 +6 |
| kern_emu | 40,222 -> 40,217 (-5) | 37,821 -> 37,820 (-1) | 91,145 -> **91,139 (-6)** | as kern_big |
| kern_mega | 45,728 -> 45,715 (-13) | 48,583 -> 48,582 (-1) | conventional **-13** | V2 -8 (the SLOT's per-cell `mega_sl_N` stub goes), R12 -5; `.cold` above 1MB -1 |

`.bss`, `.lowbss`, `.vgabuf`, `.vga` and `.mega` unchanged everywhere.
EXTD.DRV (a module, not resident): -1 (EXTF_CAPS).

## TAKEN

**R2 - snapshots bounded by the caller.** `OSAPI_SYS_SNAPSHOT` and
`OSAPI_CLAIM_SNAPSHOT` take CX = the records the buffer holds (>= 1) and
answer CX = the records written, min(CX, the kernel's count). The AX/BX
answers went. The ABI list said they were read by nothing: **wrong** - WIN16's
`w16j_give` and WaveRack's `wc_myslot` walked BX = INST_MAX and megaown AX =
MEM_MAX; all three now walk the CX answer, which is the bound they wanted.
SYS_SNAPSHOT clobbers DX now: that bought a byte (the clamp is `mov dx,
INST_MAX / cmp cx, dx / jae / mov dx, cx`) and put the `.cold` run after it
back at even parity (t_hotpar caught `dsk_next_clus_x`, `dsk_runcap` and
`dsk_xfer` at +9 on both kernels with the first spelling). Spans:
`osapi_sys_snapshot_x` 126 -> 130 (+4), `mmf_osapi_claim_snapshot` 27 -> 29
(+2), on kern_big and kern_small alike. The instance copy's word count is a
byte `mul dl` inside the cli window (~70 clocks of a ~2 ms window, twice a
second). Callers: Task Manager +9 (three macro sites), RedLine +3, WIN16 +1
(and its locator moved past the call, DX being spent), WaveRack +1, megaown
+1, Sticky Notes and Audio 0.

**V2 - the yield cell enters the yield.** `OSAPI_TASK_YIELD` is `pushf / cli /
push cs / call task_yield.save / retf / nop`: it builds the int 08h frame
under the caller's far return, so the scheduler's `iret` resumes at the
`retf`. ~95 clocks a yield PREDICTED (push ds, push cs, pop ds, call, ret, pop
ds). Same eight bytes on kern_big and kern_small; **kern_mega -8 conventional**,
because its SLOT is a per-cell `.text` stub and this cell needs none (the frame
is the near task_yield's own, which its scheduler already irets). kern_emu
keeps the plain SLOT: its `task_yield` polls the VMware pointer before the
frame. `tests/unit/t_api_abi.py` decodes the shape as `YSLOT` (the `gates`
agent owns the file; the edit is three lines and a header row). **For `t5`**:
it is a hot cell of its own shape, 8 bytes; place it with the SLOTs.

**C10 - desk item removed by record.** A package's `OSAPI_DESK_ITEM` remove
takes ES:SI = the record alone; AH's zone is retired. CTRL.DRV's `sc_m_pdel`
finds the first row with the record's volume and path (a Sticky App record:
by final component among the Sticky App rows, then the same check, as before).
Resident `.cold` -7 (kern_big/emu). CTRL.DRV's `.modc` +17. WIN16's sixteen-zone
probe loop is one call (W16PG_SC0/SCN deleted); `os88stik.inc` -1 in every
Sticky App package; tests/deskitem's remove -3.

**R12 - one display register.** `OSAPI_FSX_CAPS` takes CH = 0 and BX = the
window (0 the frontmost), or CH = n for display n-1 - `OSAPI_FSX_RUN`'s
register and numbering. The BX = 1..15 overload is retired. Kernel -5
kern_big/emu/mega, -2 kern_small, EXTD.DRV -1. Packages: `mov ch, 0` at 39
window-asking sites (+78), the display walkers (DOS's `dvw_cdetect` and its
re-test, RedLine, the Video Player's `vp_canplay`/`vp_canp1`) walk CH. The C
API (`os88_fsx_caps(win, &kind)`) is unchanged; its thunk sets CH. **A bug
found and fixed on the way**: WireFleet asked FSX_CAPS with BX = the HEIGHT
`OSAPI_WM_DISPLAY` had just answered, not its window; it now tests
WM_DISPLAY's own DL (a 1bpp display offers FSXM_CGA320 exactly when it is a
CGA, `fsx_capstab`), -5 bytes, which also put it back under APP_MAX_SIZE
(the +2 had pushed it over). OSAPI_VIDEO and OSAPI_FSX_SURF were NOT given a
CH input: neither takes a display today, and adding one is a feature that
costs kernel bytes.

## REFUSED (priced)

* **R2-4** (FSX_SPK's one open verb, the door always writing IRQ0's vector).
  The door would write IVT 08h and answer the chain on kern_big - about
  **+23 `.text` kern_big/emu** (`mov cx,[spk_seg] / push ds / xor dx,dx / mov
  ds,dx / mov [0x20],si / mov [0x22],cx / pop ds / mov cx,cs / mov dx,sch_isr`)
  and the same on kern_mega's real-mode `.rm` arm, against -7 for the PM arm's
  `cmp al,2` refusal and -4 in `rms_spk`: **kern_mega ~+12, kern_big +23**.
  Packages gain (~-40 in each `os88spk.inc` user: the AL = 2 retry and the
  IVT xchg go), but resident pays. A shared `spk_vec` helper with `spk_off`
  comes to the same bytes. Not a size win.
* **R2-5** (FSXF_REAL's answer not by rewriting `kent_di`'s frame). The
  rewrite is 15 bytes of kern_mega `.text`. Answering in SI on EVERY kern_mega
  bracket (0xFFFF when not asked) is -4, but clobbers SI for every package that
  does not ask, which FSX_RUN has always preserved; a query cell costs more
  than it saves. -4 at that price is not taken.
* **V1** (clobbering rect entries). A cycles row, not bytes: the five rect
  primitives reach their bodies through GFXCLIP / GFXDISP / VXMONO / VIDEXTF
  and (kern_big) the COLD_SLIDE `.vga` rung, and the saves are inside
  `vga_solid_rect`/`sw_rect`/`gfx_clip_run`, which kernel callers rely on.
  "A new entry beside the raw body" is a second no-save chain through each
  of those hooks - well above the list's +6, in draw-owned hot code - and its
  ~25 us a call is for C packages only (the asm half needs a lint first).
  The C SDK half has nothing to do until the entries exist (the thunks
  already treat AX..DX as volatile). Wants a khp-style agent with MartyPC
  numbers, not a fold-in.

## WANTS THE OWNER

* **R15** (a clipboard format byte in AL). It buys an extension point and
  costs bytes: **+10 kernel on every kernel** (`clip_size` becomes `cmp al,1
  / sbb ax,ax / and ax,[clip_bytes] / cmp ax,1`, +5; `clip_put` +4 `or al,al /
  jnz .toobig`; `clip_get` rides `clip_size`'s test) and **37 package sites
  plus five C thunks** that must zero AL - +2 each, ~+84. GET and SIZE clobber
  AX anyway, so their sites are safe; the 13 PUT sites preserve AL today and
  each needs reading. No second format exists or is asked for. Not taken; if
  the owner wants it, it is half an hour with the site list in this note's
  ROWS section.
* **`OSAPI_BATCH_END` withdrawn** (found). No package or C package calls it
  (the C thunk is gated on use and nothing uses it); any `gfx_unlock` ends a
  batch. Cell 6 + `dsk_batch_end_x` 12 = **-18 resident kern_big/emu/small**,
  -6 conventional kern_mega. But SPEC.md 20.3.1 rule 1 names it as the
  example of a cell that is optional BY DESIGN, so withdrawing it is the
  owner's.

## WANTS AN ABI BREAK (found, not taken)

* **`OSAPI_GFX_PIXEL` withdrawn**: 7 users (word, scribe, redline, the C
  thunk, `os88ui.inc`'s UI_PIXEL, two tests); a pixel is `OSAPI_GFX_FILL`
  with x1 = x2, y1 = y2. `gfx_pixel` stays (gfx_points calls it), so only
  the 8-byte cell goes, -8 resident (-2 under T5's rare form, but it is hot),
  +4 a site. A basic drawing verb: the owner's.
* The usage census (every published cell against its code users in `apps/`,
  `drivers/`, `tests/`, `kernel/`) is the method for the next sweep: cells
  with one user are `OSAPI_DRV_DLG` (RAMDISK), `OSAPI_PKG_REHOME`
  (os88parts), `OSAPI_WM_MINIMIZE` (ArtfulType), `OSAPI_MOUSE_FEED`
  (USBMOUSE), `OSAPI_WM_QUIET` (FTPD) - each a capability with a body, so
  none is a withdrawal by rule 1; `OSAPI_FILE_APPEND_SYS` has none
  (`memdoors`' C4').

## ROWS (the final pass's union)

V2 changes every yield, so any booting row exercises it; the ones ABOUT what
moved are:

* C10: `deskitem` (run, PASS), `stikregister`, `stikboot`, `stikmenu`,
  `stikshort`, `stikmove`, `stiknodesk`, `win16pmdde` (DeleteGroup).
* R2: `stiknote`, `stiknotevga`, `stiknotesmall`, `tmsmall`, `tmload`,
  `tmowner`, `tmmaxrun`, `tmgraph`, `megaown`, `redlineinspcga`,
  `redlineinspherc`, `redlineinspvga`, `waverackrec`; the Audio player's
  sibling check and WIN16's jump (`w16j_give`) ride the WIN16/Audio rows.
* R12: `fsxdisp`, `vidxdisp`, `isotrontwodisp`, `dosconcga`,
  `excitebikeega`, `vesafsx`, `pxsmd`, `wirefleetsystem`, `wirefleetdeploy`,
  `fsxclip`.
* V2: `stkbalance` (fast, green), `vmmouse` (kern_emu keeps the SLOT - QEMU).
* fast tier: `api-abi` (the YSLOT shape), `abilock`, `hotpar` - all green.

## For the next wave

* `t5`: V2's cell is a hot 8-byte `YSLOT`; `t_api_abi` knows it. kern_mega's
  SLOT stubs (`mega_sl_N`) cost conventional bytes per hot SLOT - V2 removed
  one; any other hot cell whose body can be entered without the kernel's DS
  would too.
* `tools/abi.lock.json` was `--update`d twice here (R2/C10, R12) without a
  bump, as the brief says; it will conflict.
* The R2 lesson: a parity shift in `.cold` moves the hot `dsk_*` routines
  (t_hotpar), so an odd delta in `instance.inc`/`memory.inc` cold bodies
  wants a byte found or a pad.
