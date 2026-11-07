# Kernel size pass 18: sys

The sys agent's record: a general whole-file finder over `kernel/disk.inc`,
`diskw.inc`, `drvvol.inc`, `loader.inc`, `lz.inc`, `lzfilt.inc`,
`compress.inc`, `memory.inc`, `mod.inc`, `driver.inc`, `xmem.inc`,
`hiber.inc`'s resident half, `clock.inc`, `clockw.inc`, `serial.inc`,
`cpudet.inc`, `sched.inc`, and where code lives (`boot/boot2.asm`'s kernel
blocks, `.cfx`, `.ovl`) - except every routine `fresh-routines.md` assigns to
`hot`, `dir` or `main`, none of which was edited. Branch `ksp18-sys`, cut
from `c5a7b0f9` (the brief). Every figure is ASSEMBLED: `tools/kernsize.py
--json` on each kernel's own build, the section lengths of the map, and
`tools/os88rdiff.py kernel` per routine. No estimate is quoted as a saving.

## BASE AND TIP

Resident is `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf` + `.vga`
(kern_mega: plus `.mega`, unchanged at 23,072).

| kernel | base (`c5a7b0f9`) | tip | delta | where |
|---|---:|---:|---:|---|
| **kern_big** | 91,767 | **91,683** | **-84** | `.text` 40,633 -> 40,623 (-10), `.cold` 38,034 -> 37,960 (-74) |
| kern_small | 57,333 | **57,259** | **-74** | `.text` 30,232 -> 30,222 (-10), `.cold` 21,287 -> 21,223 (-64) |
| kern_emu | 92,011 | **91,927** | **-84** | as kern_big |
| kern_mega | 127,276 | **127,192** | **-84** | `.text` 46,307 -> 46,297, `.cold` 46,573 -> 46,499; conventional 63,488 -> **62,976** (one 512-byte step down, 5,120 spare of 68,096) |

`KERN_SIZE` is unchanged on kern_big (97,280), kern_small (59,392) and
kern_emu (97,792). No `.bss`, `.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl`
or `.cfx` byte moved on any kernel; `BOOT2_SECS` is unchanged, so
`t_canary`'s band is not touched. No module or driver image changed.
`disk.inc`'s `align 2` after `dsk_img16` swallowed nothing here: every
`.text` saving in this note is in `sched.inc` and `clock.inc`, above it, and
the figures above are the sections' own lengths (`.text` -10 on kern_big is
the 10 bytes of TAKEN 13-15).

## TAKEN

Bytes are kern_big / kern_small (kern_emu = kern_mega = kern_big unless
said). Hotness is khp1's scale (BRIEF.md section 1). Every speed figure is a
hand count in 8088 terms (`max(clocks, 4 x bytes)`) unless it says unicorn;
every changed path got FASTER or stayed equal - none got slower. **No
`int 13h` call was added or removed on any operation**: no transfer, mount,
read-ahead, FAT-window or loader decision changed, only how registers are
banked and constants loaded around them (`navcalls` below counts them).

### diskw.inc (all past `dsk_next_clus_x`: no pinned routine follows)

1. **`dskw_zerobuf` has no KENT bank** (-8 / -8). Its two callers are
   `dskw_mkbody`, between a `dskw_dotents` that spends AX/CX/DI/ES itself
   and a reload of AX and CX, and `dskw_dotents`, which spends them: the
   ladder kept nothing anybody read. It now leaves ES = LOW_SEG, which both
   callers want next, so `dskw_dotents`' own `push ss / pop es` goes too
   (`dskw_now` keeps ES). -6 and -2. H4 (New Folder); fewer cycles.
2. **`dskw_read_at_x` holds the capacity in SI** (-4 / -4). SI is the stat's
   name and is spent once `dskw_stat_x` has answered; the FAT arm reads
   `[dwr_cap]` three times after it. H4.
3. **`dskw_mark_free` without its AX/DI bank** (-4 / -4). Both calls are
   `dskw_find`'s: DI is reloaded for every entry's name compare and AX is
   the entry byte the loop reloads or is answered over at `.noent` (the
   loop's own comment already said "DI holds nothing here"). The directory
   scan, per free entry: ~54 cycles fewer.
4. **`dskw_wone` / `dskw_rone`: CX is the caller's to bank** (-8 / -8). Both
   calls are `dskw_xclus`'s; `.stg` banks its own loop count round the call
   and `.tail` reads no CX after it, so the routines' CX banks went (-4);
   and the write's zero-fill count is `dsk_secbuf + 512 - DI` off the DI the
   copy leaves, so DX is never borrowed (-4). Per staged sector: faster.

### disk.inc

5. **`dsk_dirw_next_x` compares a byte through AL** (-5 / -6). Inside a
   cluster `[dsk_wsec]` runs 0..spc and spc is at most 128, so the word
   compare against BX is exact as a byte compare against AL (AL is free:
   both arms reload the cluster). BX is then never written, so the bank round
   the routine goes, and the `mov bl` skip over `.end`'s `stc` (which needed
   BL banked) is `SKIPF1`, whose CF = 0 IS the answer - kern_small's `clc`
   goes with it. Callers read CF alone on this path (`dsk_dirw_get_x`,
   `dsk_dscan`, FILECP.DRV's `fcp_*`: all `jc`). Per directory sector:
   ~35 cycles fewer.
6. **`dsk_read_chain_x`'s redirected arm reuses `fpg_begin`'s DX:CX** (-9 /
   0, OS88_REDIR). `fpg_begin` keeps every register (its banner), so the
   bytes the widget was handed are the 32-bit capacity READAT wants; it was
   multiplied out a second time. The FAT arm's pops moved below the arm.
7. **`dsk_get_icon_x` tests the listing segment through AX** (-2 / -2):
   `mov ax, [dsk_dseg] / mov es, ax / or ax, ax` where it loaded ES from
   memory and compared memory with 0. Per icon drawn: fewer cycles.
8. **`dsk_geom_pick` written at its one call, in `dsk_xfer`** (-7 / -7, and
   with item 9 -8). It is indexed through BP - which no path reads before
   `.sector`'s arms set it - so neither the caller's BX nor AX is banked,
   and the call and `ret` go. The inserted block is 38 bytes, an EVEN count,
   so **`dsk_xfer`'s per-sector loop is byte-identical and at an even shift**
   (`os88rdiff --show dsk_xfer`: only the insertion differs; `dsk_xfer` -4,
   `dsk_runcap` +34). **~95 cycles fewer per `dsk_xfer` call** on the BIOS
   arm (call + ret, two push/pop pairs and the BX index set-up, against two
   `ds:` overrides). This was pass 17's CROSS-FILE (fresh.md: "~-6, not
   checked"); `dsk_xfer` is in no fresh row this pass.
9. **`dsk_vol_fixed_x` drops a `clc`** (-1 / -1). CF is 0 at `.out` on every
   way in: `sbb al, 0` on a 1 cannot borrow, `cmp ah, DVK_BIOS` is a compare
   with 0, and `test` clears it. Taken for item 8's parity: with it,
   everything from `dsk_vol_rmv` to `dsk_next_clus_x` (`dsk_fat_ofs_x` and
   `dsk_clus2lba_x` among them) is at an even shift; `os88rdiff` reports
   one identical routine at an odd shift, `dsk_vol_fixed_x` itself.
10. **`dsk_bpb_check`'s two fail relays are `stc / ret`** (-2 / -2): the
    relay IS the answer, a byte shorter than the near jump to it. Per mount.
11. **`dsk_up_open` loads `[dsk_fsup]` before the branch** (-2 / 0,
    OS88_REDIR): `mov` writes no flag, and `dsk_dotdot_x` answers over it.
12. **`dsk_synth_x` compares the extension's two 8s as one word** (-6 / -6),
    `cmp word [es:si+DSK_R_EXT+1], '88'`. Per directory entry whose
    extension starts with O: one compare and branch fewer.

### loader.inc

13. **`ld_run_body_x` holds the read offset in BX** (-10 / -10): `xor bx, bx`,
    the compressed arm computes into it, one store at `.rdgo`, and the
    reload before `dsk_read_chain_x` goes - `dsk_bytes_secs_x` spends CL
    alone. Per launch.
14. **`ld_start` loads ES from the AX that holds the base** (-2 / -2).

### memory.inc

15. **`osapi_sys_kb_x` loads its four constant rows into AL** (-4 / -4). AH
    is 0 there: the image row just stored is the kernel's KB less
    constants, under 256 on every kernel (the `cbw` / kern_mega's `mov ah,
    0` above it are the same fact). kernel.asm gains an `%error` guard
    beside `KBUF_KB` holding KERN_KB and the four figures under 256 (no
    bytes; the file is wmui's - see CROSS-FILE). Per Task Manager refresh.

### sched.inc and clock.inc (`.text`)

16. **`sch_pit_now` builds the count in AX** (-4 / -4): the low byte parked
    in AH, `xchg al, ah`, `neg ax`; CX is never borrowed. **~27 cycles fewer
    a read** (it paces `wm_anim`'s yields and `sch_fast_off`'s re-seed; not
    on the IRQ0 path, whose copy in `sch_account` is untouched).
17. **`sched_stop` on `kent_bp`, with ONE interrupt window** (-5 / -5).
    Pass 17's held item (ui.md: "two interrupt windows, -3") plus the
    ladder (-2). The PIT channel-0 reset now runs in the vector's window,
    before `mouse_unhook` and `snd_unhook` rather than after; neither
    touches channel 0 or its vector, and the three OUTs stay atomic
    (SPEC.md 8.1). Shutdown and reboot only.
18. **One parity pad, +1** (never executed, after `sched_unhook`'s far
    jump): items 16 and 17 are odd together, and `sch_isr` / `sch_switch`
    are pinned. **The next odd byte saved between `cur_move_1` and
    `sch_isr` takes it with it** - that interval is `mouse.inc`'s tail
    (draw's) and `sched.inc`'s head.
19. **`str_len` leaves SI one past the NUL** (-1 / -1), pass 17's held
    shape (`mov cx, si / not cx / cmp byte [si], 1 / inc si / jnc / add cx,
    si`): its one caller, `toast_place`, loads SI itself and reads it no
    further. Paired with **`clk_tick`'s `inc ax` for `or al, 1`** (-1 / -1;
    AX is 0 there) in the same pinned interval (`sch_switch` ..
    `wm_paint_all`). str_len: 3 instructions a character against 5.

kern_big total: 8 + 4 + 4 + 8 + 5 + 9 + 2 + 7 + 1 + 2 + 2 + 6 + 10 + 2 + 4 +
4 + 5 - 1 + 1 + 1 = **84**.

**Files in no row taken here**: none - every file touched is in sys's row
(`kernel.asm` gained only an assembly-time guard).

**Parity.** `hotpar` is green on kern_big and kern_small at every commit.
Each pinned interval's sum is even: `[cur_move_1, sch_isr)` -4 -5 +1;
`[sch_switch, wm_paint_all)` -1 -1; `[gfx_blit1_x, dsk_xfer)` -4;
`[dsk_xfer, dsk_runcap)` +38; `[dsk_runcap, dsk_next_clus_x)` -45 -1 -2 -2
-6. One new pad (item 18).

**Equivalence.** Beyond the rows: a scratch unicorn harness ran the base and
tip bodies of `str_len` (3,000 random strings of 0-300 bytes: CX, and SI one
past the NUL on the tip) and `sch_pit_now` (3,000 random latched counts,
`[ticks]` and incoming CX: AX, DX, CX and SP) - **0 differences**; each tip
body broken on purpose (`cmp byte [si], 2`, `not ax`) reports 2,544 and
3,000 mismatches, so the harness can fail. The rest was proved by reading,
each caller named above. `tools/stkbalance.py` over every touched file
reports the same count at the base and the tip (disk.inc 8 and 8;
diskw/loader/clock/sched/disk 10 and 10; disk/memory/kernel.asm 14 and 14):
`dskw_zerobuf`'s KENT-plus-`push es` finding went and `sched_stop` now shows
that same accepted shape (`tail jmp to kret_es at depth +1`, as
`disk_mount_x`, `dskw_imgro` and `dskw_flushrun` do).

## REFUSED

1. **`mod_needi`** (`call mod_needi / db id` for `push ax / mov al, id /
   call mod_need|mod_drop / pop ax`). Counted on kern_big's listing: 12
   seven-byte sites (-36) - not ~15 - and the cheapest trampoline that reads
   the inline id, keeps AX and serves both need and drop is ~29 bytes
   (`stc`/`clc` entries, `xchg si, [bp+2] / cs lodsb` twice, one CF branch
   to each), so ~-7 net, for edits in nine files across four other rows.
   The five `mov al, id / call` sites that keep no AX would each read a
   different AL afterwards. Not built.
2. **`sch_account` through `sch_currec` and a `shr`** (-5): ~+40 cycles on
   every tick's charge (H2). The speed rule.
3. **`dsk_batch_end_x` as `cmp byte [x], 1 / adc byte [x], -1`** (-1): it
   would answer CF = 1 where the cell answers CF = 0.
4. **`dsk_media_ok` reaching the BDA as 0:440h** (-1): on kern_mega 40h is a
   selector and 0 the NULL one; it needs an arm of its own.
5. **`ld_start`'s relaxed `jne .rehome` made short by moving `.rehome` above
   `.abort`**: `jc .abort` relaxes instead; 0 net.
6. **`dsk_bpb_sig` with `es lodsw`**: the loop is a byte shorter and the
   result needs an `xchg` back; 0 net.
7. **`clk_snapshot` as one `rep movsb`** (~-2 with `clk_sn_*` re-laid out
   to mirror the clock fields): the copy needs ES = DS and four banks;
   CTRL.DRV reads the snapshot by symbol. Not worth the layout change.
8. **A dispatcher for `hiber.inc`'s six `hbf_*` thunks** (~-10 estimated):
   the six have three different refusal tails (`ret`, `retf`, the toast) and
   two drop rules; not built.
9. Still held for parity, unchanged: **`dsk_fatw_pick`'s `jmp short .out`
   to a `ret`** (-1, between `dsk_runcap` and `dsk_next_clus_x`, which is
   even now with items 8-12; any other odd byte in that span takes it) and
   **`mem_region_reloc`'s `cbw / xchg si, ax`** (-1, between `wm_paint_all`
   and `ui_task`, where memory.inc's `.text` has nothing else; any odd byte
   in clip/instance/menu/fprog/toast takes it).
10. Pass 16's and 17's REFUSED items in these files still hold and were not
    re-proposed; the placement lever is still spent (nothing in these files
    gained a routine since pass 17 that runs only at boot).
11. **Not built, priced for a later pass**: the 22 resident `.DRV` file names
    (`mod_f_*`, `drv_f_*`, `xm_fname`) end in the same `'.DRV', 0` - ~110
    bytes - and a stem plus one appended suffix would be ~-80. But the names
    are shown (the Control Panel's driver pages, error strings),
    `tools/os88rom.py` and the ROM adoption key on them, and SYSTEM.CFG may
    name them; it is a design, not a size item.

## WANTS THE OWNER

None. No behaviour changed; every refusal path answers as it did. The one
ordering change (item 17: channel 0 reset before the two unhooks rather than
after them) changes no end state.

## WANTS AN ABI BREAK

None found in this row.

## CROSS-FILE

1. **`drv_load_at`** (main's, fresh-routines.md): the `pop bx / push bx`
   between `mem_movable_x` and the read is dead - BX is zeroed for the read
   and the stacked row is popped after it, and `mem_movable_x` keeps DX for
   the `mov es, dx` (-2).
2. **Parity partners for the coordinator**: item 18's pad (any odd saving
   in `mouse.inc`'s tail after `cur_move_1` or `sched.inc`'s head retires
   it); `dsk_fatw_pick`'s -1 (any odd in `[dsk_runcap, dsk_next_clus_x)`,
   which holds hot's `disk_mount_x`/`dsk_fatw_demote` and dir's
   `dsk_sdcmp`/`osapi_fs_ent_x`); `mem_region_reloc`'s -1 (wmui's
   clip/instance/menu/fprog/toast).
3. **`kernel.asm`** (wmui's): an `%if ... %error` guard beside `KBUF_KB` for
   item 15, no bytes. A merge with wmui's `kernel.asm` work should keep it.
4. `mod_needi`'s sites (REFUSED 1) are in ctrl, desksc, diskops, deskmod,
   fdlg, filecp, hiber, serial and diskw.

## ROWS RUN

Once, after the last code commit (`e62159e9`), with the tree not edited
while they ran: `python3 tools/os88test.py soak --marty-jobs 1 -j 2` scoped
to 28 rows: **27 passed, 0 failed, 1 skipped** (`win16file`, which needs
the Win16 fixtures this container has not got) in 1,614 s. `fatwpin`
(234 s against a declared 30) and `lzship` (167 s against 30) ran long
under six other agents' load and passed.

| rows | what of this branch they reach |
|---|---|
| `navcalls` | the int 13h CALLS of opening a root, entering a folder and Up on 360KB and 720KB (the speed rule's bar): items 5, 8, 11, 12 |
| `bigdir`, `curdisk`, `dirwshed`, `fatwpin` | the listing walk and FAT window: items 5, 7, 8, 9, 12 |
| `imgmount`, `imgswap`, `rdmount` | image and RAM-disk volumes: New Folder on an image (item 1), `dsk_vol_fixed_x` (9), the redirected chain read (6) |
| `dskwstage`, `dosbar`, `viddisk`, `wseq`, `wseqheld`, `wseqioerr` (and `win16file`, skipped) | the staged sectors, READ_AT and READ_SEQ, the write sequences: items 2, 3, 4, 6 |
| `lzship`, `ldcost`, `smalllaunch` | launches, compressed and plain, both kernels: items 8, 13, 14 |
| `shutdown`, `dispreboot`, `tickzero` | `sched_stop` and the PIT (16, 17) |
| `tmload`, `tmsmall` | `osapi_sys_kb_x` (15) |
| `toastbar`, `dtfield` | `str_len` (19), the clock (19) |
| `hddgeom`, `hdboot` | the fixed-disk cylinder bound now written in `dsk_xfer` (8) |
| `hibernate` | `hb_pick_x` through `dsk_vol_fixed_x` (9) |

`make`, `make small`, `make emu` and `make mega` build at the tip; the fast
tier is 73 passed, 1 skipped (w16cens, fixtures); `hotpar` green on both
kernels; `megaseg` green (inside `make mega`).

## DEFECTS FOUND

1. **`docs/INDEX.md` was stale at the brief's commit** (`c5a7b0f9`), which
   fails `make` in a fresh worktree; regenerated in the first commit (pass
   16's and 17's defect, a third time).
