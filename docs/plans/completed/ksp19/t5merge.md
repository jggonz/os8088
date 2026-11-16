# Kernel size pass 19 - `t5merge`: T5 merged onto the wave-C tip

Branch `ksp19-t5merge`, cut from `kernel-size-19` at `ca0a7836` (nogrow,
swwm, swdrv, swmem merged since t5's base `385d14c7`); merges `ksp19-t5`
(`cd54fdb5`).

## How it merged

The tip's sweeps moved no `OSAPI_*` address (`git diff 385d14c7 HEAD` on both
SDKs changes one comment), so no citation needed re-deriving from a token
form: the tip's only added citations were `docs/INDEX.md`'s, which is
generated. Six files conflicted:

* `apps/os88api.inc` (3): `SYS_SNAPSHOT`, `CLAIM_SNAPSHOT`, `FSX_CAPS` -
  t5's `OS88_DOOR` define over nogrow's contract text (R2 and R12 reverted,
  the snapshots' AX answers withdrawn).
* `kernel/instance.inc`, `kernel/memory.inc`: the snapshot bodies are
  nogrow's (no caller bound), with t5's "the door banks ..." wording.
* `kernel/kernel.asm`: the `fsx_caps` table cell is gone (t5); its `RDPUB`
  comment was still R12's `CH = 0` form, and the two snapshot `RDPUB`
  comments still promised the withdrawn AX answers - all three corrected to
  the tip's contracts.
* `docs/INDEX.md`, `tools/abi.lock.json`: regenerated (`os88index`,
  `abilock --update "Merge of t5 (kernel size pass 19)"`, no bump:
  PKG_FMT 13, DRV_VER 14).

Semantic: nogrow's KB arm (`MEMP_KB`/`MEMP_KBH`) is reached through the door
with no edit - `OSAPI_MEM_P` is an RCX door cell, `OS88KB_BODIES`' `call
OSAPI_MEM_P` assembles to the door, and the door banks BX before reading its
index byte, so BL arrives as the verb. swmem's non-mega HUGE fall-through
keeps its label (`mmf_osapi_mem_huge`), which is the routine rd_tab names.
`os88cells --write`: both blocks unchanged (95 + 1 door cells).

## Bytes (measured, `kernsize --json`, text+bss+cold+lowbss+vgabuf+vga+mega)

| kernel | tip `ca0a7836` | merged | delta |
|---|---:|---:|---:|
| kern_big | 90,026 | **89,674** | -352 |
| kern_small | 56,223 | **55,877** | -346 |
| kern_emu | 90,272 | **89,920** | -352 |
| kern_mega | 128,973 | **128,596** | -377 |

(Tip figures = t5's base plus the four branches' recorded deltas; the merged
figures are measured and equal tip + t5's own deltas exactly.)

`t_hotpar` green on kern_big and kern_small with no re-pad.

## The five rows that hooked a cell address (edited, NOT run - soak)

* `isotronsmall`: traces `os88_door` and keeps a hit when the byte at the int
  frame's CS:IP is `rdp_MEM_P`; the verb masks `MEMP_KB|MEMP_KBH` (isoTron's
  KB entries use the arm since nogrow).
* `os88huge` (host, Unicorn): the stub's three cells are door cells; the
  model answers `int 81h`, reads the index byte, steps IP past it and maps
  the SDK's index to its private number. It runs in seconds: PASS, 3,482
  checks.
* `deskcalremind`: TOAST is a door cell - traced at `toast_show`, the routine
  its rd_tab entry names; `TONE_CELL` (stale since t5's renumber, `0x00D3` ->
  `0x00B5`) read off the SDK. `stiksmall`'s identical stale `TONE_CELL` too.
* `megapfl --break free`: points `OSAPI_XMEM_FREE`'s rd_tab word (an RS
  entry) at a `.text` `ret` instead of poking the cell.
* `x3mega` conv leg: `OSAPI_MEM_HUGE`'s rd_tab word -> `mod_gone`;
  `OSAPI_XMEM_CAPS` is a hot XCELL whose routine is an immediate (code - the
  Bochs icache trap), so the leg points the data word one call further in,
  `[xm_svc + XMV_CAPS]`, at a `.mega` `xor ax, ax / ret`.

Not reconciled: `tests/picomem.py` still names `OSAPI_MOUSE_FEED` at
`0x041C` and `OSAPI_FILE_READ`'s old cell (`0x0109`, now a door cell) - its
Unicorn model plants code at both; it wants the door model `os88huge` now has.

## Rows

`make` (fast tier 79/79), `make small/emu/mega`, the eight C packages,
`t_api_abi --all` (3,341 checks, 77 hot + 96 door on four kernels),
`dosvec` ok, `dosdir` ok. Looked: MartyPC `os8088_5150_cga` and
`os8088_xt_vga` on the 360KB pair (Disk window, Calc, Paint, Note Pad and its
Open chooser, Desk Clock, DOS prompt); kern_mega under QEMU (qemu32, 16MB:
protected-mode desktop, Disk window, APPS, Calculator).
