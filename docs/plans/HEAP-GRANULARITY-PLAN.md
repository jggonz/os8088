# HEAP-GRANULARITY-PLAN — claims finer than a kilobyte

**STATUS: BUILT on `heap-size` at a 512-byte granule, the 256-byte class built
as a knob and not shipped; section 10 is what was built, against this plan.**
The sections before it are Revision 2 of the research, after the owner's
review of revision 1 (section 9 records what changed and why), and are kept as
the design record. Every figure marked *measured*
was taken on `heap-size` cut from `elendilon` at `3cf34ee` (2026-10-31),
host-side, off `build/`. Every figure marked *estimated* is arithmetic.

The question: claims have been whole KB since SPEC.md 50 was written, because
that made the Task Manager's figures easy to show. What would 512-byte or
256-byte claims take, what would break, what would they cost, and what do we
recommend?

## 0. The answer

1. **The allocator already counts in paragraphs.** `MC_PARA` is paragraphs
   (`kernel/memory.inc:76`). The scan, the 64KB DMA page test, the compaction
   mover, `mem_regrow` and `mem_region_reloc` carry no KB assumption. KB exists
   only at the doors (`mem_claim_1`'s `shl ax, 6` at `memory.inc:975`,
   `mem_regrow`'s at `:4334`), in the byte-to-KB rounders (`mem_bytes_kb`,
   `mem_bytes_kb_x`), and in the readers (`mem_avail`, `mem_sum_kb`,
   `osapi_sys_kb`, the Task Manager).
2. **Do it once: make the granule ONE constant, `MEM_GRAN`, and give packages a
   door in PARAGRAPHS.** A paragraph door outlives any granule: the kernel
   rounds up to whatever `MEM_GRAN` is and answers what it granted. Moving from
   512 to 256 later is then a constant and an alignment rule, not an ABI. The
   311 package call sites only gain if they can express a size finer than a KB,
   and that door is how they do. Most of them get it from three shared files
   (section 3.3).
3. **Alignment and size granularity are two different properties, and today one
   is a side effect of the other.** The Hard Rule needs every int 13h target
   **base** to be 512-aligned. It does not need every **size** to be. Claims
   split into two classes: **DISK** (base 512-aligned: package regions,
   drivers, modules, the disk caches, and every claim through today's KB
   doors, which packages already rely on) and **CPU** (base `MEM_GRAN`-aligned:
   everything a CPU only touches). Two of the three alignment asserts in
   packages (section 1.2) exist only because nothing stated this distinction.
4. **Recommendation: build the machinery for 256 and ship it at 512 first.**
   The alignment bump is written once, assembled out while `MEM_GRAN` = 512
   (where every base is aligned by construction), and kept assembling by a
   `MEMGRAN=256` knob. Flip the default after a field measurement of 256-byte
   holes (section 2.6). The step to 256 is real for CPU claims (~128 bytes each). For
   top-down disk images it turns rounding into a hole above the image, which is
   the trap `tools/os88drv.py` already measured once (14,336 bytes stranded).
5. **What it buys at 512: ~3–5 KB of heap in an ordinary session, for ~100–150
   resident bytes** (*estimated*, section 2.5 and section 4). The largest single kernel item
   is the heap's own tail. On today's kern_big the floor is 96.5 KB, so the top
   512 bytes of every machine's heap can never be claimed (*measured*: `HEAP
   0x1820`).
6. **The Task Manager stays four characters wide and becomes EXACT up to 99½K**
   with a private ¼ ½ ¾ glyph set, blitted in place of one cell. Totals close
   by cumulative rounding (section 5).

## 1. Where the KB lives today

### 1.1 The allocator: only at the edges

| site | what it does | under `MEM_GRAN` |
|---|---|---|
| `mem_claim_1` `memory.inc:970-977` | `cmp ax,1024 / shl ax,6`: KB to paragraphs, the one door every claim reaches | takes **paragraphs** and rounds up to `MEM_GRAN` itself (section 3.1) |
| `mem_claim_dma_x` `:822` | DMA head, KB to paragraphs | the paragraph door takes the head in paragraphs. Heads are stored in paragraphs already (`MC_DMA`) |
| `mem_compact` `:3787` | the pending claim's KB to paragraphs | follows `mem_claim_1` |
| `mem_regrow` `:4334` | the new KB to paragraphs | paragraph door, and the KB door shifts |
| `mem_avail` `:4752` | paragraphs to KB, **rounded down** | the KB door keeps rounding down, because "claim what AVAIL said" must always fit. The paragraph door answers exact paragraphs |
| `mem_sum_kb` / `mem_owned_kb` `:4803-4873` | sum paragraphs, then one `shr 6` | readers move to paragraphs (section 5) |
| `osapi_sys_kb` `SK_HEAP` `:5749` | `(top-base) >> 6`, truncated | append `SK_HEAPP` in paragraphs. `SK_HEAP` stays so that `apps/dos/dos.asm:8128`'s `SK_KERN + SK_HEAP` identity keeps cancelling |
| `mem_bytes_kb` `:5972`, `mem_bytes_kb_x` `:6011` | bytes to KB, rounded up | become bytes to paragraphs (`add 15 / shr 4`). The granule rounding happens in the claim, once |
| `mem_hiclaims` `:3566-3571` (kern_mega) | `MC_PARA >> 6` to the KB the pool claim gets, **truncating**, while the copy after it moves all of `MC_PARA` | **a latent defect a finer unit exposes**: a 1.5K claim would get a 1K pool block and the copy would overrun it. Needs `add ax,63` first, 3 bytes, kern_mega only |

Paths with **no** KB assumption: the overlap scan, the DMA page bump
(paragraph arithmetic at `:1025-1043`, `:1563`, `:2588`), `mem_bcopy`
(64-paragraph chunks with an exact short tail, `:1658`; its comment at `:1636`
already reasons about a 16-paragraph shift), the compaction walk,
`mem_rr_walk`, `mem_hifit`, shed and purge, `mega_dmapb`, and hibernate's line
stepper.

### 1.2 Alignment: what actually needs 512

The heap floor is rounded to 512 (`mem_floor_ax`, `add ax,31 / and al,0xE0`)
and the top is KB-aligned (`int 12h`). Every candidate base `mem_claim_1`
tries is one of the floor, `top − size`, a claim's end, a claim's base −
size, a page floor, or a page floor − head. Compaction's destinations are
base + Σsizes and top − Σsizes. All of these stay 512-aligned **exactly when
sizes and heads are multiples of 32 paragraphs.** So at `MEM_GRAN` = 512 the
alignment is free and needs no class code at all. Guard 6b
(`kernel/kernel.asm:9303`, `%if MEM_PARA_KB % 32`) is the assembler's form of
that sentence.

Below 512, sizes no longer imply alignment, so it has to be asked for. Who
needs a 512-aligned **base**:

| holder | why | class |
|---|---|---|
| package region (loader) | the image is read into it through `dsk_xfer`, and on a hard disk through `HDD.DRV`, which has no bounce (`drivers/hdd/hdd.asm:331`: *"forcing a run of 1 would issue exactly the straddling transfer"*) | DISK |
| driver image, module image | read the same way | DISK |
| FAT window, read-ahead, dirw, the mount buffers | int 13h targets | DISK |
| `apps/os88type.inc:806` `ty_open` (faces) | reads into the claim and **asserts** `test ax, 0x01FF` | DISK, honestly declared |
| `apps/pixel/pxpump.inc:34` `px_dmaclaim` | asserts the same | DISK |
| `apps/audio/audio.asm:325` look-ahead ring | relies on the alignment without checking | DISK |
| everything else: menu save-under, clipboard, icon store, canvases, documents, Frotz story memory, a parts scratch, a C heap | CPU only | CPU |

The kernel's own BIOS floppy path copes with an unaligned buffer
(`dsk_runcap` answers 0 and `dsk_xfer`'s `.bounce` stages one sector through
`dsk_secbuf`), and so does the file layer (`dskw_runmax`, SPEC.md 18.4.1). The
cost is one extra `int 13h`, ~400 ms on the XT, per 64KB page the transfer
crosses. That is the price of letting a DISK-class claim go unaligned, and it
is why the class exists: an alignment bump costs at most `MEM_GRAN` bytes,
while a bounce costs a disk revolution or two every time.

### 1.3 The kernel's own byte-sized claims

*(measured from the constants)*

| claim | bytes | claim at 1K | waste at 1K | at 512 | at 256 |
|---|---:|---:|---:|---:|---:|
| FAT window `MEM_P_FATW`, kern_big (`disk.inc:5791`, 9 sectors) | 4,608 | 5K | **512** | 0 | 0 |
| read-ahead `MEM_P_RAHB` (`disk.inc:7181`, 9 sectors, DMA head = whole) | 4,608 | 5K | **512** | 0 | 0 |
| dirw window `MEM_P_DIRW` (`disk.inc:7063`, n × 4.5K) | n × 4,608 | ⌈4.5n⌉K | 512 when n is odd | 0 | 0 |
| `VIEW_KB` per Disk window (`files.inc:1216`, holds 1,600) | 1,600 | 2K | 448 | 448 | **192** |
| assoc cache (`assoc.inc:1934`, sectors → ⌈n/2⌉K) | 512n | | 512 when n is odd | 0 | 0 |
| the compressor's read claim (`compress.inc:2039`) | U + 1 | ⌊U/1K⌋+1 | up to 1,023 | up to 511 | up to 255 |

The read-ahead's DMA head becomes 9 × 512. The downward page bump puts it at
`page_end − 4.5K`, which is still 512-aligned.

**The compressor is not a math bug, and it is not the old margin either.**
`cmz_kb` is `⌊U/1024⌋ + 1`, which is `⌈(U+1)/1024⌉`: room for **one byte past
U**. `cmz_uread` reads the whole file into the claim and then refuses unless
**exactly U** bytes arrived (`.rbad`, *"the hint said one thing and the read
another"*). With a claim of exactly U, a file *longer* than its hint would fill
the claim, report U and pass. The extra byte is what lets the over-long read be
seen. The in-place expansion margin it might be mistaken for is gone:
SPEC.md 20.13.7's raw tail made a buffer of exactly U enough. What was
really wanted is one byte, and the KB arithmetic turned that into up to
1,023 of them. Under a paragraph door it is `claim(U + 1)` with the reason
written next to it.

## 2. What finer claims would recover

### 2.1 Package regions (`kernel/loader.inc:1140-1198`)

`ld_alloc` rounds image + bss to **512** (`add 511 / and 0FE00h`) and then
`mem_bytes_kb_x` rounds it again to a KB. The 512 step is not a property of
the program. It is the loader's *read*: `roundup512(max(img+bss, fsize))`
(`loader.inc:712`), because the last sector of the file has to land inside the
claim. Every shipped package is packed (`PKGZ ?= lz4`), so `fsize` is less than
the image, and for nearly all of them `roundup512(fsize) ≤ img + bss`. The
loader can claim `roundup_G(max(img + bss, roundup512(fsize)))`: the read still
fits, and the claim is the program's own size rounded to the granule.

Over the 59 packages in `build/` *(measured)*:

| granule | total slack | mean per launch |
|---|---:|---:|
| 1 KB (today) | 30,373 | 515 |
| 512 | 16,037 | 272 |
| 256, with the loader change above | 7,077 | 120 |

Examples at 1K → 512: Calculator 1,004 → 492, Tracker 976 → 464, Task
Manager 907 → 395, Texpad 826 → 314, Note Pad 788 → 276, Browser 758 → 246.
A package already near a boundary gains nothing at 512: Paint 367, Sheet 299,
DOS 61.

**But a region is top-down, and that decides what 256 is worth for it.** A
DISK-class claim at 256 granularity needs its base aligned down to 512. Coming
down from the ceiling, that leaves the odd 256 *above* the region, between it
and the claim it hangs under. The loader's other option, claiming 512 and
shrinking once loaded, frees the same 256 in the same place. This is the
failure `tools/os88drv.py:155-165` records for drivers: *"the tail of a
top-down claim is walled in above the image. Measured on a machine wanting
every driver, 14,336 bytes stranded and the largest free run 375.0K ->
361.0K."* The 256-byte hole is real memory, but only a CPU-class claim of 256
bytes, or kern_big's compactor, can ever use it. kern_small has no compactor.

So at 256: a package region gains its ~150 bytes only when something small
fills the hole above it. That is plausible on kern_big after a compaction and
unreliable on kern_small.

### 2.2 Driver images (`kernel/driver.inc:3370`, `drv_bss` `:3596`)

Over 25 `.DRV` files, image + `DRV_H_BSSP` *(measured)*: 13,660 at 1K (mean
546), 7,004 at 512, 3,676 at 256. Examples: ETHER 1,008, FORMAT 901, HTTPFS
945, RAMDISK 924 at 1K. The same top-down caveat as section 2.1 applies at 256.

**The strip rule is the coupling to fix, not something to move in lockstep.**
`os88drv.py:166-168` strips trailing zeros down to the 1024 rung, and
`drv_bss` refuses (`.nofit`) when image + bss exceeds the claim sized from the
file. The tool's own comment says why stopping at a rung is now nearly free:
*"on a packed disk it costs almost nothing, because a run of zeros is what LZ4
is best at."* So the granule-proof rule is **do not strip below image + bss at
all**. The file then always covers its own bss, the claim covers the file at
any granule, `.nofit` cannot fire on a driver this tree built, and the tool
never has to learn the kernel's granule. It is still a `DRV_VER` bump, because
a driver built under the old rule can be refused by a claim rounded finer.

### 2.3 Modules: size them exactly, and the pads go away

The modules (CTRL, HIBER, WIN95, FILECP, FDLG) keep their bss in the claim's
rounding slack (MODULE-SELFCONTAIN-PLAN 3, with `%error` asserts at
`kernel.asm:8645, 8801, 8815, 8831, 8839`). That is not a need for 1 KB
granularity. A module is **size x = image + bss**, and the claim is sized from
the image alone and trusted to round up far enough. The asserts and the
**dead-zone pads** (`kernel.asm:8692`, `:8769`, the `DOSRMARK` arm's `align
1024` + `db 0` at `:8753`) exist to keep that trust true. HIBER is padded to
7,169 bytes so that its 594 bytes of bss get a fresh KB of slack.

The fix is MODULE-SELFCONTAIN-PLAN 3.2's named fallback, a per-module bss
size, read by `mod_need` before it claims: `claim(image + bss)`. Cost: a word
per `mod_tab` row (8 bytes on kern_big, 12 on kern_small) or, cheaper, a byte
in paragraphs (4 or 6 bytes). What it buys:

- every pad is deleted, including the bytes they add to the module images;
- the five fit-the-rounding asserts become a plain size check;
- `mod.inc:509-528`'s tail zeroing covers exactly the bss;
- modules become granule-proof, so they never need revisiting.

At 512 *(measured, slack = claim − image)*:

| module | image | bss | today, KB slack | exact claim at 512 | exact claim at 256 |
|---|---:|---:|---|---:|---:|
| CTRL (big) | 15,092 | 164 | 268, fits | 15,360 (same) | 15,360 (same) |
| HIBER (big, padded) | 7,169 | 594 | 1,023, fits | ~7,680 unpadded | ~7,680 |
| WIN95 (big) | 1,440 | 126 | 608, fits | 1,536 | 1,792 → 1,792* |
| FILECP (small) | 1,821 | 76 | 227, fits | 2,048 | 2,048 |
| FDLG (small) | 1,587 | 16 | 461, fits | 1,536 | 1,792* |

\* Modules are DISK class, so at 256 a top-down module image follows section 2.1's
caveat. HIBER's unpadded image is *estimated* at the size its pad was added to.

### 2.4 The heap's tail

`mem_floor_ax` rounds the floor to 512 and `mem_top` is whole KB. When the
floor sits on a half (today's kern_big: `HEAP 0x1820` = 96.5 KB), every
claim is a KB multiple measured from one end or the other, so **the odd 512
bytes can never be claimed**, whatever the layout. It moves with the kernel's
size. kern_small sits at 60.0 today and loses nothing, and the next size pass
can flip either build. SPEC.md 28.4.7 calls it *"dead memory"*. At 512 it is
ordinary heap. At 256 nothing more is recovered, because the floor is
512-aligned.

### 2.5 Package data claims: the 311 sites

The package-facing doors are where the bulk of claims are made: 172
`OSAPI_MEM_CLAIM`, 37 `REGROW`, 30 DMA/HI variants and 2 `HUGE`, in 118 files
and 52 directories. About 125 lines in ~60 files round a **byte count** up to
KB themselves (`add ax, 1023 / shr ax, 10`), and those are the claims that would
gain. The rest ask for whole-KB constants and gain nothing at any granule.

The concentrations are in **shared** source, which is what makes this cheap:

- `apps/os88partsbody.inc` (`:313`, `:1060`, `:1906`, `:1974`): every
  package with parts, i.e. every part claim, keep and lift. One file.
- `apps/cc/crt0.asm:1217` (the C overlay) and `apps/cc/os88.h`
  (`os88_mem_claim(kb)`): every C package. Two files.
- Frotz's story memory (`apps/frotz/zmem.inc:682`, `zpic.inc:738`), Word's
  document (`word.asm:8848`, `wddoc.inc:1065`), The Wire's catalog
  (`thewire.asm:2651`, `:2955`, `wrarc.inc:521`), DOS (`dosh.inc:1429`), the
  Browser's downloads (`brdownload.inc:769`), RealDMG (`realdmg.asm:1476`).

A byte-derived size has uniformly distributed rounding, so the expected waste
per such claim is half a granule: **512 at 1K, 256 at 512, 128 at 256**.

### 2.6 A session, added up (*estimated*)

kern_big, three windowed packages, two drivers, and eight byte-sized data
claims live (parts, a document, a C overlay, a story, a catalog):

| item | at 512 | further at 256 |
|---|---:|---:|
| the heap tail (when the floor is on a half) | 512 | 0 |
| FAT window + read-ahead | 1,024 | 0 |
| 3 package regions | ~730 | ~450, **only if** the holes above them get filled (section 2.1) |
| 2 drivers | ~540 | ~260, same condition |
| 8 data claims via the paragraph door | ~2,050 | ~1,020 |
| **total** | **~4.9 KB** | **~1–1.7 KB more** |

On kern_small, a 128 KB machine with two packages and four data claims gets
~1.5 KB at 512 against ~50 KB of heap.

**What 256 needs before it is turned on is a MEASUREMENT, not an argument**:
how often a 256-byte hole above a DISK image gets filled in real use, on each
kernel. `tools/heapmap.py` already reads holes off a running machine. A
`MEMGRAN=256` knob build plus a soak-row session script answers it.

## 3. What it would take

### 3.1 The kernel

1. **`MEM_GRAN`**, one constant in paragraphs (32 or 16). `mem_claim_1`
   and `mem_compact` take **paragraphs** in AX and round up themselves
   (`add ax, MEM_GRAN-1 / and al, -MEM_GRAN`, after a bound test that keeps
   the carry out). Guard 6b changes to `%if MEM_GRAN % 32` **unless**
   `MEM_ALIGN_CLASS` is built.
2. **The KB doors** (`osapi_mem_claim*`, `mmf_osapi_mem_claim`, regrow, and the
   internal `mem_claim_x` family kernel callers use with KB constants) shift by
   6 on the way in. Their contract becomes explicit, where today it is
   inferred from guard 6b: **a KB-door claim is DISK class, base 512-aligned.**
   That keeps every package that asserts or relies on alignment correct
   without a change.
3. **The byte rounders** `mem_bytes_kb(_x)` become bytes to paragraphs
   (`add 15` / `shr 4`; the `.cold` twin `dec / mov cl,4 / shr / inc`).
   `loader.inc:1217`'s `shl ax, cl` reads CL, so with CL = 4 it rebuilds
   `I_SIZE` from paragraphs **unchanged**. The two ownership fences using
   `I_SIZE` (`fsx.inc`, `instance.inc`) get a tighter bound, the safe
   direction. The 64 KB saturation needs a re-check: 0x1000 paragraphs << 4
   is 0 with CF set, as before.
4. **Loader**: claims `roundup_G(max(img + bss, roundup512(fsize)))` as DISK.
5. **Driver loader**: sized from the file under section 2.2's no-strip rule.
   `DRVR_KB` becomes `DRVR_PARA` (or half-KB), and `osapi_drv_classk_x`
   converts for its KB answer. The Control Panel's driver column is the place
   that gains a `.5` (section 5.3).
6. **Modules**: section 2.3's exact size; pads and fit-asserts deleted.
7. **Caches**: FAT window, read-ahead and dirw ask in paragraphs. Their steal
   thresholds (`DSK_FATW_MINK`) follow.
8. **`MEM_ALIGN_CLASS`**, assembled only when `MEM_GRAN` < 32:
   - a record bit `MC_DISK`, bit 14 of `MC_DMA`; bits 0–12 hold heads ≤
     0x1000 and bit 15 is `MC_DMA_HI`;
   - a fourth bump in `mem_claim_1`'s scan, `add dx,31 / and dl,0xE0` going up
     and `and dl,0xE0` going down, which is monotonic like the overlap and page
     bumps, so termination is unchanged;
   - the same test in `mem_hifit` and `mem_cp_dest`, so a compaction never
     un-aligns a DISK claim.
9. **Reporting**: `SK_HEAPP` appended to `osapi_sys_kb` (`SYSKB_SIZE` 20 →
   22, an append).
10. **kern_mega**: the `mem_hiclaims` ceiling (section 1.1).

### 3.2 The new public door

One cell, **`OSAPI_MEM_P`**, AH = verb, in paragraphs throughout. This is
SPEC.md 20.3's rare-cell shape, and one cell is cheaper than six:

```
AH = 0  claim      AX = paragraphs (1..0xFFFF), AL... -> see below
AH = 1  claim_hi
AH = 2  claim_dma  CX = HEAD in paragraphs
AH = 3  claim_dma_hi
AH = 4  regrow     DX = the claim
AH = 5  avail      out AX = largest run, BX = total, in PARAGRAPHS
flags (BL): bit 0 = MEMP_DISK, base 512-aligned
out: CF=0, DX = base, AX = paragraphs GRANTED (>= asked: use the slack)
```

The answer is **what was granted**, so a caller can use its slack exactly as
modules do today, but by asking rather than by trusting the rounding. On
`MEM_GRAN` = 512, `MEMP_DISK` costs nothing. It is honoured from the day the
door ships so that callers are already correct when the granule moves.

### 3.3 Tools, SDKs and the ABI

- **`PKG_FMT`: no bump.** The new cell is an append. Today's KB doors keep
  their contract (now written down: DISK class). `tools/abilock.py --update`
  records the fingerprint move.
- **`DRV_VER`: one bump** (section 2.2).
- **The three shared files** move to the paragraph door, and every package that
  includes them gains at its next build with no per-package work, the same way
  `os88ui_glyph`'s change reached sixteen carriers (CTRL-GLYPH-PLAN):
  `os88partsbody.inc` (its `zkb` part-table field stays KB; a part scratch is
  claimed as `zkb << 6` paragraphs, which is exact), `crt0.asm`, and `os88.h`
  (new `os88_mem_claim_p(paras)` and an `OS88_BYTES_PARA(n)` macro beside the
  KB calls). Then the per-package byte sites in section 2.5, one at a time, each
  deciding DISK or CPU.
- **SDK macros** in `os88api.inc`: `OS88_CLAIM_BYTES` / `OS88_CLAIM_BYTES_DISK`
  so a package site is one line.

### 3.4 SPEC.md first (CLAUDE.md: update it before the change)

These sections change:

- 50: "KB-granular" becomes "`MEM_GRAN`-granular, published in paragraphs".
- 50.2's record: `MC_DISK`.
- 50.3: the KB doors are DISK class, and the paragraph cell is added.
- 51.1.2: the strip rule.
- 28.4.7: the dead half-KB paragraph is withdrawn.
- 28.4.10's closing identity.
- 20.9: `SK_HEAPP`.
- 2.1.1: the rule restated as a property of DISK claims.
- The five places that say "every claim is whole KB": around SPEC.md 51604,
  9828, 36278 and 103502, plus section 2.1.1's own text at 362.

## 4. The kernel byte cost (*estimated*: measure the SPAN, never the rung)

| piece | kern_big | kern_small |
|---|---:|---:|
| `mem_claim_1` / `mem_compact` take paragraphs and round to `MEM_GRAN` | ~6 | ~6 |
| the KB doors shift (5–6 doors, mostly sharing one tail) | ~10–14 | ~8–12 |
| the byte rounders to paragraphs | ~0 | ~0 |
| loader / driver / cache size sites | ~10–20 | ~8–16 |
| modules: the bss byte per row, minus the pad logic | ~+4/−? | ~+6/−? |
| `OSAPI_MEM_P`: the cell + AH dispatch onto the existing bodies | ~40–60 | ~40–60 |
| `SK_HEAPP` | ~6 + 2 `.bss` | ~6 + 2 |
| **at `MEM_GRAN` = 512** | **~80–110** | **~75–100** |
| `MEM_ALIGN_CLASS` (assembled only below 512): scan bump, `mem_hifit`, `mem_cp_dest` | +30–50 | +15–25 (no compactor) |
| kern_mega `mem_hiclaims` ceiling | +3 | |

Nearly all of this is `.cold`, which is resident. Against it, at 512: ~3–5 KB
of heap back in an ordinary session (section 2.6). This is runtime heap, which
`kernsize` will not show, so the gates have to read the heap (section 6). In the
banner's terms: **N ≈ 100 resident bytes buys M ≈ 30–50× N of heap in normal
use.** At 256, the further ~40 bytes buy ~1–1.7 KB *if* the holes fill.

## 5. The Task Manager: exact, in the same width

### 5.1 What goes wrong if only the kernel changes

From the survey of `apps/taskmgr/taskmgr.asm`:

- `tm_hkb` (`:4397`) says *"the heap deals in whole KB, so this divides
  exactly"* and truncates per claim. Group totals (`tm_gkb`) then undercount,
  while `SK_CLAIM` truncates once per total. **IVT+BIOS + System + Programs
  ≠ RAM used**, which `tests/megatmmem.py` leg 2 asserts exactly.
- The process page shows a region as ⌈bytes/1024⌉ and the memory page as
  ⌊para/64⌋, so a 19.5K region reads 20K on one page and 19K on the other.
- **A sub-KB claim prints `-`** (`tm_kcol` and `tm_memcol_kb0` print 0 that
  way), and a group whose claims are all under 1K totals 0 and is
  **hidden** (`:5246`, `:5446`, `:5458`). That is SPEC.md 28.4.6's "memory
  on no row" defect again.
- `TM_LOWKB` = 2 (`:239`) bakes in the dead half-KB argument.
- RAM free = ⌊H⌋ − ⌊C⌋ but AVAIL = ⌊H − C⌋. They disagree by 1K whenever
  frac(H) < frac(C).

Accuracy is the point of the page: an inexact figure hides what is going on.
So the target is exact, not merely plausible.

### 5.2 The design

1. **Carry paragraphs, never KB.** `tm_hkb` stops dividing. `tm_gkb[]`
   becomes a paragraph sum; a word holds 1 MB in paragraphs, and a dword is
   only needed for a total that includes XMS, which is already separate. The
   process page's MEM is built from the same raw `CLS_PARA` records the
   snapshot already returns (region + data claims, by owner), not from
   `SSI_SIZE`/`SSI_KB`. So **both pages print the same number by
   construction**.
2. **A private fraction glyph set: `¼`, `½`, `¾`.** The system font is ASCII
   32–126 only (`kernel/font.inc:22`), and its RAM codes are private to the
   seam cut (`FONT_SEAM_A/B`). The Task Manager carries three 8×8 1bpp cells,
   24 bytes, and draws a figure as up to three opaque pieces: `font_run`
   of the digits, `OSAPI_GFX_BLIT1` of the fraction cell with its ground in
   the same pass (nothing drawn twice, SPEC.md 6.1's rule kept), and `font_run` of
   the `K`. A figure with no fraction is still one `font_run`. At 512 only `½`
   is ever reached. `¼`/`¾` are for 256 and cost 16 bytes to carry from the
   start.
3. **`tm_pfig`, four cells, exact to 99¾K:**

   | value | shows |
   |---|---|
   | 0.5K | `  ½K` (never `-`: a claim always has a row and a group never vanishes) |
   | 2.5K | ` 2½K` |
   | 19.5K | `19½K` |
   | 19.25K (at 256) | `19¼K` |
   | 100K–999K | `nnnK`, rounded to nearest: at most 0.5% off |
   | ≥ 1000K | the existing `d.dM` |

   Dropping the `K` was considered and is not needed: the fraction cell sits in
   the `K`'s place only for 100K+ values, which do not print a fraction.
4. **Totals that close: cumulative rounding**, the convention `SK_*` already
   uses (SPEC.md 20.9: *"rounded cumulatively"*). Each row prints the
   rounded running sum minus the previous rounded running sum, so the printed
   rows add to the printed total. Below 100K every row is exact anyway, so the
   rounding only acts on big rows. That restores 28.4.10's "SIZE sum + AVAIL =
   Ram total" and `megatmmem`'s identity exactly, and retires `TM_LOWKB`:
   IVT+BIOS becomes `1.5K + EBDA`, shown as `1½K` when there is no EBDA.
5. **The heap total** reads `SK_HEAPP`, and AVAIL comes from the paragraph
   door, so free and used come off one unit.

Cost: `tm_pfig`, the three-piece draw and 24 bytes of glyphs, ~80–110 bytes of
TASKMGR.O88. **Zero resident bytes.** The small build (SPEC.md 28.12) keeps
the process MEM column and the RAM/HEAP pair, so it takes the formatter and
the glyphs and nothing else. The draw is two more calls per fractional figure,
~1.5 ms on the XT, on a page that repaints numbers once a second in chunks.

**BUILT, with two departures** (SPEC.md 28.4.7 is the contract now):

- **Rows 100K-999K with a fraction keep it, and drop the `K`** - `100½` in
  four cells - instead of rounding to the nearest KB. Item 4's cumulative
  rounding cannot close the sums: `IVT+BIOS` is 1½K on every machine and
  System is over 100K on every kern_big, so with System printed as a whole
  KB the rows add to `n.5` against a whole-KB total whatever the rounding
  rule. Exact everywhere below 1000K needs no rounding at all, and every sum
  on both pages closes to the paragraph. The summary line's `nnn/nnnK` pairs
  are the one place a figure rounds (no cell for a fraction in three digits),
  and they round so the RAM and HEAP free halves are one number.
- **No `SK_HEAPP`.** The heap's size is derived: `MEMP_AVAIL`'s free total
  plus every non-cache claim, the claim snapshot and the avail call taken in
  one `pushf`/`cli` window so the two describe one moment, and checked
  against `SK_HEAP`'s whole KB (`MEMP_AVAIL` answers 0 inside a compaction).
  A paragraph `SK_HEAPP` would make it exact by construction instead.

The carried unit is a figure in two halves - paragraphs below 1MB, KB above
- rather than paragraphs alone, because a group's blocks above 1MB on
kern_mega or a store-backed kern_big can be megabytes and a word of
paragraphs stops at 1MB. Cost: `TASKMGR.O88` +423 bytes of image and +74 of
bss; the small arm +440 and +202, 192 of that the claim snapshot it reads now.

### 5.3 Other displays

- **Control Panel drivers column** (`ctrl.inc`, `(~34K)` from the build-time
  `drv_memk`): it has room, so it shows `(~34.5K)`. It is a compile-time
  figure, so this is a table change plus a digit in the formatter, in
  `CTRL.DRV`, which is not resident.
- Every package that prints `OSAPI_MEM_AVAIL` (Pixel, Sheet, Browser, isoTron,
  Win16, Redline) keeps getting floor-KB, which is still true and claimable.
- `tools/heapmap.py` already prints `%.1fK`.

## 6. Tests

**Existing rows that cover this** (run once in the final pass, per the Testing
section): `soak -k 'heap*' 'reg*' drvmove sndmove tmowner heapdrv megatmmem
small128 kernresident pins pinsrt trkcompact paint1small`. Of these,
`heapcheck`'s model (`tests/heapcheck.py:198-308`, `//64`) and
`tools/heapwhatif.py`'s `PARA = 64` constructor need their arithmetic made
paragraph-exact. `tests/facescan.py:186` keeps asserting face alignment, which
is now its DISK claim's contract.

**New gates, each broken on purpose first** (docs/WRITING-TESTS.md 1):

1. **`t_heapgran`** (fast, host-side):
   - every module's `image + bss` equals its claim's request;
   - `os88drv.py` leaves every driver's file ≥ image + bss;
   - `MEM_GRAN` is 32 or `MEM_ALIGN_CLASS` is built;
   - the KB doors' DISK contract appears in the SDK.

   Red when the strip goes back to a rung, or when a module pad is reintroduced.
2. **`heaptail`** (soak, MartyPC): boot kern_big with the floor on a half,
   claim until refused, and assert the free remainder is **0**. Red on today's
   kernel.
3. **`heapalign`** (soak): open a dozen packages in varying orders on a
   `MEMGRAN=256` build, read `mem_tab` through `os88sym`, and assert every
   `MC_DISK` base is 32-paragraph aligned. Red with the bump taken out of
   `mem_cp_dest`.
4. **`tmhalf`** (soak): a package holding a 512-byte claim shows `½K` on both
   Task Manager pages, its group is present, and IVT+BIOS + System +
   Programs = RAM used exactly.
5. **Driver no-strip A/B**: a `.DRV` stripped to the 1K rung is refused
   `.nofit` on the new kernel. That is the reason for `DRV_VER`.
6. **`gran256`** (soak, the measurement of section 2.6): a fixed session on both
   kernels at `MEMGRAN=256` and 512, reporting stranded hole bytes and the
   largest free run. It decides the default, and stays as the regression
   row once it has.
7. `t_buildmatrix` gets a `memgran256` row so the class code keeps assembling.

## 7. Recommendation

**Do it once, ship it at 512.** Five waves on one branch. The order puts every
contract change before the code that depends on it:

1. **Kernel core**: `MEM_GRAN` = 32, paragraph internals, the byte rounders,
   the heap tail, `SK_HEAPP`, the kern_mega ceiling, the KB doors' DISK
   contract written down. SPEC.md first.
2. **Exact sizes**:
   - the loader's `max(img+bss, roundup512(fsize))`;
   - modules sized as image + bss, pads deleted;
   - drivers on the no-strip rule, with the `DRV_VER` bump;
   - the caches in paragraphs;
   - the compressor's one-byte read sentinel spelt as one byte.
3. **The public door** `OSAPI_MEM_P`, and the three shared files moved onto it
   (parts, crt0, `os88.h`), so every package that includes them gains at its
   next build.
4. **The Task Manager**: paragraph sums, `tm_pfig`, the ¼½¾ cells,
   cumulative rounding, `TM_LOWKB` retired, and the Control Panel's `.5`.
   Land it **with wave 1** if that wave ships alone, so the totals never
   disagree on a shipped build.
5. **`MEM_ALIGN_CLASS` and the `MEMGRAN=256` knob**, with `gran256` measuring
   the holes. Flip the default only on that measurement.

Then the per-package byte sites (section 2.5), as each package is next touched. Each
one decides DISK or CPU, which turns the alignment those packages rely on from
an accident into a declaration.

Open questions for the owner:

- `OSAPI_MEM_P`'s verbs: is avail-in-paragraphs plus "granted" enough, or
  should it also answer `MEM_GRAN` so a package can see the granule? That
  costs ~3 bytes, and is useful for a caller that packs several things into one
  claim.
- 256 on kern_small: with no compactor, the holes of section 2.1 never close. Should
  kern_small keep `MEM_GRAN` = 512 even when kern_big moves? The constant can
  differ per build. The class code is the same.

## 8. What is not changed, and why

- **The KB doors stay**, for the binary compatibility of every `.o88` built
  so far. They are not the API packages should reach for any more, and the
  SDK says so.
- **`zkb` in the part table** stays KB: it is a disk-format field
  (`os88pkg.py`), and a scratch part is rarely byte-sized.
- **The 64KB DMA page rule** needs nothing. It is already paragraph
  arithmetic.

## 9. Revision record

Revision 1 (same day) recommended a kernel-internal 512 step with the
package doors left in KB, modules left on the KB, 256 refused, and
`cmz_kb` called a defect. The owner's review corrected four points, each now
in the body:

- KB doors alone mean the 311 package sites never benefit (section 2.5, section 3.2).
- 256 is a superset of 512 if alignment is asked for rather than inherited.
  It needs a class, not a refusal (section 1.2, section 3.1.8). The top-down hole (section 2.1) is
  what the measurement in section 2.6 has to settle.
- Modules use the slack because their claim is sized from the image, not
  because they need KB granularity (section 2.3).
- The Task Manager can carry its own fraction glyphs (section 5.2).

`cmz_kb` was re-read: it is a one-byte overrun sentinel for the whole-file
read, inflated to a KB by the arithmetic (section 1.3).

## 10. What was built

Owner's decisions on revision 2: build it, ship 512 first, `OSAPI_MEM_P`
reports the granule, kern_small does what is practical (it ships 512 like
kern_big), keep the resident cost near 80-150 bytes.

| wave | what | where |
|---|---|---|
| 1 | `MEM_GRAN` = 32; the KB doors shift once into `mem_claim_x.gran`, which rounds and refuses; `mem_claim_1`/`mem_compact` take paragraphs; kernel paragraph doors `mem_claimp_x/_hi_x/_dma_x`; kern_mega's `mem_hiclaims` rounds UP | SPEC.md 50.2.1 |
| 2 | package regions `roundup512(image+bss)` exactly; driver images to 512 (`DRVR_PARA`, `mem_bytes_gp_x`, `mem_regrowp` for KEEP, `os88drv.py`'s `DRV_RUNG`, `DRV_VER` 11 -> 12); FAT window, read-ahead, dirw, ASSOC.DAT cache exact; modules carry their bss as zeros in the file and claim image+bss (pads, fit-asserts and kern_small's zeroing deleted) | SPEC.md 50.2.1, 51.1.2, 2.8.1 |
| 3 | `OSAPI_MEM_P` (0x04E9): claim/claim_hi with the DMA head in CX and `MEMP_CPU`, regrow, avail with CX = the granule; C SDK `os88_mem_claim_p`/`_regrow_p`/`_largest_p`/`os88_mem_gran`; the C `.OVL` load; the parts standard end to end in paragraphs (The Wire's fence gains level 7 for the cell) | SPEC.md 50.3.7, 20.12.14, 92.2 |
| 4 | the Control Panel's Drivers column counts half-KB (`~10.5K`); the Task Manager (exact figures, fraction cells) - see its own commit | SPEC.md 31.6.2, 28.4 |
| 5 | the DISK class (`MC_DISK`, the scan's fourth bump, `mem_hifit`, `mem_cp_dest`) under `MEM_ALIGN_CLASS`, built by `MEMGRAN=16` only; gates `t_heapgran` (fast) and `memp` (soak) | SPEC.md 50.2.1.1 |

**Resident bytes**, measured against the base of this work (`kernsize`
sections): kern_big `.text` +6 `.cold` +130 = **+136**; kern_small `.text` +6
`.cold` +111 = **+117** (the module change took 22 back). kern_mega `.cold`
+99. The class code is not in any of them; `MEMGRAN=16` adds 136 to `.cold`.

**Measured on MartyPC** (5150, 360KB disks, kern_big): Calculator's region
5.5K (6K before), Note Pad's 19.5K (20K), the FAT window 4.5K (5K); every base
and size a whole number of 32 paragraphs; `memp`'s eleven checks pass and the
256-byte knob boots with every base aligned.

**Where it differs from the plan above:**

- **No `SK_HEAPP`**: `OSAPI_MEM_P`'s avail answers in paragraphs, so the
  heap's exact figures come from the door and the snapshot rather than a new
  `osapi_sys_kb` word.
- **Image claims round to 512 on every granule** (`mem_bytes_gp_x`, the
  driver KEEP), and `os88drv.py`'s rung is a constant `DRV_RUNG` = 512 rather
  than the granule: an image is a top-down DISK claim, and on a finer granule
  the difference would only be a hole above it (section 2.1's finding).
- **Modules took design (A)**, bss as zeros in the file, at zero resident
  bytes and minus kern_small's zeroing code - cheaper than section 2.3's word
  per row.
- **`OSAPI_MEM_P`'s verbs**: bit 0 the direction, bit 1 `MEMP_CPU`, so REGROW
  is 4 and AVAIL 5 (renumbered before release).
- **The compressor's claim is unchanged** (`cmz_kb`): it is a deliberate
  one-byte sentinel for the whole-file read, its KB word is shared with the
  Compress path's region sizing, and the claim lives for one operation inside
  an on-demand module. Spelling it as `claim(U + 1)` is worth doing the next
  time DISKOPS is touched.
- **A defect this work shipped and fixed**: wave 1's granule mask was `and
  al`, so every claim that was a multiple of 4KB was refused as zero for one
  commit; `memp` checks 8 and 9 are the gate, seen red with it put back. And
  wave 2 had broken `make mega` (a short jump out of range in `disk.inc`),
  fixed in the module wave.
- **The boot blob is full to the byte** (6,655 of 6,656): the driver KEEP
  rounding lost its carry test to fit. The word is the driver's own, not a
  disk's.

**The sweeps** (owner: "go for the rest of the unconverted packages and
drivers"). Every byte-sized claim in the tree is on `OSAPI_MEM_P` now:

- **Packages, first sweep**: Frotz, Word, Scribe, Note Pad, TeXPad,
  ArtfulType, The Wire, the Browser's download, RealDMG, Skies, Pixel,
  Excitebike, Desk Clock, MIDIRack, Dot Delirium, the C64/Apple II/RunCPM
  loaders.
- **Packages, second**: ModPlug, Tracker, XTMP3, the Video Player, WaveRack,
  WIN16, Pixel's editor and master, Scribe's pictures, and `os88ui.inc`'s
  save-under banks (so every package that includes it).
- **Drivers**: SOUND's staging pool and volume panel, ETHER's socket pool
  (16,896 bytes: 16.5K where the KB door took 17, and the Control Panel's
  figure with it), the RAM disk's tables, and the HDDTOOL, RAMPAGE and
  HTTPPAGE page images.

Each kept its fixed-KB claims, multi-KB grow steps and "largest free run"
policies on the KB doors, and the agents' reports name every one.

**`MEMP_CONV`** (CX bit 15) came out of the first soak pass: the parts'
carve, their fetches and the C overlay had used a DMA door with no head to
stay conventional on kern_mega, and `OSAPI_MEM_P`'s pool arm sent them above
1MB, where PiXEL's code part faulted as data (`pxmega`, `megapixel`,
`megapixelship`). With it all three pass. The same pass found `t_pins` blind
to `OSAPI_MEM_P` (270 claim sites seen, 321 now) and `t_wire` using Note
Pad, a level-7 package now, as its level-3 sample.

**The half cell is `.5`, not `½`** (owner, after the first build): the
three-pixel `½` was hard to read beside the bold ROM digits, so the cell is the
ROM font's baseline dot and a `5` condensed to five columns, in one 8×8 cell -
`96.5K`, `AVAIL 522.5`. Same width, same draw path; only the bitmap moved.
The quarters keep the stacked form, reachable only on a 256-byte granule.

**Still open:** the 256-byte question is a measurement (`gran256`, section 6
item 6): no package asks for `MEMP_CPU` yet, so on the knob build every claim
is still a 512 multiple and there are no holes to measure until one does.
