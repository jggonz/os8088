# Compressing `.o88`: what it costs, what it buys, and where the decoder goes

**Status: WAVES 0, 1, 2, 3a, 3b AND 5 ARE BUILT** (§13) — the host-side formats
and the packer flag, the kernel's two decoders behind `COMPRESS=` with
`OSAPI_DECOMP`, the loader expanding a compressed `.o88` in place, drivers
doing the same, and **any file at all read transparently**. SPEC.md §20.13 and
§20.14 are the contract; `lzfmt`, `lzfence`, `lzload`, `lzdrv`, `lzfile` and
`lzmod` are the gates. **§13.1 is what the building corrected**, and the design
held through wave 3: the in-place margin measured 2 bytes in practice as
predicted, the loader's sizing needed no change at all, and `.text` moved only
by the API cell and its stub.

**§13.4 is what wave 5 corrected, and it is the largest correction in this
document.** The transparent read was designed with a 64KB ceiling, on the
reasoning that a bigger file is the application's to expand — and the file the
whole feature is *for* is **`BEVERLY.MOD` at 116,085 bytes**. It compresses to
**42,169, 36.3%**, which is 145 sectors of floppy and 72 of a 360KB disk's 114
clusters. So the decoder learned to cross segments (SPEC.md §20.14.5), the
alternative — splitting the stream into segment-sized blocks — having been
measured at **18,766 bytes, 44% of the entire win**. `lzmod` is that row, and
it double-clicks the module, opens Tracker through the association, and
compares all 116,085 bytes. **`lzmod-lzb` is the same row on a `COMPRESS=both`
kernel** and is the only thing in the tree that ever *executes* LZB's own
crossing arm — the default build does not carry LZB at all, so `t_buildmatrix`
keeps it assembling and nothing else keeps it correct. Both pass byte for
byte.

**The rest is still investigation.** Every number below was taken on a
cycle-accurate 4.77 MHz 8088 or off a disk image this tree actually builds; the
two that are modelled say so in the sentence that carries them.

**§15 is the one to read if you care that listings stay fast**: the flag and
the unpacked size go in four bytes of the FAT directory entry that this
kernel already zeroes and ignores, so knowing costs **no extra I/O at all**,
and the staged 24-byte entry needs no widening either.

**§1–§11 ask whether compressing a package pays. §12 costs the FRAMEWORK** —
transparent file compression, a Compress verb in the file manager, compressed
`.o88` parts, the image itself, and drivers — and §13 is the build order.

**Read §1 and then §6.** §1 is the load-time answer, and it is *narrow*: only
one of the three candidate formats pays at all, it pays only on the 360KB
floppy, and on a 1.44MB one it loses. §6 is the answer that is not narrow, and
it is the reason to build this: **compression collapses the 360KB two-disk
split back into one disk** — the shipped apps floppy plus `BEVERLY.MOD`, which
SPEC.md §24.4 created a whole third disk to carry, come to **331 of 354
clusters**. That is not a model. `tools/os88disk.py` built the image.

---

## 1. The finding, in one table

Three formats, each the best of its family that an 8086 can decode. Ratio is
over **all 25 shipped packages** (313,475 bytes); speed is measured with a
breakpoint either side of the decoder on `os8088_5150_cga_gla`, IF=0, bare
metal, so the cycles are the decoder's and nothing else's.

| format | ratio | cycles/byte | decoder | verdict on the target machine |
|---|---|---|---|---|
| `rep movsw` (the floor — a plain copy) | 100% | **13.09** | — | what any decoder is beaten against |
| **LZ4** — byte-oriented, min match 4 | **79.6%** | **42.4** | 76 B | the only one that pays |
| …the same, **bounds-checked** (§7.3) | 79.6% | **50.6** | **115 B** | **the shippable one** |
| **LZSS 12/4** — flag byte, 4KB window | 78.0% | 90.8 | 51 B | 1.6 pp of ratio for 1.8× the time. **No** |
| **LZB** — bit-oriented, Elias gamma (aPLib family) | **69.3%** | 168.0 | 75 B | best ratio, and it loses by 2× |

The load-time trade is one line. **On a 360KB floppy the disk gives back 332
cycles for every byte a format removes** (§5), so a decoder costing *C*
cycles per output byte must remove more than *C*/332 of the file:

| | must remove | actually removes | |
|---|---|---|---|
| LZ4, bounds-checked | 15.2% | **20.4%** | pays, ×1.34 |
| LZSS 12/4 | 27.3% | 22.0% | loses |
| LZB | 50.6% | 30.7% | loses, badly |

**On a 1.44MB floppy the same figure is 152 cycles a byte**, so LZ4 would have
to remove 33.4% and it removes 20.4%: *nothing in the table pays.* The medium
decides, not the format — which is §9's recommendation in one sentence.

Summed over every shipped package, LZ4 is **+1,130 ms** of launch time saved
across 25 launches — an average of **+45 ms** each, and negative for four of
them (§5). Against that, §6's disk win is 61 KB, unconditional, and does not
depend on the speed trade at all.


### 1.1 Three packages, measured end to end

Bytes against time, for the largest shipped package, a mid-size one, and a
small one. Every cycle count in these tables was taken directly on the subject
named — none is extrapolated from a rate — and every row's output was
checksummed on the guest. `read` is the 360KB floppy at 35.6 ms a sector (§2);
`saved` is positive when compression makes the launch **faster**.


**`sheet.o88` — 48,352 bytes, 95 sectors, 48 clusters of a 360KB disk's 354** (the largest)

| format | bytes | ratio | sectors | read | decode | total | saved | on 1.44MB |
|---|---|---|---|---|---|---|---|---|
| **raw — today** | 48,352 | 100% | 95 | 3,384 ms | — | **3,384 ms** | — | 1,544 ms |
| LZ4, bounds-checked | 36,485 | 75.5% | 72 | 2,565 ms | 534 ms | **3,099 ms** | **+286 ms** | 1,704 ms (-160) |
| LZ4 (unbounded) | 36,485 | 75.5% | 72 | 2,565 ms | 445 ms | **3,009 ms** | **+375 ms** | 1,615 ms (-71) |
| LZSS 12/4 | 36,152 | 74.8% | 71 | 2,529 ms | 888 ms | **3,417 ms** | **-33 ms** | 2,041 ms (-498) |
| LZB (bit-oriented) | 31,700 | 65.6% | 62 | 2,209 ms | 1,650 ms | **3,858 ms** | **-474 ms** | 2,657 ms (-1,113) |

**`paint.o88` — 27,422 bytes, 54 sectors, 27 clusters of a 360KB disk's 354** (the example asked for)

| format | bytes | ratio | sectors | read | decode | total | saved | on 1.44MB |
|---|---|---|---|---|---|---|---|---|
| **raw — today** | 27,422 | 100% | 54 | 1,924 ms | — | **1,924 ms** | — | 878 ms |
| LZ4, bounds-checked | 21,405 | 78.1% | 42 | 1,496 ms | 289 ms | **1,785 ms** | **+139 ms** | 971 ms (-94) |
| LZ4 (unbounded) | 21,405 | 78.1% | 42 | 1,496 ms | 242 ms | **1,738 ms** | **+186 ms** | 924 ms (-47) |
| LZSS 12/4 | 21,146 | 77.1% | 42 | 1,496 ms | 513 ms | **2,010 ms** | **-86 ms** | 1,196 ms (-318) |
| LZB (bit-oriented) | 18,547 | 67.6% | 37 | 1,318 ms | 953 ms | **2,271 ms** | **-347 ms** | 1,554 ms (-676) |

**`calc.o88` — 6,508 bytes, 13 sectors, 7 clusters of a 360KB disk's 354** (a small one)

| format | bytes | ratio | sectors | read | decode | total | saved | on 1.44MB |
|---|---|---|---|---|---|---|---|---|
| **raw — today** | 6,508 | 100% | 13 | 463 ms | — | **463 ms** | — | 211 ms |
| LZ4, bounds-checked | 5,240 | 80.5% | 11 | 392 ms | 58 ms | **450 ms** | **+13 ms** | 237 ms (-26) |
| LZ4 (unbounded) | 5,240 | 80.5% | 11 | 392 ms | 50 ms | **442 ms** | **+21 ms** | 229 ms (-17) |
| LZSS 12/4 | 5,115 | 78.6% | 10 | 356 ms | 121 ms | **477 ms** | **-14 ms** | 283 ms (-72) |
| LZB (bit-oriented) | 4,654 | 71.5% | 10 | 356 ms | 209 ms | **565 ms** | **-102 ms** | 371 ms (-160) |

**Three things these tables say that the summary above cannot.**

**The saving is quantised to the sector, and `paint` is the demonstration.**
LZSS 12/4 packs it 259 bytes smaller than LZ4 and lands on **the same 42
sectors** — so its better ratio buys exactly nothing, and it still pays 513 ms
of decode against 289. A ratio that does not cross a sector boundary is not a
saving at all.

**The per-byte rate is NOT flat, and it moves the way the ratio does.** Over a
7x size range the bounded LZ4 decoder runs 42.86 cycles a byte on `calc` and
52.69 on `sheet`. That is not noise and it is not size: `calc` compresses
*worse* (80.5% against 75.5%), so more of its output arrives as literals, and
literals leave through `rep movsb` at the 18.00-cycle floor. **A file that
compresses badly decodes quickly**, which softens the trade at both ends and is
why the small package is not the disaster the ratio alone suggests.

**On a 1.44MB floppy every row is a loss, including the best one** — `sheet`
goes 1,544 ms to 1,704. That is the same conclusion §5 reaches by arithmetic,
here as three worked examples, and it is what recommendation 2 in §9 is for.

---

## 2. How the numbers were taken

**Ratio.** `os88pkg.py`'s real output, all 25 packages. LZ4 is the reference
`lz4hc` encoder at level 12; LZSS and LZB are parsed with a shortest-path DP
over the whole file, so what is reported is the **format's** ratio and not a
greedy parser's. All three round-trip against a reference decoder on the host
before anything is measured.

**Speed and size.** A bare-metal floppy: a boot sector, then a payload holding
the three decoders and the compressed blobs, entered with IF=0 and never
re-enabling it. The host arms an exec breakpoint on a marker `nop` either side
of each decoder and differences MartyPC's cycle counter — the same instrument
`tools/os88boot.py` uses, and the same shape as `tests/paintlzw.py`'s
measurement of the LZW loop in SPEC.md §42.21. **Each row checksums its own
output on the guest and the host compares it with the file**, so a row that
decoded wrong cannot be published as a fast one.

Two controls say the harness is measuring what it claims. `rep movsb` reads
**18.00** cycles a byte and `rep movsw` **13.09**, against the 17 and 12.5 this
tree already quotes. The suite was then re-run on four more packages spanning
6.5 KB to 48 KB (§1.1). Between two packages of similar size the rates move by
**1.3%, 3.6% and 6.0%**; across the whole range LZ4 moves **±10% and
systematically**, with the badly-compressing small file decoding *fastest* —
§1.1 says why. So a rate quoted alone is a mid-size package's rate, which is
what §1's table uses; the three worked examples are measured on their own
subjects and extrapolate nothing.

**Disk.** Not modelled: SPEC.md §18.91.1 measured `KERNEL.SYS` — 202 sectors,
cylinder-bound, **12 `int 13h` calls, 7,196 ms** on a 5150. That is a long
contiguous read of a freshly built file, which is exactly a package load, and
it gives **35.6 ms a sector**. It decomposes cleanly against SPEC.md §15.5's
185 ms of per-call ROM: 18 sectors a call is 2 revolutions (400 ms) plus 185,
which is 32.5 ms a sector against the 35.6 measured.

**The 1.44MB figure is the one thing here that is MODELLED** — the same 185 ms
of ROM plus 2 revolutions over a 36-sector cylinder, giving 16.2 ms a sector.
It has not been measured and it decides §9's second recommendation, so it is
the first thing §10 asks for.

---

## 3. The ratio: 8086 machine code does not compress

This is the finding that shapes everything else, and it was a surprise.

| | LZ4 | LZSS | LZB | deflate | LZMA |
|---|---|---|---|---|---|
| all 25 packages | 79.6% | 78.0% | 69.3% | 67.2% | 62.6% |

**Even LZMA only reaches 62.6%**, and it is not runnable here. Dense 8086 code
with 16-bit relative displacements has little for a byte-matcher to find: the
same call written twice is two different byte strings.

That last point suggests the standard x86 BCJ filter — rewrite `E8`/`E9`
displacements from relative to absolute so repeated calls become identical —
and it was tried. **It is worth 2.7 pp to LZ4** (78.9% → 76.2% over the twelve
largest) **and it is refused**: undoing it is a byte-at-a-time scan of the
*output* for two opcodes, which costs ~20 cycles a byte on top of the
decoder's 50.6, against a saving worth 332 cycles a byte × 2.7% = 9. It loses
by more than a factor of two. It would be worth having if the goal were disk
space alone, where the filter is free on disk and the cost is only at load —
and §6 is that goal, so this is worth re-opening if §9's second recommendation
is taken and load time stops mattering.

**Data is a different question entirely, and §6 turns on it.** `BEVERLY.MOD`
is 8-bit PCM instrument samples and compresses to **35.8%**; the C64's ROM
part (SPEC.md §20.12) is 84.6%, which is code again.

---

## 4. The speed: what a decompressor costs on this machine

The project has been here once already. SPEC.md §42.21 measured Paint's GIF
LZW decode at **999 cycles a pixel** and got it to 17 by making the inner loop
emit a **run** through `rep movsb` instead of a pixel through three calls.
That is the same lesson as this table: what a decompressor costs is decided by
how much of its output leaves through a `rep`, and how much through a decision.

- **LZ4 (42.4)** copies both literals and matches with `rep movsb`, so a long
  run costs 17 cycles a byte and the per-sequence overhead amortises. It is
  ~3.2× the `rep movsw` floor.
- **LZSS 12/4 (90.8)** is *slower than LZ4 despite a better ratio*, and that
  is the whole of why it is refused. Its literals are one `movsb` each with a
  flag shift, a bounds test and a loop branch around every one — a decision
  per byte where LZ4 makes one per run.
- **LZB (168.0)** reads lengths and offsets bit by bit. Its literals are
  byte-aligned and its tag byte is carried in `DL` with a guard bit, so no
  register is spent on a counter; even so, the Elias gamma codes cost it 4×
  the floor. The ratio is genuinely the best of the three and it does not come
  close to paying for itself.

**The decoders are small — 51 to 115 bytes.** That is the one part of this
investigation that came out better than expected, and it is what makes §7's
answer cheap.

---

## 5. The trade, stated once

One sector saved on a 360KB floppy is 35.6 ms, which at 4.77 MHz is 169,900
cycles, spread over the 512 bytes that sector held:

```
    the disk gives back  35.6 ms / 512 bytes x 4,772,727 Hz  =  332 cycles
                                                                per byte removed
```

So a format pays iff `cycles_per_output_byte / 332 < 1 - ratio`. The same
arithmetic on a 1.44MB floppy (16.2 ms a sector, MODELLED — §2) gives **152**,
and on a hard disk it is smaller again. **Compression on this machine is a
bet on slow media**, and the shipped configuration contains both kinds.

Two consequences worth stating because they are easy to get backwards:

- **A faster machine does not help.** Halving the cycles a decoder spends and
  halving the milliseconds a sector costs move the break-even the same way; a
  286 with a 1.44MB drive is faster at both and the trade lands roughly where
  the XT's does. What moves it is the *ratio* of the two, which is media
  speed against CPU speed.
- **The saving is quantised to the sector, and on disk to the CLUSTER.** A
  package that compresses by 300 bytes saves nothing at all. **Measured
  directly on the eight smallest packages**, four are net negative — `hello`
  (−6 ms), `mines` (−17), `recorder` (−1) and `solitair` (−18) — and that is
  what §9's per-file opt-in exists to handle. **It is not a size threshold**,
  which is why the opt-in has to be arithmetic rather than a rule of thumb:
  `wire` at 2,750 bytes is +14 ms and `solitair` at 5,881 is −18, because what
  decides it is whether the last sector is crossed. `recorder` at −1 ms is the
  knife edge, and the honest reading of that row is that it is zero.

---

## 6. The prize that is not load time: the 360KB disk is 97% full

`build/apps360.img` is **351 clusters of 354** — three kilobytes spare. The
360KB system disk is 331 of 354. That geometry is the real constraint on this
project's shipped software, and SPEC.md §24.4 already paid for it once:
`BEVERLY.MOD` is 114 of a 360KB disk's 354 clusters, so at that geometry alone
the module was moved to **a third floppy of its own**, and a user who wants to
hear it has to swap disks.

Compression does not model its way out of that. It was built:

| disk built by `tools/os88disk.py --size 360` | clusters of 354 |
|---|---|
| today's `apps360.img` | **351** |
| the same payload, packages LZ4-compressed | **290** |
| …**plus `BEVERLY.MOD`, compressed** | **331** |

**The third disk goes away, with 23 clusters spare.** The packages were
written in the format §7.1 proposes — an uncompressed header prefix followed
by an LZ4 body — so the layout on the image is the real one, not a stand-in.

This win has properties the load-time win does not:

- **It does not depend on §5's trade at all.** It is true on fast media too.
- **It is where the next application goes.** A disk with three clusters spare
  refuses the next thing anyone writes; one with 64 does not.
- **It gets better with the ratio, not worse.** LZB frees 93 KB where LZ4
  frees 61 — so if disk space is the goal, the format choice inverts and §3's
  refused BCJ filter comes back into scope.


### 6.1 The same sums on the 1.44MB apps disk — and the LZ4/LZB exchange rate

**The byte savings on `build/apps.img`, built rather than modelled** (24
packages, 310,725 bytes; a 1.44MB disk's cluster is 512 bytes, so a cluster is
a sector):

| | package bytes | saved | disk | clusters freed |
|---|---|---|---|---|
| raw — today | 310,725 | — | **914 of 2,847** | — |
| LZ4 | 247,955 | **62,770 (61 KB)** | 791 | 123 |
| LZB | 215,967 | **94,758 (92 KB)** | 729 | 185 |
| LZ4, data files too | — | 145,633 (142 KB) | 630 | 284 |
| LZB, data files too | — | 184,261 (180 KB) | 554 | 360 |

(the packages compress to a hair less than §1's figure because the ≤112-byte
header prefix stays in the clear — §7.1 — which is ~31 bytes a package.
`BEVERLY.MOD` alone is 82,863 of the LZ4 data row and 89,503 of the LZB one.)

**And they buy nothing, because that disk is 68% empty.** 1,933 free clusters
is **967 KB**, against an average shipped package of 12.6 KB — 76 more apps of
runway before it binds. Meanwhile the same change *costs* 1.15 s of load time
across those 24 launches at §5's 1.44MB arithmetic. **The 1.44MB disk is where
compression is all cost and no benefit**, which is recommendation 2 in §9
arrived at from the space side rather than the time side.

**Runway is the reason to want the ratio, and it lives entirely at 360KB.**
Building the one-disk 360KB image of §6 both ways:

| on one 360KB disk (apps + `BEVERLY.MOD`) | clusters | spare | ≈ average apps of runway |
|---|---|---|---|
| LZ4 | 331 of 354 | 23 KB | **1.8** |
| LZB | 297 of 354 | 57 KB | **4.4** |

**So the trade, stated as the only number that matters.** Averaged over the 24
packages, on a 360KB floppy, against today:

| | disk saved | per launch |
|---|---|---|
| LZ4, bounds-checked | 61 KB | **52 ms FASTER** |
| LZB, **unbounded** | 92 KB | 163 ms slower |
| **LZB instead of LZ4** | **+31 KB** | +215 ms |

> **THESE TWO LZB ROWS ARE NOT THE SHIPPABLE ARM AND ARE SUPERSEDED BY
> §12.7.** They were taken before a bounded LZB existed, so LZ4 here is
> bounds-checked and LZB is not — which flatters LZB by a quarter of its
> time. Bounded on both sides it is **280 ms slower a launch and 332 ms
> against LZ4, or 254 ms per KB**, not 164. The rows are kept because §6's
> disk figures were computed with them and are unaffected: the ratio did
> not move, only the decode time.

LZ4 is not a trade at all — it is faster *and* smaller, so on the 360KB
geometry it is free money. LZB is the real question, and re-priced on the
bounded decoder (§12.7) its two halves are **+31 KB of runway, once** — about
2.4 more average apps — against **~332 ms on every launch, for ever**. Those
are different units on different clocks and §12.7 says why they must not be
divided into one another.

#### 6.1.1 Mixing the two per package is REFUSED, and the reason is a measurement

The obvious escape is to pay LZB's price only where it buys most — LZB on the
packages that give the most disk per millisecond, LZ4 on the rest. **The rate
is too flat for that to work.** Here the per-KB form is legitimate: it is
comparing packages with each other, so the "one launch" assumption sits on
both sides and cancels (§12.7). **Re-measured with both decoders bounded**, it
runs 202 ms/KB (`arkanoid`) to 410 (`mines`), with 18 of 23 between 150 and
300:

> the **cheapest eight** packages give 12.0 KB for 2,696 ms; the **dearest
> eight** give 6.9 KB for 2,079 ms.

That is **1.34x** efficiency for a per-package policy, a second format in the
kernel and a decision nobody can check by eye — where **picking one format per
DISK gets the same shape for one line of the Makefile**. (The unbounded
reading was 119–282 ms/KB and 1.45x; bounding both decoders made the spread
*flatter*, so the refusal is stronger than it was.) The flatness is not a
coincidence: both formats decode at a near-constant cost per output byte, so
the ratio between them barely moves with the file.

---

## 7. What it costs IN THE KERNEL

### 7.1 The format change is one flag bit and no new header field

SPEC.md §20.2's header has bits 3–7 of the flags byte spare. **Bit 3 = the
image is compressed.** No length field is needed: the loader already has the
file's size staged from the directory entry (`LD_DE_SIZE`, SPEC.md §19.1), and
the compressed length is that minus the uncompressed prefix.

**The prefix is what the mount reads and must stay in the clear** — the
32-byte header, the 64-byte icon if bit 0 is set, and the 16-byte association
block if bit 1 is set (SPEC.md §54.6). At most 112 bytes, and every byte of it
is inside the first sector, which `disk_mount`'s icon harvest and the loader's
own step 2 peek already read. **So the mount, the icon harvest, the
association scan and `ld_check_hdr`'s first pass are all untouched**, which is
the property that makes this cheap rather than invasive. The format version
stays 3, for SPEC.md §20.12's reason: an old kernel refuses the file through
the guard that already exists, by the correct route.

### 7.2 The loader change is four edits to `ld_run_body`

`kernel/loader.inc` reads the image straight into the region at `ES:0` today.
Compressed, it becomes:

1. **step 4, sizing** — unchanged. `image` is already the *uncompressed* size,
   which is what the region must hold. Add the margin below.
2. **step 6, the read** — read the whole *file* to the **top** of the region
   instead of the bottom, at `roundown512(image + margin - filesize)`. The
   rounding down keeps the destination 512-aligned, which `int 13h` requires.
3. **step 6a, new** — copy the ≤112-byte prefix down to offset 0, then
   decompress from just past it. The reader is ahead of the writer throughout,
   so the region holds its own compressed source and **no second buffer and no
   second heap claim exist**.
4. **step 6b** — the in-region header re-check that guards against a disk swap
   now runs on the *decompressed* header, where it means the same thing.

**The in-place margin is 2 bytes.** Measured, by simulating the decode over
every shipped package and taking the worst point at which the writer is
nearest the reader. Round it to 16 for the paragraph and it is free — the
region is already rounded up to whole kilobytes by `mem_bytes_kb_x`. That
number is the single most important line in this section: **compression costs
no RAM**, which is what makes it admissible at all on a 128KB machine with
49.5 KB of free heap.

### 7.3 The decoder must be BOUNDED, and that is 39 of its 115 bytes

Every byte off a disk is hostile (SPEC.md §19), and `os88pkg.py` is not a gate
on a foreign `.O88`. An unbounded LZ4 decoder handed a corrupt stream will
write past the image and read below the region base — which is a resident
package's code, because `mem_claim_hi_x` claims top-down (SPEC.md §50.3). It
is the same class of failure `ld_check_hdr`'s carry test exists to stop.

Three checks make it safe, and the cost of all three was measured rather than
guessed: **76 bytes → 115, and 42.4 cycles a byte → 50.6.**

- the writer may not pass the image end — on literals *and* on matches;
- a match offset of zero is refused (it would copy from itself for ever);
- a match offset larger than what has been produced is refused;
- and a stream that ends short of the image is a truncated file, not a
  package.

**The 50.6 figure is the one §1 uses**, deliberately: the unbounded 42.4 is not
a number this kernel could ship.

### 7.4 The bill

| | bytes |
|---|---|
| the bounded LZ4 decoder | **115** |
| `ld_run_body` steps 2/3/4 above, estimated | ~80–120 |
| **total, `.cold`** | **~200–235** |
| `.text` + `.bss` | **0** |

**It all goes in `.cold`.** `loader.inc` is already a cold module (SPEC.md
§2.6) with a CS of its own, so none of this touches `KERN_CODE_MAX` — which is
the binding constraint on this kernel, with 9,171 bytes left and no way to
raise it. That is worth saying plainly: **this feature costs the scarce budget
nothing.**

What it does cost is footprint. `.cold` stands at 36,793 bytes with **71 left
in its current rung** and 441/512 accrued, so ~200 bytes crosses one cold
rung: 512 bytes of every machine's RAM, drawn from the 20,480 spare in
`KERN_BUDGET`. Per CLAUDE.md that crossing is not an argument against the
design and not one for it — the amortised price of a byte is a byte, and the
byte cost is ~200.

---

## 8. What it costs IN EACH APP — and the one thing an app cannot do

**A package can already do this today, with no kernel change at all.**
SPEC.md §20.12's parts mechanism gives it `OSAPI_FILE_READ_AT` for the bytes
and `OSAPI_MEM_CLAIM` for somewhere to put them; the decoder is 115 bytes of
its own 60KB segment, which is its budget and not the kernel's. Nothing in
this document needs permission for that, and §6's `BEVERLY.MOD` row is exactly
that shape: Tracker reads a compressed module and expands it.

**And there is one thing an app cannot reach: its own primary image.** The
kernel loads that before a single instruction of the package runs, so the
bytes that make up most of every package are the loader's or nobody's. The
alternatives are both worse than they sound:

- **Ship a stub and far-call a compressed second segment.** This is the
  SPEC.md §73.14 overlay shape, and it costs the app a split by call
  frequency, a far call on every crossing, and the rule that no overlay
  function's address may be taken. It is a large structural change to buy
  20% of one file.
- **Let each app do it.** 115 bytes × 25 is not the objection — they are
  separate files and no shared budget is hit. The objection is §7.3:
  **25 independent chances to get a hostile-input bound wrong**, on data
  read off a removable disk, in a decoder each author writes once.

| | kernel | per app |
|---|---|---|
| reaches the primary image | **yes** | no |
| kernel bytes | ~200 `.cold`, 0 `.text` | **0** |
| per-app bytes | 0 | 115 + fetch/claim code |
| gets §6's disk win | **yes, all of it** | only on parts and assets |
| places to get the bounds wrong | **one** | one per app |
| needed for `BEVERLY.MOD` | no | **yes — it is not a package** |

They are not alternatives. **The kernel is the only place the primary image
can be compressed; the app is the only place a data file can be.** §6 needs
both, and the two together are what put the module back on the apps disk.

---

## 9. Recommendation

1. **Take LZ4, bounds-checked, in the loader.** It is the only candidate that
   pays on the target machine, it is 115 bytes of `.cold`, it needs one spare
   flag bit and no new header field, and its in-place margin is 2 bytes so it
   costs no RAM.

2. **Make it a property of the DISK, not of the package.** The Makefile
   already builds a separate payload list for the 360KB geometry (`APPS360`
   against `APPS`), which is exactly the geometry where the medium is slow
   *and* the disk is full. Compress there; ship the 1.44MB and 1.2MB disks
   uncompressed, where §5's arithmetic says compression is a net loss and the
   space is free anyway. One `os88pkg.py --compress` flag and one Makefile
   list carry the whole decision, and the kernel never learns which disk it is
   reading.

3. **Let `os88pkg.py` refuse to compress a file that would not gain**, on the
   sector arithmetic in §5 — that removes the four negative rows without the
   kernel or the author knowing anything about it.

4. **Compress `BEVERLY.MOD` in Tracker**, which is app work needing no kernel
   change, and put the media disk's contents back on `apps360.img` (§6).
   This is separable from 1–3 and can go first.

**Refused, and recorded so they are not re-derived:** LZSS 12/4 (slower than
LZ4 *and* a worse trade — and §1.1 shows it packing `paint` 259 bytes smaller
onto the same 42 sectors, which is the whole case against it in one row); a
per-package choice between LZ4 and LZB (§6.1.1 — the exchange rate is flat, so
the policy buys 1.45x for a second decoder and an unauditable decision); and
the x86 BCJ filter (§3 — 2.7 pp for ~20 cycles a byte, unless the goal becomes
space alone, when it should be re-opened).

**LZB is NOT refused — it is a decision about runway**, and §6.1 is the sheet
to take it on. It is the right answer if and only if the 360KB disk's runway
is worth 164 ms a kilobyte, permanently, on every launch; LZ4 is the right
answer if the 23 KB it leaves is enough. Nothing else in this document
depends on which way that goes: the format is a flags-byte field, the loader
change in §7.2 is identical, and the bound in §7.3 is the same bound.

**Not investigated: compressing the kernel itself.** `kernel.bin` is 102,829
bytes and LZ4 takes it to 76.9%, which is 46 of its 201 sectors — worth ~1,640
ms against ~918 ms of decode, so **~700 ms of an 11.4 s boot**. It is a
different subsystem with a much harder constraint: the decoder would have to
live in the boot blob, which stands at 3,989 bytes of 4,096. **107 bytes free
against a 115-byte decoder** — it does not fit, and that is before the loader
changes. Worth its own page, not a paragraph in this one.

---

## 10. Open — settle these before building

1. **Measure the 1.44MB per-sector cost.** It is modelled at 16.2 ms (§2) and
   recommendation 2 rests on it. `BOOTPROF=1` on a 1.44MB machine answers it,
   and if it comes in near the 360KB figure then compression should simply be
   on everywhere and recommendation 2 collapses into recommendation 1.
2. **Verify the bounded decoder actually refuses.** §7.3's checks are written
   and measured for *size and speed*; that they reject a corrupt stream rather
   than running away is asserted and not yet tested. A row that feeds it a
   truncated blob, a zero offset and an over-long match is the gate, and it
   should exist before the decoder does.
3. **Where does the 2-byte margin come from on a package that exactly fills a
   kilobyte?** `mem_bytes_kb_x` rounds up, so the slack is there in every case
   measured — but the case to check is `image + bss` landing exactly on a KB
   boundary, where the rounding gives nothing.
4. **Does `--verify`'s structural fsck need to know?** `tools/os88disk.py`
   validates `image == file size` today and would refuse every compressed
   package; so would `tests/unit/t_image.py`, which walks every shipped
   floppy. Both are one condition, but both are gates that run in `make`.
5. **What does a compressed package do to the Task Manager's SIZE column?**
   `I_SIZE` is the region, which is unchanged — but the file is now smaller
   than the program, and SPEC.md §29.2's accounting should be read once to
   check nothing quotes the file.

---

## 11. The harness

Everything in §1–§6 is reproducible. The measuring apparatus is four files: a
host-side format library with encoders, reference decoders and a round-trip
self-check; a 512-byte boot sector; a bare-metal payload holding the four
decoders under test with a marker pair around each; and a driver that packs a
real `.o88`, assembles, builds a 360KB image, boots it under MartyPC, arms the
breakpoints and differences the cycle counter.

It is **not committed**, deliberately: nothing under `tests/` ships, and a
harness for a design that has not been taken is a row of the suite that
measures nothing. If §9 is accepted it should land as `tests/lzbench/` with
the format library beside `tools/os88pkg.py`, because at that point the
encoder is shipped software and the ratio is a thing that can regress.

---

## 12. The framework — five workstreams, costed

§1–§11 asked whether compressing a package pays. This part asks what a
compression *facility* costs, across the five places the machine would use
one. Everything measured in this part is new and was taken the same way
(§2): bare metal, IF=0, a breakpoint either side, output checksummed on the
guest.

**Three measurements arrived with it and they change the shape of the
answer.**

| | |
|---|---|
| **A bounded LZB decoder** | **91 bytes, 207 cycles/byte** — not the 165 of §1's unbounded arm. Bounding costs LZB +16 bytes and **+25% of its time**, where it cost LZ4 +39 bytes and +19%. §12.7 re-prices the LZ4/LZB trade on it, and it gets worse: **254 ms/KB, not §6.1's 164.** |
| **An on-machine LZ4 COMPRESSOR** | **307 bytes of code and 8,192 bytes of hash table** — 2.7× the decoder in code and, at **~290 cycles a byte**, **5.7× its time**. `sheet.o88` compresses in **2.9 s**. Verified end to end: the guest's output was read back and decompressed by the reference decoder. |
| **…and it gives up 4.4–6.5 pp of ratio** | The packer parses optimally; a machine cannot. `sheet` is 75.5% from `os88pkg.py` and **82.0%** from the machine. Anything compressed by the user is meaningfully worse than anything compressed by the build. |

### 12.1 The shared core, and the ABI question

**One decoder, one format field, one bound.** All five items below call the
same routine; they differ only in who calls it and where the bytes come from.

**The format field is two bits, not a version.** `.o88` flags bit 3 = "the
image is compressed", bit 4 = which format (0 = LZ4, 1 = LZB). Non-package
files carry the same two bits in their own header (§12.2). Nothing needs a
version bump, for SPEC.md §20.12's reason: an old kernel refuses a compressed
file through the guard it already has.

**`OSAPI_DECOMP` should be a plain `OSAPI_SLOT`, not an X cell**, and the
precedent is `OSAPI_GFX_BLIT4` — a plain slot that takes a caller's far
pointer in `ES:SI` and is documented to hand the segment registers back.

```
    OSAPI_DECOMP   DS:SI = source, ES:DI = destination, CX = source bytes,
                   DX = output capacity, AL = format
                   out: CF=0 and DI = bytes produced; CF=1 = refused
                   DS and ES come back yours (OSAPI_DRV_CALL's wording)
```

**A plain slot works because the decompressor touches no kernel variable at
all.** That is unusual enough to be worth stating as the reason: every other
cell needs `DS = KERNEL_SEG` for its own data, so it needs a stub family to
get the caller's segment somewhere. This one needs *both* segments to be the
caller's — source in DS, destination in ES, exactly as the measured decoders
already run — and it has nothing of its own to address. It saves DS, works,
restores it.

**The cell costs 8 bytes and the free list is EMPTY** (SPEC.md §20.3.1):
0x01F0 and 0x01E8 have both been spent, so this is an append and
`osapi_table_end` moves.

**There should be no compression ABI, and that is the "better way".** A
compressor is 307 bytes and 8 KB of RAM against the decoder's 115 and 0, it
has exactly one caller (§12.3), and no package in the tree writes a
compressed file. Publishing it as a kernel cell would put 40× the decoder's
code in every machine to serve a menu item. It belongs in `COMPRESS.DRV`
(§12.3) with an ABI of its own, the way §52.11's `HDDTOOL.DRV` and §41.12's
`XMEM.DRV` already do it — and if a package ever needs to write one, it far-calls
that module rather than the kernel.

### 12.2 Item 1 — non-package files, transparent at open

**Where it goes: `diskw.inc`**, the by-name file I/O layer, not `disk.inc`.

**The header.** Eight bytes in front of the compressed body:

```
    +0  2  magic 'CZ' (0x5A43)
    +2  1  format: 0 = LZ4, 1 = LZB
    +3  1  flags, 0
    +4  4  the UNPACKED size
```

**The size an app is told is the unpacked size, which is the whole point.**
`OSAPI_FILE_FIND`'s record has a **`+22 word reserved, currently 0`** — so
the flag costs **no record growth at all**, the size at +18 becomes the
unpacked size, and an app built before any of this reads a correct number
without being recompiled. Paint refuses on the real figure before a sector of
the body is read, which is what was asked for.

**`OSAPI_FILE_READ` gets it; `OSAPI_FILE_READ_AT` must REFUSE it.**
**REVERSED WHEN IT WAS BUILT — see §13.4.** `READ_AT` stays RAW instead of
refusing, and the reasoning below is intact but leads somewhere else: a raw
`READ_AT` is not a transparent random-access read, so the O(n²) walk this
paragraph is about does not arise, and being raw is exactly what lets a
chunked copy be byte-exact. The paragraph is kept because the counts in it are
the evidence either way. That is
the one hard edge in this item and it is worth stating plainly. A whole-file
sequential read decompresses transparently; a random-access read into a
compressed stream cannot, because reaching offset *N* means decoding from
zero — a sequential walk in chunks becomes O(n²). The counts say this is
affordable: **22 app callers and 5 driver callers of `FILE_READ` against 8
and 3 of `READ_AT`**, and of those eight, three are the SDK's own
declarations. The real random-access users are Frotz (story paging), ftpd
(streaming a file out) and the parts loader — none of which wants a
compressed file underneath it anyway. `READ_AT` on a compressed file returns
a new `FERR_*`, and the file manager simply never compresses a `.Z3`/`.DAT`
a paging reader owns.

**The inconsistency to decide before building.** The size an app sees is the
unpacked size; the space the file occupies is the packed size. So "will this
fit on the target disk" is now a different question from "how big is this
file", and the file copier (§22.5) is where that bites — copying a compressed
file byte-for-byte is right and cheap, but a copy that *expands* needs the
packed figure. Recommendation: the copier moves the file **as it is**, and
`OSAPI_FILE_DFREE`'s arithmetic keeps using the on-disk size, which is what
the directory entry already holds.

**SETTLED, and the recommendation held** — SPEC.md §20.14.3 is the table.
`dskw_stat_x` (which is what the copier and the free-space arithmetic ask)
answers the on-disk size and was not touched. What the building added is the
half this paragraph did not reach: the copy has to keep the file's HINT, and
propagating the source entry's three bytes would have been wrong. §13.4's
third bullet is why.

**Cost:** the header parse and the size substitution are small; the read path
already stages whole files. Estimate **~150–200 bytes of `.text`** plus the
decoder. This is `.text`, not `.cold` — `diskw.inc` is resident — which
makes it the most expensive item in the list against `KERN_CODE_MAX`
(9,171 bytes left, and it cannot be raised).

**MEASURED: 1 byte of `.text` and 6 of `.bss`** (§13.5), against an estimate of
150–200. The estimate's premise was wrong rather than its arithmetic: the part
of `diskw.inc` this lands in — `dskw_rbody`, `dskw_commit`, `dskw_find` — is
`.cold`, not `.text`, so 578 of the 585 bytes are outside `KERN_CODE_MAX`
entirely. **`diskw.inc` is resident, and resident is not the same as `.text`**
(SPEC.md §2.6), which is the distinction the estimate flattened.

### 12.3 Item 2 — Compress / Uncompress in the file manager

**This is the only item that needs a COMPRESSOR, and it is 40× the decoder.**
307 bytes of code, 8,192 bytes of hash table, ~290 cycles a byte.

**So it goes on demand, exactly as the user's instinct said, and the pattern
already exists**: SPEC.md §2.8's on-demand modules — `CTRL.DRV`, `FORMAT.DRV`,
`CLONE.DRV`. A module is the **fourth lever on the footprint and the only one
that relieves `KERN_BUDGET` without relieving nothing else** (§2.8), so
`COMPRESS.DRV` costs the resident kernel a `mod_need` call site and nothing
else.

**And it inherits `FORMAT.DRV`'s disk swap, which is the real cost.**
SPEC.md §2.8.5: the module is on the system disk and *the file the operation
is about is on the disk in the drive*. On the one-floppy calibration machine,
Compress on a file on the apps disk needs the system disk in, then the apps
disk back — the formatter's prompt, verbatim. That is acceptable for a
deliberate operation and it is why this item is a module rather than a hot
path.

**What the user waits.** At 290 cycles a byte on a 4.77 MHz 8088:

| | compress | (decompress, for scale) |
|---|---|---|
| `calc.o88`, 6.5 KB | **0.4 s** | 0.06 s |
| `paint.o88`, 27 KB | **1.7 s** | 0.29 s |
| `sheet.o88`, 48 KB | **2.9 s** | 0.53 s |
| `BEVERLY.MOD`, 113 KB | **~6.9 s** (modelled from the rate) | ~1.2 s |

That needs SPEC.md §12.8's progress widget, which the file manager already
drives for copies — so there is nothing new to build for it.

**Memory.** The operation needs the whole input, the whole output and the
8 KB table. In place is not available here (the file is being replaced, not
loaded), so the peak is roughly 2× the file plus 8 KB. For `BEVERLY.MOD` that
is ~240 KB, which a 128KB machine does not have — so on `kern_small` the
verb must refuse by size, in its own words, before it starts. That is
`OSAPI_MEM_AVAIL` and a toast, not new machinery.

#### 12.3.1 Taking longer compresses much better, and the curve saturates

Compressing is a *deliberate* action — nobody is waiting on it the way they
wait on an open — so the greedy parse above is the wrong default. **A model
of the shipped 8086 compressor, validated by reproducing its output length to
the byte on all four subjects**, prices the alternatives:

| variant | ratio | vs shipped | relative work |
|---|---|---|---|
| shipped: greedy, one slot | 83.9% | — | 1.0x |
| + insert the positions a match covers | 83.5% | −0.41 pp | 1.2x |
| + lazy matching | 83.1% | −0.75 pp | 1.2x |
| **+ hash chains, depth 4** | **79.3%** | **−4.57 pp** | 1.8x |
| + depth 16 | 78.1% | −5.72 pp | 2.2x |
| + depth 64 | 78.0% | −5.91 pp | 2.4x |
| `os88pkg.py`'s optimal parse (the ceiling) | 77.7% | −6.17 pp | (host) |

**Chains are the whole story and depth 16 gets within 0.4 pp of the packer.**
Lazy matching and filling the table — the two cheap tricks — are together
worth 0.75 pp, and everything else is the chain.

**What a chain costs is RAM, not code**: two bytes per window position. So the
window bounds the memory, and it is the dial worth having rather than §12.3's
hash-table size:

| match window | chain RAM | ratio |
|---|---|---|
| 4,096 | 8 KB | 81.0% |
| 8,192 | 16 KB | 79.8% |
| **16,384** | **32 KB** | **78.8%** |
| 32,768 | 64 KB | 78.2% |
| 65,535 | 127 KB | 78.1% |

**At the SAME 8 KB the shipped greedy parse gets 83.9% and a depth-16 chain
gets 81.0%** — 2.9 pp for time alone, no extra memory at all. The memory was
never the binding constraint; the algorithm was.

**Recommendation: depth 16 over a 16,384 window — 32 KB and ~2.2x the time**,
which is `sheet.o88` in about 6.5 s instead of 2.9, and lands 1.1 pp off what
the build-time packer achieves. On `kern_small` the window drops to 4,096 for
8 KB, which still beats today's greedy by 2.9 pp.

**The hash table stays 8 KB.** 2,048 entries would halve it for a loss that
has not been measured; with the chain in place the table matters much less
than it did, and 8 KB is what every row above was taken with.

**kern_small: YES, and it is nearly free.** Being a module, it costs the small
kernel no resident byte — only ~1 KB of its system disk. The user's
"maybe not the file manager bit" can be resolved in its favour: what makes
this item expensive is RAM at run time, not kernel size, and the refusal
above handles that.

### 12.4 Item 3 — compressed parts in a `.o88`

**Almost all of this already exists.** `apps/os88parts.inc` reads parts with
`OSAPI_FILE_READ_AT` into claims the package sizes from a table compiled into
its own image (SPEC.md §20.12.3). A compressed part changes two things:

- **A new part flag, `OP_COMP`**, beside `OP_SEG`/`OP_ASSET`/`OP_SCRATCH`/
  `OP_XMS`/`OP_OPT`/`OP_LAZY` (SPEC.md §20.12.4). The table's `len` stays the
  **unpacked** length — which is what the package sizes its claim from, so
  the refusal still costs no disk — and the packer adds the packed length
  beside it.
- **The claim is read high and decompressed down**, exactly as §7.2 does it
  for the image, with §7.2's measured 2-byte margin.

**The package calls `OSAPI_DECOMP`; it does not carry a decoder.** That is
the whole argument for §12.1's cell: the kernel has the decoder anyway for
items 1, 4 and 5, so a package paying 115 bytes for its own copy is 115 bytes
wasted and one more place to get the bound wrong.

**Cost: `apps/os88parts.inc` only** — SDK source, not kernel bytes, and only
packages that use parts pay it. `apps/c64` is the one shipped consumer today
and its ROM part is 84.6% under LZ4, which is code again: it would save
3,147 bytes and cost ~217 ms at launch.

**BUILT (SPEC.md §20.12.7), and the estimate above was right to a byte in
one place and wrong in another.** The ROM measures **20,480 → 17,361, 84.8%**,
so the saving is **3,119** against the 3,147 predicted. What the design note
missed is where the packed length goes and what the claim's layout becomes;
§13.7 is what the building corrected.

### 12.5 Item 4 — the `.o88` image

**Costed in full in §7 and unchanged by this part.** Flags bit 3, no version
bump, ~200–235 bytes of `.cold`, a 2-byte in-place margin. The only addition
here is bit 4 selecting the format, which the loader passes straight to
`OSAPI_DECOMP` as `AL`.

### 12.6 Item 5 — drivers and modules

**Drivers compress far better than packages — 89,212 bytes to 60,777 (68.1%)
under LZ4 and 53,613 (60.1%) under LZB** — and the reason is worth knowing
before the decoder is reached for.

`drivers/os88drv.inc` says it in its own words: *"There is no bss. A driver's
zeroed data is written as `db 0` in the image and ships on the floppy."* So
a driver image is **6.6% to 38.8% literal zeros**: `saver.drv` is 5,360 of
13,797, `ether.drv` 5,341 of 17,666 with a single 4,066-byte run.

**How much of that a `bss` field would take instead, measured three ways** —
the way `os88net.com` went 34 KB to 18 KB, with no decoder and no cycles:

| | bytes off the disk | cost at load |
|---|---|---|
| trailing zeros only — a header field, **no driver source change** | 6,722 | none |
| **every zero run ≥ 64 bytes** — the same field, with those buffers moved to the end of each image (about 21 runs across 12 files) | **14,648** | none |
| every zero byte (an unreachable bound) | 20,674 | none |
| **LZ4 over the whole image** | **28,435** | 50.6 cycles a byte |

**And they are near-SUBSTITUTES rather than complements, which I had
backwards.** Doing both — reorganise *and* compress — comes to 28,582 bytes,
which beats compression alone by **147 bytes**: LZ4 already takes a
4,066-byte run of zeros down to a couple of dozen, so deleting it first buys
almost nothing *on top*.

**Do the bss field anyway, and first, because the argument is load TIME.**
The two options save nearly the same disk, but a compressed zero costs
50.6 cycles a byte to expand where a reserved one costs `rep stosw` — about
1.3. Reorganising first means 14,648 fewer bytes have to go through the
decoder every time a driver loads, worth roughly **150 ms across the twelve**
and correspondingly less per driver, for a diff that is a section move in
about 21 places.

**Two loaders, not one.** `.DRV` files are read by `driver.inc` and modules by
`mod.inc`, and each needs the same four edits `ld_run_body` needs (§7.2).
`mod.inc` is the more delicate: a module is *this kernel's own code* cut out
of this build's binary and validated by two stamps (§2.8), so the stamps must
be computed over the **uncompressed** image or they stop meaning what they
mean.

**Cost: ~60–80 bytes of `.cold` per loader**, both already cold.

### 12.7 Where the decoders live, what the knob is, and kern_small

**Both decoders are resident, in `.cold`, and cost 206 bytes together**
(LZ4 bounded 115, LZB bounded 91) plus ~10 for the format dispatch. They
cannot be modules: `mod.inc` itself would need one to load one.

**The knob is `COMPRESS=lz4|lzb|both`.** Default `lz4`; `both` builds the
dispatch and is what makes a field A/B possible on a real 5150. This is the
project's standard shape — a design decision that turns on a machine nobody
here has gets a knob, and the knob is also the only thing keeping the unused
arm assembling.

**Re-priced on the bounded LZB, the LZ4/LZB trade is worse than §6.1 said**,
because §6.1 used the unbounded arm. Both bounded, over the 24 packages on
the apps disk:

| | ratio | disk saved | per launch, 360KB | per launch, 1.44MB |
|---|---|---|---|---|
| raw — today | 100% | — | — | — |
| LZ4 bounded | 79.6% | 62 KB | **+52 ms faster** | −48 ms |
| LZB bounded | 69.2% | 93 KB | **−280 ms** | −431 ms |
| **LZB instead of LZ4** | | **+31 KB** | **+332 ms** | +383 ms |

**The +332 ms is PER LAUNCH and the +31 KB is ONCE, and they do not reduce to
one rate.** It is tempting to divide — 7,965 ms of extra time summed over one
launch of each package, over 31.4 KB, gives *254 ms per KB* — but the
numerator is **recurring** and the denominator is **one-time**, so that figure
silently assumes *exactly one launch of every package*. Launch Paint twice and
the milliseconds double while the kilobytes do not. Quote the two separately:

> **LZB costs ~332 ms on every launch, for ever, and buys 31 KB once.**

The per-KB form is still the right one for comparing packages *against each
other* (§6.1.1), because there the launch assumption is the same on both sides
and cancels. It is the wrong form for deciding whether to take LZB at all.
LZ4 remains free money on 360KB; LZB's per-launch price went up by half
against §6.1's unbounded reading, and §9's decision should be taken on the
two figures above rather than on a rate.

**kern_small takes everything except the compressor**, and that falls out of
where each piece lives: the decoders and the four loader edits are `.cold`
and `.text` in both kernels alike, and `COMPRESS.DRV` is a file on a disk.
The one small-specific cost is §12.2's `~150–200 bytes of .text`, which on
`kern_small` competes with the same `KERN_CODE_MAX` and is the item to cut
first if it does not fit.

---

## 13. Build order

Each wave is independently useful and independently revertible.

| wave | what | why here |
|---|---|---|
| **0 — BUILT** | `lzfmt` in `tools/`, `os88pkg.py --compress`, the round-trip gate | the packer's round trip is what makes §7.3's bound the *only* runtime check needed for our own output |
| **1 — BUILT** | the decoders + `OSAPI_DECOMP` + `COMPRESS=` knob | 206 bytes of `.cold`, one cell; nothing reads a compressed file yet |
| **2 — BUILT** | item 4 — the `.o88` image (§7.2) | the biggest single win, and the loader is the best-fenced consumer |
| **3a — BUILT** | the driver header's `bss` field (§12.6) | **6,672 bytes** with no decoder and no cycles. Moving the interior buffers, which is the other 8 KB, is a per-driver source change and is NOT done |
| **3b — BUILT for DRIVERS** | item 5 — compressing drivers (§12.6) | **19,697 bytes** over the nine drivers and overlays. MODULES are not done: `CTRL`/`FORMAT`/`CLONE.DRV` are worth only 1,953 between them and go through `mod.inc`, whose two stamps would have to be computed over the uncompressed image |
| **4** | item 3 — `OP_COMP` parts (§12.4) | SDK-only; `apps/c64` is the test case |
| **5 — BUILT** | item 1 — transparent file reads (§12.2), **and the decoder learning to cross a segment** | **585 bytes, 7 of them resident** (§13.5) against an estimate of 150–200 of `.text` — and it takes `BEVERLY.MOD` from 114 of a 360KB disk's clusters to **42**. The `READ_AT` refusal was reversed: it stays RAW (§13.4) |
| **6** | item 2 — `COMPRESS.DRV` (§12.3) | needs waves 0 and 1 only; can be done any time after them. **§15.1's dirent hint is already written** — wave 5 put it in `dskw_commit`, derived from the bytes being written (§20.14.4), so the module has only to produce them |
| **7** | the badge or the column (§15.4) | pure drawing over data waves 5 and 6 already put in the listing. Prototype it, look at it, throw it away if it clutters — nothing depends on it |

### 13.1 What the building corrected

**The design held and the three surprises were all small.** Recorded because
each is a thing the next reader would otherwise re-derive.

**The clear prefix is always a multiple of 16** — 32, 48, 96 or 112 — and that
is what makes §7.2 cheap. `OSAPI_DECOMP` wants its destination at offset 0 of a
segment (§12.1) and the expanded body belongs at `prefix`, which would have
forced either a wider contract or three extra instructions in the hottest test
in the decoder. Instead the destination is a **segment** `prefix/16` paragraphs
along and `DI` stays 0. Nothing was designed for this; it falls out of the icon
being 64 bytes and the association block 16.

**The loader's SIZING needed no change at all, because the PACKER refuses.**
Reading the file high needs `R = roundup512(image − file + margin)` bytes of
headroom, and `R + file` does not always fit the region the loader was going to
claim. Rather than make every compressed launch claim more, `os88pkg.py`
computes the same `R` and refuses to compress a package that would not fit —
so SPEC.md §21 step 4 is untouched. **Measured across the tree, exactly one
package is refused for this**: `hello.o88`, where compression saves 65 bytes
and would have cost a kilobyte of region. A package that marginal was never
going to save a sector.

**And the bug worth recording**: `ld_expand` left `ES` on the body's segment,
so step 6's disk-swap re-check read the header 96 bytes into the image and
every compressed launch answered *Bad package*. It is the shape §12.1's "both
segments are the caller's" invites — a routine that is *documented* to hand the
segment registers back, called by one that then depends on the value it had
before. `lzload` catches it because it asserts the whole expanded image rather
than that a window opened.

### 13.2 What wave 3a settled, and the half of it that is not done

**The driver header had a free byte and it was already zero.** +31 is the last
byte of a 16-byte name field capped at 15 characters, so every driver ever
built has a NUL there and 0 reads as "no bss" — the same trick SPEC.md §20.2's
stack class byte used, and the reason this needed no version bump and no
driver source change at all. `os88drv.py` strips whole paragraphs of trailing
zeros and `drv_bss` puts them back.

**The hard part was WHEN the loader learns the size.** `drv_load` claims from
the size the directory entry reported *before a byte is read* — which is the
property `drivers/os88drv.inc`'s banner named as the reason there was no bss in
the first place. Three ways out were weighed and two are refused:

- **peek the header first** — an extra `int 13h` per driver, on every boot,
  which at ~200 ms a call costs far more than the ~150 ms of decode this whole
  item was worth;
- **use the KB rounding slack** — arithmetic kills it. Moving *b* bytes out of
  the file needs `roundup1024(file − b) ≥ file`, so **no scheme of this shape
  can ever move more than 1023 bytes**, and `ether.drv`'s ceiling works out at
  257. Useless for a 4 KB buffer;
- **over-claim and shrink** — which is what shipped. The claim is padded by a
  fixed `DRV_BSS_KB` = 4, and `drv_bss` hands the slack straight back through
  `mem_regrow`, whose shrink path (SPEC.md §50.3 path 1) "changes the record's
  length and that is all". No extra read, no permanent memory, and a refused
  shrink costs only the slack until the driver detaches.

**Only the TRAILING run is stripped, so this is 6,672 of §12.6's 14,648.** The
other ~8 KB is interior — `saver.drv` is 5,360 zero bytes and only 6 of them
are at the end — and getting at it means moving each driver's zeroed buffers
to the end of its own source, about 21 runs across the twelve. That is a
per-driver change with a per-driver risk and it is deliberately not in this
wave; the mechanism is built and any driver that reorganises gets the benefit
for free.

**And a bug worth recording, because the fix is the whole lesson**: the first
version rounded the paragraph count UP. `sound.drv` has nine trailing zeros, so
it stripped sixteen bytes — seven of them real code. `t_drvmem` now asserts
that the stripped file plus its bss reproduces the assembler's own output byte
for byte, which is the invariant that makes the transformation safe rather
than merely plausible.

### 13.3 What wave 3b settled

**The saving is bigger after 3a, not smaller.** Stripping the trailing zeros
first took 6,672 bytes off, and compressing what is left is still worth
**19,697** — because the interior zeros §12.6 measured are exactly what LZ4
eats. `saver.drv` is the extreme: 13,797 bytes to 7,949, 57.6%, and 5,354 of
its zero bytes are interior with six at the end.

**A driver could not use the package path, and the reason is WHEN each loader
knows its sizes.** `ld_run_body` validates a header from a one-sector peek and
sizes its region before it reads, which is what lets it read high and expand
downwards in place with a 2-byte margin. `drv_load` claims from the size the
directory reported *before any read at all* — the same property that made
wave 3a's bss awkward — so the image size is unknown until the file is in
memory and the claim is already wrong. Two claims, the second freed
immediately, was cheaper than a peek at ~200 ms a driver.

**Modules are left out on their own arithmetic.** `CTRL`, `FORMAT` and
`CLONE.DRV` are 1,953 bytes between them, they go through `mod.inc` rather
than `driver.inc`, and §12.6's warning applies: a module is *this kernel's own
code* validated by two stamps, which would have to be computed over the
uncompressed image. That is a second loader's worth of care for a tenth of
the win.

**The gate could not assert the thing it most wanted to.** `lzdrv` compares
the expanded image byte for byte, but not the bss: by the time a driver's row
has a segment it has ATTACHED, and its bss is its working memory. A first
version checked that the far end of the bss was still zero and failed on a
single `0xFF` nine bytes in — the Ram Disk's own state. What stands in for it
is the three driver probes, which a driver handed a bss full of floppy
leftovers does not answer, and `t_drvmem`'s host-side check that the stripped
file plus its bss *is* the assembled image.

**Gates each wave needs**, and wave 1's is the one that must exist before any
of the others: a row that feeds each decoder a **truncated blob, a zero
offset, an over-long match and a match reaching below the region base**, and
asserts a refusal rather than a hang. `tests/pkgfence.py` is the existing
gate of that shape to extend. **§7.3's bounds are written and measured for
size and speed; that they refuse is still an assertion, and it is the first
thing to make true.**

---

### 13.4 What wave 5 corrected — and the ceiling that had to go

Wave 5 was designed with a **64KB ceiling** on the transparent read, on the
reasoning that `lz_decomp_x` works inside one segment and a bigger file is the
application's to expand with `OSAPI_DECOMP` and a claim it sized itself. That
was the wrong reading of the requirement, and the file it excluded is the one
the whole feature is for.

**`BEVERLY.MOD` is 116,085 bytes.** LZ4 takes it to **42,169 — 36.3%**; LZB to
**38,289 — 33.0%**, which is 3.3 points and is the largest single data point
§14.1's open A/B has (the two disks are 42 and 38 clusters, so LZB buys four
clusters here and costs ~4x the decode time). At the
360KB geometry it is **114 of 354 clusters**, which is why §24.4 gives it a
floppy of its own; compressed it is **42**, and Tracker plus the module now
build onto one disk with 294 clusters left. In time it is ~145 sectors, about
**5.2 seconds of floppy against ~1.2 seconds of decode**. Nothing else in the
tree comes close.

**Blocking the stream was measured and rejected.** The cheap way to cross 64KB
is not to: compress in segment-sized blocks and decode them one at a time with
the 16-bit decoder untouched. Two blocks of 61,440 cost **18,766 bytes — 44% of
the entire win** — because a MOD's sample data matches back tens of KB into
itself and a block boundary throws that history away. Measured per block:

| | bytes | ratio |
|---|---|---|
| one stream | 42,169 | 36.3% |
| `[0:61440]` alone | 25,470 | 41.5% |
| `[61440:116085]` alone | 35,457 | 64.9% |
| ...as part of the whole | 16,699 | 30.6% |

The second half compresses to **less than half** as much when it can see the
first. That table is the argument.

**So the decoder crosses, and three of the four pieces were nearly free**
(SPEC.md §20.14.5). Bumping `ES` by 4,096 paragraphs is the exact 64KB step, so
a wrapped `DI` needs no arithmetic; a match reaching below the segment base
falls out of the **borrow** on `SI = DI − BX`, which wraps to precisely the
right offset; and the match-offset bound stops being a compare at all once the
output has passed 64KB, because every 16-bit offset is inside what has been
produced by then. Only the straddling copy needed real code, and it is out of
line in `lz_cross` — **once per 64KB of output**, so a whole segment of
literals and matches pays for it once.

**The input deliberately does NOT cross.** `CX` stays 16 bits and `SI + CX`
wrapping is refused at entry, which keeps the source end a plain offset in one
segment — and that compare is tested twice per sequence and is the hottest one
in the routine. What it costs is that a file compressing to 64KB or more is
stored plain, which is the right answer to a file that compressed that badly
anyway; `cz_wrap` enforces the same rule at the other end and `t_lzfmt` proves
it does.

**Three smaller things the building corrected:**

- **`AL` is the format and `AX` was the segment.** `dskw_czexp` banked the
  source segment in `AX` and then loaded two header bytes with `mov al`, which
  ate its low half. The symptom was a decode that started, produced real text
  and then refused on a garbage match offset — a failure that looks exactly
  like a broken decoder and was a clobbered register.
- **`OSAPI_FILE_READ_AT` stays RAW, and that is load-bearing rather than an
  omission** (§20.14.3). It exists for a file bigger than the caller's claim,
  which is precisely the case in-place expansion cannot serve; and being raw is
  what lets a chunked copy be byte-exact, so a copy of a compressed file is
  still a compressed file.
- **The hint is derived from the bytes being written, never propagated**
  (§20.14.4). The first design had `fcp_relink` carrying the whole 32-byte
  entry — which works for a MOVE and not for a COPY, and a copy that silently
  turns a 2,682-byte document into 1,943 bytes of gibberish is the worst defect
  this feature could have shipped. `dskw_czstamp` reads the first eight bytes
  of what is being written, so a save, a copy and a package's own write all get
  it right without knowing the mechanism exists.

### 13.5 What wave 5 cost

Measured tree-to-tree — the working tree against the commit wave 3b left, both
built by the same `make`, which is the reading CLAUDE.md's own note about
`kernsize`'s stale-baseline trap asks for:

| section | wave 3b | wave 5 | |
|---|---:|---:|---:|
| `.text` | 50,466 | 50,467 | **+1** |
| `.bss` | 5,922 | 5,928 | **+6** |
| `.cold` | 37,342 | 37,920 | **+578** |
| | | | **+585** |

**Seven bytes of it are inside `KERN_CODE_MAX`**, which is the constant that
cannot be raised (CLAUDE.md's memory rules): one instruction in the write
path's caller and the three words `dskw_uz`/`dskw_uzhi`/`dskw_czseg`. Everything
else — the whole `.czfile` arithmetic, `dskw_czstamp`, `dskw_czexp`, `lz_take`,
`lz_cross` and both formats' crossing arms — is `.cold`, which has its own
segment and costs that budget nothing (SPEC.md §2.6).

That is **the entire transparent-file feature**: reading any compressed file,
deriving the hint on every write, reporting the unpacked size through
`OSAPI_FILE_FIND`, and a decoder whose output is no longer bounded by a
segment. For 585 bytes it takes BEVERLY.MOD off a floppy of its own.

### 13.6 THE WHOLE SET, COMPRESSED — `make zset ZFMT=lz4|lzb`

`PKGZ=` is the fleet-wide form of `--compress`: every shipped package and every
shipped driver, plus the four data files, in one build. `zset` pairs it with
`COMPRESS=` and drops the result in `build/z-<fmt>/`, because separately the
two are a trap — a disk built with one and not the other is a floppy of
programs that will not open.

**Measured, on the 360KB geometry**, in clusters of the 354 a disk has:

| disk | plain | LZ4 | LZB |
|---|---:|---:|---:|
| apps360 | **351** *(and no module)* | **331** | **294** |
| os8088-360 (system) | 326 | 293 | 279 |
| media360 (the module alone) | 115 | *gone* | *gone* |

**The two-disk split of §24.4 does not exist on a compressed set.** The apps
disk today is **three clusters from full without `BEVERLY.MOD` on it**; with
LZ4 it holds the module *and* has 23 spare, with LZB 60. That is the whole
§6.1 argument, built rather than modelled.

**Three packages ship plain and each says why.** `--compress-if` is what a
fleet flag needs: `HELLO.O88`'s in-place layout would make the loader claim
more than it does today (747 bytes, saving 89, needing 1,170 of a 1,024-byte
region), `OS88NET.COM` is an MS-DOS binary for the machine at the other end of
a cable, and `ASSOC.DAT` is the mount's own cache. A per-package `--compress`
stays STRICT — there the refusal is the answer to a question somebody asked.

**`lzship` is the row**, and it exists because this configuration has a failure
mode no single-subject row can have: the system disk's **nine drivers expand
during BOOT**, by a kernel that has not finished starting, into a heap still
being laid out. It asserts the boot, the drivers attaching off `drv_tab`
rather than off the screen, a compressed package opening from the shipped
disk, and all 116,085 bytes of the module out of `MEDIA/` on the apps disk.

#### 13.6.1 What building it caught, and what caught it

The four wrapped **data** files are the only compressed artefacts whose rule
does not pass through `$(PKGZSTAMP)`, whose *name* carries the format. One
`build/zdata/` for both meant `make zset ZFMT=lzb` after an `lz4` one found
`BEVERLY.MOD` up to date and **shipped the LZ4 module on the LZB disk**.

What caught it is the kernel doing exactly what SPEC.md §20.13.3 requires: a
build that does not carry a format **refuses** rather than running the one it
has. `lz_decomp_x` was entered with `AL = 0` on an LZB-only kernel and took
`.refuse`; the user-visible symptom was `Disk error` on a module that had been
fine an hour earlier. That rule was written for a hostile file and its first
real catch was a stale build artefact — which is the better argument for it.

### 13.7 What wave 4 corrected

**Almost all of this already existed** — §12.4 said so and it was true — but
the two things it did not reach are the two that took the time.

**1. `len` stays unpacked, and `zkb` takes the packed length.** That is the
one word a file-backed row has spare, and the macro already forbade a KB
there. It also has two other tenants — a scratch row's KB and a lazy row's
fetched segment — so `OP_COMP` is refused with `OP_ZERO`, `OP_LAZY` and
`OP_XMS`, each in the macro, in `os88pkg.py` and in SPEC.md §20.12.7.

**2. THE CLAIM STOPS BEING THE DISK'S LAYOUT, and that is the real change.**
`op_seg` answered a part's address from its own sectors — `base + slack/16 +
(off − first) × 32` — which is exactly what compressing one part breaks: the
following parts move. So the packed run is read **high** and `op_unpack` walks
it down, and `op_seg` walks the table instead of doing arithmetic. **The two
agree when nothing is compressed** (the packer lays part *i* at
`roundup512(len)` past *i−1*, so the sum *is* `(off − first) × 512`), which is
why the fast path stays and why a package with no compressed part is unchanged
to the instruction — `multiseg` and `mseg360` still pass 7/7, and every part
lands at the same segment it always did.

**Three defects, and the last one is the one worth having found:**

- **`AL` carried a segment's low byte instead of the format.** `op_unpack`
  banked the source segment in AX and never read the table's format byte, so
  the decoder was handed `0x64` and refused every stream: *"A part will not
  unpack"* on a package whose bytes, lengths and pointers were all correct.
- **`op_want` was cut from the SECTOR count.** A part is padded up to a sector
  in the file, but the last one is the end of the **file** — so a packed
  length that is not a sector multiple leaves those bytes not merely unread
  but **absent**, and asking for them is *"Cannot read my parts"*. C64 found
  it and `mseg` could not: its last part is `OP_LAZY`, so the file continues
  past the carve and the rounding was harmless. `op_bend` is the run's exact
  byte end and `op_want` is cut from that.
- **`R` was 512 short, and nothing would have caught it.** Both totals are
  measured in whole sectors, so a row contributes `roundup512(len)` to one and
  `roundup512(packed)` to the other — while what row *i* actually needs is
  `(len − packed) + its own margin` in **exact** bytes. The telescoping leaves
  the gap short by up to 511 when `packed` lands one byte past a sector
  boundary and `len` lands on one. Both fixtures happened to clear it
  (`mseg`'s tightest row had 130 bytes of slack), so this was a rare,
  data-dependent overwrite that the gates would have passed for months. 512
  closes it for every row at once, **once** and not per part.

**What it cost.** Nothing in the kernel, like every other part flag: `op_load`
already had the decoder through `OSAPI_DECOMP`, and `op_unpack` is SDK source
that only a package with a compressed row assembles a use of. `apps/c64`'s
`C64.O88` goes **58,833 → 55,714 bytes** and its floppy from 88 to 85 clusters
at 360KB.

**The gates are the ones that already existed.** `msegz` is `mseg.asm` built
`-DMSEG_COMP`, so all seven of its per-part proofs — a signature, a far call
answering a computed value, and a rotating checksum over the part's data —
have to come out identical; and `c64part` reads the ROM's bytes out of the
guest and runs the 6510 on them. Neither is a new assertion, which is the
point.

### 13.8 THE KERNEL ITSELF

**§14.5 refused this and the refusal was right when it was written**: *"the
boot blob has 107 bytes free against a 115-byte decoder"*. Two things moved
since, in opposite halves of that sentence. `.boot2` shed 200 bytes in the
splash's own size pass (SPEC.md §15.3.8.5.1), so the free space is **374**;
and the decoder that goes in there does not need §20.13.4's bounds at all,
because **we are the only thing that ever makes this stream** — the file is
packed by `tools/os88kz.py` in the same `make` that assembles the kernel it
holds, and a corrupt one fails §18.93.1's canary and §2.9.12's blob sum long
before a match offset could be believed. Unbounded LZ4 is 39 bytes and 19% of
its time cheaper, and it is legitimate here and nowhere else on the machine.

**The layout is three parts and the middle one is why it works:**

```
KERNEL.SYS = [ the blob, 8 sectors ][ 9 sectors PLAIN ][ LZ4 blocks ]
                stage 2 + .ovl        SPL_RESIDENT       the rest of .text
```

`SPL_RESIDENT` = 9 is already a published constant: it is how far into the
kernel the loading screen's **first tick** reaches (`viddet.inc`), so those
sectors have to be real code before the read finishes whatever else happens.
Leaving them plain is therefore not a concession — it is the same nine sectors
the splash already depended on, and it means the decode happens **after** the
last `int 13h` rather than interleaved with it.

**Stage 2 reads the packed tail HIGH and expands it DOWN**, which is §21 step
6's in-place trick one level below itself: `R = roundup512(unpacked − packed +
margin)` = 20,992, the destination is bumped by `R/16` paragraphs before the
body read, and `kz_expand` walks each block down into place. Two things about
that number are load-bearing and both were got wrong first:

- **R is rounded to 512 and not to a paragraph.** `read_run`'s third bound is
  the 64KB DMA page, and it finds the page edge by shifting the destination
  offset right by 9 — arithmetic that assumes 512-alignment. A paragraph-
  aligned R gives a truncating shift, which gives a **zero-sector run**, which
  is an infinite loop at boot rather than a wrong picture.
- **`b2_runmax` is a DIVISOR, not a cap.** The first version bounded the plain
  head's read with `mov word [b2_runmax], KZ_HEADSEC`, which does not shorten
  one read — it redefines the geometry `read_run` divides the LBA by. The head
  is bounded by `[b2_left]` alone.

**The canary needed no move at all**, which §14.5 had budgeted for. `KSIG_OFF`
is a *memory* offset from `KERNEL_SEG` and the Makefile takes the word from
the file at `KSIG_OFF + BOOT2_PAD`; with the body loaded `R` higher, the same
word is at `es + KZ_RPARA` and the same `KSIG_OFF` — the `+head/16` and the
`−head` cancel exactly. So the check still runs **mid-read**, on the packed
bytes, which is what it is for: it proves the transfer landed where it was
told, not that the kernel is correct.

**What it costs and what it buys.** 180 bytes of `.boot2` (2,250 → 2,430 of
2,624, leaving 194) and **nothing resident**: `mem_unblob` gives the whole
blob back to the heap at the end of `kmain`, so a machine that has finished
booting carries no part of this. The blob is `BOOT2_SECS` sectors either way,
so it costs no disk either. On disk, `KERNEL.SYS` goes 104,365 → **83,632**
bytes and 204 → **164** sectors of the 360KB system disk.

**With §13.9's default underneath it the system disk is 270 of 354 clusters**,
against 326 before either — 56 clusters, 15.8% of the floppy, and the two
halves are exactly additive: 36 for the packages, the drivers and the readme,
20 for the kernel.

**And it boots FASTER**, measured on MartyPC's 4.77 MHz 8088 with a period
BIOS, CGA, from the 360KB floppy:

| phase | plain | `KZIP=1` |
|---|---:|---:|
| `post` (the ROM) | 4,817.5 ms | 4,817.5 ms |
| `boot: int 13h` | **7,098.0** — 220 sectors, 15 reads, 3,279 ms mechanical | **5,819.8** — 173 sectors, 14 reads, 2,717 ms |
| `boot: splash` | 216.0 | 205.6 |
| `boot: sector loop` (the decode) | 70.4 | **869.4** |
| **whole boot, from reset** | **13,877.9** | **13,278.8** |
| the kernel's own `boot_ticks` | 165 | 154 |

**−599 ms, and the shape of it is the interesting part.** 47 fewer sectors is
only 562 ms of *mechanics*; the other 716 is the BIOS's per-call and
per-sector overhead, which is the term §10 item 1 keeps asking about and here
is measured rather than modelled. Against that, the decode is 799 ms for
95,661 bytes — **39.9 cycles an output byte**, against the kernel's bounded
LZ4 at 50.6, which is §20.13.4's 19% arriving exactly where it was priced.

−1,278 + 799 = −479, and the last 120 ms is not in this table's subject at
all: the splash ticks twice fewer (−10) and **`drv_boot_x` comes in 108 ms
faster on a change that does not touch it**, because the nine drivers sit 40
sectors earlier on a disk whose `KERNEL.SYS` shrank and the seeks to them are
shorter. Worth writing down because it is the kind of secondary effect that
gets attributed to the change under test.

**`tests/kzboot.py` is the gate and it asserts the image, not the desktop.**
A decoder that gets one match wrong still boots, still draws a desktop, and is
a kernel with a wrong instruction in it — so the row breaks at `KERNEL_SEG:0`
and compares all 95,661 decoded bytes against the file. That breakpoint is the
one moment the image is exactly what the file says: stage 2 writes
`boot_cylrun` at +4 and the boot timer at +12 before it jumps, and a
comparison taken at the desktop reports **103** differences on a boot that
went perfectly.

**…and then it became the default**, on every geometry, on `kern_small` and on
the hard disk. `NOKZIP=1` is the A/B. §13.10 is what that took.

### 13.9 …AND THEN LZ4 BECAME THE DEFAULT

**`PKGZ ?= lz4`.** After a field cycle on the 360KB set, every shipped package,
every shipped driver and every shipped data file is compressed by a plain
`make`; `make PKGZ=` is the uncompressed build and the A/B. SPEC.md §20.13.5
is the contract and carries the disk table. LZB is **not** shipped and both its
knobs stay — §14.1 item 1 is settled below.

**`README.TXT` joined them**, which needed the one split in the change:
`$(SYSDOC)` is compressed and goes on every FAT volume, `$(SYSDOCRAW)` is
plain and is the live CD's host-visible copy (SPEC.md §80.2). It gains the
manual no room — `np_load`'s 16 KB is claimed against the **unpacked** size —
so `checkreadme.py` rule 2 still measures the CRLF source.

**Three host-side gates were reading the FILE and calling it the IMAGE**, and
all three were wrong in the safe direction, which is the kind nobody notices:

- `t_drvmem` measured every driver's resident claim off the compressed file,
  under-reporting the Drivers page's KB column by the compression ratio;
- `t_pkg` refused a v4 header whose `image` exceeds its file — which for a
  driver **is** the compression signal, there being no flags byte (SPEC.md
  §20.13.2) — and the v3 compressed branch above it was testing bit 3 of a
  byte that means the CLASS in a driver;
- `t_appsmall` hashed a compressed artefact against a fresh assembly and
  reported the wrong build arm.

The fix is one function per container — `os88pkg.image_unwrap` and
`os88drv.image_unwrap` — and the rule for which a caller wants: **a gate
comparing a floppy against what the build produced wants the file; a gate
about a size field, the bss arithmetic or an assembly wants the image.**

**And one artefact had to be taken back out** (SPEC.md §20.13.5.1).
`HDDTOOL.DRV` is the only file on either floppy whose reader is not a loader:
`HDD.DRV` reads it with `OSAPI_FILE_READ`, which hands back the disk's bytes.
The reason it is worth a paragraph rather than a line is *how it fails* — the
32-byte header crosses compression verbatim, so all seven of `hd_tool_check`'s
tests pass on a compressed tool and the driver far-calls `[es:6]` into the
stream. A crash on Format or Install, on a machine with a hard disk, which is
not the machine a shipped floppy gets tested on. Its rule names `os88drv.py`
rather than `$(OS88DRV)`, and `t_pkg` asserts it, because "who loads it" is
not a property of the file. It cost four of 354 clusters at 360KB, and
buying them back meant a peek-then-read or a second claim in `HDD.DRV` —
until a compressed driver became a `'CZ'` file and the read the tool already
made became the transparent one, at which point it was one word in a
Makefile rule (13.14.5, taken).

### 13.10 MAKING THE COMPRESSED KERNEL THE DEFAULT

Five things, and only one of them was the interesting one.

**1. The file and the image needed two names, unconditionally.** The first
build had `KERNFILE = $(if $(KZIP),kernel.sys,kernel.bin)` and twenty-five
rules naming one or the other — which is twenty-five places for the two to
disagree, and they DID: `boot360.bin` was taught the packed file and
`ether360.img`, `lzdrv360.img`, the 720KB image and four bench disks were not.
So `$(BUILD)/kernel.sys` is now built always (a copy under `NOKZIP=1`),
`KERNFILE` is unconditional, and every rule that puts a kernel on a volume
names it. **The gate is `t_image`**, which compares `KERNEL.SYS` on every
shipped image against `build/kernel.sys` — the same file — so a half-swept
Makefile fails the fast tier rather than at somebody's `Disk error`. What that
gate does NOT reach is a disk `all` never builds, and three of them wanted the
same sweep afterwards: `emu.img` and `vmmouse.img` (SPEC.md §9.11.7) and
`audio-hdd.img` still named `kernel.bin`, so each got a boot sector built from
the packed file in front of the unpacked one. `vmmouse` is the row that said
so, and it said it in seven assertions about a pointer — a kernel whose head
is read as a decoder's input boots far enough to be read and answers nothing
right. A boot
sector built from one file in front of another fails twice over and neither
failure names itself: §18.93.1's canary and §2.9.12's blob sum come from the
file the sector was built from, and stage 2's own sector count is assembled
into the kernel, so it reads 156 sectors of a 204-sector image.

**2. `os88sym` had to stop needing to be told.** The four `KZ_*` numbers are
properties of a file that does not exist when the kernel is assembled, so
nobody can pass them by hand — and with this on by default every tool that
resolves a symbol would have met the byte-identity refusal about a kernel that
is perfectly fine. `os88kz.py` writes them beside the kernel it packed and
`os88sym` reads them from there. That deleted the Makefile's `$(KZSYMS)` and
its four call sites: the json IS the build.

**2a. …and a second reader had to be told to stay out of it.**
`tools/os88build.py` gives a knob row a private build tree and hands
`os88sym` the defines that tree was assembled with, scraped off `make -n`'s
own assembler line. Two things about that line are true and neither is
obvious: it is the **pass-1** command, so its `KZ_SECS` and `KZ_RPARA` are the
placeholder zeroes rather than the numbers `build/kernel.bin` on disk was
finally assembled with (§2.9.13's pass 2 rewrites it) — and the scrape kept
only the `-D` NAME and dropped `=value`, which no knob had needed until this
one. So every private tree published `KZIP` with no value at all, `os88sym`
saw `KZIP` already named and left the json unread, and `boot2.asm` answered
six `expression syntax error`s about a kernel that builds perfectly.
`defines_for` now keeps a define's value — ten knobs assemble to `-DX=<n>`
and had been quietly losing theirs — and drops the `KZ_*` family outright,
which is the Makefile's own rule written on the other side of the fence: the
four numbers belong to the json and to nothing else.

**3. THE HARD DISK IS A THIRD LOADER, and that was the interesting one.**
`boot/boothd.asm` never enters stage 2 — it loads the blob, loads `.text` and
jumps — so a packed kernel arriving that way is jumped into, and the sector
has **23 spare bytes**. Five is what it took, because the two loaders'
arithmetic already agrees: one run to `KERNEL_SEG + R` puts the head at the
bottom of it and the blocks at `KERNEL_SEG + R + head/16`, which is exactly
where the floppy's two reads put them. So the packed arm is one changed
immediate and a far `call BLOB_SEG:KZ_HD`; `kz_hd` moves the head down and
calls `kz_all`, which is the block loop made a routine so there is no second
copy of it (SPEC.md §2.9.13.5). `tests/hdboot.py` installs and boots, so it
was covered the moment the default flipped, and the live USB and CD inherit
the same VBR.

**4. `SPLSTARS=1` had to be made exclusive with it** (SPEC.md §15.3.8.5.2).
The twinkle's `.boot2` is 2,568 against the spinner's 2,250 and the decoder is
180 more — 2,748 of `OVL_AT`'s 2,624. The escape the `%error` names is a ninth
blob sector, which costs every shipped image 512 bytes and brings back the two
blob lengths §15.3.8.5.1 deleted; that is the tail wagging the dog for a look
knob that ships in no configuration. What the shipped build lost is margin,
and it is worth stating: `.boot2` has **194 free bytes, not 374**, and `.ovl`
has 55. The next thing that wants blob space is a `BOOT2_SECS` conversation.

**5. The stamp had to name the EFFECTIVE setting.** `$(VIDSTAMP)` carries
`$(if $(KZIP),-kz)` and not `$(if $(NOKZIP),…)`, because a stamp built from
the request has the same name before and after the day a default changes —
which is a `build/` full of an unpacked kernel that `make` believes is
current, and every image shipped from it wrong. It cost a rebuild to notice
and it is one line.

**What it is worth, on the four geometries** — `KERNEL.SYS` 204 → **164**
sectors on all of them, `kern_small`'s 153 → **124**, and the 360KB system
disk **326 → 270** clusters of 354 with §13.9 underneath it.

### 13.11 WAVE 6 — the design, before a line of it

§12.3 has the shape (`COMPRESS.DRV`, on demand, 307 bytes and an 8KB table at
~290 cycles a byte). Three things came up when it was time to build, and two
of them cost nothing.

**1. `.O88` CAN be compressed, and it is worth the ~50 bytes.** The first
reading of this was that the verb would have to learn the package format; it
does, and the whole of it is arithmetic:

- read four header fields — version 3, flags at +3, `image` at +8, `bss` at
  +10;
- `pre = 32, +64 if flags bit 0 (icon), +16 if flags bit 1 (assoc)`, so 32,
  48, 96 or 112 — the bytes the mount reads out of the first sector without
  opening the file, which must stay clear;
- compress `image[pre:]`, which is the same call on a different byte range;
- refuse on flags bit 2 (PARTS — `C64.O88`'s case, §20.12.7 is its own
  answer), on bits 3 or 4 already set, on `packed >= image`, on a margin over
  `LZ_MARGIN`, and on
  `roundup512(image − packed + LZ_MARGIN) + packed > roundup512(image + bss)`
  — the in-place layout must fit the region the loader was going to claim
  anyway, which is the one refusal that is not obvious and is still one
  subtraction;
- write the header with bits 3 and 4 set, then the clear prefix, then the
  stream.

**The read half is already built and shipped**, which is the real argument:
`ld_run_body` has expanded compressed packages since wave 2 and `lzload`
tests both formats on them, so compressing one on the machine produces a file
the existing loader already handles. And it needs no interaction with the
`'CZ'` hint — `dskw_czstamp` looks for the `'CZ'` magic in the bytes being
written and a `.O88` begins `'O8'`, so the mark is correctly not written.

**`.DRV` is refused, and NOT because the format is hard** — the driver
container is simpler than the package one (§20.13.3.1: the header crosses
verbatim, then a format byte and the stream, and `image` at +8 stays the
unpacked size). It is that a `.DRV` is read by **four loaders and two of them
do not expand**: `HDDTOOL.DRV` by `HDD.DRV` through `OSAPI_FILE_READ`
(§20.13.5.1), and `CTRL`/`FORMAT`/`CLONE.DRV` by `mod_need`, which wants a v5
module image. Allowing `.DRV` therefore means a name list of four exceptions,
which is the shape that rots.

**`KERNEL.SYS` is refused too**: the boot sector reads it raw, and §2.9.13's
numbers are assembled into stage 2 at build time.

**2. Compressing an already-compressed file needs no special case at all.**
`OSAPI_FILE_READ` on a `'CZ'` file hands back the UNPACKED bytes (§20.14), so
"read, compress, write" is the same code for a plain file and an LZ4 one — and
LZ4 → LZB, which is what the verb is *for* on a shipped disk, falls out of it.
The only thing to add is the refusal when the result is not smaller, which is
§47's grey-a-fact and is needed anyway.

#### 13.11.0 What parse the machine can actually run — measured first

`os88lz.lzb_compress` is a **shortest-path parse**: a cost array over every
position and a chain matcher at depth 128. That is `2n` bytes of `dp` plus
`2n` of chains — 464 KB for `BEVERLY.MOD` — so it is not what the 8088 will
run, and the machine's stream will be *bigger* than the host's. The question
is how much, and whether LZB is still the right format once the parse is one
the machine can afford. **Measured, before a line of the encoder:**

| | LZB 4K greedy | LZB 4K lazy | LZB 8K lazy | LZ4 4K greedy | *host LZB* | *host LZ4* |
|---|---:|---:|---:|---:|---:|---:|
| `README.TXT` | 54% | 53% | 52% | 66% | *43%* | *54%* |
| `PAPER.TEX` | 72% | 71% | 70% | 83% | *62%* | *78%* |
| `DEMO.HTM` | 62% | 61% | 60% | 73% | *52%* | *66%* |
| `calc.bin` | 79% | 79% | 78% | 83% | *71%* | *80%* |
| `notepad.bin` | 81% | 81% | 79% | 87% | *71%* | *82%* |
| `paint.bin` | 77% | 77% | 75% | 83% | *67%* | *78%* |
| `BEVERLY.MOD` | 42% | 42% | 40% | 46% | *32%* | *36%* |

("4K"/"8K" is the hash table in ENTRIES — 8 KB and 16 KB of RAM. "lazy" is one
extra probe at `i+1`, no extra memory. Every one round-trips.)

**Three things fall out, and one of them is a decision.**

1. **LZB is right, and the reason is the LITERAL.** Greedy LZ4 on the machine
   is 12 points worse than greedy LZB on text (66% against 53%) because LZB's
   literal is 9 bits wherever it sits and its minimum match is 3 where LZ4's
   is 4 — so LZB wins exactly where a weak parse leaves many literals. The
   4× decode is still the price, and it is still the user's to pay
   deliberately.
2. **The machine beats the BUILD on six of seven files** — 1 to 7 points
   better than the LZ4 those files ship as — so "compress this again" is a
   real gain on a shipped disk and not a wash. (§13.11.0.1 makes that 3.9 to
   10.3 on all seven that the verb can reach; and re-measuring these rows
   against the shipped encoder put every one of them within ~1 point of the
   figure in this table, which is what says the table is describing the same
   parse.)
3. **AND IT LOSES ON `BEVERLY.MOD`**: 40% against the 36% the build's LZ4
   already achieves, because the host's LZ4 has a full chain matcher and the
   machine has one candidate per slot. So **re-compressing an
   already-compressed file can make it BIGGER**, which is exactly what
   §13.11.1's `P_new < P_on_disk` guard is for — it turns out to have a
   common trigger rather than a theoretical one, and it has to be checked
   before the delete or that file is lost.

**So the encoder is: one candidate per hash slot, LAZY, 4,096 entries.** Lazy
costs one probe and buys 0–2 points; going to 8,192 entries buys another 1–2
for 8 KB more RAM, which on the machine this verb exists for is the wrong
trade.

#### 13.11.0.1 — AND THAT CONCLUSION IS WRONG. It is a CHAIN matcher

The paragraph above rejects chains on one sentence: *"a chain would need two
bytes per input byte of `prev` array — 128KB for a 64KB file"*. **That is true
of a chain over the whole file and false of a chain over a WINDOW**, which is
the only kind anybody builds — `prev` is indexed `i & (window−1)`, so it costs
`2 × window` bytes whatever the file is. §12.3.1, four hundred lines further
up this same document, had already said so and already recommended *"depth 16
over a 16,384 window — 32 KB"*. The measurement above was taken and read
without it.

**Re-measured over the same seven files, and this is what ships** (SPEC.md
§20.15):

| parse | scratch | ratio | vs. the LZ4 those files ship as |
|---|---:|---:|---:|
| one slot, lazy, fill — 13.11.0's answer | 8 KB | 74.5% | +0.8 to +6.9 |
| **chains, depth 16, window 16,384, no lookahead** | **40 KB** | **68.9%** | **+3.9 to +10.3** |
| chains, depth 16, window 4,096 (`kern_small`) | 16 KB | 71.4% | — |
| *the host's shortest-path parse, for scale* | *(host)* | *63.9%* | *—* |

**Two findings inside that, both of which change the code:**

1. **The lookahead is DOMINATED, not merely marginal.** At every work budget
   measured, a deeper chain with no lookahead beats a shallower one with it —
   depth 8 plain is −5.37 points at 2.49× the work where depth 4 lazy is
   −5.05 at 2.83×. So `lazy` is gone, and with it `cmz_probe_ro` and the
   insert-suppression flag that made it correct.
2. **Filling the table across a match is worth 2.5 of the 5.7 points**, which
   is far more than it was worth without chains (1.2–2.3). It is what makes
   the chain dense.

**What it costs is 40 KB of the caller's block and about 3.2× the parse's
work** (bytes compared; modelled, not measured on iron). The window is the
verb's dial — it drops until the claim fits — so a 128 KB machine runs the
same encoder over less history rather than a different one.

**The lesson is the one §12.3.1 was already an example of**: this document is
long enough that a measurement can be taken against an argument the document
itself has already refuted. `os88lz.lzb_compress_machine` now exists so that
the next one is re-run rather than re-derived.

#### 13.11.1 The write, and why the threshold is smaller than it looks

**`OSAPI_FILE_WRITE`'s replace path is already safe by construction.** It
allocates the NEW chain while the old one stays allocated, commits one
directory sector, and frees the old chain only then (`dskw_oldclus`, §18.6).
There is no moment where the file does not exist.

**So the space it needs is `P`, the COMPRESSED size — not a second copy.**
That is the number worth having before designing around it: `BEVERLY.MOD`
wants ~42 KB free, not 116. On a full 360KB disk that can still bite, but it
is a different order of problem, and the verb can say it exactly — it knows
`P` before it writes anything, because the output is whole in RAM by then
(§12.3's memory model).

**When it does not fit, delete-then-write is the right fallback and it needs
one guard.** The output is already in RAM, so the risk window is a single
write of a smaller file — but the free space after the delete is the OLD
file's, so `P_new < P_on_disk` has to be checked *before* the delete or a
file that compresses worse than it already is would be lost outright. That is
exactly the LZ4 → LZB case where LZB happens to lose, and it is one compare.

It should be a QUESTION and not a fallback the machine takes by itself
(§47): the numbers are known, so the box can say *"There is not enough room
to write the smaller copy alongside. Delete X first? If the machine stops
during the write the file is lost."* with Cancel as the default.

**The better answer, when the tight case turns out to be common, is
SHRINK IN PLACE**: keep the existing chain and directory entry, write the new
bytes over the file's own clusters, then free the tail and update the size.
Peak extra disk **zero**, and a better failure than delete's — the entry never
disappears, so a power cut leaves a file with its name and its old size and
wrong content, which is visibly broken rather than gone. It is not wave 6's,
because it is a new write path in `dskw_*` and that is the code in this kernel
where a mistake costs somebody's disk rather than their afternoon.

#### 13.11.2 WAVE 6 AS BUILT — what the design got right, and the two it did not

**It ships** (SPEC.md 22.22, 20.15). `tests/lzcomp.py` is the gate and its
assertion is equality with a host reference — `os88lz.lzb_compress_machine` for
a `'CZ'` file and `os88pkg.compress_image` with that encoder passed in for a
package — so what is checked is the whole file, byte for byte, and not a ratio
or a round trip.

**Three things the design above got right and are worth confirming:**

1. **`.O88` is worth the arithmetic and it was about fifty bytes**, as §13.11
   said. The one thing it did not foresee is where the saving actually comes
   from: `os88pkg.compress_image` gained a `packer=` parameter, so the
   refusals, the clear prefix, the flag bits and the in-place layout are stated
   **once** and the test carries no copy of them.
2. **Compressing an already-compressed file needed no special case**, exactly
   as written — and `tests/lzcomp.py` proves it the hard way: the plain leg and
   the LZ4 leg produce the IDENTICAL file.
3. **The refusal by name is not a name list.** A `.DRV` and `KERNEL.SYS` are
   hidden + system, so §19's species filter keeps them out of the listing and
   `dskw_write_x` would refuse them anyway. Two independent gates.

**And two things it got wrong.**

**§13.11.0's parse was the wrong one**, and §13.11.0.1 is that correction.

**And the verb found a kernel bug on its first run.** `drv_vol_bank` banked
`osapi_file_here`'s answer, which is the CALLING INSTANCE's directory
(SPEC.md 19.2.1) and only falls back to the machine's own words when there is
no instance stamp — and the stamp is set for the length of any dispatched
callback, which is where every one of those brackets runs. So Compress read a
file on B: and wrote the result to A:. It was invisible for as long as both
callers worked by DRIVE; the first caller that writes a file BY NAME after a
`mod_need` found it immediately. SPEC.md 51.5.2 carries the correction.

**§13.11.1's write question is NOT built**, and the reason is worth writing
down: the guard it exists behind turns out to be the verb's own `it would not
get smaller` test. The new file is strictly smaller than the old one by
construction, so the space a delete would free always exceeds what the write
needs — which makes delete-then-write safe by construction rather than by a
check. What is left is a one-write risk window and a question to ask about it,
and the honest place to ask it is BEFORE any work: the verb knows the file's
size and the volume's free space at entry, so the worst case is decidable then
and no claim has to be held across a suspended interaction. That is the shape
to build if the tight case turns out to be common.

### 13.12 WAVE 7 — `Uncompress`, and the asymmetry that made it cheap

**534 bytes for the other half of the feature** — `.text` +78, `.cold` +456 —
against wave 6's 1,477 resident plus a 712-byte module. It is a third of the
price for a verb of the same standing, and the reason is one sentence:
**compressing needs an encoder that is not in the kernel; expanding needs a
decoder that already is.** `kernel/lz.inc` is resident because the loader, the
driver loader and every transparent read go through it, so `fm_c_uncomp`
spends no `COMPRESS.DRV`, takes no `mod_need`, and has no `not found` verdict
to say. §22.23 is the contract.

Making it a module was considered and refused for exactly that: it would have
*added* a system-disk requirement to a verb that has none.

**One arm of it is not code at all.** A `'CZ'` file is expanded by
`dskw_read_x` (§20.14), so that arm is a claim, a transparent read and a plain
write with nothing in between — the transparency wave 5 built for applications
turns out to be the whole verb here. Only a **package** needs work, because a
`.o88` is not a container: `fm_uncmp_pkg` is `ld_expand` between two regions
instead of inside one, and the format is read back out of the source header
once `DS` is the source segment, which is what saves carrying it through the
prefix copy.

**Two claims, and it is `image` that forces them.** Wave 6 sizes everything
from `U`, which the directory hint hands over before a sector moves. A package
carries `U` in its own header and there is no way to read 32 bytes of a file on
this machine — `dskw_read_x` reads the whole of it or answers `FERR_BIG`, and
`dskw_read_at` wants whole clusters (§18.4.4). So the package arm claims for
the file, reads it, learns `image`, and claims again. Peak is `P + U` against
an in-place scheme's `U + LZ_MARGIN`, and it is affordable by construction:
§22.22.2's claim on the same file is `2·U` plus tables, so **any machine that
could compress a file can expand it**.

#### 13.12.1 The right-click menu, and the leg that caught its own harness

Both verbs went onto `fm_ctx_file` at the same time, which is a requirement
rather than a courtesy: under `WF_FULL` a file-manager window has no menu bar
at all (§11.2), so a right-press is the only surface either one has there.

`tests/lzcomp.py` grew five legs — two round trips (a `'CZ'` file and a
package, both back to bytes the run itself wrote), the expanded package opened
again, a plain file refused, and the context menu driving `Uncompress` off
`fm_cxc_file` rather than off the bar. **A round trip is the weakest possible
assertion about an encoder and the strongest available one about this pair**,
which is why wave 6's legs are byte-equality against a host model and these are
not: `lzb_compress_machine` cannot say whether the decoder gives the bytes
back.

Three things had to be built under it, and each is reusable:

* **the harness had no right button.** `os88mouse` drove `mouse_btn` bit 0
  only, so `_edge` learned which button it is proving and `to` learned to carry
  both levels through a drag. `rmenu` is `menu` with the other button — and it
  has to be a press-drag-release for `menu`'s own reason, `menu_track` polling
  a level either way;
* **an item's y cannot be computed from the press point.** `menu_popup`
  anchors at the pointer and then SHIFTS rather than clips — left off the right
  edge, up off the bottom — so `rmenu` takes an `aim` callback that runs with
  the button DOWN and reads `menu_x1`/`menu_y1` out of the guest, after waiting
  for `menu_btn` to say the popup is up;
* **and the first run of that leg failed for a reason that was not the
  feature.** Two Calculator instances are on screen by then (the package legs
  open one each), and a right-press goes to whatever is under the pointer — an
  empty toast and an untouched file, reported against the context menu and
  caused by the z-order. The leg raises the window with a left click first,
  which is what `compress()` had been doing all along.

`FM_ICOMP` and `FM_IUNCOMP` are `equ`s now and the test reads them out of the
kernel's own map, so the two menu indices it picks by number cannot go stale —
the same discipline `tests/diskclone.py` and `tests/modstr.py` already use for
`FM_ICLONE` and `FM_IFMT`, both of which moved down one when the item landed.

### 13.13 THE SIZE PASS — 3,553 to 2,725, and `files.inc` 1,516 to 684

The feature landed at **3,553 bytes of sections / 4,096 of footprint** and the
shipping target is about 1,024. This is the first pass, and it took the largest
single row of the bill without cutting anything a user can see.

**Where the 1,516 in `files.inc` actually was**, measured off the map by
adjacency rather than estimated:

| | `.cold` | |
|---|---:|---|
| `fm_cmpr_go` | 375 | Compress |
| `fm_uncmp_go` | 284 | Uncompress |
| `fm_cmpr_hdr` | 113 | Compress |
| `fm_cmpr_pkg` | 88 | Compress |
| `fm_uncmp_pkg` | 72 | Uncompress |
| `fm_cmpr_claim` | 64 | Compress |
| `fm_cmpr_pct` | 55 | Compress |
| `fm_uncmp_free` | 38 | Uncompress |
| `fm_cmpr_sel` / `ferr` / `say` / the two entries | 79 | shared |
| `fm_uncmp_read` / `claim` | 51 | Uncompress |
| `fm_cmpr_drop` | 27 | Compress |

plus **251 of `.text`, of which 217 was PROSE** — nine verdict strings and a
ten-word table — and 16 of `.bss`. **Compress 722, Uncompress 445, shared 79.**

#### 13.13.1 The move, and what made it possible

**Every one of Compress's 722 bytes only ever ran after `mod_need` had already
fetched `COMPRESS.DRV`.** What kept them resident was ORDER and nothing else:
the body sized, claimed, read and refused a package *before* asking for the
module, so a refusal could be said on a machine with no system disk in it.
`mod_need` goes first now (§20.15), and the trade is stated rather than
hidden: such a machine says `No disk` instead of the specific reason, which is
the truer answer to "compress this" on a machine that cannot compress anything,
and a refusal costs one image read on a machine that can.

The mechanism was already in the tree and needed nothing invented. A module is
entered with **`DS = KERNEL_SEG`** (the caller is `.cold`), so `fm_onam`,
`fm_ftype` and the `fm_cmpr*` words are plain `[label]` reads and only the
image's own scratch needs `cs:`; the cloner had already established
`COLD_SEG:dwf_*` and `COLD_SEG:mmf_*` far shims for the file layer and the
heap, and only `dskw_usize` wanted a new one (4 bytes). The verdicts moved out
with the body because **`OSAPI_TOAST` takes `ES:SI` and not `DS:SI`** — a
detail written for packages putting text in a heap claim, and a module image is
one.

`COMPRESS.DRV` went 712 → 1,613 bytes on the system disk, which is not RAM.

#### 13.13.2 Four aliases, and two of them read better than what they replaced

* `fm_s_znomem` **=** `driver.inc`'s `drv_e4`, the same nineteen bytes of
  `'Not enough memory'` — guarded, because that whole block is inside
  `%ifdef OS88_DRIVERS` and `kern_small` has no driver layer, so there it is
  the only copy rather than the second one;
* `FMZ_BIG` **=** `fm_stattab`'s `fm_s_toobig`, `'Too large'`. The wording had
  to lose the word *compress* anyway, Uncompress being the verb that does not;
* `FMZ_NOMOD` **=** `fm_s_enodisk`, `'No disk'` — **the same words the cloner
  already uses when ITS image cannot be fetched**, for the same cause, and it
  cost 23 bytes to tell the user the name of a file they cannot act on;
* and `fm_cmprz` / `fm_cmprw` went into the image as `cs:` scratch, being the
  two words of the eight that Uncompress does not read.

#### 13.13.3 What is left, and what the arithmetic says about the target

`files.inc` is **684**: `fm_uncmp_go` 284, `fm_uncmp_pkg` 72, `fm_uncmp_free`
38, `fm_uncmp_read` 30, `fm_uncmp_claim` 21, the shared front and the thunk 86,
and 99 of `.text` (four verdict strings, a seven-word table and two menu
labels). **It is Uncompress now, plus the plumbing both verbs share.**

The whole feature is **2,725 / 3,072**:

| | bytes |
|---|---:|
| `lz.inc` — both decoders | 916 (650 `.cold`, 266 `.bss`) |
| `files.inc` — the two verbs | 684 |
| `diskw.inc` — the transparent read, `dskw_usize`, `czstamp` | 597 |
| `driver.inc` — a `.DRV` expands at load | 219 |
| `loader.inc` — `ld_expand` | 187 |
| `kernel.asm` / `mod.inc` / `disk.inc` | 122 |

#### 13.13.4 Three rows that were waiting on the wrong clock

The pass ran into elendilon's new `wants=` mechanism and found two bugs of its
own under it. `lzdrv`, `lzload`, `lzfence` and `lzfile` called
`os88fixture.need()` with a **phony target name** where every other row in the
tree passes a PATH — the runner compares the argument against what the row
declared, so all four died before their first instruction. Paths, declared in
`wants=`, and the four dropped `builds=True` with them: they no longer build
anything, which is the whole point of the mechanism.

That made `lzdrv` fail *differently*, and the second bug is the interesting
one: it read DRVCALL's answer strings the instant the window existed and slept
6 host seconds for a driver to come off a floppy. Both are HOST-clock waits for
GUEST work, and docs/plans/SOAK-PARALLEL.md §1 is exactly about that — a loaded box
hands the guest about a third less work per host second, so each was right
alone and short beside anything else. It polls `drv_tab`'s segment and the
answer bytes now, and is **faster** for it (28.2s against 34.3s): the poll
returns when the event happens instead of always spending the budget.

`lzmod` had the same 20-second sleep and polling it exposed a third thing —
the row never called `os88marty.no_saver`, so a long wait ends in SPEC.md §79's
saver drawing and `settle` refusing for ever. That is fixed too, and it is not
what ails the row: **`lzmod` FAILS, and it fails identically before this size
pass** — `[trk_modseg]` is 0 after ninety seconds with the saver off, so
Tracker opens and never gets the module. Ruled out so far: the package's
symbol map (the re-assembly is byte-identical to `build/tracker.bin`), the
in-place layout (`R + P` = 116,149 against a 114KB claim, which fits), memory
(a 640KB machine has ~527KB of heap), and this pass (the failure predates it).
It is the one open item in the feature.

#### 13.13.5 What the target now needs

**Zeroing `files.inc` entirely would still leave 2,041**, so the remaining
1,700 has to come out of the four consumers below it — which is a decision
about which of them stay resident, not a shaving exercise. `lz_wbuf` alone is
256 bytes of `.bss` serving one case (`.cznoroom`, 20.14.2.4 as it then was).
**13.14 below is what came of this**: the window, the margin and the case are gone.
**13.14 below is what came of this**: the window, the margin and the case are gone.

### 13.14 THE SIZE PASS 2 — 2,725 to 1,025, and the format change that paid for it

The ask was **under 1,024 bytes** for a feature that stood at 2,725, with three
constraints: LZ4's decode speed stays, the transparent read stays, and a file
that compresses right up to the end of its kilobyte still loads. The requester
opened the format itself. Two things were measured before anything was
written, and both decided the pass.

#### 13.14.1 The finding: cut the stream at its peak, and the margin is ZERO

The in-place scheme reads the packed bytes `R` above the destination and
expands downwards. `R` had to clear the PEAK of `produced − consumed` over the
whole decode, and that peak sat wherever the stream was densest, not at the
end — so `LZ_MARGIN` = 64 was reserved above every placement, a caller whose
buffer was exactly `U` was a case of its own (a sliding window for LZB, a
refusal and a build-time rule for LZ4), and the manual was EDITED to dodge the
rule.

**The peak can be moved to the end by the encoder, for free.** After the peak
the symbols are net-EXPANDING by exactly the margin: storing them raw makes
the file smaller by that much, and every prefix of what is left leads by no
more than the whole stream does — which is `U − P`, the placement itself. So a
stream is `[T word][LZ symbols][T raw bytes]`, the encoder cuts at the first
peak, and the decoder finds the tail with the compare it already made: where
it tested `SI` against the source's END it tests it against `end − T`. Zero
cycles on the fast path. SPEC.md §20.13.7 is the contract, `os88lz._cut` is
the rule, and `kernel/compress.inc`'s `.next` tracks the peak with the six
instructions that used to measure the margin — the cut is the same compare.
`tools/os88lz.py --selfcheck` asserts the margin at 0 over every binary in
the tree, and a Python decoder run INSIDE a buffer of exactly `U` bytes
(reading and writing in the kernel's order) confirmed it over the awkward
cases before a line of assembly was touched — `window.txt`, 4,096 bytes into
4,096, in both formats.

What that deleted, in the kernel: `LZ_MARGIN` and its three mirrors, the
package loader's `+ 64`, the `'CZ'` read's two-verdict capacity ladder and
its scratch claim, the **256-byte window and its cursor** (266 bytes of
`.bss`, ~150 of `.cold`), `dskw_czwin`, and the driver loader's second claim.
In the tools: `cz_inplace_short`, `cz_room`, `PKG_COMP_MARGIN`,
`CZ_MARGIN`, `OP_MARGIN`'s value, `checkreadme.py` rule 4, and the refusal
that stored one file in sixteen plain. The boot blob's kernel stream is the
one exception — `kz_expand` is its own copy of the LZ4 arm and reads the
classic ending — so `os88lz.lz4_compress` takes `tail=False` for it and
`KZ_MARGIN` stays real there; the blob could adopt the tail for ~22 bytes of
blob and nothing resident, and has not.

#### 13.14.2 The format question, measured — and both formats stay

Since the format was on the table, four byte-oriented alternatives were
prototyped in Python on the shared matcher and run over the tree's own files:
LZ4 with an implicit 7/15-bit offset (`c1`), a token bit for an 8/16-bit
offset (`c2`), `c2` with a repeat-offset (`c3`), and `c1` at min match 2.
Ratio, per cent of the input, host parse (H) and the machine's greedy
parse (M):

| | LZ4 | LZB host | LZB machine | c1 H | c2 H | c3 H | c1 M | c2 M |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 12 packages, 232 KB | 79.6 | 69.7 | 74.9 | 75.9 | 76.3 | 76.1 | 76.7 | 77.1 |
| README.TXT | 55.1 | 43.9 | 47.2 | 54.0 | 53.2 | 53.2 | 56.9 | 55.9 |
| BEVERLY.MOD | 36.3 | 33.0 | 52.5 | 51.2 | 35.1 | 35.2 | 53.0 | 52.6 |
| whole corpus, 425 KB | 65.1 | **57.2** | 66.4 | 66.9 | 62.6 | 62.5 | 68.4 | 68.6 |

A byte candidate recovers about a third of LZB's advantage on code and almost
none on text; `c1`'s 15-bit offsets lose `BEVERLY.MOD` outright (the samples
match tens of KB back), and the one that keeps 16-bit offsets is still five
points behind LZB over the corpus. The decoder it would save is ~130 bytes
against a `compress.inc` rewrite and a worse Compress verb. **So both formats
stay**, and the bytes came out of what the two decoders had been carrying
twice: a format decides how a length, an offset and a literal are SPELLED,
and everything done with them — the output bound, the offset bound, the copy
that may cross a segment, the match a segment below, the tail — is one body
of code under two arms of 51 and 89 bytes. `lz.inc` is 340 bytes of `.cold`
and no `.bss` where it was 650 and 266.

#### 13.14.3 What moved, and where the bill landed

| | before | after | how |
|---|---:|---:|---|
| `lz.inc` | 916 | 340 | one core, two arms, no window, no margin |
| `files.inc` | 684 | ~110 | Uncompress into the module with its four verdicts; the selection test too; one thunk for both verbs, the command id in `BX` telling them apart |
| `diskw.inc` | 597 | ~250 | one placement path for plain and compressed (`R = U − P`, and 0 for plain), `dskw_usize` gone (the module reads the hint off `dskw_raw` after `dwf_dskw_stat`), `czexp` loading its pair with one `lds`, the hint re-read after the transfer instead of a flag |
| `driver.inc` | 219 | ~0 | a compressed `.DRV` is a `'CZ'` file; `drv_find` reads the hint (+14 in `drvvol.inc`) and the transparent read expands it into the claim; `drv_expand` and its two claims are gone |
| `loader.inc` | 187 | ~170 | the prefix computed off the header the file carries, the format as `(flags >> 3) & 3` |
| `mod.inc` + `kernel.asm` + `disk.inc` | 122 | ~60 | **the compressor rides in `CLONE.DRV`** (§20.15.3): no `mod_tab` row, no name, no `mod_fp` block — a second entry in an image that had six spare slots |

`kernsize` on the tree that took it: **`.text` −84, `.bss` −312, `.cold`
−1,304, sum −1,700**; the cold rung uncrossed three times, 1,536 bytes of
every machine's RAM back; `.text + .bss` 56,885 of `KERN_CODE_MAX`. Against
§13.13's 2,725 that is **1,025** — and by that section's own arithmetic, which
is the one the requester quoted. Six of those bytes arrived with the two
defects `tests/lzcomp.py` found on the first full run, and the number is the
tree the row passes on, not the one it was first read off:

- the thunk entered `fmv_sync_x` with DI unset. The old body reached that
  line through `fm_sel_ok`, which loads DI with the state block, and the
  cut took the load with the test — so the sync mounted a drive read off the
  stack (142, on the machine), `mod_need` then remounted the boot volume and
  put the garbage back, and every verb said `No disk`. Traced with
  `bp_exec` on the eight routines between the thunk and the toast rather
  than reasoned about;
- `cmz_sizes`'s container arm returned with the `jbe`'s own carry. An LZB
  mark compares equal and leaves CF clear; an LZ4 mark compares BELOW and
  leaves it set, so both verbs on an LZ4 file answered a FERR whose number
  was the file's unpacked size, and `toast_say` said nothing for an index
  past its table. `xor dh, dh` clears it and is a byte shorter.

#### 13.14.4 What it costs, stated

- **Two bytes per stream** for `T`, and the tail's own `rep movsb`. The files
  are smaller anyway: the cut saves the old margin, which was 2 on a package
  and up to 5 on `BEVERLY.MOD`.
- **The LZ4 fast path is UNCHANGED in cycles** on the symbol loop — the tail
  compare replaces the end compare — and gains the shared copy's second
  carry test per match, which the old arm also made. This is arithmetic on
  the listing and NOT a MartyPC measurement; §4's 50.6 cycles a byte has not
  been re-taken.
- **A 64KB boundary is now crossed by a byte loop** rather than a four-way
  minimum: a few hundred cycles once per segment of output, against sixty
  bytes of kernel.
- **A no-op Compress on a folder fetches the image** to find out it is a
  no-op: the selection test was 31 resident bytes for a case the context menu
  of a folder does not offer.
- **Either verb reads `CLONE.DRV` whole**, 5,810 bytes where `COMPRESS.DRV`
  was 2,265 — one sector run rather than two, on a verb somebody waits
  seconds for.
- **The 128KB machine**: nothing resident was added anywhere; the sliding
  window's 256 bytes came off `.bss`.

#### 13.14.5 Refused, and left on the table

- LZB's literal path through the shared take/copy: −7 bytes for ~+120 cycles
  a literal, which on that format is half its speed. Refused.
- The bit reader as a subroutine: −6 bytes for ~+30% on LZB. Refused.
- `HDDTOOL.DRV` (SPEC.md 20.13.5.1) — **TAKEN**, the day after the pass
  landed: `hd_tool_need`'s read IS the transparent one and its `HDTOOL_KB`
  claim is cut from the IMAGE by the Makefile, so an expanded tool fits it by
  construction. One word in a Makefile rule, the gate in `tests/unit/t_pkg.py`
  turned round to assert it is packed whenever the rest are, and
  `tests/hddcp.py` opening Format and Install off it on MartyPC. 8,304
  bytes against 12,567, 9 clusters of the 360KB system disk
  against 13; the disk stands at 273 of 354 against 277.
- The kernel modules, the ten faces and the licence — **TAKEN**, with
  `HDDTOOL.DRV` and for its reason: every reader was already the transparent
  read into a claim cut from the image or bigger than any file. `os88mod.py`
  wraps what it has checked (`--wrap`, off `PKGZ`), the faces ship packed out
  of `build/faces/` under their own basenames, and the licence follows
  `README.TXT`'s rule. Modules 7,394 / 5,810 / 3,497 / 1,129 → 6,104 / 4,965
  / 3,003 / 1,018 (five clusters); faces 14,992 → 8,937 (ten); licence 6,718
  → 3,783 (three). The 360KB system disk stands at **255 of 354 clusters**
  against 277 before the tool. Proved on MartyPC: `tests/facetest`'s package
  opens a packed face (`ty_openfam` err 0, ten families), and the module rows
  `modstr`, `dispthm`, `lzcomp`, `diskclone`, `hibernate`, `fcpsmall` and
  `fdlgsmall`. What it turned up on the way: `tools/os88hdd.py` wrote no
  directory hint, so a packed driver on the hibernate fixture's volume read
  as plain — it stamps one now, `os88disk.dirent`'s rule. And `RAMPAGE.DRV`
  came through the merge PLAIN, with a gate asserting it must stay so: the
  soak branch had found the Ram Disk page dead under the v4 body format and
  fixed it the only way that format allowed. Its reader is `HDDTOOL.DRV`'s
  shape exactly (a claim cut from the image, a read at offset 0), so on this
  tree it packs by construction, the gate is turned round, and `rdup` is
  the row that says so.
- The boot blob's classic ending: one dialect instead of two, ~22 bytes of
  blob, `KZ_MARGIN` gone.

## 14. Decisions still open before wave 1

Everything in §12 is costed. These are the things a build would have to
*choose*, grouped by the wave that first needs them. Six of the nine have a
recommendation; three are genuinely the owner's.

### 14.1 Before wave 1 — the decoders

1. **LZ4, LZB, or both behind the knob?** **SETTLED — LZ4 SHIPS, LZB DOES
   NOT** (§13.9, SPEC.md §20.13.5). The recommendation was *both, default
   LZ4*, and what shipped is the knob rather than the pair: a stock kernel
   carries LZ4 alone, `COMPRESS=lzb|both` still builds and `PKGZ=lzb` still
   packs, and `tests/lzship.py --fmt lzb` boots the whole set through it — so
   the 5150 A/B this item exists for is still one command, and adding LZB
   later moves no disk layout. What is *not* settled is the A/B itself: the
   per-launch/one-time split in §12.7 is still not a number a table can
   resolve, and until it is measured on iron the machine takes the fast
   decoder.

1a. **…and how much of the in-place work is LZ4's?** **MEASURED, and the
   answer is none of it.** The room an in-place read needs is
   `R + P = (U − P + margin) + P = U + margin` — **the packed size cancels**,
   so whether a file needs SPEC.md §20.14.2.1's scratch depends only on its
   UNPACKED size against a kilobyte boundary and on the margin. The ratio
   never enters, and neither does the format. The build-time rule
   (20.14.2.3 as it then was), the scratch, and the un-rounded `R` are all format-blind.
   **Superseded by 13.14 below**: there is no margin, no scratch and no rule any more.

   The one format-dependent number is the margin, over 77 raw inputs:

   | | worst | on `BEVERLY.MOD` / `weave` / `loom` |
   |---|---:|---:|
   | LZ4 | 10 | 5 / 3 / 4 |
   | LZB | **243**, then 11, then ≤3 | **0 / 0 / 0** |

   **The 243 is `OS8088.GIF`, which GROWS under both formats** (2,138 →
   2,148 LZ4, 2,379 LZB) and is refused on "did it get smaller" before the
   margin test is reached — which is why `cz_wrap` runs its tests in that
   order, and why that order is deliberate rather than incidental. Among
   files that actually compress, LZB's worst is 11 against LZ4's 10, and on
   the big files LZB is **better**: a bit-oriented literal costs 9 bits per
   byte produced, so an incompressible tail pulls the lead down hard and the
   peak sits closer to the end.

   Mean ratio over the same 77: **58.2% LZB against 65.8% LZ4** — the ~10
   points §12.7 claims, measured at 7.6.

   **So a wave-6 that compresses to LZB needs nothing new here.** The only
   thing that would cost twice is the sliding-window decoder of §13.10's
   open item, and that is two implementations of one idea rather than a
   second idea: shipped files are LZ4 and foreign LZ4 files exist whatever
   the file manager writes.

2. **The compressor's effort level** (§12.3.1). *Recommend depth-16 chains
   over a 16,384 window: 32 KB, ~2.2x the time, 78.8%.* The alternative is
   the greedy parse already built (8 KB, 83.9%). This is a real choice only
   because of the RAM: on a 128KB machine the window must fall to 4,096.

3. **Format field placement.** *Recommend `.o88` flags bits 3 and 4* (bit 3 =
   compressed, bit 4 = format), which SPEC.md §20.2 documents as zero today,
   *and an eight-byte `'CZ'` header for everything else* (§12.2). No version
   bump either side.

### 14.2 Before wave 5 — the transparent read

4. **What does `OSAPI_FILE_READ_AT` answer on a compressed file?** A new
   `FERR_*` is the mechanism; the question is what the *caller* does with it.
   Frotz and ftpd are the two real random-access readers and neither should
   ever meet one — *recommend the file manager refuse to compress a file
   whose extension is claimed by an association* (§54), which is a rule the
   machine can already evaluate and needs no new state.

5. **Which size does "will it fit" use?** The app is told the unpacked size;
   the disk holds the packed one. *Recommend the on-disk size for
   `OSAPI_FILE_DFREE` arithmetic and for the copier*, which is what the
   directory entry already holds — so a copy moves the file as it is and
   nothing has to expand to find out whether it fits.

6. **How does a user SEE that a file is compressed?** **SETTLED, and it goes
   LAST — see §15.** The icon badge is preferred and the list-view column is
   the fallback; both are pure drawing over data the listing already carries,
   so neither constrains anything in §12. **The one thing that must happen
   early is that the compressor WRITES the hint** (§15.4), four bytes into a
   sector already being written — without that, files compressed before the
   UI lands need a rescan to catch up. **DONE, and more widely than asked**:
   wave 5 put it in `dskw_commit`, so *every* whole-file write in the machine
   derives it from the bytes it is writing (SPEC.md §20.14.4) and the
   compressor gets it without doing anything at all.

### 14.3 Before wave 3

7. **Does the driver header get a `bss` field?** *Recommend yes, and before
   any driver is compressed* (§12.6): 14.6 KB with no decoder, and it is what
   keeps those bytes out of the decoder afterwards.

### 14.4 Not decisions — measurements that must happen first

8. **The 1.44MB per-sector cost** (§10 item 1). Still the one modelled figure
   on this page, and §9's recommendation 2 — compress per geometry — rests on
   it entirely. If it comes in near the 360KB figure, that recommendation
   collapses into "compress everywhere" and the Makefile gets simpler. **It
   has been overtaken rather than answered**: §13.9 compresses every geometry
   because a per-geometry `PKGZ` is a second shipped set to test rather than a
   Makefile branch — so this measurement now decides whether that was worth
   it, not whether to do it. **§13.8 measured the term it turns on**, on the
   360KB drive: 47 fewer sectors bought 1,278 ms, of which only 562 was
   mechanical, so **56% of a sector's cost there is the BIOS per call** and
   not the media.

9. **That the bounds actually refuse** (§13's wave-1 gate). Written and
   measured for size and speed; still an assertion.

### 14.5 Deliberately NOT in scope

- ~~**Compressing `KERNEL.SYS`** (§9): the boot blob has 107 bytes free
  against a 115-byte decoder.~~ **BUILT — §13.8.** Both halves of that
  sentence moved: `.boot2` shed 200 bytes in the splash's size pass, and the
  decoder that goes in the blob needs none of §20.13.4's bounds, because this
  is the one stream on the machine that nothing but this `make` ever writes.
  196 bytes of blob, nothing resident, 40 sectors off the system disk and
  **599 ms off the boot**.
- **A compression ABI for packages** (§12.1): no caller, 40x the decoder.
  `COMPRESS.DRV` publishes one if a package ever needs it.
- **`OSAPI_FILE_WRITE` producing a compressed file**: follows from the
  above — an app cannot write one until that module exists to be called.

---

## 15. The listing must not get slower — and it does not have to

**A directory listing may not pay for this feature.** That constraint is
sharper than it looks, because the unpacked size lives in the file's own
header, in its first data sector — and **the mount does not read that sector
for an ordinary file.** `dsk_mount`'s harvest (SPEC.md §18.3 step 4) reads a
first sector only for `type == 1`, the packages, and everything else takes
the zero slot with no I/O at all.

**This tree already knows what that read costs**: §54.7's `ASSOC.DAT` exists
precisely to stop the mount doing it *even for packages*, and the harvest's
own comment records that `kern_small`, which has no assoc at all (§54.0),
"reads every icon the slow way". Adding one sector per ordinary file would be
the same cost again over a much larger population — on a full 360KB apps disk
that is 29 extra reads on every mount.

### 15.1 The four bytes are already there, and they are already free

A FAT12/16 directory entry is 32 bytes and **os8088 writes only six of its
fields**. `dskw_commit` clears all 32 and then stamps name, ext, attribute,
`DSK_R_CTIME`/`CDATE`/`ADATE`, `WTIME`/`WDATE`, `DSK_R_CLUS` and
`DSK_R_SIZE`. That leaves four bytes that this kernel zeroes on create and
never reads:

| offset | what FAT calls it | os8088 today |
|---|---|---|
| **12** | reserved (NT case flags) | zeroed, never read |
| **13** | creation time, tenths | zeroed, never read |
| **20–21** | `FstClusHI` | **SPEC.md §19.1: "FAT32-only per spec — ignored"** |

**Put the hint there.** One byte at 12 saying *compressed, and in which
format*, and a 24-bit unpacked size across 13 and 20–21 — 16 MB of range
against a `PKG_FILE_HI` of 1 MB and a largest shipped data file of 113 KB.

**The directory sector is read anyway**, so the cost of knowing is **zero
extra I/O at mount, zero per listing, and zero per entry**. Nothing about
§18.3's harvest changes and no disk gets slower.

### 15.2 The staged entry needs no more room either

SPEC.md §19.1's staged entry is **24 bytes and has no spare field** — bytes
24..31 were deliberately dropped to save 256 bytes of `.lowbss`, "the tightest
rung in the kernel", and widening the stride would break `dsk_ioff`, a
driver's donated claim and `FS_IOFH`'s "must be a multiple of 256" all at
once. It does not need widening:

- **the type word at +16 is a word holding 0, 1, 2 or 3**, so its high byte is
  free for the flag;
- **the size dword at +20 can simply carry the UNPACKED size**, which is what
  the user of a listing wants to see, with the on-disk figure still available
  from the raw entry for anything that needs it (§14.1 item 5).

So both halves of the requirement — the flag for display, the unpacked size
for an app sizing a claim — land inside the existing 24 bytes.

**HALF OF THIS IS BUILT AND IT IS THE OTHER 24 BYTES.** Wave 5 put the flag
and the unpacked size in `OSAPI_FILE_FIND`'s record (§20.14.3) — a **different**
24-byte block, the one a package reads, where +22 was already reserved and
zero and needed no argument about `.lowbss` at all. `dsk_ent`, the staged
listing entry this section is about, is **untouched**, so the Disk window
still shows the on-disk size. That is wave 7's to change, and the paragraphs
above are still the design for it.

### 15.3 The hint is a CACHE, and the body is the authority

**This is the rule that keeps it safe, and it is not optional.** Those four
bytes survive an os8088 copy, but a foreign tool that rewrites the directory
entry may drop them — most zero exactly these fields. So:

- **a missing hint reads as "not compressed"**, which if trusted would hand
  an application compressed bytes and look like file corruption;
- therefore **`dskw_read` always checks the body's `'CZ'` magic** and never
  decides from the hint;
- the hint is used for **display and pre-flight sizing only**.

**CORRECTED BY THE BUILDING: the hint decides the LAYOUT, and the body decides
whether to DECODE.** "Display and pre-flight sizing only" turned out to be too
weak a role. The read expands in place, so the packed bytes have to land `R`
bytes up inside the caller's buffer — and `R` is computed from the unpacked
size **before the first sector is read**, which is the only figure available at
that moment. So the hint is load-bearing for the read path, not merely
cosmetic. The safety argument survives intact and is sharper for being split:
a wrong hint can only put the bytes in the wrong place *inside the caller's own
buffer*, whose bounds are checked against the caller's capacity either way, and
`dskw_czexp` then refuses the decode because the file's own header disagrees
(SPEC.md §20.14.1). Nothing is decoded on the hint's word.

That is the same split §54.7 already uses: `ASSOC.DAT` is a cache and the
package's own header is what the loader believes. A hint byte at offset 12
with a distinctive value also cannot be forged by accident — a conformant
FAT writer puts zero there.

### 15.4 …which is what makes the badge genuinely last

**Yes — the display can be the final wave, on one condition: the compressor
writes the hint from the day it ships, even though nothing reads it yet.**
Four bytes into a sector that is being written anyway, costing nothing. Get
that wrong and every file compressed before the UI lands has no hint, and the
badge arrives needing a rescan of the disk to catch up.

With the hint being written, the two candidate displays are pure drawing over
data already in the listing, and either can be built, looked at, and thrown
away without touching the plumbing:

- **an icon badge** — the preferred one, and the risk is exactly as suspected:
  §25's library is 16x16 and a corner mark on a tile that already carries a
  document fold is cluttered. Worth prototyping, cheap to abandon.
- **a list-view column** — needs the Disk window's default width to grow, and
  §22's list view has the room for one on a 640px CGA only if something else
  gives. That is a real layout change, which is the argument for the badge.

**Nothing else in §12 or §13 depends on which is chosen**, or on either being
chosen at all.
