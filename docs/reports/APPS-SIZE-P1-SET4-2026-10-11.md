# Apps size pass 1, set 4 — measured 2026-10-11

**A MEASUREMENT** (docs/README.md): true of the trees below and of no other.
A later measurement is a new file. All six packages merged; the
coverage line below is the merged tree's.

The fourth batch of the first apps size pass: packages 25-32 of a raw
`ls apps/`, less the two the owner retired (`modplug`, `pacman` - their
agents were stopped before committing anything): **loom, midirack, mines,
missile, notepad, paccman**.

- **Tree**: branch `apps-size-4`, cut from `elendilon` at `8a9176f9`.
  "Before" is `8a9176f9` built in this container.
- **Box**: the 4-core cloud container this session ran in; every figure is
  counted, not timed, unless a row says it reports cycles off MartyPC.
- **Method**: as sets 1-3 (docs/reports/APPS-SIZE-P1-2026-10-10.md), one agent
  per package, with set 4's brief adding the shared round's helpers
  (docs/plans/HANDOFF-APPS-SHARED.md 0.1) and the process fixes from its
  section 7.

## The table

| package | image+bss before | after | % | file before | after | runtime claims |
|---|---:|---:|---:|---:|---:|---|
| midirack | 50,834 | 47,584 | -6.4% | 30,998 | 30,992 | wavetable bank claims its own KB, not a 32 KB rounding: GeneralUser ~224 -> ~206 KB, the gate bank 32 -> 7 KB |
| mines | 2,452 | 1,849 | -24.6% | 1,744 | 1,678 | none. The file ships PLAIN now: `--compress-if` refuses LZ4 because in-place expansion would need 2,096 B of a 2,048 B region, and the plain file is still smaller. Reveal flood -1.9%, new-game redraw -1.4% cycles (MartyPC) |
| paccman | 40,466 | 39,090 | -3.4% | 19,038 | 19,035 | none. Half the mirror-symmetric maze ships; the prelude and death tunes pack one byte a tick; `pmc_snd_tick` in play identical (2,485/2,504 cycles); a table-driven sound-effect rewrite saved 242 B and measured 3,009/3,206 - REFUSED |
| notepad | 20,276 | 20,233 | -0.2% | 15,266 | 15,310 | load: the file's own KB (was 16 KB whatever the file - a small file could fail "No memory"); save staging 2x -> note + 1 B a newline (full note 32 -> ~17 KB); undo arena freed on the last undo (up to -16 KB) |
| notepad (APP_SMALL) | 13,362 | 13,344 | -0.1% | 10,356 | 10,367 | as above, less the undo arena |
| loom | 51,804 | 47,746 | -7.8% | 27,894 | 26,929 | LOOM.WPV's claim 19 -> 17 KB (thunks gated after the C) and 13,916 -> 7,996 on disk (packed); LOOM.OVL 17,359 -> 17,060 packed; the 29 KB sources and Pack's 50 + 62 KB unchanged (format bounds). No compiled function changed size; `weavepack` (the machine byte-identity gate) green |
| missile | 16,200 | 15,893 | -1.9% | 10,812 | 10,689 | none. Almost all code (13.7 KB of routines); `mcperf`'s 400 frames 1.7% FEWER cycles with the same end state |
| **total (shipped arms)** | **182,032** | **172,395** | **-5.3%** | **105,752** | **104,633** | plus the heap: Note Pad's load and save claims, the wavetable bank's ~18 KB, LOOM's Preview 2 KB |

## Coverage

The merged tree (`apps-size-4`) built clean (fast tier 41/41) and ran the
union of the six agents' emulator and host rows through `tools/os88soak.py`
at lane 4: **31/31 ok** - midtab, midirackfm, midiracksb, midiracksbrate,
midirackspk, midiracktone, midirackend, midirackmpu, midirackcovox,
midirackcovoxpoll, midirackwt, mrdraw, mrdrawvga, dispmine, minexflag,
minesrc, pmcrom, paccman, dispnp, dispclose, editmove, npscroll, pkgthumb-np,
regrowshed, appsmall, lmpack, weaveprev, weavepack, mcperf, mcseg, dispmcfs.

Not covered by any row, by the agents' own account: MIDIRack's GeneralUser
bank on a large-cluster hard disk (the `READ_AT` fallback was reasoned, not
run); Note Pad's oversized-file early exit (the `toobig` assertion cannot see
it) and its last-undo arena free; Missile's windowed key bindings one by one;
a break-on-purpose run of `weavepack` against a wrongly shared LOOM table; the
WEAVE rows (no WEAVE file was touched).

## Defects

- **None found in the six packages.** Note Pad's load did have a latent
  behaviour bug - raising the claim to 16 KB on every load made a small file
  fail with "No memory" when only a few KB were free - fixed by sizing it from
  the file. `regrowshed` was red on the base (it expected 16 KB; the manual has
  shrunk) and is fixed.
- **A defect in the shared round's own work**: `tools/stkbalance.py` (the
  fast-tier `stkapps` gate) cannot see the labels `OS88_POPLADDER` builds with
  `%+`, so every package that adopts the macro fails `stkapps`. Note Pad and
  Missile both hit it independently and wrote the ladder out by hand, naming
  only the rungs something jumps to (an unreferenced label reads as an entry
  point). Recorded in docs/plans/HANDOFF-APPS-SHARED.md.

## Missing components and shared wins (folded into HANDOFF-APPS-SHARED.md)

- `OS88_POPLADDER` costs DISK under LZ4 again: Note Pad image -50 / file
  +20..30, Missile image -42 / file +34 - the third and fourth measurements
  after Cyclone's. Both kept it, because the image is RAM.
- C toolchain: literal pooling in `cc8086.py` (`apps/loom/lmpool.py` is the
  prototype: LOOM -2,795, and measured on the others CWORD 444, WEAVE 313,
  APPLE2 198, PACCMAN 121, RUNCPM 103, C64 15); a `ccpeep` fold of
  `mov ax,sym / add ax,imm` (which would make C unions free - a union cost LOOM
  +521..+1,202); a direct store to a constant address; and the observation
  that cc8086's table indexing is dearer than a compare chain (PACCMAN's
  refused rewrite).
- SDK: a form for "trailing data in the file, never loaded" (PACCMAN's 1,072 B
  licence); `OSAPI_FERR_TEXT` (the kernel already holds the strings; Note Pad,
  Word, Scribe carry copies); `os88ui_btninit`'s register contract documented
  (it keeps all); W_PAINT's clobber contract; a `READ_SEQ` door that accepts a
  sector-multiple last chunk.
- `WEAVE.WSM` still ships plain; `CC_OVLZ_RULE` likely applies.
- HANDOFF 3.6's grid hit-test row: Mines does NOT carry a rect table (its hit
  test is ~30 B of arithmetic) - struck from the carrier list.

## For apps size pass 2

- **MIDIRack was barely looked at, and pass 2 should go deeper.** Its agent
  stopped after ~14 minutes of a ~60-minute budget, believing it had run out
  of time, and took only the buffer aliasing (three output buffers in one
  2 KB page, the engines' voice state inside `mry_otab`) and the exact bank
  claim. Left on the table, by its own account:
  - `mrt_gmname` (1,492 B of General MIDI names), token-compressed to
    ~400 B - UI path only, not taken for time;
  - `OS88_POPLADDER`/`OS88_PUSHER` never priced (image AND file);
  - `mru_fc`'s 48-byte stride per field-cache slot (1,440 B);
  - `mrt_fn`/`mrt_fq` (1,536 B) are derivable from `mrt_pow` with one `MUL`
    - REFUSED, correctly, at ~120-130 cycles per pitch in the event pump; a
    pass 2 that wants them must measure the pump first;
  - ~26 KB of code across `mrui`, `mrout`, `mrsyn`, `mrcb` that no one has
    read for size.
- **PACCMAN**: 22 unused sprites (1,408 B) the port plan calls "slack to
  reclaim first" need a renumbering that did not fit the budget; the licence
  is 1,072 B incbin'd into the resident image that nothing reads.
- **Brief fix**: an agent cannot see elapsed time unless it checks; set 5's
  brief says "check `date` at the start and before deciding you are out of
  time".
