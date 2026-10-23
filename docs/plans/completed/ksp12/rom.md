# ksp12 agent "rom" - the ROM arm, SPEC.md 2.10

Kernel size pass 12. Concept: `ae906414` ("ROM: `.cold` in a 5150's U28-U32
sockets, waves 0-4", SPEC.md 2.10, docs/plans/ROM-PLAN.md), which had no
size pass. Branch `ksp12-rom`, cut from `kernel-size-12` at `df8603a1`.
Every figure is assembled bytes, `tools/kernsize.py --json`, against the
base's absolutes (not kernsize's printed deltas, which are against the
blessed baseline at `b6acc4f7`).

**What the concept cost**, measured by building `ae906414^` and `ae906414`
from `git archive` trees: **+67 resident on kern_big** (`.text` +1, `.cold`
+66) and **+110 on kern_small** (`.text` +36, `.cold` +74), which is what
SPEC.md 2.10.4 says. The pass target was 30% of that, 20 and 33 bytes.
**Taken: 46 and 65, 69% and 59%.**

## BASE AND TIP

| | base `df8603a1` | tip | delta |
|---|---:|---:|---:|
| kern_big `.text` / `.cold` | 43,738 / 38,690 | 43,740 / 38,642 | +2 / **-48** |
| kern_big `.bss` / `.lowbss` / `.vgabuf` | 5,212 / 5,598 / 336 | same | 0 |
| **kern_big resident** | **93,574** | **93,528** | **-46** |
| kern_small `.text` / `.cold` | 31,967 / 22,515 | 31,947 / 22,470 | -20 / -45 |
| **kern_small resident** | **60,413** | **60,348** | **-65** |
| kern_emu `.text` / `.cold` | 43,997 / 38,690 | 43,999 / 38,642 | +2 / -48 |
| **kern_emu resident** | **93,833** | **93,787** | **-46** |
| `.boot2` / `.ovl` (all three) | | | 0 / 0 |
| `KERN_SIZE` big / small / emu | 99,328 / 62,464 / 99,840 | same | no rung moved (cold rung on big: 222 -> 270 left) |
| modules, drivers | | | 0 (no module or driver source touched) |

**The ROM arm** (`make rom`, SPEC.md 2.10.3) and **the HMA arm**
(`COLDHMA.SYS`, 2.10.7):

| | base | tip | delta |
|---|---:|---:|---:|
| kern_big ROM: window spare | 778 | **868** | **+90** (`.cold` -48, tail 1,476 -> 1,434 = -42) |
| kern_small ROM: window spare | 4,061 | **4,148** | **+87** (`.cold` -45, tail 1,137 -> 1,095 = -42); it still holds FDLG, FILECP, CTRL, FORMAT and the Task Manager |
| `COLDHMA.SYS` file | 2,920 | 2,889 | -31 |
| ...of which stays in the HMA | 2,056 | **2,025** | **-31** (rom_modfix and rp_list are in the half that stays) |
| ...spare over it | 200 | **279** | +79 (with `.cold`'s -48) |

The kern_big ROM's 868 is what the next `.cold` growth has before
`tools/os88rom.py` refuses the image (SPEC.md 2.10.6).

## TAKEN

Six commits. Per item, big / small resident:

1. **`rom_mfp`'s seed is a door: `rom_none`** (`cmp al, ROMF_FIX` / `retf`,
   3 bytes of `.text` beside `api_far`, which keeps the ROM's `.text` hash at
   one span there). It refuses SIZE, COPY and PKG and passes FIX - what a ROM
   holding nothing would answer for a kernel with nothing to re-point - so
   `mod_need` (twice) and `ui_sys_open` no longer test `[api_coldseg]` before
   asking: three 8-byte tests for a 3-byte door. `ROMF_FIX` moved to 3 and
   `ROMF_PKG` to 2 so that one compare is the whole stub (both files'
   equates - and `tests/unit/t_mirror.py` does not read osrom.asm, so
   `os88rom.py --selfcheck`, the fast tier's `romtool`, now compares the two
   sets; checked to fire with osrom.asm's FIX put back to 2).
2. **COPY refuses with every register kept**, so `mod_need` asks COPY
   unconditionally ahead of the disk read and the `SI = 0` mark that carried
   SIZE's answer down to it is gone (`xor si, si`, `or si, si / jnz`, the
   `jmp short`). With 1: **mod_need -22 on both.**
3. **The heap floor reads one word.** `mem_floor_ax` was HEAP_SEG, the VGA
   arm, then `cmp [api_coldseg] / je / sub COLD_PARA`. It is now
   `mov ax, [rom_vgabw]` plus the VGA arm turned round (`jz` / `add`):
   `[rom_vgabw]` names the `.vgabuf` rung, so `rp_walk` moves it from
   `VGABUF_RAM` to `COLD_RAM` in the same walk that re-points
   `[api_coldseg]`, and those two differ by exactly `COLD_PARA`. On kern_big
   the word is the planar decoder's own `mov ax, VGABUF_SEG` immediate,
   labelled `vga_pr_bufw` in vga12.inc (no byte); a kernel with no decoder
   has a 2-byte `rom_vgabw0` that `tools/os88rom.py` finds as a `.vgabuf`
   site by itself (kern_small's ROM now lists "1 `.vgabuf`"). **-10 big, -8
   small.** If the label ever pointed at the wrong two bytes the floor would
   be wrong on EVERY machine, ROM or not, so it cannot fail quietly.
4. **The byte compare.** `osapi_sys_kb`'s test asks `[api_coldseg + 1]`
   against `COLD_HOME_HI`: the high byte alone tells RAM, ROM and HMA apart
   (hiber.inc already stores it for that reason), and kernel.asm now asserts
   that at assembly time - checked to fire. -1 each.
5. **`kmain` retires `[spl_fseg]` through AX** (dead there): -1 each.
6. **FIX takes the row and BP** as `mod_need` holds them; the ROM loads the
   claim out of `[bx]` (MODR_SEG = 0) and the limit from BP. -4 each.
7. **PKG launches the package itself**: `rom_pkg` ends in `jmp near
   ldf_ld_pkg_start` - kern_small's `.cold` is the ROM's own segment
   (asserted) - so `ui_sys_open` loses its far call and a branch, and the
   ROM's `.text` site list loses that call's segment word. Then **PKG reads
   `[ui_desc]`'s name itself** (RT_UIDESC; ui.inc asserts UO_FILE = 0).
   `tools/os88rom.py` emits RT_LDFPKG and RT_UIDESC. **-14 small.**
8. **`osapi_sys_kb` subtracts once.** SK_IMG is SK_KERN less rows that are
   constants everywhere, so both subtractions are made on AX and the second
   word is `AX - (KERN_KB - SK_IMG_KB)`; BX's copies and the `xchg` go.
   **-7 big, -4 small.** (This takes the VGA arm's second `sub` too: it is a
   pre-existing line in a routine this concept changed, and nobody else's.)

ROM side (not resident on any kernel; the window and the HMA):

9. **`rp_list` is one body for both passes**: BX is the value each word
   gets - RT_COLDSEG to look, which hands every checked word back what it
   holds, ROM_COLDSEG to write - so the per-word branch is gone; `cs lodsw`
   reads the list and the limit is precomputed less one. Lists are distinct
   offsets by construction (os88rom.py walks the diff two bytes at a time),
   so the write pass's compare cannot fail.
10. **`rom_modfix` banks per function** and shares one exit, and no longer
    re-checks a row the adopted kernel's `mod_row` produced. With 9: tail
    -34, HMA-resident -35.
11. **`rom_putc` is the socket check's**: a kernel ROM carried it
    unreached. -13 of the window.

**Cycles.** No hot path is touched. A kernel whose `.cold` stayed in RAM now
makes up to three far calls into `rom_none` per module load (~3 x 60
cycles) and one per Task Manager open, against a load that is one or more
`int 13h` at ~400 ms each.

## EQUIVALENCE

* `rom_none` against the old tests: `CF = AL < 3` is exactly "refuse" for
  SIZE/COPY/PKG and "done" for FIX; every register is kept. At each site the
  registers the new call clobbers (AL; ES and CX at FIX; BX/SI at the PKG
  arm) were already dead - read off mod_check's documented clobbers,
  `kretc_es` and `kent_bp`.
* The floor: `[rom_vgabw] + (VGA ? VGABUF_PARA : 0)` against `HEAP_SEG -
  (mono ? VGABUF_PARA : 0) - (adopted ? COLD_PARA : 0)`, with HEAP_SEG =
  COLD_RAM + COLD_PARA + VGABUF_PARA under ROM_COLD - equal in all four
  cases. `[rom_vgabw]` and `[api_coldseg]` are written in the same rp_walk
  write pass, which cannot stop between them, and nothing else writes
  either (grep).
* `osapi_sys_kb`: the same two subtrahends came off both words, so the
  second word is the first less a constant - an identity, not a model.
* Rows: romsmall, rombig and hmacold read the floor, `[api_coldseg]`,
  `rom_mfp` and a module's re-pointed words off the machine with and without
  the ROM; romnodisk opens the Task Manager out of the ROM.
  **Broken on purpose** (rom_pkg reading the descriptor's second word),
  romnodisk went red on exactly *"the Task Manager opened, TASKMGR.O88 out
  of the ROM"*, and green again restored.
* `tools/stkbalance.py` over every file touched: 5 unbalanced paths at base
  and at tip, the same five (`kretfc_*`).

## REFUSED

* **The ES repair into the ROM** (`push ds / pop es` after a ROM launch):
  -2 on kern_small, but `rom_pkg` would have to `call` the far entry rather
  than jump to it, +4 bytes of UI-task stack on that path and +4 of ROM.
* **`osapi_sys_kb`'s cold arm off `[rom_vgabw]`**: the KB figure needs the
  paragraph delta shifted by 6, 16 bytes against the 10 of the byte compare.
* **Patching immediates instead of reading a word** (the floor's HEAP_SEG,
  osapi_sys_kb's KERN_KB): both routines are `.cold`, and on a ROM machine
  `.cold` runs FROM the ROM, which cannot be written.
* **A shared `call far [rom_mfp]` helper in mod_need**: 5 bytes to save 3.
* **`ui_sys_open`'s first `jmp .out`** stays near: 138 bytes to `.out`
  after this pass, and no ordering of the ROM arm gets it under 127.
* **The doorbell** (29 bytes of the blob, transient): both its compares stay;
  without the `'OS'` one, a foreign option ROM at F4000 whose +8 word read
  `02 02` would be far-called at its +10.
* **The `rt_spans` table** (73 spans, 292 bytes of the installer half on
  kern_big): a delta encoding would be roughly half, but it is ROM-only,
  run once, and adds a decoder - a candidate if the window ever binds.

## WANTS AN ABI BREAK

None. Every interface this pass changed is the ROM's own and none is on
`main` (`git show origin/main:kernel/kernel.asm` names no `ROMF_*` or
`rom_mfp`, and `boot/osrom.asm` is not there): `ROMF_FIX`/`ROMF_PKG`'s
numbers, FIX's registers (BX, BP; ES clobbered), PKG's (no SI in; it
launches), COPY's refusal keeping every register, and `rp_list`'s BX.

## CROSS-FILE

* **`osapi_sys_kb`'s VGA arm subtracts nothing on kern_big at this tip**:
  `SK_VGAB_KB` is 0 (the 512-byte `.vgabuf` rung rounds to no KB on the
  cumulative boundary), so `test` / `jnz` / `sub ax, 0` is 10 bytes that do
  nothing. It cannot be `%if`'d where it stands - SK_VGAB_KB is defined at
  the foot of kernel.asm, after memory.inc - and it is not this concept's;
  a guard that moves the arm, or the constant, is the fix. ~10 bytes, big.
* `mem_floor_ax`'s VGA arm was turned round (`jnz`/`sub` -> `jz`/`add`)
  as part of item 3 - pre-existing lines, recorded here because they are not
  `ae906414`'s.
* `vga12.inc` gained one label line (`vga_pr_bufw equ $ - 2`), no byte.
* `docs/plans/completed/ksp12/BRIEF.md` and `docs/INDEX.md`: the brief's two
  *"P12 handoff section-sign 1.2"* failed `checkdocs` (read as a SPEC.md citation) and the index
  was stale, so the base did not `make`. Fixed in the first commit on this
  branch; every agent will have hit it, so expect the same edit from others.
* None of the HMA hunks (`6e5d2a1c 12f48233 2d5dbdf6 9690c3bb`) was edited:
  in osrom.asm their `%if ROM_KIND == ROM_KIND_HMA` blocks are untouched,
  including rom_modfix's `jae rom_hma` head and rom_patch's slot loop, which
  run with BX now holding ROM_COLDSEG rather than 1 (neither reads BX).

## ROWS RUN

Each batch: `make -j2`, `make -j2 small`, `make -j2 emu`, `make -j2 rom`,
`python3 tools/os88test.py fast` (45/45 every time, `romtool` among them).

| rows | result |
|---|---|
| romsmall, rombig, romnodisk, romsock, hmacold, coldpic, small128, kernresident, heapmap (batch 1) | ok |
| hmadrv (batch 1) | FAIL in the shared run (no Disk window for C: within the wait), **ok alone** at 157.6s - it ran beside hmacold, both QEMU, which is contention, not the change |
| romsmall, rombig, romnodisk, hmacold, coldpic (batch 2) | ok |
| romsmall, romnodisk (batch 4), and romnodisk broken on purpose | ok; red as above |
| romsmall, rombig, romnodisk, hmacold, coldpic, hmadrv (batch 5, the ROM side) | ok |
| romsock, romsmall (batch 6) | ok |
| cpup, cpnames, fdlgsmall (a module off the disk with `.cold` in RAM, both kernels) | ok |

## DEFECTS FOUND

None in the concept. One measurement trap worth keeping: `tools/os88rom.py`
reads data labels off the SOURCE with both `%if` arms, so a name defined as
an `equ` alias in one arm and as `name: dw` in the other is treated as data
in BOTH, and kern_big's ROM hash quietly split a span (+4 bytes of tail)
until the `dw` arm took a name of its own (`rom_vgabw0`).
