# ksp15 agent "fsys" - a general finder over the file side

Kernel size pass 15 (`docs/plans/completed/ksp15/BRIEF.md`, row `fsys`): the
shapes of ksp14 BRIEF 0.2 over `disk.inc`, `diskw.inc`, `files.inc`,
`fdlg.inc`, `filecp.inc`, `loader.inc`, `mod.inc`, `driver.inc` (not blob's
lines), `lz.inc`, `compress.inc`, `hiber.inc`'s resident half, `serial.inc`,
the keyboard and the clock. Branch `ksp15-fsys`, base `49ad5d67` (the brief,
on `e4896481`). Figures are ASSEMBLED bytes, `tools/kernsize.py --json`
absolutes; resident = `.text` + `.bss` + `.cold` + `.lowbss` + `.vgabuf`
(+ `.mega` on kern_mega).

**The finding**: the file side's single-caller helpers carry FRAMES their one
caller never reads. `disk_mount_x` is framed (`kentc_bp` + ES) and calls nine
helpers in a row that each bank their own registers again; five of them
(`dsk_bpb_check`, `dsk_bpb_sig`, `dsk_bpb_bank`, `dsk_fatw_want`,
`dsk_fatw_pick`, and AX in `dsk_fatw_park`) are called with nothing live
across them. The same held in `drv_load_at` (`drv_check`/`drv_bss`), in
`fm_editkey` (`fm_edit_commit`, `fm_extkeep`) and in the listing sort. The
method that found them is mechanical: list every single-caller routine whose
first instruction is `kentc_*` or a `push` (a reference walk over `kernel/`,
`kerndos/`, `boot/` and `drivers/`), then read what the caller holds across
the call. About two-thirds of the candidates fail that reading (the caller
does carry a register) and are listed under REFUSED.

## BASE AND TIP

| | base `49ad5d67` | tip | delta |
|---|---:|---:|---:|
| **kern_big resident** | 90,008 | **89,862** | **-146** |
| kern_big `.text` / `.bss` / `.cold` / `.lowbss` / `.vgabuf` | 41,136 / 5,398 / 37,780 / 5,358 / 336 | 41,136 / 5,398 / 37,634 / 5,358 / 336 | 0 / 0 / -146 / 0 / 0 |
| kern_big `.boot2` / `.ovl` / `KERN_SIZE` | 6,447 / 5,110 / 97,280 | 6,447 / 5,110 / 97,280 | 0 |
| **kern_small resident** | 58,220 | **58,128** | **-92** (`.cold` 21,581 -> 21,489) |
| kern_small `.boot2` / `.ovl` / `KERN_SIZE` | 4,565 / 1,482 / 59,904 | unchanged | 0 |
| **kern_emu resident** | 90,264 | **90,118** | **-146** (`.cold`) |
| **kern_mega resident** | 124,192 | **124,046** | **-146** (`.cold` 44,957 -> 44,811; `.mega` 18,237 unchanged) |

No rung moved on any kernel. `.bss`, `.lowbss`, `.boot2` and `.ovl` are
untouched. No module or driver image changed (every change is in resident
`.cold`; `DISKOPS.DRV`, `FORMAT.DRV`, `CTRL.DRV`, `HIBER.DRV` and kern_small's
`FILECP.DRV`/`FDLG.DRV` are byte-identical). kern_small's -92 is the same
items less the ones assembled only on kern_big (`OS88_REDIR`'s
`dskw_mounted` fold, `OS88_DRIVERS`'s `drv_*`).

## TAKEN

Bytes are kern_big / kern_small resident (`.cold` throughout). kern_emu and
kern_mega move exactly as kern_big.

### 1. `disk_mount_x`'s helpers (-46 / -46)

`disk_mount_x` is `kentc_bp` + `push es` and carries **BP and ES alone** past
the BPB/FAT-window block (every register after it is reloaded before it is
read: `.fat_ok` loads AX, the scan loads ES/SI/CX, `.quiet` loads BP and AL,
`.fail` and `.publish` read memory). So:

| routine | change | bytes |
|---|---|---:|
| `dsk_bpb_check` | no `kentc_di`/`push es`, exits are `ret` (it never touched BP; ES is not read after `.staged`) | -8 |
| `dsk_bpb_sig` | no AX/CX/SI bank (`dsk_bpb_check` runs next and spends all three) | -6 |
| `dsk_bpb_bank` (get/put) | no `kentc_di`/`push es`; GET copies at its own site (+1, below) | -8 |
| `dsk_fatw_want` | no `kentc_di`; `.out` is `ret` | -5 |
| `dsk_fatw_pick` | no AX/BX bank | -4 |
| `dsk_fatw_park` | no AX bank (the mount `xor ax, ax`s next; BX and DX kept - DX is the new drive) | -2 |
| `dsk_fatw_pinned`/`dsk_fatw_evict` | the scan both spelled is `dsk_fatw_scan`; pinned is written in `dsk_fatw_want` as the scan (CX/SI are the want's to spend); evict re-scans until none holds the pin, so even a second holder (impossible by 18.8.3) still goes | -14 |

### 2. `dskw_mounted` answers ZF = redirected (-21 / 0)

Where `OS88_REDIR` is built, all five callers (`dskw_rtbody`, `dskw_wbody`,
`dskw_rbody`, `dskw_nbody`, `dskw_wabody`) made `cmp byte [dsk_vkind],
DVK_FILE` straight after it. It now ends `jne .no / cmp byte [dsk_vkind],
DVK_FILE / ret` (+6) and each caller loses its 5-byte compare;
`dskw_wabody`'s `jnc .gated / .out: ret` arrangement goes too (its shared
`ret` moves under the redirected arm). kern_small (no `OS88_REDIR`) assembles
the same bytes as before.

### 3. The listing sort (-12 / -12)

`dsk_sortdir` drops `kentc_di` (its one caller, the mount, carries only BP and
ES past it, and `dsk_icopass`'s note already records that `.done` reads BP
alone), and `dsk_swapent`, its one callee, is written at the call with CX
banked round the 32-byte exchange.

### 4. The driver loader (-34 on kern_big)

* **`drv_check` leaves ES = the image** (-3). `drv_load_at`, its one caller,
  already holds ES = the claim (it read the file into it), and `drv_bss`,
  `drv_hi` (`in: ES = its image`) and the attach half's `ROMF_HPLACE`
  (`ES = the checked image`) all want ES there - so the bank only ever put
  back the value it had loaded. The failure path's `kretc_es` restores the
  caller's ES as before.
* **`drv_bss` drops `kentc_di`, `push es` and its own `mov es`** (-12, with
  `.nofit`'s `jmp short .out` a `ret`): the attach that follows reads BX and
  ES and nothing else (`%1_att` loads every register it uses).
* **`drv_svc_clear` written in `drv_release`** (-19): release had just called
  `drv_cls_fp_x` and zeroed the segment half itself, then called a routine
  that did both again; the owner slot is kept from release's own `drv_cls_idx`
  (`push di`/`pop di`) and the service table cleared with AL banked, because
  `dsk_vol_drop_drv_x` takes the class in AL. Store order unchanged: fptr
  segment, owner, table, then `cp_drv_gone_x`.

### 5. The Disk window (-22 / -22)

* `fm_edit_commit` (one caller, `fm_editkey`, banks AX..DI round it) drops
  its frame, and its `.done: call fm_edit_end / jmp kretc_di` is `jmp
  fm_edit_end`; `fm_extkeep` (one caller, the commit's `.ren`, which reloads
  SI and DI and reads nothing else) drops its frame (-11).
* `fm_icon_blank` written in `fm_draw_icon16` as a `[si+bx]` walk from the
  top, BX being the icon painter's own banked register (-9; fewer clocks per
  icon too - no call, no frame, one `dec`/`dec`/`jns` against `inc`/`inc`/
  `loop`).
* `fmv_sync_x`'s `push si`/`pop si` round `fm_settitle0` went: the next
  instruction is `jmp kretc_di`, which restores SI anyway (-2).

### 6. The KENT ladder and a held register (-13 / -13)

* `ld_lname_cp` and `dskw_rt_byclus` banked four registers each and return
  nothing in any of them: `kentc_di` + `jmp kretc_di` is six bytes against
  nine (-6). `dskw_rt_byclus` now restores AX too, which its header listed as
  clobbered and no caller reads.
* `dsk_free_clus_x` holds `[dsk_maxclus]` in CX through the FAT12 walk and
  `.slow` (`dsk_next_clus_x` keeps CX; `.f16` spends CX and still reads the
  word): -4, and the FAT12 loop's two compares are register compares.
* `mod_check` banked BX round a body that never writes it (-3).

### Hot paths and parity

Nothing on the brief's hot list changed content (`dsk_xfer`'s per-sector
path, `dsk_runcap`, `dsk_next_clus_x`, the mouse/scheduler paths). **One
parity pad, of a kind**: `mod_check`'s -3 and `dsk_bpb_bank`'s -8 sit in front
of `dsk_xfer`, `dsk_runcap` and `dsk_next_clus_x`, and put all three on an odd
shift (`hotpar` red on both kernels). `dsk_bpb_bank`'s GET arm now ends with
its own `rep movsw / ret` where it jumped to the shared one (+1) - a byte
that is also four clocks faster - and `hotpar` is green on kern_big and
kern_small at every commit. No pad byte was added.

## EQUIVALENCE

By reading, routine by routine, recorded above: for every frame removed, the
one caller (confirmed by a reference walk over `kernel/`, `kerndos/`, `boot/`
and `drivers/` - kerndos assembles `disk.inc`/`diskw.inc` but none of the
changed routines is reached from it except `dskw_mounted`, whose kerndos
arm has no `OS88_REDIR`) and every instruction between the call and the next
write of each register were read. `tools/stkbalance.py` over the six files
touched, base against tip: the same 12 known tail-`jmp kret*` paths, none new.

## REFUSED

* **`dskw_sync_x` written in front of `fmv_mark`** (-4): kerndos assembles
  `diskw.inc` without `files.inc` (its `kdshim.inc` supplies `fmv_mark`), so
  the routine cannot move files; the tail-jump spelling is -1 and was not
  worth the parity check.
* **`fm_drag`, `fmv_take`, `fmv_mark`, `fmv_reload_all`, `fm_find_view`,
  `fm_front_win`, `dsk_get_dir_x`, `dsk_ico_stage`, `fmv_get_dir`** (frames or
  banks, 3-6 each): each caller DOES carry a register across (AX = drive and
  DX = cluster into `fm_seed`/`fmv_load`, AL into `cmp al, FCPS_ERR`, DI into
  `FS_DIRTY`, SI as an output) or the routine answers in a register the KENT
  ladder would restore.
* **`dskw_rt_scan`'s bank** (-11 with `dskw_find.out`): `dskw_dbody_n83`,
  which follows it in the walk, documents SI as an input; not proven dead.
* **`disk_mount_x`'s own `push bx`/`pop bx` round `dsk_vol_row_dl`** (-2): BX
  could reach a redirected volume's `FSV_CHDIR`/`FSV_ENUM` through
  `drv_fs_call`; not proven across every DRVC_FILE driver.
* **`fdlg_btn` written at its call** (kern_big `.cold`): it leaves through
  three external targets that `ret` (`fdlg_post_cancel`, `fdlg_beep`,
  `fdlg_save`), so the call stays.
* **`mod_disarm`'s called `.slot` loop written out** (-4): the call is pass
  14's parity pad for `mem_bytes_kb_x`, and -4 there would put pass 14's pair
  back on odd.
* **`dskw_dotents`'s two time/date runs as a loop**: the field order (skip,
  T, D, D, skip, T, D) is not periodic; at most -2.
* **String tails**: the only suffix pair in this row's files (`fm_s_folder`
  inside `fm_s_up`) is already the tail of `fm_s_rootf`; the module file
  names stay whole (pass 14's refusal: `os88rom.py`/`os88mod.py` read them).

## WANTS THE OWNER

1. **Ship the LZ4 decoder alone** (`COMPRESS=lz4` as the default): **-100
   resident** on kern_big and on kern_small (`.cold` 37,634 -> 37,534 /
   21,489 -> 21,389), MEASURED. Every shipped disk is LZ4 (`PKGZ ?= lz4`); the
   LZB decoder is what lets a machine READ an LZB file somebody else made and
   is why the Makefile calls `both` "a door". It is a functionality change - an
   LZB-packed file would no longer open - so it is the owner's.

## WANTS AN ABI BREAK

None. Every interface touched is kernel-internal (`dskw_mounted`'s flags out,
`drv_check`'s ES out, the frames of single-caller routines).

## CROSS-FILE

* `disk.inc`'s `dsk_fatw_*` and `ico_*` share nothing changed here; the
  recent agent's `ico_*`/`dsk_rah_reloc` were not touched.
* `kernel/ctrl.inc`'s `cp_drv_gone_x` (blob's file) still banks AX for
  `drv_release`, which now needs it: unchanged.
* SPEC.md 17315 and 112070 name `drv_svc_clear` as history (what pass 12
  changed in it); the body is `drv_release`'s now and the comment there says
  so. Left as the record.

## ROWS RUN

Once, at the tip, after the last code commit, `-j 1 --marty-jobs 1`:
`python3 tools/os88test.py soak -j 1 --marty-jobs 1 -k fatwpin -k rdmount
-k imgmount -k drvmove -k lzdrv -k drvcall -k kernmods -k lzmod -k fmcommit
-k fdlgchoose -k shedrelist -k rdicon -k hddcp -k fcpcopy -k small128 -k
imgswap -k instassoc -k fmthumb -k assocflush -k hmadrv` - **20 passed, 0
failed, 0 skipped** (677.9 s; `fatwpin` took 154.7 s against 30 declared, on a
box shared by five agents, and passed).

What each subject is covered by: the mount's helpers (BPB check, sig, bank,
FAT-window want/pick/park/evict) - every row boots and mounts, and `fatwpin`,
`imgmount`, `imgswap`, `shedrelist`, `small128` (kern_small on its 128KB
machine) drive the window and the bank; the redirected arm - `rdmount`,
`rdicon`; the sort - every listing (`fmthumb`, `fdlgchoose`, `instassoc`);
the driver loader - `drvmove`, `lzdrv`, `drvcall`, `hmadrv` (the HMA
placement, which reads ES); modules - `kernmods`, `lzmod`; the Disk window's
commit, icons and copy - `fmcommit`, `fmthumb`, `fcpcopy`, `hddcp`,
`assocflush`.

**Broken on purpose**: `dskw_mounted`'s new compare against `DVK_BIOS`
instead of `DVK_FILE` (every floppy read as redirected) - `lzmod` went
**red** (a module read through `dskw_rbody`), and green again restored;
`fmcommit` stayed green, because its three verbs (New Folder, Delete, a typed
character) enter through `dskw_gate` and not `dskw_mounted`. No row renames a
file in the Disk window or writes to a redirected volume (pass 14 found the
first gap; this is the second), so `dskw_nbody`'s and the redirected arms'
use of the fold is equivalent by reading.

The fast tier (66 rows, `hotpar` and `asmrules` among them) green at every
commit; `make small`, `make emu` and `make mega` build at the tip.

## DEFECTS FOUND

None in shipped behaviour. **Coverage gaps**: no row writes to a redirected
volume (`dskw_wbody`/`dskw_nbody`/`dskw_rtbody`'s `FSV_*` arms) and, as pass
14 found, none renames a file in the Disk window.
