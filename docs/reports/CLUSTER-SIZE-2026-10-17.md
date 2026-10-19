# 512-byte clusters on the floppies: what depends on 1KB, and where 512 pays

**A measurement, not a description.**
- **When and where:** taken 2026-10-17 on a four-core cloud container, on
  branch `cluster-size` cut from `elendilon` at `6fe659f`, built with a plain
  `make` (so a shallow-clone build number; no figure below depends on it).
- **What kind of figures:** cluster and byte counts are read off the built
  images by a FAT12 walker. The DOS results come from **IBM DOS 3.30 on the
  owner's own boot floppy** (`IBM  3.3`, 360KB), running under MartyPC
  (`os8088_5150_herc_sb_720_gla`, whose 80-cylinder B: holds a 720KB disk).
  The kernel result comes from one `os88ui` boot of the same machine class.
  No time was measured; the CPU costs in section 5 are arithmetic over the
  per-cluster prices the kernel's own comments quote.
- **Scope:** true of that tree. It is not maintained against later ones.

## 1. The answer

- **The 1KB cluster is not a kernel assumption.** `kern_big`'s FAT code reads
  `[dsk_spc]` everywhere, sizes no buffer to a cluster, and already runs every
  1.44MB and 1.2MB disk at 512 bytes. It booted a 512-byte-cluster 360KB system
  disk, and opened and launched packages off a 512-byte-cluster apps disk, with
  no change.
- **What does depend on 1KB is four things outside that code:**
  1. **`kern_small`'s 2-sector FAT cap.** It cannot mount any 360KB disk with
     512-byte clusters.
  2. **The boot canary on 720KB.** It goes inert there.
  3. **A rule the tree inherited from a mis-diagnosis about DOS.** The rule is
     SPEC.md 19.3, enforced by `t_image`'s `STANDARD` table. It is **wrong as
     stated**; section 3.
  4. **About a dozen host tools and test fixtures** that typed 1,024 or 354/713
     in.
- **Where 512 pays: the 360KB SYSTEM disk, and nowhere else worth the
  churn.** It is the only shipped image near full. 512-byte clusters take it
  from **5KB free to 17KB**, while every other cost lands on things that disk
  does not need: `kern_small` boots its own disk, and the canary stays live at
  360KB. The other 360KB disks gain 3-7KB each with room to spare already. The
  720KB pair gains 10-14KB and costs the canary and a stage-2 call. The hard
  disk should not go below what it has.

## 2. What runs at which cluster size today

| volume | sec/clus | FAT sectors | clusters | who chose it |
|---|---:|---:|---:|---|
| 1.44MB shipped | 1 (512 B) | 9 | 2,847 | `GEOMETRY`, the IBM standard |
| 1.2MB shipped | 1 (512 B) | 7 | 2,371 | `GEOMETRY`, the IBM standard |
| 720KB shipped | 2 (1 KB) | 3 | 713 | `GEOMETRY`, the IBM standard |
| 360KB shipped (six images) | 2 (1 KB) | 2 | 354 | `GEOMETRY`, the IBM standard |
| `kern_small` 360KB disks | 2 | 2 | 354 | `--fatcap 2` (Makefile 12124, 12361) |
| `kern_small` 1.44MB disks | 8 (4 KB) | 2 | 357 | `--fatcap 2` (Makefile 12139, 12371) |
| hard disk, at most 16MB | 2 or more | windowed | | `drivers/hdd/fmt.inc` `hd_fmt_spc0` |
| hard disk, up to 32MB (the live image) | 4 or more (2 KB) | windowed | | the same |
| on-machine FORMAT | 1 / 1 / 2 / 2 | 9 / 7 / 3 / 2 | | `dskw_fmt_tab`, kernel/diskw.inc:4916 |

So the question is only about the 360KB and 720KB rows. The opposite
direction, raising a cluster to fit a cap, is already a tool: `fatcap`.

## 3. DOS: the measurement, and what it does to SPEC.md 19.3

SPEC.md 19.3 says DOS *"builds a floppy's BPB from its own table of standard
formats ... for a standard floppy format it is not consulted at all"*. On that
basis `tests/unit/t_image.py`'s `STANDARD` table requires every shipped floppy
to be byte-exact IBM. Had that been true, a 512-byte-cluster 360KB disk with
media `FDh` would be read at the standard offsets and misread.

**The experiment.** The test floppy held the same three files in each case:
a 6,360-byte `TEST1.TXT`, a 36-byte `LAST.TXT`, and `SUB\INNER.TXT` at 2,174
bytes and 5 clusters. Each disk was built by `tools/os88disk.py`, or by a
scratch copy of it with the 360/720 `GEOMETRY` rows changed, and run under
the owner's IBM DOS 3.30 with `DIR B:`, `DIR B:SUB`, `TYPE B:LAST.TXT` and
`TYPE B:SUB\INNER.TXT`.

| disk in B: | OEM ID | DOS 3.30 says free | correct? |
|---|---|---:|---|
| 360KB, spc 2, FAT 2 (today's) | `MSDOS5.0` | 350,208 = 342 x 1,024 | yes |
| **360KB, spc 1, FAT 3** | `MSDOS5.0` | **351,232 = 686 x 512** | **yes**, all four files intact |
| 360KB, spc 1, FAT 3 | `OS8088  ` | 351,232 | yes |
| 360KB, spc 1, FAT 3 | `IBM  3.1` | 351,232 | yes |
| 360KB, spc 1, FAT 3 | `IBM  3.2` | 351,232 | yes |
| **720KB, spc 1, FAT 5** | `MSDOS5.0` | **717,824 = 1,402 x 512** | **yes** |
| 360KB, spc 2, FAT 2, **`RsvdSecCnt` 3** (two junk sectors inserted) | `MSDOS5.0` | **0** | **no**: a garbage root and "File not found" |

What it says:

- **IBM DOS 3.30 honours `SecPerClus` and `FATSz16` from the boot sector**,
  whatever the OEM ID says. A 512-byte-cluster floppy is a disk DOS 3.30 reads
  correctly.
- **It does NOT honour `RsvdSecCnt`.** It looks for the FAT at LBA 1
  regardless. That is precisely the failure SPEC.md 19.3 recorded: the
  kernel-in-the-reserved-area layout, and its "52,224 bytes free". So the
  move that section describes, taking the kernel out of the reserved area,
  **was the right fix for a narrower cause than the one written down**. The
  section's general claim, and `t_image`'s comment repeating it, should be
  corrected to say what was measured.
- **Not measured here:**
  - **DOS 2.x, 3.0 and 3.1.** By every account these identify a floppy by the
    FAT's media byte alone, and would misread a 512-byte-cluster 360KB disk
    ([OS/2 Museum](https://www.os2museum.com/wp/dos-boot-sector-bpb-and-the-media-descriptor-byte/),
    [retrocmp](https://retrocmp.de/fdd/general/floppy-bpb.htm)). None of them
    can read a 720KB disk at all, so this costs nothing at 720KB. At 360KB it
    is the one real interop cost, and it falls on DOS older than the 3.30 the
    owner runs.
  - **MS-DOS 5.00**, which 19.3 also cites. A DOS 4+ BPB is the norm, and
    nothing suggests it is stricter than 3.30.
  - **DOS writing to a 512-byte-cluster floppy.** It is low risk, since
    allocation follows the same BPB, but it was not exercised.

## 4. What the shipped images would gain

Every file was re-rounded at each cluster size, every folder re-sized from
its entry count, and the FAT re-derived. These are not estimates of slack:
the spc=2 column reproduces each image's real `used/total` exactly.

| image | free today (spc 2) | free at spc 1 | gain | slack today -> at spc 1 |
|---|---:|---:|---:|---:|
| `os8088-360.img` (system) | **5 KB** (349/354) | **17 KB** (671/706) | **+12 KB** | 27 -> 13 KB |
| `apps360.img` | 30 KB | 37 KB | +7 KB | 17 -> 9 |
| `media360.img` | 148 KB | 154 KB | +6 KB | 12 -> 5 |
| `office360.img` | 73 KB | 76 KB | +3 KB | 9 -> 5 |
| `network360.img` | 289 KB | 292 KB | +3 KB | 6 -> 2 |
| `games360.img` | 111 KB | 115 KB | +4 KB | 10 -> 5 |
| `os8088-720.img` (system) | 191 KB | 205 KB | +14 KB | 31 -> 15 |
| `apps720.img` | 73 KB | 83 KB | +10 KB | 24 -> 12 |

- The FAT's growth is already netted out: 360KB gives up 1KB of data area
  (FAT 2 -> 3 sectors, twice) and 720KB gives up 2KB (3 -> 5).
- The 4KB row is not shown. It would put the 360KB system disk 34KB over.

The gain is about half the average slack, roughly 256 bytes a file. So it
scales with the NUMBER of files, which is why the system disk, at 46 files and
6 folders, gains most.

## 5. What 512-byte clusters cost, by owner

### Breaks: must change or the build fails

- **`kern_small` refuses the disk** (kernel/kernel.asm:730 `DSK_FAT_SECS equ 2`;
  mount rule 10 at kernel/disk.inc:4398).
  - A 512-byte-cluster 360KB disk needs a 3-sector FAT.
  - No 360KB layout at spc 1 fits 2 sectors: that needs 680 clusters or
    fewer, so a root of 560+ entries, and rule 7 caps the root at 512.
  - Its own disks are untouched, because `--fatcap 2` searches upward and
    still lands on spc 2.
  - What changes is that a 128KB machine could no longer read a **shipped**
    360KB disk handed to it. Raising the cap to 3 is +512 bytes of resident
    `FAT_SEG` on a kernel that is on a diet (SPEC.md 39.27.4).
- **`tests/unit/t_image.py:71-76` `STANDARD`.** Every changed image fails
  "BPB is the standard format". The table is the 19.3 rule; section 3 is the
  case for relaxing it to "standard except `SecPerClus`/`FATSz16`, with
  `RsvdSecCnt` = 1 and the standard media byte".
- **`tests/unit/t_canary.py`, 720KB only.** At spc 1 the data area starts at
  LBA 18, so the canary's file sector 21 lands at LBA 39: cylinder 2, head 0,
  the first half of a run. The canary goes inert, which is the fault it
  exists to catch.
  - At 360KB (data LBA 14) sector 21 still crosses a head.
  - With 720KB changed, the only file sectors legal on every geometry are 106
    and 107. That puts `KSIG_OFF` back at the top of `.text`, the constraint
    Makefile 848-901 records retiring.
- **`tests/unit/t_blobruns.py:204-209`, 720KB only.** Stage 2's read goes
  from 11 `int 13h` calls to 12. 360KB stays inside its ratchets.
- **`tools/os88cz.py:55-62` `SIZES`, with `dostools/os88cz.asm:1544-1572`
  and `tests/unit/t_cz.py`.**
  - A split-set part is sized at 354 x 1,024 or 713 x 1,024 bytes.
  - A fresh 512-byte-cluster image holds 1KB or 2KB less, so `make_images`
    raises.
  - OS88CZ.COM writes to DOS-formatted disks, which stay at spc 2, so the two
    sides would disagree unless the part size takes the smaller figure.
- **The head-slack fixtures.** These rely on a 360KB cluster leaving
  `op_claim` slack:
  - `tests/multiseg.py:191-210` fails;
  - `tests/rehome.py:173-202` fails;
  - `tests/rehomemove.py` loses its in-carve arm;
  - their images are Makefile 8312, 8333, 8408, 8451 and 7277.
  - `--fatcap 2` on those recipes, or simply leaving them out of the change,
    keeps them at spc 2.

### Stale numbers: nothing fails, but they misprice

- `tools/getruncpm.py:147-148` and `tools/getcpmsw.py:844-845` price CP/M
  fills in 1,024-byte clusters against 354/713, and are chained through
  `--reserve-clusters`.
- `tests/unit/t_lzbclust.py` is the owner's rule that LZB is worth shipping
  where it "saves a 1KB cluster on the 360KB disks". That rule needs
  re-deciding, not just re-numbering.
- Comments: `tools/getstories.py:84`, `tools/excitebike_assets.py:1715`,
  `tools/os88diskuse.py:9`.
- About sixty documents cite 354 or 713, among them SPEC.md 19, 19.3 and
  24.3-24.6, Makefile prose, README and CLAUDE.md. The SPEC.md 19 geometry
  table is the one that binds.

### Behaviour changes: no break, but a user could see them

- **A folder the KERNEL creates holds 14 files, not 30**, if `dskw_fmt_tab`
  or a shipped disk's folders change.
  - `dskw_mkbody` (kernel/diskw.inc:586) allocates exactly one cluster, and
    no directory is ever grown (`FERR_DIRFULL`, SPEC.md 18.5). That is
    today's 1.44MB behaviour already, and SPEC.md records `MEDIA/` filling
    at 16.
  - Folders `os88disk.py` builds are sized to their contents in clusters.
    Anything a program saves into needs `--dir-slots`, as on 1.44MB.
- **Appends align more finely.** `OSAPI_FILE_APPEND`/`WRITE_AT` offsets must
  be cluster-aligned (kernel/diskw.inc:3344), so more files become appendable.
  The same goes for `kern_small`'s cluster-aligned `READ_AT`
  (kernel/diskw.inc:2683).
- **On-machine FORMAT keeps writing spc 2**, so a disk formatted on the
  machine would differ from a shipped one. Leave the table alone:
  - its 360KB row is the only one `kern_small` can mount;
  - and it is what DOS 2.x reads.

### CPU that scales with the cluster count

Doubling the clusters doubles each of these. The 1.44MB disk already runs
**2,847** clusters, so a 706- or 1,422-cluster volume stays inside a regime
the kernel lives in daily.

- **`dsk_free_clus_x` on every mount.** About 93 clocks a cluster: +352 x 93
  is about **+7 ms** at 4.77 MHz on 360KB.
- **The first allocation after a mount.** The rover starts at 2, at about
  850 clocks a used cluster. On a full 360KB system disk that is +322 x 850,
  about **+57 ms**, once.
- **Chain walks.**
  - `dsk_read_chain` costs one lookup a cluster.
  - The rest grow with the file: `READ_AT` re-walks from the head on every
    call (quadratic over a file, docs/plans/DISK-CPU-PLAN.md), and so do
    `APPEND` and truncate.
  - `kern_big`'s READ_SEQ and WRITE_SEQ tokens avoid most of it.
  - `kern_small` has no READ_SEQ and pays it in full, but `kern_small` cannot
    mount these disks anyway.
- **What does NOT scale is the I/O.** Read and write coalesce contiguous
  clusters into one transfer, split only by track, cylinder and DMA page, and
  `os88disk.py` lays every file out contiguously. The `int 13h` count is
  unchanged, and at ~400 ms a call that is where the time is.
- **Hibernate.** The extent claim `hbm_xcap` doubles: about 4KB to about 8KB
  for a 640KB image. `HS_XMAX` = 1,280 was already sized for spc 1.

### Boot: survives a moved data area

- Stage 1 (boot/boot.asm:338-359), stage 2 and `boothd.asm` derive the data
  area from the BPB. KERNEL.SYS is read as a flat run from cluster 2, and
  `bpb_spc` is never read.
- A 2- or 4-sector shift is absorbed, apart from the canary and blob-run
  ratchets above.

## 6. Where 512 makes sense

| volume | verdict | why |
|---|---|---|
| 1.44MB, 1.2MB | **already 512** | the IBM standard |
| **360KB system disk** | **yes, if room on it is wanted** | +12 KB on the one image that is full, a 2.4x margin. DOS 3.30 reads it, `kern_big` boots it (measured), the canary and blob runs still pass at 360KB. `kern_small` never boots it. Cost: DOS 2.x-3.1 misread it, `t_image` needs the relaxation from section 3, and SPEC.md 19 changes |
| 360KB apps and category disks | **no, for now** | +3 to +7 KB, against making every shipped data disk unreadable on a `kern_small` machine. Worth revisiting the day one of them is the full one |
| 720KB pair | **no** | +10-14 KB where 73-205 KB is free, bought with the canary going back to the top of `.text` and a 12th stage-2 call |
| `kern_small` disks | **no** | its FAT cap pushes clusters UP (`--fatcap`), not down |
| on-machine FORMAT | **no** | the 360KB row is `kern_small`'s only mountable one, and DOS 2.x's |
| hard disk | **no** | 512 B on 32 MB is ~65,000 clusters and a 254-sector FAT the 9-sector window covers 3% of, scanned on every mount. 1 KB at 16 MB or less is already the floor that pays |

If the system disk is taken, the change is small and contained:

1. A per-image cluster override in `tools/os88disk.py`, for example
   `--spc 1`, used on the `$(IMG360)` recipe only. `GEOMETRY` stays the IBM
   table, so every other 360KB recipe, the head-slack fixtures included,
   keeps 1 KB.
2. `t_image` accepts spc 1 with FAT 3 at 720 sectors.
3. SPEC.md 19's table and 19.3's DOS paragraph are corrected per section 3.
4. `t_canary` and `t_blobruns` are re-read on the result. Both pass at 360KB
   by the arithmetic above, but that is to be confirmed by the gates, not by
   this report.

Independently of any of that, **SPEC.md 19.3's claim should be narrowed**:
IBM DOS 3.30 reads `SecPerClus` and `FATSz16` from a floppy's boot sector and
ignores `RsvdSecCnt`. The rule it actually supports is "`RsvdSecCnt` = 1 and
nothing between the boot sector and the FAT".

## 7. Reproducing it

- **The slack table** comes from walking each image's FAT and directory tree,
  then re-rounding every file and folder at spc 1, 2 and 4 with the FAT
  re-derived. The spc 2 column must reproduce the image's own `used/total`.
- **The DOS rows.** Build a test floppy with `tools/os88disk.py`, or with a
  copy of it whose 360/720 `GEOMETRY` rows read
  `(9, 2, 720, 1, 3, 112, 0xFD)` and `(9, 2, 1440, 1, 5, 112, 0xF9)`. Boot a
  DOS 3.30 floppy under `os88marty.launch(dos, apps=test,
  machine="os8088_5150_herc_sb_720_gla", boot=30)`, press Enter twice for
  the date and time, type the commands, and read `m.screen()`.
  `tools/os88dosdbg.py`'s `dos_answered()` is the wait.
- **The kernel row** builds the `$(IMG360)` recipe's own arguments through
  that copy and runs `os88ui.boot(sys, apps=apps)`, then:
  - `ui.path("B:/APPS/CALC.O88")`, which reached Calculator;
  - `ui.path("A:/SYSTEM")`, which listed 6 entries.
