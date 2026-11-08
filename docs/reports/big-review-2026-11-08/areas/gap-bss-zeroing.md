# Does .bss arrive zeroed? (follow-up to the big review)

## The question

The owner asked whether code may rely on `.bss` arriving zeroed:

> *".bss-zeroing: iirc we didn't used to, and now do, so likely that's more out
> of date comments. But read the code and find out; maybe its mixed."*

The tree disagrees with itself. Some passages say `.bss` arrives zeroed:
SPEC.md §10, §12.8.5.1 and §8's `sched_init` bullet, `kernel/events.inc`,
`clip.inc`, `ui.inc`'s `ui_tclkw` and `mod.inc`'s `mod_fpinit`. About forty
others say `-f bin` zeroes nothing, or that a `.bss` byte boots as whatever
the RAM held. Two investigators traced it independently: A started from the
code, B from the claims. This report settles the points where they disagreed,
using the code and the built binaries at `fc92a669`.

**Short answer: you remembered right.** The kernel's `.bss` did not used to be
zeroed. Since PR #61 (2e8e2924, 2026-08-08) it is zero when `kmain` first runs,
on every shipped load path. No instruction clears it. The file layout does,
and the tree's own measurements confirm it. What is genuinely mixed is the
window BEFORE `kmain`, and the sibling sections `.lowbss` and `.vgabuf`.

## How it works

`.bss` is `nobits vfollows=.text`, so NASM emits nothing for it. But the next
section in the file is placed with an explicit `start=` past the image rung,
and NASM fills the gap with zeros. The image rung is `KIMG_PARA`, which
includes `KBSS_SIZE` (kernel.asm:1679). Today that next section is:

- `.ovl` at `OVL_START = BOOT2_PAD + (FAT_SEG-KERNEL_SEG)*16` (kernel.asm:2183)
  under `ROM_COLD`, the shipped default;
- `.cold` at `COLD_START` (kernel.asm:8712) on the `NO_ROM_COLD` knob.

Either way the `.bss` range is zero padding, and every loader writes the file
over it:

| | kern_big | kern_small | kern_mega |
|---|---|---|---|
| `.bss` file range (`kernel-full.bin`) | 0xB788..0xCC7E | 0x8B53..0x96CB | 0xCFFC..0xE518 |
| non-zero bytes in that range | **0** | **0** | **0** |
| next section's file start (`.ovl`) | 0xCE00 | 0x9800 | 0xE600 |
| `kernel.sys` tail decodes back to `kernel.bin` | yes | yes | yes |

Checked against `build/*/kernel.map`, `kernel-full.bin`, `kernel.bin` and
`kernel.sys`, and decoded with `tools/os88lz.py` using the block list in
`kernel.kz.json`.

## Per path and per section

"Zero" means zero at `kmain`'s first instruction.

| path | section | zero at kmain? | by what (file:line) |
|---|---|---|---|
| floppy, KZIP (shipped: Makefile:1393-1395), all four kernels | `.bss` | **yes** | file padding before `.ovl` (kernel.asm:1679, 2183). `tools/os88kz.py:67` packs `image[blob+head:]`, padding included, and checks the round trip at build time (os88kz.py:76-79). `boot/boot2.asm` `call kz_all` (488) writes the zeros before `jmp KERNEL_SEG:0` (514) |
| floppy, `NOKZIP=1` (knob) | `.bss` | **yes** | the same padding, delivered directly by stage 2's `.load_next` read (boot2.asm:356-384, `B2_KSECS` up to `MODC_START`, boot2.asm:122) |
| hard disk (`boot/mbr.asm` → `boot/boothd.asm`), live USB/CD (SPEC.md §80: the same VBR) | `.bss` | **yes** | boothd reads the whole `KERNEL.SYS` to `KERNEL_SEG+KZ_RPARA`. `kz_hd` (boot2.asm:621-639) moves the head down and runs the same `kz_all`. boothd.asm:293 then jumps |
| ROM `.cold` (`boot/osrom.asm`, `ROM_ADOPT`) and `COLDHMA.SYS` | `.bss` | **yes** | these supply or adopt only `.cold`. `.text` and `.bss` still arrive by one of the paths above |
| warm restart (ui.inc:3177, disk.inc:10557, hbstub.inc:228, megares.inc:271, kern_mega's `mega_svc` warm reset) | `.bss` | **yes** | every restart is `int 19h` → stage 1 → full reload. The only `jmp KERNEL_SEG:0x0000` sites are boot2.asm:514, boothd.asm:293 and boot.asm:512 (`FLAT_PAYLOAD`), and nothing re-enters `kmain` without a load |
| hibernate resume (`HIBER.DRV`/`hbstub`), kern_mega park/unpark and streaming resume (`megares.inc`) | `.bss` | n/a | the saved RAM image is restored and execution resumes where it stopped. `kmain` does not run, so `.bss` holds the session's own values, as it should |
| **during the load** (stage 2, the splash's first tick, before `kz_all`) | `.bss` | **NO** | under KZIP the packed tail lands `KZ_RPARA` up (18,432 bytes on kern_big), from `KERNEL_SEG:0x5A00` to 0x17AB0, so the `.bss` range holds **packed LZ4 bytes**. On `NOKZIP` it is unlanded RAM until its sector arrives. viddet.inc:1022-1078 refuses every store past the plain head on that call, after a real defect: on the 1.44MB kern_small, the splash's `ui_post = 1` was expanded into `menu_track`'s first compare |
| floppy/HD, shipped `ROM_COLD` kernels | `.lowbss` | **partly** | zero padding from the end of the FAT window up to `.vga`/`.cold`, which start above the LOW rung (`COLD_RAM = LOW_SEG + LOW_PARA`, kernel.asm:1687-1699). **Except `dsk_secbuf`**: `.ovl` overruns the FAT window by 511 bytes on kern_big (0x13FF against 0x1200), 450 on kern_small and 510 on kern_mega, so `.lowbss` 0x000-~0x1FE holds boot-overlay code at `kmain` |
| `NO_ROM_COLD` knob | `.lowbss` | **no** | the image ends where `.ovl` ends (`MODC_START`), so `.lowbss` past the spill is never loaded |
| kern_big | `.vgabuf` | **no** (by design) | `.cfx` lands on `VGABUF_RAM = COLD_RAM + COLD_PARA` (kernel.asm:1852, 8772; `.cfx` is the file's last section, `MODC_START = CFX_START + CFX_SIZE`, kernel.asm:8980). `vga_pbuf` fills it before it is read |
| kern_mega | `.vgabuf` | **no** | above everything the load writes, so it holds whatever RAM held. Its only reader fills it first |
| kern_small | `.vgabuf` | n/a | `VGABUF_PARA` = 0 |
| on-demand modules (`kernel/mod.inc` `mod_need`) | module bss | **yes, per load** | `MODB_TAIL` (kernel.asm:9013-9030, since d323a9a6, 2026-11-02) writes each module's bss as zeros at the end of its file, and every load reads them (mod.inc:650, "NO ZEROING HERE"). Two exceptions: **`CTRL.DRV`'s core-only read** stops short of them (SPEC.md §2.8.7: `cpc_buf` is NOT zeroed), and a module that **stays loaded** keeps its values between calls |
| drivers (`kernel/driver.inc`) | driver bss | **yes** | `drv_bss` (driver.inc:3739) zeroes it past the file's end. The HMA copy is made after that |
| packages (`kernel/loader.inc`) | package bss | **yes** | `ld_start` step 7, `rep stosb` over `[ld_img, +ld_bss)` (loader.inc:1306-1311). On kern_mega it runs after `ld_hi`. The **re-home** path (`.call8`, loader.inc:1343) skips step 7 by design: the bss ships inside the part, with the loader's handoff written into its head (SPEC.md §20.12.10). C packages: the off-by-one between `cc_bss_beg` and `cc_image_end` is **already fixed** by the `align 2` in `CC_IMAGE_END` (apps/cc/crt0.asm:1920-1930) |
| `kerndos/` | `.bss`, `.lowbss` | **yes** | `kd_entry` zeroes both with `rep stosb` around the launch block (kerndos/kdentry.inc:59-84), because nothing that loads that image writes them |

### Disagreements, settled

- **NOKZIP `.bss`.** A said yes. B said "partly", in case the splash writes a
  `.bss` byte after its sector has landed. **Yes.** The splash's own state is
  either `cs:`-relative (blob) or in the plain head (`spl_live` 0x922,
  `spl_busy` 0x924, `spl_fseg` 0x669). `bvs_apply` on the first tick runs on
  the boot sector's stack, and its `cmp ax, LOW_SEG / jne .homed` refuses the
  row table and every store past the head (viddet.inc:1022-1078). The same
  guard covers KZIP, where such a store would corrupt the stream.
- **Packages.** B's "partly" rested on the re-home path and a C off-by-one.
  The re-home path is deliberate and carries its own zeros. The C off-by-one
  is the **pre-fix** bug that crt0.asm's header describes; `CC_IMAGE_END`'s
  `align 2` closed it.
- **kern_big `.vgabuf`.** Both said "no". A has the mechanism right: `.cfx`
  is inside `kernel.bin` (it ends exactly at the image's end, 0x193B2), and
  its 0x212 bytes cover the whole 0x150 of `.vgabuf`.
- **SPEC.md §10 (line 18121).** The conclusion is true. The mechanism it gives
  (".text/.cold gap", "stage 2's one contiguous read") is the
  `NO_ROM_COLD`+`NOKZIP` arm. Fixed.
- **docs/KERNEL-MEMORY.md:575.** The first clause is true. "Read before that
  point (`[fdlg_win]`)" is not: `fdlg_win` is first read on the first mouse
  press. The next paragraph ("the gap is where `.cold` lands") is the
  `NO_ROM_COLD` arm. Fixed.

## Code that relies on it

These all depend on `.bss` being zero at `kmain`, and they are correct:

- `[sch_chbusy]` (sched.inc:2163/2201/2228, fsx.inc:1386-1421). No init
  routine stores it, and sched.inc:610 reads it as a word together with
  `sch_rbusy`.
- `evq_head` / `evq_tail` / `evq_count`. `evq_init` was deleted in kernel size
  pass 3 (events.inc:62, SPEC.md §10).
- The loader's resting state. `loader_init` was deleted (kernel.asm:5904).
- `clip_kb` / `clip_bytes`: an empty clipboard, with no init entry
  (clip.inc:263, 321).
- `ui_tclkw` (ui.inc:1661, the first title-bar press) and `ui_armw`
  (ui.inc:3276).
- `mem_cp_msk` on the first compaction pass (memory.inc:4059), and `fcp_made`
  held to 0 or 1 (filecp.inc:1695).
- `dock_ck`. Its clearing loop was deleted; `dock_paint`'s full pass is a
  second backstop (dock.inc:140).
- `mod_fp` slots read 0000:0000 until `kmain_o` fills them (kernel.asm:5820).
  This is safe only because nothing calls a thunk before then.
- Package side: SPEC.md §45.13.7 `trk_txw`, `np_ffield` and `[dos_fhome]` =
  A:, all rely on `ld_start`'s zeroing.

Two stores are redundant today and harmless:

- `sched_init`'s `mov word [ticks], 0`. Nothing writes `[ticks]` between
  `kz_all` and `sched_init`.
- The `wm_init` backstop. wm.inc itself calls it a backstop.

**Test observation:** `tests/tickzero.py` was written for a hazard that cannot
occur on today's layout. It plants junk in `[ticks]` *after* the load, so it
still detects the store being deleted. Its stated premise, that dirty
power-on RAM reaches `[ticks]`, is false and is corrected below.

## History

| when | commit | what changed |
|---|---|---|
| 2026-07-24 | 4e7fba97 | `.bss` is `nobits` at the very end of the file. Stage 1 reads `KERNEL_SECTORS`, the file size, so `.bss` was **whatever RAM held** |
| 2026-07-29 | f7a3b297 | `.fartext follows=.text` in the file while `.bss` `vfollows=.text` in memory, so `.bss` received `.fartext`'s bytes |
| 2026-08-04 | 3a38877d | `.fartext` gone. `.bss` is the file's tail again, still not zeroed. The `dskw_isdir` defect (SPEC.md §18.4) was found here |
| **2026-08-08** | **2e8e2924 (PR #61)** | `section .cold start=COLD_START`, `COLD_START = roundup512(text+bss)`. **From here `.bss` is zero padding and every loader lands it.** The same commit moved `dsk_fatwc` and `ui_tm_drv` to `.text` "because -f bin zeroes no .bss" |
| 2026-08-14 | ea48c34a | first "`.bss` arrives zeroed" wording. Size passes then delete `evq_init` (b9bb0407, 2026-09-04), `loader_init` and `dock_ck`'s clear on it |
| 2026-09-08 | 2ce1e37b | KZIP becomes the default. The padding is now packed, and `kz_all` writes the zeros |
| 2026-10-19 | 571befce (PR #268) | ROM_COLD becomes the default. `.cold`/`.vga` move above LOW, so the gap now ends at `.ovl` (FAT_SEG), and `.lowbss` becomes padding too, apart from the `.ovl` spill |
| 2026-11-02 | d323a9a6 | `MODB_TAIL`: module bss is zeros in each module file |
| 2026-11-08 | 14428309 | §8's "No instruction clears .bss at boot", the exact statement |

After 2026-08-08 the "-f bin zeroes nothing" reasoning kept being copied into
new code: `menu_dropd` (ea48c34a), SPEC.md §2.9.5.4's `drv_cfg` note (b47de145,
2026-10-14) and SPEC.md's package-bss line (073c7ecf, 2026-10-22).

## Verdict

**The behaviour is uniform; the wording is mixed.**

- **Kernel `.bss`: zero at `kmain` on every shipped load path.** That covers
  kern_big, kern_small, kern_mega and kern_emu; KZIP and NOKZIP; floppy, hard
  disk and live media; ROM- or HMA-adopted `.cold`; and every warm restart.
- **Before `kmain`, `.bss` is not zero.** Under KZIP it holds the packed
  stream, so anything stage 2 or the splash writes or reads during the load
  must live in the plain head of `.text`: KERNEL_SEG:0 up to `KZ_HEADSEC*512`,
  which is 4,608 bytes.
- **`.lowbss`: mixed.** It is zero on shipped kernels except `dsk_secbuf`
  (the `.ovl` spill). On `NO_ROM_COLD` it is not loaded at all. Nothing
  promises it.
- **`.vgabuf`: not zero** (`.cfx` bytes on kern_big, stale RAM on kern_mega).
- **Sibling bss:** each has its own mechanism, all listed in the table.

## What code may assume

1. **Kernel `.bss` reads 0 from `kmain` on, on every kernel, the first time.**
   A zero resting value may live in `.bss` with no init. A non-zero resting
   value (0xFF "none", 0xFFFF, a pointer, a count of 1) must be in `.text` or
   stored by an init routine, which is `t_bsssentinel`'s rule.
2. **Nothing before `kmain` may read or write `.bss`.** Stage-2 and splash
   state goes in the plain head (`spl_fp`, `vid_rowmax`, `vid_w`, ...). The
   real reason `spl_fseg` must be in `.text` is that stage 2 stores it
   *before* `kz_all`.
3. **Do not assume `.lowbss` is zero.** Store what you need, as kmain does for
   `mega_pm`.
4. **Module bss is zero on each fresh load**, not on `CTRL.DRV`'s core-only
   read, and not for a module still resident from an earlier call.
5. **"Garbage on a warm restart" is never true** of kernel `.bss`: every
   restart is a full reload.

The remaining "`-f bin` zeroes nothing" lines are **stale but harmless**.
About 30 such rationales stay unfixed after the fixes below: disk.inc
429/526/943/6887/6894/6935, wm.inc 8712/8794, vga12.inc 46/596/1034,
vidext.inc 114, xmem.inc 107, diskw.inc 194, assoc.inc 21/2463, drvvol.inc
109, softgfx.inc 876, driver.inc 1163, fprog.inc 1326, clock.inc 2142,
mouse.inc 3607, desk.inc 2321, files.inc 2284, ui.inc 2659/3011, and SPEC.md
7444, 11819, 15553, 37333, 43456, 58360, 75201, 78164, 102506, 108482, 109356,
111234, 111416, 112398.

They are literally true: NASM does emit nothing *for* `.bss`. They only
justify putting a default in `.text`, which costs the same bytes, because
`KIMG_PARA` rounds `.text` and `.bss` together, and many of those defaults are
non-zero anyway. Fixing each one is a one-word sweep with no behaviour
attached. The 65 fixes below are the statements that are **false** ("not
zeroed", "garbage", "previous session's bytes", "no image bytes") or that
**name one arm's mechanism as the rule**.

## Recommendation: a gate

**Today nothing gates it.** `t_bsssentinel` (soak tier) checks only the
non-zero sentinels. A layout change that left `.bss` as the file's tail, for
example a new knob that drops `.ovl`'s `start=`, would silently bring back
the pre-#61 behaviour. Every reliance listed above would then break on real
hardware only, because emulators power on with zeroed RAM.

**Proposed: `tests/unit/t_bsszero.py`, fast tier.** For each built kernel
(`build/`, `build/smallk/`, `build/megak/`, and any `$(BUILD)` that has a
`kernel.map`):

1. Parse the map's section summary for `.text` (file start) and `.bss`
   (vstart, length).
2. Assert that `kernel.bin` covers `[text_file + bss_vstart, + bss_len)`
   (the file runs past `.bss`), and that every byte in that range is 0.
3. Leave the packed image alone. `os88kz.py` already refuses to write a
   `kernel.sys` whose tail does not round-trip (os88kz.py:76-79), so a zero
   `kernel.bin` range is a zero decoded range.

The test should be broken on purpose once: build with `.ovl`'s `start=`
removed in a private tree, and watch it go red.

**Cost:** 0 resident bytes, 0 image bytes, about 40 lines of host Python, and
roughly 50 ms per `make` (three maps, three binaries). The suite row's `secs`
should be measured when the row is registered. This is the build-time guard
the owner prefers to resident bytes, and it protects real user-visible
failures: the `ui_post`, `fsx_cur` and `dskw_isdir` classes.

An assembly-time `%if` cannot do the same job, because NASM cannot name "the
next section after `.bss`" generically. A `%if OVL_START < BOOT2_PAD +
KTEXT_SIZE + KBSS_SIZE` would cover one arm and miss the next. The host check
reads what NASM actually placed.

A second, optional row: assert that no symbol stage 2 writes before `kz_all`
lies at or past `KZ_HEADSEC*512`. `spl_fseg` and offsets 0x0004 and 0x000C
are the ones today. That would turn viddet.inc's SS-guard lesson into a gate.

## Doc fixes

The 65 exact fixes are in `../data/gap-bss-zeroing.json`. Each was checked
unique with `count == 1`, applied in sequence to a copy of the tree, and
confirmed to change no non-comment byte in any `.asm` or `.inc`.
