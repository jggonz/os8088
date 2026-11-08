# Disk window, file clipboard, Standard File dialog, associations

Reviewer key: `files`. Tree: `the-big-review` (cut from `elendilon`), 2026-11-08.
Read-only review: nothing assembled, no emulator run, no repository file edited
but this one.

## Scope read

| file | lines | how |
|---|---:|---|
| `kernel/files.inc` | 9,928 | every code line (4,995, through a comment-stripped listing carrying the original line numbers), comments at every proc header and wherever a claim was being checked |
| `kernel/filecp.inc` | 2,363 | every code line (1,262); header and section comments in full |
| `kernel/fdlg.inc` | 1,384 | in full, comments included |
| `kernel/assoc.inc` | 2,506 | every code line (1,159); header, table and cache comments |
| `SPEC.md` 22 | 51151-51260, 52834-53060, 53252-53290, 53330-53480 | preamble and FS table, 22.3, 22.5/22.5.4, 22.6/22.6.3 |
| `SPEC.md` 38 | 75274-75930 | in full |
| `SPEC.md` 54 | 108119-108300 | 54.0 to 54.2.2 |
| cross-reads | | `instance.inc` `app_launch` (1440-1625), `memory.inc` purge ladder, `mem_shed_one`, `mem_pg_forget`, `mem_rr_tab` (330-440, 1280-1515, 3345-3420), `disk.inc` `dsk_dest_x`, the mount's step 2, `dsk_dscan` (4175-4275, 4685-4700, 4850-4890, 6244-6254, 7447-7480, 7880-7990, 8530-8600), `diskw.inc` `dskw_name83`/`dskw_char_x`/rmtree header (1000-1040, 5148-5250), `ui.inc` call sites (215-240, 445-475, 840-870), `kernel.asm` `api_fdlg_open` (4740-4805), `driver.inc` (1175-1190, 3300-3320, 6720-6890) |
| plans | | `docs/plans/ABI-BREAK-LIST.md`, `docs/plans/KERNEL-BYTE-QUEUE.md`, `CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (nothing on this area) |

## Verdict

This is the most mature subsystem in the area list: a file manager, a chooser, a
file clipboard and an association layer that share one browser, one listing
store per window and one copy engine, with refusal paths that are real and
hostile-input bounds that are checked. The 2026 merges (the chooser becoming a
Disk window, the listing-home change, the clipboard engine folding into one
step machine) left very little duplication. The weak spot is the seam those
merges created: a Disk window's listing claim is now **the mount's write
destination**, named by `[dsk_dseg]`, and the two memory mechanisms that can
take or move that claim mid-mount were not both taught about it. On
`kern_small` a shed during the mount can hand the claim to the FAT window
while the listing is still being written into it (files-1). On `kern_big`
`fm_reloc` does not patch the window being launched, so a compaction inside
`fm_kinit`'s first mount leaves `FS_VSEG` stale (files-2). Both are small
fixes. The rest is a Save box that accepts names the file system refuses
(files-3), a global purge floor that two unrelated parties write (files-4),
cheap speed in `fm_measure`, and a lot of stale text: SPEC.md 22's preamble
and 54.0/54.1 still describe the global listing snapshot that 22.6.3 retired.

## Strengths

1. **The chooser IS a Disk window** (`fdlg.inc` header; `fm_kinit_x`
   adopting `[fdlg_win] = 0FFFFh`, `files.inc:636-643`). One browser, one row
   painter, one line editor; the glue is hooks raised at named sites through
   one resident compare (`fdlg_hook`, `fdlg.inc` foot / `files.inc:3116` etc.).
   -1,750 resident bytes on `kern_big` when it landed and every Disk window
   got the chooser's keyboard selection. Copy this shape anywhere a "dialog"
   is a role of an existing window.
2. **The answer is POSTED and reaped** (`fdlg_post`, `fdlg_reap_x`,
   `fdlg.inc:531-660`). Ending a modal inside its own callback, with the
   caller about to draw that window, is the classic use-after-destroy; posting
   `[fdlg_act]` and closing on the next pass with the lock taken fresh is
   exactly what Windows does with `PostMessage(WM_CLOSE)` rather than
   `DestroyWindow` inside a WndProc. `fdlg_gate` answers "gone or answered"
   in ZF so both filters freeze the machine between post and reap without a
   test of their own.
3. **Guard on the caller's side of the boundary** (`ui.inc:845-868`, SPEC.md
   38.1.1). A per-pass hook whose answer is "no" 99% of the time costs a
   compare, not a far call. The written rule ("a cheap test that decides
   whether to do expensive work must live on the caller's side of any boundary
   the call crosses") is worth lifting into SPEC.md 1.
4. **Per-instance remembered folder** (`fdlg_open_x.seed`,
   `fdlg_home_dir`, `inst_fdrv/fcwd/fpick/fname`). Better than Mac System 6,
   whose Standard File remembered one machine-wide folder in low memory
   (`SFSaveDisk`/`CurDirStore`), so one app's dialog moved every app's default.
5. **The clipboard is a name, not an index; the tree walk has no call stack;
   the operation suspends on a question** (`filecp.inc` header, `fcp_step`,
   `fcp_stack` of `FCP_MAXD` frames). Attacker-shaped trees cannot blow a
   384-byte task stack, and "Replace?" is a trip through the event loop with
   every byte of state in `.bss`. Same-volume Cut is a relink
   (`fcp_relink`, three sector writes), cross-volume is copy+delete.
6. **Compression survives a copy without the engine knowing**: the copy
   reads raw (`READ_AT`/`dsk_read_chain`) and the write DERIVES the hint
   from the bytes (SPEC.md 20.14.4), so a packed file arrives packed, hint
   and all, on both builds.
7. **Coherence without I/O**: `fmv_bcast` re-aims `dsk_dest_x` at the acting
   window's own claim and runs `fmv_store` for every other window on the same
   folder (`files.inc:2184-2273`), a block copy instead of a re-mount;
   `fm_paste_res` paints the union once through `wm_dmg_wins`
   (`files.inc:7307-7430`), never `wm_paint_all`.
8. **One layout cache both painter and hit-tester read** (`fm_layout`,
   `files.inc:3109-3219`), asserted field order (`%error` at 9814), so they
   cannot disagree about where a row is.
9. **Hostile on-disk input is bounded where it is read**: `ASSOC.DAT`'s
   counts against `ASC_NAPP`/`ASC_NEXT`, its sector count against `ASC_KB*2`,
   the table extent against the file's own size (`assoc.inc:1964-2045`); a
   package header's declaration count against `ASSOC_DECLMAX`
   (`assoc.inc:698-702`); `fm_vp_set` bounds `I_SPTR` into `fm_pool`
   (`files.inc:747-750`).
10. **Associations composed at mount, not at draw** (SPEC.md 54.1's decision):
    no drawing path knows associations exist.

## Findings

Ranked. `[now]` = no ABI break, `[flagday]` = needs one, `[record]` = write
down, do not do. Bytes are signed estimates; "resident" means `.text`/`.bss`/
`.cold` unless a module image is named.

### files-1 - kern_small: a shed during a loud mount can hand the listing's own destination to the FAT window (bug/hazard, high, [now])

`fmv_load` aims the mount at the window's own claim and then mounts:
`dsk_dest_x` with `DX = [di+FS_VSEG]` (`files.inc:1693-1706`), then
`dsk_chdir_x` (`files.inc:1715`). Inside that mount, after the aim and
before the listing is written, the disk layer **claims**: `dsk_rah_want`
re-claims the read-ahead window if it was shed (`disk.inc:4252`, rank
`MEM_PG_HIGH`, `memory.inc:429`) and the FAT window's first load claims for a
non-boot volume (`dsk_fat_window`, `MEM_P_FATW` at `MEM_PG_MED`,
`memory.inc:363`). On `kern_small` the Disk window cache is purgeable at
`MEM_PG_LOW` (`memory.inc:384`), and `mem_shed_one` takes the cheapest record
strictly below the claimant's rank (`memory.inc:1405-1420`) - so once the
TRIV caches are gone, the FAT window's or read-ahead's claim sheds **the
listing destination the mount is about to write**. `mem_pg_forget` hands it to
`fmv_demote` (`memory.inc:1505-1507`), which zeroes `FS_VSEG` and
`[fm_vseg]` (`files.inc:1418-1437`) but not `[dsk_dseg]`. The freed
paragraphs are what the retrying claim then receives; step 3 of the mount
writes up to 32 entries x 24 bytes plus the reference index through
`mov es, [dsk_dseg]` into them. If the claimant was the FAT window, its
freshly loaded FAT sectors are overwritten with directory entries in RAM, and
the next write to that volume flushes the window - on-disk FAT corruption. The
`MEM_RR_ONE dsk_dseg` backstop (`memory.inc:3369`) covers a MOVE, not a shed.

SPEC.md 22.6.3's safety argument ("The one claimer in the whole mount is
`asc_use`", SPEC.md:53470) is true of `kern_big`'s compaction window after the
write and false of the claims before it; `tools/dsegaudit.py` checks a segment
held in a register and cannot see this.

**Recommendation.** In `mem_shed_one`'s scan, `%ifdef KERN_SMALL`, skip a
record whose `MC_SEG` equals `[dsk_dseg]` (beside the existing
`mem_fatw_dirty` skip): `mov dx,[ss:si+MC_SEG] / cmp dx,[dsk_dseg] / je .scan`.
That covers every loud mount (Disk window and chooser alike) with one test.
(Raising the UI task's floor around the mount is the alternative, but the
floor word is already contended - files-4.) Add a soak row that fills a
128KB heap until the VIEW caches are the cheapest takeable, navigates a B:
window, and asserts the FAT window's bytes against the disk; break it by
removing the skip and watch it go red.
**Bytes: +9 `.cold` kern_small, 0 kern_big.**

### files-2 - kern_big: `fm_reloc` does not patch the window being launched (bug, medium, [now])

`app_launch` runs `KD_INIT` (`fm_kinit_x`) before it publishes
`I_STATE = 1` (`instance.inc:1600-1610`). `fm_kinit_x` claims the cache and
declares it MOVABLE with `fm_reloc` (`fmv_fit`, `files.inc:1297-1319`), then
lists the seed folder (`fmv_load`, `files.inc:681-683`), whose mount reaches
`mem_claim` (`asc_use`'s `mem_claimp_x`, `ico_need`, the FAT window) and so
the compactor. If the cache moves, `fm_reloc` (`files.inc:9899-9926`) walks
`inst_tab` for `I_STATE == 1` Disk records - the launching record is still 0 -
so `FS_VSEG` keeps the OLD base while `[fm_vseg]` and `[dsk_dseg]` (the rr
row) move. `fmv_store` then compares `FS_VSEG` with `[dsk_dseg]`
(`files.inc:1216`), finds them different, and **copies the listing into the
old segment** - freed or reused heap. On `DSK_BIGDIR` the grow's
`mem_regrow` (`files.inc:1817-1819`) is also handed the stale segment. The
Standard File chooser goes through the same `fm_kinit_x`.

**Recommendation.** Walk the `FM_NSLOT` pool blocks instead of the instance
records: a block whose `FS_VSEG` equals the moved base is that claim's holder
whoever owns it, `fm_kinit_x` zeroes `FS_VSEG` on reuse (`files.inc:569-583`),
and the walk is shorter. **Bytes: about -3 `.text` kern_big** (CX then needs
banking; `inst_next` and two `I_STATE`/`I_KIND` compares go).

### files-3 - the Save box accepts names the file system refuses (bug, medium, [now])

`fm_editkey`'s text arm (`files.inc:6686-6729`) bounds the buffer at 12,
filters each character through `dskw_char_x`, and refuses a leading or second
dot - but enforces neither the 8-character stem nor the 3-character
extension, and allows a trailing dot. `dskw_name83` refuses all three
(`diskw.inc:5166-5205`). `fdlg_save` (`fdlg.inc:970-980`) checks only
`FS_MOK` and a non-empty box, so a Save chooser commits `ABCDEFGHIJ` or
`A.BCDE` to the application, whose write then fails `FERR_NAME` after the
dialog has gone. Rename and New Folder are not affected (their `dskw_*`
call reports `Bad name` in the window). SPEC.md 38.5's "the box accepts
exactly the bytes `dskw_name83` will store and nothing else" is not true.

**Recommendation.** In `fdlg_save`, after the empty test:
`mov si, fdlg_name / FDX dskw_name83n / jc fdlg_beep` - the commit is refused
with the beep an empty box already gets. Then 38.5's sentence is true of the
COMMIT. (Enforcing 8/3 per keystroke in `fm_editkey` would make it true of
the box too, for ~+14 `.cold`, and helps Rename/New Folder - the owner's call.)
**Bytes: +8 `.cold` kern_big; +8 `FDLG.DRV` image, 0 resident, kern_small.**

### files-4 - one global purge-floor word, written by the clipboard and lifted by anyone (hazard, low, [now])

`mem_pg_fl` is one word: level and owning task (`memory.inc:1291-1300`).
`fcp_paste`'s multi-file arm overwrites it with the UI task's `MED`
(`filecp.inc:457-459`) whatever another task had declared through
`OSAPI_MEM_FLOOR`; `fcp_lend` sets `MEM_LVL_TOP` unconditionally
(`filecp.inc:2347-2348`), as does `mem_free_rec_x` for ANY instance teardown
(`memory.inc:4826`). DOS sets a floor, posts a compaction and claims on the
wake (`memory.inc:5822-5850`'s own account); a multi-file paste or any window
close in that gap silently lifts it, and DOS's claim then sheds the read-ahead
window it asked to keep. Performance, not corruption.

**Recommendation.** Lift only one's own: `cmp bh?`-style test of the stamp
(`mov ax,[mem_pg_fl] / cmp ah,[sch_cur] / jne .keep`) in `fcp_lend` and
`mem_free_rec_x`; and in `fcp_paste` decline to set a floor when another
task's is standing. **Bytes: about +6 at each of three sites, `.cold`, both
kernels (fcp_paste/fcp_lend kern_big only: `FM_MSEL`).** The longer answer is
a floor byte per task in the scheduler's table - rethink RT7.

### files-5 - `fm_measure` stages every entry through a 24-byte far copy, on every listing (perf, low-medium, [now])

`fm_measure` (`files.inc:1849-1925`) sums `Used` by calling `fmv_dient`
per entry (`files.inc:1891-1907`): bank and re-publish `[fm_vp]`/`[fm_vseg]`,
a 16x16 `mul`, and a 24-byte `dsk_copy_seg` into `dsk_ent`, to read two
words. That is several hundred cycles an entry, on every `fmv_load` and for
every window `fmv_bcast` refreshes: ~40 ms on the 8088 for a 300-entry folder,
~130 ms at the 1,024-entry `kern_big` ceiling, on every navigation, on top of
the disk. **Recommendation:** walk the cache in place - `ES = FS_VSEG`,
`SI = FV_DOFF + 20`, `add si, DSK_DE_STRIDE` per entry, `add cx,[es:si] /
adc bx,[es:si+2]` - about 40 cycles an entry (falls back to the existing loop
when `FS_VSEG = 0`). **Bytes: about 0 to -6 `.cold`; ~35 ms a 300-entry
navigation, PREDICTED** (take it on MartyPC in the final pass).

### files-6 - `fmv_owner`'s "cannot happen" fallback names the menu save-under (hazard, low, [now])

With `[fm_vinst] = 0` the `kern_big` arm tags the cache `MEM_P_MSAVE`
(`files.inc:1376-1382`), a purgeable TRIV tag whose `mem_pg_forget` row
zeroes `menu_sseg` (`memory.inc:1313`). If the impossible ever happens, the
shed zeroes the wrong naming word and leaves `FS_VSEG` dangling - silent
corruption from a defensive branch. Use any non-purgeable kernel tag (e.g. the
existing `MEM_K_*` space) or refuse the claim. **Bytes: 0.**

### files-7 - the commit's put-back ignores a failed mount (weakness, low, [now])

`fdlg_reap_x`'s commit calls `FDX dsk_chdir_x` when the globals moved
(`fdlg.inc:597-606`) and never reads CF. Packages are unaffected (their file
calls re-enter the instance's own folder, `inst_vol_enter`), but the four
kernel requesters with no instance (`drv_dlg_done_x`, `fm_img_done_x`,
compress's Uncompress To, DISKOPS's clone-to-image) act on whatever the failed
mount left - the root of the drive it tried. **Recommendation:** `jc .unlk`
plus a beep (the callback is skipped, as staleness already skips it).
**Bytes: about +2 to +5 `.cold`.**

### files-8 - opening a chooser ends other windows' prompts without repainting them (weakness, low, [now])

`fdlg_open_x`'s `.endp` loop (`fdlg.inc:340-351`) clears modes 1-2 in every
pool block so the chooser can own `fm_ebuf`. Those windows' status lines keep
showing `New folder: ...` / `Rename ... to:` with no editor behind them, during
the dialog and after it, until something repaints them. **Recommendation:**
mark the cleared block `FS_DIRTY = FSD_PIXELS` (spent by `fm_focus` as a
redraw) or call `fm_owed`. **Bytes: about +4 `.cold`.**

### files-9 - modality covers presses, not drops (weakness, low, [now])

`fdlg_grab` filters `EVT_MDOWN`/`EVT_RDOWN` (`ui.inc:459-470`); a drag that
STARTS in the chooser is the chooser's, and `fm_dgdrop` resolves the drop with
`wm_hit` (`files.inc:7136-7156`) - so a file dragged out of a Save chooser
lands in a Disk window behind it, or on the desktop (`OS88_SHORTCUTS`'s
`.desk`, `sc_modcall`), while the system is supposedly input-modal. Nothing
breaks, but it is the one way round 38.2's "nothing gets through".
**Recommendation:** in `fm_dgdrop`, when `[fdlg_win]` is non-zero, refuse a
target window that is not the chooser. **Bytes: about +10 `.cold`.**

### files-10 - kern_small keeps a closed window's cache claimed (weakness, low, [now])

The cache is owned by `MEM_P_VIEW + slot`, which no instance teardown reaps, so
it is freed only when the next tenant of that pool block runs `fm_kinit_x`
(`files.inc:555-565`). Every closed Disk window, and the chooser's fifth block
after every dialog, leaves 2KB standing in a ~31KB heap. Purgeable, so a claim
gets it back, but it fragments the heap until then and a shed of it calls
`fm_owed` for a dead block (`files.inc:1432-1433`). **Recommendation:** free
it in `fdlg_reap_x` for the chooser's block (the common case) and on
`app_close_win` for `KIND_FILES`. **Bytes: about +10 `.cold` kern_small.**

### files-11 - an empty-icon test reads 64 bytes a row (perf, low, [now])

`fm_draw_icon16` ORs all 32 words of the staged icon to decide "no icon"
(`files.inc:7746-7755`): ~800 cycles a row, ~8,000 a full list paint, ~1.7 ms
on the 8088. `fmv_get_icon` has the reference byte in hand
(`files.inc:999-1002`) - `ICO_R_NONE` is that fact. Test it.
**Bytes: about -6 `.cold`.**

### files-12 - hidden and system files are skipped by a folder copy, silently (weakness, low, [record])

Both `fcp_scan` arms skip attributes 2/4 (`filecp.inc:1225` passes `DL = 0`
to `dsk_dscan`, `disk.inc:7950-7955`; the other arm `test ah, 0x0E`,
`filecp.inc:1297`). A copied folder is short those files with no notice, and a
cross-volume Cut's `dskw_rmtree` then stops at the first one with `FERR_PROT`
(`diskw.inc:1006-1012`), leaving a split tree. It is XCOPY's default (no
`/H`), so defensible, but the user is not told until the delete half fails.
Worth a line in SPEC.md 22.3's limits; a "skipped N hidden" toast is a few
bytes if wanted.

### files-13 - association slots can be exhausted by any volume's cache (hazard, low, [now])

Nothing frees an `assoc_*` slot in a session (SPEC.md 54.2). `asc_use_x`'s
merge creates an app slot for every app row an extension row names
(`assoc_app_get`, `assoc.inc:2077-2100`), and `assoc_ext_take` accepts any
first byte above 0x20 as an extension (`assoc.inc:740`) - lowercase or
high-bit bytes that no sanitized display name can ever match. A crafted (or
merely large) `ASSOC.DAT` fills 16/32 for the session and later real
declarations drop silently. **Recommendation:** pass the three bytes through
`dskw_char_x` (refuse what a display name cannot contain) and stop absorbing a
volume's rows when fewer than, say, four app slots remain. **Bytes: about
+10 `.cold` kern_big.**

### files-14 - `inst_tab + I_SPTR` is a relocation row over values that are not all segments (hazard, low, [record])

`mem_rr_tab`'s `MEM_RR_ROW inst_tab + I_SPTR, INST_MAX, I_RECSZ`
(`memory.inc:3351`) patches every `I_SPTR` equal to a moved base. For a
package that is its region segment; for a built-in kind it is a NEAR pointer
into the kind's `KD_POOL` (`fm_pool`, `files.inc:9626`). Safe today only
because those `.bss` offsets (~0xC5xx up, KERNEL-BYTE-QUEUE's sizes) are above
every conventional segment. On `kern_mega`, where a claim can be an LDT
selector, or with a smaller `.text`, a collision would rewrite a Disk window's
state pointer. Worth a `%error` (or an `I_KIND` filter) rather than an
assumption; cross-area, so recorded here for the memory reviewer.

### files-15 - three copies of the module far-hop, two macro vocabularies (consistency, low, [now])

`fdx_go`/`fdt_go` (`fdlg.inc:236-275`), `fcpx_go` (`filecp.inc:256-284`) and
`extmod.inc`'s `exk_go` are the same hand-built far frame; `FDBODY/FDX/FDK/
FDKT/FDRET/FDXJ/FDENT/FDV` and `FCPBODY/FCPX/FCPXJ/FF/FD/FCV` are two dialects
of one idea. Each image must carry its own hop (an image cannot near-call
another), so no resident byte is at stake, but one `%include`d macro set
(`kernel/modhop.inc`) would make the next module a copy of nothing.
**Bytes: 0.**

### files-16 - the source is shaped around tools that cannot evaluate `%ifdef` (rethink, medium, [now])

`FDH_RECT` exists because `tools/os88ovlchk.py` files every `fdlg.inc` body
as `.modd` on both builds and refuses a near call that is legal on
`kern_big` (SPEC.md 38.9's "`FDH_RECT` is not a convenience"); `FDENT` is
spelled `call %1` so that same scan cannot see the word (`fdlg.inc:184-196`);
`tools/stkbalance.py` needs `STKBALANCE-OK` annotations on the hops. Teaching
both tools to read NASM's preprocessed source (`nasm -E` with the build's
defines) or the per-build listing would let the code say what it means and
retire the hook indirections that exist only for them. **Bytes: 0 kernel;
unlocks ~-10 `.cold` kern_big in `fm_brect`.**

### files-17 - the completion callback could say WHERE (rethink, low, [now])

The callback gets the name in DI and must trust that "the file's volume is
mounted and its folder is current" (SPEC.md 38.6), which `fdlg_reap_x` makes
true with a put-back (files-7). `OSAPI_ARG_FILE` already answers a document
as SI = name, DX = cluster, BL = drive (`assoc.inc:1595-1603`). Handing the
callback AH = volume and BX = cluster is ADDITIVE - BX is unused and nothing
is promised in AH today (`fdlg.inc:672-688`) - and makes the folder explicit
for kernel requesters with no instance. Mac's `SFReply` carried `vRefNum` for
exactly this. **Bytes: about +6 `.cold`; an `abilock --update` without a bump
for the prose.**

### files-18 - an extension filter for the Open form (rethink, medium, [flagday])

Every Open chooser lists everything, and SPEC.md 38.6 tells each app to refuse
after the pick. Mac `SFGetFile`'s `typeList` and Win 3.1 `GetOpenFileName`'s
`lpstrFilter` both filtered in the dialog. The natural carrier is AH (one
three-byte extension, or an index of the app's declared ones), but
`fdlg_open_x` deliberately never reads AH (`mov ah, 0`, `fdlg.inc:318`) and
existing packages leave junk there, so it needs the flag day. Grey (not hide)
the rows that do not match, as 47's "grey a fact" rule wants. **Bytes: about
+30 `.cold` kern_big / `FDLG.DRV` kern_small. Not in ABI-BREAK-LIST; propose
adding it.**

### files-19 - system-modal, where Windows and Mac were owner- and app-modal (prior-art, [record])

The chooser freezes every window, the bar and the dock (38.2). COMMDLG
disables only the owner; Mac SF was app-modal in a single-app world. Owner-
modal would be the better UX, but SPEC.md 38.2 lists five packages that now
depend on system modality (Telnet's ZMODEM cancel inference, Artful, RAMDISK,
Paint's fsx fence, the image commands). Recorded so nobody reaches for it
without paying for those.

### files-20 - the single "acting window" context (rethink, [record])

Everything reads `[fm_vp]`/`[fm_vseg]`/`[fm_vinst]` and the `fm_lrec` mirror,
published by `fm_vp_set` (`files.inc:738-760`); a path that skips it reads the
previous window's directory (the proc header says so). GEOS passed every
method its instance (`*ds:si`), Windows every WndProc its `hwnd`. Here the
painter uses every register including BP, so the global is the honest choice
on an 8088; it is recorded as the subsystem's one fragile invariant held by
discipline, and the reason `fmv_dient` exists.

## Rethinks

* **RT1 - shared folder caches.** Two windows on one folder hold two copies,
  kept equal by `fmv_bcast`. A reference-counted cache per (volume, folder)
  would save up to 32KB of heap on `DSK_BIGDIR` at the cost of a refcount and
  a teardown rule; the Win 3.1 File Manager and the Finder both kept per-window
  state too. Not worth it at 1KB caches; revisit if big folders become common.
* **RT2 - make 22.6.3's "nothing claims while the destination is aimed" a
  mechanism, not an audit.** files-1 and files-2 are both "the aimed store
  moved or vanished during the mount". A one-byte `[dsk_dpin]` that
  `mem_shed_one` and the compactor's mover both refuse for the aimed segment
  (the `mem_fatw_dirty` precedent) would make the property structural on both
  kernels. ~+12 resident, both kernels; files-1's skip is the kern_small half
  of it.
* **RT7 - a purge floor per task**: one byte in the scheduler's per-task row
  instead of `mem_pg_fl`, so floors cannot be clobbered across tasks
  (files-4). ~+16 `.bss` (one per `MAX_TASKS`), -6 code in `mem_rank_bh`'s
  stamp compare. The owner's call; files-4's guards are the cheap version.
* **Validation belongs at the commit, not the keystroke**, for the same reason
  38.1.1 puts guards on the caller's side: the box is shared by four prompts
  with different rules; the commit knows which one it is (files-3).

## Prior art

* **Mac Standard File (System 1-6)** - synchronous `SFGetFile`/`SFPutFile`
  running their own `ModalDialog` loop, returning `SFReply` (good, name,
  `vRefNum`), with a `typeList` filter and `fileFilter`/`dlgHook` procs. A
  synchronous call is impossible here (the caller holds the non-recursive gfx
  lock), so os8088's posted-answer + completion proc is forced and is the
  right adaptation. What SF did better: the filter (files-18) and a reply that
  names the volume (files-17). What os8088 does better: the remembered folder
  is per application, not a machine-wide low-memory global.
* **Windows 3.1 COMMDLG** - `GetOpenFileName` with filter strings,
  owner-modal, a hook proc; also synchronous (cooperative multitasking made
  that free). The File Manager had MDI directory windows and drag-drop
  move/copy but **no file clipboard**, and in 3.0 no automatic refresh after a
  change - os8088's `FS_DIRTY` + `fmv_bcast` + damage-union repaint is ahead
  of it.
* **Mac Desktop file / Desktop DB** - a per-volume, Finder-maintained cache of
  BNDL/FREF/icon resources, rebuilt on demand. `ASSOC.DAT` is the same idea
  arrived at independently, including "rebuild when stale" (54.4.2.4). What
  the Mac did better: type/creator codes, which bind a document to its program
  without trusting a three-letter extension. FAT has no place for them without
  a sidecar, so extensions are the right answer here.
* **Windows 3.x `[Extensions]` in WIN.INI / REG.DAT** - a single global text
  table edited by hand or by Setup. os8088's build-time defaults + header
  declarations + per-volume cache need no installer and follow the disk, which
  is better for a floppy world.
* **OS/2 1.x Extended Attributes (`.TYPE`)** - per-file typing stored in
  `EA DATA. SF` on FAT. Too expensive (a hidden file, a second directory
  walk) for an XT; recorded.
* **GEOS** - per-file token and creator token in the GEOS file header, and a
  file selector that filtered by token. The closest peer, and it chose
  per-file metadata because it owned its file format; os8088's package-header
  declaration (54.6) is the same move for the one file type os8088 owns.
* **DOS XCOPY** - skips hidden/system without `/H`, which `fcp_scan` matches
  (files-12); `COPY` buffering in available memory is `fcp_claim`'s
  `mem_avail` sizing.
* **AmigaOS Workbench** - `.info` sidecars carry each project's icon and
  default tool: per-file association, more flexible than an extension table,
  paid for with a file per file. Not adoptable on floppies at this cost.

## Doc/code mismatches

Each gives the exact current text, what the code does, and replacement text.

1. **SPEC.md:75606** - "`0x0150 fdlg_open   in AL = 0 Open / 1 Save`". The
   slot is 0x0124 (`kernel.asm:2744`, `apps/os88api.inc:1160`).
   Replace `0x0150` with `0x0124`.
2. **SPEC.md:75687** - "`fdlg_sizeof` **stats the chosen name**". There is no
   `fdlg_sizeof`; the stat is in `fdlg_reap_x`'s commit half
   (`fdlg.inc:620-656`). Replace with "`fdlg_reap_x`'s commit half **stats the
   chosen name**".
3. **SPEC.md:53418** - "| the Standard File dialog | `MEM_K_FDLG`, `VIEW_KB`,
   taken at `fdlg_open` | the dialog's |". `MEM_K_FDLG` is reserved and
   unused (`os88api.inc:2834`); the chooser is a Disk window with its own
   `FS_VSEG` (38.1). Replace with "| the Standard File dialog | its chooser
   Disk window's own `FS_VSEG` claim (§38.1), taken at `fm_kinit` | the
   chooser's |".
4. **SPEC.md:53470** - "The one claimer in the whole mount is `asc_use`".
   `dsk_rah_want` (`disk.inc:4252`), the FAT window's first load and
   `ico_need` all claim with the destination aimed. Replace with "The claims
   inside the mount are `dsk_rah_want` and the FAT window's first load (step
   2, before the listing is written), `ico_need` and `asc_use` in the harvest
   (after it). A compaction in any of them is covered by `mem_rr_tab`'s
   `MEM_RR_ONE dsk_dseg` row; a SHED on `kern_small` is not (files-1)."
5. **SPEC.md:51153** - "Built-in app kind (KIND_FILES), **cap 4**". `KD_CAP`
   is `VIEW_SLOTS+1` (`instance.inc:203`). Replace with "**cap 5** - the
   user's four, which `fm_count` holds to `FM_MAXWIN`, and the Standard File
   chooser's (§38.1)".
6. **SPEC.md:51160** - "**The binding rule of this whole section, one
   sentence: paints read the window's cache; actions re-sync the global
   snapshot first.**" and the paragraph after it (`disk_dir`, `disk_icons`,
   a "copy of that snapshot in its `VIEW_SEG` slot"). §22.6.3 retired the
   global snapshot: a mount writes into the acting window's claim. Replace the
   paragraph with: "**The binding rule of this whole section: paints read the
   window's own cache, and a listing mount writes straight into it.** There is
   no global listing (§22.6.3): `fmv_load` aims `dsk_dest_x` at the window's
   `FS_VSEG`, mounts, and aims it back at nothing. An action that needs the
   volume where the window is calls `fmv_sync`, which re-lists only if the
   window's `(drive, cwd)` is not `([disk_drive], [dsk_cwd])`, and otherwise
   moves the folder with a quiet `dsk_chdir_q`."
7. **SPEC.md:51176** - "57 bytes under `KERN_BIG`". `FS_SIZE` is 61
   (`files.inc:383`, `fm_pool` asserts `5*61` at 9634). Replace with "61
   bytes under `KERN_BIG`".
8. **SPEC.md:51191** - "| 14 | `FS_FERR` b |". Offset 14 is `FS_2UP` and 15
   `FS_IOFH` (`files.inc:261,268`); there is no `FS_FERR`; `FS_FREEH` and
   `FS_USEDH` follow `FS_PATH` on kern_big (377-378). Replace the row with
   "| 14 | `FS_2UP` b | the status line was last drawn two-line (§22.20) |",
   add "| 15 | `FS_IOFH` b | the reference index's base, 0 = derive it
   (§22.6.4) |", and add `FS_FREEH`/`FS_USEDH` after `FS_PATH`.
9. **SPEC.md:51211** - "`fm_msgbuf` (16B) plus `fm_msgwin` (word, the state
   block that asked) carry the free-space line". Neither exists; the resting
   line is drawn from `FS_USED`/`FS_FREE` (`files.inc:9197-9218`, §22.7).
   Delete the sentence.
10. **SPEC.md:51244** - "`fm_title_flush` spends it through `wm_title_set`"
    and "`fm_go`, `fm_mount` and `fm_view` hold it". The flush is inline in
    `fm_repaint` (`files.inc:7717-7722`); `fm_view` is gone (the toggle is
    `fm_vflip`, which does not retitle). Replace with "`fm_repaint` spends it
    through `wm_title_set`" and "`fm_go` and `fm_mount` hold it".
11. **SPEC.md:52907** - "the operation ends with one `fmv_reload_all`", and
    **SPEC.md:52936** "**`fcp_selfchk` refuses a folder pasted into itself**".
    Both are folded: the re-list is `fm_paste_res`'s pass
    (`files.inc:7307-7360`) and the self-paste walk is `fcp_run2`'s `.selfup`
    (`filecp.inc:631-652`). Replace with "ends with one re-list pass in
    `fm_paste_res`" and "**`fcp_run2` refuses a folder pasted into itself**".
12. **SPEC.md:75778** - "and the kernel's own three callers'". There are four:
    `ctrl.inc:1155`, `files.inc:5892` (`fm_c_dlg`, both image commands),
    `compress.inc:2398`, `diskops.inc:1521`. Replace "three" with "four".
13. **SPEC.md:75569** - "the box accepts exactly the bytes `dskw_name83` will
    store and nothing else". False today (files-3). If files-3's commit check
    lands: "the box accepts only `dskw_char`'s characters, and a Save commit
    refuses (with a beep) any name `dskw_name83` would refuse".
14. **SPEC.md:108138** - "`asc_use_x` claims `ASC_KB` = 3,072 bytes under
    `MEM_K_ASC`, ... and nothing frees it." `ASC_KB` is 8 (`assoc.inc:1846`);
    the claim is the file's own size, freed at `asc_use_x`'s one exit
    (`assoc.inc:2233-2241`, §54.7.4). Replace with "`asc_use_x` would claim
    the volume's `ASSOC.DAT` (up to `ASC_KB` = 8KB) under `MEM_K_ASC` at
    every mount, the boot mount included".
15. **SPEC.md:108151** - "answers *"Bad package"*". A shipped kernel folds
    `LD_EBAD` into `Load failed` (`files.inc:2370-2374`, `LDDIAG=1` restores
    it). Replace with "answers *"Load failed"* (*"Bad package"* with
    `LDDIAG=1`)".
16. **SPEC.md:108177** - "`disk_icons` is `dsk_nmax` × 64 bytes, already
    allocated" ... "the file dialog (§38), which reads the snapshot directly
    because it is modal." No `disk_icons`, no snapshot; icons are reference
    bytes into the machine-wide store (§25.9), and the chooser is a Disk
    window. Replace with "A listing's reference index (§22.6.3) already has a
    byte per entry and the icon store (§25.9) already holds bodies, so a
    document icon composed into the store costs no per-file RAM and no paint
    work: every Disk window, the chooser included (§38.1), shows it with no
    change to a drawing path."
17. **SPEC.md:108215** - "The raise is 20 bytes a slot". A slot is 19 (stem 8,
    glyph 8, `assoc_clus` 2, `assoc_drv` 1; `assoc_dfold` went in size pass
    13, as the paragraph above says). Replace with "19 bytes a slot".
18. **kernel/assoc.inc:34** - "app slots - 20 resident bytes each (stem,
    glyph, cluster, volume, default folder)". Replace with "app slots - 19
    resident bytes each (stem, glyph, cluster, volume)".
19. **kernel/files.inc:5** - "FOUR windows may be open at once (KIND_FILES,
    cap 4)". Replace with "four of the user's windows may be open at once
    (KIND_FILES, cap VIEW_SLOTS + 1 - the fifth pool block is the Standard
    File chooser's, SPEC.md 38.1)".
20. **kernel/files.inc:12-20** - "disk_nfiles stay exactly ONE global mount
    snapshot". Replace the paragraph with SPEC.md 22's corrected rule (item 6):
    paints read the window's cache, a loud mount writes into it through
    `dsk_dest_x`, and `fmv_sync` re-lists only when the window is not where the
    volume is.
21. **kernel/files.inc:22** - "(fm_pool, 4 x FS_SIZE)". Replace with
    "(fm_pool, FM_NSLOT x FS_SIZE: the user's four and the chooser's)".
22. **kernel/files.inc:46** - "File/Edit/Nav/Builtins are this window's
    commands". The bar is File, Edit, Nav, Disk (SPEC.md 22.28,
    `files.inc:2683-2712`). Replace "Builtins" with "Disk".
23. **kernel/files.inc:544** - "KD_INIT runs on the UI task with NO lock held,
    which is exactly the context a mount needs." For the chooser it runs under
    the requester's held lock (`inst_lkhd`, `fdlg.inc:409-414`). Append "-
    except for the Standard File chooser, which fdlg_open launches under the
    requester's held gfx lock (SPEC.md 38.6); the mount is legal there too,
    as it is in any callback".
24. **kernel/files.inc:701** - "Each window owns one 4KB VIEW_SEG slot
    holding a byte-for-byte image of disk_dir". Replace with "Each window owns
    one listing claim (VIEW_KB, grown per folder on DSK_BIGDIR) that the mount
    writes straight into (SPEC.md 22.6.3)".
25. **kernel/filecp.inc:44** - "create followed by a run of dskw_append
    calls". On kern_big the appends are one held `WRITE_SEQ` stream
    (`filecp.inc:1852-1883`, SPEC.md 22.5.3). Replace with "create followed
    by appends - dskw_append on kern_small, one held WRITE_SEQ stream on
    kern_big (SPEC.md 22.5.3)".
26. **kernel/filecp.inc:47-49** - "It is the one of the four with no resident
    thunks and no `_x` bodies at all: every caller of an fcp_ routine is
    files.inc". `fcp_door` is reached from the `OSAPI_FILE_COPY` cell through
    `fcpf_fcp_door` (`filecp.inc:2242`), and on kern_small every body is an
    `_x` in `FILECP.DRV`. Replace with "On kern_big its callers are files.inc
    (near, both `.cold`) and the OSAPI_FILE_COPY cell (through fcpf_fcp_door);
    on kern_small the bodies are FILECP.DRV's (FCPBODY, below)".
27. **kernel/kernel.asm:4778** - "fdlg_home_name was unreachable" and **4784**
    "fdlg_seed decides where a DIALOG opens". Both folded: the remembered name
    is `fdlg_hidx` + `fdlg_cpy`, the seed is `fdlg_open_x.seed`. Replace the
    names.
28. **kernel/kernel.asm:6689** - "fdlg_home_save records it into the
    instance". Replace with "fdlg_reap_x's commit records it (fdlg_home_dir)".
29. **kernel/kernel.asm:8316** and **kernel/driver.inc:3310** -
    "fdlg_commit dispatches" / "Called by fdlg_commit". Replace with
    "fdlg_reap_x's commit half".
30. **kernel/files.inc:5860** "(SPEC.md 38.2 - fdlg_close, no commit)",
    **5922** "fdlg_commit's contract", **5976** "fdlg_commit destroys the
    dialog before it calls back". Replace with "fdlg_reap_x's close half" /
    "the completion contract" / "fdlg_reap_x closes the dialog before it calls
    back".
31. **kernel/disk.inc:6252-6253** - "no listing is ever in a claim and
    [dsk_dseg] is LOW_SEG for the life of the machine". Since 22.6.3 it is 0
    at rest and names a Disk window's claim during a loud mount, which is why
    `memory.inc:3369` carries the row. Replace with "the donation went with
    SPEC.md 22.6, but [dsk_dseg] still names a claim during a loud mount - the
    window's own (22.6.3) - and mem_rr_tab's MEM_RR_ONE dsk_dseg row is what
    follows it through a move".
32. **kernel/instance.inc:1445** - "UI task only, gfx lock NOT held (takes its
    own)". Append "- unless [inst_lkhd] is set, when the caller (fdlg_open)
    holds it and the show runs under that hold (SPEC.md 38.6)".

## Open questions

* files-1 wants a measurement before its severity is final: how often does a
  128KB machine reach the state where a VIEW cache is the cheapest takeable
  record while a mount needs the FAT window? The test row named in files-1 is
  the way to find out, and the fix is cheap enough to take regardless.
* files-14: does `kern_mega`'s mover ever call `mem_region_reloc` with a
  selector as the old base? If so the `I_SPTR` row is a live hazard there, not
  a theoretical one.
* files-3: should the 8.3 rule move into `fm_editkey` for every naming prompt
  (+~14), or is a commit check on the Save box alone the right size?
* files-18 is not on ABI-BREAK-LIST; the owner may want it there as a
  candidate row for the next flag day.
