# Purgeable claims for packages: the use cases

A follow-up to the 2026-11-08 review (`docs/reports/BIG-REVIEW-2026-11-08.md`,
theme 6). It answers the owner's question:

> *"Packages being able to make a purgable claim: Find me a list of usecases to
> prove its worth doing. What package makes claims that could be purged and
> then recovered when the package comes back to need them?"*

Everything below comes from reading the code and the records at the tree's
current HEAD. Nothing was built or booted. The part sizes come from the `.o88`
files already in `build/`: each part table was decoded with
`tools/os88pkg.py`'s `image_unwrap` and `part_table`.

---

## 1. What the review proposed

Two findings say the same thing: `hm-9` (`areas/histmem.md`,
`data/histmem.json`) and `mem-15` (`data/memory.json`). The amended form is in
`synth/systemic.md:890-916`, `synth/phase1.md:340-354` and the main report's
section *6. Memory* (`BIG-REVIEW-2026-11-08.md:513`):

- **The verb.** `MEMP_PURGEABLE` is appended to `OSAPI_MEM_P`, with AX = the
  claim's relocation proc. The claim is already declared movable.
- **The flag.** `MC_DMA` bit 13 carries it. That needs `MC_DMA_HEAD` narrowed
  from 0x3FFF to 0x1FFF (`kernel/memory.inc:156-169`).
- **The rank.** It is fixed at `MEM_PG_TRIV`.
- **The purge.** The kernel calls the holder's existing proc with
  **DX = 0, meaning "purged"**, after `mem_can_move` agrees. It does this at
  IF=1 under `sch_lock`, as `mem_reloc_call` does (`memory.inc:3863`), with
  `mem_cp_busy` set across the call. `mem_shed_one` (`:1417`) runs inside
  `pushf/cli`, so it either skips these records or leaves their purge to the
  compactor's drop arm.
- **Recovery.** The package's own "this is gone" path: it sees a zero word and
  rebuilds or re-reads.
- **Cost.**
  - kern_big: **+35..+80 `.cold`**.
  - kern_small: **+4..+6** for a real refusal. Today `osapi_mem_p` answers an
    unknown verb as AVAIL with CF clear.
  - kern_mega: refuses, or invalidates the selector.
  - ABI: additive, so `abilock --update` without a bump.
- **The two cited consumers.**
  - Skies' title art: "-485 ms of exit" (`docs/plans/HANDOFF-APPS-SHARED.md:153`).
  - Tracker's Text Screen shadow: "~420 ms to rebuild" (SPEC.md 45.13.8).

**One of those two consumers no longer exists.** Skies' title bands were moved
into a bss union with the bracket's tables (SPEC.md 88.10.7). The owner then
decided to keep the packed stream in the region and decode only the two bands
the title page draws (SPEC.md 88.10.7.1, `apps/skies/skies.asm:800-830`). That
landed in `b47de145` (#252), the same squash that wrote the HANDOFF row. Leaving
a flight now reads no disk, and the decode is a fraction of the 192 ms that
decoding all six bands costs. The HANDOFF row is stale.

---

## 2. What the kernel already does for this, with no package API

These cover most of what a package-owned purgeable claim would be used for.
Some of them do it on the package's behalf.

| mechanism | where | what it does for packages |
|---|---|---|
| **The window raise cache, `MEM_P_WSAVE` (TRIV)** | `memory.inc:363`; `OSAPI_WM_SAVEU`, `OSAPI_WM_BAND` (`apps/os88api.inc:5711`) | **The largest package-content cache in the system is already purgeable**, kernel-owned on the package's behalf. Paint banks its whole 1bpp content (~9KB) or its tool band (~13KB on colour). That saves the 399 ms canvas reduction on a raise (`apps/paint/paint.asm:13086-13097`): *"a PURGEABLE claim ... the memory goes back the instant anything else wants it"*. Users: browser, calc, fractal, ftpd, isotron, mines, notepad, paint, piano, scribe, solitaire, taskmgr, telnet, texpad, word |
| The icon store `MEM_P_ICO`, the menu save-under `MEM_P_MSAVE`, the file list `MEM_P_FLIST` | `memory.inc:182-189, 429` | Icons drawn for every package's files, and menus over every package |
| The directory read-ahead `MEM_P_DIRW` (HIGH) and the FAT windows `MEM_P_FATW` (MED) | `memory.inc:373, 439` | The disk cache every package's file I/O rides on. DOS's "disk cache" dial controls this kernel claim rather than owning one (`apps/dos/dosset.inc:1064-1110`) |
| Shed and retry on `mem_claim`, `mem_claim_hi` and `mem_regrow` | SPEC.md 50.6.2, 50.6.2.1 | A package's claim takes the kernel's caches before it is refused |
| Purge levels and the claimant's floor (`OSAPI_MEM_FLOOR`, `OSAPI_MEM_AVAIL` at a level) | SPEC.md 50.6.4, 50.6.6 | A package can choose to compact the disk cache without destroying it |
| Compaction, movable claims and movable regions | SPEC.md 66; `OSAPI_MEM_MOVABLE`, `OSAPI_MEM_COMPACT` | Removes the *wall*. It does not give memory *back*: a movable 80KB claim is still 80KB |
| **Lazy parts with `op_drop`** | `apps/os88partsbody.inc:1934, 2297` | A package-controlled discard. A dropped part is re-read by the next `op_fetch`. This is purgeability with the package deciding *when* instead of the heap deciding under pressure |
| Package discipline | `apps/fractal/fractal.asm:270-279`, `apps/thewire/thewire.asm:2980` | An optimisation grows only when twice the increment is free. A catalog shrinks to fit. Staging claims are transient (Word's save and load staging, `apps/word/word.asm:8917, 9072`) |

The missing piece is narrow. **Packages own caches, and those caches cannot be
taken back under pressure.** There is also no kernel-to-package "memory is
short" notice, which would be a GrowZone or an AmigaOS `AddMemHandler`.

---

## 3. Survey: package claims whose contents can be rebuilt

There are 314 claim sites under `apps/` (asm and C, host tests excluded). Most
fall into four classes that purgeability cannot help:

- **Document state**: the text, CHP and PAP arenas in Word, Notepad, TexPad
  and Scribe; Sheet's cells, text, borders and notes.
- **Undo snapshots**: Sheet's 16KB (`sheet.asm:382`), CWORD's `cw_u_save`
  (`apps/cword/cword.c:2219`), Word's and Notepad's undo arenas.
- **Transient operation buffers**: freed at the end of the operation anyway.
- **The live working set of a running game or emulator**: in use every frame.

Every remaining class is listed below.

### 3a. Disk-reloadable code and assets: lazy parts held after their first use

This is the Windows DISCARDABLE-segment case and the Mac purgeable-resource
case. `op_fetch` already knows how to re-read a part, `op_seg` already answers
0 for an absent part, and `op_lazymv` (`os88partsbody.inc:2458`) is already the
relocation proc.

| package | part (in-memory, on disk) | lifetime today | how it comes back | what a purge would buy |
|---|---|---|---|---|
| **Sheet** | chart, formats and dialogs module, 52,339 B (37.5KB packed); macro module, 28,672 B (13.4KB packed) (`apps/sheet/sheet.asm:57444-57445`) | **Fetched at every launch** (`sheet.asm:2724-2727`), **held until exit**. SPEC.md 82.16.11 says *"80KB of wall"*: movable since P1b, but never given back | `ch_ovneed` and `sh_m2need` already re-fetch when the word is 0 (`apps/os88chartovl.inc:241-245`) | **~80KB** while a sheet sits open in the background. The re-read is one `op_fetch` each, ~51KB off the disk: a few cylinder runs, ~1-2 s on an XT floppy |
| **C packages with a lazy overlay** (LOOM, Lemmings, Apple II, WEAVE: `CC_OVL_LAZY`) | the `ovl_*` module | `cc_ovneed` (`apps/cc/crt0.asm:1124`) fetches on first call and never drops | `cc_ovneed` again | The module's size. These disks are not in `build/`, so sizes are not measured here |
| **xtmp3** | MP3 backend, 57,736 or 58,698 B (20-24KB packed) | **Dropped after each file** (`apps/xtmp3/x3eng.inc:4296-4306`, *"a finished file plays with no decoder resident"*) and re-read for the next | `op_fetch` (`x3eng.inc:4187`) | The other way round: a purgeable keep saves **one ~22KB read per track** of a playlist and gives the 58KB back under pressure |
| **WIN16** | loader and census part 5, 18,812 B; dialog/library part 3, 64,164 B; others 0.5-18KB (`apps/win16/win16.asm:213-232`) | The loader is fetched per program load and dropped (`win16.asm:1966`). The dialog part is dropped by `w16_dlgout` (`:2016`) | `op_fetch` | A keep saves a ~14.5KB read per Windows-program launch |
| **PiXEL** | nine tool parts, 6-23KB each (one is 27.6KB) | **Dropped straight after each use** (`apps/pixel/pixel.asm:1880, 2094`) | `op_fetch` | A keep saves one 5-14KB read per repeated tool use |
| Browser painter 3.3KB, Video p0 4.7KB, RealDMG p3 24KB | small or rarely repeated | dropped or held | - | marginal |
| 1942, Arcade, DrMarco, Pixelstein, WireFleet, Skies art and sound parts | 2-61KB each | Fetched for a screen, dropped once in VRAM or on leaving it | - | **None.** These run inside an `OSAPI_FSX_RUN` bracket, where nothing else can claim, so there is no pressure for a purge to relieve. Keep them as they are |

**What the code case costs on top of the kernel.** A part holding code must not
be purged while one of its frames is on the UI stack. A claim made from inside
the module would otherwise shed the module and return into freed memory.

- Sheet's `ch_xthunk` (`os88chartovl.inc:92`) already dereferences a TAG on the
  way back (SPEC.md 82.16.11). It would need a pin count, or a re-fetch on
  return (Windows' return thunk).
- `cc_ovthunk` is the same shape.
- `mem_in_nest` pins only the region (SPEC.md 66.6.2).

The pin is a few bytes in the shared source, not in the kernel.

### 3b. Caches the package recomputes, already backed by a slow path

This is the shape the kernel's own contract asks for: *"a zero in that word must
ALREADY mean no buffer, do it the slow way"* (`memory.inc:310-325`).

| package | claim | size | life | rebuild today | purge buys / costs |
|---|---|---|---|---|---|
| **`os88type.inc`**: Word, CWORD, Scribe, PiXEL, FontView, WIN16 GDI | the pre-shifted glyph table `ty_psseg` (`apps/os88type.inc:1179-1215`) | **6-12KB** (6 at 8 rows, 12 at 16) | session | **~160 ms**. Without it every line takes the general compose, 2.01x slower (`:1158-1166`) | 6-12KB per open editor. Shape already exactly right: CF=1 means "general compose" |
| `os88type.inc` | the face file (`:785`) | up to `TY_FACE_KB` = 8KB | session | one file read, ~0.4 s | 8KB. LOW rank (disk I/O) |
| **DeskClock** (Sticky App, on the desk all session) | face cache `dk_cseg` (`apps/deskclok/dkwid.inc:683-724`) | **2KB on 1bpp, 7KB VGA Medium, 12KB Large**, per clock (SPEC.md 110.14.4) | as long as the clock is on the desk | one whole compose. Without the cache it is *"290 ms a box on a 4.77 MHz 8088"*, and a refused claim already composes every paint whole | 2-12KB per clock that the user is not even using. **The strongest recompute case**: resident forever, with the cheapest loss |
| **Tracker** | Text Screen shadow `ttx_shseg` (`apps/tracker/tracker.asm:1389-1396`) | 9,676 B (9.5KB) | **freed on every bracket exit** (SPEC.md 45.13.8) | **~420 ms**, synchronous on re-entry | The other way round: a purgeable keep saves 420 ms per `F` press and gives 9.5KB back under pressure. SPEC.md 45.13.8 names this API as missing |
| **Sheet** | live chart canvas (`sheet.asm:388`, 2712) | 19KB | session | a re-render of the chart | 19KB. Sheet is at `MEM_OWNER_MAX` = 8 claims, which is why it is held |
| **Fractal** | run cache (`apps/fractal/fractal.asm:1198`, regrown at `:858`) | 4KB, doubling to 32KB | session | **recompute: ~0.5 s a row**, a whole frame in tens of seconds (`:60-83`). A refused claim already falls back to restart-on-repaint | 4-32KB. The loss is severe, so it would need a HIGH rank, not TRIV |
| RealDMG | recompiler cache 64KB + 16KB block table (+ further 64KB tiers) (`apps/realdmg/realdmg.asm:1396-1412`) | 80KB+ | while a ROM runs | retranslation. The interpreter is the fallback, 51 vs 21.7 clocks per M-cycle | Large, but it is used every frame and the emulator's worker pins it (`mem_can_move`: a live worker until parked). It already gives the cache back when the user picks Interpreter (`:1685-1714`). Not a practical first consumer |

### 3c. Not candidates, and why

- **Sheet's 32KB staging** (`sheet.asm:302`). It is scratch, not a cache, and
  is held only because Sheet is at the 8-claim cap. The answer is
  claim-per-operation, or the review's `apps/os88lmem.inc` sub-allocator
  (hm-12b), not purge.
- **VBattle's clip cache** (`apps/vbattle/vbcache.inc`). It lives in XMS or
  EMS, outside the conventional heap the shed works on.
- **Audio and Video rings, Word and Notepad staging.** These are transient.
- **Emulator RAM and state** (C64, Apple II, RealDMG banks). These cannot be
  rebuilt. RealDMG's bank cache is already an internal cache, refilled from
  the ROM file.
- **TheWire's catalog** (`thewire.asm:2858-2980`). It could be rebuilt over the
  network, but it already shrinks to fit.

---

## 4. What the period systems used it for in practice

- **Windows 3.x.** Almost all discardable memory was **code and resources** the
  loader could reload from the EXE: NE segment flags MOVEABLE, DISCARDABLE and
  LOADONCALL, and `LoadResource` with `LockResource`.
  - The repository's own record: *"Almost all of a Windows program's code is
    marked DISCARDABLE: Write 3.0 has 163 KB of it in 78 segments and no fixed
    code at all, Windows Help 120 KB, Paintbrush 109 KB"* (SPEC.md 124.26).
  - WIN16 re-implements exactly that inside its own arenas (124.26.5:
    discarded when *its* global heap is short, least recently used first, with
    the stack scanned rather than patched).
  - Application data allocated with `GMEM_DISCARDABLE` and re-filled after
    `GlobalReAlloc` or a `GMEM_DISCARDED` check was the rare case.
    `CreateDiscardableBitmap` is one of them, and WIN16 exports it
    (`apps/win16/w16api.inc:298`).
- **Mac (System 1-7).** Purgeable handles were overwhelmingly **resources**:
  CODE segments (`UnloadSeg`), fonts, PICTs and sounds, each reloaded by the
  Resource Manager after an `HPurge`, `EmptyHandle` or NIL master pointer.
  `GrowZone` was the application's hook to free its *own* reserve when a
  request failed. App-built purgeable caches existed but were the minority.
- **GEOS.** Discardable **code resources and UI object blocks**, reloaded from
  the geode or VM file. Swappable blocks went to EMS or disk.

**The common pattern**: the thing purged is almost always **something the
system's loader can reload from the program's own file**. That is the
equivalent of a lazy part here. App-built recompute caches came second.

---

## 5. Conclusion

**Is there a real use case?** Yes, but a narrower one than the review argued,
and it is **conditional on memory pressure**. The memory comes back only on a
machine that runs out, which means 256-384KB machines and busy multi-window
sessions. On a 640KB desk with one or two apps nothing is ever purged, and the
feature costs its kernel bytes for nothing.

One of the review's two cited consumers (Skies) is already solved by other
means. The other (Tracker) is real but small.

The consumers worth naming, in order of what they free:

1. **Sheet's two modules: ~80KB**, held from launch to exit today. This is the
   one large, concrete case. Recovery code already exists (`ch_ovneed` and
   `sh_m2need` re-fetch on a zero word). It also needs a frame pin in
   `ch_xthunk`.
2. **Lazy parts that packages drop to save memory and then re-read**: xtmp3's
   58KB backend per track, WIN16's 18.8KB loader per launch, PiXEL's 6-23KB
   tools per use. Purgeable turns *drop and re-read every time* into *keep
   until the heap needs it*. The win is disk reads saved, roughly 0.4-1 s each
   on an XT, not memory.
3. **Small recompute caches**:
   - the type pre-shift table, 6-12KB per editor across six packages;
   - DeskClock's face, 2-12KB per clock, resident forever;
   - Tracker's shadow, 9.5KB and 420 ms;
   - Sheet's chart canvas, 19KB.

   Each is a few KB, and each already has its slow path.

**Not worth it for**:

- fullscreen games, because nothing claims during an FSX bracket;
- emulator JIT caches, because the worker pins them;
- documents, undo and scratch.

**The smallest API, if it is built at all:**

- **The kernel part is the review's, with one change.** Take the rank in AL
  rather than fixing it at TRIV. A part re-read is disk I/O, so it is LOW by
  the kernel's own scale (`memory.inc:340-343`). A recompute is TRIV, and
  Fractal would be HIGH. Cost stays **+35..+80 `.cold` on kern_big**, and the
  low end is enough: purge only from the compactor's drop arm, on the UI task.
  kern_small refuses (+4..+6). kern_mega refuses.
- **The package part goes in shared source, not per package.**
  - `os88partsbody.inc` gains `op_keep`, "a part I am done with but would like
    to keep". It declares the fetched part purgeable through `op_lazymv`.
  - `op_lazymv` learns that DX = 0 means "drop the row": clear `OP_FETCHED`,
    zero the banked word, and call the package's hook as it does for a move.
  - Then Sheet, xtmp3, WIN16, PiXEL and the C overlay arm each change one
    `op_drop` (or the absence of one) into `op_keep`, plus the code-part frame
    pin.
  - A recompute cache (type table, clock face) declares itself with the bare
    verb and a proc whose DX = 0 arm zeroes one word: ~6-10 bytes a package.

**Recommendation.** It does not need to be done now. When it is done, it is
justified by **Sheet's 80KB** and by the lazy-part keep, not by the two
consumers the review cited.

- **Measure before spending the kernel bytes.** Boot a 256KB profile with Sheet
  open behind a second application and read `tests/heapmap.py`. If Sheet's
  modules are what makes the second launch fail, the case is proven.
- **The zero-kernel-byte alternative.** Sheet could `op_drop` its modules after
  each use, as PiXEL does, paying a ~1-2 s re-read on every chart or file
  dialog. That is precisely the trade a purgeable claim exists to avoid, and it
  is the comparison to put to the user.
