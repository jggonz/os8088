# ksp18 agent "files" - a general finder over the file manager

Kernel size pass 18 (`docs/plans/ksp18/BRIEF.md`, row `files`): every
routine of `files.inc`, `fdlg.inc`, `filecp.inc`, `desk.inc`, `desksc.inc`'s
resident half, `assoc.inc`, `icons.inc`, `dskwin.inc`, `diskops.inc`'s and
`fsck.inc`'s resident halves, except the routines `fresh-routines.md` gives
to `dir`, `hot` and `main`. Branch `ksp18-files`, base `c5a7b0f9` (the
brief). Figures are ASSEMBLED, `tools/kernsize.py --json` absolutes on each
kernel's own build; resident = `.text` + `.bss` + `.cold` + `.lowbss` +
`.vgabuf` + `.vga` (kern_mega: plus `.mega`, which did not move).

**TAKEN outside the row**: none. `tests/ascabsorb.py`'s docstring named a
trampoline this branch removed, so it is edited with it (no code).
`dskwin.inc` is data and `fsck.inc`/`diskops.inc`'s resident halves are an
API door and three module shims; read, nothing taken.

**The finding**: pass 17's two shapes (single callers, banks no caller reads)
still paid, but the largest new share was **a single caller whose OWN frame
already does the callee's job**. Three ASSOC.DAT parse routines were each a
whole `kentc_di` ladder under `asc_use_x`'s ladder; `fm_paste_res` carried two
the same way; and `fm_draw_status` called `fm_stat_line` and then left by the
identical ladder exit, so it now jumps into the line's body UNDER ITS OWN
FRAME and the line's `jmp kretc_di` is the status painter's. Second: **dead
code a SPEC section said was dead and kept anyway** (the strip pass, -70, see
WANTS THE OWNER). Third, mechanical: `lea` for `mov r, base / add r, imm`,
`xlatb` for a byte table read, `xchg r8, [m]` for a read-then-store.

## BASE AND TIP

| kernel | base `c5a7b0f9` | tip | delta | where | `KERN_SIZE` |
|---|---:|---:|---:|---|---|
| **kern_big** | 91,767 | **91,564** | **-203** | `.cold` 38,034 -> 37,831 | 97,280 -> **96,768** (13 bytes inside the rung below) |
| kern_small | 57,333 | **57,206** | **-127** | `.cold` 21,287 -> 21,160 | 59,392 -> **58,880** |
| kern_emu | 92,011 | **91,808** | **-203** | as kern_big | 97,792 -> **97,280** |
| kern_mega (with `.mega` 23,072) | 127,276 | **127,073** | **-203** | `.cold` 46,573 -> 46,370 (above 1MB) | conventional 63,488 of 68,096, unchanged |
| kern_mega segment overhead (`os88megaseg`) | 3,682 | 3,682 | 0 | - | - |
| kern_small FILECP.DRV image (`.modp`) | 1,886 | **1,871** | **-15** | not resident | - |

`.text`, `.bss`, `.lowbss`, `.vgabuf`, `.vga`, `.boot2`, `.ovl` and `.cfx`
are unchanged on every kernel, so the `.text` delta is 0 and no `.bss` word
moved parity. Every byte is `.cold`, which is RESIDENT - "-203 resident".
`BOOT2_SECS` unchanged. No other module image moved (`DESK.DRV`, `CTRL.DRV`,
`FDLG.DRV`, `DISKOPS.DRV` untouched).

## TAKEN

Bytes are kern_big (= kern_emu = kern_mega) / kern_small, assembled
(`tools/os88rdiff.py kernel c5a7b0f9 .`, with and without `-D KERN_SMALL`).
Commits in order: `e332ac7b`, `da242b84`, `f42408d9`, `159bdcc6`,
`e289ab83`, `2f40a679`, `84eece1f`, and `3ff685c2`, which put back one item
of `f42408d9` that `msel` found wrong (DEFECTS FOUND 1).

### 1. Dead code: the strip pass (-70 / -70) - `e332ac7b`

`fm_scrollpaint`'s strip pass erased and re-iconed the columns a scroll's
inward rounding left behind. `fm_tiles` rounds `[fm_cx]` UP to a byte column,
so `fm_bx1 <= fm_cx + 7`, and the pass's own test was `cmp [fm_bx1],
fm_cx + 8 / jle .expfill`: it could never fall through, and SPEC.md 22.11.1.1
already said so ("The pass is therefore dead code, and it is kept"). The test,
the strip fill, the row loop and `fm_draw_licon` (its one callee) are gone:
234 -> 177 and 13 -> 0. The invariant is now an assembly-time `%if` on a new
`FM_PEN_X` (the inset `fm_pen8` adds, which IS the icon's): the build fails if
it goes below 7, the one change that would need the pass back. SPEC.md
22.11.1.1 and 22.11.1 say so. ~61 clocks a blit-tier scroll fewer (the four
instructions of test). See WANTS THE OWNER: it was a recorded decision to keep.

### 2. Written at the one call, under the caller's own frame

* **The ASSOC.DAT parse into `asc_use_x`** (-24 net / kern_big only) -
  `da242b84`. `asc_merge_ext` and `asc_seed` were `.parse`'s two calls, and
  `asc_row_take` was `asc_seed`'s one per row: three `kentc_di` ladders under
  `asc_use_x`'s, whose `.out` reads no register. Written in, they save 27
  bytes and ~180 clocks a frame - per cache ROW for the take, so up to 90 x
  180 = ~16k clocks (~3.4 ms on a 4.77 MHz XT, PREDICTED) a volume switch.
  The take's SI comes back with one `pop si / push si`. The first build put
  six refusals' short jumps to `.out` past their reach (+19); every refusal
  above `.parse` now lands on the `.rfno` trampoline that was already in the
  routine, which also turned the one LONG jump there short (-3), so the net
  is -24 with no relaxation. `asc_use_out`, the top refusal's own trampoline
  in front of the routine, goes too: that refusal reaches `.rfno` (-3).
* **`fmv_reload_all` and `fmv_repaint_all` into `fm_paste_res`** (-15 / -15)
  - `f42408d9`. Both were called from there alone, each a ladder; the caller
  banks AX and DI itself and clobbers the rest. AL (the FCPS_*) is banked
  round the reload with one push. The pause arm moved FIRST, because the
  pass is too long for the short `je .ask` over it to reach (a 5-byte form,
  measured, then removed). DX joins the documented clobbers: no caller reads
  it (`fm_docmd` clobbers everything, `fm_editkey` and `fm_drag` are ladders,
  DESK.DRV's drop returns through its own).
* **`fm_draw_lrow` into `fm_draw_lrows`** (-6 / -6) - `e332ac7b`. The row
  painter's one caller is the loop; nothing in the body writes DI but the
  size column's own bank (`fm_draw_icon16`, `fm_stage_name`, `font_run`,
  `font_width`, `fm_pen8_di` and `dsk_get_icon_x`/`dsk_ico_stage` all keep
  it), so the loop counter's second bank went with the call. **H3, faster**:
  ~70 clocks a row (call 23, ret 20, push 15, pop 12 on the 8088) - a CGA
  PgDn of 12 rows ~840 clocks, ~0.18 ms of 106 ms (khp1 L, CGA), PREDICTED.
* **`fm_stat_line` entered under `fm_draw_status`'s frame** (-4 / -4) -
  `f42408d9`. `call fm_stat_line / .fs_done: jmp kretc_di` (`.fs_done`
  unreferenced) is `jmp fm_stat_line.inframe`, a label after the line's own
  `call kentc_di`: the two frames are the same shape and the line's body reads
  nothing stack-relative, so its `jmp kretc_di` unwinds the status painter's
  and returns to ITS caller. A ladder frame (~180 clocks) fewer a status line.
* **`fcp_selfchk` into `fcp_run2`** (-8 / FILECP.DRV -7) - `84eece1f`. Its one
  call was `call / jc .loop_err`: the refusals now ARE `.loop_err` (one short
  jump for the exhausted walk) and the pass falls through, so the call, the
  `jc` and both `stc/ret` and `clc/ret` exits go.

### 3. Banks no caller reads (-17 / -6)

* `fm_brect`'s header arm banked BX and SI (-4, kern_big): BX is never
  written there and SI is the column arm's output already; both callers
  (`fm_bhit` banks SI, `fm_btn1` is a ladder) keep it. Header updated.
* `fm_paste_res`'s pause arm banked SI and CX round `fcp_ncopy` (-4 / -4):
  its header already said it clobbers both, and none of its four callers reads
  them (as above).
* `fm_clone_res` re-banked AX/BX round `fm_fmt_home` and `fm_mount`, which
  are ladder routines that keep everything (-2 / -2): one `push ax / pop ax`
  round the `mov al, bh` instead.
* `asc_inkck` banks SI alone (-3, kern_big): its three callers reload or never
  read AX and CX (the glyph take and the row take load their next CX,
  `assoc_reduce` opens on the ladder). ~150 clocks a call fewer.
* `assoc_ext_new` no CX (-3, kern_big): `assoc_ext_take` spends CX itself and
  `osapi_assoc_set_x`'s ladder reads none; the two exits are `stc/ret` and
  `clc/ret`.
* `pth_pop` jumps past `pth_leaf`'s `push ax` with its own AX still banked
  (-1, kern_big).

### 4. Encodings (-38 / -33)

* `fm_setdown`: `xchg al, [fm_dbtn]` is the read of the old id AND the store
  of the new one (-7, kern_big). `fm_btn1` draws the old button up because
  `[fm_dbtn]` already differs from it on every path that reaches that call
  (the `je .out` above leaves only a change of window when they match, and
  that path skips the draw). ~25 clocks a button change fewer.
* `desk_zs`: `mov bx, desk_zslot / xlatb` for `mov bl, al / xor bh, bh / mov
  al, [bx+desk_zslot]` (-4 / -4). ~12 clocks a zone lookup fewer.
* `lea r, [base+imm]` for `mov r, base / add r, imm` where the flags die
  (-14 / -9): `fm_kinit_x`'s title pointer, `fm_dgxor`'s and `fm_sel_bar`'s
  two bottom edges, the row take's two body pointers and the seed's, and
  kern_small's `fmv_demote`. (`lea ax, [di-fm_pool]` is not encodable - a
  relocatable negated - and is left as it was.)
* `fcp_undo`: `shr byte [fcp_made], 1 / jnc` is the test and the clear in one
  (-6 kern_big, -6 FILECP.DRV): `[fcp_made]` is only ever 0 or 1, and `.bss`
  is zero at boot (SPEC.md 2.5).
* `fm_editkey`'s two-line cancel: `cmp [FS_EDIT], 5 / je / cmp 6 / je` is
  `mov al, [FS_EDIT] / sub al, 5 / cmp al, 1 / jbe` (-3 / -3): AL is dead,
  every way out being `fm_edit_end` and the ladder.
* `assoc_padcpy`, `assoc_locate`'s stem copy and `fm_edit_commit`'s
  same-name compare by `lodsb` (-2, -3, -2 / -2): the SI each steps past a
  stopping byte is restored or reloaded.
* `fm_szfig_k` keeps CH = 0 so the unit and its NUL are one word store (-2,
  kern_big); kern_small's `fm_kbfig` stores 'K'/'?' with the NUL as a word in
  both arms (-4, kern_small).
* `asc_use_x`'s drop reads and zeroes `[asc_seg]` with one `xchg` (-2,
  kern_big); `[asc_n]`/`[asc_ne]` are zeroed unconditionally, which they
  already are whenever there is no claim (the free is the only way one ends,
  and `.load` zeroes `[asc_n]` before one is made).
* the seed's `[assoc_clus + 2*slot]` store through BX+SI instead of `shl bx /
  ... / shr bx` (-2, kern_big).
* `fm_onup_x` hands a chooser column's id to the hook in AH, which
  `fm_setdown` keeps and never reads, instead of banking AX round it (-2,
  kern_big).
* `fm_measure`'s empty-listing test is the loop's own foot compare (-2 / -2).
* `fcp_step`'s pause tail-jumps into `fcp_unbatch` with AL already set
  (`dskw_sync_x` keeps AX) (-1 kern_big / -1 FILECP.DRV); `fcp_door`'s two
  refusal loads share a `SKIPF2` (-1 / -1).
* `mov word [m], 0` -> `and word [m], 0` where no flag is read before the next
  write: `pth_clear` (-1, kern_big), `fm_vflip` (-1 / -1), `fm_kinit_x`'s
  `[fm_tdirty]` (-1 / -1). Each is a read-modify-write, +5 to +8 clocks, at
  one per root navigation, view toggle or window open - under 0.01% of each.

## HOT PATHS, PARITY AND THE DISK

* **No H1/H2 routine is touched.** The H3 paths that changed - the list row
  painter (PgDn, every full list paint), the blit-tier scroll, a header
  button's press and release, a selection band, a status line, a zone lookup
  - are all FASTER by the counts above; the three `and` stores are the only
  added clocks, each once per H3/H4 action and under 0.01% of it.
* **No `int 13h` added anywhere.** Every inlined body makes exactly the calls
  it made; nothing on the mount, listing, read-ahead, FAT-window or loader
  path moved. `navcalls` (open a root 2 calls, a folder 2, Up 1 on 360KB and
  720KB) and `fdswap` are in the rows below.
* **Parity**: `hotpar` green at every commit on kern_big and kern_small. The
  only bytes in front of the pinned `.cold` routines (`dsk_xfer`,
  `dsk_runcap`, `dsk_next_clus_x`) are `assoc.inc`'s: their shift run is -48,
  EVEN (the asc_use_out trampoline's -3 was taken to pair the -39 the rest
  came to). Everything else is past them. **No new pad.**

## EQUIVALENCE

By reading, routine by routine: for every bank removed the callee's header
AND body (every exit), and every instruction between the call and the next
write of the register on every path of every caller; for every inline, the
callee's register life against the caller's frame and its exits against the
caller's continuation, and the listing checked for relaxed jumps
(`os88rdiff`: no `relaxed` row on either kernel). `tools/stkbalance.py` over
`files.inc`, `assoc.inc`, `filecp.inc` and `desk.inc`, base against tip: 2, 3,
0 and 2 unbalanced paths at both (the same ladder tails); none new. kern_emu
and kern_mega build at the tip; `os88megaseg` unchanged at 3,682.

## REFUSED

* **`fm_mdraw` written into `fm_lsel_bar`** (its one caller; -3 as first
  written, and WRONG - DEFECTS FOUND 1). The walk needs DI = `[fm_vp]`, which
  `fm_manyvp` loads, and the caller's DI is banked under the ladder the walk
  would push, so a correct inline needs a `mov di, [fm_vp]` and costs +1.

* **The defensive `cld`s** (`assoc_ext_of`, `osapi_desk_item_x` and others):
  the owner DECLINED this class in pass 13 (P14 handoff 3), and nothing has
  changed.
* **Backspace into `fm_keytab`** (-6): the table is scanned after `and al,
  0DFh`, which folds '(' (0x28) onto 8 - a typed '(' would go up a folder.
  That is why it is tested before the fold.
* **`fm_editkey` written into `fm_onkey_x`** (its one call): ~300 bytes into
  a routine whose short jumps already span it, and `fm_onkey_x` reads SI after
  the call; not tried.
* **`fcp_claim`, `fcp_relink`, `fcp_scan`, `fcp_mksub`, `fcp_file2` into
  `fcp_step`** (their one caller each, ~-4 to -5 apiece on paper): each
  pushes a short jump of the step machine out of reach (`.walk`'s, `.jcfail`'s
  or `.fail`'s), and kern_small's FILECP.DRV assembles the much longer
  non-`DSK_DSCAN` `fcp_scan` into the same `jmp short .walk` loop.
* **`desk_owner`'s miss by `cmp cl, 1`** and **`icon_draw_x`'s refusal by
  `CLC_OR_STC`**: the same byte count either way.
* **`fdlg_hidx`'s AX bank** (-2): without it `fdlg_open_x`'s `.seed` would
  reach `app_launch` with AH = the high byte of the old SI instead of 0; not
  proved harmless.
* **`clo_fnbuf` sharing another resident 13-byte name buffer**: a clone runs
  for minutes with the rest of the desktop live, so no other buffer's lifetime
  can be shown disjoint.
* **Jumping into a callee's frame where the caller's exit label has other
  users** (`asc_gly_pair` -> `assoc_reduce`, `fm_onup_x`/`fm_onkey_x` ->
  `fm_repaint`, `fm_go` -> `fm_settitle`): cycles only, 0 bytes.

## WANTS THE OWNER

1. **The strip pass (item 1), TAKEN and revertible for +70 resident on every
   kernel.** SPEC.md 22.11.1.1 recorded it as dead code KEPT "as four
   instructions of test that state the invariant, and moving the inset back
   would need it". This branch deletes it and moves the invariant to an
   assembly-time `%if FM_PEN_X < 7` that fails the build if the inset ever
   goes back below the most the rounding can cut. Nothing a user sees can
   differ - the code could not run. If the runtime statement of the invariant
   is wanted back, revert the hunk of `e332ac7b` in `fm_scrollpaint` (the
   parent commit has it whole).

## WANTS AN ABI BREAK

None found on this row. Everything changed is kernel-internal (local labels,
`.cold` frames, the clobber lists of `fm_brect`, `fm_paste_res`,
`asc_inkck`, `assoc_ext_new` and `pth_clear`, headers updated). The frozen
surface read - `OSAPI_FERR_TEXT`, `OSAPI_FILE_COPY`'s door,
`OSAPI_ASSOC_SET`, `OSAPI_ARG_FILE`, `OSAPI_ARG_TEXT`, `OSAPI_DESK_ITEM`,
`OSAPI_VOL_CHECK` - has nothing that pays to break.

## CROSS-FILE

Seen in routines `fresh-routines.md` gives to another agent, not edited:

* **`fm_c_up` (hot)**: `mov bx, [fm_vp] / add bx, FS_PATH` is `lea bx,
  [di+FS_PATH]` (DI is `[fm_vp]` from the routine's first line, kept by
  `fmv_ishere`, `fmv_sync_x` and `fmv_get_dir`), and the `push ax / pop ax`
  round `pth_pop` is dead - `pth_pop` and `pth_leaf` keep AX: **-6**, an Up
  One Folder (H4).
* **`fm_draw_isel` (dir)**: ends `jmp fm_more_mark`, and `fm_more_mark` is the
  next instruction: **-2**.
* **`fmv_store` (dir)**: `mov cx, bx / sub cx, FV_DOFF` is `lea cx,
  [bx-FV_DOFF]` if the flags die (they do: `call fmv_copy_out` next): **-2**.
* **`fm_edit_end` (dir)**: its last store `mov word [fm_elen], 0` before
  `jmp kretc_di` is `and` (-1) if none of its callers reads the flags.
* **`fmv_load` (dir)** calls `pth_clear`, which now clobbers the flags; its
  next flag read is a compare (checked), so nothing to do - named so the dir
  agent knows.

## ROWS RUN

Once, after the last code commit then (`84eece1f`'s kernel), with the tree not
edited while they ran: `python3 tools/os88test.py soak -j 1 --marty-jobs 1 -k
...` over **39 rows: 38 passed, 1 failed (`msel`), 0 skipped** (888.6 s):
`fmrefuse dragui fmcommit clipkeep fcpapi fcpcopy fcpsmall fcproom dispcold
disptitle fdlgsmall fdlgdrop fmarrows autostart msel fdlgchoose fmthumb fmbtn
sbar sbardlg desksc zonedmg deskwhole deskdrag deskfdd assocglyph assocopen
assocfind assocvol ascabsorb dosglyph instassoc bigvol diskclone fmtpair
navcalls fdswap icoclip glyphcost`. `msel`'s failure was this branch's
(DEFECTS FOUND 1); after `3ff685c2` put the routine back - which restores the
base's bytes for exactly the code the failure was about and touches nothing
the other 38 rows ran - **`msel` re-run alone: passed** (26.5 s). That failure
is also this row's break-it-on-purpose: it was new to me, and it went red on
the one item that was wrong.

What each item is covered by: the strip pass and the row painter -
`fmthumb`, `fmarrows`, `sbar`, `sbardlg`, `fdlgchoose`, `glyphcost`, `icoclip`
and every list paint; `fm_setdown`/`fm_brect`/`fm_onup_x` - `fmbtn`,
`fdlgchoose` (the chooser's column); `fm_lsel_bar`/`fm_sel_bar` - `msel`,
`fmarrows`; `fm_paste_res` (both passes) - `fcpcopy`, `fcpapi`, `fcproom`,
`clipkeep`, `dragui`, `fmrefuse`, `desksc` (DESK.DRV's drop); FILECP.DRV -
`fcpsmall`; `fm_draw_status`/`fm_stat_line`/`fm_measure`/`fm_szfig_k` -
`bigvol`, `fmcommit`; `fm_editkey`/`fm_edit_commit` - `fmcommit`, `fmtpair`;
`fm_clone_res` - `diskclone`; `pth_*`, `fm_kinit_x` - `disptitle`,
`dispcold`, `navcalls`, `fdswap`, `autostart`; the chooser - `fdlgsmall`,
`fdlgdrop`; `desk_zs` - `deskfdd`, `zonedmg`, `deskwhole`, `deskdrag`,
`desksc`; `asc_use_x` and the rest of `assoc.inc` - `assocopen`, `assocfind`,
`assocvol`, `assocglyph`, `ascabsorb`, `dosglyph`, `instassoc`. The fast tier
(`hotpar`, `canary`, `asmrules`, `megaseg` among them) is green at every
commit.

## DEFECTS FOUND

1. **Mine, found by `msel` and fixed in `3ff685c2`**: `f42408d9` wrote
   `fm_mdraw` into `fm_lsel_bar` with the `pop di` moved above the ladder, so
   `fm_mset`'s walk read the CALLER's DI instead of the `[fm_vp]` that
   `fm_manyvp` had just loaded - every multi-selection band (Shift+Down,
   Shift+Up, Ctrl+A, both drag selects, a press right of the names) drew from
   the wrong block. The equivalence reading missed that `fm_manyvp` is an
   OUTPUT of DI, not just a test; the row's eight band checks went red, and
   green again with the routine back (`msel` re-run alone after the fix).
2. **`docs/INDEX.md` was stale at the brief's commit** (the brief added
   `fresh-routines.md` without regenerating it), so `make` failed in this
   fresh worktree - pass 17's lesson again. Regenerated and committed with
   this note.
3. Not fixed, a header only: `fmv_copy_in` says "all registers preserved",
   and its no-store arm's `rep stosw` leaves CX = 0. Both callers bank CX
   themselves, so nothing reads it.
4. Not fixed, a comment only: `fcp_undo`'s header says it preserves the flags
   "because .err's `stc` is above this" - the `stc` is AFTER the call, and the
   routine has never preserved the flags (it opens on a compare).
