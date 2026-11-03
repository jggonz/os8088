# PART-PACKING-PLAN - a part should not start on a 512-byte sector

**ADOPTED - the 16-byte granule is the standard's only layout (SPEC.md
20.12.2, 20.12.2.1); section 8 is what the adoption measured and found.**
The study below is kept as it was written. The owner's ask: *"Each part
starts on a 512-byte sector inside an .O88. If this is still the case, we need
to see if we can fix that, so we're not wasting multiple KB inside each packed
parted .o88 with many parts. ... I don't care if we have to READ a few bytes we
discard - just that we are not storing a 513b part on 1024b of disk."*

Figures are marked **M** (measured, on the tree this branch was cut from,
`74d6926f`, `dos-windowed`) or **E** (estimated). `tools/os88partpad.py` is the
instrument and re-derives every M figure in sections 1 and 4 from a built tree:

```
python3 tools/os88partpad.py build/*.img          # every shipped disk
python3 tools/os88partpad.py --files build/kdos/DOS.O88 --cluster 1024
```

## 0. The answer in five lines

1. **It is still the case.** SPEC.md 20.12.2: every part starts on a 512-byte
   boundary of the file, and `os88pkg.py` fills the gap with `0xE5` (M: 41,937
   bytes across the 62 parted files on the seventeen shipped disks).
2. **The fix is a granule of 16 bytes, not of 1.** A part laid on a PARAGRAPH
   keeps the one property the 512 was for - a cluster-aligned read's head slack
   is a whole number of paragraphs, so a part is still a segment with offset 0
   and nothing is copied that was not copied before - and gives back all but
   ~7 bytes a part (M: 50 of the 51 clusters byte-packing would return).
3. **It costs the kernel nothing resident.** The kernel does not read a part
   table (SPEC.md 20.12.1). The standard is package code, so a package carries
   its own reader; the format marks itself with a second magic, `O88PARTP`, and
   no `PKG_FMT` bump is needed. **One** kernel-side reader exists - the DOS
   hibernate hand-off in `kernel/hiber.inc`, which is `HIBER.DRV`, an on-demand
   module (E: 30-40 module bytes, 0 resident).
4. **Prototyped and passing.** The standard (`apps/os88parts.inc`,
   `apps/os88partsbody.inc`, `apps/os88pself.inc`) and the packer
   (`tools/os88pkg.py`, `tools/os88parts.py`) take `-DOP_PARA`; the default
   build is **byte-identical** (M: every `.o88` and `.img` under `build/`).
   `tests/multiseg.py 360 --comp` - MSEG's seven parts, two compressed, a plain
   lazy one, scratch, OPT and XMS - **PASSES** on MartyPC with the paragraph
   layout (M, the one measurement). `DOS.O88` packs **50,781 -> 50,349 bytes**
   (M).
5. **Recommendation: adopt it as the one layout, after the DOS parts work
   lands** (section 6) - it touches `dos.asm`'s hand-off record, `kdxpack.inc`
   and `hiber.inc`, which that work is moving.

## 1. What the padding costs today (M)

Per file - every parted `.O88` in a built tree, with the bytes of `0xE5` it
carries, and the clusters it takes now / packed back to back / on 16 bytes:

| file | parts | bytes | pad | 512 B clus | 1 KB clus | 2 KB clus |
|---|---:|---:|---:|---|---|---|
| `1942.O88` (on demand) | 12 | 229,734 | 3,823 | 449 -> 442 | 225 -> 221 | 113 -> 111 |
| `PIXEL.O88` | 10 | 111,370 | 2,383 | 218 -> 213 | 109 -> 107 | 55 -> 54 |
| `WIN16.O88` | 7 | 64,162 | 2,090 | 126 -> 122 | 63 -> 61 | 32 -> 31 |
| `XTMP3.O88` | 4 | 97,096 | 1,377 | 190 -> 187 (188 at 16) | 95 -> 94 | 48 -> 47 |
| `SHEET.O88` | 2 | 88,629 | 993 | 174 -> 172 | 87 -> 86 | 44 -> 43 |
| `PXSTEIN.O88` | 3 | 46,004 | 837 | 90 -> 89 | 45 | 23 |
| `REALDMG.O88` | 3 | 52,883 | 641 | 104 -> 103 | 52 | 26 |
| `SCRIBE.O88` (on demand) | 2 | 43,198 | 498 | 85 -> 84 | 43 -> 42 | 22 -> 21 |
| `DOS.O88` | 4 | 50,781 | 450 | 100 -> 99 | 50 | 25 |
| `MIDIRACK.O88` | 1 | 30,451 | 434 | 60 -> 59 | 30 | 15 |
| `CWORD.O88` (on demand) | 1 | 31,921 | 417 | 63 -> 62 | 32 -> 31 | 16 |
| `BROWSER.O88` | 1 | 34,584 | 372 | 68 -> 67 | 34 | 17 |
| `LOOM.O88` (on demand) | 2 | 49,711 | 333 | 98 -> 97 | 49 | 25 |
| `SKIES.O88` | 2 | 44,581 | 270 | 88 -> 87 | 44 | 22 |
| `DRMARCO.O88` | 3 | 47,510 | 243 | 93 | 47 | 24 |
| `WIREFLET.O88` | 2 | 46,905 | 231 | 92 | 46 | 23 |
| `C64.O88` (on demand) | 2 | 39,393 | 227 | 77 | 39 | 20 |
| `APPLE2.O88` (on demand) | 2 | 36,233 | 207 | 71 | 36 | 18 |
| `WORD.O88` | 2 | 43,665 | 137 | 86 | 43 | 22 |
| `VIDEO.O88` | 1 | 47,326 | 88 | 93 | 47 | 24 |

Not measured: the packages whose disks a plain `make` does not build and that
were not in the tree read (WEAVE, RUNCPM, EXCITEBIKE, LEMMINGS, PACCMAN,
ISOTRON). The test fixtures show the shape at its worst: `MSEG.O88` carries
1,339 bytes of padding in 14,315 (28 -> 26 sectors).

**The waste is not "multiple KB a file", and it is real.** A part pads by 0 to
511 bytes, ~256 on average, and the last part is never padded - so a file
carries roughly `256 x (parts - 1) + 256` bytes. That is multiple KB only in
the files with many parts (1942 3.7 KB, PiXEL 2.3 KB, WIN16 2.0 KB), and it
costs a CLUSTER only when it carries the file across a boundary.

Per disk - the shipped seventeen (`$(SHIPIMGS)`):

| disk | cluster | parted files | padding | clusters back |
|---|---:|---:|---:|---:|
| `os8088.img` | 512 | 3 | 910 | **2** |
| `os8088-120.img` | 512 | 3 | 910 | **2** |
| `os8088-720.img` | 1 KB | 3 | 910 | 0 |
| `os8088-360.img` | 512 | 1 | 450 | **1** |
| `apps.img` | 512 | 12 | 9,959 | **19** (18 at 16 bytes) |
| `apps120.img` | 512 | 11 | 8,582 | **16** |
| `apps720.img` | 1 KB | 7 | 3,225 | **1** |
| `apps360.img` | 1 KB | 3 | 730 | 0 |
| `media360.img` | 1 KB | 2 | 1,811 | **1** |
| `office360.img` | 1 KB | 3 | 3,513 | **3** |
| `network360.img` | 1 KB | 1 | 372 | 0 |
| `games360.img` | 1 KB | 4 | 1,581 | 0 |
| `emu360.img` | 1 KB | 2 | 2,731 | **2** |
| `util360.img` | 1 KB | 0 | 0 | 0 |
| `more720.img` | 1 KB | 5 | 5,791 | **4** |
| `small360.img` | 1 KB | 1 | 231 | 0 |
| `small720.img` | 2 KB | 1 | 231 | 0 |
| **all** | | **62** | **41,937** | **51** (50 at 16 bytes) |

**The 360KB set - the tight one - gets back 7 KB** (`os8088-360` 0.5,
`media360` 1, `office360` 3, `emu360` 2) of 41,937 bytes it stores as `0xE5`.
The 1.44MB apps disk gets back 9.5 KB. `games360` and `apps360`, the two
tightest by free space, get back nothing at all: every file on them happens to
land under its boundary.

## 2. "Someone fixed that for something" - what it was, and why it stayed local

Nothing in the tree lays parts out unaligned. What exists is **four local
workarounds round the alignment**, each found and taken by the package that
was hurting:

- **One stream instead of two parts.** RunCPM's CCP was a row of its own and
  that made the file one sector longer than the three files it replaced, so it
  went INSIDE the module's LZ4 stream - *"no row and no padding"* (SPEC.md
  74, the RUNCPM carve). This is the one real fix, and it is local because it
  only works where two pieces are always loaded together: a part is the unit a
  lazy fetch, a drop and a claim operate on, and merging two parts merges those.
- **The worst-padding part goes LAST**, because `os88pkg.py` does not pad the
  final part: DrMarco's VGA front screen (*"413 bytes off the file"*),
  Excitebike's CGA splash, 1942's `REEF.C42` (*"what bring the file under a
  1,024-byte cluster, 233,469 bytes against 233,472"*). It is local because it
  saves ONE pad per file and costs the package a row order it has to keep in
  step with its code.
- **Measured and declined**: PiXEL tried the same reordering and found 271
  bytes and no cluster on any geometry (SPEC.md 106).
- **`op_self`'s head slack** (`apps/os88pself.inc`) already reads from the
  cluster below a part and skips forward, so it TOLERATES an unaligned start in
  principle - but it converts `off` with `shl 9`, so it is not a fix either.

The reason none of these generalised is that the alignment is not an accident
of the packer: SPEC.md 20.12.2 derives it from `dskw_read_at`'s cluster rule,
and the standard's whole address arithmetic - `op_seg`'s fast path, the
carve's one read, `op_unpack`'s two pointers - leans on "the slack is a
multiple of 512 and therefore of 16". The fix has to keep the second half of
that sentence and drop the first, which is what section 3 does.

## 3. The design

### 3.1 Why 16 and not 1

SPEC.md 20.12.2's argument needs exactly one thing of a part's file offset:
that `offset mod cluster` - the head slack a cluster-aligned read leaves - is
**a multiple of 16**, because `claim + (slack + off_i - start) / 16` is then an
exact segment for part *i* with no copy. 512 satisfies it; so does 16, and
nothing coarser is needed anywhere:

| what reads the offset | what it needs | 512 | 16 | 1 |
|---|---|---|---|---|
| the carve's read (`op_claim`, `op_read`) | a cluster-aligned start below the run | yes | yes | yes |
| `op_seg` fast path, plain eager part | slack + offset in whole paragraphs | yes | yes | **no - every plain part MOVES** |
| `op_unpack` (eager, compressed) | both pointers paragraph segments, offset 0 | yes | yes | no - pointers become seg:off |
| `op_fetch`, plain lazy part | moved onto its claim's base (20.12.7.4.4) | yes | yes | yes |
| `op_fetch` / `op_self`, compressed lazy | the stream at R + slack | yes | yes | yes |
| `OP_XMS` span | even byte counts | yes | yes | no |

**Byte packing would buy 1 cluster across all seventeen disks** (M: 51
against 50 - XTMP3 on the 512-byte disks) and costs a move of every plain
part, a far pointer in `op_unpack` and the end of `op_seg`'s fast path. A part
pads ~7.5 bytes on average at 16. **REFUSED**, on that one row.

**One stream for several parts** (RunCPM's shape) also removes the LZ4 stream
boundary, which byte packing does not. It is the right answer where two parts
are always loaded together, and that is a per-package decision, not a layout:
no change to the standard is needed for it and none is proposed.

### 3.2 The format, and who has to know

- **`off` counts PARAGRAPHS.** A word of them names 1 MB of file, against
  32 MB at 512; the largest parted file is 1942's 229,734 bytes. `os88pkg.py`
  refuses a part past it.
- **The table says which by its MAGIC**: `O88PARTP` where it was `O88PARTS`.
  Not the reserved byte at +9 - that is `OP_T_FMT`, the compression format,
  and eight sites in six files hand it straight to `OSAPI_DECOMP`, so a
  bit there would be a decoder refusing every stream. A magic costs no
  instruction anywhere, and a host tool that knows only the old one finds
  nothing and REFUSES rather than misreading - with one exception, below.
- **`PKG_FMT` does not move.** The kernel learns flags bit 2 and the link word
  (SPEC.md 20.12.1, 20.13.8) and neither changes meaning; a package's own
  reader is assembled into it, so an old package keeps its 512 table and its
  512 reader and a new one carries both halves of the new pair. The ABI lock's
  fingerprint is `apps/os88api.inc`, which is untouched.
- **The kernel's loader is unaffected** (M, by reading it): step 6 reads
  `roundup512(image)` - or `roundup512(link)` packed - into a region already
  sized for it, so the tail of the image's last sector holds a part's first
  bytes where it held `0xE5`, and step 7 zeroes the bss over them either way.
- **The image's own packing** (SPEC.md 20.13.8's fixed point) works unchanged
  at 16: every part moves by the same whole number of granules. A pack that
  saved no SECTOR was declined; at 16 it is declined only when it saves no
  paragraph, so more images will pack (E: small).

### 3.3 What the prototype changes (built, under `-DOP_PARA`)

`apps/os88parts.inc` defines `OP_GSH` (9, or 4 under `OP_PARA`), `OP_GRAN`,
`OP_GPSH` and `OP_RUNMAX`, and the magic. `apps/os88partsbody.inc`: every
`shl 9`/`shr 7` pair that turns `off` into a byte offset becomes
`OP_GSH`/`16 - OP_GSH` (`op_size`, `op_claim`, `op_xload`, `op_lin`,
`op_lazyp`, `op_fetch`); the two roundups that step `op_unpack`'s pointers and
`op_seg`'s walk become `OP_GRAN`; the granule->paragraph `shl 5`s compile out;
and **R, the compressed carve's lift, is rounded up to a SECTOR** - in the
512 build it was one by construction, and it is where a disk read lands
(CLAUDE.md, every disk-visible base). `op_zrpara` (a lazy fetch's R) was
already rounded to a sector on its own and is untouched. `os88pself.inc`
converts with `OP_GSH`. `tools/os88pkg.py` finds either magic and lays out at
the granule it names; `tools/os88parts.py` reads either.

What it buys besides disk: **the carve's CLAIM shrinks too**. The unpacked
layout steps by `roundup16(len)` where it stepped by `roundup512(len)`, so a
table of *n* eager parts claims up to 496 x *n* bytes less heap (E; it is the
same arithmetic as the disk side).

### 3.4 The readers outside the standard, and their cost

| reader | where | change | cost |
|---|---|---|---|
| the standard | `os88parts.inc`, `os88partsbody.inc`, `os88pself.inc` | **built** | M: DOS's loader **-11 bytes** (2,809 -> 2,798), part 0 +2 (54,829 -> 54,831); SHEET, PIXEL, SKIES, DRMARIO, BROWSER, VIDEO, MIDIRACK, WIREFLEET **+0**; XTMP3 and PXSTEIN **-11** |
| private row readers | `apps/realdmg/realdmg.asm` (1 site), `rwaot.inc` (2), `apps/midirack/midirack.asm`, `apps/excitebike/video.inc`, `apps/skies/csload.asm` | the same `shl 9`/`shr 7` -> `OP_GSH` | E: 0 bytes each (same encodings) |
| DOS's hand-off record | `apps/dos/dos.asm` posts `KDH_POFF`, `KDH_COFF`, `KDL_L3OFF` straight off `OP_R_OFF` | nothing in DOS; the record's UNIT changes | 0 |
| kern_mega's low kern_dos | `kerndos/kdxpack.inc` `kd_tolow` skips `L3OFF` SECTORS into the chain | skip `L3OFF >> 5`, read `ceil((head + ZB) / 512)`, stream at `head` into the window | E: ~15 bytes of kern_dos (package side, part 2) |
| **the hibernate hand-off** | `kernel/hiber.inc` - the one kernel reader. Extents from `KDH_COFF` sectors, `KDS_CDELTA = (POFF - COFF) << 5`, the stub expands the core from `KDS_TSEG:0` | skip `COFF >> 5`, count from `head + (POFF - COFF) x 16 + PLEN`, `KDS_CDELTA = POFF - COFF + head`, the core from `KDS_TSEG + head` (one new staged word) | E: 30-40 bytes of **`HIBER.DRV`, an on-demand module: 0 resident**. The record's magic goes `KDH1 -> KDH2`, so a DOS.O88 and a HIBER.DRV from different trees refuse the hand-off rather than misread it |
| host tools | `tools/gbaot.py` (**finds `O88PARTS` and writes at `at + 8` without checking - `-1 + 8` on a paragraph table is SILENT CORRUPTION**), `tools/os88wire.py`, `tests/rehome.py`, `tests/mseglzslack.py`, `tests/kdpart.py`, `tests/multiseg.py` (asserts `op_slack` 512 on a 360 disk - it is a multiple of 16 now) | `os88parts.gran()` / `os88pkg.PART_ALIGN` | host only |
| the C SDK | `apps/cc/crt0.asm` includes the standard; `apps/cc/Makefile.inc` | `-DOP_PARA` | 0 |

**The kernel's resident cost is 0 bytes on every build.** `kern_small` has no
hibernate, so its whole cost is the package side.

### 3.5 Time

The same `int 13h` calls: the carve is still one read per 32 KB and a lazy
part one read, and every run is the same length or shorter (E, by
construction). Two things move:

- **A plain lazy part on a 512-byte-cluster disk is now copied down** onto its
  claim's base (SPEC.md 20.12.7.4.4), where at 512 its slack was zero there.
  On 1 KB clusters - every 360 KB and 720 KB disk - it already was. E:
  `rep movsw` at ~17 clocks a word is ~8.5 clocks a byte, so a 30 KB part is
  ~55 ms on a 4.77 MHz 8088, against ~400 ms for the read it follows.
- **A fetch may read one sector more** when a part's head slack plus its
  length crosses a cluster the aligned one did not - at most one cluster a
  fetch, and the read's call count is unchanged.

No launch was timed: the one MartyPC measurement went to correctness (section
4), and nothing in this design adds a disk call for MartyPC to price.

## 4. The prototype, measured

**The default build is byte-identical** (M): with the standard and the packer
changed, `make` rebuilt every parted package and `md5sum` matched all 77
`.o88`/`.img` files under `build/` against the tree before; the fast tier
passed (68/68).

**`tests/multiseg.py 360 --comp` PASSES on the paragraph layout** (M,
MartyPC `os8088_5150_cga_gla`): `MSEG 7/7 OK` - parts 0 and 2 compressed and
expanded by `op_unpack` with both pointers stepping by paragraphs, part 1
plain and moved down between them, two scratch parts, the `OP_XMS` part
falling back into the carve on an 8088, and part 6 a plain LAZY part at file
offset 7,760, whose head slack on a 1 KB-cluster disk is **592 bytes - not a
multiple of 512**, fetched and moved down. The fixture: `msegz.o88` 11,755 ->
**10,811** bytes, `msegz360.img` 22 -> **20** clusters; `mseg.o88` 14,315 ->
13,003. Built by hand with `-DOP_PARA` into `build/para/`, put in place for
the run, and rebuilt from the tree after it.

**`DOS.O88`** (M, `build/para/kdos/DOS.O88`, `dosload.bin` and `dosp.bin`
assembled `-DOP_PARA`, the other three parts unchanged): **50,781 -> 50,349
bytes**, padding 450 -> 25, **100 -> 99 sectors** on the 512-byte-cluster
system disks (`os8088.img`, `os8088-120.img`, `os8088-360.img`) and 50 -> 50
clusters on the 1 KB ones. It is a SIZE artefact: it would not reach
`kern_dos` through a hibernate until section 3.4's `hiber.inc` row is built,
and no row was run on it.

## 5. Alternatives judged

| option | disk back (M) | cost | verdict |
|---|---:|---|---|
| keep 512, order rows so the worst pad is last | 1 pad a file | a row order per package | what four packages did; does not scale |
| **16-byte granule** | **50 clusters** | the standard's unit, 6 private sites, one module routine | **recommended** |
| 1-byte granule | 51 clusters | every plain part moves; seg:off pointers in `op_unpack`; `op_seg` loses its fast path | refused: one cluster |
| `off` stays sectors + a byte-in-sector field | as 1-byte | the row grows 8 -> 10 bytes (`OP_ROW` is mirrored and stepped everywhere) or steals `zkb`, which already has four meanings | refused |
| several parts in one stream | case by case, and the stream boundary too | merges the unit a lazy fetch, drop and claim work on | per package, as RunCPM did; not a format change |

## 6. Recommendation

**Adopt the 16-byte granule as the standard's only layout** - flip the
default rather than ship a switch, since every package is rebuilt from the
tree and an old file keeps its own reader - in one change carrying the six
private reader sites (five files), `kdxpack.inc`, `hiber.inc` with the `KDH2` record, the host
tools and SPEC.md 20.12.2, 20.12.3 and 20.12.7.1. Keep the host tools able
to READ `O88PARTS` for files built before it. Add a fast-tier gate that fails
on `OP_R_OFF` converted with a literal shift outside the standard, because
the private reader sites are exactly how a missed site would ship: a package
that assembles cleanly and reads the wrong sector.

**Take it after the DOS parts work lands**, not beside it: three of its rows
are DOS's hand-off (`dos.asm`'s record, `kdxpack.inc`, `hiber.inc`), and
DOS-PARTS-PLAN is moving exactly those.

What it buys, honestly: **7 KB across the nine 360 KB disks, 9.5 KB on the
1.44 MB apps disk, 51 clusters over the shipped seventeen** - and 3.7 KB on
1942's disk and the carve's claims smaller by the same arithmetic. It is not
"multiple KB inside each file" except in the three files with seven or more
parts; it is a format with no resident cost that stops a part costing up to
511 bytes for being a part, which is what makes "split this into a part" a
free decision for every package after it.

### 6.1 The rows an adoption needs

Run once, as the deduplicated union, after the last batch (CLAUDE.md,
Testing):

- the standard: `multiseg`, `mseg360`, `msegz`, `msegz360`, `msegw`,
  `msegw360`, `msegwz`, `msegwz360`, `msegnomem`, `mseglazy`, `mseglzslack`,
  `msegplslack`, `msegxms`, `rehome`, `rehomemove`, `rehomemove360`,
  `rehomeabort`, `pkg`, `pkgrun`, `pkgbig`, `pkgfmt`, `pkgmax`, `pkgread`,
  `pkgmove-*`
- the C SDK: `c64part`, `apple2part`
- DOS and its hand-off: `kdpart`, `dos*`, `kd*`, `megadosp`, `megadosroom`,
  `hibernate`, `hibernatedrv`, `hibernatem`, `win16hiber`
- the private readers: `rdg*`, `midirack*`, `excitebikeassets`,
  `excitebikeload`, `excitebikefront`, `skies`, `drmariofront`, `pxsdisk`,
  `xtmp3move`, `sheetmove`, `vidplay`

## 7. Files on this branch

- `tools/os88partpad.py` - the measurement (new, read-only).
- `apps/os88parts.inc`, `apps/os88partsbody.inc`, `apps/os88pself.inc` - the
  standard, switchable by `-DOP_PARA`; byte-identical without it.
- `tools/os88pkg.py`, `tools/os88parts.py` - the packer and the host reader,
  for either magic.

## 8. The adoption, measured (M, on `dos-windowed` at `ac4eda02` + this work)

The owner adopted section 6's recommendation as the ONLY layout - no switch
left behind - in one change: the standard, the packer and host readers, every
private reader, `kdxpack.inc`, `HIBER.DRV`'s hand-off (`KDH1` -> `KDH2`),
`gbaot.py`, `os88wire.py`, the tests, and a fast-tier gate. Every figure here
was taken at ONE build number (the "before" tree built at the same commit
count, `OS88_BUILDNUM` pinned where a later commit had moved it), so no
difference below is the About box's.

### 8.1 Disk - the seventeen shipped images

| disk | cluster | parted files | their bytes before -> after | saved | free clusters before -> after | back |
|---|---:|---:|---|---:|---|---:|
| `os8088.img` | 512 | 4 | 173,929 -> 171,545 | 2,384 | 1557 -> 1561 | **4** |
| `os8088-120.img` | 512 | 4 | 173,929 -> 171,545 | 2,384 | 1081 -> 1085 | **4** |
| `os8088-720.img` | 1024 | 4 | 173,929 -> 171,545 | 2,384 | 52 -> 54 | **2** |
| `os8088-360.img` | 512 | 1 | 52,302 -> 50,910 | 1,392 | 30 -> 32 | **2** |
| `apps.img` | 512 | 12 | 713,061 -> 703,221 | 9,840 | 110 -> 129 | **19** |
| `apps120.img` | 512 | 11 | 615,958 -> 607,254 | 8,704 | 121 -> 138 | **17** |
| `apps720.img` | 1024 | 7 | 339,009 -> 335,969 | 3,040 | 24 -> 26 | **2** |
| `apps360.img` | 1024 | 3 | 127,522 -> 126,258 | 1,264 | 27 -> 28 | **1** |
| `media360.img` | 1024 | 2 | 127,554 -> 126,018 | 1,536 | 24 -> 25 | **1** |
| `office360.img` | 1024 | 3 | 244,186 -> 240,666 | 3,520 | 18 -> 23 | **5** |
| `network360.img` | 1024 | 1 | 34,591 -> 34,367 | 224 | 253 -> 253 | 0 |
| `games360.img` | 1024 | 4 | 185,512 -> 183,880 | 1,632 | 66 -> 67 | **1** |
| `emu360.img` | 1024 | 2 | 117,557 -> 114,693 | 2,864 | 209 -> 212 | **3** |
| `util360.img` | 1024 | 1 | 39,198 -> 38,910 | 288 | 271 -> 272 | **1** |
| `more720.img` | 1024 | 5 | 307,400 -> 301,336 | 6,064 | 38 -> 44 | **6** |
| `small360.img` | 1024 | 1 | 46,905 -> 46,681 | 224 | 16 -> 16 | 0 |
| `small720.img` | 2048 | 1 | 46,905 -> 46,681 | 224 | 177 -> 177 | 0 |
| **all seventeen** | | **62** | | **47,968** | | **68** |
| **the nine 360KB** | | | | **12,944** | | **14 (13 KB)** |

More than section 1's 51 because the tree moved on under the study: DOS.O88
carries five parts now (the network and HELP parts), and the free-cluster
column is the whole disk, so it includes `HIBER.DRV`'s +24 file bytes on the
four system disks (which cost no cluster). Per file, the largest:
`1942.O88` -3,680 (on demand), `PIXEL.O88` -2,640, `WIN16.O88` -2,368,
`DOS.O88` -1,392, `XTMP3.O88` -1,136, `SCRIBE.O88` -784, `PXSTEIN.O88` -688,
`SKIES.O88` -560, `REALDMG.O88` -496, `VIDEO.O88` -480, `SHEET.O88` -464,
`WORD.O88` -416, `MIDIRACK.O88` -400. `tools/os88partpad.py` over the new
disks reports 2,084 bytes of padding left in 97 parted files (the mega and
test images included), against 47,942 in 66 before.

### 8.2 Bytes

- **Kernel resident: 0 on every build.** `build/kernel.bin` is byte-identical
  at one build number; `kern_small` `KERN_SIZE` 59,904 before and after;
  `make budget-check` passes all four kernels. The hand-off code is `.modh`.
- **`HIBER.DRV`: image 7,722 -> 7,754 (+32), file 6,142 -> 6,166** - the
  paragraph walk, `KDS_CHEAD` staged, and the stub's core segment sum. An
  on-demand module (SPEC.md 2.8), so none of it is resident.
- **Package images** (the bytes a launch reads and holds):

  | image | before -> after | |
  |---|---|---:|
  | `dosload`, `csload` (SKIES), `pxstein` loader, `scload`, `wdload`, `xtmp3` | -15 each | the asop total's `mov cl, 5 / shl` gone with the granule |
  | `dosp` (DOS's part 0) | 50,531 -> 50,533 | +2 |
  | `midirack` | 37,470 -> 37,496 | **+26**: `mra_load` reads at the sector below (8.3) |
  | `realdmg` | 38,050 -> 38,054 | **+4**: `rw_jpart`, the same (8.3) |
  | 1942, BROWSER, DRMARCO, PIXEL, REDLINE, SHEET, SKIES' program, VIDEO, WIN16, WIREFLET | +0 | same encodings, magic byte only |

- **The default build's non-parted outputs are unchanged**: built at the
  baseline's build number, every `.bin`, `.drv` and `.o88` that is not a
  parted package (or a part of one) is byte-identical - except `SYSCHECK`,
  whose generated manifest carries `HIBER.DRV`'s size and checksum.

### 8.3 What the study got wrong, and the reader it missed

- **"Private row readers: the same shift -> `OP_GSH`, E: 0 bytes each" was
  half true.** Four of the six only convert `off` to a byte offset and then
  read from the CLUSTER at or below (rwaot.inc's two, Excitebike, Clear
  Skies' program), so for them it is the shift. Two - **MIDIRack's
  `mra_load` and RealDMG's `rw_jpart`** - read with `OSAPI_FILE_READ_AT` at
  the part's own SECTOR (READ_AT takes sectors on kern_big, SPEC.md 18.4.4.2),
  which a paragraph part is not on: converted with the shift alone they would
  have been refused `FERR_NAME` on every machine. Each now starts at the
  sector at or below and steps over the head, and R is cut as
  `roundup512(len - packed - head)` so the claim is no bigger than before.
- **`op_load`'s file-operation total** (`op_asop`, SPEC.md 12.8.10) shifted
  `op_secs` by 5 as sectors -> paragraphs; at a paragraph granule that is 32
  times the bar's total. The prototype missed it because the bar clamps.
- **`os88pkg.py`'s OP_XMS span bound** compared granules with 128; it is 64KB
  of granules now.
- **`tests/multiseg.py`'s slack assertion** is derived from the table rather
  than written down as 512, and still refuses a zero slack on the 360KB disk.

### 8.4 Time (MartyPC, `os8088_5150_cga`'s GLaBIOS twin, guest cycles)

| launch | before | after |
|---|---:|---:|
| `A:\APPS\DOS.O88` off `os8088-360.img` | 8.702 s, 8.669 s | 8.419 s, 8.306 s |
| `B:\SHEET.O88` off `office360.img` | 20.303 s | 20.324 s |

DOS.O88's launch is ~0.32 s (3.7%) shorter - it is three sectors shorter on
a one-sector-cluster disk; SHEET's is unchanged to the noise. The click-to-
window span is read off the guest's cycle counter around one `os88ui.open`.

### 8.5 The rows the adoption needs

Section 6.1's list, unchanged, run once as the deduplicated union after the
last batch: `multiseg`, `mseg*`, `rehome*`, `pkg*`, `c64part`, `apple2part`,
`kdpart`, `dos*`, `kd*`, `megadosp`, `megadosroom`, `hibernate*`,
`win16hiber`, `rdg*`, `midirack*`, `excitebike*`, `skies`, `drmariofront`,
`pxsdisk`, `xtmp3move`, `sheetmove`, `vidplay` - and `partoff`, the new fast
row. `kdpart` is host-only and passes; the rest were not run in this change.
