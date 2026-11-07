# Kernel size pass 18: sweep (the second wave's fresh look at dir's and main's routines)

The **sweep** agent's record (BRIEF.md section 2; started when `main` and
`dir` had landed, pass 17's section 8 lesson that a second-wave fresh reader
pays). Scope: every routine `fresh-routines.md` assigns to `dir` or to
`main`, whatever file it is in, as it stands AFTER those two agents' work -
including the `+0` rows they did not read, which were most of the bytes -
plus the CROSS-FILE items their notes leave and main.md's three priced,
untaken kern_mega items. Branch `ksp18-sweep`, cut from `5b376f4c`
(kernel-size-18 with `main` and `dir` merged). Nothing in a routine
`fresh-routines.md` gives `hot`, and nothing in a general agent's file that
is not a dir/main routine, was edited.

**TAKEN outside the scope**: `fm_c_up`, which `fresh-routines.md` gives
`hot` - taken at the coordinator's word once `hot` had finished, with
files.md's two `dir` items (`fmv_store`, `fm_draw_isel`), after `git merge
kernel-size-18` at `750788df`. Every other edited routine is a `dir` or
`main` row: `assoc_run_x`, `ld_pkg_byname`, `osapi_fs_ent_x`,
`dsk_ico_stage`, `fmv_get_dir`, `fmv_get_icon`, `fmv_store`, `fmv_fit`,
`fmv_icorefs`, `fmv_load`, `fm_onclick_x`, `fm_open_sel`, `fm_edit_end`,
`fm_draw_isel`, `toast_now` (dir's), and main's `mega.inc` items. The files
they sit in (`assoc.inc`, `loader.inc`, `disk.inc`, `files.inc`,
`toast.inc`, `mega.inc`) belong to general agents for everything else; only
these routines' lines moved.

## BASE AND TIP

Assembled, `tools/kernsize.py --json` (kern_mega off `make mega`'s
`kernsize[mega]` lines). Resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf` + `.vga` (+ `.mega` on kern_mega).

**This branch's own bytes, at the merged tip** (`ksp18-sweep` with
`kernel-size-18` at `750788df` merged in): the tip's absolutes, and this
agent's share from `tools/os88rdiff.py kernel 750788df .` on each kernel's
arm (`-D KERN_SMALL`, `-D KERN_EMU`, `-D KERN_MEGA`), which is every byte
between the merge base and the tip and nothing else:

| kernel | resident at `750788df` -> tip | delta | tip `.text` / `.cold` | tip `KERN_SIZE` |
|---|---:|---:|---|---|
| **kern_big** | 91,217 -> **91,150** | **-67** (`.text` -2, `.cold` -65) | 40,496 / 37,562 | 96,768 |
| kern_small | 56,964 -> **56,931** | **-33** (`.text` -2, `.cold` -31) | 30,098 / 21,027 | 58,880 |
| kern_emu | 91,391 -> **91,324** | **-67** (`.text` -2, `.cold` -65) | 40,670 / 37,562 | 96,768 |
| kern_mega | 126,723 -> **126,642** (`.mega` 23,056) | **-81** (`.text` -2, `.cold` -79) | 46,170 / 46,100 | conventional 62,976 of 68,096 |

**Before the merge** (the first five commits, against the branch's own base
`5b376f4c`, each kernel built and measured both ends): kern_big 91,669 ->
91,613 (-56), kern_small 57,309 -> 57,278 (-31), kern_emu 91,913 -> 91,857
(-56), kern_mega 127,164 -> 127,094 (-70). The merge's three items add -11
on kern_big and kern_emu, -2 on kern_small.

`.bss`, `.lowbss`, `.vgabuf`, `.vga`, `.boot2` and `.ovl` moved by nothing
here on any kernel; no module or driver image changed. `tests/megaseg.txt`
is untouched (`os88megaseg` reads 3,682 of 3,684 at the tip: nothing
kern_mega-only in `.text` moved here). The `.text` -2 is `toast_now`'s,
ahead of `ui_task`: an even shift, `hotpar` green on both gated kernels at
every commit.

## TAKEN

kern_big bytes unless a kernel is named; kern_emu follows kern_big, kern_mega
follows kern_big in the shared code. H-grades are khp1's (BRIEF.md 1). Cycles
are the 8088's `max(clocks, 4.34 x bytes)` per instruction. **Every item is
the same instructions or fewer on its common path; none is on an H1/H2 path,
none touches a khp1 pick or a KHP arm, and none adds, removes or moves an
`int 13h` call** - no item changes what a mount, the listing, the read-ahead,
the FAT window or the loader reads.

Parity: the `.cold` hot routines (`dsk_xfer`, `dsk_runcap`,
`dsk_next_clus_x`) sit at shifts -8 and -12 on kern_big (items 1 and 3 are
the only bytes in front of them, both even) and at +0 on kern_small (nothing in front
of them moved there). Everything else edited is in `.cold` past
`dsk_next_clus_x`, where no routine is pinned.

1. **`assoc_run_x`'s resweep jumps back into the sweep it repeated (-8;
   kern_big, kern_emu, kern_mega - kern_small has no associations).**
   `.nofile`'s re-sweep after a FULL rebuild
   ended `pop ax / mov bp, assoc_tryext / call assoc_sweep / jc .noassoc /
   jmp .known` - exactly the four instructions `.home` runs after its own
   `pop ax` and ASSOC_FIND test, at the same stack depth. It is `pop ax /
   jmp .sweep` now, `.sweep` a label on `.home`'s copy. H4, once per document
   whose program went missing: +~15 cycles (one near jump) on a path that has
   just rebuilt a volume's ASSOC.DAT.
2. **`ld_pkg_byname` (-3; kern_small has only the tail jump).** The
   `ASSOC_PWBANK` store is `dec ax / mov [assoc_pwin], ax / inc ax` out of the
   zero the `[ld_base]` compare wants back (main.md's CROSS-FILE, -1); and
   `.posted`'s `xor al, al` was dead (-2): both arms arrive with AX = 0 - the
   `jne .posted` off `cmp [ld_base], ax` with AX = 0, and the called arm
   because `assoc_run_x` restores every register (`kentc_bp` ... `kretfc_es`)
   - so AL is LD_OK either way. A new `%if` in assoc.inc holds ASSOC_PWBANK
   to 0FFFFh, which both the `dec` here and assoc_run_x's `inc di` test
   assume. Once per OSAPI_PKG_START with a document.
3. **`osapi_fs_ent_x` banked BX and SI that nothing writes (-4, kern_big,
   kern_emu, kern_mega; kern_small's is driver.inc's refusal).**
   `dsk_copy_seg_x` and `dsk_put_dir` keep every register, and the routine
   itself writes neither. Between `dsk_runcap` and `dsk_next_clus_x`, so an
   EVEN cut was the only kind allowed: it is. H4, per entry a redirected
   driver lists: ~-54 cycles.
4. **`dsk_ico_stage`'s destination is `mov ax, ds` (-1, every kernel).** DS
   is the kernel's in kernel code, and on kern_mega in protected mode it is
   the writable data alias that `dsk_copy_seg_x` would otherwise substitute
   for KERNEL_SEG's code selector - the same memory either way, as
   `osapi_fs_ent_x` and `fmv_copy_in` already spell it.
5. **`fmv_get_icon` (-6, both kernels).** `dsk_ico_stage` answers SI ->
   dsk_ico on every exit (dsk_get_icon_x already leans on it), so the
   trailing `mov si, dsk_ico` went (-3); DX was banked but nothing in the
   routine writes it - `fmv_iofs`, `fmv_copy_in` and `dsk_ico_stage` all keep
   it (-2); the slot offset goes to SI by `xchg` (-1: AX is reloaded and
   popped, and `dsk_ico_stage` reads AL alone - its `ico_body`/`ico_need`
   never read AH). H3, per icon painted: ~-44 cycles an icon, faster.
6. **`fmv_get_dir` (-2, both kernels).** `xchg ax, si` for the slot offset
   (AX is popped), and SI -> dsk_ent from the DI that `fmv_copy_in` keeps
   rather than an immediate. H3, per entry staged: ~-9 cycles.
7. **`fmv_store` writes FS_IOFH once (-2, both kernels), and its copy
   length is one `lea cx, [bx-FV_DOFF]` (-3, kern_big - files.md's
   CROSS-FILE; FV_DOFF is 128, which `sub` cannot carry as a sign-extended
   byte and `lea` can as a displacement; the flags die at the copy; on
   kern_small FV_DOFF is 0 and the arm is `mov cx, bx`).** It stored a zero
   before the copy and the shape after it; now BX is zeroed up front, `.none`
   (no claim) stores that BH = 0 through the same `mov [di+FS_IOFH], bh` the
   copy path ends in, and nothing between the old store and the new one
   reads FS_IOFH (`fmv_iofs` reads FS_N / `[dsk_nmax]`, the copies read
   nothing of the block). H4.
8. **`fmv_fit` (-4 kern_big, -1 kern_small).** `mem_claim_x` keeps every
   register but DX, so after the claim AX is still VIEW_KB (the FS_VKB store
   takes AL, -1) and BX is still the owning slot `fmv_owner` derived - and a
   compaction inside the claim moves no instance record - so the second
   `call fmv_owner` before `mem_movable_x` went (-3, kern_big; kern_small
   declares nothing movable). H4, per claim taken.
9. **`fmv_icorefs` (-1, kern_big).** On DSK_BIGDIR the `xor bx, bx` served
   only the FSD_ICONS byte's zero (dsk_dest_x's offset there is FV_DOFF), so
   the zero is an immediate and the `xor` is kern_small's alone.
10. **`fmv_load` (-7, both kernels).** `mov bl, al / mov ax, dx / mov dl, bl`
    is `xchg ax, dx` (-5): DH is the caller's AH afterwards, and nothing reads
    DH - `dsk_chdir_x` hands DL to `disk_mount_x`, and the grow's re-list
    (`jmp .again`) already came round with DH = FS_VSEG's high byte; BL, the
    copy, was read by nobody. And the BX bank round the aim went (-2):
    `dsk_chdir_x` reads AX and DL alone, and BX's next readers are kern_big's
    `lea` and the retire's `dsk_dest_0`, which stores it as `[dsk_doff]` with
    no segment - a word that held the CALLER's BX there before, and that
    nothing reads until the next aim rewrites it (its readers are the
    scan's sort, `dsk_ent_ofs` and `fmv_store`'s copy, all after an aim). H4,
    per listing: faster by the two stack ops and two moves; no mount, read or
    `int 13h` call changes.
11. **`fm_onclick_x` (-10 kern_big, -4 kern_small).** The scroll-bar hit
    rebuilt the absolute press as `CX + [fm_cy]` from the content-relative
    pair; that is exactly `[fm_pressy]`, banked before the conversion, since
    `fm_relayout` subtracts the same `wm_content` y that `fm_layout` stores as
    `fm_cy` (-4, both). On kern_big the header-button test before it had just
    loaded those same two words round the same push pair, and `fm_bhit` keeps
    CX and DX, so a click that is no button falls to `.sbar_abs` with them
    loaded (-6). H3, per click in a Disk window: faster on the bar path (two
    pops, two pushes and two loads fewer), +one short jump on a click with
    no scroll bar.
12. **`fm_open_sel`'s folder test is `fmv_gdtype`'s (-4, both kernels).** The
    `mov bx, [si+18]` between the stage and the compare writes no flag, so
    `call fmv_gdtype` is the stage AND the `cmp word [si+16], 2`. +one near
    call/ret (~43 cycles) per entry opened - a launch or a navigation, ~1 s.
13. **`fm_edit_end` stores the loop's zero (-2, both kernels).** `[fm_elen]`
    takes CX, which the `loop` over the pool has just counted to 0. (files.md's
    CROSS-FILE `and word [fm_elen], 0`, -1, is the longer of the two and is
    not taken on top.)
14. **`toast_now` (-2 `.text`, every kernel).** Its `push bx / pop bx` round
    `fpg_bare` was dead: its one caller, `toast_show`, leaves by `jmp short
    .out` / `clc` / `jmp kret_di` straight after, which restores BX as it
    restores the AX this routine already spends. Even, ahead of `ui_task`.
15. **`fm_c_up` (-6, kern_big; `hot`'s, at the coordinator's word, from
    files.md's CROSS-FILE).** BX is `lea bx, [di+FS_PATH]` off the DI the
    routine loaded from `[fm_vp]` - `fmv_ishere` writes AX and DL alone,
    `fmv_sync_x` and `fmv_get_dir` keep DI, and nothing on the way calls
    `fm_vp_set` - and the AX bank round `pth_pop` went: `pth_pop` pushes AX
    itself and leaves through `pth_leaf`'s pop. khp1's files-1 path (Up One
    Folder, H4): two pushes/pops and a `mov` + `add` fewer, faster; the
    mount `fm_go` runs is unchanged, so no `int 13h` call moves.
16. **`fm_draw_isel`'s `jmp fm_more_mark` jumped to the next instruction
    (-2, both kernels; files.md's CROSS-FILE).** Only comments stood between.
    The tail of every row repaint (H3): ~-15 cycles.
17. **kern_mega: `mega_segty` (-11 `.cold`, assembled) and `mega_npx` (-3 `.cold`) -
    main.md's two priced REFUSED rows, taken now that Bochs and the Open
    Watcom fixtures are here and the rows reach them.**
    `mega_segty` derived the descriptor's offset (`DX & FFF8h`) and the LDT
    alias in ES twice, once on the window arm and once on the handle arm;
    the offset now rides in AX across the window walk (AX is banked), the
    walk's fall-through sets CF and `.f`'s equal compare leaves it clear, and
    both arms meet at one `xchg ax, di / mov ax, LDTD / mov es, ax / jnc .set`
    (neither `xchg` nor `mov` writes a flag). `mega_npx`'s two seven-pop
    exits are one: the success arm falls through `SKIPF1` (test al, imm8 over
    the `stc`: CF = 0) into `.no`'s `stc`, the pops, and `jc` to the fault's
    `add sp, 2 / jmp mega_fault`. The handler is entered with the flags this
    leaves, as it was with `mega_bownf`'s - the faulting FLAGS are the CPU
    frame's, under the far return - and `mega_fault` begins with `cli` and
    reads only its frame. Both protected-mode only, `.cold` above 1MB after
    the switch, so conventional is unchanged; not the KHP arms. Rows:
    `win16csalias` (SEG_TYPE on windows and handle descriptors), `win16np`
    (CX = 2/3 and the #NP router), `win16ovr` - ROWS RUN.

## REFUSED

| item | bytes | why not |
|---|---:|---|
| `mega_i21`'s `.none` storing `KERNEL_SEG:kbm_isr` into the slots and sharing the `retf` (main.md) | -1 `.mega` | `.mega` is 23,056 after `fsx.inc`'s `align 16`; one byte falls into the pad and buys nothing assembled. Free and correct; take it with another `.mega` cut that crosses the 16 |
| `mega_segty`'s `.no` and `.done` as one tail (`clc / SKIPAL1 / .no: stc`, pops, `jnc` over `mov ax, 4`) | -1 | `.no` would move to the routine's end, out of short reach of its first `jz .no`, and 8086 has no near `jcc` |
| `assoc_run_x`'s `.noassoc` falling into `.fwd`'s `assoc_back` (the toast before the remount) | -5 | the toast is staged and its TTL starts BEFORE the remount instead of after: a timing a user sees. WANTS THE OWNER if anyone wants it |
| `drv_load_at`'s `.attach` / `.ovl` / `.already` jumps reordered (kern_big) | 0 | every order found is 17 bytes against 17; the `jne .already` cannot reach `.out` short. (A kern_small-only variant was committed and then REVERTED: `drv_load_at` is not assembled on kern_small at all, so it bought nothing on any built kernel) |
| `fm_open_sel`'s `.quiet` / `.titled` tail as `stc / SKIPAL1 / clc` | 0 to +1 | the `.package` arm then needs a jump of its own |
| `fmv_get_icon`'s `call fmv_iofs` fallback | -5 | dir.md DEFECTS 3: the fallback reads the caller's DI on DSK_BIGDIR. Unreachable by reading, but dropping it or fixing it is the Disk window owner's decision, as dir.md says |
| `mod_cfix`'s `.hit` arm (`inc di / dec cx` then `inc cx / loop`) | 0 | every shorter spelling of "skip the high byte" is a byte longer |
| `osapi_drv_classk_x`, `fdlg_open_x`, `fm_mdelarm`, `fm_menter`, `fm_mone`, `fm_mto`, `fm_many`, `fm_clamp_scroll`, `fm_draw_rcore`, `mem_floor_ax`, `mod_fpr`, `drv_shutdown_x` | 0 | read; nothing found that is shorter at the same behaviour (`fm_draw_rcore` is files-2's H3 path and was read for size only) |
| `kbm_isr` | - | H2, and main took its one free item; nothing else cycle-neutral found |

The `+0` rows that are not resident were read for the record and not cut:
`drv_boot_x` (`.boot2`), `mem_init_x`, `files_init_x`, `drv_init_x` (`.ovl`),
`cfx_slide` (`.cfx`), `drv_status_x`, `drv_errstr`, `drv_memk` (CTRL.DRV),
`cpc_bit` (`.modc`), `hbm_sweep`, `hbm_reload_m` (HIBER.DRV), and the
TOASTSTR rows (messages: a reworded one is a function change).

## WANTS THE OWNER

None taken. (REFUSED's `.noassoc` reorder, -5, is one if anyone wants a
toast that starts its clock before the volume is put back.)

## WANTS AN ABI BREAK

None: nothing here is in `apps/os88api.inc`, `drivers/os88drv.inc` or
`apps/cc/os88.h`. `OSAPI_FS_ENT` (item 3) keeps its contract - CX, DX, SI,
DI and BX are all still preserved.

## CROSS-FILE

* `dsk_copy_in_x` (disk.inc, `sys`'s): `mov ax, KERNEL_SEG` is `mov ax, ds`
  for the reason of item 4 (-1); and any other `.cold` site that names
  KERNEL_SEG as a copy's destination.
* `fmv_store`'s `call KERNEL_SEG:wm_su_stale` (5 bytes) - if a `ct_` trampoline
  for it ever exists, -2; not worth one for a single site.
* main.md's `ld_pkg_byname` item is TAKEN here (item 2), with two bytes more
  than it priced.
* The `mega_i21` byte (REFUSED) is free for whoever next cuts `.mega`.

## ROWS RUN

Once on the merged tip (`749eee19`, after the last code commit, tree not
edited while they ran), one `python3 tools/os88test.py soak -k ...
--marty-jobs 1`: **37 passed, 0 failed, 0 skipped** (574.5 s). Bochs and
the Open Watcom fixtures are present in this container now (`make
win16fixtures` fetched the compiler at its pinned hash into this tree's
`build/watcom/`), so the kern_mega rows main.md could not run did run.
(The same 36 less `lzcomp` had passed once before the merge, 735.4 s; the
merge and the three CROSS-FILE items came after, so the run above is the one
that counts.)

| rows | what of this note they cover | result |
|---|---|---|
| `win16csalias`, `win16np`, `win16ovr` (Bochs, kern_mega) | item 17: SEG_TYPE on a window and on a block's own handle descriptor (both `mega_segty` arms), CX = 2/3 and the #NP router (`mega_npx`'s success arm) | ok 11.6 / 39.3 / 16.0 s |
| `assocstale`, `assocfind`, `assocopen`, `assocwake`, `assocsweep`, `assocflush`, `win16nowin` | item 1: `.nofile`'s rebuild and re-sweep, `assoc_ownex`'s refusal into `.needs` | ok |
| `win16nedos`, `dosopen` | item 2: OSAPI_PKG_START by name with a document, posted and called | ok 27.1 / 39.3 s |
| `rdmount`, `rdicon` | item 3: a redirected volume's `OSAPI_FS_ENT` and its listing | ok |
| `icostore`, `icohd`, `icogrow`, `icoshed`, `ascabsorb`, `regwork` | items 4, 5, 9: icon staging, the FSD_ICONS repair | ok |
| `bigdir`, `msel`, `shedrelist`, `dirwshed`, `hdnoclaim`, `dirwsize`, `fmcommit` | items 6-8, 10, 13: the listing claim, its grow/cut, the store, the shed and re-list, the editor's end | ok |
| `fmbtn`, `sbar`, `fmarrows`, `fmthumb` | items 11, 16: header buttons, scroll-bar clicks and drags, the row repaint's tail | ok |
| `fdlgchoose`, `fdlgchsmall` | items 5-8, 10-12 through the chooser, on kern_big and kern_small | ok |
| `navcalls` | items 10, 12, 15: **the int 13h counts of open/enter/Up, unchanged** | ok 18.6 s |
| `lzcomp` | item 15: Nav > Up One Folder (`fm_c_up`) | ok 33.0 s |
| `toastbar`, `toastfpg` (and the fast tier's `toast`) | item 14 | ok |

`win16nowin` reported "UNDERRAN 4% of its declared 60s": it is a QEMU row
and was re-run alone - both its checks run and pass (the toast reads 'Needs
WIN16.O88', the BIOS tick moves), so the declaration is generous, not the
row empty.

**Broken on purpose** (`win16np`, new to this pass's agents): `mega_npx`'s
`SKIPF1` replaced by a `nop`, so the success arm fell into `.no`'s `stc`
and every routed #NP went to `mega_fault` - the row went red in 9.2 s
("kern_mega FAULT E4 err 121C", `w16_cnload 0`, three checks failed). The
source was restored from a copy, `make mega` rebuilt it to the same
`kernsize[mega]` figures, and `win16np` and `win16csalias` passed again.

Not reached by any row: `fmv_bcast`'s adopt step with a sibling window (dir
DEFECTS' note - `fmv_store` is the only routine of mine on it, items 7 and
its BH store, proved by reading). Gates at each commit: `make -j2` (the fast
tier, 73-74 passed, `hotpar` green on kern_big and kern_small), `make -j2
small`; `make emu` and `make mega` at the tip (and before the merge);
`tools/stkbalance.py` over every touched file, base against tip: the same 16
paths; `checkdocs` after `git add`.

## DEFECTS FOUND

1. **`drv_load_at` is not assembled on kern_small**, which this agent's
   second commit assumed it was (its message claimed -2 there). The edit
   moved no byte on any built kernel and is reverted by `31001568`
   (kern_big byte-identical across it); the message of `8b9e03cd` is wrong
   on that one point. Found reading
   `os88rdiff -D KERN_SMALL`.
2. Not a defect in the tree, a hazard for the next agent: a `make mega`
   started and then a kernel source edited under it failed with "the map
   describes a DIFFERENT kernel" - the rule that `make` is single-tenant
   covers the SOURCES as well as `build/`.
