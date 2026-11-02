# msp1 agent "shared" - kern_mega outside `mega.inc`

kern_mega size pass 1 (`docs/plans/completed/msp1/BRIEF.md`). Scope (BRIEF
0.2, row `shared`): everything kern_mega adds outside `kernel/mega.inc` -
`megados.inc`, `megapark.inc`, `megares.inc`, every `%ifdef KERN_MEGA` hunk in
the kern_big files, `kernel/kernel.asm`, the `?macro` growth and `.lowbss`.
Branch `msp1-shared`, base `4fb1e6d5`. Every figure is ASSEMBLED, read off
`tools/kernsize.py --build build/megak --json` and the two maps; the
conventional-resident figure is `.text` + `.bss` + `.lowbss` + `.vgabuf`
(BRIEF 0.1: what stays below 1MB once `.cold` and `.mega` are above it).

## BASE AND TIP

| kern_mega | base `4fb1e6d5` | tip | delta |
|---|---:|---:|---:|
| `.text` | 47,548 | 47,458 | **-90** |
| `.bss` | 5,435 | 5,435 | 0 |
| `.lowbss` | 7,679 | 6,655 | **-1,024** |
| `.vgabuf` | 336 | 336 | 0 |
| **conventional resident** | **60,998** | **59,884** | **-1,114** |
| `.cold` (above 1MB) | 44,957 | 45,023 | +66 |
| `.mega` (above 1MB) + scratch | 18,237 + 1,493 | 18,297 + 1,497 | +60 (+4) |
| `.boot2` / `.ovl` | 6,620 / 5,109 | 6,620 / 5,109 | 0 / 0 |
| **`KERN_SIZE` at boot** | **131,584** | **130,560** | **-1,024** |
| `.text`+`.bss` in the 64KB window | 52,983 (12,553 left) | 52,893 (12,643 left) | -90; `tests/megaseg.txt` 4,417 -> 4,327 |
| `DISKOPS.DRV` image (`.modl`, unpacked) | 12,056 | 11,647 | **-409** (claim 12 KB either way) |

**kern_big: BYTE-IDENTICAL** at every commit (`kernel-full.bin` re-assembled
at the base's build number and `cmp`ed, `os88cfx` stamped). kern_small and
kern_emu assemble none of the changed lines (every one is inside
`%ifdef KERN_MEGA`, or `dskwin.inc`'s new arm ahead of `KERN_KNOB`).
The hot routines on kern_mega (t_hotpar's kern_big list plus `mega_t08`,
`mega_k09`, `mega_i16`, `mega_gpfix`, `mega_gpw`, `mega_null0`,
`mega_lincopy`, `mega_svc`, `mega_fd13`) are at an EVEN shift at the tip:
`.text` ones -48, `.cold` ones +70, `.mega` ones 0.

## TAKEN

1. **No knob pad in kern_mega's `.lowbss`: -1,024 `.lowbss`, `KERN_SIZE`
   -1,024.** (`kernel/dskwin.inc`) kern_mega is a `KERN_KNOB` build by
   construction (the Makefile does not exempt it from guard 1), and that
   alone bought it `DSK_OVLPAD`'s 1,024 bytes - conventional memory for the
   whole session - for a boot overlay that does not need them: its `.ovl`
   is 5,109 and rounds to the 5,120 the unpadded window holds. Every kernel
   arm of `tests/unit/t_buildmatrix.py` was re-assembled with `KERN_MEGA=1`
   to find who does need it: BOOTMARK (5,272), MOUDIAG/MOUROUND (5,182),
   SNDSNIFF=sb (5,205), FDDABSENT=1/2 (5,146) and FONT= (a baked face); the
   pad stays 1,024 when one of their defines (`BOOT_MARK`, `MOU_DIAG`,
   `SND_SNIFF_SB`, `FDD_FORCE_ABSENT`, `BAKED_FONT`) is set and is 0
   otherwise - so MEGATRAP, MEGADIAG, MEGAFDMASK, PICLOSE=1/2 and FONTSLOW
   (`megafont`'s tree) lose it too, all re-assembled. The decision is made
   from defines nasm sees, so `os88sym`'s re-assembly (which adds
   `KERN_KNOB` itself) and `kernsize`'s agree with `make` with no tool
   change. A future knob that grows `.ovl` fails kernel.asm's `.ovl` guard,
   which now names the list on kern_mega. Functionality: none - the pad was
   never addressed, only reserved.
2. **KSEG's near helpers in DISKOPS.DRV: -409 bytes of the kern_mega image.**
   (`kernel/kernel.asm`) 50 of the tree's 54 KSEG sites are in `.modl`
   (`compress.inc` 35, `diskops.inc` 15) and each was 16-18 bytes inline
   (`push ax/push di/mov ax,src/mov di,slot/call far mega_kaimf/mov
   sreg,ax/pop di/pop ax`). In `.modl` the macro is now `push ax / mov ax,
   src / call ksg_<sreg>_<slot> / pop ax` (the `mov` dropped when src is
   AX), and nine 6-byte rows plus two tails sit before `modl_end`, built
   for exactly the nine (sreg, slot) pairs the image uses - a new pair is
   an undefined label at assembly. Same AX in, same selector out of the
   same slot, DI and FLAGS kept (mega_kaimf keeps FLAGS); +2 bytes of stack
   (the near return) at each site, and a near call/ret more per site - every
   site sets up a window per chunk or per job, none is per byte. The claim
   stays 12 KB; the packed file shrinks with it. `stkbalance` needed the
   two tails as local labels (`.go`) so every row walks balanced.
3. **The speaker door's IRQ0 gate writes into `.mega`: -27 `.text`, +35
   `.mega`.** (`kernel/snd.inc`) `osapi_fsx_spk`'s protected-mode open wrote
   gate 20h inline (21 bytes) and `spk_off` wrote it back (23); both now far-
   call `spk_gate0f` (AX:SI) / `spk_gate0kf` (sch_isr) with every register
   kept. Both sites are already protected-mode-only arms under `[ss:mega_pm]`,
   IF = 0. `spk_off`'s depth inside the arm grows 2 -> 14 bytes (the far
   frame, two saves, a near call), on a slow path.
4. **`osapi_snd_stream`'s ring test into `.mega`: -15 `.text`, +25 `.mega`.**
   (`kernel/snd.inc`) The base-above-1MB test after the `[mega_on]` check is
   `snd_ringhif`: CF = 1 and AX = 2 at or above 1MB, else CF = 0 with AX and
   DX kept - `cmp dx, 10h / cmc` is the base's `cmp dx, 0Fh / jbe` with the
   sense the caller wants.
5. **Three API bodies into `.cold` behind 6-byte doors: -35 `.text`, +54
   `.cold`.** (`kernel/kernel.asm`) `osapi_xmem_sel` takes
   `osapi_xmem_run`'s shape (`push bx / call xm_ownbl / call far / pop bx /
   ret`, the `[mega_on]` test and refusal in `osapi_xmem_selc`) - xm_ownbl
   is a pure query, so it may now run in real mode before the refusal;
   `osapi_xmem_walk` and `osapi_mem_codesel` become `call COLD_SEG:...c /
   ret`. The refusal keeps `mov ax,1 / cmp al,2` rather than `stc`: the byte
   is what keeps the `.cold` hot routines after it even.
6. **kmain's tail after `mega_enter`: -15 `.text`, +12 `.cold`.**
   (`kernel/kernel.asm`, `kernel/desksc.inc`) the four far calls
   (`drv_hiboot_f`, `mem_hicaches_f`, `stik_bootf`, `as_bootc`) are one,
   `kmain_hic` in `.cold`, which calls `stik_boot` near and tail-jumps to
   `as_bootc` (whose `retf` returns to kmain). `stik_bootf`'s 4-byte door
   is deleted. Same calls, same order, boot-only.

## REFUSED

* **`menu_sseg` as a `mem_rr_tab` row on kern_mega** (menu_reloc's 11-byte
  proc against a 4-byte row, -7 `.text`). kern_mega keeps the four kernel
  words off the table because `[ico_seg]` and `[dsk_rah_seg]` can hold a
  selector; whether `menu_sseg` ever can was not provable in the time, and
  a wrong row is a silent wrong patch on a compaction.
* **kern_mega dropping `KERN_KNOB` altogether** (it would take the pad, the
  extra blob sector and `HMA_COLD_AT`'s knob arm in one go). It changes
  what the variant IS - `kernsize`, `os88sym`, guard 1 - and is the owner's
  call; item 1 takes the only resident part of it.

## DEFERRED

Estimates are labelled; nothing here was assembled unless it says so.

1. **CSDO in the on-demand modules: ~-750 bytes of module images (estimate).**
   405 sites (`compress.inc` 125, `stikmenu.inc` 105, `ascw.inc` 51,
   `hiber.inc` 42, `serial.inc`/`shutdown.inc` 16 each, ...) each carry
   `push ds / call KERNEL_SEG:mega_dsimg / ... / pop ds`, +7 bytes on
   kern_mega. A module-local `call near` door (`call KERNEL_SEG:mega_dsimg /
   ret`, 6 bytes a module) makes it +5, -2 a site. Not taken: several sites
   are inside per-byte loops in the compressor, where the extra near
   call/ret is a cycle cost - those want their DS loaded ONCE per routine
   instead, which is the larger and better rewrite (est. -1,500+ in
   `compress.inc` alone).
2. **The four remaining KSEG sites** (`driver.inc` 3 in `.cold`, `hiber.inc`
   1 in HIBER.DRV): the same helper shape per section, est. -15 `.cold`,
   ~0 net in HIBER.DRV (one site does not pay for a helper).
3. **`drv_fs_hi`'s state in `.bss`: -24 conventional `.bss` (estimate).**
   (`driver.inc`) ten words of `dfh_*` live for one call; a BP frame on the
   caller's stack would hold them, and the code likely shrinks too
   (`[bp+x]` is a byte shorter than `[abs16]`). Wants a stack-depth reading
   on the UI task (it runs only there, SPEC.md 20.6 rule 7).
4. **`megados.inc`'s `.text` data: ~170 bytes of cells and tables** written
   only by `.mega` code and `hiber.inc` (`mega_dpbase` ... `mega_hbf`,
   `mega_dsave`, `mega_dname`, `mega_dpchk`). They could be `.mega`'s PSCR
   scratch tail or a `.mega` data alias, but HIBER.DRV names eight of them
   through DS, so every access learns a segment - a module/kernel interface
   change, both halves. Conventional `.text` -170 (estimate).
5. **`mega_dropr`'s ICW sequence: ~-13 `.text` (estimate).** It programs the
   master 8259 to base 08h exactly as `mega_pic_pm` (mega.inc, pmcore's)
   does to MEGA_PIC_BASE; with the base in AH, one body serves both. See
   CROSS-FILE.
6. **The `Autostart skipped` toast built by nine `mov ax,imm/push ax`** in
   `as_bootc` (36 `.cold` bytes): a `.text` string would be 18 conventional
   bytes - the wrong trade on kern_mega, where `.cold` is above 1MB. Left.
7. **`font_rn_lem`/`font_rn_sendm` (18 `.lowbss`) and the 16-byte copy into
   `.bss` in `font_run_x` (+19 `.text`)**: `fnt_rn_edge` is handed a pointer,
   so on kern_mega it could read the `.lowbss` pair through SS and skip the
   copy. HOT (font_run); needs the cycle count both ways, est. -19 `.text`.
8. **More protected-mode-only arms in `.text` that would go to `.mega` the
   way items 3-4 did** (est. 5-15 bytes each): `osapi_snd_stream`'s
   remaining gate, `wm.inc`'s `wm_pkgcall` MEGA_PCALL frame (+22, hot-ish:
   every package callback - leave), `sched.inc`'s `sch_rhook` (+22, task
   switch - leave), `menu.inc`'s `menu_reloc` (see REFUSED).
9. **`DIRW1=1` does not assemble with `KERN_MEGA=1`** (`disk.inc:6995`, a
   short jump out of range) - found by the knob survey for item 1. Not in
   the build matrix, so nothing has asked; a knob combination rather than
   a shipped defect.
10. **`mem_hicaches_f` (173) / `mem_hiclaims` (147) / `as_bootc` (221) /
    `drv_hiboot_f` (76) / `mod_hiboot` (49)** are kern_mega's boot-only work
    after `mega_enter`, resident in `.cold` (above 1MB) for the session
    because `.ovl` is gone by then. A post-switch overlay - a `.mega` tail
    the boot drops, or a module - would return ~660 bytes of the code area
    (estimate). Not conventional memory, so low value per byte.

## WANTS THE OWNER

Nothing taken changes what a user sees. One question raised by item 1: kern_mega
is still a `KERN_KNOB` build "while it is brought up" (Makefile); when it
becomes a product, dropping `KERN_KNOB` from its variant is the clean form of
item 1 (and takes its extra blob sector back), at the price of a budget of its
own.

## WANTS AN ABI BREAK

None found.

## CROSS-FILE

* `kernel/mega.inc` (pmcore): `mega_pic_pm` taking the vector base in AH
  would let `megados.inc`'s `mega_dropr` share its ICW1-ICW4 body, est. -13
  `.text`.
* `kernel/mega.inc` (pmcore): far entries per KSEG slot (`mega_kaimf_k0..k5`,
  DI set inside) would make every KSEG site outside `.modl` 3 bytes shorter
  (`push di / mov di / pop di` gone), est. -12 for the four left.

## E9 SITES

The owner's decision is that all port-E9 output on the shipped kern_mega
goes behind MEGADIAG=1 in a separate step, so this pass left every one of
these exactly as it was. In this agent's files (`megados.inc`; no E9 write
in any `%ifdef KERN_MEGA` hunk of the kern_big files, `kernel.asm`,
`megapark.inc` or `megares.inc`):

| routine (section) | what it prints | bytes (code + string, read off the source) |
|---|---|---:|
| `mega_dosparkf` (`.mega`) | `kern_mega: parked kkkkK at pppppp sum ssss:ssss` - KB parked, the park's base, the sum (KERN-MEGA-DOS-PLAN 10, W1: where a break-on-purpose run writes) | ~50 + 30 (`mega_m_park`, `mega_m_at`, `mega_m_sum`) |
| `mega_dwake` (`.mega`) | `kern_mega: unparked` | 6 + 21 (`mega_m_unpark`) |
| `mega_hbwake` path (`.mega`, before `jmp far [mega_hbwk]`) | `kern_mega: resumed` | 6 + 19 (`mega_m_hbwake`) |
| `mega_wrprobe` (`.boot2`, given back after boot) | `kern_mega: a park survived the reset` | ~12 + 38 (`.said`) |

They print through `mm_puts`/`mm_hex4`/`mm_hex2`/`mm_putc` (mega.inc's
`.mega` writers). Readers to adapt when they are gated: grep `tests/` and
`tools/` for `parked`, `unparked`, `resumed` and `a park survived` (the
megados/megahiber/megadosroom family reads them).

## ROWS RUN

Once, after the last code commit (`8eaf7a76`), scoped, `--marty-jobs 1`,
Bochs; the box shared with three other agents (load 5-7 on 4 cores):

| row | covers | result |
|---|---|---|
| `megaxmove` | xmem cells | ok |
| `megacaps` | OSAPI_XMEM_CAPS / the pool | ok |
| `megatmmem` | Task Manager memory page: `OSAPI_XMEM_WALK` (item 5) | ok |
| `megahiber` | hibernate/resume across the `.lowbss` change (item 1) | ok |
| `megadosp` | the DOS park/trip (MEGATRAP tree: the pad's absence on a mega knob build) | ok |
| `mega-diskclone` | DISKOPS.DRV's clone, `diskops.inc`'s KSEG sites (item 2) | ok |
| `mega-czjoin` | DISKOPS.DRV's compressor, `compress.inc`'s KSEG sites (item 2) | ok |
| `mega-autostart` | `kmain_hic` -> `as_bootc` (item 6) | ok (2nd run; the 1st died at 0.2s: `mcopy` not installed - mtools added to the box) |
| `mega-stikboot` | `kmain_hic` -> `stik_boot` (item 6) | ok (2nd run, same cause) |
| `mega-ringmove-tracker` | the speaker door's gate writes in PM (item 3) | ok (2nd run, same cause) |
| `mega-recorder` | `osapi_snd_stream`'s ring test (item 4) | **FAIL, PRE-EXISTING**: Bochs stops advancing guest time at `menu_pick Recorder > About` after Play; `os88bisect classify --base 4fb1e6d5 -n 2` - 2/2 failed at the tip AND 2/2 at the base, same leg |
| `megacz` | DISKOPS compress on 86Box | SKIP (no 86Box here) |

## DEFECTS FOUND

* `DIRW1=1 KERN_MEGA=1` fails to assemble (DEFERRED 9).
* `mega-recorder` fails at the base (ROWS RUN) - Bochs's guest clock
  stalls after Play; not investigated.
* The box had no `mtools`, which three kern_mega port rows need
  (`tests/megafix.py`); `os88soak.py check` did not say so before the run.
* `docs/INDEX.md` was stale at the base (the brief's own `BRIEF.md`), so
  `make`'s doc gate failed on a fresh tree; regenerated in the first commit.
