# FAT32-MEGA-PLAN — reading FAT32 on kern_mega

**Status: in progress on `feat/fat32`.** §5's decisions were taken by the
owner on 2026-11-19 and the contract is SPEC.md §19.12; §6 is the story list
and what each wave has actually shown. It came out of issue #386, alongside the
logical drives and the large-disk transports that shipped with
[HDD-EXTENDED-PLAN.md](HDD-EXTENDED-PLAN.md).

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

## 5. The owner's decisions (2026-11-19)

| | asked | decided |
|---|---|---|
| O1 | folder numbers past 65,535 | **(a) 32-bit door forms**, high word in a second register; the old doors refuse what they cannot name. With three door indices left (125 of 128) that is ONE RN door, `OSAPI_FILE_PLACE`, with a verb in AL (SPEC.md 19.12.8) |
| O2 | the volume ceiling | **2TB**, the format's own (not the recommended 8GB): TotSec32 whole, FAT sector numbers 32-bit. The transport is the lower bound in practice: rung 1's LBA28 reaches 128GB |
| O3 | DOS | **hidden** from DOS sessions |
| O4 | read-only first? | **straight to read-write** |

## 6. Stories

Each wave closes with `tools/kernsize.py` on kern_mega and a hash of the other
three kernels against the merge base. Soak rows run once, after the last wave.

- [ ] **W1. The FAT layer is 32-bit on kern_mega.** `OS88_FAT32`, the `CLU_*`
  macros and `[dsk_ch]` (19.12.3); the FAT32 BPB rules and layout (19.12.2);
  the 4-byte window (19.12.4); the root chain called 0 (19.12.5); FstClusHI on
  read and write and the listing's high word (19.12.6); allocation, free
  chains, root growth, FSInfo (19.12.7); the Disk window, the loader and the
  instance folders widened. HDD.DRV accepts 0Bh/0Ch under `CPU_F_FAT32`
  (19.12.10). Verified by: the three other kernels byte-identical; a host
  mtools FAT32 image listed, entered, launched from, written to under QEMU.
- [ ] **W2. The API.** `OSAPI_FILE_PLACE`, the 0FFFFh rule, FIND's
  `OSAPI_FIND_FAR`, `OSAPI_FCP_FAR`, VOL_STAT scaling, READ_SEQ's cursor and
  the RUN/MAP refusal (19.12.8); the C SDK; the copy engine, the file dialog,
  assoc and Sticky Apps widened; the refusals of 19.12.9.
- [ ] **W3. The gate.** `tests/fat32.py` (19.12.11), the host FAT32 reader in
  `tests/instdeep.py`, the suite rows, the final pass.

**Measure first, then cost**: each phase opens with `tools/kernsize.py` on
kern_mega and closes with the same, and the resident line is the one that
binds.
