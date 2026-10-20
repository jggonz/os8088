# Large LZ4 — decompression past 64KB, in one stream

> **STATUS: THE CORE HAS LANDED, AND TWO OPTIONS ARE DEFERRED BY DECISION.**
> The kernel decoder, the published cell, the parts carve and the host
> encoders all cross 64KB now. SPEC.md is the contract for what shipped:
> §20.13.3, §20.14.5.2, §20.14.5.3 and §20.12.11. This file keeps four
> things:
>
> - **why** one stream and not split volumes (§2);
> - the **consumers** that were bent by the old limits (§3) — two converted,
>   the rest follow-ups;
> - the **two options the owner decided not to take now** (§4.1 and §4.2),
>   costed and explained, so whoever revisits them starts from the arithmetic;
> - two things considered and **not recommended** (§4.3 and §4.4).
>
> Figures marked *estimate* were not prototyped. Every other figure was
> measured by assembling the change.

## 1. What landed, and what it cost

Three walls were left once SPEC.md §20.14.5 / §20.14.5.1 had made the output
and the LZB input cross 64KB:

| wall | fix | cost (measured) | where |
|---|---|---:|---|
| an LZ4 stream that **packs** to 64KB or more was refused | literal runs of 16KB or more are copied in 16KB pieces, with `lz_at` sliding `DS` between them | **+37 `.cold`**, both kernels | SPEC.md §20.14.5.2 |
| `OSAPI_DECOMP` took a 16-bit input length | `AL` bit 7 (`OSAPI_LZ_BIG`): `AH` is the high byte. An older kernel refuses the bit as an unknown format | **+6 `.cold`**, both kernels | SPEC.md §20.13.3 |
| a parted package's eager **carve** was under 128 sectors | `op_cap` counts paragraphs, and `op_want`/`op_bend` are 32 bits. The bound is `OP_SECMAX` = 1,920 sectors | **+14 / −6 / +6** bytes a package (plain / `OP_COMP` / `OP_COMP`+`OP_LAZY`) | SPEC.md §20.12.11 |
| *and a latent bug:* both host encoders could write a match of 64KB or more, which the kernel cannot count | both encoders cap at `LZ_MAXLEN`, and both host **decoders** refuse past it, as the kernel does | **0 kernel bytes** | SPEC.md §20.14.5.3 |

**The kernel total is +43 bytes of `.cold`.** kern_big goes 41,877 → 41,920
(64 bytes left in the rung), and kern_small 25,466 → 25,509 (91 left). No rung
is crossed, no slot is added, and no byte of `.text`, `.bss` or `.lowbss` is
spent. The LZ4 hot path got *shorter*: the literal length is read inline, so
it executes **1.9% fewer instructions on BEVERLY.MOD and 2.5% fewer on 60KB of
text**. That is a count, not a timer; a field timing on the 5150 is still owed
(PERFORMANCE.md rule 4).

The parted packages that ship today, rebuilt: `dosload` 2,187 → 2,193,
`csload` 2,256 → 2,262, `pxstein` 2,895 → 2,900, `wdload` 1,436 → 1,430.

**The gates added:**

| gate | what it shows | negative control |
|---|---|---|
| `msegw`, `msegw360`, `msegwz`, `msegwz360` | MSEG's carve widened by `tests/multiseg/mkwide.py` to 179 sectors (146 packed with compression), spanning 92KB of claim; every per-part proof unchanged on both geometries, plain and compressed | the old loader refuses the same file at launch |
| `lzmod-lz4big` | a 176,085-byte module packing to 92,508 in LZ4, read through `OSAPI_FILE_READ` and compared byte for byte; its noise is one ~30KB literal run, so the 16KB pieces run on the machine | the old decoder answers `FERR_IO` |
| `t_lzfmt`'s `lengths()` | a 70KB run of one byte round-trips, and both host decoders refuse a hand-built 70,000-byte match | fails with the cap taken out |
| `t_lzfmt`'s mirrors | `OP_SECMAX` (parts include ↔ packer) and `LZ_BIG` (kernel ↔ SDK) | — |

Before any of it landed, the decoder was also checked off the machine. A
Unicorn-hosted harness ran `kernel/lz.inc` itself, in place, against
`os88lz.py`, on text packing to 75–246KB, on mid-stream noise of 16–65KB at
varied offsets, and on BEVERLY.MOD doubled. 200 randomly corrupted 150KB
streams wrote nothing past the declared length.

## 2. Why one stream and not split volumes

Splitting a stream into blocks that each fit a segment is the workaround every
consumer reached for, and the tree has measured its cost twice:

- **BEVERLY.MOD in two 61,440-byte blocks: +18,766 bytes, 44% of the whole
  win** (docs/plans/O88-COMPRESSION-PLAN.md 13.4). A MOD's sample data matches
  back tens of KB into itself, and a block boundary throws that history away.
- **The kernel image in two LZ4 blocks: +1,076 bytes, 2 sectors**
  (`tools/os88kz.py`). Machine code matches locally, so here it is small (§4.3).

A split pays in ratio on exactly the data big enough to need one. Crossing the
boundary in the decoder cost 43 bytes, paid once.

## 3. Consumers bent by the old limits

Each of these was shaped by a wall that is now gone. Each section's reasoning
was right when it was written, and SPEC.md §20.12.11 names the four that cite
the carve.

**Converted — the two art streams.** Both are `OP_COMP | OP_LAZY` rows now:

| | before | after |
|---|---|---|
| **Clear Skies** part 1, title bands (10,480 → 4,487) | `csart.py --stream` packed them; `csl_art` fetched the stream, claimed again and decoded through `OSAPI_DECOMP` | `csart.py --raw`, `os88pkg.py` packs, `csl_art` is `op_fetch` + `op_seg`. Loader image 2,262 → 2,190 |
| **Pixelstein** part 4, art masters (37,440 → 8,890) | `pxsart.py --stream` packed them; `pxl_art` the same as `csl_art` | the same shape; loader image 2,900 → 2,811 |

**Both stayed LAZY, and this table originally said "eager".** An eager row
fits the carve since §20.12.11, but the carve is all or nothing. An eager art
row makes the art a reason to refuse the launch: Clear Skies would no longer
fly with its plainer title page on a machine short of 11KB (§88.10.4.1), and a
256KB Pixelstein machine that plays Flat would carry 37KB of masters it never
draws. Lazy keeps every refusal survivable and still deletes both private
expanders and both private pack paths. **The session pays nothing**: the
fetch's claim (R plus the read) comes out at the old exact sizes, 11KB and
37KB.

**The conversion found a defect in the parts standard**, the first time any
package fetched a compressed lazy row across a 64KB physical page.
`op_zrpara` rounded R to a paragraph rather than a sector, so the read could
start mid-sector and straddle a page: int 13h error 09h, `FERR_IO` on
Pixelstein's masters. R is a whole number of sectors now, and `op_lazykb`
prices a compressed fetch as R plus the read, so `op_fetch`, `op_lazyok` and
a package's own precheck agree. SPEC.md §88.10.4.1 has the account.

**Not converted — follow-ups:**

| consumer | bent how | now possible |
|---|---|---|
| **Pixelstein** part 3, levels (9,831 bytes) | LAZY and stored **plain** with a hand RLE, because eager it made 131 sectors (§97.9) | an eager row, `OP_COMP` if it pays — eager is right here, every launch needs the floors |
| **DOS box** part 2, `kern_dos` (~29KB) | LAZY because the eager pair would be refused (§96.44.4.1) | nothing: the row is never fetched anyway, so it stays lazy for its other reason |
| **Word** part 1 | its origin is capped so the carve stays under 65,024 (§68.10) | the cap is gone; the layout can stay |
| **Video player** resident blocks | a block packing past 61,440 (`BLK_PACKED_MAX`) is written **stored** (`os88vid.py`) | the player can pass `OSAPI_LZ_BIG`; package bytes not measured |

The comments in `csload.asm`, `pxstein.asm` and `csart.py` also give *"a lazy
row cannot be OP_COMP"* as a reason. That was already untrue before this work:
SPEC.md §20.12.7.4 lifted it.

## 4. Options

### 4.1 DEFERRED — a single part past 65,024 bytes (estimate +60–90 package bytes)

**What the limit is.** One row of a part table describes at most 65,024 bytes
(`OP_PARTMAX`, 127 sectors). Its `len` and `zkb` are words, and `op_size`
rounds `len` up to a whole sector in a word and refuses the carry. The packer
says so at pack time.

**What it restricts.** Less than it looks:

- **A PLAIN asset bigger than that is two adjacent rows, at no cost.** Rows sit
  back to back at `roundup512(len)`, and 65,024 is a whole number of sectors,
  so the second row begins exactly where the first ends. The package treats
  the pair as one block from the first row's segment, and since §20.12.11 the
  carve holding them may pass 64KB.
- **A COMPRESSED asset bigger than that pays a stream boundary per row.** Each
  row is its own stream, so a match cannot reach back across the boundary.
  That is §2's split cost on a small scale. For data that matches locally
  (graphics, levels, code) it is nearly nothing; for a sample bank it is the
  BEVERLY.MOD kind of loss.
- A **SEGMENT** part (code you far-call) cannot usefully pass 64KB anyway.

**What lifting it takes.** The high bits of `len` and `zkb` need somewhere to
live: `kind` uses one bit of eight and `flags` five. Old tables have zeros
there, so they read unchanged. Then:

- `op_size`, `op_lazykb`, `op_unpack` and `op_fetch` take 32-bit lengths;
- `op_unpack` and `op_fetch` call the cell with `OSAPI_LZ_BIG`;
- `os88pkg.py` writes the bits;
- `os88parts.py` and the C SDK's `os88_part_*` read them.

**Not prototyped.** The estimate is package-side code only. No kernel byte is
needed, because the cell already takes 24 bits.

**Why it waits.** Nothing in the tree has a single asset that needs it, and
the plain case already works.

### 4.2 DEFERRED — a raw tail over 64KB (estimate ~25 `.cold`, and a format change)

**What the raw tail is.** Every stream ends in a stretch stored as-is (SPEC.md
§20.13.7). The encoder cuts the symbols at the **first peak** of
`produced − consumed`, the point where compression was furthest ahead, and
stores everything after it raw. Past that point the data only ever cost bytes
to encode, so storing it raw is free. It is also what makes in-place expansion
need no margin: the reader can never be overtaken, and a buffer of exactly the
unpacked size is enough. The tail's length is the stream's first word, `T`, so
it is at most 65,535 bytes.

**What it restricts.** A file whose **last 64KB or more never earns back its
encoding overhead** cannot be compressed **at all**:

- the encoder has no valid place to cut, because any cut after the peak would
  break in-place expansion;
- so the whole file is refused, not just its end. `cz_wrap` stores it plain,
  and the machine's `Compress` says *"Its end won't compress"* and leaves it.

In practice that means a file ending in 64KB or more of **incompressible**
data:

- a file that ends with embedded already-compressed data (a packed asset, a
  ZIP, an image);
- a module or sample bank whose last 64KB or more is noisy sample data;
- `tests/lzbig.py`'s `TAIL.DAT`: 40KB of text, then 70KB of noise.

`TAIL.DAT` shows the cost: the text alone would have saved roughly half its
40KB, and those savings are lost to noise that was never going to shrink.

**What it does not restrict:**

- **total size**: a 400KB file is fine if its incompressible end is under
  64KB;
- **incompressible data in the middle**: compressible data after it puts the
  encoder ahead again, the cut moves later, and the tail stays short;
- **packages and parts**: a package is under 61,440 bytes and a part under
  65,024, so their tail can never pass it. Only `'CZ'` files over 64KB can hit
  it.

**What lifting it takes.** It is a **stream-format** change, not a decoder
change:

- a way for a stream to say *"`T` is 24 bits"* (a flag the decoder can read
  before it has the tail);
- both kernel arms and `lz_tail`, whose copy would have to cross a segment;
- both host encoders and decoders, and `kernel/compress.inc`'s writer and its
  cut;
- `os88lz.lzb_compress_machine`, which must stay byte-identical to the
  machine (`tests/lzcomp.py`);
- `t_lzfmt`, `lzbig`'s `TAIL.DAT` (which flips from *refused* to
  *compressed*), and every reader of the format.

About 25 bytes of `.cold` is the *estimate* for the kernel's share. The rest
is host code and gates.

**Why it waits.** The case is rare in this tree, and when it does come up,
putting the incompressible part somewhere other than the end is a cheaper fix
than a format change.

### 4.3 NOT RECOMMENDED — one stream for the kernel image (estimate +40–60 bytes of `.boot2`)

`boot2.asm`'s `kz_expand` is a separate, unbounded LZ4 copy of about 60 bytes
that never leaves a segment. That is why `os88kz.py` cuts the kernel into
61,440-byte blocks, at a cost of 1,076 bytes and 2 sectors, about 71 ms of
read on the 5150.

Crossing would need both pointers renormalised per sequence and a borrow on a
back-reference, which puts a test in the decode loop. Against 799 ms of decode
at 39.9 cycles a byte, a few percent of slowdown eats most of the 71 ms. The
blob is transient, so its bytes are free; the time is not. **Not without a
field measurement saying otherwise.**

### 4.4 REFUSED — a multi-block container

Refused on §20.14.5's measurement (44% of BEVERLY.MOD's win). It is the thing
this work exists to avoid.

### 4.5 Left as it is — the `OP_XMS` span's 128 sectors

`op_xload` climbs the span in a word of bytes, and SPEC.md §20.12.11 left it
alone. No package declares `OP_XMS`, so there is nothing to price it against.
