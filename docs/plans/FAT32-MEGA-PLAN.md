# FAT32-MEGA-PLAN — reading FAT32 on kern_mega

**Status: a plan, nothing built.** It came out of issue #386, alongside the
logical drives and the large-disk transports that shipped with
[HDD-EXTENDED-PLAN.md](HDD-EXTENDED-PLAN.md). Those were driver work. FAT32
is kernel work, and it is a decision for the owner before it is code: §5's
open questions need answers before P1 can start.

## 1. Why kern_mega, and why not the XT

FAT32 is a FORMAT, not a partition type. §18.7.5 got `kern_big` past 32MB for
259 bytes because only the data area's SECTOR numbers widened, and they could
be widened beside the old ones (`[dsk_lbahi]`, `[dsk_c2hi]`). FAT32 widens the
CLUSTER number, which is a 16-bit word in every structure the file system has
and in four API doors (§3). That is resident code on `kern_big`, inside a
100KB budget the owner wants held at zero growth, and it fails the on-demand
module test (SPEC.md §2.8): the hooks are on every file operation's hot path.
It would also be slow there: a 2GB FAT32 volume at 4KB clusters has a 2MB
FAT, walked through a 4.5KB window at ~400 ms a BIOS call.

On `kern_mega` the `.cold` and `.mega` code rungs live above 1MB and cost no
conventional memory. The survey (2026-11-18, against a bless older than the
tree) found roughly **16.5KB of `.cold` headroom and 40.7KB in `.mega`**, and
the transport is rung 1's native ATA with LBA mode (§52.1.5), so a FAT32
volume's whole extent is addressable. The resident budget is the tight one:
68,096 bytes against ~64,512 used, and `FAT_SEG` (4,608 bytes) is
conventional memory.

**The XT answer stays FAT16**: up to six hard-disk volumes of up to 2GB each,
primary or logical (§52.4.1).

## 2. What has to change — the inventory

A read-only survey of the kernel, file:line in the agent report this plan was
written from; counts are routines, not lines.

| Area | Routines | What widens |
|---|---|---|
| FAT access, window, type | 8 (`dsk_fat_ofs_x`, `dsk_fat_window`, `dsk_next_clus_x`, `dskw_setfat`, `dskw_flush_x`, `dsk_bpb_check`, `disk_mount_x`, `dsk_fatw_demote`) | 4-byte entries; `dsk_maxclus`, `dsk_fattype`, `dsk_fatw0`, the EOC constants; the staged BPB (`DSK_BPB_SZ` 18) has no room for FATSz32, RootClus or FSInfo |
| Allocation, free count, rover | 6 | `dsk_rover`, `dsk_fcgoal`; the free count comes back in AX |
| Cluster to LBA | 6 | `dsk_c2hi` is a byte; `DSK_B_TOTHI` is one byte (an 8GB ceiling) |
| Directory walk | 23 | `dsk_wclus`, `dsk_wlast`, `dsk_cwd`, `dskw_rt_*`; "cluster 0 is the root" everywhere |
| Entry first-cluster field | 10 | FstClusHI @20 is read nowhere, and is WRITTEN by the compression hint (§4 D5) |
| Open file / stream state | 10 | `dskw_prev/first/cur`, `dsk_chain_end`, `dws_*` (with an adjacency assert), `RSQ_CUR` in the caller's 16-byte `FSEQ` record |
| Copy engine (FILECP) | 10 | the 6-byte frames, `fcp_*clus` |
| Loader | 5 | `LD_DE_CLUS`, `ld_clus` |
| fsck (FSCHECK) | ~16 | its bitmaps sit in one segment: a cluster bit per cluster cannot cover 2^28 |
| Everything else | ~25 | `FS_CWD`, `inst_fcwd`, `drv_vol_cwd`, `ASC_ROWCLUS`, `asw_dclus`, `fdlg_scwd`, `ui_tm_cwd`, diskops, hiber, compress |

**The root is a fixed region** in the layout maths (`dsk_rootsecs`,
`dsk_rootlba`), the walker (`wclus == 0`), `dskw_dirgrow` (the root cannot
grow), fsck and the formatter. On FAT32 it is a cluster chain starting at
RootClus.

## 3. The API surface

| Door | Today |
|---|---|
| `OSAPI_FILE_HERE` / `GOTO` / `GOTO_Q` / `GOTO_QM` | DX = a folder's cluster, a word, 0 = the root |
| `OSAPI_FILE_COPY` | DX and CX are folder clusters |
| `OSAPI_ARG_FILE` | DX = a folder cluster |
| `OSAPI_FILE_FIND` record | +16 a first-cluster word (`os88_find.clus`, `os88_place.clus` in C) |
| `OSAPI_FILE_READ_SEQ` | `FSEQ_RUN`/MAP hand back 4-byte runs (first, last) |
| `OSAPI_VOL_STAT` | BX = free clusters, DX = CountOfClusters |
| `OSAPI_VOL_CHECK` | `FSW_*` word counters |
| DOS (`apps/dos/`) | AH=36h passes `dos_be_vstat` through; no AH=1Fh/32h, no 7303h |

**kern_mega's DOS sessions PARK into kern_dos** (kernel/megados.inc), a
real-mode kernel with FAT12/16 code of its own, so a FAT32 volume on kern_mega
is not a DOS volume until kern_dos reads it too.

## 4. Design, as far as it can go before §5 is answered

- **D1. One switch, `OS88_FAT32`, defined by `KERN_MEGA` only**, in the shape
  `OS88_BIGVOL` set: macros that read a dword on the build that has one and a
  word on the builds that do not, so `kern_big`, `kern_small` and `kern_dos`
  assemble to the bytes they are today. Code goes in `.cold`/`.mega`; what
  must be resident is `.bss` dwords, counted in conventional memory.
- **D2. The FAT stays a window** (§18.8), of 4-byte entries on FAT32: the same
  4,608 bytes are 1,152 entries. Entries are masked to 28 bits on read, the top
  four preserved on write, end-of-chain at 0FFFFFF8h and up.
- **D3. The root stays "cluster 0" to everything above the walker.** The walker
  resolves 0 to RootClus on a FAT32 volume. That keeps every door's "0 = the
  root" and every package that tests for it.
- **D4. A folder past cluster 65,535 is the API's problem, not the walker's**:
  §5 O1.
- **D5. The compression hint moves off @20 on FAT32**, because @20 IS
  FstClusHI there. The hint is a cache and the read path already checks each
  file's own `CZ` header (disk.inc, `DSK_R_CZL`), so on a FAT32 volume it is
  never written and never trusted. Writing it there would point the file's
  first cluster into another file.
- **D6. FSInfo's free count is advisory.** It is read at mount as a hint,
  written as 0FFFFFFFFh ("unknown") on the first write a session makes, and the
  real count is a FAT scan run on demand, as `dsk_free_clus_x` does now.
- **D7. Read before write.** P1 mounts read-only: a FAT32 volume lists,
  launches and copies FROM. The write path (allocation, directory growth,
  FSInfo, the copy engine's destination) is P2.
- **D8. fsck and the formatter refuse FAT32** in P1 and P2, by name. FSCHECK's
  bitmaps need a different design, and the disk tool formats FAT16, which is
  what an XT can read too.
- **D9. HDD.DRV mounts 0Bh/0Ch only where the kernel says it reads them**: a
  capability the driver asks for (an `OSAPI_CPU_INFO`-style fact, not a
  guess), so the same driver on `kern_big` keeps refusing them with a reason
  on the page.

## 5. Open decisions for the owner

- **O1. Folder numbers past 65,535.** Either (a) new 32-bit door forms with
  the high word in a second register, the existing doors refusing (CF = 1) a
  folder they cannot name, so no built package changes; or (b) a per-volume
  folder TOKEN table, so DX stays a word everywhere and means "folder n"
  rather than "cluster n". (a) is the smaller change and an ABI addition
  (no `PKG_FMT` bump); (b) touches no door but adds a table and an eviction
  rule. Recommendation: (a).
- **O2. The volume ceiling.** The staged BPB's `DSK_B_TOTHI` is one byte, an
  8GB volume. FAT32's own is 2TB. Recommendation: 8GB in P1, which is past
  every CF card a period machine takes.
- **O3. DOS.** A FAT32 volume hidden from DOS sessions in P1 and P2
  (recommended), or kern_dos gains FAT32 as well.
- **O4. Read-only first** (D7) — or straight to read-write.

## 6. Stories, once §5 is answered

- [ ] **P1. A FAT32 volume on kern_mega lists and launches.** Mount, BPB rules
  for FAT32, the 4-byte window, the root chain, FstClusHI on read, the
  doors per O1, read-only. Gate: a host-made FAT32 image (mtools
  `mformat -F`) on Bochs, a package launched off it, and a folder past
  cluster 65,535.
- [ ] **P2. ...and writes.** Allocation, free chains, directory growth,
  FSInfo (D6), the compression hint's FAT32 rule (D5), the copy engine.
  Gate: a copy onto it read back by mtools on the host, byte for byte.
- [ ] **P3. The rest of the inventory**: the loader's runs, Sticky Apps,
  the file dialog, assoc, hibernation's directory records.
- [ ] **P4. (if O3 says so) DOS sees it.**

**Measure first, then cost**: each phase opens with `tools/kernsize.py` on
kern_mega and closes with the same, and the resident line is the one that
binds.
