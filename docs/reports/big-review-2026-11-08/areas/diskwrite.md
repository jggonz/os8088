# The FAT write path and fsck

Reviewer area key: `diskwrite`. Tree: `the-big-review` at `5e4740b`.

## Scope read

| file | lines | read |
|---|---:|---|
| `kernel/diskw.inc` | 6,831 | every proc: the doors, gates, mkdir/rmdir/rmtree, write/read/delete/rename/stat, the run engine (`dskw_runadd`, `dskw_flushrun`, `dskw_xclus`), the compressed-read placement, READ_AT / READ_SEQ / WRITE_SEQ / WRITE_AT / APPEND / truncate, `dskw_commit`, `dskw_find`, `dskw_ent_store`, `dskw_alloc`, `dskw_free_chain`, `dskw_setfat`, `dskw_flush_x`, `dskw_refat`, `dskw_now`, `dskw_name83`, the whole `FORMAT.DRV` image and its resident stubs, the `.bss` block and the far entries |
| `kernel/fsck.inc` | 3,197 | every proc: the resident `osapi_vol_check` stub, `fsk_entry`, `FSK_DELSYS`, BEGIN/STEP/END, the queue, FAT compare, the tree walk and its check / trim / defrag handlers, the FAT scan, lost-chain save, FAT sync, the defragmenter (`fsk_findrun`, `fsk_move`, `fsk_link`, `fsk_kids`, `fsk_krefs`, `fsk_kernok`) |
| `kernel/disk.inc` | targeted | mount rules 3-16 (`:5037-5300`), `dsk_fatw_park` (`:5760-5815`), rover reset (`:4130-4145`), `dsk_free_clus_x` (`:6337`), `dsk_runcap` (`:2576`), the `dsk_relist` header (`:8604`) |
| `kernel/kernel.asm` | targeted | the file N-stub's `api_copyname` / `api_name` (`:5192-5215`), `api_file_sysc` |
| SPEC.md | 18.4 (`:34471-34815`), 18.4.4, 18.4.7.x, 18.4.10, 20.14.1, 38.2's note, 22's `fmv_*` table (`:51770`), 118.14-118.14.9 (`:229416-229800`), 18.101.2-3 | |
| docs | `ABI-BREAK-LIST.md` (rows C2, C4, L1), `KERNEL-BYTE-QUEUE.md`, `LAST-DROP-BYTES.md` 8.x, `DISK-CPU-PLAN.md` headings, `CODE-REVIEW-TWO-CYCLES-2026-11-01.md` (only "the diskw high-carry helpers", checked sound) | |

Not re-derived here: `dsk_xfer`, the FAT window's load/evict policy, `disk_mount` beyond the rules cited. They belong to the disk-read reviewer. Where a finding leans on them, the evidence line says so.

## Verdict

The write path is the best-argued subsystem in the tree. The commit order (data, FAT flush, entry, free, flush) is kept by every mutator: write, mkdir, delete, rmdir, rmtree, truncate, append-grow, held streams, fsck repairs and defrag moves. Replace is shadowed, so a power cut never costs the user's existing bytes. That is a real improvement over DOS, which truncates in place. Every LBA is derived and every chain walk is bounded.

The defects are at the edges the prose did not reach:

- **Hostile `FileSize` crashes two divides.** The fix exists in the truncate arm and was never carried to its two siblings.
- **The FAT window can make a failed write leak space for good.** The window replaced the whole-FAT snapshot, and since then a failed write on a multi-window FAT16 volume can turn the free space it walked into permanent lost clusters.
- **An unreadable FAT window reads as "Disk full".** That is the misreport `dskw_alloc`'s own header forbids.
- **The UI-task rule is still enforced by nothing.**

On the design side:

- **The whole-file model is under-used.** The kernel knows a file's size before it writes a byte, and does not use that to refuse early or to allocate contiguously.
- **Folders never grow.** This is DOS 2.0 behaviour os8088 never adopted.
- **The compression hint sits in bytes other systems use.** It lives in three dirent bytes that Windows NT, Linux, macOS and OS/2 all give a meaning to. Every packed file on every shipped disk lists in lower case on a modern host.

fsck is sound in its model: kernel image, package-owned state, a generation counter, and repairs ordered like the write path. Its findings here are one parse bug and stale documentation.

## Strengths

1. **Shadow replace, one commit point.**
   - *Evidence:* `dskw_wbody` (`diskw.inc:1576-1597`). The old chain is held until the entry write lands; `dskw_relold` frees it after.
   - *Why:* DOS's create-over-existing truncates first, so a crash loses both versions. Here a crash loses at most lost clusters. Mac MFS/HFS File Manager rewrites in place too. Keep it.
2. **One order, everywhere.**
   - *Evidence:* mkdir (`:666-743`), delete/rmdir (`:906-913`, `dskw_dbody`), truncate (`:3838-3879`, entry first, then FAT), grow (`.seal`, `:4007-4057`), and fsck's repairs and moves (`fsck.inc:1704-1724`, `:3105-3191`).
   - *Why:* the same argument ("the worst a stop leaves is LOST CLUSTERS") is checkable at each site.
3. **Hostile-disk discipline.**
   - *Evidence:* every cluster passes `dsk_clus2lba_x`/`dskw_clok`. `dskw_free_chain` is bounded by `dsk_maxclus` (`:4801-4816`). Directory walks go through the `DSK_DIRW_MAX` guard. A size whose sector count does not fit 16 bits is refused before I/O (`dskw_size32`).
   - *Why:* this kernel assumes every disk is hostile, and DOS assumed none was.
4. **Allocation marks before it returns.**
   - *Evidence:* `dskw_alloc` writes EOC on the spot and tells "full" (AX=0) apart from "could not mark" (AX=FFFFh) (`:4716-4731`).
   - *Why:* a half-built chain can never hand a cluster out twice.
5. **Runs that cross clusters, capped by the DMA page from the advancing segment.**
   - *Evidence:* `dskw_runadd`/`dskw_flushrun` (`:1709-1848`). One sector per 64KB page is staged; the zero-filled tail is staged too (`dskw_wone`).
   - *Why:* the 8237 page bug cannot be reached from any caller buffer, and a contiguous file is one int 13h per track.
6. **`dskw_find` takes the entry while the sector is in hand** (`:4421-4434`), and **`dskw_ent_store` always re-reads before it patches** (`:4590-4669`).
   - *Why:* the first drops a whole revolution per open. The second means a stale `dsk_secbuf` can never resurrect neighbours. Both halves are right.
7. **The protection tiers are a routine, not copies.**
   - *Evidence:* `dskw_pmask` (`:220-315`). `dskw_imgro`'s virtual read-only bit lives only in the staged copy (`:247-282`).
   - *Why:* a mounted image's file cannot be destroyed, and nothing is written to the disk to say so.
8. **`dskw_rmtree` uses the disk's own `..` instead of a stack, and re-checks emptiness at every removal** (`:977-1203`).
   - *Why:* 384-byte task stacks make recursion a non-starter. Here a disagreement between the two scans is a refusal and not a lost subtree.
9. **fsck's concurrency model.**
   - *Evidence:* the kernel holds nothing between calls (`fsck.inc:20-22`). `dsk_wgen` plus `dsk_sigcur` plus `dsk_maxclus` are compared at every step (`:770-787`). Moves only ever write into free clusters. Folder moves are gated on no other package being open (`fsk_foldok`, asked again before the link).
   - *Why:* DOS CHKDSK /F and DEFRAG need an idle machine. This one checks a live desktop and stops cleanly when the disk moves under it. Copy this pattern for any future long-running kernel service.
10. **The formatter's probe separates the medium from the drive** (`:5590-5837`), and the reach test **writes a marker** rather than trusting a read (`:6064-6111`).
    - *Why:* this is the honest answer to the 40-track-in-80-track trap, and the right shape for any "is it really there" test.

## Findings (ranked)

### dw-1 Hostile `FileSize` faults two divides (bug, medium, now)

**Evidence:**
- `diskw.inc:3930-3932`: `dskw_wabody .div` does `div bx` on `[dwr_off]` with no guard. On APPEND the offset is the entry's size (`:3786-3791`), and `.at` accepts it whenever the size is cluster-aligned.
- `diskw.inc:3168-3170`: `dskw_read_at_x` does `div word [dwr_clb]`. The offset is the caller's, bounded only by the entry's size (`:3128-3134`).
- The truncate arm alone guards it: `cmp dx, bx / jae .corrupt` (`:3850-3851`). SPEC 18.4.7.5 states the reason: "because the divide would otherwise fault inside the file layer".

**What happens:** the quotient overflows when the high word of the offset is at least the cluster size, which on a 512-byte-cluster volume means an offset of 32MB or more. Take a floppy entry claiming 0x02000000 bytes. Then:
- `OSAPI_FILE_APPEND` on it faults;
- any DOS program that lseeks past 32MB of it and reads faults (DOS.O88's window is READ_AT).

**What the fault does:** no handler for INT 0 exists in the tree.
- On an 8088 with the BIOS dummy it continues with garbage AX.
- On a 286+ it re-executes the `div` forever.
- If a DOS program's runtime hooked INT 0, its handler runs in kernel context.

**Recommendation:**
- Before each `div`, add `cmp dx, <clb> / jae <FERR_IO arm>`. `.jcor` is in reach at `:3932`. READ_AT needs `stc` before its `.rdend`.
- The comments `:3165-3167` and `:3930-3933` claim "the offset is under 33MB (SPEC.md 18.7)". That is false on OS88_BIGVOL and irrelevant to a hostile entry, so fix them in the same edit.

**Bytes:** about +11 `.cold` resident on kern_big (+4 wabody, +7 read_at), about +4 on kern_small (append door only).

### dw-2 A failed write on multi-window FAT16 leaks what the window flushed (bug, medium, now)

**Evidence:**
- `dskw_wbody .rollback` (`:1600-1602`) and `dskw_wabody .rbdrop` (`:4089-4092`) only call `dskw_refat`, which is an invalidate with no I/O (`:5014-5019`).
- Its own header admits "an eviction may already have flushed part of a half-built chain" (`:5008-5012`).
- `dskw_alloc`'s FAT16 scan and `dskw_setfat`'s links move the window across a FAT longer than 9 sectors. `dsk_fat_window` flushes before it evicts, so the chain built so far lands on disk as an orphan.

**What happens:** when the failure is FERR_FULL, the write has walked every free cluster it could get. Every window it left is now allocated on disk and unreachable.
- The volume reads "full" until a System Check repair, even after the user deletes the file they were saving.
- A first-time user on a hard disk hits this by copying one file too large.
- FAT12 floppies are immune (whole window), and so are held streams: `.rollback`'s HELD arm frees its own sub-chain first (`:4069-4087`), which is the right pattern.

**Recommendation:** give the plain arms the HELD arm's treatment.
1. `mov ax, [dskw_first] / call dskw_free_chain / call dskw_flush_x`.
2. `dskw_refat` only if that flush fails.

The chain is linked in RAM (each link is a `setfat`), and evicted windows fault back in with the flushed links, so the walk frees exactly what was built. On FAT12 it is RAM-only work.

**Bytes:** about +12 `.cold` resident, both kernels (kern_small has only the wbody arm, about +9).

### dw-3 An unreadable FAT window reports "Disk full" and retries per cluster (bug, low-medium, now)

**Evidence:**
- `dskw_alloc .one` (`:4703-4713`): `call dsk_next_clus_x / jc .skip` treats a load failure as "used" and goes on to the next candidate. The rover keeps candidates in range, so the CF can only be I/O.
- The FAT16 `.scan` falls back to `.one` on the same failure (`:4749-4750`) and asks again for the next cluster.
- When both FAT copies are unreadable, every candidate costs a failed load (three attempts and resets each). The answer is then AX=0 CF=1, which callers map to FERR_FULL.

**What happens:** the header (`:4680-4684`) names exactly this as the bug it exists to prevent: "a disk that answered an I/O error while there were thousands of free clusters on it used to say 'Disk full'".

**Recommendation:** in `.one`, `jc` to a `pop ax / stc / jmp .iofail` that returns 0FFFFh.

**Bytes:** +5 `.cold`, both kernels.

### dw-4 The UI-task rule has nothing behind it (hazard, medium, now)

**Evidence:**
- SPEC 18.4: "The kernel cannot enforce this; it is an author rule with nothing behind it".
- `diskw.inc:27-31`.
- §20.6 rule 7.
- Every body shares `.bss` scratch (`dskw_raw`, `dskw_n83`, `dsk_secbuf`, the dirty range).
- The N-stub stages names into one `api_name`/`api_name2` pair (`kernel.asm:5213-5214`).

**What happens:** a worker calling a file cell while the UI task is inside another cross-links the FAT silently.

**Recommendation:** the kernel already answers "am I the UI task" in five places with `cmp byte [sch_cur], 0` (`memory.inc:955`, `instance.inc:2203`, `toast.inc:407`). Put that test once in the file cells' common N-stub (or in `dskw_mounted`/`dskw_gate`, which every body passes) and refuse `FERR_NODISK`.

Prior art:
- DOS's InDOS flag made re-entry *detectable*.
- The Mac File Manager *queued* async calls rather than run them concurrently.

os8088 needs neither: a refusal is enough, because the rule is already the contract.

**Before taking it:** confirm no kernel path (kerndos's DOS context, HIBER, drivers) calls the bodies off task 0. `KD_BUILD` is the one to check.

**Bytes:** about +9 resident in the N-stub `.text`, or about +14 `.cold` in the two gates.

### dw-5 A failed flush leaves the dirt behind for `dsk_fatw_park` to land elsewhere (hazard, low, now)

**Evidence:**
- `dskw_flush_x .ioerr` (`:4989-4992`) jumps straight to the pop run and skips `.reset`, so the dirty range survives a failed write.
- `dskw_relold` flushes "EITHER WAY" precisely because "dirt carried across a media change lands on the NEXT disk's FAT" (`:962-970`).
- `dsk_fatw_park` (`disk.inc:5794-5799`) flushes outgoing dirt at the next mount of another volume, before it can know a floppy was swapped.

**What happens:** a post-commit flush failure, then a swap, then a switch, writes the old disk's FAT sectors onto the new one.

**Recommendation:** `.ioerr: call dskw_ioerr / call dskw_refat` (drop the range and the window). A post-commit failure costs lost clusters, which is what the order chose; a pre-commit failure's caller refats anyway.

**Bytes:** +3 `.cold`.

### dw-6 The INSIDE write rounds its tail from past the caller's buffer onto disk (hazard, low, now)

**Evidence:**
- `dskw_wabody`'s INSIDE arm (`:3954-3966`) hands `dsk_read_chain_x` `[dskw_rem]` whole sectors straight from `ES:BX`.
- `.loose` (END at or past the size) allows any count (`:3815-3831`), so up to 511 bytes after the caller's buffer go to the slack.
- SPEC 18.4's binding rule says otherwise: "**No memory ever leaks past EOF** ... The kernel never hands a foreign machine the contents of whatever happened to sit after a package's buffer". The GROW arm keeps it (`dskw_wone` zero-fills, `:5053-5056`); this arm does not.

**What it leaks:** DOS.O88's window buffer, i.e. bytes of whatever file that window held before. That is the classic DOS slack leak, inherited.

**Recommendation:** either
- split the last partial sector off and send it through `dskw_wone` with `[dskw_clba]` aimed at it (the run engine already does this for the grow), or
- amend 18.4's rule to name this arm as the exception.

The first is about +25 `.cold`.

**Bytes:** about +25 `.cold` (kern_big only: kern_small has no INSIDE arm).

### dw-7 `DSKW_RT_MAX` bounds the size of a tree, not the depth of a cycle (bug, low, now)

**Evidence:**
- `dskw_rtbody .loop` (`:1142-1144`) decrements the guard on *every* iteration, every file deleted and LFN zapped included.
- A tree with 4,096 entries plus levels stops part-deleted with FERR_IO.
- The comment's premise is "A 1.44MB volume cannot legitimately need this many" (`:1023-1028`). A 32MB partition's installed program tree can.

**Recommendation:** re-arm the guard (`mov word [dskw_rt_guard], DSKW_RT_MAX`) after each successful removal.
- A cycle is descent with no removal, which a per-removal re-arm still catches at 4,095 consecutive descents.
- The append walk that shares the constant is unaffected.

**Bytes:** about +6 `.cold`, kern_big only (the band is `%ifndef KD_BUILD`).

### dw-8 kern_small's WRITE_AT stub answers CF=1 with AX = the caller's offset (bug, low, now)

**Evidence:**
- `diskw.inc:6805-6811`: `dwf_dskw_write_at: stc / retf` enters *below* `mov ax, FERR_NAME`, which its three neighbours fall through.
- os88api.inc's contract for the cell is "else AX = FERR_*". The offset's low word is often 0, which reads as FERR_OK with CF=1.

**Recommendation:** move the label above the `mov`.

**Bytes:** 0.

### dw-9 `dskw_find` matches the volume label (consistency, low, now)

**Evidence:**
- `:4407-4410` skips only `attr & 3Fh == 0Fh`.
- A file or folder whose name equals the root's label is refused: FERR_PROT on write (0x08 is in `DSKW_PROT`), FERR_EXIST on mkdir.
- DOS matches the label only for searches that ask for attribute 08h.

**Recommendation:** `test al, 08h / jnz .next` replaces `and al, 3Fh / cmp al, 0Fh / je .next`, and skips both LFN and label entries. `fsk_delsys`'s own label test (`fsck.inc:665`) becomes redundant but harmless.

**Bytes:** -2 `.cold`, both kernels.

### dw-10 The compression hint occupies three bytes other systems use (interop, low-medium, flagday or now)

**Evidence:**
- `disk.inc:154-157`; SPEC 20.14.1's table. +12 is `NTRes`, set to 5Ah/5Bh. +13 is `CrtTimeTenth`, set to U's high byte. +20 is `FstClusHI`, set to U's low word.
- `dskw_commit` writes them (`:4195-4224`), and so does `tools/os88disk.py` for every package, since `PKGZ=lz4` packs them all.

What each host makes of those bytes:
- **+12:** Windows NT/2000+, Linux vfat (`shortname=mixed`, the default) and macOS msdosfs all read NTRes bit 3 (base lower-case) and bit 4 (extension lower-case). 5Ah has both set, so every packed file on every shipped disk lists as `notepad.o88` there.
- **+13:** a create time off by up to 2 s; above 199 it is an invalid value.
- **+20:** on FAT12/16, OS/2 and NT's FASTFAT treat +20 as the **EA handle** into `EA DATA. SF`.

SPEC 20.14.1 calls these "three bytes FAT12 and FAT16 leave free", and they are not.

**Recommendation:** the hint is a cache with a sniff fallback (`dskw_czknow`), so moving it is safe for reads.
- Pick a mark with bits 3 and 4 clear (40h-47h, A0h-A7h, and so on).
- Accept the old 5Ah/5Bh for one release cycle, about +6 in `dskw_czknow`.
- Have `os88disk.py` write the new mark.

Whether U may stay at +13/+20 should be checked against a real NT CHKDSK (open question 1). The alternative is to drop U from the entry and learn it from the 'CZ' header at open (`dsk_peek_x` already does this on the miss path, and SPEC 20.14.6 argues the peek is free under §18.95's cache). That keeps a 1-bit flag for FIND's +22 and frees +13 and +20.

**Phase:** an on-disk format change that every shipped disk carries. Batch it with the flag day, or do it now with the dual-mark reader.

**Bytes:** +6 transitional (`dskw_czknow` and the listing's reader), then ±0.

### dw-11 Short names with bytes 80h-FFh, or a leading 05h, are listed but unusable (interop, low, now)

**Evidence:**
- `dskw_char_x` (`:5217-5248`) refuses everything outside A-Z, 0-9 and `dskw_syms`.
- The FAT short-name set includes 80h-FFh (the OEM code page), and a name whose first byte is E5h is stored as 05h.
- The listing synthesises such names, but open, delete and rename all go through `dskw_name83`, so they answer FERR_NAME: a file a DOS machine wrote is visible and untouchable.

**Recommendation:** accept 80h-FFh in `dskw_char_x`, with no upcase for them as DOS did without COUNTRY, and map a leading E5h to 05h in `dskw_name83`.
- A font with no glyph for them is a display matter, not a reason to refuse the file.
- Prior art: DOS 3.3+.

**Bytes:** about +8 `.cold`.

### dw-12 Folders never grow (weakness, medium, now)

**Evidence:**
- `dskw_find` reports `DSKW_FNONE` when a subdirectory's chain ends without a free slot. Every create then answers FERR_DIRFULL (`:1548-1551`, `:692-693`).
- SPEC 24.x confirms (`SPEC.md:43108-43111`): "a folder the kernel makes on it holds 14 (§18.5's `FERR_DIRFULL`, which a directory never grows past)". That is a 1.44MB / 720KB folder at one 512-byte cluster.
- DOS 2.0 grew subdirectories from the day it had them.

**Recommendation:** when the walk ends on a subdirectory's chain with no slot, take a `dskw_dirgrow` step:
1. alloc;
2. zero the cluster with mkdir's existing loop, factored out of `dskw_mkbody :715-728`;
3. flush;
4. link the walker's last cluster to it;
5. flush;
6. publish its first sector as `[dskw_fsec]` with `[dskw_fend]`=1.

This keeps the commit order: a crash after step 4 leaves an empty extra cluster in the folder, which is harmless. `fcp_relink`, the other `dskw_take_slot` caller, gets it for free if the grow sits inside `dskw_find`'s no-slot exit.

**Bytes:** about +90 `.cold` resident, kern_big. It could be gated off kern_small, whose disks are 360KB with 2-sector clusters and 32-entry folders.

### dw-13 A whole-file write learns FERR_FULL only after writing everything it could (perf, medium, now)

**Evidence:**
- `dskw_wdata` allocates cluster by cluster (`:1662-1683`) and discovers a full disk at the first failing `dskw_alloc`, after writing every byte before it.
- On an XT that is seconds to minutes of floppy spent on a save that cannot land, followed by dw-2's leak on FAT16.

**Recommendation:** the kernel knows `[dskw_rem]` before the first sector, which DOS's incremental write never did. Compare `ceil(rem/spc)` against `dsk_free_clus_x` and refuse FERR_FULL before any data I/O.
- The count is CPU only. It is free on a floppy and about 105 ms on a 20MB disk (SPEC 18.4.5), against seconds of data.
- Prior art: the Mac File Manager's `Allocate`/`SetEOF` reserved space up front; OS/2 HPFS preallocates from the known size.

**Bytes:** about +18 `.cold`, both kernels. If dw-14 lands, its run search gives the count for nothing.

### dw-14 Allocate contiguously when the size is known (rethink, medium, now)

**Evidence:**
- `dskw_alloc` is first-fit from the rover, one cluster per call (`:4686-4790`).
- SPEC 18.4 says a full disk "fragments exactly the way DOS's did".
- The run engine (`:1621-1627`) then pays one transfer per fragment. On a floppy each fragment is also a seek, about 100 ms or more on the XT.

**Recommendation:** for `dskw_wbody` and the non-held grow, search the FAT window once for the lowest free run of N clusters at or after the rover. That is fsck's `fsk_findrun` idea applied to the FAT itself; the FAT16 arm is a `repne scasw` variant of `.scan`.
- Fall back to first-fit when no run is long enough.
- Prior art: Mac `PBAllocContig` and HFS clumps. GEOS's VM files were contiguous by design.
- What os8088 has that DOS lacked is the size up front. Use it.

**Bytes:** about +70 `.cold`, kern_big; optional on kern_small.

### dw-15 Defer the post-commit FAT flush to the gate held streams already use (rethink and perf, medium, now)

**Evidence:**
- `dskw_relold .flush` (`:962-974`) writes FAT1 and FAT2 again after every replace, delete, rmdir and truncate: two int 13h calls, about 0.8 s on the XT, per operation.
- A 50-file `rmtree` pays 100 of them.
- Dirt made only of *frees* is always safe to lose (lost clusters), and safe to reuse before it is flushed: the clusters are unreachable on disk.
- The thing that makes deferral dangerous is a floppy swap. WRITE_SEQ already solved that by committing at `gfx_unlock`, "which is every way the user can reach the drive" (`:3366-3372`; the `dwsf_commit` far entry `:6798-6801`).

**Recommendation:**
- `dskw_relold` frees and leaves the range dirty under a `dskw_lazy` byte.
- `gfx_unlock`'s existing hook (`dwsf_commit`) and `OSAPI_BATCH_END` flush it.
- `dsk_fatw_park` and the next write's own flush behave exactly as today.
- Prior art: DOS's FAT buffers flushed at close or disk reset, SmartDrive's write-behind, and HFS's lazy VBM/MDB flush. All of them deferred everything; this defers only what is safe to lose.

**Bytes:** about +15 `.cold`, kern_big (DSK_STREAM). kern_small keeps the immediate flush.

### dw-16 The rover resets at every navigation, and FAT12 allocation is three calls a cluster (perf, low, now)

**Evidence:**
- `disk_mount_x` stores `[dsk_rover]` = 2 unconditionally (`disk.inc:4136-4140`). That includes the `[dsk_keepcwd]` remounts every `dsk_chdir` performs (19.2).
- The first save after any navigation re-walks the used front of the FAT.
- FAT16 got `repne scasw` (18.4.10, about 4.5 s down to 0.29 s). FAT12 "keeps the loop" at about 1,400 cycles a cluster (18.4.10's own per-cluster figure): about 0.8 s on a nearly full 1.44MB disk, after every chdir.

**Recommendation:**
- Store the rover only on the non-keepcwd arm (`:4130-4132` already branches there). A same-disk remount cannot invalidate a hint. About 0 bytes.
- Optionally give FAT12 a whole-window scan in the style of `dsk_free_clus_x`'s fast path. About +40.

**Bytes:** 0 (rover); about +40 (FAT12 scan).

### dw-17 `rmtree` finds every file twice (perf, low, now)

**Evidence:**
- `dskw_rt_scan` stages the entry and its location via `dskw_hit` (`:1283-1292`).
- `.loop` then calls `dskw_dbody_n83` (`:1151`), whose first act is `dskw_find` again (`:2765-2766`): a second directory walk from entry 0 per file.

**Recommendation:** enter `dskw_dbody_n83` past its `dskw_find` (at the imgro and protection test) when `dskw_raw` is already staged.

**Bytes:** about 0 (a label and a `jmp`).

### dw-18 fsck's `FILEnnnn.CHK` number parses with an 8-bit multiply and wraps at 10,000 (bug, low, now)

**Evidence:**
- `fsk_lost_num .d` (`fsck.inc:2151-2163`) uses `mul dh`, which multiplies **AL** by 10. Once the three-digit prefix passes 255 (n ≥ 2,560) the number is wrong.
- `fsk_save .dig` (`:2298-2307`) writes four digits of P_CHKN, so 10,000 becomes `FILE0000`, a duplicate of an existing name.
- Neither can collide in a 224- or 512-entry root in practice, but both are plainly wrong.

**Recommendation:**
- `mov dh, 0 / mov cl, 10 / mul cx` style, keeping the loop count elsewhere.
- Saturate P_CHKN at 9,999 and treat the cap like a full root (free the chain), as DOS CHKDSK stops at FILE9999.

**Bytes:** about +6 in `FSCHECK.DRV`, none resident.

### dw-19 Taking the terminator in a sector's last slot trusts the next sector to be zero (hazard, low, record)

**Evidence:**
- `dskw_ent_store` (`:4648-4654`): "the next slot is the next sector's, which has never been written and is already 00".
- `fsk_save` makes the same assumption (`fsck.inc:2285-2291`).
- It holds for every directory DOS or os8088 formats or makes, since both zero new clusters. A foreign or hostile disk with non-zero sectors past the terminator would have their contents become live entries.

**Recommendation:** record only. A fix costs a second sector RMW in 1 case of 16, and every reader already treats entries as hostile.

**Bytes:** 0.

### dw-20 Pop runs and refusal tails shared across 1,000 lines, held by prose (weakness, low, now)

**Evidence:**
- `dskw_find.out/.pdi/.psi/.pdx` is the epilogue of six routines (`:4453-4463`): `dskw_rt_scan`, `dskw_isempty`, `dws_commit`, `dskw_alloc`, `dskw_wrp` and `dskw_read_x` (via `dskw_ent_store.pdi`).
- More than ten `X.out equ neighbour.ret` relays exist (`:942`, `:1927`, `:2184`, `:2775`, `:3541`).
- Refusals jump to another body's labels (`dskw_wbody.prot`, `dskw_rdata.corrupt`, `dskw_nbody.badarg`, `dskw_mkbody.exists`).
- Each one is correct today and each depends on a push order or a flag state written in a comment. One reordered push in `dskw_find` silently breaks five callers.

**Recommendation:** a `DSKW_PUSH6`/`DSKW_POP6` macro pair that defines the order once, used at every routine that jumps into the shared run. Zero bytes, and an assembly-time single source.

**Bytes:** 0.

### dw-21 Two "same question, two predicates" pairs (consistency, low, now)

**Evidence:**
- LFN detection: `dskw_find` masks (`and al, 3Fh / cmp al, 0Fh`, `:4408-4409`); `dskw_rtbody` compares exactly (`cmp byte [dskw_rt_attr], 0Fh`, `:1147`). An LFN slot with high attribute bits makes rmtree try to delete it as a file and stop with FERR_NOENT.
- Mounted images: the defragmenter skips any `*.IMG` but HIBERNAT by name (`fsck.inc:2640-2646`). `dskw_imgro` asks the claim table by cluster (`diskw.inc:269-282`), which is the actual fact.

**Recommendation:** use one predicate each.
- Mask in rmtree too.
- Let the defragmenter ask `dsk_img_next` by cluster, which also frees it to move an unmounted `.IMG`.

**Bytes:** about +2 resident (rmtree); about +10 image (fsck).

### dw-22 WRITE_SEQ has one kernel-wide stream record (rethink, low, flagday)

**Evidence:**
- `dws_rec` is a single record (`:6597-6614`); the token is `[dsk_mgen]` (`:3357-3364`).
- Two concurrent streams (an FTP upload and a DOS COPY, or two FTPD sessions) make each other's tokens cold every call. Each call then commits and falls back to an APPEND: a name lookup plus a walk of the whole chain (DISK-CPU-PLAN 6), which is O(n²) over the file.
- DOS's SFT had `FILES=` entries; READ_SEQ already made its cursor the caller's.

**Recommendation:** make the record caller-held, an `ES:DI` cursor like READ_SEQ's, with the *hold* still the kernel's (one at a time is fine; the committed position is what thrashes). Batch it with the C2/C4 rows.

**Bytes:** about 0 to -20 kern_big. It is an ABI change of OSAPI_FILE_WRITE_SEQ.

### dw-23 No "volume dirty" mark while dirt is pending (prior-art, low, record)

**Evidence:**
- A held stream (`dws_hold`) or dw-15's deferred frees can be pending across callbacks on a fixed disk (`WSQF_KEEP`, `:3445-3451`).
- FAT16 reserves FAT[1] bit 15 (`ClnShutBitMask`) for exactly this. AmigaOS's root-block bitmap-valid flag ran the disk-validator at the next insert. HPFS had a dirty flag in its spare block.

**Recommendation:** record. Setting the bit costs one FAT-sector write per transition. With System Check's `AUTOSTRT` link (118.14.9) a set bit at boot could trigger a check unattended.

**Bytes:** about +20 if ever taken.

### dw-24 Orphaned LFN entries on delete and rename (prior-art, low, record)

**Evidence:**
- SPEC 18.4: "Deleting or renaming a file a host created with a long name therefore leaves that name's LFN entries orphaned". The scan passes over them (`:4410`).
- Windows 95+ deletes the preceding LFN run whose checksum matches. ScanDisk and `fsck.fat` report the orphans otherwise.

**Recommendation:** record. Marking the contiguous preceding 0Fh entries E5h in the same sector is about 30 bytes and no I/O.

**Bytes:** about +30.

## Rethinks

- **Use the whole-file model's foreknowledge** (dw-13, dw-14). `OSAPI_FILE_WRITE` hands the kernel the size before the first sector, which DOS's handle model never did. Three things follow from it:
  - an up-front FERR_FULL;
  - a contiguous run search;
  - for the replace case, an explicit answer to "a nearly full disk can refuse a save it could have done in place" (SPEC 18.4): the caller can be told the *in-place* alternative exists (WRITE_AT's INSIDE arm) rather than that the disk is full.

  Together these cost about 90 bytes.
- **Make "dirt that is only frees" a first-class, lazily flushed state** (dw-15). It is the one class of FAT change whose loss the commit order already accepts, so it is the one that can wait for `gfx_unlock`. WRITE_SEQ built exactly this gate for held chains; the rest of the write path does not use it.
- **Folders grow** (dw-12). This is the only DOS-2.0 FAT behaviour the kernel lacks, and the reason it is missing (a slot search that only reads) is a few lines away from the code that already allocates and zeroes a directory cluster.
- **Move the compression hint out of the reserved bytes, or shrink it to one bit** (dw-10). The sniff path already exists and is argued free, so most of the hint is optimisation over a cache.
- **Enforce the context rule at the one door** (dw-4). It turns "silent cross-link" into "FERR_NODISK" for about 9 bytes.

## Prior art

| system | its model | better than os8088 | os8088 better | cheap to adopt |
|---|---|---|---|---|
| MS-DOS 2-6 | handles (SFT) holding a cluster position; FAT and dir sectors through BUFFERS, written lazily or at close; CHKDSK/SCANDISK offline | O(1) sequential position per handle; folders grow; lazy FAT writes; code-page names accepted | shadow replace (DOS truncates in place); ordered commit (DOS's buffer LRU order is not a commit order); hostile-input bounds; FAT1≡FAT2 by construction; online fsck | dw-12, dw-15 (only the safe class), dw-11 |
| Windows 3.x | DOS underneath plus SmartDrive write-behind | batching of FAT/dir writes | nothing is ever lost to a write-behind cache's crash window, because the deferral here is only of droppable frees | dw-15 |
| OS/2 1.x | FAT with EAs in `EA DATA. SF` (handle at dirent +20); HPFS with run extents, preallocation, dirty flag, lazy writer | preallocation from the known size; a dirty flag; extents | os8088's run coalescing gets most of the extent benefit on contiguous files | dw-13/dw-14, dw-23; **and stop using +20** (dw-10) |
| Mac System 5-7 (MFS/HFS File Manager) | FCB with extents cached; `Allocate`/`AllocContig`/clumps; volume bitmap and MDB flushed lazily; serialized async queue | contiguous allocation; space reserved up front; a queue that makes concurrency safe | os8088 has no B-tree to corrupt, and a host OS can fsck its disks | dw-13/dw-14; the queue's lesson, cheaply, as a refusal (dw-4) |
| GEOS (PC/GeoWorks) | GEOS file headers inside the file; VM files with a transactional save | metadata travels *inside* the file, so no host strips or misreads it | the 'CZ' header already follows GEOS's lesson; the dirent hint is the part that does not (dw-10) | dw-10 |
| AmigaOS FFS | bitmap valid flag; the disk-validator rebuilds on insert | crash recovery is automatic at mount | FAT has no rebuildable bitmap, and os8088's order bounds damage to lost clusters, which is the same bound | dw-23 |
| Unix SVR fsck | phases 1-5 (blocks and sizes, pathnames, connectivity, reference counts, free list), lost+found | names both owners of a duplicate block | fsck.inc's phases are the same decomposition, and run online in bounded steps | none |

## Doc/code mismatches

Each is listed with file:line, an exact quote, and the proposed text. The structured result carries the full set.

1. **`kernel/diskw.inc:4-6`.** Quote: "this module is the only one that modifies one, and the only caller of". `disk_write_x` is also called by `disk.inc:9094`, `diskops.inc:2102/2362`, `driver.inc:6506` and `fsck.inc:1082`. Rewrite as "this module is the FAT write path: every FILE-level write goes through it (the cloner, the drivers' raw sector door and FSCHECK.DRV call disk_write_x below it)".
2. **`kernel/diskw.inc:6-15`.** Quotes: "Five public routines - dskw_write," / "they ARE the API cells 0x0120..0x0140" / "No handles, no seek, no partial rewrite". The cells are 0x0103..0x0118 (os88api.inc:959-1103), and READ_AT, APPEND, WRITE_AT, READ_SEQ, WRITE_SEQ, STAT, VOL_STAT and rmtree exist.
3. **`SPEC.md:34474-34475`.** Quote: "and the only module that calls `disk_write`. Five public routines are the file surface". Same correction as 1-2.
4. **`SPEC.md:34485`.** Quote: "is the largest subset that stays honest in 256KB with 12 pre-emptive tasks". There is a read-ahead cache (18.95) and a 128-640KB ladder.
5. **`SPEC.md:34520`.** Quote: "which no volume this kernel mounts can hold". On kern_big (OS88_BIGVOL, 18.7.5) volumes past 32MB mount; the refusal is the 16-bit sector count.
6. **`SPEC.md:34524`** and **`kernel/diskw.inc:476-477`.** Quotes: "`spc`×512 overflows 16 bits at the §18.2-legal `spc` = 128" and "BX = SECTORS per cluster (spc*512 overflows". Mount rule 4 caps spc at 64 (`disk.inc:5094-5108`).
7. **`kernel/diskw.inc:495-503`.** Quotes: "clusters * spc <= TotSec16 <= 5,760" and "are under 65,536 * 128 = 2^23". On BIGVOL DX may be non-zero, and the bound is 65,524 × 64 < 2^22.
8. **`SPEC.md:34527`** (dskw_sync row), **`:34653`**, **`:34680`** and the **`fmv_gneed` row at `:51775`.** Quotes: "then `dskw_remount` only if `fmv_gneed` says", "The second is a **remount**", "`fmv_gneed` is the question". `dskw_sync_x` (`diskw.inc:2882-2903`) never remounts; `fmv_gneed` is gone (SPEC 38.2, `:75460`).
9. **`kernel/diskw.inc:2869`, `:2901`.** Quotes: "paid by dsk_relist." / "dsk_relist is the payment". `dsk_relist` is deleted (files.inc:2187).
10. **`SPEC.md:34551`, `:34594`.** Quotes: "The resident FAT snapshot (`FAT_SEG`, §2.1) is the" / "(`dskw_refat`, one `disk_read` of `dsk_fatsz` sectors)". It is a per-volume window (18.8), and `dskw_refat` is an invalidate with no I/O (`diskw.inc:5002-5006`).
11. **`SPEC.md:34740-34742`.** Quote: "no *recursive* directory removal", "no append or seek, no". `dskw_rmtree`, APPEND, WRITE_AT and truncate (18.4.7.5) all exist.
12. **`kernel/diskw.inc:2972`.** Quote: "<= 32,768 by mount rule 8". It is rule 4.
13. **`kernel/diskw.inc:3137`.** Quote: "(mount rule 2: spc is)". It is rule 4.
14. **`kernel/diskw.inc:3012-3016`** and **`apps/os88api.inc:6093`, `:6109`.** Quotes: "capacity must each be a whole number of CLUSTERS" and "Offset AND capacity must each". kern_big takes SECTORS (`diskw.inc:3136-3141`, SPEC 18.4.4.2). Add the kern_big note.
15. **`kernel/diskw.inc:3165-3167`, `:3930-3933`.** Quote: "offset is under 33MB (SPEC.md 18.7), so its". False on BIGVOL and for a hostile size; this is dw-1's comment.
16. **`kernel/diskw.inc:3676-3677`** and **SPEC 18.4.7.3.** Quote: "real offset can carry (a volume caps at 32MB)". The conclusion holds (FAT16 at spc 64 keeps offsets under 2^31), but the reason is stale.
17. **`kernel/diskw.inc:1906-1907`.** Quote: "a real chain on any volume this kernel mounts (TotSec16 <= 65,535)". Stale on BIGVOL.
18. **`kernel/diskw.inc:2219`, `:6732`, `:1284`.** Quotes: "spent by dskw_rflush", "(dskw_rflush)", "so dskw_rt_zap can". The routine is `dskw_flushrun`, and `dskw_rt_zap` is inlined at `.lfn`.
19. **`kernel/fsck.inc:24`.** Quote: "RESIDENT: the stub (.text), fsk_cnear (.cold)". The stub is `.cold` (`:214`).
20. **`kernel/fsck.inc:308`.** Quote: "ES IS THE WORKSPACE, FROM fsk_entry's `mov es, dx` TO ITS RETURN". `fsk_delsys` reloads ES (`:536-537`, `:563-564`, `:599-601`).
21. **`kernel/fsck.inc:383`, `:1565`, `:1668`.** Quotes: "through [fk_sp]", "[fk_a] = the cluster, [fk_b] = its place", "([fk_e])". The names are `fsk_sp` and `P_A`/`P_B`/`P_E`.
22. **`SPEC.md:229710`, `:229756`.** Quotes: "`.bss` 20 (`fsk_fp`, `fsk_cold`, `fsk_ws`, `fsk_sp`" and "the kernel's (`fsk_ws`, `fsk_sp`, `fsk_cold`". `fsk_fp` and `fsk_ws` went in size pass 17; on kern_big it is 14 bytes (4+2+DVOL_MAX 8).
23. **`SPEC.md:229576`** (the 118.14.3 table). `FSKF_PARTIAL` = 23 is missing (`fsck.inc:83`). It is used as row 4 Note (`:2080-2083`) and row 7 Warning (`:2439-2441`).
24. **`SPEC.md:229610`.** Quote: "It runs only after a check that found nothing Broken". The code also refuses after a check that left a folder unread (`PF_SKIP`, `fsck.inc:2439-2441`). 118.14.4's LOST row likewise omits that lost chains are neither saved nor freed then (`:2078-2084`).
25. **`SPEC.md:49252`.** Quote: "three bytes FAT12 and FAT16 leave". See dw-10: +12, +13 and +20 all have meanings on NT, Linux, macOS and OS/2.

## Open questions

1. Does NT's CHKDSK (or OS/2's) on a FAT16 os8088 disk report or clear the +20 "EA handle" when `EA DATA. SF` is absent? The 86Box Windows reference machines (`win16-reference`) could answer it in one run. It decides whether dw-10 needs only a new mark or the hint out of +20 too.
2. dw-4: does anything call the dskw bodies from a task other than 0 by design? The suspects are kerndos's DOS context and HIBER. If something does, the check belongs in the package-facing N-stub only.
3. dw-15 versus `WSQF_KEEP`: a kept hold on a fixed disk already outlives callbacks. Should deferred frees on a fixed disk also outlive them, with flushing at the next write or batch end only? That would remove nearly every second flush on the hard disk.
4. dw-12 on kern_small: 360KB disks have 2-sector clusters (32-entry folders). Is growth worth about 90 bytes of the 64KB budget there, or kern_big only?
5. The `.cold` resident figure in 118.14.7 ("90 ... the 73-byte stub") predates pass 17. It needs a `kernsize` span to restate; no map was built here.

## Verification

Adversarial pass over every finding and doc mismatch above, against the code at this tree (no build, no emulator). The verified set, with the doc fixes to apply, is `data/diskwrite.json`.

### Findings

- **dw-1** CORRECTED: both divides are unguarded as stated, but both are assembled on kern_small too (neither is under an `%ifdef`), so both kernels need both guards; bytes about +4 (wabody) and +6-8 (READ_AT) each.
- **dw-2** confirmed: `[dskw_first]` is the unlinked sub-chain on both plain arms (the link lands only at `.link`, after the sub-chain flush), so the proposed free-then-flush is exact. `dskw_refat`'s header accepts lost clusters; disk-full is what makes them the whole free space.
- **dw-3** confirmed: `dsk_next_clus_x` sets CF only for out-of-range or a failed window load, and `.try` keeps candidates in range.
- **dw-4** confirmed; added that DOS.O88 owns a worker (`dvw_start`), whose file calls must be shown to run on task 0 before a refusal lands.
- **dw-5** confirmed (`.ioerr` skips `.reset`; `.stale` does not).
- **dw-6** confirmed; SPEC 18.4.7.2 permits the round-up that 18.4's "No memory ever leaks past EOF" forbids.
- **dw-7** CORRECTED: real, but there are three removal arms to re-arm (or one shared label), and the guard's `jz .corrupt` also skips `dskw_rt_go_home`, leaving the volume deep in the tree.
- **dw-8** confirmed.
- **dw-9** confirmed (6 bytes to 4).
- **dw-10** confirmed (5Ah sets bits 3 and 4; `os88disk.py` writes it for every packed file).
- **dw-11** confirmed.
- **dw-12** CORRECTED: 14 entries holds for 512-byte clusters (1.44MB, 1.2MB, the 360KB system disk); a 720KB folder holds 30. RunCPM's section records the limit failing a save on the glass.
- **dw-13** CORRECTED: on a multi-window FAT16 `dsk_free_clus_x` faults FAT windows in, so it is disk I/O, not CPU only; use the existing goal-bounded count (`[dsk_fcgoal]`, kern_big).
- **dw-14** confirmed (byte figure is an estimate).
- **dw-15** confirmed; it interacts with dw-5 (deferred dirt is what park carries across a swap).
- **dw-16** confirmed.
- **dw-17** confirmed (the second walk usually ends in the same sector).
- **dw-18** confirmed.
- **dw-19** confirmed.
- **dw-20** confirmed (spot-checked).
- **dw-21** confirmed.
- **dw-22** confirmed (spot-checked).
- **dw-23** unchecked (low, record only).
- **dw-24** confirmed.

None of the findings is already listed in `docs/plans/ABI-BREAK-LIST.md` or `docs/plans/KERNEL-BYTE-QUEUE.md` under these names, and none is refuted.

### Doc/code mismatches

- 1-13, 15-25 confirmed, each with a refined replacement in the JSON. Refinements worth naming:
  - 11: the replacement's cross-reference for `dskw_rmtree` is **§18.90**, not §18.6.
  - 15: the proposed comment described a guard that does not exist yet (dw-1). The applied text says the divide is unguarded.
  - 18: `disk.inc:9094` is the INSIDE arm's own chain write (called from this module), and `driver.inc:6506` is the Control Panel's boot-sector byte (`cpc_fdbs`), not a raw sector door.
  - 20: `fsk_delsys` does not restore ES on every path, so the comment does not say it does.
- 7 (`diskw.inc:495-496`), the `:1284` half of 18 and the `:383` half of 21 were not in the structured set. They are confirmed and added.
- 14, the `apps/os88api.inc:6093`/`:6109` half: **REFUTED**. "Offset AND capacity must each be a cluster multiple" is the portable contract, binding on kern_small and still accepted on kern_big. SPEC 18.4.4.2 documents kern_big's relaxation. Editing the OSAPI contract comment also moves `tools/abi.lock.json`'s fingerprint. The `diskw.inc:3014` half is confirmed, because that comment describes the code, and the code differs per arm.
