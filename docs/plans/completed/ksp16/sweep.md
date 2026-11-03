# ksp16 agent "sweep" - everything else resident, and a second look

Kernel size pass 16 (`docs/plans/completed/ksp16/BRIEF.md`), second wave: the
sweep agent, after heap, place and ops merged. Scope: `viddet.inc`,
`vidsel.inc`, `vidext.inc`, `snd.inc`, `dock.inc`, `blank.inc`, `apps.inc`,
`xmem.inc`, `events.inc`, `cpudet.inc`, `desk.inc`, `desksc.inc`,
`ctrl.inc`'s resident thunks, `kernel.asm`'s resident code, and a SECOND LOOK
at the finished agents' `memory.inc`, `mod.inc`, `toast.inc`, `fprog.inc` and
`fsx.inc` (their REFUSED lists read first, and none re-proposed). gui's and
fsys's files (running in parallel) were not edited. Branch `ksp16-sweep`, base
`14b28b74` (the ops merge). Figures are ASSEMBLED, `tools/kernsize.py --json`
absolutes on each kernel's own build; resident = `.text` + `.bss` + `.cold` +
`.lowbss` + `.vgabuf` + `.vga` (kern_mega: plus `.mega`, which did not move).

## BASE AND TIP

| kernel | base `14b28b74` | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,912 | **91,809** | **-103** | `.text` 41,177 -> 41,104 (-73), `.cold` 37,588 -> 37,558 (-30) |
| kern_small | 57,902 | 57,857 | **-45** | `.text` -27, `.cold` -18 |
| kern_emu | 92,168 | 92,065 | **-103** | as kern_big |
| kern_mega (with `.mega` 21,712) | 125,374 | 125,266 | **-108** | `.text` -75, `.cold` -33 |
| kern_mega segment overhead (`os88megaseg`) | 3,250 of 3,250 | **3,248** of 3,250 | -2 | gate green |

No `.bss`, `.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl`, `.cfx` or module
byte moved; `BOOT2_SECS` unchanged. kern_big's `KERN_SIZE` is 97,280 at the
tip against the brief's 97,792 at the pass's base - the rung the heap, place
and ops merges uncrossed, not this branch's, and per the banner not the point.
`tests/megaseg.txt` is left at 3,250 although the figure is 3,248: gui and
fsys are still landing and a tightened ratchet would fail their merges for
bytes that are not theirs - the coordinator's call at the close.

kern_big and kern_mega move almost together, which is what keeps the megaseg
gate green: a `.text` byte off kern_big alone RAISES kern_mega's overhead.
Every `.text` item below is on both, and the two that are kern_big's alone
(SKIPF1 in `db_hit`, blk_pass's `.act`) are matched by mega-only bytes.

## TAKEN

Bytes are kern_big (= kern_emu) / kern_small / kern_mega. Every item
assembled and measured; none is on the hot list's own bodies.

### 1. Single-caller bodies written at the call

1. **`vid_mono_text` into `vid_text`** (-18 / -7 / -18). Its one caller already
   banks all eight registers (`vid_pop8`), so the routine's six pushes, six
   pops, its CF answer and the caller's `jc` went; it is `vid_text`'s `.mono`
   arm, after the `.out` jump so the ROM path is the fall-through. On
   kern_small (no `GFX_VGA`) the `clc / ret` stub and its call simply vanish.
   SPEC.md 39.20's paragraph and table now say `vid_text`'s mono arm and no
   longer CF. Reached on a Hercules primary with a VGA beside it, at reboot
   and at fsx's `FSXM_TEXT80` (`disptext` is its row).
2. **`vid_ac_pal` into `vid_bk`'s `.planar`** (-8 / 0 / -8): `vid_bk` banks AX
   and DX already (its two entry stubs push DX), so the call, the ret and the
   inner bank went. The header is kept for SPEC.md 39.24/64, which cite the
   name. Blanker only (`savervga`).
3. **`ss_start_x` and `ss_set_x` into the far entry `ssf_set`** (-7 .cold /
   n/a / -7). Two calls, three rets and two CF answers each made for the next
   one to test; now every refusal is ONE `stc` and every success ONE `clc`,
   with SKIPAL1 between them (AL is the pushed AX's). Saver only.
4. **`mod_check` into `mod_need`** (-7 / -8 / -7, .cold). Written in place it
   relaxed six early refusals' `jc .fail` (+9, measured): the check sits BELOW
   the answers, reached by `jmp short .check`, and its refusals land on
   `.freefail` with `.badarm` falling into it. Then the success path joins the
   refusal's `call drv_vol_back` - that routine keeps the flags (`pushf ..
   popf`), so one call answers both ways - with the `clc` only where the arm
   before it does not already leave CF = 0 (kern_small's ROM door falls out of
   its `jc`). `mod_cfix` stays a routine: `tools/os88rom.py` tests for its
   SYMBOL to decide whether to prove the slide. Every module load.
5. **`db_px_tile`, `db_xor_ring`, `db_xor_body` into `db_paint`'s tile loop**
   (-11 / -11 / -11). Each banks only what the loop still needs (the ring
   AX/BX/CX, the body BX/CX, the span all four). Faster on every dock paint
   (no call/ret, fewer pushes). `db_draw_tile` was NOT written in too: the
   loop's `jb .tile` and `je .next` would relax past 128 bytes (measured room
   ~60). Dock paint (`dockpos`, `dockmark`, `deskdock`).

### 2. Banks and tails

6. **`api_cpname` banks the caller's ES and DI** (-6 / 0 / -8, `.text`). Three
   of its four callers (`api_rn`, `api_fdlg_open`, `api_file_sysc`) pushed both
   around the call and popped them on the next instruction; the bank is in
   the routine now (+8) and the rename, which wants ES = KERNEL for its second
   copy, calls the old fall-through entry as `api_cpname0`. `api_file_sysc`'s
   refusal shares the success path's `pop si / pop ds / retf` (-1 of it,
   swallowed on kern_big by `disk.inc`'s `align 2`).
7. **`mem_region_reloc` on the ladder** (-10 / 0 / -10, `.text`): `call kent_bp
   / push es` and `jmp kret_es`, so the worker block's own DX/BP bank and the
   later `push bx` went with the five pushes and six pops. Per region MOVE,
   beside a `rep movsw` of up to 64KB; +2 bytes of stack depth there, and
   `stkclass` is green.

### 3. Encodings, skips and stores (all flag-checked at the site)

8. **`and word [x], 0` for `mov word [x], 0` where the flags are dead** (-1
   each): `toast_kill`, `db_paint`'s `.zap`, `lnk_open`, `osapi_desk_item_x`,
   six claim-record clears in `memory.inc` (`mem_shed_one`, `mem_pg_forget`,
   `mem_cp_walk`, `mem_free_x`, `mem_free_owner_x`, `osapi_mem_compact_x`'s
   `[mem_cp_self]`)
   and, on kern_mega alone, `mem_claim_1`'s `.mpno`, fsx's run walk and
   kmain's `[mega_seg0]`. Each is followed by a `popf`, a flag-writing
   instruction or a call that answers in the flags. `fpg_end` was NOT
   converted: it preserves the flags by contract.
9. **SKIP tails**: `snd_tone_req`'s grant through SKIPF2 (-2), `desk_hit`'s
   miss (SKIPF1, -1), `db_hit` (SKIPF1, -2, kern_big's DOCK_OPT arm),
   `db_click`'s `.consumed` (SKIPAL1, -1), `mem_hitag` (kern_mega, -1).
10. **Small rewrites**: `vid_setmode`'s GC writes by AL (-2, AH still 0);
    `snd_tone_req`'s two `1` stores off AL (-2); `osapi_img_name`'s copy by
    `lodsb/stosb` with the answer banked in CH (-2, .cold); `osapi_file_here`'s
    `xchg ax, dx` (-1); `mem_hifit`'s answer as `cmp si, 1` (-2);
    `desk_zones_paint_x` clears its known-set bit by `xor` (-2); `desk_cell_xy`
    by `cbw` (its AL is a cell, < 64; -1); `toast_arm`'s two stores off one AX
    (-1); `blk_set`'s display counter by `sbb ch, ch / inc ch` (-2) and its walk
    entered at the test (-2); `blk_pass`'s `.act` by `dec byte [blk_act]` (its
    two writers store 1 and nothing else; kern_big only, -1) and its wheel
    clear by `and` (-1).

### Per routine, kern_big (`tools/os88rdiff.py kernel 14b28b74 .`)

`vid_setmode` 167 -> 165, `vid_text` 44 -> 75 + `vid_mono_text` 49 gone,
`vid_bk` 52 -> 74 + `vid_ac_pal` 30 gone, `ssf_set` 6 -> 89 + `ss_start_x` 44
and `ss_set_x` 46 gone, `mod_need` 143 -> 234 + `mod_check` 98 gone, `db_paint`
218 -> 244 + `db_px_tile` 13, `db_xor_ring` 12, `db_xor_body` 10,
`db_xor_pop` 3 gone, `db_hit` 64 -> 62, `db_click` 61 -> 60, `api_rn` 31 -> 27,
`api_fdlg_open` 26 -> 22, `api_file_sysc` 45 -> 38, `api_cpname` 5 -> 8,
`osapi_file_here` 10 -> 9, `osapi_img_name` 81 -> 79, `mem_region_reloc`
111 -> 101, `mem_hifit` 88 -> 86, `blk_pass` 86 -> 84, `blk_set` 41 -> 37,
`toast_arm` 27 -> 26, `toast_kill` 22 -> 21, `snd_tone_req` 87 -> 83,
`desk_zones_paint_x` 58 -> 56, `desk_cell_xy` 35 -> 34, `desk_hit` 41 -> 40,
`osapi_desk_item_x` 133 -> 132, `lnk_open` 117 -> 116, and six memory.inc
clears at -1.

## HOT PATHS AND PARITY

- Nothing on the hot list changed its own bytes. `ui_task`'s idle pass calls
  `blk_pass`, whose idle path is untouched (only `.act`, after input, moved).
  The dock paint is FASTER (three calls and their pushes gone per changed
  tile); the event ring, IRQ0 (`snd_tick`), the mouse ISRs, `sch_*`, the blits
  and `font_run` are untouched.
- **`t_hotpar` green on both gated kernels with NO pad.** The first build of
  batch 1 failed it (the api stubs' -7 and kern_small's `vid_text` -7 ahead of
  `gfx_blit4`); `osapi_file_here`'s odd byte paired them. Every later item was
  chosen per inter-hot region so each region's sum stays even - which is why
  `blk_pass`'s `dec` is kern_big's alone (kern_small had no partner byte in
  that region) and `osapi_segr`'s kern_small arm was left as it is.
- **No new parity pad.** Not gated and now at an odd shift from the base:
  `api_file_rename`/`api_cpname`, `osapi_file_here`, `toast_kill`, `desk_hit`,
  `lnk_open`, `snd_tone_req` and the `.cold` stretch from `ssf_frame` to
  `mod_need` - none on a hot path.

## REFUSED

- **`desk_draw_zone` onto `kentc_bp`** (-4 on both arms): it is the desk's
  per-zone repaint, and pass 15's DECLINED 3 refused `kent_di` on the
  damage-repaint path in exactly this shape (~90 clocks a call, priced per
  item drawn).
- **`db_draw_tile` into `db_paint`** (~-7): relaxes the tile loop's branches
  (+6 or more) - see TAKEN 5.
- **`mod_cfix` into `mod_need`** (-4): `tools/os88rom.py` keys the slide
  proof on the `mod_cfix` symbol in `.cold`.
- **`mem_hifit` / `mem_cp_dest` into their one callers** (-4 to -8 each): both
  sit across `mem_regrowp`'s / the walk's short branches, which would relax;
  not built.
- **`snd_beep` on `kent_di`** (-1): reached from refusal paths on any task,
  so +6 bytes of stack on a worker slice for one byte.
- **`osapi_file_goto`'s `mov ax, dx` as `xchg`** (-1): it would return a
  different DH, and `OSAPI_FILE_GOTO` is frozen ABI that does not promise DX.
- **`toast_say`'s range test as one unsigned compare** (-1): wrong for CX = 0.
- **`fpg_end`'s store by `and`** (-1): it preserves the flags by contract
  (the `dskw_*_x` epilogues sit it between an answer and its caller).
- **A shared `snd_evtmp`/`fsx_evtmp`** (8 bytes of `.bss`): refused in
  events.inc's own words - only the snd drain holds `sch_lock`.
- **`vx_null` reshaped** (~-2): a VESA detach target; not worth the ABI read.

## WANTS THE OWNER

1. **The About box into a module, ~-260 resident on every kernel (ESTIMATED
   from spans)**: `app_about_paint_x` 68 (.cold) and its strings, template and
   vid table ~210 (.text: `app_about_s1/s2/s3p/s3c`, `app_about_v0..v3`,
   `app_about_vid`, `app_tpl_about`, `app_ttl_about`), less a thunk. What
   changes: opening About would read the system disk (one `int 13h`, ~400 ms
   on a 5150) and refuse without it, where today it always opens.
   `app_about_center_x` stays (`ui_note_paint_x` uses it).

## WANTS AN ABI BREAK

None. No cell, header, file format or `SYSTEM.CFG` key moved.

## CROSS-FILE

- **fsys**: `kernel/mod.inc`'s check now relies on `drv_vol_back`
  (`drvvol.inc`) keeping the flags, which its body does (`pushf .. popf`) - a
  change there changes `mod_need`'s success answer.
- **gui**: ops's CROSS-FILE (`sch_isr`'s ROM chain in `fsx_rtick`'s shape,
  ~-7 and faster on IRQ0) is still open and still gui's.
- Comments in gui's/fsys's files that name `vid_mono_text`, `ss_set_x` or
  `vid_ac_pal` (`mouse.inc:5181`) were left; the names are kept in the
  headers here so the references still resolve.

## TEST FIX: `dockpos`

`tests/dockpos.py` asserted *advanced Dock has exactly its rounded module
claim* against `(MODK_SIZE + 1023) // 1024 * 64` paragraphs, the KB, and
failed at the base: since `aafe18bc` `mod_need` sizes a module's claim with
`mem_bytes_gp_x`, the size the read delivers rounded to the SECTOR (SPEC.md
50.2.1), and DOCK.DRV unpacks to exactly `MODK_SIZE` (2,252 bytes: 160
paragraphs, where the KB rule says 192). The row now expects `(MODK_SIZE + 511)
// 512 * 32` - except under Bochs, where kern_mega's protected-mode pool still
counts whole KB (`mx_tab`'s `MX_KB`, and the KB over the sector is the KB over
the bytes). See ROWS RUN for the break-it check.

## ROWS RUN

Once, after the last kernel commit (`38e9d895`), `python3 tools/os88test.py
soak -j 2 --marty-jobs 1` over 45 rows: **44 ok, 1 FAIL** in 1,202 s.

| rows | what of this branch they reach | result |
|---|---|---|
| `disptext` | `vid_text`'s `.mono` (Hercules primary, VGA beside it) | ok |
| `saver`, `savervga`, `dispsaver`, `mega-savervga` | `ssf_set`'s merged arms, `vid_bk`'s planar blank, `blk_set`'s walk | ok |
| `cfgtrip`, `cpnames`, `lzmod`, `modstr`, `kernmods`, `coldslide`, `small128`, `mega-cfgtrip` | `mod_need`'s written-in check and joined tail, on every module load; the slide's `mod_cfix` | ok |
| `rombig` | kern_big's ROM adoption (`mod_need`'s ROM arm is kern_small's) | ok |
| `romsmall` | kern_small's ROM adoption | **FAIL**, deterministic, green at the base: DEFECTS FOUND 1, fixed, re-run **ok** |
| `dockpos`, `dockmark`, `dockmodule`, `deskdock`, `mega-dockpos` | `db_paint`'s written-in bodies, `db_hit`, `db_click`; the test fix on both arms | ok |
| `desksc`, `deskitem`, `deskwhole`, `deskhover`, `stikboot`, `mega-desksc` | `desk_hit`, `desk_cell_xy`, `desk_zones_paint_x`, `osapi_desk_item_x`, `lnk_open` | ok |
| `fdlgchoose` | `api_fdlg_open` on the new `api_cpname` | ok |
| `regmove`, `regwork`, `regapp`, `rehomemove`, `heapcheck`, `regrowshed`, `memp`, `heapmap`, `drvmove`, `sndmove`, `mega-regmove`, `mega-heapcheck` | `mem_region_reloc` on the ladder, the `and` clears, `mem_hifit` | ok |
| `toastbar`, `toastfpg` | `toast_arm`, `toast_kill` | ok |
| `sndplay` | `snd_tone_req` | ok |
| `tmsmall`, `megatmmem` | `osapi_img_name`'s copy | ok |
| `megarmb` | kern_mega's real-mode bracket (fsx's run walk) | ok |

After the `os88rom.py` fix (`497d57a4`): `romsmall`, `rombig`, `hmacold`,
`hmadrv` **ok**; `hmahiber` and `hmapool` SKIP (need qemu9, unicorn).

**Broken on purpose**: `mem_bytes_gp_x` rounding to the KB again (`add ax,
1023 / and ax, -1024`) took `dockpos` red on exactly *advanced Dock has
exactly its rounded module claim* (and the fast tier's `heapgran` with it);
restored, rebuilt, green.

Every batch: `make -j1` (the fast tier: `hotpar`, `stkclass`, `asmrules`,
`ovlchk`, `canary`, `blobruns`), `make small`, `make emu`, `make mega` (the
megaseg gate). `tools/stkbalance.py kernel/*.inc kernel/kernel.asm`: 0
unbalanced at the base and at the tip.

## DEFECTS FOUND

1. **`tools/os88rom.py` gave `vid_seg` an EMPTY hash span on kern_small**,
   so the ROM refused the kernel whenever `vid_seg` sat at an even address
   (fixed, `497d57a4`). A data label's span ran to the next label in the map,
   and `vid_tab_end` - a marker that emits nothing - shares `vid_seg`'s
   address and sorts after it by name, so the span ended where it began; the
   boot writes `vid_seg` (the adapter's segment) before the ROM is asked, and
   the hash disagreed. It had worked because `vid_seg` sat at an odd address,
   where the round-up to whole words covered the word by luck; this branch's
   odd `.text` shift ahead of it moved it. Found by `romsmall` going red here
   and green at the base, then by dumping RAM `.text` against the file over
   the ROM's own spans (one word differed: `vid_tab_end+0`, `00A0` -> `00B8`).
   The span now ends at the next HIGHER address. `COLDHMA.SYS`'s span list is
   cut by the same code (`hmacold`, `hmadrv` green).
2. `dockpos` failed at the base on a stale mirror of the claim rule (fixed,
   TEST FIX above).
