# ksp11 agent "xfile" - the cross-file follow-up on the merged tree

Kernel size pass 11. The role pass 10's xfile agent had: collect what the
per-concept agents left on their cross-file lists, on the MERGED tree. Branch
`ksp11-xfile`, cut from `kernel-size-p11` at `7d426141` (base `2a05e31f` plus
the diskwrite, caption, voltake, picomem and drivers branches; multisel was
still running and owns `kernel/files.inc` and `kernel/filecp.inc`, which this
branch does not touch). Figures are `tools/kernsize.py --json` sections,
ASSEMBLED bytes, against THIS branch's start (not `2a05e31f`, and not
kernsize's blessed-baseline "+N").

## START (`7d426141`, measured)

| | text | bss | cold | lowbss | vgabuf | resident | ovl | ovlw |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| kern_big | 43,374 | 5,154 | 37,857 | 5,598 | 336 | **92,319** | 2,471 | 5,075 |
| kern_small | 32,008 | 3,069 | 22,987 | 2,868 | 0 | **60,932** | 2,076 | 1,480 |

`kern_dos` (`build/kerndos.bin`): 33,114 bytes.

## TIP

| | text | bss | cold | lowbss | vgabuf | resident | vs start |
|---|---:|---:|---:|---:|---:|---:|---:|
| kern_big | 43,369 | 5,154 | 37,841 | 5,598 | 336 | **92,298** | **-21** |
| kern_small | 31,830 | 3,067 | 22,603 | 2,868 | 0 | **60,368** | **-564** |
| kern_emu (`make emu` at the tip) | 43,628 | 5,154 | 37,959 | 5,598 | 336 | 92,675 | kern_big's routines, the same -21 |

`.ovl`/`.ovlw` unchanged on both; `KERN_SIZE` unchanged (97,792 / 62,976) -
no rung moved, which is not the question. `kern_dos`: kerndos.bin 33,114 ->
**32,799 (-315)**. Modules: DOCK.DRV -12 (kern_big); on kern_small CTRL.DRV
4,490 -> 4,406, FDLG.DRV 1,246 -> 1,232, CLONE.DRV 7,196 -> 7,184
(os88mod's file sizes). No driver image touched.

## TAKEN

### batch 1 - kern_small: the redirector and driver-volume arms the first gate left (small -403; big byte-identical; kern_dos -301)

The list's item 1 asked for `osapi_desk_item_x`'s driver arm on kern_small.
**It was already gone**: `desk.inc` has carried `%ifndef KERN_SMALL` round the
whole service item since it was written (kern_small's body is `stc / retf`,
and `desk_live`, `desk_zone_label` and the icon draw gate their `.svc` arms
the same way). So nothing to take there - and the question behind it, *what
else on kern_small became dead with voltake's `b2c40bee`*, had a much larger
answer one layer down.

SPEC.md 62.9.2.3 already says `kern_small` "carries none of" the redirector,
because `[dsk_vkind]` can only ever be `DVK_BIOS` on a kernel that can stamp
no other kind. The first round of that gate (a previous squash) took the
mount and the six write verbs and left **eleven arms assembled**:

| site | kern_small bytes |
|---|---|
| `dsk_xfer`: the `DVK_DRV`/`DVK_FILE` dispatch and the whole `.drv` arm (`OS88_DRIVERS`: only a driver stamps either kind); the read cache's `DVK_DRV` test | part of disk.inc's -153 |
| `dsk_vol_fixed_x`'s `cmp ah, DVK_BIOS / jne` | " |
| `dsk_up_open`'s `.fsup` arm, and `[dsk_fsup]` (.bss) | " , bss -2 |
| `dsk_free_clus_x`'s `FSV_DFREE` arm (a `jmp short .fat` over the relays in its place) | " |
| the directory enumerator's `.fsenum` | " |
| `dskw_rtbody`, `dskw_rbody` (+ `.fsread`), `dskw_stat_x`, `dskw_read_at_x` (`jmp short .fatat`), `dwf_dskw_read_seq`, `dskw_wabody`; and `dskw_fsop`/`dskw_fsstat`, whose callers are all gated now | diskw.inc -169 |
| `ld_take`'s handle test | loader.inc -7 |
| FDLG.DRV's two (`.media`, the size probe); CLONE.DRV's `cmz_sizes` | module images: fdlg.drv 1,246 -> 1,232, clone.drv 7,196 -> 7,184 (os88mod's file sizes) |

Each `jmp short` that replaces a removed arm is CHEAPER than what it replaces:
15 cycles against the `cmp byte [mem], imm8` (~20) plus a taken `jne` (16).
No path got slower.

And the zeros a reader read to learn "no driver" went with their readers:

| item | bytes |
|---|---|
| `snd_release_inst`'s `drv_svc_call` of `DSV_RELINST` (the stub refuses) - `%ifdef OS88_DRIVERS` | text -13 |
| `cp_snd_rowok` / `cp_snd_row` (CTRL.DRV): on kern_small `cmp al, 1 / cmc / ret` and `xor al, al / ret`, the answers the arms computed from zeros | ctrl.drv 4,490 -> 4,406 |
| driver.inc's stub data: `drv_svc` (36), `drv_owner` (14), `drv_memk` (8, no reader at all on kern_small), `drv_blkcls` (1) | text -59 (-55 in kernsize's attribution, the other 4 of it landing as an alignment pad) |
| the `drv_cls_fp_x`/`drv_cls_svc_x` stub - no caller left | cold -4 |

kern_small: text 32,008 -> 31,940 (-68), bss 3,069 -> 3,067 (-2), cold
22,987 -> 22,654 (-333): **resident 60,932 -> 60,529, -403**. kern_big:
the assembled image is byte-identical (listing build `cmp`'d). `kern_dos`
defines neither symbol and §96.44.9 fences every non-BIOS row out of it, so
the same arms leave it: 33,114 -> 32,813, **-301**.

Every caller proof is the chain SPEC.md 62.9.2.3 gives: a row is stamped
`DVK_DRV`/`DVK_FILE` only by `dsk_vol_add`, which is `OS88_DRIVERS`'s
(voltake batch 2), and `[dsk_vkind]` is copied from a row at the mount
(`disk.inc` 3317/3335). SPEC.md 62.9.2.3 carries a paragraph saying so.

`tools/stkbalance.py` reads both arms of a `%if`, so the gates are laid out
for it: no `pop` is duplicated across `%else`, and the redirected arms' relays
stay inside their routines (two `equ`-aliased relays outside an entry were
tried first; the walker reads an `equ` as an address taken and walked the
relay from depth 0).

### batch 2 - relaxed jumps, and `lea sp` (big -21, small -7; DOCK.DRV -12)

The sweep (item 3) is a `nasm -l` listing of both kernels searched for the
relaxed pair's signature, `7x 03 E9 lo hi` on a line whose source is a `jcc`
(the diskwrite agent's method), each hit mapped back to its file. 83 hits on
kern_big and 31 on kern_small at this branch's start, `files.inc` and
`filecp.inc` (multisel's) excluded from action. Taken:

| item | kern_big | kern_small | cycles |
|---|---:|---:|---|
| `drv_load_row`'s one-per-class check moved FIRST, ahead of the disk (it reads only `DRVR_CLASS`, the row's own expectation, and `[drv_owner]`): the five refusals under it (`jne .already`, `je .bad`, `jc .nodisk`, `jc .noent`, `jb .bad`) reach their answers short; its own exit becomes a near `jmp .fail` (+1) | cold -14 | (not built) | each refusal not taken is 12 cycles cheaper (a short `jcc` falls through in 4 where the relaxed one TOOK its `j!cc +3`, 16). A second driver in one class is now refused before a byte is read; the answer is the same DRVE_TWICE, and only a row that is BOTH a duplicate and, say, missing now reports TWICE where it reported NOENT. SPEC.md 51.2.1 says so |
| `menu_draw_clock`'s "no cells" guard: `or bx, bx / jz .out` (2 + a relaxed 5) became `jcxz .jout` after `.hash`'s `mov cx, bx` (2). With BX = 0 every step above clamps to an empty copy and one NUL into `menu_clkbuf`, and `.jout` is in short reach where `.out` is not | text -5 | text -5 | the live path pays `jcxz` not taken (6) for `or` + `jnz +3` taken (19) |
| `gfx_blit1_x`'s `.noswap`: `mov sp, bp / add sp, 18` -> `lea sp, [bp+18]` (pass 10's leftover, item 4) | cold -2 | cold -2 | 3 bytes to fetch where 5 were - on the 8088 both spellings are fetch-bound, ~20 -> ~12 cycles once per call |
| DOCK.DRV `dk_pass`'s five refusals to `.off` share one relay `.offj: jmp .off` behind the `jmp short .count` | module -12 (`dock.drv` 2,246 -> 2,233 in os88mod's count) | - | a pass that finds nothing is 12 cycles a test faster; the `.off` path pays 12 more |

**`tools/stkbalance.py` reads `lea sp, [bp+N]`** as the walk already reads
`mov sp, bp` / `add sp, N` - it never modelled `mov sp, bp`, trusting the
frame's `mov bp, sp`, so the one-instruction spelling is its `add sp, N`
half. Eight lines. `tests/unit/t_stkbalance.py` gains a QUIET fixture (both
spellings in one routine, on two paths that meet: the old walker reports the
meet at two depths, which is how the fixture was checked to discriminate -
26 passed, 1 FAILED with the tool reverted) and a LOUD one (a wrong N leaves
a word). 27/27.

At batch 2: kern_big text 43,369, cold 37,841 -> resident **92,298** (-21
from start); kern_small text 31,935, cold 22,652 -> **60,522** (-410).

### batch 3 - kern_small: what the driver fence answered for every caller (small -115; big byte-identical)

The same audit as batch 1 one layer out: every resident caller, on
kern_small, of a stub whose answer is fixed (a listing scan of the small
build for assembled references to the stub block's labels).

| item | kern_small |
|---|---|
| `OSAPI_FILE_WRITE_SYS` / `_APPEND_SYS` (`api_file_sysc`): the fence (`dvf_drv_owns_seg`, `stc / retf` here) refused every caller, so both cells are `.refuse`'s answer alone - `mov ax, FERR_PROT / stc / retf`, the registers `.refuse` restored untouched; `[api_sysfp]` goes with them | text -60 |
| `api_file_find`'s fence: `call far` + `sbb al, al` + `inc al` was always AL = 0 (CF is dsk_find's output, never its input - the routine's own note) -> `xor al, al` | text -7 |
| kernel.asm's `drv_svc_call` thunk (6) and the `drv_svc_call_x` stub (4): no caller left after batch 1's `snd_release_inst` | text -6, cold -4 |
| `ui_cmd_reboot`'s `call COLD_SEG:drv_shutdown_x` and `app_close_win`'s `drv_cp_closed_x` (resident far calls to a `retf`): gated. kmain's `drv_notice_x` call stays - kmain is boot overlay, so its site costs no resident byte, and the stub comment's readability argument still holds there | text -10 |
| `cp_drv_gone_x` (its one caller, `drv_release`, is OS88_DRIVERS's) and the `drv_cp_class_x` stub it called | cold -24 |
| the `dvf_drv_owns_seg` stub (no caller left) | cold -2 |
| `osapi_desk_item_x`'s own `stc / retf` -> a label on the stub block's `stc / retf` (the volume slots') | cold -2 |

kern_small: text 31,935 -> 31,852 (-83), cold 22,652 -> 22,620 (-32):
**resident 60,522 -> 60,407**. kern_big byte-identical (listing build
`cmp`'d). SPEC.md 51.0.2 says so, and its rule 3 (`drv_svc` as zero bytes)
is rewritten: a reader of a table nothing can publish is gated with it.
`memory.inc`'s `drv_owns_seg_x` call (the compactor) is the one resident
caller left, and it is on the cross-file list.

## ITEM 2 - `ui.inc`'s two relaxed jumps: REFUSED, both

Counts are 8088 clocks from PERFORMANCE.md Part 2's table, fetch ignored.

* **`jne .evloop`** in `.yield` (`je $+5 / jmp near`, 290 bytes back). The
  idle pass reaches `.yield` through `evq_pop`'s empty exit (`jc .yj2`, 16;
  `jmp .yield`, 15) and then takes the relaxed pair's `je` (16). Two
  re-layouts, neither a size win:
  (a) put the drain test AT `.yj2`, falling into `.evloop`: the test is the
  same 16 bytes with a near `jmp .tail` where the pair was, `.yj1`'s
  trampoline goes near (+1) and `.drag` gains a `jmp short` - **21 -> 21
  bytes**; (b) point `evq_pop`'s empty exit straight at `.tail`: **+3 bytes**
  and ~65 clocks off an idle pass (17.7 passes a second, SPEC.md 8.1.2:
  nothing), and dropping the `[evq_count]` compare would make every pass that
  DISPATCHED pay one more `evq_pop`. A speed trade for bytes, out of scope.
* **`jz .posted_done`** at the top of `.tail` (172 bytes forward over the
  posted-flag ladder). Moving the ladder past the task's closing `jmp .loop`
  makes `jnz .ladder` short (98 bytes) but the ladder's way back is a near
  `jmp .posted_done` (3): **5 -> 5 bytes**, and the once-a-tick ladder pass
  pays 15 clocks more. Shrinking the ladder by the 45 bytes a short `jz`
  needs is not on offer.
* Not touched, so `tests/dispreboot.py`'s step-0 fingerprint is unaffected
  (and the row is green on this branch).
* **Refused also: `[ui_drain]` as a byte** (-1 text, -1 bss, and `dec byte`
  is 8 clocks cheaper than `dec word` on the 8088): the odd byte flips the
  parity of every `.bss` word after `ui.inc`'s block, which costs a 286 a
  wait state on whichever hot words land odd, and keeping parity with a pad
  is 0 bytes.

### batch 4 - kern_small: two relocation procs for a kernel that moves nothing (small -39; big byte-identical; kern_dos -14)

`mem_movable_x` is `stc / ret` without `OS88_COMPACT` (SPEC.md 66.0), and a
listing scan for resident calls to refusal-only bodies found two declarations
still assembled on kern_small: the read-ahead cache's (`mov ax,
dsk_rah_reloc` / `call`, after its claim) and the icon store's (`mov ax,
ico_reloc / mov bx, MEM_P_ICO / push dx / call / pop dx`), and with them the
two procs, 11 bytes of `.text` each and named by nothing else. All four
gated `OS88_COMPACT`: text -22, cold -17 = **resident 60,407 -> 60,368**.
Nothing after either call reads its CF or AX (`dsk_rah_flush` takes no
input; the icon path falls into `.have`, which pops BX). `kern_dos` defines
no `OS88_COMPACT` either: kerndos.bin 32,813 -> 32,799.

Left alone on purpose: `xm_release_rec` is a bare `ret` on kern_small and
`ld_unreserve` still far-calls it through `cw_xm_release_rec` (5 + 4 bytes);
`xmem.inc`'s note says the three teardown sites are unconditional so a
fourth cannot forget one, and that argument is about the kernel that HAS the
body. Not taken; 9 bytes.

## ITEM 3 - the relaxed jumps NOT taken (and why)

At the tip the listing scan finds 83 on kern_big and 30 on kern_small (89
and 31 at the start). By file, outside `files.inc`/`filecp.inc`:

| where | jumps | why left |
|---|---|---|
| `vga12.inc` adapter dispatch: `jne sw_fill`, `sw_spans`, `sw_fill_gray`, `sw_fill_pat`, `sw_xor_fill` x2, `sw_save`, `sw_restore` (kern_big) | 8 | the far target is `softgfx.inc`; any short spelling is a relay (2 + 3 = 5, no saving) except a SHARED one at `gfx_xor_fill_raw`/`vga_xor_fill_vram` (-3) - and that costs the 1bpp path +12 clocks per call to save VGA 12. 1bpp is the target machine: refused. `sw_save`/`sw_restore` are the cursor's, inside IRQ4: not touched at all |
| `vga12.inc` `gfx_blit4`: `jb .run`, `jnz .row` (both kernels) | 2 | the per-RUN and per-ROW loop of the blit. Relaxed costs +3 clocks a looping run; making it short means hoisting ~70 bytes out of the hottest loop in the primitive. A speed job for its own pass, not a size one |
| `vga12.inc` `gfx_blit4`: `jne .hkforce`, `jc .cut` (kern_big); `jc .percol` (kern_small, `gfx_blit1`) | 3 | multi-display paths; a re-layout is -1 at best around `NOBLITCUT` |
| `font.inc` `font_run_x`: `je .planar`, `jne .cells` x2, `jz .out`, `jne .rmu` | 5 | `font_run` may not get slower (the brief); every re-layout moves its per-run prologue |
| `wm.inc` `wm_hit` `je .none`, `wm_dmg_wins` `jae .draw`, `wm_tpen_*` `je thm_t*` x2, `jnz .none` | 5 | the caption agent's refusals (`ksp11/caption.md`), re-checked: unchanged |
| `mouse.inc` `mou_p2_init` `je .none`, `jc .quit` x3; `disk.inc` `dsk_fdd_probe` `jc .nordy` | 5 | `.ovlw`, boot only: no resident byte. A relay for the three `jc .quit` is -6 of `.ovlw` if somebody wants it |
| `loader.inc` `ld_pkg_byname` `jz ld_run_name_x` (kern_big) | 1 | moving the routine beside `ld_run_name_x` relaxes `ld_pkg_start`'s `jz ld_pkg_byname` instead: 0 net |
| `apps/os88ui.inc` `os88ui_btn` `jae .out` | 1 | in ~20 package images and the kernel; no relay site short of `.out` without OS88UI_BOWN |
| modules - CLONE (`clone.inc` 1, `compress.inc` 7), HIBER (`hiber.inc` 15, `hbstub.inc` 1), CTRL/FORMAT (`driver.inc` 3, `shutdown.inc` 2), `desksc.inc` 3, FDLG (`fdlg.inc` 1, kern_small) | 33 | module images, no resident byte; not swept. `hiber.inc`'s `.b1`..`.b6` and `hbm_*` dispatch ladders are the largest (~45 bytes of HIBER.DRV) |
| `boot2.asm` `jne boot2_entry.rerun` | 1 | the boot blob, given back at the end of kmain |

## DEFECTS

None found.

## CROSS-FILE

* **multisel - `kernel/filecp.inc`**: six `cmp byte [dsk_vkind], DVK_FILE`
  arms (lines ~1018, 1191, 1318, 1513, 1739, 2189) are unreachable on
  kern_small by the same chain as batch 1 (SPEC.md 62.9.2.3), and so is
  `drv_fs_call`'s one remaining kern_small caller (2223, `fcp_` chdir) and
  `FCPX drv_fs_has` (1322). `%ifdef OS88_REDIR` round each (FILECP.DRV
  bytes on kern_small, plus the resident ones at 2189/2223, which sit in
  `.cold` there). Not touched: your file.
* **multisel / coordinator - `kernel/memory.inc`**: line ~3959's `call
  drv_owns_seg_x` (the compactor's IVT test) is `stc / ret` on kern_small;
  OS88_COMPACT is kern_big's anyway, so check whether that site is even
  assembled there before spending time on it. And `osapi_mem_movable_x` /
  `mmf_mem_movable` call the `stc / ret` stub too - the cell has to stay.
* **multisel - `kernel/files.inc`**: line ~1227's `call mem_movable_x` is a
  refusal on kern_small, the shape batch 4 took in `disk.inc`.
* `tools/stkbalance.py` now counts `osapi_desk_item_x` as "defined twice"
  (9 -> 10): it is desk.inc's on kern_big and a label in driver.inc's stub
  block on kern_small, and the walker reads both arms of a `%if`. Harmless.

## ROWS RUN (all on this branch's own build, MartyPC unless named)

* After batch 1: `smallboot`, `small128`, `smalllaunch`, `fdlgsmall`,
  `fcpsmall`, `fdlgchsmall`, `dispclose-small`, `deskclipsmall`, `kerndos`,
  `kdos`, `kdhdd`, `kdcwd` - 12 ok. `tmsmall` and `deskitem` FAILED in that
  run, both with os88sym's *"the map describes a DIFFERENT kernel"*: I had
  edited `kernel/` sources while the rows ran (the symbol reader assembles
  the SOURCE). Re-run on a frozen tree below, both green.
* After batch 2: `tmsmall`, `deskitem`, `drvup`, `drvmove`, `lzdrv`, `ems`,
  `rdmount`, `dockmodule`, `dockpos`, `dockmark`, `gfxpoints`, `gfxptsmall`,
  `dispblit`, `paint1blit`, `toastbar`, `dispreboot`, `uiblock`, `small128`
  - **18/18 ok**.
* After batch 3: `smallboot`, `small128`, `smalllaunch`, `fcpsmall`,
  `fdlgsmall`, `dispclose-small`, `tmsmall`, `deskclipsmall`, `fdlgchsmall`,
  `fdlgdrop`, `appsmall`, `smallreq`, `deskitem` - **13/13 ok**.
* After batch 4: `smallboot`, `small128`, `smalllaunch`, `tmsmall`,
  `kerndos`, `kdos`, `kdhdd`, `kdcwd`, `kdreturn` - **9/9 ok**.
* Gates at every batch: `make -j2` (fast tier 61/61), `make -j2 small`,
  `make emu` at the tip, `checkdocs`; `tests/unit/t_stkbalance.py` 27/27;
  `tools/stkbalance.py kernel/kernel.asm kernel/*.inc` **0 unbalanced at
  start and tip** (3,899 entries both), the suite's `stkbalance` row green.

## ROUND 2 - the cross-file list, after multisel merged (small -43; FILECP.DRV -166; big byte-identical)

Merged `kernel-size-p11` (which carries this branch and `ksp11-multisel`),
`make` once. Start of round 2, measured: kern_big 92,219 (text 43,369 · bss
5,154 · cold 37,762), kern_small 60,357 (31,830 · 3,067 · 22,592),
FILECP.DRV 1,936, FDLG.DRV 1,232, kerndos.bin 32,799.

| item | kern_small |
|---|---|
| `filecp.inc`'s six redirected arms, `%ifdef OS88_REDIR`: `fcp_relink`'s decline, `fcp_scan`'s FSV_ENUM arm (the FAT path now enters with one `jnc .fatwalk` over its own `.fsio` relay - one jump where the compare and taken `jne` were), `fcp_mkroot`'s FSV_ENUM probe (`FCPX drv_fs_has`) and `.noenum`, `fcp_rdnext`'s FSV_READAT arm, `fcp_xfer`'s FSV_COPY attempt, and the resident `.fsgo` FSV_CHDIR arm (`call drv_fs_call`) | FILECP.DRV 1,936 -> **1,770**; resident `.cold` -21 |
| `mem_sum_kb`'s driver-buffer test (`push bx / mov bx, [owner] / call drv_owns_seg_x / pop bx / jc .scan` after `je .take`) is a fixed "skip" on kern_small -> `jne .scan` | -11 |
| `fm_fmt_ok`'s `cmp byte [bx+DV_KIND], DVK_BIOS / jne .no` (`files.inc`, every row BIOS), `%ifdef OS88_DRIVERS` | -6 |
| driver.inc's last two refusal bodies, `drv_svc_none`/`drv_fs_call`/`drv_blk_call_x` (4) and `drv_fs_has`/`drv_owns_seg_x`/`osapi_vol_fence` (2): no caller left on kern_small, so a future redirected arm there fails to assemble instead of calling a refusal | -6 |

`files.inc`'s `call mem_movable_x` (~1227) was already `%ifndef KERN_SMALL`
(multisel's), nothing to take. kern_small: cold 22,592 -> 22,549 =
**resident 60,357 -> 60,314 (-43)**; kernsize attributes -44 by file, the
extra byte being alignment. kern_big byte-identical (listing builds
`cmp`'d). FDLG.DRV and kerndos.bin unchanged (filecp.inc is not in kern_dos;
the disk-side code did not move). SPEC.md 62.9.2.3 and 51.0.2 say so.

Round 2 rows, on this branch's build after the commit: `fcpcopy`, `fcpapi`,
`fcproom`, `fcpsmall`, `fdlgdrop`, `fdlgchsmall`, `dispclose-small`,
`small128`, `smallboot`, `smalllaunch`, `tmsmall`, `msel` (kern_big sanity)
- **12/12 ok**. kerndos/kdos rows not re-run: kerndos.bin is byte-for-byte
the size it was and no disk-layer source moved. Gates: `make -j2` (fast
61/61), `make -j2 small`, `checkdocs`, `tools/stkbalance.py` 0 unbalanced
(3,900 entries; "names defined twice" back to 9, because `osapi_vol_fence`'s
second definition went with its stub).
