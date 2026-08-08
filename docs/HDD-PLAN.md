# os8088 hard-disk plan

**Research document, not a contract — and now IMPLEMENTED.**
> The plan below was written before the code and is kept as the reasoning
> behind it. What actually shipped is SPEC.md §18.7, §18.8, §22.6, §26.1,
> §31.9, §51.2.1, §51.8 and §52; where the two disagree, SPEC.md is the
> contract. The two places the plan was wrong are recorded at the end.

**Research document, not a contract.** SPEC.md is the binding contract for what
the kernel *is*; this is the study of what it would take to put hard disks
behind a loadable driver (SPEC.md §51), what has to move in the kernel to make
that possible, and what the numbers say about each choice. Nothing here is
implemented. Every interface named would land in SPEC.md *before* its code.

The ask, in the requester's words:

- Hard-disk support as a **driver package**: MFM through an XT controller card
  (the Seagate ST-11M and its family) and IDE. No ATA/EIDE or later standards —
  an 8088-to-386 machine.
- A **Hard Drive page in the Control Panel that exists only when the driver is
  loaded**, and whose contents live *in the driver*, not in the kernel.
- That page lists detected drives with geometry and size in MB; a drive whose
  geometry could not be determined lets the user **type it in**.
- Buttons on that page open a **partitioning tool** and a **FAT formatting
  tool** (up to FAT16), both inside the driver, and **mount**, which puts an
  **HDD C icon on the desktop below Disk B**.
- **Keep everything possible out of the kernel.**
- And separately: **what would it take for the FAT not to have to be resident in
  full — the way DOS does it.**

---

## 0. The verdict, up front

**It fits, and the driver can own nearly all of it — but not all of it, and the
kernel changes are not optional.** Five things are structurally impossible from
inside a driver today, and each is a small, well-bounded kernel change:

| # | Why the driver cannot do it | Kernel change | Est. |
|---|---|---|---|
| 1 | `dsk_xfer` *is* int 13h CHS floppy code; there is no hook for another transport | a volume table + one branch | ~180 B |
| 2 | The published service table is a **singleton** — `[drv_owner]`/`[drv_fseg]` are one row, so a second driver class would silently steal the sound driver's dispatch | publication per class | ~120 B |
| 3 | `dsk_bpb_check` rules 10–13 reject every hard-disk BPB by construction | rules become volume-kind aware | ~90 B |
| 4 | `cp_items` is a static table of near procs; a driver cannot append a page | table split + far dispatch | ~200 B |
| 5 | `desk_ndrives` comes from int 11h and tops out at 2, with the labels hard-coded | driver-registered zones | ~150 B |

Plus the FAT window (part 4), which is ~250 B and is required by arithmetic rather
than by taste. Call it **1,000–1,100 bytes of kernel `.text` + `.bss`**, against
**2,048 bytes of headroom** measured on this build:

```
build/kernel.bin  57,182 bytes (.text)
KERN_SIZE         78,848 bytes  (image + .bss + FAT_SEG + .lowbss + STK0)
KERN_BUDGET       80,896 bytes   (74,240 today; see KERNEL-MEMORY.md)
headroom           2,048 bytes
```

It fits with ~950 bytes to spare, and **without raising `KERN_BUDGET`** — which
matters, because the budget has already moved four times and each move is a
decision taken with whoever asked for the feature (`docs/KERNEL-MEMORY.md`).
The one item that does *not* fit inside that envelope is raising the 32-entry
directory cap (part 5.7); it is deferrable and the reasoning is below.

Everything else — the controllers, the geometry probes, the partition table, the
FAT formatter, the Control Panel page's pixels, the two tool windows, the
manual-geometry editor, the persistence — lives in `HDD.DRV`.

Three things this plan says **no** to, with reasons in part 11: 32-bit LBAs in the
kernel's FAT layer, booting os8088 from the hard disk, and low-level (MFM
surface) formatting.

---

## 1. The hardware, and which of it we actually talk to

### 1.1 MFM / ST-506 on an XT bus

The Seagate **ST-11M** is an 8-bit ISA MFM host adapter with an on-board BIOS
ROM (normally at C800h) that hooks **int 13h** and chains the BIOS floppy
service to int 40h. Its register interface belongs to the XT fixed-disk family
that starts with the IBM/Xebec adapter and includes the WD1002 series:

```
base+0  320h   data register - the 6-byte Device Control Block, and status
base+1  321h   read: controller status     write: controller reset
base+2  322h   read: drive-configuration switches   write: select pulse
base+3  323h   write: DMA/IRQ mask
        IRQ 5, DMA channel 3 (jumperable; base also 324h/328h/32Ch)
```

Commands are 6-byte DCBs — `00` test ready, `01` recalibrate, `03` request
sense, `04` format drive, `06` format track, `08` read, `0A` write, `0B` seek,
`0C` initialize drive characteristics, `0E`/`0F` read/write sector buffer.
Data moves by **DMA channel 3** on the IBM-compatible cards.

**We should not program that interface, and the reason is the ROM.** The card
carries a BIOS whose whole job is to present int 13h for exactly this drive, on
exactly this card's jumper settings, with the drive-parameter table the card's
own low-level format wrote. Re-deriving all of that from ports buys nothing a
user can see and costs an 8237 programming path, a page-boundary-safe DMA buffer
(`OSAPI_MEM_CLAIM_DMA` exists for it, SPEC.md §50.3), an IRQ 5 hook, and a
per-card jumper matrix we cannot test. **MFM is an int 13h story.** The DCB path
is listed here so that a later phase has somewhere to start, not because Phase 1
needs it.

One consequence worth stating because it disappoints people: the ST-11M's
low-level format is its ROM utility (`g=c800:5` from DEBUG), and low-level
formatting an MFM surface is minutes of per-track work with vendor-specific
interleave and defect handling. **The driver's "format" button is a FAT format,
not a surface format** (part 11).

### 1.2 IDE, CHS only

Registers, primary channel (secondary at 170h, control 376h):

```
1F0h  data (16-bit)        1F4h  cylinder low
1F1h  error / features     1F5h  cylinder high
1F2h  sector count         1F6h  drive/head  (A0h | drv<<4 | head)
1F3h  sector number        1F7h  status / command
3F6h  device control (nIEN, SRST)
```

The command set this needs is ATA-1 with everything optional removed:

```
ECh  IDENTIFY DEVICE            - geometry and model, 256 words
91h  INITIALIZE DEVICE PARAMS   - tell the drive the CHS translation we will use
10h  RECALIBRATE
20h  READ SECTORS  (with retry)
30h  WRITE SECTORS (with retry)
```

Polled PIO, no interrupt, no DMA, no LBA bit, no ATAPI, no 48-bit — which is
exactly the "no ATA/EIDE or later" the request asks for. **`91h` is not
optional**: the geometry the drive is addressed with has to be the geometry it
was told about, or every read lands somewhere else.

Two hard facts about the era shape the design:

- **An 8088 cannot talk to a plain IDE data port.** `in ax, dx` on an 8-bit bus
  is two 8-bit bus cycles at the same address, and the drive's high byte is
  gone. That is what XT-IDE adapters latch, and what the XTIDE Universal BIOS
  drives. So **the raw task-file path is a 16-bit-bus (286/386) path**, and on
  an XT the answer for IDE is the same as for MFM: whatever ROM is in the
  machine, through int 13h.
- **`rep insw` is a 186 instruction and this tree is `cpu 8086`** — the driver
  macro emits `cpu 8086` too. The portable sector loop is 256 × (`in ax, dx` /
  `stosw`). This said "about 5,400 8086 cycles ≈ 1.1 ms per sector on the floor
  machine"; that was a book-cycle reading and it is **low by roughly 2.5×**.
  `tests/sysbench` measures a real ISA port `in` at **8.7 µs** on a 4.77MHz
  8088 (PERFORMANCE.md Part 9) — about 41 clocks, not the book's 8–14 — so 256
  of them alone is 2.2 ms and the loop is nearer **2.5–3 ms per sector**. The
  general form is the 8088 instruction floor (4.34 clocks per instruction
  *byte*), which is why every hand-counted figure in this plan should be read
  as a lower bound. On a machine that reports `CPU_286` or better from
  `OSAPI_CPU_INFO` the driver may emit the two opcode bytes by hand
  (`db 0F3h, 06Dh`) and take the `rep insw` path instead. That is a legitimate
  optimisation and it is measurable; it is not Phase 1.

### 1.3 The transport ladder

The driver probes in this order and each device records which rung answered:

```
0  int 13h, drive 80h/81h    AH=08h answers, AH=02h reads LBA 0 -> usable
                             (ST-11M and every XT/AT card with a ROM; IDE
                             on any machine whose BIOS knows the drive)
1  IDE task file 1F0h/170h   CPU tier >= CPU_286 AND a device answers the
                             signature/status dance -> IDENTIFY, else manual
2  XT DCB at 320h            not implemented; the row exists so the Control
                             Panel can say "not supported" rather than
                             "no drive"
```

Rung 0 is tried first *on purpose*, and not only because it is easy: the machine
whose BIOS already knows a drive is the machine whose partition table, geometry
and drive-parameter block were all written to agree with that BIOS. Reaching
past it to the task file is how you get a driver that reads a disk DOS cannot.

### 1.4 Geometry — five ways to learn it, and the one that needs the user

| Source | Where from | When it lies |
|---|---|---|
| `int 13h AH=08h` | CH = cyl low, CL bits 6–7 = cyl high, CL bits 0–5 = spt, DH = max head | XT ROM present but wrong drive type jumpered; AT CMOS type wrong |
| IDENTIFY word 1/3/6 | cyls / heads / sectors | pre-ATA-1 drives answer ABRT |
| The MBR's own CHS fields | back-derive spt and heads from a partition's end CHS vs its end LBA | only if the disk is already partitioned |
| The volume's BPB | `BPB_SecPerTrk` / `BPB_NumHeads` | only if already formatted, and DOS wrote them |
| **The user** | the Control Panel page | never — it is what the request asks for |

**The manual path is not a fallback for exotic hardware; it is the common case
on a 286 with an early IDE drive and a CMOS type table that predates it.** The
page therefore treats "geometry unknown" as an ordinary state with an editable
C/H/S triple, and the *derived* figures — size in MB, the maximum partition — are
recomputed live from whatever is typed. `cyl × heads × spt × 512` in MB, with
`heads ≤ 255`, `spt ≤ 63`, `cyl ≤ 1024` for anything int 13h will ever address.

The editor is not a new idiom: **the Date/Time page already is one**
(SPEC.md §31.5) — a table of `{x, w, y}` field records, a selected field, and a
`+`/`-` button pair that calls an adjust routine indexed by the field number.
Three numeric fields instead of seven, the same shape, in the driver's segment.

---

## 2. What blocks a hard disk today

Every one of these is a deliberate floppy assumption with a comment explaining
itself. None is a bug. All of them have to become volume-kind aware.

**`kernel/disk.inc`**

- `dsk_xfer:159–177` — LBA→CHS with `[disk_spt]`/`[disk_heads]`, `int 13h` with
  `DL = [disk_drive]`, one sector per call, three attempts with an `AH=00h`
  reset. There is no other transport and no hook.
- `dsk_xfer:166` — `cmp ax, 80 / jae .fail`. The cylinder guard. A hard disk
  starts failing at cylinder 80 of ~600–1,000.
- `disk_read`/`disk_write` take **`AX` = LBA**: 16 bits, volume-relative, no
  partition base anywhere in the kernel.
- `dsk_bpb_check` rule **10** — `FATSz16 ≤ DSK_FAT_SECS` (9). Rejects every
  hard-disk FAT; the comment says so, and says it is what makes FAT16
  structurally unreachable.
- rule **11** — `SecPerTrk ∈ {8,9,15,18,21,36}`. A hard disk has 17, 26, 34, 63.
- rule **12** — `NumHeads ∈ {1,2}`. A hard disk has 2–16.
- rule **13** — `TotSec16 ≤ spt × heads × 80`, the cylinder guard restated in the
  BPB so a volume cannot mount-list and then die on geometry.
- rule **8** — `TotSec16 ≠ 0`. Keeps volumes inside 16-bit LBAs. **This one we
  keep** (part 4).
- `disk_mount` — snapshots the **whole** FAT into `FAT_SEG` on every mount, and
  navigation *is* a remount (`dsk_chdir`, SPEC.md §19.2). At 64 FAT sectors that
  is 64 sector reads per folder entered.
- the **32-entry listing cap** (`disk_mount:396`), `disk_dir` 1 KB and
  `disk_icons` 2 KB in `.lowbss`.
- the icon harvest reads the first sector of **every** type-1 entry at mount.

**`kernel/desk.inc`** — `desk_init` counts floppies from the int 11h equipment
word and clamps to 2; `desk_draw_zone` picks between two hard-coded labels;
`DESK_ZSTEP` is 60 and `DESK_ZY0` is 32.

**`kernel/ctrl.inc`** — `cp_items` is a static 5-row table of *near* paint/click
procs, walked from `cp_items` to `cp_items_end`.

**`kernel/driver.inc`** — `[drv_owner]`, `[drv_fptr]`, `[drv_fseg]` and
`drv_svc` are **one** of each. `drv_publish` overwrites them. Two attached
drivers means the second one's attach silently disconnects the first.

**`kernel/files.inc`** — carries a per-window drive (`FS_DRV`) and cwd already,
and prints the letter as `add al, 'A'`. This module needs **nothing** if the
drive stays a small volume index (part 4). That is not luck; it is why the index
model is the recommendation.

---

## 3. The volume model

**A volume is a small index (0 = A, 1 = B, 2 = C, …) and the kernel's LBAs stay
16-bit and volume-relative. The driver holds the 32-bit partition base and adds
it.** Everything else follows from that one sentence.

```
kernel/disk.inc  .text
DVOL_MAX   equ 4                ; A, B, and two driver volumes
DV_KIND    equ 0    ; db 0 = BIOS int 13h floppy, 1 = driver-backed
DV_UNIT    equ 1    ; db  kind 0: the int 13h DL; kind 1: the driver's own
                    ;     volume handle, opaque to us
DV_CLASS   equ 2    ; db  kind 1: which DRVC_* row services it
DV_SIZE    equ 4
```

`dsk_xfer` gains one branch at the top: kind 0 falls into the code that is there
now, unchanged; kind 1 calls the driver's `DSV_BLK` service with the same
arguments it was given. `[sch_lock]` is already raised around the whole of
`dsk_xfer`, so the driver inherits the no-switching rule for free and needs no
knowledge of the scheduler.

**Why 16-bit and volume-relative, when the disk is plainly bigger than 32 MB:**

- `TotSec16` is a word, so a *volume* over 65,535 sectors needs `TotSec32`,
  which BPB rule 8 already refuses in writing, with the reason given.
- 65,535 sectors is 31.99 MiB — **the DOS 3.3 partition limit**, which is what
  the target machines ran. Four primaries of ≤32 MB is 128 MB of usable disk,
  against MFM drives of 10–40 MB and early IDE drives of 20–120 MB.
- Going 32-bit means touching *every* LBA in `disk.inc` and `diskw.inc`:
  `dsk_clus2lba`, `dsk_read_chain`'s run coalescer (`DI` = run LBA, `CX` = run
  length), `dsk_dirw_next`, `dskw_flush`, `dsk_fatlba`, `dsk_rootlba`,
  `dsk_datalba`, and the `AX = LBA` contract of `disk_read`/`disk_write` that
  three modules and the SPEC all name. That is a rewrite of the FAT driver for
  capacity nothing in the era had.
- And it buys an invariant for free: with `TotSec16` a word, `dsk_clus2lba`'s
  16-bit `(cluster-2) × spc` **cannot** overflow, because the product is bounded
  by `TotSec16`. The existing `test dx, dx / jnz .bad` belt-and-braces check
  stays exactly as true as it is today.

The driver's side of the same arithmetic is one `div` pair, and it is safe
without a 32/16 two-step: with `spt × heads ≤ 63 × 16 = 1008` and a CHS-
addressable disk capped at 1,032,192 sectors, `DX` before the `div` is at most
15 — far below the divisor — so the quotient always fits.

**Letters.** `'A' + index` keeps working, `desk_click`'s zone→drive mapping keeps
working, `FS_DRV` keeps working, `osapi_file_here`/`goto`'s `BL = drive` keeps
working, and `dskw_*` needs no thought. A design that put `80h` in `[disk_drive]`
would have broken all five, and every one of them silently.

---

## 4. The FAT window — the DOS question

### 4.1 Why it is required rather than nice

A 32 MB FAT16 volume at 2 KB clusters has 16,384 clusters, so its FAT is
**32 KB = 64 sectors**. At 1 KB clusters it is 128 sectors. The resident
snapshot is `DSK_FAT_SECS` = **9 sectors (4,608 bytes)**, and it is not a buffer
with slack — it is an *acceptance threshold*, sized to the largest FAT any
geometry this OS boots declares (1.44 MB = 9). There is no version of hard-disk
support that keeps the whole FAT resident: 32 KB against 2,048 bytes of budget.

### 4.2 What DOS actually does, and what of it applies

DOS has no FAT buffer at all. It has a **sector buffer cache** (`BUFFERS=`),
LRU, shared by FAT sectors, directory sectors and data sectors alike, and a
per-drive DPB carrying the "last allocated cluster" hint so the allocator does
not restart its scan at cluster 2. A FAT read is `getbuf(fat_lba + n)`; a FAT
write dirties the buffer and the flush happens at the commit. The FAT12
straddling entry — 12 bits at 1.5 bytes each, so an entry can span the 512-byte
boundary — is special-cased, because two buffers must be live at once.

Two of those three ideas port; the third is already here in better form.

- **The sliding window ports.** os8088 does not need a general buffer cache: it
  has exactly two FAT readers/writers, and they are already funnelled.
- **The allocator hint is already here.** `[dsk_rover]` is the DPB's
  `last-allocated` by another name, already persists across allocations within a
  mount, and already exists for exactly this reason.
- **The straddle does not need special-casing**, because the window is nine
  sectors and a straddle needs two consecutive ones. It only needs the
  *re-window* rule to be written so that the second sector is always inside.

### 4.3 The whole blast radius is five routines

This is what makes it cheap. `FAT_SEG` has exactly one reader and one writer,
and both say so in their headers:

```
dsk_next_clus    disk.inc   the SINGLE reader of FAT_SEG, via ES only
dskw_setfat      diskw.inc  the SINGLE writer, + the dirty range
dskw_flush       diskw.inc  writes [dsk_fatd0..dsk_fatd1] to FAT1 and FAT2
dskw_refat       diskw.inc  re-reads the whole FAT: the pre-commit rollback
disk_mount       disk.inc   reads the whole FAT, with the FAT2 fallback
```

Nothing else in the tree touches those 4,608 bytes. Five routines is the entire
surface of the change.

### 4.4 The design

**The window *is* `FAT_SEG`.** Same 4,608 bytes, same segment, same alignment,
**zero new memory and zero budget growth** — what changes is that the buffer
stops meaning "the FAT" and starts meaning "these nine FAT sectors".

```
dsk_fatw0   dw      first resident FAT sector index, 0xFFFF = nothing resident
dsk_fatwn   dw      how many are resident (= min(DSK_FAT_SECS, dsk_fatsz))
dsk_fatd0   dw  \   the EXISTING dirty range, reinterpreted as window-relative
dsk_fatd1   dw  /
```

`dsk_fat_window(sector)` — ensure sector *and* sector+1 are resident:

1. inside `[dsk_fatw0, dsk_fatw0 + dsk_fatwn - 1)` → return, no I/O;
2. else flush the dirty range if there is one (the existing `dskw_flush` body,
   which already writes both FAT copies from the same bytes);
3. `dsk_fatw0 = min(sector, dsk_fatsz - dsk_fatwn)` — clamped so a straddle at
   the very end of the FAT still has its second sector;
4. read `dsk_fatwn` sectors from `dsk_fatlba + dsk_fatw0`, with the **FAT2
   fallback** the mount already implements, applied per window rather than once.

`dsk_next_clus` and `dskw_setfat` compute the byte offset they already compute,
divide by 512 to get the sector, call `dsk_fat_window`, and index at
`offset - dsk_fatw0 × 512`. Their FAT12 straddling-word read-modify-write is
untouched: step 3 guarantees both bytes are in front of them.

**Floppies get the degenerate case, byte for byte.** `dsk_fatwn = min(9, fatsz)`
means a 1.44 MB floppy's 9-sector FAT is wholly resident, `dsk_fat_window`
returns on its first compare forever, and the flush writes the same sectors it
writes today. **A 4.77 MHz XT with a floppy pays one compare per FAT access and
nothing else** — which is the property that makes this affordable to ship at
all, given that flicker and I/O stalls are the two things this project actually
optimises for.

**`dskw_refat` gets simpler, not harder.** The pre-commit rollback becomes
"`dsk_fatw0 = 0xFFFF`, drop the dirty range" — an invalidate, no I/O. Its
failure mode (a re-read that itself fails, closing the write gate) disappears
with the re-read.

**`disk_mount` stops reading the FAT.** It sets `dsk_fatw0 = 0xFFFF` and lets the
first access fault a window in. On a hard disk that is 64 sectors of mount cost
gone; on a floppy it is nine sectors moved from mount time to first use.
Watch one thing: the mount's *own* FAT2 fallback exists to survive an unreadable
FAT1, and moving it into `dsk_fat_window` moves the moment a disk is diagnosed
as bad from "mount failed" to "the first cluster walk failed". That is a real
behaviour change and belongs in SPEC.md §18.3, not in a comment.

### 4.5 The commit rule survives, and that is not obvious

SPEC.md §18.4's binding order is: allocate and write the data, **flush the FAT**,
then write the directory entry (the commit), then free the replaced chain and
flush again. A crash leaks lost clusters, never a cross-link.

With a window, a chain that spans more than nine FAT sectors gets *part* of its
FAT flushed early — at an eviction, before the commit. **The invariant holds
anyway**, because the property that matters is direction: clusters marked
allocated with no directory entry pointing at them are a leak, which `CHKDSK`
and `tools/os88disk.py --verify` both call "lost clusters" and neither calls
corruption. Nothing is ever pointed at twice. It is worth writing down precisely
because it is exactly the kind of thing that is quietly broken by a later
optimisation that reorders the flush.

### 4.6 What it costs

Two numbers, both from the shape of a FAT16 volume at 2 KB clusters (16,384
clusters, 64 FAT sectors) with a nine-sector window covering **2,304 entries**:

- **Sequential work is free.** Writing a 63 KB Paint BMP touches 32 clusters —
  consecutive entries, one window, one load. Walking any file's chain is one
  load unless the file is fragmented across more than 4.6 MB of cluster-number
  space.
- **A full-disk allocator sweep is the pathological case.** `dskw_alloc` rovers
  from `[dsk_rover]` through every cluster; on a nearly full volume that is
  16,384 entries = 64 sectors = **8 window loads (64 sector reads) for one
  allocated cluster.** DOS has precisely the same worst case and answers it the
  same way — the rover — plus a free-cluster count it maintains, which os8088
  does not have and does not need to grow one for: `dskw_dfree` already answers
  free space from the resident FAT today, and on a windowed volume it becomes a
  full sweep. **That is the one operation that gets materially slower**, it is
  called by the file manager's status line, and it should be measured before it
  is believed. Caching the answer per mount is the obvious mitigation and it is
  correct by the same argument that makes `disk_dir` a mount snapshot.

### 4.7 The alternative, and why not

The window could live in a heap claim owned by the driver, sized to the whole
FAT on a machine with the memory (32 KB for a typical volume), reverting to
`FAT_SEG` when the claim is refused. That is genuinely better on a 640 KB
machine, and it is **two implementations of the same thing** — the "is it
resident" test in the hot path either way, plus a claim lifetime tied to a mount
tied to a driver. Do it later, behind the same `dsk_fat_window` entry point, if
measurement asks for it. The window makes it a later question rather than a
now question, which is the whole point.

---

## 5. The kernel's extension points

### 5.1 Per-class service publication — fix this first

`drv_publish` writes `[drv_owner]`, `[drv_fseg]` and one `drv_svc` copy.
`drv_svc_call` dispatches through that one far pointer. **With `SOUND.DRV` and
`HDD.DRV` both attached, the second attach disconnects the first**, and the
symptom is that sound stops working when a hard disk is enabled — a bug whose
cause is nowhere near its effect.

The fix is mechanical: index the three words and the table copy by
`DRVR_CLASS`, and give `drv_svc_call` the class in a register — **not** in one
of the sound ABI's, which is the lesson `drv_svc_call`'s header already teaches
twice (the row in BX became the FM frequency; the selector in DI became the
staging destination). The selector is in BP because nothing in the ABI uses BP.
The class can ride in **BP's high half** with the offset in the low half — the
service offsets are near pointers inside a ≤40 KB image, so BP's top bits are
free — or `drv_svc_call` gains a second entry point per class. Either way the
existing sound call sites keep their exact register contract.

Two neighbours follow: `drv_task`'s spawn fence tests `ES == [drv_fseg]` and
must become "ES is the segment of the driver whose class is publishing"
(`drv_owns_seg` is already the general form and is already used by the exit
half), and `drv_release`'s disarm must clear the right class's pointer.

### 5.2 The volume table and `DSV_BLK`

`DRVC_DISK equ 2`, and a disk-class driver's service table cell:

```
DSV_BLK   dw   near proc, the block transport
          in   AL = 0 read / 1 write
               AH = the driver's own volume handle (DV_UNIT)
               SI = volume-relative LBA (16-bit)
               CX = sector count
               DX:BX = destination / source, DX a segment
               DS = the driver's segment; ES is NOT KERNEL_SEG here
          out  CF = 0 done; CF = 1 and AL = a status byte the kernel maps to
               FERR_IO / FERR_WPROT exactly as [dsk_ioerr] is mapped today
```

`DX:BX` rather than `ES:BX` because the buffer is in `LOW_SEG`, `FAT_SEG` or a
heap claim and never in the driver's segment or the kernel's — so `ES` cannot
carry its usual meaning and pretending otherwise is how a driver ends up reading
its own image as a sector buffer. The driver does `mov es, dx` itself.

`[sch_lock]` stays the kernel's, raised where it is raised now.

### 5.3 BPB rules, per volume kind

Rules 10–13 become conditional on `DV_KIND`. On a driver-backed volume:

| rule | floppy (unchanged) | driver-backed |
|---|---|---|
| 10 | `FATSz16 ≤ 9` | `FATSz16 ≥ 1` and `FATSz16 × 512` covers the cluster count (rule 16 already proves the second half — so on a hard disk **rule 16 replaces rule 10 entirely**) |
| 11 | spt ∈ {8,9,15,18,21,36} | `1 ≤ spt ≤ 63` |
| 12 | heads ∈ {1,2} | `1 ≤ heads ≤ 255` |
| 13 | `TotSec16 ≤ spt × heads × 80` | `TotSec16 ≤` the sector count the *driver* declared for this volume |

Rule 13's replacement is the important one and it is stronger than what it
replaces: the driver knows the partition's real length from the partition table,
so the check stops being "is this geometry plausible" and becomes "does this
volume claim to be bigger than the partition it lives in" — which is the actual
attack. The cylinder guard in `dsk_xfer` goes the same way: it belongs to the
kind-0 branch and the driver owns the kind-1 bound.

Rules 11 and 12 stay in the *floppy* branch untouched, because their job is to
stop a hostile `spt × heads` from zeroing `dsk_xfer`'s CHS divisor and taking a
divide fault with `sch_lock` held. On the driver branch that divisor does not
exist; the driver's own conversion must carry the same guard, and the SDK should
say so in the same words.

(Nit found while reading: SPEC.md §18.2 rule 10 says `DSK_FAT_SECS` is 10 and
the buffer 5,120 bytes; `kernel/kernel.asm` and SPEC.md §2.1 both say 9 and
4,608. Worth fixing whether or not any of this is built.)

### 5.4 A Control Panel page a driver owns

`cp_items` splits into "the kernel's five" plus "whatever is published", and the
item record grows a dispatch kind:

```
CP_I_NAME    dw   -> the list name   (kernel: .text; driver: staged, below)
CP_I_PAINT   dw   near proc  | driver near offset
CP_I_CLICK   dw   near proc  | driver near offset
CP_I_CLASS   dw   0 = kernel, near-call; else the DRVC_* to drv_call into
```

`cp_page` and `cp_onclick` branch once on `CP_I_CLASS`. The pane origin cannot
travel in BP for a driver page (BP is the dispatcher's selector), so the driver
ABI for the two procs is:

```
paint   DI = pane left, DX = pane top          (pane already white-filled,
                                                gfx lock HELD, ES = KERNEL_SEG)
click   DI = pane left, DX = pane top, CX/BX = pane-relative x/y
```

**The list name is staged into the kernel**, 16 bytes, because `cp_list` draws
it with `font_str` and `font_str` reads through DS. This is `drv_publish`'s own
retired `DSV_NAME` staging, brought back for a consumer that actually needs it —
and it is the same `dsk_get_dir` idiom the whole tree uses when a pointer has to
cross a segment.

Three consequences to write down before writing code:

- **The page exists exactly while the driver is attached**, which is what was
  asked for, and falls out rather than being arranged.
- **`[cp_sel]` must be clamped when a driver detaches** while its page is
  selected, or the panel dispatches through a freed segment. `drv_release` is
  the place; `[cp_sel] = 0` (Scheduler) is the safe answer.
- **The pane is 221 × 121 px** — 27 characters by 13 rows (`CP_PRGT`, `CP_CH`),
  and the panel is not resizable. The Hard Drive page has to fit a device list,
  a C/H/S editor and three buttons in that. It can, at the same density as the
  Date/Time page, and the detail belongs in the tool windows anyway.

### 5.5 A desktop zone a driver owns

`desk_init` keeps counting floppies; `desk_ndrives` becomes "floppy zones" and a
second small table holds registered zones:

```
DESK_XMAX  equ 2
  db  volume index          ; what files_open_drive gets on a double-click
  dw  -> a staged label     ; 'HDD C', 15 bytes, staged like the CP page name
  dw  -> a staged 32x32 icon body
```

`desk_paint`, `desk_dmg_zones`, `desk_paint_mask`, `desk_zone_rect`, `desk_click`
and `desk_zone_redraw` already loop over `[desk_ndrives]`; they loop over the sum
instead, and `desk_draw_zone` picks the label and icon from the table rather than
from its two constants. `wm_paint_dmg`'s zone folding (`desk_dmg_zones`) needs
nothing new — a third zone is a third rect.

**Staging the icon rather than adding a built-in one costs the same and says
more.** A 32×32 icon body is ~264 bytes whether it sits in kernel `.text` as a
second `ico_disk32` or in kernel `.bss` as a staged copy of the driver's; the
staged version keeps the art with the thing it depicts and lets a future volume
class (a RAM disk, a network share) look like itself. `dsk_get_icon` is the
precedent, at 64 bytes instead of 264.

**One real constraint, and it is CGA (SPEC.md §39).** Zones are at
`y = 32 + 60 × i` and 44 px tall, and the dock starts at `[vid_dock_y0]` =
`height - 24`:

```
VGA       480 high   dock at 456   zone 2 ends at 195   fine
Hercules  348 high   dock at 324   zone 2 ends at 195   fine
CGA       200 high   dock at 176   zone 2 ends at 195   OVERLAPS THE DOCK
```

On CGA the third zone must go somewhere else: a second column at
`[vid_desk_zx] - 56`, or a tighter pitch for the whole strip when
`[vid_h] < 240`. This is exactly the class of thing SPEC.md §1 warns about —
"new code that clips, centres or anchors to a screen edge must read
`[vid_*]`" — and it will not be noticed on VGA. **A drive-zone change is not
done until it has been looked at on CGA.**

### 5.6 The tool windows are already free

A driver calling `OSAPI_WM_CREATE` gets a working window **today, with no kernel
change at all**, and the reason is worth stating because it is not obvious:
`wm_create` takes `W_SEG` from `ES`, which the slot's X stub filled with the
*caller's* DS, and `W_DISP` from the fixed `PKG_DISP` — and a driver's header
carries the same three dispatcher bytes as a package's. `wm_pkgcall` does not
look at instances. So every callback lands on a near proc in the driver's
segment with `ES = KERNEL_SEG`, exactly as for a package.

What the driver does *not* get is an instance record — no dock tile, no Task
Manager row, no callback billing — and **that is the right answer**, because the
precedent already exists and is documented: the Standard File dialog
(SPEC.md §38.1) is a bare `wm_create`d window that no instance owns, whose close
and minimize boxes reduce to `wm_hide` (`app_close_win` → `inst_of_win` → CF=1 →
`.hide`). The partitioner and formatter are that species: modal-ish tools owned
by a driver, not applications.

Two things to get right: the driver must destroy or hide its windows in
`DRVV_DETACH` **before** returning (the image is freed the instant it does), and
`MAX_WIN` is 12 shared with everything else.

### 5.7 The 32-entry listing cap — the one thing that does not fit

`disk_mount` stops at 32 accepted entries; `disk_dir` is 32 × 32 bytes and
`disk_icons` 32 × 64 bytes, both in `.lowbss`, and the file manager's per-window
view cache (`VIEW_KB` = 3) mirrors both. A hard-disk root holding 512 directory
entries will show 32 of them.

Raising it is not a constant change:

- 128 entries costs 4 KB + 8 KB in `.lowbss`, i.e. **12 KB against 2 KB of
  headroom** — impossible in the kernel span.
- Making `disk_dir`/`disk_icons` a heap claim is affordable (`[dsk_dirseg]`
  instead of `LOW_SEG` in `dsk_get_dir`, `dsk_get_icon` and the mount's own
  writes — the segment is already explicit in every one of them), but the view
  cache has to grow in step, and `VIEW_KB` is per open Disk window.
- The icon harvest is one sector read per type-1 entry, so a 128-entry folder is
  128 extra reads at every mount — and navigation *is* a mount. Lazy harvesting
  (fill a slot the first time it is drawn) becomes worth doing at that size.

**Recommendation: ship the cap unchanged and say so.** The apps disk is already
foldered for exactly this reason (SPEC.md §19.2), a hard disk wants folders more
than a floppy does, and the alternative is a heap-claimed listing plus a
heap-claimed view cache plus lazy icons — a project of its own, with its own
SPEC section, that has nothing to do with hard disks except that hard disks are
where it starts to hurt. Track it; do not smuggle it in.

### 5.8 New API slots

Appended, never renumbered (SPEC.md §20.8 rule 4):

```
0x0270  osapi_vol_add   (X)  AL = the driver's volume handle, SI = a NUL label,
                             DI = a 32x32 icon body in the caller's segment
                             (0 = none). Registers a driver-backed volume,
                             stages label + icon, adds the desktop zone,
                             repaints the damage. out CF=0 and AL = the volume
                             index, CF=1 refused (no free slot / not a
                             published disk-class driver)
0x0278  osapi_vol_del        AL = a volume index this caller registered.
                             Drops the zone, forces any Disk window on it back
                             to A:, invalidates the FAT window if the mounted
                             volume is this one. Cannot fail.
```

X stubs, so the fence is `ES` — the caller's own segment — tested against the
publishing disk-class driver, which is the same identity test `drv_task` uses
and not an approximation of one.

---

## 6. What lives in the driver

Everything below is `drivers/hdd/`, assembled at `org 0`, one heap claim,
`OS88_DRIVER 'Hard Drive', DRVC_DISK, hdd_entry`, ≤ `DRV_MAX_KB` (40 KB).
`SOUND.DRV` is 5,427 bytes for comparison; this is a bigger driver, and 40 KB is
not a tight ceiling for it.

### 6.1 The Hard Drive page

```
Hard Drive
 (o) IDE 0   980x 10x 17   81M
 ( ) MFM 0   615x  4x 17   20M    <- BIOS geometry, rung 0
 ( ) IDE 1     ?x  ?x  ?    ?     <- unknown: the C/H/S row goes editable
 Cyl [ 980] Hd [ 10] Sec [ 17]  + -
 [Partition] [Format] [Mount]
 Mounted as C:
```

Row selection picks the device; the C/H/S row is the Date/Time field editor with
three fields; the caption is the one place the user is told what is actually
true. **Greying follows SPEC.md §47 to the letter** — one predicate per control,
shared by the greying, the click refusal and the explanation, `gfx_pen_cf` so a
disabled control is a *flag* and not a colour, and a fact rather than a guess:

- no device selected → all three buttons grey;
- geometry unknown → Partition and Format grey, and the caption says which;
- no partition table → Format and Mount grey, Partition live;
- already mounted → Mount becomes Unmount, Partition and Format grey (a
  partition table cannot be rewritten under a mounted volume, and refusing at
  click time would be a greyable fact stated late).

And **the page has to be looked at on a 1bpp adapter before it is called done**,
because a greyed glyph there is a checkerboard and a greyed ring is dotted.

### 6.2 The partitioner

Its own window. Four primary slots, listed as `{bootable, type, start MB, size
MB}`, with create / delete / toggle-active, and a single explicit **Write**
button behind a confirmation, because everything else in the window is edits to
a copy in RAM.

MBR at LBA 0: 446 bytes of code (we write a stub that prints and halts, or
zeros — not a boot loader), four 16-byte entries at 0x1BE, `55 AA` at 510.

```
+0   status      80h active / 00h
+1   start CHS   head; sector in bits 0-5 and cyl bits 8-9 in bits 6-7; cyl low
+4   type        01h FAT12, 04h FAT16 <32MB, 06h FAT16, 05h extended
+5   end CHS
+8   start LBA   dword
+12  sector count dword
```

Era conventions that are not optional if DOS is ever going to read the disk:
the first partition starts at **LBA = spt** (head 1, sector 1, cylinder 0),
partitions end on a cylinder boundary, and the CHS fields must agree with the
LBA fields under the geometry the drive is being addressed with. Partitions are
capped at 65,535 sectors by part 3, so four primaries is the whole offering;
extended partitions (type 05h) chain and are a later phase, not a Phase 1
feature.

**Type 06h vs 04h** is a real choice and the driver should follow the size:
04h for ≤32 MB (which is all of them here) is what DOS 3.3 wrote and what the
oldest tooling expects.

### 6.3 The formatter

A FAT format, not a surface format. Its own window: target partition, volume
label, FAT type (auto / 12 / 16), cluster size (auto), quick vs. verify.

```
sectors/cluster   pick the smallest that lands the cluster count in range:
                  FAT12 needs < 4085 clusters, FAT16 needs >= 4085 and
                  < 65525.  At <=32MB that is spc in {2,4,8,16} for FAT16.
reserved          1
NumFATs           2
RootEntCnt        512  (32 sectors) - the hard-disk convention
Media             F8h
HiddSec           the partition's start LBA - DOS reads it, we do not
BS_jmpBoot        EB xx 90 - *required*, because our own BPB rule 2 rejects
                  a volume whose first byte is not EB or E9
BS_DrvNum 80h, BS_BootSig 29h, VolID, VolLab, FilSysType
```

Then: zero the FATs, write `F8 FF FF` / `F8 FF FF FF` at entry 0, zero the root
directory, write the volume label entry. **The data area is not touched** — that
is what makes a 32 MB format a hundred-odd sector writes instead of 65,000.
A verify pass reads back each data sector and marks bad clusters `FFF7h`; it is
minutes on an MFM drive and belongs behind its own checkbox with its own
progress.

The formatter is also where the tree's own tools earn their keep: a volume this
writes must pass `python3 tools/os88disk.py --verify` from the host, and that is
the acceptance test, not a screenshot.

### 6.4 Mount

`OSAPI_VOL_ADD` with the partition's handle, `'HDD C'` and the icon; then the
kernel's ordinary `disk_mount` runs against volume 2 and every BPB rule in part 5.3
applies. Failure is reported in the page's caption in the same prose the Drivers
page uses for a refused load — one vocabulary, as SPEC.md §51.2 already argues
for `DRVV_TIER`.

### 6.5 Persistence — the driver's own file

Manual geometry, which device was mounted, and which partition, are the
driver's business and belong in **the driver's own file** (`HDD.CFG` on A:),
written through `OSAPI_FILE_WRITE` from a Control Panel click. Not in
`SYSTEM.CFG`: that file's key table is kernel `.text` (`drv_cfg_keys`), so
putting a driver's settings in it puts a driver's settings in the kernel, which
is the thing this whole plan is trying not to do.

Two rules the driver must follow, both of which the kernel already provides for:
the file slots are **UI-task/window-callback context only** (SPEC.md §20.3), and
the current volume must be put back — `OSAPI_FILE_HERE` before,
`OSAPI_FILE_GOTO` after, exactly as `apps/notepad` does around every save.

---

## 7. Concurrency, and the long operation

Every Control Panel and window callback runs on the UI task **with the gfx lock
held**. A format writing 100 sectors is a fraction of a second on IDE and a
couple of seconds on MFM — acceptable, drawn as a progress bar the driver
updates between chunks while it holds the lock (it may draw; it must never take
the lock). A verify pass over 32 MB is minutes and must be abortable, which
means it cannot be one callback.

A driver **may** own a worker task (`OSAPI_DRV_TASK`, SPEC.md §51.7), and the
sound driver's Sound Blaster tier proves it works. But a worker must not touch
the file API, and the disk transport shares `[sch_lock]`, `dsk_secbuf` and the
FAT window with the mount path — so a verify worker would be doing raw device
I/O only, never file I/O, on a volume that is not mounted. That is a coherent
rule and it should be written as one rather than discovered.

`DRVV_DETACH` cannot fail and must not return until its workers are gone
(`[drv_wcnt]`, and `drv_unload` waits on it). For this driver detach also means:
unregister every volume, close every tool window, and leave the controller as
the machine booted it.

---

## 8. Testing

| Thing | Where | How |
|---|---|---|
| IDE task file, IDENTIFY, read/write | **QEMU** | `-drive file=build/hdd.img,format=raw,if=ide` — the default machine's PIIX presents an ATA disk at 1F0h, which is the exact task file part 1.2 programs |
| int 13h rung 0 | **QEMU** | SeaBIOS gives drive 80h with `AH=08h`; that is rung 0 end to end |
| Partition + format + mount + desktop icon | **QEMU** | a blank raw image, partitioned and formatted by the driver, then `tools/os88disk.py --verify` on the host — the in-kernel checks and the host fsck catch different bugs, which is already the rule for `tests/filetest` |
| MFM / ST-11M | **86Box, or MartyPC for CORRECTNESS only** — it has `IbmXebec`, and rung 0 is an option ROM either way, but MartyPC is not disk-accurate (PERFORMANCE.md, docs/MARTYPC-DEBUG.md) so no timing from it counts | it ships the XT ST-506 family (IBM/Xebec, DTC 5150X, WD1002A-WX1, and the Seagate ST-11M/R). Confirm the exact `hdc =` key with the launch-and-`kill -TERM`-and-read-back trick in CLAUDE.md — 86Box rewrites its config with what it actually accepted |
| The FAT window on a floppy | **QEMU** | the degenerate case must be byte-identical: the existing `tests/filetest` gate passing unchanged *is* the assertion |
| The CGA third zone | **QEMU** | `make test VIDEO=cga`, and `tools/shot.py --crop` on the bottom-right corner. part 5.5 says why |
| A partly-full allocator sweep | **QEMU** | count work, do not time it (docs/TESTING.md): a counter on `dsk_fat_window`'s miss path, read over QMP with `xp` |

Two harness gaps to fill: `tools/os88disk.py` builds unpartitioned FAT floppies
and would need an `--mbr` mode (or a small sibling) to produce a partitioned
hard-disk image for the read side of the tests, and the `test` target needs an
`HDD=` knob alongside `ADLIB=`/`SB16=` — the same shape, and the same reason:
without a disk the probe correctly finds nothing, which is the right answer and
not the one you want to be testing against.

---

## 9. Phases

Each is shippable and testable alone.

1. **Per-class service publication** (part 5.1). No new feature, no user-visible
   change; `SOUND.DRV` keeps working and the gate for it is `tests/fmtest`
   passing unchanged. This must land first or every later phase carries a latent
   bug that presents as broken sound.
2. **The FAT window** (part 4). Floppy-only, no hard disk in sight. Gate:
   `tests/filetest` and `--verify` unchanged, plus the miss counter reading zero
   on a 1.44 MB volume.
3. **Volumes and the transport** (part 3, part 5.2, part 5.3) with a stub driver that
   exposes a QEMU IDE disk read-only through int 13h rung 0, and a hard-coded
   pre-partitioned, pre-formatted test image. First mount of a real hard disk.
4. **The desktop zone and the Control Panel page** (part 5.4, part 5.5) — the page, the
   device list, geometry detection and the manual editor. Still no writing.
5. **The partitioner** (part 6.2).
6. **The formatter** (part 6.3). At this point the whole request is met.
7. **The IDE task file path** (part 1.2) — direct IDENTIFY and PIO for machines
   whose BIOS does not know the drive. Optional, and the first phase that can
   damage a disk a BIOS would have protected.

Phases 1 and 2 are worth doing **whether or not hard disks are ever built**:
one is a real latent bug and the other is a strictly cheaper mount.

---

## 10. SPEC.md impact

SPEC first, always — every one of these lands before its code:

- part 2.1 — `FAT_SEG` stops being "the FAT snapshot" and becomes "the FAT window";
  the FAT2 fallback moves from mount to window fault.
- part 18 — a new part 18.7, the volume table and the block transport; `disk_read`/
  `disk_write` keep their `AX = LBA` contract and gain a documented kind branch.
- part 18.2 — rules 10–13 become per-kind; rule 8 keeps its documented rejection and
  gains the sentence that it is *also* the 32 MB partition limit. Fix the
  9-vs-10 `DSK_FAT_SECS` nit while there.
- part 18.3 — the mount no longer reads the FAT.
- part 18.4 — the commit order gains the paragraph in part 4.5: an early window flush is
  a leak and never a cross-link.
- part 19 — nothing. The FAT12/16 format is unchanged; only where its FAT lives is.
- part 20.3 — two appended slots.
- part 26 — desktop zones are registered as well as counted, and the CGA constraint
  is stated where the geometry is.
- part 31.1 — the item table's fourth word stops being reserved and becomes the
  dispatch class; the driver page ABI is pinned there.
- part 47 — nothing new, but the Hard Drive page is a worked example of it.
- part 51 — a new part 51.8, the disk driver class: `DSV_BLK`, the volume handle, the
  detach obligations, and per-class publication in part 51.2.
- New part 52, `hdd.inc`-equivalent — the driver itself, its page, its two tools and
  its file, in the way part 34/part 51.4 documents the sound driver.

---

## 11. Rejected, and out of scope

- **32-bit LBAs in the kernel FAT layer.** part 3. Rewrites the FAT driver for
  capacity the target machines did not have; four ≤32 MB primaries is the DOS
  3.3 answer and it is enough for a 40 MB MFM drive and a 120 MB IDE one.
- **Booting os8088 from the hard disk.** `boot/boot.asm` reads LBA 1..K with the
  floppy geometry injected at build time and relocates itself to `BOOT_RELOC`;
  hard-disk boot needs an MBR, an `AH=08h` geometry probe and a second boot
  sector variant. It is a coherent later project and it is not what was asked
  for. The system disk stays A:, and `SYSTEM.CFG` with it.
- **Low-level / surface MFM formatting.** part 1.1. Vendor-specific interleave and
  defect handling, minutes per drive, and the ST-11M ships its own ROM utility
  that does it correctly.
- **The XT DCB register path.** part 1.1. The card's ROM is the interface; the row
  exists so the page can say "not supported" instead of "no drive".
- **A general sector buffer cache** (DOS `BUFFERS=`). part 4.2. os8088 has two FAT
  accessors and both are already funnelled through one reader and one writer;
  a general cache is a bigger machine solving a problem this one does not have.
- **Raising the 32-entry directory cap in this project.** part 5.7. It is real, it
  is where a hard disk hurts first, and it is a heap-claimed listing plus a
  heap-claimed view cache plus lazy icon harvesting — its own SPEC section and
  its own phases.

---

## 12. What the plan got wrong

Kept because the reasoning above is only useful if its errors are visible too.

- **"~1,000–1,100 bytes of kernel"** was close on the code and silent on the
  consequence: the additions overran **`KERN_CODE_MAX`**, the 64KB segment, which
  cannot be raised. What paid for it was not in the plan at all — the
  768-byte per-instance icon COPY table went away, because a package's icon
  was already in the package's own region at a fixed offset for exactly as
  long as the instance lived (SPEC.md §25). The figures are 65,465 of 65,536
  in the segment and 80,384 of 80,896 in the budget, with no raise — and the
  segment is the binding one now, at 71 bytes.
- **"the 32-entry cap is a project of its own; ship it unchanged"** was the
  right call about the *work* and the wrong one about the *shape*. Making the
  listing four words instead of two labels (SPEC.md §22.6) turned out to be
  the small half, and the view cache followed it in twenty lines; what was
  genuinely big — a sparse icon table, lazy harvesting — was not needed once
  the buffer could simply be bigger on the volume that needs it.
- **Part 6.5's "the driver's own file"** was right that a driver's settings
  must not become the kernel's business, and wrong that a separate file was
  the way to keep them out of it. The argument it missed is the one it makes
  itself two paragraphs later: the file slots resolve in the *current* volume
  and directory, so putting the volume back is an `OSAPI_FILE_GOTO`, and that
  is a full **remount**. The driver's boot path was therefore two remounts and
  a directory search and a read, at every boot, for 34 bytes travelling next
  to the Control Panel's other 41 in a file the boot already reads.
  `OSAPI_DRV_CFG` (SPEC.md §51.9) carries them as an opaque blob instead: the
  kernel knows the key's name and length and nothing about the contents, so
  the separation the plan was defending survives intact, and it survives a
  boot where the driver never loads — the blob round-trips untouched rather
  than being dropped. The real cost, which the plan would have been right to
  weigh, is that `DRV_BLOB_SZ` is reserved on every machine, hard disk or not.

---

## 13. What a copy actually cost, measured

The plan never asked how *fast* any of this would be, and the answer turned
out to be "four and a half times more I/O than the files needed". The
reference workload throughout is `APPS` — nine files, 175,214 bytes,
**343 sectors** — copied from HDD C to HDD D under `make test HDD=40`, with
counters dropped into `disk_mount`, `dsk_xfer`, `dsk_fat_window` and the icon
harvest and read over QMP.

| | before | after |
|---|---:|---:|
| `disk_mount` calls | 62 | 45 |
| sectors read | 1,525 | 873 |
| sectors written | 393 | 383 |
| transfer calls (= IDE commands) | 1,420 | 394 |
| FAT window loads | 62 | 45 |
| icon-harvest reads | 348 | 8 |
| directory-scan reads | 62 | 3 |
| read amplification | 4.44x | 2.55x |

Where the 1,525 reads went, before: **1,030 of them — 68% — were remount**.
Every `fcp_goto` that crossed volumes ran a full `disk_mount`, and `fcp_xfer`
crossed about five times per file. A mount is a boot sector, a nine-sector FAT
window, a directory scan, a sort, and **one read per file in the directory**
for the icon harvest — so the destination's icons were re-harvested on every
switch back, and the cost of copying a folder grew with the number of files
already in it.

Five things changed, and one of them was a bug rather than a speed fix:

1. **A volume switch is not a mount** (SPEC.md §18.9) — `dsk_chdir_q` stops
   after the FAT window. Harvest reads 348 → 8.
2. **A file costs two switches, not five** (§22.5) — the destination is
   created carrying its first chunk, and the loop ends on `[fcp_rsz]` instead
   of switching to be told there is nothing left.
3. **`fcp_rdnext` is one `dsk_read_chain` per chunk** (§22.5), not one
   `disk_read` per cluster; `dskw_wdata` writes runs, not sectors (§18.4.1).
4. **Both driver transports batch a run into one command** (§52.1). This is
   the line that matters on real hardware and not at all under QEMU: 1,918
   IDE commands became 394.
5. **The formatter picks its cluster size from a capacity table** (§52.3).
   A 31MB partition's FAT went from 254 sectors to 64.

And the bug: change 5 made two partitions on one disk disagree about cluster
size for the first time, which exposed that `fcp_chunkset` had always used
`dskw_clbytes` of *whichever volume was current*. A chunk has to be a multiple
of both — the destination's for `dskw_append`'s precondition, the source's
because a take ending mid-cluster skips the rest of it — so a 116KB file
copied from the 31MB volume to the 6MB one arrived **64,512 bytes long, with
no error reported**. `fcp_clspan` is the fix. It was latent before only
because every partition the formatter made had 512-byte clusters.

Both of the levers that were left are now pulled, and this is where it ended:

| | original | after the first pass | now |
|---|---:|---:|---:|
| `disk_mount` calls | 62 | 45 | 45 |
| sectors read | 1,525 | 873 | **495** |
| sectors written | 393 | 383 | 383 |
| transfer calls = IDE commands | 1,420 | 394 | **192** |
| FAT window loads | 62 | 45 | **3** |
| read amplification | 4.44x | 2.55x | **1.44x** |

- **Writes coalesce across clusters** now (SPEC.md §18.4.1) — 383 sectors in
  57 calls, 6.7 each, against 217 when the run stopped at every cluster
  boundary. `dskw_wdata` still allocates and links one cluster at a time; it
  just stopped *writing* one at a time.
- **Each driver-backed volume owns its FAT window** (§18.8.1), out of the
  heap. That was the one place in this whole exercise where the answer to
  "are we missing a buffer" was yes: `DSK_FAT_SECS` sectors per volume, and
  45 switches now cost 3 loads instead of 45.

Total: **1,918 sectors and 1,918 IDE commands became 878 and 192** — 90% of
the commands gone, on a workload where the files themselves are 343 sectors.
What is left is close to the floor: 495 reads against 343 of payload, and the
343 of that payload cannot go away.
