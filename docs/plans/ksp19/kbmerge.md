# Kernel size pass 19 - `kbmerge`: kbpara merged onto the T5 tip

Branch `ksp19-kbmerge`, cut from `kernel-size-19` at `a17a8c3f` (T5 merged);
merges `ksp19-kbpara` (`1d51b331`, based on `f49e64a3`, before T5 and the
sweeps).

## How it merged

Five files conflicted; nothing kbpara added cites a cell address, so no
citation needed re-deriving.

* `apps/os88api.inc` (3): the `OS88KB_BODIES`/`MEMKB_*` block goes
  (kbpara), `OSAPI_MEM_FREE`/`_MEM_COMPACT`/`_MEM_P` keep T5's `OS88_DOOR`
  spelling with kbpara's contract text.
* `kernel/memory.inc`: `osapi_mem_p`'s banner says "a door cell" with
  kbpara's wording.
* `tests/isotronsmall.py`: T5's door trace (`memp_door`, the index byte at
  the int frame's CS:IP) kept; the verb mask is now `MEMP_CONV` (80h, a verb
  bit) where it was `MEMP_KB|MEMP_KBH`, and a claim is `v < 4`.
* `docs/INDEX.md`, `tools/abi.lock.json`: regenerated (`os88index`,
  `abilock --update "Merge of kbpara (kernel size pass 19)"`, no bump: PKG_FMT
  13, DRV_VER 14). `os88cells --write`: unchanged (95 + 1 door cells).

Semantic, beyond the conflicts: `kernel.asm`'s `RDPUB ... MEM_P` comment and
SPEC.md 50.3.7's signature line still promised the granule / named `0x045B`
(corrected); `tests/os88huge.py`'s model answered MEMP_AVAIL with CX = the
granule (now keeps CX, the new contract); `tests/picomem.py` reads
`OSAPI_MOUSE_FEED` off the SDK (a hot cell, `0x01FF`) and answers
`OSAPI_FILE_READ` as a door call (`int 81h` + index, modelled as
`os88huge` does) instead of planting code at its old cell `0x0109`.

## Bytes (measured, `kernsize --json`, text+bss+cold+lowbss+vgabuf+vga+mega)

| kernel | tip `a17a8c3f` | merged | delta |
|---|---:|---:|---:|
| kern_big | 89,674 | **89,624** | -50 (`.cold`) |
| kern_small | 55,877 | **55,827** | -50 |
| kern_emu | 89,920 | **89,870** | -50 |
| kern_mega | 128,596 | **128,545** | -51 (`.cold`, above 1MB) |

Packages (every `.o88`/`.drv`/module/C image unwrapped, parts included,
kbpara's `pkgsum`): **170 images 6,801,582 -> 6,801,338, -244**; the 98
top-level images -57 (Paint -50, Apple ][ -21, C64 -19, Tracker -15 ...;
RealDMG +44, SysCheck +20, PiXEL +19, Arcade +18). kbpara predicted about
-200 to -250 under T5. `t_hotpar` green on both kernels, no re-pad.

## Rows

`make` (fast tier 78 passed, 1 skipped), `make small/emu/mega`, the eight C
packages, `t_api_abi --all` (3,341 checks); soak `memp`, `isotronsmall`,
`os88huge` ok. `picomem`: every leg ok but `sb`, whose `sbslice.inc` fails
to assemble (`SDG_SET`) at the tip `a17a8c3f` too - not this merge's.
Looked: MartyPC `os8088_xt_vga`, 360KB system disk + a scratch B: (Paint,
ArtfulType and the Audio Player open together; `VECS.COM` under DOS printed
IBM DOS 3.30's answers).
