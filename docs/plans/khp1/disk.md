# Kernel hot-paths pass 1: disk (transfers, decompression, loading)

The **disk** agent's record (BRIEF.md section 6). Owned files:
`kernel/disk.inc`, `kernel/diskw.inc`, `kernel/drvvol.inc`, `kernel/lz.inc`,
`kernel/lzfilt.inc`, `kernel/loader.inc`, `kernel/mod.inc`,
`kernel/compress.inc`. One file outside every row was read and experimented on
(section 4), and none of its bytes were committed: `boot/boot2.asm` (stage 2
and `kz_expand`, the kernel's own decoder).

**Instruments.** MartyPC guest cycles throughout (4.772727 MHz, so cycles /
4,772.727 = ms on the 5150), on the GLaBIOS twins. Disk is priced in `int 13h`
CALLS, read off the FDC counters (`m.disk()`) and off logging breakpoints on
`dsk_xfer.attempt` with the registers, which give each call's C/H/S. This
MartyPC models the platter (patch 04), so its disk ms are close to a 5150's;
but GLaBIOS turns a call round faster than the IBM ROM (docs/MARTYPC-DEBUG.md,
*Which of them a DISK number may come off*), so a call saved is worth MORE on
the field machine than the ms quoted here. The IBM ROM is not in this tree.
The scratch tools were written for this note and not committed:

- `os88boot.py` (in the tree) for the boot, plus a breakpoint bracket on the
  blob's `kz_all` that also compares the expanded image with `kernel.bin`;
- an `int 13h` tracer from reset to `kmain` (ROM vector breakpoint, registers);
- a launch probe, `bp_trace(log=True)` on `ld_run_body_x`, `.read`,
  `lz_decomp_x`, `.expanded`, `ld_start` and `dsk_xfer.attempt`, which splits a
  launch into peek+alloc / read / decode / start and counts its calls;
- a decode sampler: stop at `lz_decomp_x`, `advance` 3,000 (or 1,500) cycles,
  read CS:IP and resolve it by LINEAR address with the `.cold` slide, until
  `ld_run_body_x.expanded`;
- a navigation tracer (open a drive, enter a folder, Up), C/H/S per call;
- **a unicorn equivalence harness for `kernel/lz.inc`**: the base decoder and
  the tip decoder assembled side by side and run on every packed package in
  `build/` (54 streams, LZ4 and LZB), separate and in place, with the declared
  length off by one each way, truncated four ways, 4 random mutations per
  stream, three synthesised outputs past 64KB (both formats, fuzzed), three
  long-literal streams (LZ4's 16KB pieces, in place too), and 200 garbage
  streams, over a 384KB arena poisoned with CCh. It compares CF, DI on
  success, DS, ES, SP and BP, and every byte of the arena: **914 cases, 0
  different**, at each commit;
- a swap test: B: changed under an open Disk window (apps360 -> games360 and
  back), and the listings checked against mtools.

## 1. BASE AND TIP

Assembled by `make`'s `kernsize` lines. Resident = `.text` + `.bss` +
`.cold` + `.vga` + `.lowbss` + `.vgabuf`. The tip column includes every branch
merged into `kernel-hotpaths` up to its notices (files, sched, the
coordinator's), so **mine** is given separately.

| kernel | base resident | tip resident | of it mine | base KERN_SIZE | tip KERN_SIZE |
|---|---:|---:|---:|---:|---:|
| kern_big | 91,530 | 92,143 | **+462** | 96,768 | 97,792 |
| kern_small | 57,258 | 57,869 | **+460** | 59,392 | 59,904 |
| kern_emu | 91,784 | 92,385 | **+462** | 97,280 | 97,792 |
| kern_mega | `.text` 45,853, `.cold` 46,395 | 45,991, 46,619 | **+212** of `.cold` (above 1MB) | conventional resident 62,976 | 62,976 |

Mine, by commit (kern_big / kern_small / kern_mega): `mod_cfix` +2 / 0 / 0;
lz_lz4 +126 / +126 / +126; lz_lzb +72 / +72 / +72; the drop before the FAT
window +14 / +14 / +14; the peek's cylinder fill +208 / +208 / 0; the LBA 0
fill +40 / +40 / 0. Every one of them is in `.cold` (the hint word is 2 of
`.text`), so none of it is before COLD_HOME except those 2 bytes. kern_dos is
byte-identical except where SPEC.md requires (it carries none of the disk
items; its image had 20 bytes left - see disk-4).

## 2. WHERE THE TIME GOES

### 2.1 Boot to desktop, 360KB, CGA (GLaBIOS), base, `os88boot.py`

| phase | ms | of which disk |
|---|---:|---|
| ROM POST | 4,817 | 1 sector |
| boot sector `int 13h` x14 | 5,918 | 167 sectors, 13 reads, 2,796 ms mechanical |
| boot sector loop (of it `kz_all`, the kernel's decode) | 727 (**643**) | - |
| boot splash | 207 | - |
| `drv_boot_x` | 1,191 | 18 sectors, 3 reads, 566 mechanical |
| `mouse_init` (pointer's) | 610 | - |
| `as_boot` (files') | 467 | 4 sectors, 1 read |
| `cfx_slide`, `ui_lcall`, `mem_unblob_x` | 121, 113, 104 | - |
| **total from reset** | **14,419** (9,602 below POST) | |

The 14 boot-sector calls, traced: the blob in two (0/1/6 x4, 1/0/1 x9 - it
straddles cylinders 0 and 1), the plain head in one (1/1/1 x9), then a
cylinder a call (2/0/1 x18 ...) **except cylinder 6, which the 64KB DMA page
cuts into 8 + 10** - two calls, 801 ms against a whole cylinder's ~600 - and a
last call of ONE sector at cylinder 10. `kz_all` is 3,067,460 cycles for
92,322 bytes, 33.2 a byte: 62,103 literal bytes and 30,219 match bytes in
3,566 symbols, block 1 (30,882 bytes) barely compressing.

### 2.2 Package launch, 360KB, CGA (GLaBIOS), base

| package (format) | peek + alloc | read | decode | calls |
|---|---:|---:|---:|---:|
| Calc (LZB, 4,662 bytes) | 346 | 26 | **291** | 1 |
| Note Pad (LZ4, 17,706) | 339 | 854 | **405** | 3 |
| Paint (LZ4, 27,487) | 431 | 1,416 | **607** | 4 |
| TexPad (LZB, 20,712) | 437 | 855 | **1,300** | 3 |

Every file here starts at head 0 sector 1 of a cylinder, and every multi-track
one took **one call more than the cylinders it occupies** (Note Pad: 8/0/1 x9,
8/1/1 x9, 9/0/1): the header peek's cache fill stopped at the TRACK.

Inside the decode, sampled:

- **Note Pad, LZ4** (1,931,258 cycles, 109.7 a byte): `lz_copy` 27%, `lz_lz4`
  21%, the near-call filter 16% (313K, `repne scasb` at its floor), `lz_match`
  14%, `lz_take` 13%, `lz_len` 6%. About 1,040 cycles of OVERHEAD for each of
  1,252 symbols - six near calls a symbol, two of them read-modify-writing a
  32-bit count on the stack.
- **Calc, LZB** (1,384,293 cycles, 303 a byte): `lz_lzb` 62%, `lz_match` 14%,
  `lz_copy` 10%, `lz_take` 5.5%, the filter 5.5%. 1,881 literals, 875 matches
  of 3.1 bytes, 5.3 gamma bits a match.

### 2.3 Navigation, base (FDC calls)

| geometry | open B: (root) | enter APPS | Up |
|---|---:|---:|---:|
| 360KB | 4 | 3 | 3 |
| 720KB | 4 | 3 | 3 |
| 1.44MB | 4 | 5 | 3 |

On 360KB, open B: is LBA 0 (one sector, the swap detector), the FAT window's
fill (LBA 1-8: both FATs AND the root's first four), the root's first sector
AGAIN (the cache dropped after the FAT load), and a data read (the icons'
files). A folder is LBA 0, the FAT fill, and its own cluster.

### 2.4 What is NOT where the time goes

The CPU paths the brief pinned are small against their calls: `dsk_xfer`'s
per-call set-up, `dsk_runcap` (~70 cycles) and `dsk_next_clus_x` (~300 a
cluster: Paint's 22-cluster chain is ~1.4 ms) are each well under 1% of the
~200-400 ms call they prepare. Directory search and the FAT window are the
same. **The decoders are the CPU lever** (H1, per byte, on every launch, every
driver and module load and every `CZ` file), and **calls are the disk lever**.

## 3. TAKEN

| id | agent | routine / path | hotness | change | kern_big B | kern_small B | cycles base -> tip (input) | user-visible (operation, adapter: ms base -> tip) | M/P | 286 | commit | FREE / [+N] |
|---|---|---|---|---|---:|---:|---|---|---|---|---|---|
| disk-1 | disk | `lz_lz4` | H1 (per byte; every LZ4 package, driver, module, part, CZ file) | the plain symbol inline; a BUDGET register `min(REM, FFFFh-DI)` makes the take one `sub`/`jb`; REM settled lazily by `lz_sync`; BP the checkpoint; slow paths unchanged | +126 | +126 | Note Pad's decode 1,931,258 -> 1,122,698 (17,604 bytes, filtered) | launch Note Pad, CGA: decode 405 -> 235 ms; Paint 607 -> 353 ms | M | same direction (fewer memory operands, fewer taken branches); not measured | f47761a8 | [+126] |
| disk-2 | disk | `lz_lzb` | H1 | the same budget for the literal byte and the plain match; bit refills out of line | +72 | +72 | Calc's decode 1,384,293 -> 871,484 (4,566 bytes) | launch Calc, CGA: decode 290 -> 183 ms; TexPad 1,300 -> 831 ms | M | as disk-1; not measured | d122c19d (depends on f47761a8) | [+72] |
| disk-3 | disk | `disk_mount_x` | H4 (every navigation) | a listing mount drops the read-ahead BEFORE the FAT window, so the window's fill serves the root | +14 | +14 | - | open B: at its root, 360KB: 4 -> 3 calls (int 13h time 1,012 -> 788 ms on GLaBIOS); superseded on kern_big/small by disk-5, kept on kern_mega | M (calls), P (~400 ms a call on the 5150) | none: calls | 04528080 | [+14] |
| disk-4 | disk | `dsk_rah_fill`, `ld_run_body_x` | H4 (every launch) | the loader's header peek arms a hint (the file's sectors); its fill runs to the cylinder's end over several slots in ONE call | +208 | +208 | - | launches, 360KB: Note Pad 3 -> 2 calls, TexPad 3 -> 2, Paint 4 -> 3, Calc 1 -> 1; 1.44MB: 2 -> 1, 2 -> 1, 3 -> 2, 1 -> 1. Peek + read, GLaBIOS: Note Pad 1,193 -> 1,038 ms, Paint 1,847 -> 1,541, TexPad 1,292 -> 1,097 | M (calls and GLaBIOS ms), P (~400 ms a call on the 5150) | none: calls | 1217b318 | [+208] |
| disk-5 | disk | `disk_mount_x` step 1 | H4 (every navigation) | a listing mount empties the cache first and reads LBA 0 THROUGH it as a fill of eight (LBA 0-7: boot sector, FAT window, root head); the chunk re-tagged with the new disk's signature | +40 | +40 | - | 360KB and 720KB: open B: 4 -> 2 calls, enter APPS 3 -> 2, Up 3 -> 1 (with disk-3); 1.44MB unchanged | M (calls), P (ms) | none: calls | 073d705b (depends on 1217b318) | [+40] |

Totals for the user, PREDICTED at ~400 ms a call on the 5150 and MEASURED for
the CPU: opening a drive's root on 360KB **-2 calls (~0.8 s)**; entering a
folder -1 (~0.4 s); Up -2 (~0.8 s); launching a multi-track package -1 call
(~0.4 s) plus the decode (Note Pad -170 ms, Paint -254, TexPad -469, Calc
-107). On MartyPC's GLaBIOS model, peek + read + decode: Note Pad 1,598 ->
1,271 ms, Paint 2,454 -> 1,894, TexPad 2,592 -> 1,928, Calc 663 -> 574.

## 4. TRIED AND REFUSED

- **A faster `kz_expand`** (the boot blob's own LZ4 decoder: the branches
  turned to fall through, CH kept 0, the match without `push si`, long
  literals a word at a time). MEASURED: `kz_all` 643 -> 588 ms. But the blob is
  **exactly full** at the base (6,656 of 6,656 bytes) and this is 21 bytes
  longer, so it needs a 14th `BOOT2_SECS` sector - and that sector costs the
  360KB boot **two** calls (MEASURED: the blob is then 4 + 9 + 1 and the head
  straddles cylinder 2). Refused: -55 ms of CPU for +2 calls.
- **The page-split bounce in `read_run`** (read the cylinder the 64KB page cuts
  into the tail's own free gap below, then copy it up). MEASURED in the same
  experiment: cylinder 6 is one call again (6/0/1 x18). It needs ~30 blob
  bytes, so the same sector: +2 calls -1 = refused at this blob size. The
  zero-byte way to the same call is in WANTS THE OWNER.
- **Word copies in `lz_lz4`** (`shr cx,1 / rep movsw / adc / rep movsb`):
  literals average 8.3 bytes and matches 5.7 in Note Pad, and the fetch
  bytes of the second `rep` eat the 4.5 cycles a byte saved below ~9 bytes.
  Estimated ~12K cycles (2.5 ms) on Note Pad. Not worth the bytes.
- **The LZB gamma code inline** (two near calls a match): ~55 bytes for an
  estimated ~87K cycles (18 ms) on Calc. Below the other items' ratio; not
  taken.
- **Fusing the near-call filter into the copies.** The filter's 18 cycles a
  byte is `repne scasb`'s 15 plus ~89 a hit; testing each copied byte instead
  would be slower than the scan. The filter is at its floor.
- **A 1.44MB LBA 0 fill of 18.** At LBA 0 the mount is on the FALLBACK 9/2
  geometry, so sectors 9-17 would be read as head 1's and cached as LBA 9-17:
  wrong bytes under a right key. The fill is 8 for exactly that reason.
- **A 1.44MB FAT-tail fill on to the root** (LBA 8 -> 20): +13 sectors of
  transfer (~140 ms) against one call; marginal, not taken.
- **CPU work in `dsk_xfer`, `dsk_runcap`, `dsk_next_clus_x`**: see 2.4.

## 5. WANTS AN ABI BREAK

None found that pays. The one candidate was **carrying a package's image and
bss sizes outside the file** (the directory entry, or `ASSOC.DAT`'s rows), so
the loader could size its region without the header peek. After disk-4 the
peek's call already reads the file's first cylinder for the read behind it, so
removing the peek saves no further call: ~0 ms, for a change to a frozen file
format. Not proposed.

## 6. WANTS THE OWNER

- **The files agent's finds 2 and 3** (docs/plans/khp1/files.md section 7):
  reuse the FAT window on a full mount when `[dsk_sigcur]` matches, and skip
  the boot-sector read while the motor still runs. These are
  docs/plans/completed/DISK-PERF-PLAN.md's Phases 2 and 3, **dropped by the
  owner's decision** (section 4 there) because a 5150's drives have no
  change line, so both NARROW what the kernel notices: a swap under a
  spinning motor, or between two disks with one boot sector. After disk-5,
  360KB and 720KB have nothing left for the first to save (the FAT window
  comes in the LBA 0 transfer); on 1.44MB it saves one call a navigation
  (~0.4 s P). The second saves one call (~0.4 s P) on a navigation within
  2.03 s of the last. Not taken.
- **Removing the boot's page-split call with no blob byte**: `tools/os88kz.py`
  could pad R (the tail's in-place gap) so the 64KB boundary falls ON a
  cylinder boundary of the 360KB/720KB layout (today +8 sectors), which needs
  `BLOB_LIFT` raised to keep the tail's read off the blob (+4KB at boot only;
  guard 5 has 23,552 left). One call (~0.4 s P; 801 -> ~600 ms MEASURED on
  GLaBIOS) on every 360KB and 720KB floppy boot, 0 resident bytes, but it is a
  host tool that writes KERNEL.SYS and a boot-layout constant: a decision.
- **The LZB packages** (`LZBPKGS`, apps size pass 2: "a package whose disks
  matter more than its launch"). The trade's two sides moved: LZB is now 191
  cycles a byte at launch (Calc) and LZ4 64 (Note Pad, filter included) -
  ~3x where it was ~4x. Re-weighing the list is the owner's.
- **A blob sector**: section 4's two boot items together buy ~55 ms and one
  call for two calls - not recommended at today's layout; recorded for
  whoever next spends `.boot2`.

## 7. CROSS-FILE

- **files.inc `fm_draw_icon16`** (files agent, find 4): ORs 32 words per icon
  to test "no icon" (~1,600 cycles). `dsk_ico_stage` (mine) knows the answer
  (ICO_R_NONE, a failed `ico_body`) and could return it in ZF for ~3 bytes;
  then files' OR loop retires. Not taken alone: half an item with no consumer.
  The folder icon's per-row decode from runs (~3,560 cycles) is the other half.
- **boot/boot.asm + boot/boot2.asm** (no row): the blob's second piece (1/0/1
  x9) and the kernel's plain head (1/1/1 x9) are ONE cylinder read as two calls
  by two stages. Stage 1 reading the whole cylinder into the blob's landing and
  stage 2 moving the head down (4.6KB, ~12 ms) would save one boot call (~0.4 s
  P). It changes the stage 1/2 contract and stage 1 has a 512-byte sector.

## 8. ROWS RUN

One pass after the last code commit (073d705b), `python3 tools/os88test.py
soak -k ... --marty-jobs 1`, 36 rows: **36 passed, 0 failed, 0
skipped**, 1,357 s (`lzbig` 280 s, `install` and `smallboot` the next).

Rows that cover the items: disk-1/2 `lzload`, `lzload-lz4`, `lzdrv`,
`lzdrv-nohint`, `lzfence`, `lzfile`, `lzcomp`, `lzglyph`, `lzbig`, `lzmod`,
`lzmod-lzb`, `lzmod-lz4big`, `lzship`, `lzship-lzb`, `lzbclust`,
`mseglzslack`, `ldcost`; disk-3/5 `dirwsize`, `dirwshed`, `fatwpin`,
`curdisk`, `imgmount`, `imgswap`, `rdmount`, `diskclone`, `install`,
`stikswap`; disk-4 `smalllaunch`, `tmload`, `ldcost`; the boot path
`bootstatus`, `postboot`, `smallboot`, `bootfloor`, `hdboot`, `kzboot`,
`cylrun`. Plus `make` (the fast tier, with `hotpar`, `canary`,
`diskverify`) at every commit, `make small`, `make emu` and `make mega` at
the tip.

## 9. DEFECTS FOUND

- **`mod_cfix`'s far-call test accepted eight opcodes** (fixed, 4cb73375;
  on `kernel-hotpaths` as 6209b524). `or al, 70h / cmp al, FAh` reads 8A 9A AA
  BA CA DA EA FA as "a far call or jump", and 8A (`mov r8, r/m8`) and BA
  (`mov dx, imm16`) are in every module. Found by the vga agent when its
  +69 `.vga` moved COLD_HOME to 08B3h and `tools/os88rom.py`'s proof refused
  CTRL.DRV's `8A 0E 3D B3 08`; without the proof that word would have been
  re-pointed. Now `xor al, 9Ah / je / cmp al, 9Ah ^ EAh`, +2 bytes of kern_big
  `.cold`, os88rom's `cfix()` the same predicate. **The sweep**: every home
  0600h-0BFFh, each with the module images' site words set to it, through
  `cfix_prove`: the old predicate refused 50 homes, the new one 14 - 0647 0674
  0675 0686 068E 06B8 06C6 073C 078B 0855 086D 087B 090B 09FA - genuine 9A/EA
  look-alikes no opcode test can tell from a site. The nearest above today's
  08AEh is 090Bh, ~1.5KB of resident growth away; the remedy past it is a
  TABLE of the sites, not a better predicate.
- **A one-sector read at a track's end is never cached** (1.44MB, entering
  APPS reads 0/1/18 twice in two calls, at the base too): a request covering
  its whole would-be chunk takes the direct path (SPEC.md 18.95.1), and two
  readers ask for that sector. One call on that navigation; not fixed.
- (Caught in development, never committed: the loader's hint first clobbered
  AX, the cluster `dsk_peek_x` reads - every launch answered LD_EDISK - and
  `DSK_RAHINT` was first defined below `disk_mount_x`, which silently built
  the LBA 0 change out. Both found by tracing, not by assembling.)
