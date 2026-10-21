# What Cut/Copy/Paste costs `kern_big`, routine by routine

**This is a measurement, not a description of the current tree.** It was taken 2026-10-18 on a four-core cloud
container with `nasm` 2.16.01, at commit `2a869324` (`origin/elendilon`,
build 1045). On that tree `kern_big`'s `KERN_SIZE` is 97,280 of
`KERN_BUDGET` 100,864. It holds for that commit only. A later measurement goes in a new file.

## The question

> Every kernel size pass offers to move the file manager's copy/paste/move
> into a module, as `kern_small` does. Big can't do that: it keeps those
> abilities resident and always available. But the size, "2.9k" iirc, is
> always huge. Small doesn't lose read, write, delete or list without the
> module, and copy/paste/move is just those combined. So what takes 2.9 KB?
> Is the number even accurate? And can copy/paste/move fit in less than 1 KB?

## The short answer

- **The copy engine (`kernel/filecp.inc`) is 2,163 resident bytes on
  `kern_big`**: 2,011 of `.cold` and 152 of `.bss`. That figure is exact. The
  total ~3 KB comes from adding the UI code in `files.inc`: another
  **~930 bytes** that is resident on both kernels and that a module would not
  move. Pass 13's "~3.2 KB" (HANDOFF-KERNEL-SIZE-P14) is a different number:
  FILECP plus FDLG together.
- **It overcounts in one place.** 88 bytes of the engine (`fcp_goto` and its
  far cell) are not copy code at all. They are the quiet change-folder
  routine, used by five other files, and they live in `filecp.inc` only by
  history. Another 95 bytes are the published `OSAPI_FILE_COPY` entry point,
  which packages call.
- **Strings are 64 bytes of the whole feature, and none are in
  `filecp.inc`.** The engine is entirely glue over the read, write, delete
  and list primitives you named, and those are already resident and shared.
- **Folder merging, meaning adopting a folder that already exists, is about
  six bytes.** The folder *recursion* around it is 363 bytes.
- **Under 1 KB cannot be reached by trimming without dropping features.**
  The no-loss changes found here are worth an ESTIMATED 350-450 bytes, which
  gets the engine (less `fcp_goto`) to roughly 1.45-1.6 KB. Getting below that means dropping
  multi-select lists, moves that relink instead of copying, or the
  free-space check, and those are your call. §5 has the list.

## 1. How it was measured

The kernel was re-assembled from `kernel/kernel.asm` with the same defines as
`make`'s pass 1, plus a `[map all]` map and a `-Le -Lf` listing. Each
top-level label's size is the distance to the next top-level label in its
section. Each emitted byte was then attributed to its `filecp.inc` source line
and summed per enclosing `%ifdef`. Both methods give **2,011** bytes of
`.cold`, so they agree to the byte. `kern_small` was assembled the same way
with `-DKERN_SMALL` to get the module split. The scratch scripts are not
committed. To redo the measurement, assemble with `[map all]` and read the
`fcp_*` rows of `.cold` and `.bss`.

## 2. The engine, by function (`kern_big`, `.cold`)

| group | bytes | routines |
|---|---:|---|
| **not copy: the quiet change-folder routine** | **88** | `fcp_goto` 84, `fcpf_fcp_goto` 4. Called by `assoc.inc`, `ascw.inc`, `desksc.inc`, `diskops.inc` and `compress.inc`; stays resident on `kern_small` too |
| **published API `OSAPI_FILE_COPY`** (SPEC.md 22.24) | **95** | `fcp_door` 91, `fcpf_fcp_door` 4 |
| clipboard entry points | 217 | `fcp_arm` 79, `fcp_paste` 78, `fcp_answer` 47, `fcp_n13` 7, `fcp_x5` 6 (shared register epilogue) |
| operation driver: start, step, suspend, stop | 292 | `fcp_step` 172, `fcp_run2` 57, `fcp_stop` 25, `fcp_here` 22, `fcp_unbatch` 9, `fcp_run` 7 |
| **file stream**: read a chunk, write or append it, retry, undo | **497** | `fcp_xfer` 177, `fcp_rdnext` 136, `fcp_file1` 67, `fcp_room` 51, `fcp_undo` 26, `fcp_took` 14, `fcp_wprep` 14, `fcp_file2` 12 |
| **folder recursion** | **363** | `fcp_scan` 186, `fcp_fnames` 47, `fcp_mkroot` 40, `fcp_push` 32, `fcp_ensure` 29, `fcp_mksub` 15, `fcp_faddr` 10, `fcp_frame` 4 |
| copy buffer: claim and chunk size | 107 | `fcp_claim` (cluster size of BOTH volumes, floor, DMA-safe claim, round) |
| same-volume move by relinking | 144 | `fcp_relink` 100, `fcp_rlup` 44 (repoints a moved folder's `..`) |
| multi-select list (SPEC.md 22.27) | 89 | `fcp_lnext` 63, `fcp_lfree` 21, `fcp_lend` 5 |
| safety: refuse pasting a folder into itself | 68 | `fcp_selfchk` 56, `fcp_srcclus` 12 |
| volume-switch thunks | 28 | `fcp_gosrc`/`gofs`/`gos`/`godst`/`gofd`/`god` |
| delete the source after a cross-volume Cut | 23 | `fcp_srcdel` |
| **total** | **2,011** | |

The same bytes, grouped by which `%ifdef` they are under:

| condition | bytes | what it is |
|---|---:|---|
| unconditional | 1,659 | |
| `OS88_REDIR` | **174** | redirected volumes (SPEC.md 62.9): RAM disk, cable, mounted images. These are four `drv_fs_call` arms (enumerate, read-at, change folder, and the 36-byte server-side copy) plus their volume-kind checks, because `dsk_dirw_*` and `dsk_read_chain` cannot walk a volume with no FAT |
| `FM_MSEL` | 136 | multi-select lists. `fcp_lnext` plus the list checks in `fcp_arm`/`fcp_paste`/`fcp_answer` |
| `OS88_BIGVOL` | 26 | volumes over 32 MB |
| `KERN_BIG` | 16 | the switch to `WRITE_SEQ`'s held commit |

**`.bss`, 152 bytes:** the clipboard record (19), a copy of it for the
running operation (19) plus 3 status bytes, three 13-byte name buffers (39),
the 36-byte descent stack (six folder levels × 6), the list's segment and
count (4), and 32 bytes of stream and step state. `kern_small` keeps 68 of
these resident and puts 80 in the module's own bss. Those are the clipboard,
the operation record and two names, which must outlive the module
(MODULE-SELFCONTAIN-PLAN §3.4).

## 3. What sits OUTSIDE the engine, and stays resident on both kernels

All of this is in `kernel/files.inc`. `kern_small` keeps it resident too
(apart from the multi-select rows), which is why your point about entry
interfaces holds.

| piece | bytes | |
|---|---:|---|
| drag-to-move (SPEC.md 22.4) | 367 + 10 `.bss` | `fm_dgdrop` 152, `fm_drag` 97, `fm_dgwait` 59, `fm_dgxor` 28, `fm_dgmove` 21, `fm_dgabs` 10 |
| redraw after a paste | 215 | `fmv_reload_all` 117, `fm_paste_res` 65, `fmv_repaint_all` 33. Only the paste path calls these, via `fm_paste_res` (the desk-shortcut paste in `desksc.inc` reuses it) |
| putting a SET on the clipboard | 102 | `fm_armset` 56, `fm_dgarm` 24, `fm_mw_rec` 20, `fm_mw_1st` 2 (`FM_MSEL`) |
| menu command bodies | 60 | `fm_c_cut`/`copy`/`clip`/`paste`/`pastein`/`pasteto`/`pasteno` |
| menu cells, `fm_jmp` entries, Ctrl+X/C/V | ~65 | ESTIMATED from the source: 9 menu-cell words, 9 id bytes, 4 jump words, 6 key compares |
| the Replace question's key handler and status line | 46 | `fm_editkey.replace` 26, `fm_draw_status.fs_st_rep` 20 |
| **strings** | **64** | `'Cut'`, `'Copy'`, `'Paste'`, `'Paste Into'` (26); `'Replace '`, `'?'`, `'Enter=yes  A=all  Esc=stop'` (38). Error text is `OSAPI_FERR_TEXT`'s, shared with everything |
| **total** | **~930** | |

**The whole feature on `kern_big` is therefore about 3.1 KB.** That is your
"2.9k". A `kern_small`-style module would move about **1.9 KB** of it: the
engine, less what has to stay resident (`fcp_goto`, the far stubs and the
68-byte clipboard state).

## 4. Why "read + write + delete + list, combined" is 2 KB of glue

The primitives are resident and the engine calls them: `dskw_stat`,
`dskw_write`, `WRITE_SEQ`, `dskw_mkdir`, `dskw_delete`, `dskw_rmtree`,
`dsk_read_chain`, `dsk_dirw_*`, `dsk_synth`, `mem_claim`. The glue is large
because of what those primitives do *not* do:

1. **They act on ONE current volume and folder** (`[disk_drive]`,
   `[dsk_cwd]`). A copy has two of each. So every step is "switch to the
   source, act, switch to the destination, act". That is 28 bytes of thunks,
   the 84-byte switch itself, and two or three bytes at each of 19 call
   sites.
2. **A file can be bigger than any buffer.** `dskw_read` and `dskw_write`
   take the whole file, so the copy is a stream: open, read a chunk, write
   the first chunk, append the rest. It sizes its chunk to the larger of the
   two volumes' clusters, or bytes are lost (SPEC.md 22.5, which records a
   116 KB file arriving 64,512 bytes long). That is the file-stream row plus
   `fcp_claim`: **604 bytes**. Less of that is chunking than the name
   suggests. The writes already go through `WRITE_SEQ` on `kern_big`. The
   read side is 150 (`fcp_rdnext`, `fcp_took`), the buffer is 107
   (`fcp_claim`), and the loop with its create-then-append and close is most
   of `fcp_xfer`'s 177, which includes the 36-byte server-side copy. The
   remaining 170 is not chunking at all: the overwrite check and question
   (`fcp_file1`, `fcp_file2`, 79), the free-space check (51), undo (26) and
   argument setup (`fcp_wprep`, 14).
3. **A folder tree has no call stack to recurse on** (SPEC.md 22.3: a
   384-byte task stack and an untrusted directory tree). So the walk keeps
   explicit frames and re-finds its place by ordinal: **363 bytes**.
4. **It must be able to stop and ask "Replace?"** from inside that walk,
   with the gfx lock held. So the walk is a resumable state machine, with
   `fcp_step`/`fcp_answer`, the `pend`/`ovw`/`started` flags and the split
   into `fcp_file1`/`fcp_file2`. Roughly 150-200 bytes are there only to
   suspend and resume. This is an ESTIMATE, because those bytes are spread
   through the driver.
5. **It refuses to do damage:** no paste of a folder into its own subtree
   (68), a free-space check before writing (51), and deleting a half-written
   file on failure (26).
6. **Four features on top:** relinking for same-volume moves (144),
   multi-select lists (136), redirected volumes (174) and the package API
   (95).

**Folder merging specifically** is `fcp_ensure`'s `test al, DSKW_DIR` /
`jz .clash` / `ret`. If the folder already exists, it is adopted and the walk
goes on into it. That is about six bytes. Making the folder (`dskw_mkdir`) is
a call to a primitive. The remainder of the 363 bytes is recursion.

## 5. Recommendations

None of these is built. Every figure is an ESTIMATE from reading the code,
and each one is labelled with what it would cost.

### 5.1 Free, and worth doing whatever else is decided

- **Move `fcp_goto` and `fcpf_fcp_goto` (88 bytes) into `disk.inc`**, next to
  `dsk_chdir_q`, which is what it wraps. It saves no bytes. It does take the
  routine out of the copy feature's account, where size passes keep quoting
  it, and it retires `assoc.inc`'s `%error` that ties `assoc_go` to FILECP
  not being a module. After the move, the copy engine proper is
  **1,923 + 152**.

### 5.2 No behaviour change: ~350-450 bytes

- **`fcp_scan` (186) repeats `dsk_find_x`'s directory walk** (SPEC.md
  19.7.1, 246 bytes, resident). Both start a directory walk, skip
  `0x00`/`E5`/`.`/space entries and hidden, system and label attributes with
  the same mask, stage the entry through `dsk_synth_x` into `dsk_ent`, and
  carry the same `FSV_ENUM` arm for redirected volumes. They differ in two
  ways: `fcp_scan` jumps straight to the sector holding its raw ordinal,
  while `dsk_find_x` counts visible entries from the start of the directory;
  and `dsk_find_x` also fills a caller buffer.
  - (a) Calling `dsk_find_x` directly with a scratch buffer would cut
    `fcp_scan` to ~40 bytes, **saving about 145**. The price is that each
    step re-reads the folder from its first sector. That costs nothing while
    the directory cache (SPEC.md 19.2.3) holds the folder, and becomes one
    `int 13h` per earlier sector once the cache has been dropped to free
    memory.
  - (b) Factoring one shared "walk from a raw ordinal" core under both would
    **save about 100** with no speed change. `dsk_find_x` would then get the
    sector skip too.
  - (b) is the recommendation.
- **`dskw_rmtree` is a second iterative tree walker**: `dskw_rtbody` 184,
  `dskw_rt_scan` 57, `dskw_rt_byclus` 34 and their state, about 330 bytes. It
  follows the same discipline (explicit frames, no recursion) for the
  opposite job. One walker that takes a per-entry callback could serve both,
  **saving an ESTIMATED 150-250 across the two**. This is the largest
  no-loss item and the riskiest: delete consumes the folder as it walks and
  copy does not, which is why they diverged (SPEC.md 22.3).
  - **Correction, after building the rest:** the premise is wrong.
    `dskw_rmtree` keeps NO frames: it re-scans from slot 0, climbs by `..`
    and re-finds the child by cluster. So there is no shared walker to build.
    docs/reports/FILECP-SIZE-IMPL-2026-10-18.md §4 has the details.
- **Replace `fcp_rdnext` (136) and `fcp_took` (14) with
  `OSAPI_FILE_READ_SEQ`'s body, `dwf_dskw_read_seq`, on `kern_big` only:
  ESTIMATED ~100 bytes net** (about 110 of `.cold` out, 8 of `.bss` in: a
  16-byte cursor replaces `fcp_rclus`, `fcp_roff` and `fcp_rgot`). The
  write half is ALREADY streaming: since SPEC.md 18.4.9 every chunk after the
  first goes through `WRITE_SEQ` with `WSQF_HELD`, and the close at
  `fcp_xfer.fin` commits it. So the read half is the only part of the
  chunking that is still the engine's own. `READ_SEQ` also serves redirected
  volumes, through `dskw_read_at_x`, so `fcp_rdnext`'s `FSV_READAT` arm goes
  with it. `fcp_claim` stays: `READ_SEQ` wants a capacity that is a multiple
  of the source cluster and an append wants a multiple of the destination
  cluster, which is exactly the span it computes.
  - **Correction, 2026-10-18, same session.** The first version of this
    report refused this change. It claimed the cursor would go cold between
    every two reads and cost ~28 s on a 5 MB fixed-disk file. That was
    wrong. `dsk_mgen` moves on a committed write (`dskw_sync_x`), at the
    START of a hold and at its commit (`dskw_append_x.held`, `dws_commit`),
    and on a mount that re-reads a BPB (`disk_mount`). A held append that
    continues a hold (`.hmore`) moves nothing. So on a copy inside one
    volume, or between two fixed disks (whose BPB is banked, SPEC.md
    18.9.2), the cursor goes cold for the first two or three chunks only.
    Their offsets are under 130 KB, so the walk is negligible.
  - **What it still costs:** a copy whose OTHER side is a floppy re-reads
    that floppy's BPB at every switch, so the cursor is cold for every
    chunk. Each chunk then re-stats the source by name and walks its chain
    to the offset. For a hard disk to floppy copy of a 1.44 MB file that is
    roughly 23 walks averaging ~0.7 MB, about 2-3 s at SPEC.md 18.4.8's
    142 ms per MB. That is on top of a write that takes about a minute.
    `WRITE_SEQ`'s token already goes cold in exactly those cases, and the
    write side already pays the same lookup and walk for them. So this
    doubles an existing cost rather than adding a new kind of cost.
    `kern_small` has no `READ_SEQ` (it answers `FERR_NAME`), so there the
    engine keeps its own reader, behind a second `%ifdef KERN_BIG` beside
    the one `fcp_xfer` already has.

### 5.3 Feature decisions for the owner, largest first

| drop | saves | what is lost |
|---|---:|---|
| multi-select lists on the clipboard (`fcp_lnext` and its checks; `fm_armset`, `fm_dgarm`, `fm_mw_rec`) | ~242 | copying or dragging a SET. You could keep the selection and only refuse it as a clipboard source. `make MSELOFF=1` already drops this, along with the rest of SPEC.md 22.27 (755 bytes in all) |
| relinking for same-volume moves (`fcp_relink`, `fcp_rlup`) | 144 | a move inside one disk becomes a copy and delete: slower, and it needs free space equal to what is moved |
| the free-space check (`fcp_room` and its stores) | ~60 | a copy onto a nearly full disk writes until the disk fills before it fails. `fcp_undo` still removes the partial file, so nothing is left behind, but the time is spent |
| the server-side copy arm in `fcp_xfer` (`FSV_COPY`) | 36 | a same-volume copy on a redirected volume (cable, RAM disk) streams through this machine instead of being done at the far end. On a serial cable that is a real slowdown |
| `OSAPI_FILE_COPY` (`fcp_door`) | 95 | **not available**: it is a published slot, so removing it is an ABI break (SPEC.md 20.2.0.1) |

### 5.4 What under 1 KB would take

From 1,923 (after §5.1), §5.2's ~350-450 brings the engine to **about
1.45-1.6 KB**, with no lost behaviour. Every row of §5.3 except the ABI one
takes off about another 480. The result is **~1.0-1.1 KB**, and a machine
built that way can no longer copy a set, move without copying, or avoid
filling a disk to learn that it is full. Under 1 KB, *with* the folder
recursion and the Replace question, means taking those and also giving up
redirected-volume support in the engine (174). That support is what makes
copy work to and from the RAM disk and the cable.

The one route to well under 1 KB resident that keeps every feature is the
one already refused for availability: the module, which saves ~1.9 KB. A
hybrid (resident single-file copy and move, with folders and lists in the
module) would make copying a folder need the system disk while copying a
file would not. That is still an availability change, just a narrower one.

**Recommendation:** take §5.1 now. Take §5.2(b), the shared directory walk,
and the `READ_SEQ` swap as the next size pass's FILECP items. Cost the shared tree walker as its own
design. Decide §5.3 row by row as features rather than as size items.
Nothing in this report argues for the module on `kern_big`.
