# Where DOS.O88's packed bytes go - and what the source can win back

**A measurement.** Taken 2026-11-01 on commit `ef034d7d` (branch
`dos-windowed`, clean tree), on a 4-core Linux container. Host-side only: no
emulator and no shipped byte changed. Every figure here is MEASURED by running
`tools/os88lz.py`'s own encoders on the shipped parts unless it says ESTIMATED.

**The owner's question**: *compression finds what duplicates well - is there
something we can unduplicate ourselves, and save the same space without
changing compression type?*

**The answer in one paragraph.** Mostly no, and the reason is measurable:
**74% of part 0's lz4 stream is literal bytes** - 17,412 of 23,461 - i.e.
8086 code that does not repeat in runs of 4 or more anywhere. The
duplication that exists is already taken, and nearly cheaply. The 2.8 KB
lz4-to-LZB gap on part 0 is **not** missed duplication either: 80% of it is
2- and 3-byte repeats that LZ4's 4-byte minimum match cannot express, and the
rest is LZB coding near offsets in fewer bits. No source edit moves either.
What DOES move the number is three things, none of them a new compression
type: **(1) near-call displacements** (an `E8 rel16` to the same routine is
different bytes at every call site - a 30-line post-decode filter makes them
identical and wins **1,386 bytes across the three parts**); **(2) the 27 KB of
zeros part 0 ships** (116 bytes packed, but ~96 ms of decode a launch); and
**(3) an optimal-parse lz4 encoder** (103 bytes, zero decode cost, host-only).
Together **-1,700 bytes = 30% of the 5.6 KB gap, and -3 sectors of DOS.O88**.

## 1. Method

- The three parts were re-assembled from source with the Makefile's own
  flags plus `-l` and a `[map all]` wrapper (`%include` of the real source),
  into `/tmp`; all three came out **byte-identical** to `build/dosp.bin`,
  `build/doscore.bin`, `build/kerndos.bin`. (kern_big's arm: `-DDOS_WIN16`.)
- The encoders were re-run with their parse recorded (lz4: `lz4_compress`'s
  lazy parse, depth 128; LZB: `lzb_compress`'s shortest path). Each output
  byte's cost was attributed back to the input bytes it covers: a literal
  costs 8 bits (+ its share of a run-length extension); a match's token,
  offset and extension bytes are spread over the bytes it copies; the raw
  tail costs 8 bits a byte. **The attribution sums to the real stream to the
  byte** (23,461 / 13,672 / 11,855 lz4; 20,684 / 12,065 / 10,626 LZB).
  Note the brief's LZB figures (20,899 / 12,079 / 10,631) are from an older
  tree; this tree's gap is **2,777 / 1,607 / 1,229 = 5,613 bytes**.
- Bytes are owned by the nearest preceding non-local label in the map.
- Counterfactuals (cut zeros, call filter, glyph removal, reorder, string
  tokenising) were applied to the BYTES and re-packed. They measure packed
  size only; a runnable build of each was not made.

## 2. Where part 0's packed bytes go (dosp.bin, 55,316 -> 23,461 lz4)

| lz4 stream component | bytes | share |
|---|---:|---:|
| literal bytes | 17,412 | 74.2% |
| match offsets (2 x 1,857 matches) | 3,714 | 15.8% |
| tokens (1,858 sequences) | 1,858 | 7.9% |
| length extensions (literal 360, match 116) | 476 | 2.0% |

Matches are short: 673 of 1,857 are exactly 4 bytes, 442 are 5. A typical
match is a pop ladder or a `mov`/`call` prologue fragment.

By family (attributed): `dos_` 15,210 in -> 11,228 packed (0.74 a byte),
`dvw_` 4,222 -> 3,088, `con_` 9,087 -> 2,841 (the screen buffer is zeros),
`os88ui_` 3,057 -> 2,492 (0.82), `dn_` 3,175 -> 2,472, `os88line_` 935 -> 743.
**Code packs to 0.75-0.99 of itself everywhere**; nothing stands out the way
a table or a font would.

**Top regions by packed cost** (in -> lz4 / LZB):

| symbol | in | lz4 | per byte | LZB |
|---|---:|---:|---:|---:|
| `con_glyf` (CP437 face, 2,048 incl. 768-byte hole) | 2,048 | 969 | 0.47 | 778 |
| `dos_h_text` (HELP text) | 798 | 557 | 0.70 | 443 |
| `dos_run` | 470 | 436 | 0.93 | 389 |
| `dos_pkt_go` | 435 | 428 | 0.98 | 404 |
| `dvw_int10` | 345 | 328 | 0.95 | 308 |
| `dos_w16_try` | 314 | 239 | 0.76 | 223 |
| `dos_entry` | 228 | 226 | 0.99 | 227 |
| `dvw_runprog` | 274 | 225 | 0.82 | 213 |
| `con_compose` | 262 | 222 | 0.85 | 202 |
| `os88ui_btn` | 256 | 217 | 0.85 | 203 |
| `dos_pt_body` | 234 | 213 | 0.91 | 195 |
| `dos_key` | 263 | 213 | 0.81 | 192 |
| `dvw_scroll` | 218 | 208 | 0.96 | 192 |
| `dos_pkg_go` | 237 | 200 | 0.84 | 172 |
| `dn_tcp_open` | 218 | 196 | 0.90 | 163 |
| `dos_con_ext` | 231 | 196 | 0.85 | 162 |
| `dn_accept` | 222 | 191 | 0.86 | 168 |
| `dn_pump` | 239 | 190 | 0.80 | 170 |
| `dvw_fsmain` | 255 | 183 | 0.72 | 158 |
| `dos_paint_mem` | 242 | 178 | 0.74 | 153 |
| `dn_arp` | 194 | 170 | 0.88 | 143 |
| `dos_click_mem` | 187 | 166 | 0.89 | 148 |
| `dos_con_key` | 190 | 164 | 0.86 | 153 |
| `dvw_capt` | 197 | 163 | 0.83 | 141 |
| the core hole + header (`<start>`, 19,029 of which 18,944 zero) | 19,029 | 157 | 0.01 | 80 |

Worst per input byte (>= 64 bytes): `dvw_betab` 1.01, `dos_entry` 0.99,
`dos_fsx_main` 0.99, `dos_pkt_go` 0.98, `dvw_copy` 0.97, `os88ui_drpress`
0.96, `dwm_mouse` 0.96, `dos_mem_arena` 0.96 - i.e. code that is
incompressible to LZ4 by construction: unique addresses and displacements.

## 3. The lz4 -> LZB gap on part 0, by mechanism (2,777 bytes)

Every input byte classified by what each parse did with it:

| mechanism | bytes of input | gap (lz4 - LZB) |
|---|---:|---:|
| LZB takes a **2-byte** match where lz4 emits literals | 4,820 | +673 |
| LZB takes a **3-byte** match where lz4 emits literals | 4,042 | +1,462 |
| LZB takes a 4+ match the lazy lz4 parse skipped | 144 | +84 |
| both match, offset < 256 (LZB's gamma offset is ~10 bits, lz4's 16) | 4,393 | +806 |
| both match, offset 256..4095 | 4,196 | +528 |
| both match, offset >= 4096 (**long distance**) | 2,258 | +45 |
| the two long zero runs (LZB's gamma length vs 74 lz4 `255` bytes) | 26,958 | +103 |
| both literal: **LZB's tag bit taxes every literal** | 8,401 | -882 |
| lz4 matched, LZB chose literals; raw tail | 104 | -41 |

So: **short matches 2,219 (80%) + cheaper near-offset coding 1,334 (48%) +
long distance 45 + zeros 103 - literal tag tax 882 - other 41 = 2,777.** Long-distance matches are worth 45
bytes, and neither format codes entropy (for scale, raw LZMA `xz -9e` packs
part 0 to 18,930 - 92% of LZB's output, so even an entropy coder finds
little more duplication to take). The core (1,607) and kern_dos (1,229) split
the same way: short matches +1,503 / +1,282, near-offset coding +622 / +359,
tag tax -544 / -496. **None of these is a property of the source; all of them
are properties of the format.** That is why the size passes saw what they saw:
lz4 already prices a repeated 4-8 byte ladder at ~3 bytes, and the `call` that
replaces it is 3 *unique* bytes plus a broken match.

## 4. What the source (or the packer) can do - ranked, measured

| # | lever | lz4 bytes saved (dosp / core / kern_dos) | decode cost | status |
|---|---|---|---|---|
| 1 | **Call-displacement filter** ("BCJ"): before packing, rewrite every `E8`/`E9 rel16` as the ABSOLUTE target; after decoding, rewrite back | **616 / 565 / 205 = 1,386** (E8 only: 612 / 486 / 176 = 1,274) | one scan of the part: ~15-19 cycles a byte, ESTIMATED ~0.1-0.2 s for part 0 (less if #2 cuts the zeros out of the scan), against LZB's +1.7-1.9 s | MEASURED on bytes |
| 2 | **Stop shipping zeros in part 0**: the core slot hole (`times CORE_MAX + CORE_BSS_SIZE db 0`, 18,910 bytes at 0x77) and the trailing bss (7,284 at `os88_image_end`) | 116 / 0 / 75 = 191 | **saves** ~27 KB of `rep movsb` match copy a launch, ESTIMATED ~96 ms at ~17 cycles a byte | MEASURED on bytes |
| 3 | **Optimal-parse lz4 encoder** in `tools/os88lz.py` (forward DP on price with literal-run state; same format, same decoder; passes `lz4_decompress` and `in_place_margin` = 0) | 69 / 20 / 14 = 103 | **none** | MEASURED, prototype ran 8 s |
| 4 | Box-drawing/shade/block glyphs 0xB0-0xDF generated at launch from an arms table (the tool already derives them, `tools/cp437font.py` `box()`) | upper bound 217 for removing the 384 bytes; net of a ~100-150-byte generator ESTIMATED 50-120 | a few ms once | upper bound MEASURED, net ESTIMATED |
| 5 | Accented glyphs (33) composed from ROM letters + an accent | upper bound 187; net ESTIMATED <= 60, and changes the look on clone ROMs | - | not recommended |
| 6 | Near-duplicate code differing in one operand -> table + one routine | ceiling **183** (61 places where a match resumes at the same offset after 1-2 literals; 3 bytes each) - before the routine and table are paid for | cycles per use | ceiling MEASURED; not worth it |
| 7 | Reorder code/data so similar things sit together | lz4: **nothing** (offset is a flat 16 bits; a random shuffle of the 754 blocks costs only +364 from boundaries) - LZB +1,250 from the same shuffle, so source order already helps LZB | - | MEASURED |
| 8 | Tokenise strings (word dictionary) | **loses**: 1,903 bytes of text are 1,483 lz4; with 16/32/64 dictionary words, 1,513 / 1,537 / 1,599 | - | MEASURED, refuted |
| 9 | Merge parts to share matches across them | 250-480 bytes a pair (e.g. core given dosp -477), but each part is decoded on its own and two are LAZY | - | MEASURED, refuted by the container |
| 10 | Factor repeats into calls | grows lz4 (the size-pass agents' finding, explained in 3) - **unless #1 lands**: with absolute targets every `call x` is the same 3 bytes and factoring stops fighting the compressor | - | consistent |

All three of 1+2+3 together, measured on the bytes:

| part | lz4 today | 1+2+3 | LZB today | sectors today -> after |
|---|---:|---:|---:|---|
| 0 host | 23,461 | **22,655** | 20,684 | 46 -> 45 |
| 1 core | 13,672 | **13,075** | 12,065 | 27 -> 26 |
| 2 kern_dos | 11,855 | **11,558** | 10,626 | 24 -> 23 |
| total | 48,988 | **47,288 (-1,700)** | 43,375 | **-3 sectors** |

So 30% of the gap, at roughly a tenth of LZB's decode bill (and #2 alone pays
some of that back). The remaining 3.9 KB is LZ4 being LZ4.

## 5. Notes on each recommendation

**1 - the call filter.** 892 `E8` sites in part 0 (789 real calls per the
listing to 330 distinct targets; the rest are `E8` bytes inside data or
operands). The flat target distribution (top: `con_say` 37 calls, `dn_copy`
23, `dvw_post` 22) is why no SOURCE spelling helps: `call [cs:tbl]` makes
sites identical but costs a byte of image and RAM per call and ~10+ cycles
each. The filter is the same transform xz's x86 BCJ does, reduced to the
8086: walk the buffer, at each `E8`/`E9` byte replace the next word `w` with
`w + (i + 3)` (encoder) or `w - (i + 3)` (decoder) and skip 3. Data bytes
that happen to be `E8` are transformed too and transformed back: it is a
bijection, round-trip verified on all three parts, total on hostile input
(nothing to bounds-check but the last two bytes). **It is a container change,
not a compression type**: a part flag beside `OP_COMP` and ~25-30 bytes of
decoder wherever parts are expanded (the loader's `op_load` path and
kern_dos's stub reader for part 2) - an ABI decision under SPEC.md 20.2.0.1,
and the same filter would serve every package and driver, not only DOS.

**2 - the zeros.** Part 0 is an `OP_SEG` and its row's length is the
expanded length, so its bss and the core's slot are shipped as zeros. Packed
they are nearly free (116 bytes), but the decoder WRITES them: 26,966 bytes
of match copy at every launch. Either a row that expands to a prefix and
zero-fills (or does not touch) the rest, or laying the core slot and the bss
past the part's end, takes both. The core slot needs no zeroing at all - the
core is copied over it.

**3 - the optimal parser.** `lz4_compress`'s docstring says an exact parse
buys "a fraction of a percent"; measured here it is 0.3-0.4% (103 bytes over
the three parts). Small, but it is the only item in this report with no
cost on the machine, and it reaches every lz4 file on every disk. Search
depth is not the limit: depth 100,000 instead of 128 moves part 0 by 4 bytes.

**What is NOT a lever.** Long-distance matches (45 bytes), match-finding
depth, string tokenising, reordering for lz4, merging parts, the HELP text
(1,918 bytes of text in 89 runs cost 1,466 - the same 0.76 a byte as code).

## 6. Core and kern_dos, briefly

- **doscore** (15,388 -> 13,672, 0.89 a byte - the least compressible part):
  no zero runs at all; `dos_int21` is 2,391 bytes -> 2,278 (0.95). 507 `E8`
  sites in 15 KB - the densest call traffic of the three - which is why the
  filter takes the most from it (-565 with `E9`, 4.1%). Strings 432 bytes.
- **kern_dos** (31,841 -> 11,855): 19,021 bytes of zero runs (75 packed);
  the rest is the kernel's disk layer (`dskw_wabody` 541 -> 502,
  `dsk_bpb_check` 434 -> 428, `dsk_xfer` 423 -> 420) and the handoff/resume
  (`kd_entry` 575 -> 556, `kd_resume`, `hbs_stub`) - 0.93-0.99 a byte.
  Part 2 is lazy, so its decode cost is paid only on a whole-machine run.

## 7. Reproducing

Assemble each part with the Makefile flags plus `-l /tmp/x.lst` (never
`/dev/null`) and a two-line wrapper `[map all /tmp/x.map]` / `%include
"<source>"`, `cmp` against `build/`, then re-run `os88lz.lz4_compress`'s loop
and `lzb_compress`'s DP recording each symbol (`Chains`, `_lz4_seq_len`,
`_lzb_cost` are the cost model). The scripts were throwaway, in `/tmp/dpcs/`,
and are not part of the tree.
