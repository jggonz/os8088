# kern_mega: programs that think in huge reservations (survey, 2026-11-08)

The owner: *"I have a feeling a bunch of mega apps need to start using and
thinking in huge reservations."* This is the survey of who could and should
use the region-crossing claim, and what the facility lacks before they can.
Nothing here is built yet. Two agents read every package under `apps/` on
`mega-release` (5e4740bb); no source was changed and no emulator was run, so
every figure is read off the source or taken from KERN-MEGA-PLAN 14.166's
measurements. Their full tables, with file:line for every buffer, were the
session's scratch reports; what matters from them is below.

## 1. The facility as it is

`OSAPI_MEM_HUGE` (SPEC.md 50.3.5) is a claim of any size, walked by the step
it answers. On kern_mega in protected mode (`mega_hugef`, `mega_hugeqf`) it is
a run of LDT selectors over a pool block above 1MB, and a 64KB-step claim may
**bridge**: one piece in the pool, one in the conventional heap, each
contiguous in its own region, movable blocks packed first. AX = 0 asks for the
largest grantable. On kern_big and kern_small it is a conventional claim
walked by paragraphs (CX = 1000h), refused at 1024KB.

What one claim can be on the measured machines (14.166, after a session):

| machine | pool free | conventional free | one bridged claim |
|---|---|---|---|
| 640K + 384K | 239K | 560K | 688K |
| 2MB | 879K | 560K | 1,328K |
| 4MB | 2,926K | 560K | 3,376K |

Today's users: RealDMG's cartridge in place (`rwbank.inc`, 16KB windows),
WIN16's virtual claims (`w16pm.inc`), the C SDK's `os88_mem_huge` (which no C
package calls).

Two facts shape every verdict:

- **A claim of 64KB or less already goes above 1MB** for an `OS88_HIGHMEM`
  package (`osapi_mem_p`, the KB doors). A claim OVER 64KB through
  `OSAPI_MEM_CLAIM` / `OSAPI_MEM_P` stays conventional, and so competes for
  the ~560K heap. Huge is what buffers over 64KB need.
- **Most document caps are 16-bit arithmetic, not memory**: Word, Scribe and
  Note Pad's save walk, the WAB format's `W_CAP`, RunCPM's 16-bit file size,
  Sheet's 16-bit record offsets. A huge claim lifts none of them without the
  code that walks the buffer changing too.

## 2. The facility's gaps - first, because the packages wait on them

| # | gap | who it blocks | severity |
|---|---|---|---|
| F1 | **A bridged claim is only correct walked by its step.** `SEG_SUB` (`mega_segf .sub`), `SEG_AT` (plain base arithmetic, `mega.inc` ~12807), `SEG_LIN` plus an offset, and the kernel's own file I/O stepping (`dskw_hicarry` / `dskw_hifix` / `dskw_hisub`, `diskw.inc` ~2555-2600, by `SEG_SUB` and not by the run) all compute past the pool piece into whatever pool memory follows it - another program's block, or free pool. Reproduced: not yet (source reading by both agents) | WIN16 TODAY (`w16_vsel`'s `SEG_SUB`, `w16_vnew`'s `SEG_LIN`, on a large GlobalAlloc that neither region holds alone); any one file call across a seam; XTMP3's `x3h_at`, the Browser's `br_at`, the parts' `op_at` the moment they take a huge claim | **a correctness defect**: fix first |
| F2 | Nothing reports a bridge or lets a caller refuse one | every caller that is not a pure step walker | needed with F1 |
| F3 | 16KB / 32KB-window claims (BX = 4000h / 8000h) can never bridge, and the AX = 0 ask ignores BX - it answers the bridge figure for an ask that will then be refused | RealDMG in place, the Video Player, PiXEL | S (skip the bridge term for those BX) |
| F4 | A compressed file that expands past 64KB cannot be read whole into a claim above 1MB (`dskw_tolo`, `diskw.inc` ~2078) - and every shipped data file is lz4-packed; a plain read into a bridge stops at the cut | Tracker's modules, RealDMG's packed cartridges, PiXEL's packed whole-file read | M |
| F5 | No shared helper to walk, or copy in and out of, a huge claim split at its windows (`OSAPI_XMEM_COPY` takes XMEM tokens only). A source-shared include that walks by the CX the cell answers works on every kernel | XTMP3, Audio, WaveRack, VBattle, the Browser, the Video Player | M, once |
| F6 | "How much can I have" counts conventional only on kern_mega: `OSAPI_MEM_AVAIL`, `MEMP_AVAIL`, `os88_mem_largest_kb` / `total_kb` | false refusals or undersizing in Weave and Loom's pre-read check, Frotz's pre-read check, WIN16's third arena, the Apple II's halving loop | S |
| F7 | The C SDK: `os88_mem_huge`'s thunk calls the cell with BX = SP (a latent bug: SP = 8000h asks for 32KB windows the caller then walks as 64KB; `xor bx, bx` fixes it); no wrapper for the AX = 0 ask; `os88_file_read_seg` / `_write_seg` cap the count at 16 bits and `os88_file_read_at` reads only into DS; no bulk copy between a claim and DS (peek/poke at ~11 us a byte) | RunCPM, any C package that wants huge | S each |
| F8 | Bridged and 16/32KB claims do not regrow | the Browser's growing buffers | minor |
| F9 | Unverified: the bridge's CONVENTIONAL piece is claimed through the DMA door, so it may be born pinned with no way for the package to unpin it - the mid-heap block 14.166 removed, which `megajw` would not see | everything that bridges | verify with F1 |

SPEC.md 50.3.5 names neither the bridge nor the AX = 0 ask (both are only in
the SDK comment and KERN-MEGA-MEMORY-PLAN); the step-only rule, or the
facility that makes it unnecessary, belongs in the contract.

## 3. The packages

**SHOULD** - a clear win once the facility is right:

| package | buffer today | with huge | effort |
|---|---|---|---|
| XTMP3 | file hold: one XMEM block (the pool's run) or 192K conventional, else streams (`x3hold.inc`) | 688K / 1,328K / 3,376K held, against 239K / 879K / 2,926K; a UI-task copy-out reader, bridge-safe walked by window (F1 for `x3h_at`) | S pool-only up to 1MB; M with the bridge |
| Frotz | a story over 64KB is a CONVENTIONAL `MEMP_CLAIM` for the whole session (`zio.inc`) | already walks 32KB pages with 64KB reach - exactly `BX = 8000h`'s shape, which never bridges: 100-512KB out of conventional memory, up to 15 fewer LDT entries a story; the pre-read refusal needs F6 | S (~40 bytes, no arm) |
| WaveRack | play hold, conventional through `MEM_P` and a bound window, ~480K on the 1MB machine | 688K / 1,328K / 3,376K: at 11 kHz, ~44 s held becomes 1 / 2 / 5 minutes | M (`wt_hoff` promises 64KB from ES:DI; callers split at windows); a `$(MEGAPKGS)` arm for non-trivial bytes |
| PiXEL | master, `BX = 8000h` already, capped at 1MB (`PX_MWN`) | no change at 1MB; 560K -> 879K at 2MB; up to 2.9MB at 4MB with the cap raised on the arm | M (needs F3) |
| WIN16 | `w16_vclaim` asks BX = 0 over 64KB, so it can bridge, and reaches in by `SEG_SUB` / `SEG_LIN` | correctness: F1 | S, or nothing if F1 is fixed in the kernel |

**COULD** - wins on big files or big machines:

| package | what | effort |
|---|---|---|
| RunCPM | eight open files each held whole, refused past 65,535 bytes (three master-disk files are not shipped for it); a 64KB window is exactly 512 records | M (F4, F7) |
| Tracker | the module, pool-only up to 2.9MB at 4MB; per-sample windows unchanged | S (F4) |
| MIDIRack | the wavetable bank: up to 640K of conventional memory back, or a bigger bank | S |
| Audio | a whole-file hold built from XTMP3's `x3hold.inc`, into the existing `AP_RSEQ` hook | M (F5) |
| Video Player | hold / bank; or a second conventional bank of 32KB slots, ~450K more on the 1MB machine without F3 | M |
| VBattle | bundle hold: reads are copies, bridge-safe with a splitting walker | M (F5) |
| the Browser | source and document: pool-only, because it uses `SEG_AT` | M (F1, F8) |
| ARCADE | MAME-zip import: lifts its 448K cap | S |
| RealDMG | cartridge RAM kept as 32KB of a 64/128KB cart (check the core's bank addressing first); a packed cartridge over 256K refused (F4); a cartridge over the pool's free refused (F3) | S-M |
| Sheet | cells capped at 32KB = 1,638 cells; 64KB claims in its existing arm double that (M), huge ~52K cells (L: a sorted array searched with a 16-bit `mul`) | M / L |
| Word, Scribe | documents of hundreds of KB: one flat segment walked with 16-bit DI everywhere | L; Scribe's pictures over 64KB M |
| WIN16 | arenas sized from the pool-aware ask (S); lifting its 1MB virtual-paragraph map (~300 sites, L) | S / L |
| Weave, Loom | the pre-read refusal checks conventional memory while the claims go to the pool | S (F6) |
| XTMP3 | Decode First into RAM at 4MB, no hard disk needed | L |

**NO** - fixed, screen-sized or streamed by design: Recorder, CDRack,
FontView, Paint (the canvas is screen-sized), ArtfulType, Lemmings,
Excitebike, 1942, DrMarco, Skies, Pixelstein, isoTron, WireBall, WireFleet,
FTPD, Telnet, The Wire, WireChat, Chart (40 bars is the real cap), CWORD (no
far pointers in C), Note Pad, TexPad, Calc, DOS (kern_mega always takes the
trip; its XMS arena is already the pool), the C64 (the D64 is already an XMEM
block), the Apple II, Install, System Check (its landing is a deliberate DMA
target), the Task Manager.

## 4. Order of work (a proposal)

1. **F1 + F2 + F9 in the kernel**, with a probe that claims a bridge on a 4MB
   machine and compares `SEG_SUB` / `SEG_AT` / `SEG_LIN` and a file read across
   the seam against the step walk - red today if the reading is right. This is
   a defect fix whether or not any package adopts huge, and WIN16 is exposed
   now.
2. **F3, F6, F7** (small), and the step-only rule into SPEC.md 50.3.5.
3. **F5** (the walker include) and **F4** (packed whole-file reads past 64KB).
4. **XTMP3 and Frotz** as the first adopters; then WaveRack and PiXEL; then
   the COULD list by what users would notice.

## 5. Status

| item | state | where |
|---|---|---|
| F3 | **done** - the ask reads BX; 16/32KB windows are answered without the bridge, which they never get (and should not: overlapping windows cannot straddle a cut, SPEC.md 50.3.5) | KERN-MEGA-PLAN 14.168; tests/megahuge leg 7 |
| F6 | **done** - a new ask rather than a changed cell: `OSAPI_MEM_HUGE` AX = 0, BX = `MEMH_AVAIL` (AX largest KB-door claim, BX total, DX the pool's run; CF = 1 on kern_big/small, where `OSAPI_MEM_AVAIL` is the answer). Callers moved: the C SDK's `os88_mem_largest_kb`/`_total_kb` (Weave, Loom, Apple II, C64, Lemmings, RunCPM at their rebuild), Frotz, WIN16's third arena | 14.168; tests/megahuge leg 8, chuge-mega |
| F7 | **done** - `os88_mem_huge` BX = 0; `os88_mem_huge_w`, `os88_mem_huge_ask`, `os88_file_read_huge`/`_write_huge`/`os88_file_hi`, `os88_file_read_at_seg`, `os88_seg_get`/`_put` (SPEC.md 73.9.2) | 14.168; chuge-mega, chuge-big |
| (found) | **done** - a plain file over 64KB was refused FERR_IO into any claim above 1MB (`dskw_rbody` -> `dskw_tolo`); it now takes the selector arm's own walk. Through a BRIDGE that walk is F1's | 14.168 |
| Frotz | **done** - a story over 64KB is one `OSAPI_MEM_HUGE` claim, BX = 8000h, its page table the claim's run (`zm_run`) | SPEC.md 61.4; megafrotz, megafrotz-low |
| F1, F2, F9 | **done** (mega-hugebridge) | KERN-MEGA-PLAN 14.169 |
| F5 | **done** - `apps/os88huge.inc`: a record and `hw_aim`, `hw_get`/`hw_put`, `hw_claim`, `hw_fread`, `hw_fwrite`, every piece split where a window ends (SPEC.md 50.3.8) | KERN-MEGA-PLAN 14.170; os88huge, megahw, megahw86, megahwbig |
| XTMP3 | **done** - the arm's hold is one huge claim, a bridge allowed: 192 -> 608 KB held at 640K + 384K, 2,848 -> 3,232 KB at 4MB | SPEC.md 121.6.1; 14.170; x3huge, x3mega |
| F4, F8 | open | |
