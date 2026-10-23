# Cut/Copy/Paste size plan: built and measured

**This is a measurement, not a description of the current tree.** It was
taken 2026-10-18 on a four-core cloud container with `nasm` 2.16.01 and
MartyPC built from this tree. It compares commit `2a869324`
(`origin/elendilon`, build 1045, called *base* below) with the commit on
branch `disk-copy-size` that adds this report. The plan it implements is
sections 5.1 and 5.2 of docs/reports/FILECP-SIZE-2026-10-18.md. It holds for that pair
of trees only. A later measurement goes in a new file.

## 1. What was asked, and what came of each item

> Implement the 350-450 plan, and measure it.

| item (FILECP-SIZE section) | estimate | built | kern_big result |
|---|---:|---|---:|
| 5.1 move `fcp_goto` to `disk.inc` | 0 | yes, as `dsk_goto` / `dskf_dsk_goto` | **+3** (a tail jump replaces a fall-through) |
| 5.2 shared directory walk | ~100 | yes, `dsk_dscan` | **-68** |
| 5.2 read through `READ_SEQ` | ~100 | yes, and `fcp_xfer`'s own stat removed with it | **-129** |
| 5.2 one tree walker with `dskw_rmtree` | 150-250 | **no**, see section 4 | 0 |
| **total** | **350-450** | | **-194** |

The plan delivered 194 of the 350-450 bytes. The tree walker was the largest
item, and its premise turned out to be wrong.

## 2. Bytes

Each kernel was assembled from source with `make`'s pass-1 defines and a
`[map all]` map, so these are section lengths and symbol spans, not rungs.

| | base | after | delta |
|---|---:|---:|---:|
| `kern_big` `.text` | 42,716 | 42,716 | 0 |
| `kern_big` `.cold` | 37,522 | 37,328 | **-194** |
| `kern_big` `.bss` | 5,227 | 5,227 | 0 |
| `kern_small` `.text` / `.cold` / `.bss` | 31,133 / 21,970 / 2,979 | the same | **0 resident** |
| `kern_small` `.modp` (`FILECP.DRV`'s image) | 1,855 | 1,837 | -18 |
| `kern_small` `.modpb` (`FILECP.DRV`'s bss) | 80 | 76 | -4 |
| `kern_dos` (`kerndos.bin`) | 31,752 | 31,752 | **byte-identical** |

On kern_big `KERN_SIZE` reads 97,280 before and after, because the change
crosses no rung. That is the rung rule's own example. The copy engine's own
account (`kernel/filecp.inc` on kern_big) went from **2,163 to 1,818**
(`.cold` 2,011 to 1,666, `.bss` 152 unchanged). That is 345 bytes less: 194
of it saved, 88 of it `dsk_goto` moving to `disk.inc`, and the rest the
directory walk now living in `disk.inc` too, shared with `dsk_find`.

Per routine, kern_big `.cold`, base to after:

| routine | base | after | delta |
|---|---:|---:|---:|
| `fcp_scan` | 186 | 62 | -124 |
| `fcp_rdnext` | 136 | 27 | -109 |
| `dsk_find_x` | 246 | 150 | -96 |
| `fcp_took` | 14 | gone | -14 |
| `fcp_xfer` | 177 | 163 | -14 |
| `fcp_goto` + `fcpf_fcp_goto` | 88 | gone | -88 |
| `dsk_goto` + `dskf_dsk_goto` | | 88 | +88 |
| `dsk_dscan` (new) | | 151 | +151 |
| `dskw_read_at_x` | 235 | 243 | +8 |
| `fcp_god` | 3 | 6 | +3 |
| `fcp_step` | 172 | 173 | +1 |

In `.bss`, `fcp_rclus`, `fcp_roff`, `fcp_rgot`, `fcp_rsz`, `fcp_rszh` and
`fcp_need` (16 bytes) became one 16-byte `READ_SEQ` cursor, `fcp_rcur`.

## 3. What each change is

- **`READ_SEQ` on kern_big** (SPEC.md 22.5.4). `fcp_rdnext` is one call to
  `dwf_dskw_read_seq`. The run coalescer, the byte-count stop, the short-chain
  `FERR_IO` and the redirected volume's `FSV_READAT` are all `READ_AT`'s, so
  the engine stops carrying a second copy of them. The cursor also holds the
  file's size, so `fcp_xfer`'s own `dskw_stat` went: `fcp_room` reads the
  size from the cursor, and the loop's end test is size minus offset. For
  that to hold on a redirected volume, `dskw_read_at_x`'s `DVK_FILE` arm now
  stores the size into the cursor as its FAT arm already did (+8 bytes).
  kern_small has no `READ_SEQ`, so it keeps its reader. Folding `fcp_took`
  into it saves the module 18 bytes of image and 4 of bss.
- **`dsk_dscan`** (SPEC.md 19.7.1). One routine takes a starting slot and a
  count of live entries to step over, and handles the `FSV_ENUM` arm once.
  `dsk_find` calls it with (0, ordinal) and `fcp_scan` with (its slot, 0), so
  each keeps its exact I/O shape. It is kern_big's alone. On kern_small the
  shared walk would be **resident** while the scan it replaces lives in
  `FILECP.DRV`. The first build measured that at **+61 resident bytes on
  kern_small**, which SPEC.md 39.27.4's diet forbids. kern_dos has no copy
  engine to share with. So both keep the walk they had, behind `DSK_DSCAN`.
- **`dsk_goto`.** This is the quiet change-folder routine that `assoc`, the
  ASSOC.DAT writer, desk shortcuts, the Sticky Apps menu, DISKOPS.DRV, the
  compressor and `OSAPI_FILE_GOTO_Q` all call. It now lives next to the
  `dsk_chdir_q` it wraps. `assoc.inc`'s `%error` tying `assoc_go` to FILECP
  not being a module is retired.

## 4. Why the tree walker was not built

docs/reports/FILECP-SIZE-2026-10-18.md §5.2 said `dskw_rmtree` *"follows the same discipline (explicit
frames, no recursion)"*. **It does not.** `dskw_rmtree` keeps no frames at
all. It re-scans the current folder from slot 0 every time, which works
because each delete consumes an entry. It climbs back up through the on-disk
`..` link (`dsk_dotdot`) and re-finds the child it just emptied by cluster
(`dskw_rt_byclus`). The copy engine keeps a frame per level because it does
not consume its source. Even the scans differ: rmtree's must see LFN
fragments and hidden or system entries, and copy's must not. What the two
could share is already shared: `dskw_scan1` with `dskw_isempty`, and
`dsk_byclus` with `OSAPI_FILE_PATH`.

A walker shared by both would also have to be resident on kern_small, where
today the copy walk is module code. That is the same diet refusal as in section 3.
So the item was costed and refused rather than built. Nothing was found to
replace its 150-250 bytes.

## 5. Two traps the build found

- **The old body was a fall-through target.** On kern_big the engine's four
  volume-switch thunks did not jump to `fcp_goto`. They fell into it,
  because it was written immediately below them. Moving the body silently
  re-pointed that fall-through at `fcpf_fcp_door`, a far entry, and it
  assembled cleanly. The tail is now an explicit `jmp dsk_goto`, which is the
  +3.
- **kern_dos assembles `disk.inc`.** Ungated, the move added 67 bytes to
  `kerndos.bin` (`dsk_goto` without its redirected arm) and the shared walk
  added another 61. Both are now gated (`%ifndef KD_BUILD`, and `DSK_DSCAN`
  is defined for kern_big only). `kerndos.bin` assembles byte-identical to
  base, which is checked by `cmp` against a base build in a worktree.

## 6. Speed: what the copy costs now

`tools/os88fcpcost.py`, run on MartyPC's `os8088_xt_hdd` (a 4.77 MHz 8088
with an XT-IDE fixed disk) once per tree, measures this fixture: a 360KB
floppy holding `TREE\` with 22 files in three levels, one of them a 200KB
file.

- **A** pastes B:\TREE into C:\. The floppy is the other side of every
  chunk, so the `READ_SEQ` cursor is cold each time.
- **B** pastes C:\TREE\SUB, including the 200KB file, into C:\. That is one
  fixed disk, where the cursor stays hot after the first chunks.

The time is guest cycles from `fcp_paste` to `fm_paste_res`, read with the
guest stopped.

| | base | base, 2nd run | after | after - base |
|---|---:|---:|---:|---:|
| A, ms | 39,333.2 | 39,514.9 | 39,398.3 | +65 (the two base runs differ by 182) |
| A, `int 13h` (HD read / HD write / FD read) | 30 / 157 / 86 | 30 / 157 / 86 | 30 / 157 / 86 | **0** |
| A, sectors (HD read / HD write / FD read) | 180 / 691 / 704 | the same | 180 / 691 / 704 | **0** |
| B, ms | 5,061.3 | 5,059.1 | 5,100.5 | **+39.2 (+0.77%)** |
| B, `int 13h` (HD read / HD write) | 39 / 87 | 39 / 87 | 39 / 87 | **0** |
| B, sectors | 574 / 528 | the same | 574 / 528 | **0** |

**The disk work is identical, call for call and sector for sector.** The
floppy copy's difference is smaller than the gap between two runs of the base
kernel, which is the floppy's rotational phase. The fixed-disk copy repeats
to within 2 ms, so its +39 ms is real. It comes from the cursor's cold first
chunks (one stat by name and a short walk each, CPU only, since the directory
sector is in SPEC.md 19.2.3's window) and from `READ_SEQ` copying its cursor
in and out on every call.

A first build kept `fcp_xfer`'s own stat beside `READ_SEQ`'s. It measured
**+249 ms in A and +82 ms in B**: one extra directory walk per file. Removing
it is what brought B to +39.

Both kernels left **every byte of all 22 files identical to the source**, at
both destinations, and no cluster chain shared by two entries. That was
walked on the host by `tests/instdeep.py`'s FAT reader.

## 7. Correctness gates

`make` passes, including the fast tier (52/52, with `stkbalance` and
`os88ovlchk`), and kern_small assembles. The scoped soak, run once after the
last change, covered the 18 rows this change can reach:

`fcpcopy` `fcpapi` `fcpsmall` `fcproom` `msel` `desksc` `dosmedia` `czto`
`czseq` `assocsweep` `assocfind` `stikmenu` `instassoc` `instdeep`
`pathcost` `rdcz` `rdpreserve` `rdxms`

**17 passed and 1 failed.** The failure is `msel`, at its step 7: a drag of a
two-entry selection onto GAMES never empties B:\ ("both gone from B:\ did
not happen in 120 GUEST seconds"). **It fails identically on base.** It was
run alone on both trees side by side, and each got the same 24 PASS lines
and then the same timeout at the same step. So it is red on the base branch
and not this change's. Every step of `msel` that reaches the code changed
here passes on both: the multi-select Copy, and the list Paste that walks
`fcp_lnext` -> `fcp_run2` -> `fcp_xfer` through the new reader.

What the 17 cover is the copy engine end to end on both kernels (`fcpcopy`,
`fcpsmall`, `fcpapi`, `fcproom`), `READ_SEQ` on a redirected source (RAM disk
copies in `rdcz`, `rdpreserve` and `rdxms`), `dsk_goto`'s callers (`desksc`,
`dosmedia`, `czto`, `czseq`, `assocsweep`, `assocfind`, `stikmenu`), and
`dsk_find` through the installer and `OSAPI_FILE_PATH` (`instassoc`,
`instdeep`, `pathcost`). One path is NOT covered by any row: copying a
FOLDER whose source is a redirected volume, which is `dsk_dscan`'s
`FSV_ENUM` arm as `fcp_scan` reaches it. `dsk_find`'s use of the same arm
goes through the identical instructions.

## 8. Where this leaves the under-1 KB question

The engine proper (`filecp.inc` on kern_big, not counting `dsk_goto`) is
1,666 + 152 = **1,818**. The feature decisions in docs/reports/FILECP-SIZE-2026-10-18.md §5.3 (lists
~242, relinking 144, the free-space check ~60, the server-side copy 36) would
take about 480 more, to **~1.34 KB**. Under 1 KB, keeping the folder copy and
the Replace question, now means dropping redirected-volume support from the
engine (63 bytes after this change, which moved most of it into `READ_AT` and
`dsk_dscan`) and still finding about 280 more. The structural lever that is
left is the module, which kern_big has decided against.
