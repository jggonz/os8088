# Kernel size pass 18: dir (the 1,024-entry Disk window, the Shell sort, the TOASTSTR sweep's kernel side)

The **dir** agent's record (BRIEF.md section 2). Owned: every `dir` row of
`fresh-routines.md` - elendilon's big-directory series (`69cb417c`,
`15803099`, `82fd9bd1`, SPEC.md 22.6.4), the Shell sort (`e2c2f85f`, SPEC.md
19.4.1) and the TOASTSTR sweep's kernel strings (`a3acd430`, `d367c543`).
Branch `ksp18-dir`, cut from `c5a7b0f9`. Everything taken is in `.cold` on
every kernel, so nothing on `.text` moved and no `hotpar` routine shifted.

## BASE AND TIP

Assembled, `tools/kernsize.py --json` (kern_mega off `make mega`'s
`kernsize[mega]` lines). Resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf` + `.vga` (+ `.mega` on kern_mega).

| kernel | `.text` | `.bss` | `.cold` base -> tip | resident base -> tip | delta | `KERN_SIZE` |
|---|---:|---:|---:|---:|---:|---|
| **kern_big** | 40,633 | 5,376 | 38,034 -> **37,954** | 91,767 -> **91,687** | **-80** | 97,280, unchanged |
| kern_small | 30,232 | 2,946 | 21,287 -> **21,265** | 57,333 -> **57,311** | **-22** | 59,392, unchanged |
| kern_emu | 40,877 | 5,376 | 38,034 -> **37,954** | 92,011 -> **91,931** | **-80** | 97,792, unchanged |
| kern_mega | 46,307 | 5,414 | 46,573 -> **46,493** | 127,276 -> **127,196** (`.mega` 23,072) | **-80** | conventional 63,488 of 68,096, unchanged |

`.lowbss`, `.vgabuf`, `.vga`, `.boot2` (6,456 big) and `.ovl` are unchanged on
every kernel. `megaseg` stays at 3,682 (the `.text` overhead is untouched).
No module or driver image changed.

Against what the series added (kern_big +242 resident, `82fd9bd1`'s message,
plus the sort's +6): **-80, a third of it**, with the 1,024-entry behaviour,
the claim's grow/cut/keep rules and every refusal unchanged.

## TAKEN

kern_big bytes; kern_small, kern_emu and kern_mega follow kern_big except
where a figure is given. H-grades are khp1's (BRIEF.md 1, khp1 BRIEF 1).
Cycles are the 8088's `max(clocks, 4.34 x bytes)` per instruction, from the
Intel table with the 8088's +4 a word transfer, unless marked MODELLED
(a unicorn run of the two versions' own source under that cost table).

1. **`fmv_cap`, `fmv_need` and `fmv_rsz` written in `fmv_load`, their one
   user (-25).** The resize banked AX/BX/DX that `fmv_load`'s KENT frame
   already holds; its "already that size" `stc / je .no` and the caller's
   `jc` become one `je .done`. The floor `cmp al, VIEW_KB / jae / mov al,
   VIEW_KB` is `cmp al, 1 / adc al, 0` (VIEW_KB is 1, and the guard that
   already said so now says why). A grow re-entered `.again` with `push bx`
   of the BX = 0 the resize had restored; `fmv_owner` now spends BX, so the
   grow banks AX - its KB, <= 32, so bit 6 (ZF's) is clear and it reads
   "navigated" exactly as 0 did - with a new `%error` holding that. The
   capacity is `mov al, [FS_VKB] / cbw / mov cl, 5 / shl ax, cl` (CX is
   nobody's until the `[dsk_nover]` read). H4, per listing open: ~-190
   cycles (three call/ret pairs and three push/pop pairs gone, +22 for the
   shift by CL) on an operation of ~1 s.
2. **`fmv_take` written in `fmv_bcast`, its one caller (-17, every
   kernel).** The source pointer is BX, re-read each pass (`dsk_dest_x` spends
   it; SI is the instance walk's); the `[fm_vp] == 0` and `== DI` tests go,
   because `fmv_bcast` made both before the call and nothing on the path
   writes `[fm_vp]` (its one writer is `fm_vp_set`, called from `fm_kinit_x`,
   `fm_focus_x`, `fm_choose` and the paint entries only). On kern_small the
   unconditional `FSD_PIXELS` mark stays where it was, after the adopt step
   whatever its outcome. H4 (per write with a sibling on the folder): faster
   by the frame and the call.
3. **`dsk_sdcmp` written in the Shell sort, its one caller (-12: -7 the
   inline, -5 the guard below).** Inside the insertion step DI = j - gap and SI =
   j, so `[es:bp+di]` IS SI's byte: only DI walks, SI needs no bank and no
   `inc`, and the call and ret go. **The sort's `cmp cx, 2 / jb .sd_out` is
   gone too**: the inlined compare pushed `.sd_out` past short reach (+3), and
   the test was never needed - with 0 or 1 entries the span is 0 or one
   entry, the gap climb's first step is not below it, so the first gap is 0
   and `.sh_done` is taken before any entry is read; every register the test
   would have kept is one the sort spends at every other count, and nothing
   past `.sd_out` reads them (read: the icon blank, `.harvest0`, `.fsicons`,
   `.harvested`, `.publish`). **Equivalence: a unicorn harness assembled both
   versions from their own source** (`c5a7b0f9`'s sort + `dsk_sdcmp` +
   `dsk_upper`, and this tree's) and ran 420 random listings - counts 0 to
   300 and 1,000/1,024, bases 0/24/128/152, names sharing prefixes,
   duplicates, mixed case, garbage after the NUL, type words 0-3 and 42h:
   **identical bytes out every time**, BP and BX restored. **Speed (H4,
   every listing open), MODELLED: -4.7% at 1,000 entries** (22.12M -> 21.09M
   cycles), -5.1% at 32, -8% at 2; **+255 cycles (53 us) at 0 or 1 entries**,
   the cost of running the gap climb instead of the test - 0.005% of a
   ~1 s folder open. (The model is slower in absolute terms than
   `e2c2f85f`'s MartyPC 2.74 s, because its names share a long prefix; the
   ratio is the claim.)
4. **`fmv_iofs` (-5)**: the stride as the multiply's immediate (`mov ax, 24
   / mul word [di+FS_N]`, -2 against loading DX) and one `add ax, FV_DOFF +
   127` for the bitmap and the boundary (-3). ~-12 cycles a call; per listing.
5. **`osapi_fs_ent_x` (-2)**: the full arm's `inc word [dsk_nover]` sits on
   `.no`'s refusal tail instead of a `jb` round it and a `jmp short` back.
   H4, per redirected entry: the stored path is a not-taken `jae` (4) where
   it was a taken `jb` (16), -12 cycles an entry.
6. **`fm_mdelarm` (-3)**: the count word split with `shr dx, 1` (ZF = no
   entries, CF = the folder bit) and `mov al, 0 / adc al, al`; the registers
   it leaves are bit for bit the old ones (DX's low byte is 0/1 either way).
   H3, per Delete armed: neutral.
7. **`fm_mdiff` and `fm_mxor` lend DS, not ES (-2)**: DS is the default, so
   the `es:` override goes. An ISR loads its own DS (CLAUDE.md's register
   discipline), so the one instruction with DS lent is safe for the same
   reason it was with ES. H3: one byte and 2 cycles less per bitmap touch -
   `fm_mset` walks FS_N of them a gesture, ~2,000 cycles at 1,024 entries.
8. **`fm_mflip` (-5)**: `fm_mbit / mov cl, ch / fm_mxor` was spelt out at
   Ctrl+click and in `fm_menter`; it is an entry falling into `fm_mxor`
   now. H3, per gesture: +one call/ret (~43 cycles) against a band redraw of
   ~10 ms (47,700 cycles) - 0.09%.
9. **`dsk_dest_0` (-4 big, -2 small)**: the `xor dx, dx` the three NOWHERE
   retires (`fmv_load`, `fmv_icorefs`, `fmv_bcast`'s adopt step) spelt at the
   call, as an entry falling into `dsk_dest_x`; on kern_small a `jmp short`,
   because `dsk_dest_b0` holds the fall-through there (+15 cycles, three a
   listing). DX is 0 on the way out, as it was.
10. **`fm_many` (-1)**: the claim tested with `cmp [FS_VSEG], ax` against the
    zero it then uses. **+16 cycles a call**: the costliest caller on a
    repaint is `fm_lsel_bar` (once a paint or selection change, ~10 ms) -
    0.03%. **`fmv_icorefs` (-1)**: BP from the FS_N it has just loaded for the
    aim (`dsk_dest_x` keeps AX), -19 cycles.
11. **`fm_draw_rcore`'s icon grid (-3, both kernels)**: the row is stored at
    the loop head, so the step is `mov ax, [fm_vrow] / inc ax` and not an
    `inc` of memory reloaded. **H3 (files-2's PgDn tier and every grid
    repaint): ~8 cycles a grid ROW faster**; the list path and the per-cell
    body are byte-identical.

**No `int 13h` call was added to anything**: no item touches a mount's
reads, the read-ahead, the FAT window or the loader - the sort, the claim
arithmetic and the adopt step are memory-only. `navcalls` (MartyPC's FDC
counters, SPEC.md 18.95.11) is in ROWS RUN and is the count.

Parity: every routine above is in `.cold`; `hotpar`'s list has none there,
and `.text` is byte-identical, so no hot routine moved. `fm_draw_rcore` stays
at an even shift (-74); `fm_mdiff`'s run is at an odd one (-77), which an
8088's 8-bit bus does not see.

## REFUSED

* **The swap's four `inc`s as `cmpsw`** (-3): a dummy compare advances SI
  and DI by two in one byte, but CMPSW is ~30 clocks on the 8088 against ~16
  for the four `inc`s - ~170 cycles a swap, about 7% of a 1,000-entry sort.
* **kern_small's selection sort without its `cmp cx, 2` guard** (-5): there,
  0 entries make `CX = BX - 24`, which underflows at a base of 0 into ~2,700
  empty outer passes. Only the Shell sort's guard was redundant.
* **`fm_many` / `fm_mzero` choosing their string op by CF instead of BL**
  (-2 at best): every flag-free spelling needs CF read before the claim's
  `or`/`cmp`, so the zero test is written twice or a `pushf` frame is added;
  nothing shorter found.
* **`fmv_iofs` answering FS_IOFH units directly** (-2): drops the `shl bx, 1`
  in `fmv_store` and `fmv_icorefs` but adds two instructions to
  `fmv_get_icon`, which is per icon drawn; two bytes do not pay for touching
  the per-icon path.
* **`dsk_ioffx`'s `add ax, [dsk_doff]` as `add ax, bx`** (-2): true from
  `dsk_dest_x`, but the mount's own call has BX = `[dsk_doff]` + 24 inside a
  folder (the parent link is not in the sort).
* **The scan's over-count in `disk_mount_x` on a shared tail** (0): to put
  `inc word [dsk_nover]` in front of `.skip` the stored path needs a `jmp
  short` round it - the same eight bytes. (It is also `hot`'s routine.)
* **Stopping the name compare at the first pair of equal NULs**: a speed
  idea, not a size one, and NOT byte-identical - a redirected driver's entry
  may carry bytes after the NUL (`osapi_fs_ent_x` forces only byte 15), and
  two equal names would then sort by them as today. Not proposed.
* **`fm_armset`'s record-count arithmetic** (`mov cl, 6 / shr / inc`) and
  **`fm_open_sel`'s `inc byte [ui_post]`** (+1 over a `mov`): the shortest
  forms that are right - AL is 0 at index 255 + 1, which is why the `inc`
  is there.
* **The TOASTSTR sweep's kernel strings**: read every one of the 60 kernel
  `TOASTSTR`s. No message is a suffix of another, and the two duplicate texts
  (`'Not enough memory'` as `drv_e4` and `cmz_s_nomem`; `'Not loaded'` twice
  in `driver.inc`) are in a module image or in exclusive `%if` arms, not two
  resident copies. A label into the middle of a `TOASTSTR` would also need
  `tests/toastdyn.txt`. Nothing to take; no message reworded.

## WANTS THE OWNER

None.

## WANTS AN ABI BREAK

None: nothing here is in `apps/os88api.inc`, `drivers/os88drv.inc` or
`apps/cc/os88.h`, and no frozen interface was touched.

## CROSS-FILE

* **`disk_mount_x` is `hot`'s row in `fresh-routines.md`** (its disk-3/disk-5
  hunks), but the Shell sort inside it is this agent's by the brief (item 3).
  Only the sort's own `%ifdef DSK_BIGDIR` hunk was edited - `disk_mount_x`
  761 -> 800 with `dsk_sdcmp`'s 51 gone, -12 net - so a merge with `hot`
  meets in the same routine but not the same lines.
* **`ld_pkg_byname`'s +3 (assigned `dir`) is `d7ac8738`'s** - WIN16 Phase
  B/C's `ASSOC_PWBANK` store, main's work: the immediate store is the
  function, nothing to take.
* `drv_errstr` +2, `toast_now` -2, `cpc_bit` +1 and the `cp_*`/`hbm_*`/
  `sc_*`/`clo_*` string rows are the TOASTSTR sweep's relocation-only
  changes (+0) or its two shortened messages: read, nothing to take.
* `tests/bigdir.py`, `tests/hdnoclaim.py` and `tests/suite.py`'s `bigdir`
  row describe their red runs as "`fmv_rsz` made to refuse every grow" and
  name `fmv_cap`/`fmv_need`; those are now steps in `fmv_load` (SPEC.md
  22.6.4 says so). Left as the record of how they were verified; the same
  break today is a `jmp .done` at the resize's `cmp al, [di+FS_VKB]`.

## ROWS RUN

Once, after the last code commit (`60bc8f3c`'s kernel), with the tree not
edited while they ran: `python3 tools/os88test.py soak -k ... --marty-jobs
1` over **20 rows: 20 passed, 0 failed, 0 skipped** (696.5 s): `bigdir`
`msel` `shedrelist` `dirwshed` `hdnoclaim` `icostore` `icohd` `icogrow`
`icoshed` `ascabsorb` `rdicon` `rdmount` `fmthumb` `fmarrows` `fmbtn`
`fmcommit` `navcalls` `assocflush` `fdlgchoose` `dirwsize`.

What covers what: the 300-entry folder's list, sort, grow, keep-in-place and
cut back, Delete's '300 items' and the launch of index 300 - `bigdir` (items
1, 3, 6, 9); the bitmap, Ctrl/Shift+click and the drag select - `msel` and
`bigdir` (7, 8, 10); the shed and re-list paths through `fmv_load` and
`fmv_icorefs` - `shedrelist`, `dirwshed`, `icoshed`, `hdnoclaim`; the
reference index's shape (`fmv_iofs`, FS_IOFH) - `icostore`, `icohd`,
`icogrow`, `ascabsorb`; a redirected volume's `OSAPI_FS_ENT` and listing -
`rdmount`, `rdicon` (5); the grid and the PgDn tier - `fmthumb`,
`fmarrows`, `fmbtn` (11); a write's re-list and repaint - `fmcommit`;
**the int 13h counts of a navigation - `navcalls`, unchanged**; the chooser's
listing - `fdlgchoose`.

**Not covered by any row: `fmv_bcast`'s adopt step with a SIBLING window on
the same folder** (item 2) - no row opens two Disk windows on one folder and
writes. It is proved by reading (the two dropped tests made by the caller
first; `[fm_vp]`'s one writer off the path; BX re-read each pass; the
kern_small mark kept in place). The Shell sort is proved by the unicorn
harness as well (item 3). `make -j2` (with the fast tier, 73 passed, 1
skipped, `hotpar` green), `make -j2 small`, `make emu` and `make mega` pass
at the tip; `tools/stkbalance.py` over `kernel/files.inc` and
`kernel/disk.inc` reports the base's same ten pre-existing paths and no new
one.

## DEFECTS FOUND

1. **`docs/INDEX.md` was stale at the brief's commit** (`c5a7b0f9` added
   `docs/plans/ksp18/`), so the first `make` in this worktree failed at
   `checkdocs` - pass 17's lesson again. Regenerated in the first commit.
2. **SPEC.md 22's routine table said `fmv_take` was shared by `fmv_bcast`
   and `fm_focus`**; `fm_focus` stopped calling it when its re-list became
   `fmv_load` (SPEC.md 22.8). The row now says the adopt step is written in
   `fmv_bcast`. Found by reading.
3. **Not fixed, latent: `fmv_get_icon`'s fallback `call fmv_iofs` reads
   `[di+FS_N]` on DSK_BIGDIR with DI = the CALLER's DI** (the routine banks
   DI and has `[fm_vp]` in SI). On kern_small `fmv_iofs` reads the global
   `[dsk_nmax]` and is right. The fallback runs only when `[fm_vseg]` is not
   0 and the acting window's FS_IOFH is 0; by reading, no path leaves a
   window with a claim, a non-zero FS_N and FS_IOFH 0 (`fmv_store` writes
   FS_IOFH whenever it has a claim), so it is unreachable today. A fix is a
   `mov di, si` before the call (+2) or dropping the fallback; either is a
   behaviour decision for the Disk window's owner, so it is recorded, not
   taken.
