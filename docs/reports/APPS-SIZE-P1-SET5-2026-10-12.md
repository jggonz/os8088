# Apps size pass 1, set 5 — measured 2026-10-12

**A MEASUREMENT** (docs/README.md): true of the trees below and of no other.
A later measurement is a new file. All eight packages merged; the coverage
line below is the merged tree's.

The fifth batch of the first apps size pass: packages 33-40 of a raw
`ls apps/` - **paint, piano, pixel, pixelstein, recorder, redline, runcpm,
scribe** - none of them in `apps/RETIRED.txt`.

- **Tree**: branch `apps-size-5`, cut from `apps-size-4` (set 4, not yet on
  `elendilon`) with `elendilon` at `0c6d6796` merged in: `b13cfe60`.
  "Before" is `b13cfe60` built in this container.
- **Box**: the 4-core cloud container this session ran in; every figure is
  counted, not timed, unless a row says it reports cycles off MartyPC.
- **Method**: as sets 1-4, one agent per package, with set 5's brief carrying
  set 4's findings (the `stkapps` gate's blindness to `OS88_POPLADDER`, the
  ladder's disk cost) and, at the owner's direction, NO time or capacity
  limit.

## The table

| package | image+bss before | after | % | file before | after | runtime claims |
|---|---:|---:|---:|---:|---:|---|
| paint | 33,278 | 32,538 | -2.2% | 21,983 | 22,064 | an idle Paint holds **13 KB less**: the 9 KB fill stack is claimed with the fill tool and freed on the next tool, the clipboard's 4 KB floor is gone (a Copy claims what it holds). Counting that, 46,590 -> 32,538 (-30%). Stroke, blits and decoders untouched; fill equal within noise |
| paint (APP_SMALL) | 24,544 | 21,522 | -12.3% | 16,784 | 15,143 | one bit per pixel by construction (`PT_ONEBIT`): the packed and planar code no small build can reach is not assembled (-2,644 image); small fill 11.9% faster |
| recorder | 7,807 | 3,621 | -53.6% | 2,976 | 2,723 | region 8 -> 4 KB; the 4,000-byte read-back buffer became a 4 KB claim held only during a speaker play (never made with a card) |
| piano | 4,551 | 4,095 | -10.0% | 3,013 | 3,052 | none (Piano claims no heap). Every hot path faster on MartyPC (white key redraw -1,031 cycles, viewer -2.1%); 15 checkpoints pixel-identical to the base on CGA and VGA. **Then the owner's three fixes** (+479 image, +27 bss over the pass's 3,589): the tone first - key -> tone **41 ms -> 0.76 ms** on a 4.77 MHz 8088 - the viewer appended per changed column rather than filled and repainted (977 transient pixels -> 0 on a scrolling key), each black key drawn once (keyboard paint -17%); every final screen pixel-identical to the base |
| pixel | 52,590 | 51,983 | -1.2% | 114,684 | 113,660 | the WRITE part's claim **48 -> 6 KB** while held (Save As's card and dialog, Copy Info, a BMP save): its 42,240 zero bytes are now a scratch claim made only during a save and sized per format (PNG 42 KB, GIF 25, PCX 9, PIX 1, BMP 0); the decode ring 33 -> 32 KB (17 -> 16 on the small ring), the packed file's claim file KB + 2 -> + 1 (guard 6b already aligns every base). The head claim keeps its KB on purpose: without it BIG.BMP's ring forced a 332 KB compaction, 3.52 -> 4.69 s to first rows. Decode, render, pan, zoom and animation paths instruction-identical |
| pixelstein (part 0) | 59,386 | 58,241 | -1.9% | 51,386 | 46,778 | the number that matters: launch heap on a 256 KB 8088 (Flat rung) **86 -> 77 KB**, a 640 KB 8088 with everything 264 -> 254 KB - shadow claim 16 -> 9 KB on an 8086, scaler scratch 51 -> 50 KB, the level stream packed and shrunk to 10 KB on every volume (was up to ~41 KB on a 32 KB-cluster disk). Frame code untouched; texture and sprite sets SHA-1-identical |
| scribe | 53,160 | 51,639 | -2.9% | 34,317 | 34,391 | overlay claim 12 -> 10 KB for the session; Open's staging claim the file's own KB (WELCOME.DOC 62 -> 4 KB); Insert > Picture peak 104 -> ~43 KB for a 2 KB picture |
| redline | 51,540 | 38,983 | -24.4% | 15,153 | 14,914 | none. The workload buffers (8 KB) alias benchlib's save copy `bl_out` (never live together); the report arena 10,000 -> 8,192 with a new `arena_headroom` check (worst real report 7,703); timed bodies untouched, CGA counts identical within noise |
| runcpm | 44,019 | 41,359 | -6.0% | 21,111 | 20,760 | module claim 6 -> 5 KB (RUNCPM.OVL 5,912 -> 5,018); AUTOEXEC no longer cached; one session -3,684 B. BDOS paths measured equal or faster (F_READ 27,275 -> 26,904). One narrowing: a file of 16 MB+ on a hard disk lists as 16 MB - 1 (CP/M 2.2 addresses 8 MB; SPEC.md 74.3) |
| **total (shipped arms)** | **306,331** | **282,459** | **-7.8%** | **264,623** | **258,342** | plus the heap: Paint's idle 13 KB, PiXEL's WRITE part 42 KB, PIXELSTEIN's launch 9-10 KB, Scribe's overlay 2 KB and its open staging, Recorder's region 4 KB, RunCPM's module 1 KB |

## Coverage

The merged tree (`apps-size-5`, all eight packages plus the `lzload` fixture
fix and Piano's three owner-approved fixes) built clean (fast tier 41/41) and
ran the union of the eight agents' rows, plus the shared rows their changes
can reach (the `disp*` display set, the region/compaction rows `reg*`,
`heapcheck`, `small128`, `smallboot`, `smallreq`, `appsmall`, `ddsmall`,
`shedrelist`, the `blit*` and `alert*` rows, `curdisk`, `curbusy`,
`trkbigmod`, `wmchrome`, the DOS box's `dospkg`/`dosopen` and the C
toolchain's `ccmake`/`ctoolchain`/`cpmcache`/`allapps`), through
`tools/os88soak.py` at lane 4: **163/164 ok in 56:47** - every `paint*`,
`px*`, `pxs*`, `pixelstein*` and `redline*` row, `recorder`, `scsize`,
`lzload`, `lzload-lz4`, `regrowshed`, `pixjpegref`, `paintheld`,
`paint1small`. The one failure was **`pxdrawvga`**, and it is
**intermittent on the base, not set 5's**: `tools/os88bisect.py sample` read
the base `b13cfe60` at **3/4 failed** and the merged tree at 0/4 (2/8 over
every run of it this session, one of them alone). Every failure is the same
wait - the Rotate tool's key pressed while BOUNCE.GIF plays, and `ui_done`
never seeing the UI idle in 180 guest seconds (the frame job keeps the lock or
the drive busy). Recorded in HANDOFF-APPS-SHARED.md 0.3 as a harness/package
question; nothing in this set touched the animation path.

Not covered by any row:

- **RunCPM and Piano have no soak row of their own.** RunCPM's agent ran
  `make rczex` (ZEXDOC in the OS), `rcz80test`, `rcmemtest` and its own
  `rcuitest` cases; Piano is reached only by `lzload` (it is the LZB
  subject) and its agent's MartyPC checkpoint scripts (15 checkpoints
  pixel-identical to the base on CGA and VGA).
- **`pxanimemu` and `pxextraemu` report `ok` in 0.1 s and ran nothing**:
  Unicorn is not installed in this container, so they skip and exit 0 (and
  `pxsave`'s host leg skips the same way). This is a false green on the base
  too; PiXEL's agent ran them with Unicorn installed locally (179 fixtures,
  31 GIFs). Recorded in HANDOFF-APPS-SHARED.md 0.3.
- PiXEL: a save that needs all eight claims at once (a playing GIF, undo and
  the colour face), the PCX scratch at the full 8,192 width (the 9 KB bound is
  `PX_DIMMAX + 1` arithmetic), and the ring on hard-disk cluster sizes.

## Defects

- **Recorder: none of its four buttons ever fired.** The drag and release
  handlers lost DX (to `OSAPI_WM_CONTENT`) and CX (to the button painter), so
  the hit test ran at the content top. Fixed; SPEC.md 35.2. A scan of every
  package's drag/release handlers for the same mistake found no other.
- **Set 4's Mines broke the `lzload` fixture**, found by the Piano agent:
  `$(LZCDIR)/mines.o88` forces `--compress=lz4`, and Mines no longer fits its
  region packed (2,096 B of 2,048), so `lzload360.img` cannot be built and
  `lzload`/`lzload-lz4` fail. Set 4's coverage run did not include them. FIXED on
  this branch: the fixture's byte-compared LZ4 package is now FRACTAL, chosen
  on the machine (0 bytes differ after opening, against 2-28 for eight other
  shipped LZ4 packages); both rows green, and red on a flipped byte.
- **Scribe: Insert > Picture could write past its claim.** It read with a
  0xFFFF capacity into a 62 KB claim, so a 63,489-65,535-byte picture landed
  up to 2 KB past it; now refused as too big. And a small document needed
  62 KB free to open - now sized from the file. Fixed (SPEC.md 95.13).
- **Scribe, not fixed**: nothing checks that `SCRIBE.OVL` and `SCRIBE.O88`
  come from one build - the overlay reaches the package's bss by offset, so a
  stale overlay loads and reads an empty document (`make scribe` does not
  refresh `build/ovlz/`; `make scribedisk` does). `scsize` refuses a stale
  pair; the fix (a layout stamp checked in `sc_ovload`) is a handful of bytes.
- **Paint: its private About card drew with no clip region** (over windows in
  front of it) - fixed by taking the shared card.
- **Paint's small arm broke `regrowshed`**, whose "too big" subject was the
  small PAINT.O88 - now 15,143 bytes on disk, under Note Pad's 16 KB limit (the
  probe sees the FILE, not the unpacked image). Fixed: the row adds its own
  24 KB `TOOBIG.TXT` to a scratch disk and guards on the directory entry.
- **Recorder, not fixed**: SPEC.md 35's "stays useful with no card" is false -
  a grant needs the Sound Blaster tier; the SPEC text now says so.
- **PiXEL: a JPEG's first rows could stall behind a 16 KB read.**
  `px_pumpfill`'s interlaced rule (rows decoded and none complete, so fill
  ahead) also fired for a JPEG part-way through a band, because a JPEG's rows
  complete a band at a time; whenever a progress wake landed mid-band, 16 KB
  was read before the first rows (FAST OPEN 4.94 s instead of 3.6, 1/1 6.37 s
  instead of 5.0, depending on timing). Fixed, +7 bytes: a `PXKF_DCT` part
  waits as a complete row does.
- **PiXEL, not fixed**: `tests/pxfsx.py` (~line 484) has an `until()` race -
  the guest can hit the intended `px_fsmain` breakpoint between the condition
  check and the stopped-machine check, after every assertion has passed (seen
  once under load, green twice after); and the Unicorn skip reading as `ok`.

## Missing components and shared wins (folded into HANDOFF-APPS-SHARED.md 0.3)

- `OSAPI_FERR_TEXT` has **four** carriers now (Paint ~190 B is the largest).
- `OS88_POPLADDER`'s disk cost is a rule, measured in Paint: **six or more
  pops pay** on both (image -125 / file +54 is still a RAM win worth taking),
  **four or five lose on disk** (-34 / +43); Scribe's -98 / +74 is the fourth
  data point.
- Kernel claim policy: `mem_claim`'s refusal path compacts BEFORE it sheds,
  and there is no query for a run of N without compacting - PiXEL measured a
  332 KB move where shedding a 30 KB cache would have done; any package-size
  change can flip a benchmark this way, which is layout and not code.
- `OSAPI_ICON_DRAW` with an implied solid mask (512 B in PiXEL, every
  `OS88UI_IMG` carrier pays 32 B an icon).
- `op_fetch`: an uncompressed lazy part keeps up to a cluster of head slack
  for the session (up to ~31 KB on a 32 KB-cluster disk) - PIXELSTEIN.
- `os88pkg.py --compress-if` bounds against 512 where the loader claims KB
  (Recorder); `os88pcm.inc`'s 256-byte table belongs to the caller
  (Recorder); `benchlib.inc`'s fixed `BL_MAXROWS` and whole-file `bl_save`
  (~900 B Redline, ~17 KB sysbench).
- C toolchain: five `ccpeep`/`cc8086` folds counted per package by RunCPM's
  agent, the largest `mov ax,_sym / mov bx,ax / [bx+k]` at ~720 sites and
  ~3 KB across eight C packages.
- A module stamped to its package's build (Scribe's stale `SCRIBE.OVL` reads
  an empty document), and `make scribe` not refreshing `build/ovlz/`.

