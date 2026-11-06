# ARCADE on kern_mega - the plan

The owner's rule (2026-11-05): every new application runs on kern_mega, and
ARCADE is a new application. kern_mega may be assumed to run on a 286 or
better: below that it is a larger real-mode kernel and nothing more.

Today ARCADE refuses to start there. `OS88_MEGA_REFUSE` is the first line
of `ar_entry`, and `tests/megahigh.txt` and `tests/pins.txt` carry its
debts. This plan retires all three.

The facts below come from an audit of the tree (2026-11-05), with file and
line as of commit `b67ac5c2`. SPEC.md 20.18, KERN-MEGA-PLAN and
KERN-MEGA-MEMORY-PLAN are the rules.

## 1. What kern_mega gives a package, and what ARCADE does against it

On kern_mega in protected mode:

- **Claims are LDT selectors.** A package's region and every claim is a data
  selector (a code half 8 below). With `OS88_HIGH` / `OS88_HIGHMEM` they may
  be above 1MB. A selector is opaque: adding to one names a different LDT
  entry, not a later paragraph.
- **Packages run at ring 0 under the identity map.** Port I/O is legal, as
  are `cli`, `sti` and `hlt`. `int 16h` is reflected. `0040h`, `A000h`,
  `B000h` and `B800h` are identity selectors. FSX's framebuffer
  (`FSI_SEG`) is a selector, and ARCADE only ever loads it into a segment
  register.
- **Two things work but trap.** A store through CS, and a far transfer
  through a data selector, are completed by the kernel (`mega_gpw`,
  `mega_gpfix`). Each costs one fault, about 77 us measured on 86Box.
  That is fine once per command and ruinous in a loop: RealDMG took
  2.1 million a minute, and the Video Player 7,048 a play, before each was
  fixed.
- **Sound needs no change.** ARCADE reaches it only through the API
  (`OSAPI_SND_STREAM`, `SND_FM`, `DRV_CALL SNDV_OPLW`, `SND_TONE`). The
  DMA ring is `OSAPI_MEM_CLAIM_DMA_HI`, which stays conventional.
  `pit_now` reads ports 40h, 43h and 20h and `es = 40h`, all legal.

ARCADE's real-mode assumptions, by the audit:

| kind | where | count |
|---|---|---|
| paragraph SUMS on a claim's segment | `ar_rimg` = ROM claim + 2 (arcade.asm:533); `+1000h` past 64KB (`ar_crcimg` :593, arcqbs.inc:70); `ar_zat`'s offset >> 4 into a 448KB zip (arcimp.inc:330, kept in `ar_cdseg`); chip places `AC_AT + claim + 2` (arcimp.inc:645, 709, 733); the import image cleared by `+800h` steps (:616); `af_slot`'s `index * AF_PICPAR + af_pclaim` (arcface.inc:409) | 9 sites |
| FAR TRANSFERS into loaded memory | `call far [ar_core]`: a board's code part, by its op_seg DATA segment (arcade.asm:274, :1002); the N88 core's NMI frame `retf`s into Q*bert's program in `qb_gseg`, a plain claim (arcn88.asm:55-64, arcqb.inc:364); the game's patched yields `call far` the N88 part by its DATA segment (arcqb.inc:338-356) | 3 paths, every frame |
| STORES THROUGH CS | `mov [cs:n88_view], ax` every run (arcn88.asm:43); Pac-Man's shadow pass, per row (arcpm.inc:1565, 1602, 1607, 1608); its build (`pmb_*`, :351-408); `os88inflate.inc`'s work space `[cs:INF_WS]`, 34 sites, several per BIT decoded | per run, per row, per bit |
| CODE WRITTEN as data | `ar_fill` pokes ROM bytes into the recipe's holes through `op_seg` (arcade.asm:659); Q*bert's program patched and copied into `qb_gseg` (arcqb.inc:131, 339) | legal: through data selectors already |
| claims over 64KB | the `.ROM` image (up to 128KB), the zip (448KB) | 2 |

What is already right: the translated boards (`arcrt.inc`, `arcz80rt.inc`
and the generators) load no segment from a value and do no sums. Their
only segment load is `mov di, ds / mov es, di`, they only READ through CS,
and they run with DS = SS = the view and ES = the aux part. Q*bert's own
program loads no segment register (its loads are NOPped), has no `int`,
`in`, `out`, `cli` or `sti`, and runs with CS = DS = SS = ES = G.

## 2. The approach: protected mode throughout, region and claims high

The alternative is an `FSXF_REAL` bracket: the game loop runs in real
mode, where every assumption above holds, and only the desktop side is
made selector-clean. It is cheaper, but it pins the region and every claim
the loop touches in conventional memory, which is the opposite of
`OS88_HIGH`. **Rejected**, unless M4's measurement finds protected mode too
slow on the 286.

So the arm runs in protected mode with `OS88_HIGH | OS88_HIGHMEM`:

- ARCADE derives every segment in ONE place.
- Code it far-calls has a CODE selector.
- It never stores through CS on a hot path.
- Its two claims over 64KB are walked by window, never by sum.

Most of this is the SAME code on every kernel, a selector being just
another opaque segment. Only what names a kern_mega marker goes under
`%ifdef PKG_MEGA` (SPEC.md 20.18 rule 1), into an arm built by
`make mega`.

## 2.1 The owner's decisions (2026-11-05)

- **Protected mode, region and claims high.** It frees conventional memory
  for larger arcade ports on kern_mega later. The `FSXF_REAL` bracket is not
  taken.
- **A kern_mega twin of ARCADE's floppy** (`arcrelmega*.img`), the arm and
  `ARCADE.TXT`, rather than room on `megaapps.img`. Disk space to be
  confirmed when the arm is built (M4).

## 3. Waves

Each wave lands on `arcade-emu` (or its successor) and is verified on the
host and by looking. The rows run once, after the last wave (CLAUDE.md,
*Testing*).

### M1 - one door for every derived segment (shipped build; retires the pins)

- [x] `ar_sub` (arcade.asm): DX = a claim, AX = a paragraph offset,
      giving DX = a segment there. In the arm it is `OSAPI_SEG SEG_SUB`
      (refused, the claim is kept, never a sum on a selector - SPEC.md
      50.6.11); in the shipped build it is the sum. Every site in section
      1's first row goes through it: `ar_rimg`, the chips (`ar_iplace`),
      `af_slot`, the import image's clear, `qbs_seg`'s sound chips at
      10000h.
- [x] Past 64KB, never `+1000h`: `ar_crcimg` takes the claim and a
      paragraph and asks `ar_sub` for each 64KB. AS BUILT, the zip walk is
      NOT `SEG_AT`: a re-aimed window is not tied to its block, and on
      kern_mega a pool block moves whenever any task's claim packs the pool
      (SPEC.md 50.6.11, 50.3.5). So `ar_zat` takes the 32KB GRANULE an
      offset lies in through `ar_sub` - a SUB that moves with the claim, 14
      at most over a 448KB zip - with BX up to 32KB into it; every chip is
      8KB at most and a central directory is now refused past 32KB.
      `ar_at` (the `SEG_AT` door) is written and has no caller yet.
      MEASURED on Bochs: the arm opens, reads all seven sets READY, and
      plays Space Invaders on kern_mega in protected mode.
- [x] Every claim made MOVABLE (ARCADE-SPEC 14): the carve and parts
      (`OP_ONMOVE ar_partsmv`), the `.ROM`, the pictures, Pac-Man's and
      Q\*bert's built parts and shadows, G, `qbs_seg`. No proc checks for a
      bracket: a move happens only inside a UI-task claim and a bracket
      freezes every other instance (SPEC.md 53.2, 66.3.1), so the owner's
      reading - only while windowed - holds by construction. G's proc puts
      right the CS its stack keeps between runs. The twelve N lines in
      `tests/pins.txt` are gone; the import's two C lines and the ring's D
      line stay.
- **Verified by:** `arcbuilt`, `arcimport`, `arcface*`, `arcopt` and
      `qbglass` unchanged (the final pass), plus `arcmove` (soak, new):
      11 moves, every word followed, each board plays on; red with
      `qb_gmv` a bare `ret`.

### M2 - no stores through CS (shipped build too)

- [x] `os88inflate.inc`: `INF_DSWS`, the includer's option - the work
      space through DS, the stream's segment in AX, loaded at a byte's
      refill and a stored block's copy. ARCADE takes it in both builds.
      `tests/inflate86.py` holds BOTH modes to zlib (102 streams each) and
      puts the DSWS work space in a segment of its own, failing on any
      write into the code's: red with one `cs:` store put back (166,079).
- [x] `arcn88.asm`: the view's segment kept in G at 9FFEh (`N88_GVIEW`),
      read through SS = G at the yield.
- [x] Pac-Man's shadow pass (`pm_put`: the row in AX, the plane's two
      words on the stack) and sprite build (`pmb_sprites`: the code on the
      stack, the flip in CH, the column's bit shifted from DH). `pm_shr`,
      `pm_pp`, `pm_shp` and `pmb_code/flip/bit` are gone.
- **Verified by:** `arcbuilt` (the builds), `inflate86`, `arcspeed`,
      `qbglass`, `arclock*`. Each is byte-identical where it was.

### M3 - code selectors for code (the arm)

- [x] Board code parts called through their CODE half: `OP_WANT_CSEG`
      (the arm's) and `op_cseg` for `ar_core+2`. The recipe is still filled
      through `op_seg`'s data half.
- [x] Q\*bert's G run through its code half (`qb_gcode`:
      `OSAPI_MEM_CODESEL`, or `SEG_SUBC` over a lone pool selector), kept
      in `qb_gcs` and handed to the N88 core in the view (`NV_GCS`, 6086h):
      the first run's frame and the NMI frame push it; DS, SS and ES are
      G's data half.
- [x] The patched yields far-call the N88 part's CODE half (`qb_n88` is
      `ar_core+2`, op_cseg's).
- [x] The class byte: `OS88_HIGH | OS88_HIGHMEM`, in the ONE header line
      (as Pixel and RealDMG): the bits mean nothing on a real-mode kernel
      and the shipped build still refuses on kern_mega. `tests/megahigh.txt`'s
      two ARCADE lines are gone (M4's third box, done here).
- [x] Under `%ifdef PKG_MEGA`: `OP_WANT_CSEG`, `qb_gcode`, `ar_sub`'s and
      `ar_at`'s `OSAPI_SEG` calls, `ar_zat`'s granule. The shipped build
      keeps `OS88_MEGA_REFUSE`, now TRUE: kern_mega's disk carries the arm.
- **Found on the way, defects of EVERY build**, which a selector's limit
      caught and a real-mode segment never checks (ARCADE-SPEC 14, rule 4):
      Q\*bert's power-on wrote 8KB past its sprite part; Pac-Man's reset SP
      (FFFFh) let a push land 64KB above the view; Pac-Man's write list
      ended one word past its aux part (now 51KB, prebuilt refreshed).
- **Verified by:** a trap count. MEASURED (Bochs, `make megatrap`): all
      seven boards played 20 guest seconds each, 0 #GPs from ARCADE
      (ARCADE-SPEC 14's table). A row that does this is M5's `arcmega`.

### M4 - the arm built, shipped on kern_mega's disks, gated

- [ ] `$(MEGAAPPDIR)/arcade.bin` and `.o88`: the shipped rule with
      `-DPKG_MEGA`, in `$(MEGAPKGS)`. kern_mega's apps disks carry it:
      `megaapps.img` takes it through `MEGASUB`, or an `arcrel` twin for
      kern_mega.
- [ ] `tests/unit/t_megapkg.py` sees ARCADE. Today its `recipes()` skips
      the rule, because the first prerequisite is `$(ARCADE_SRC)` and not
      `apps/...asm`. Fix the rule's order (or the gate), so check 3 (a
      PKG_MEGA user has an arm) applies.
- [ ] `tests/megahigh.txt`'s two ARCADE lines removed, since the arm
      declares both bits. The gate turns one way.
- [x] MEASURED on the 286 (86Box `vm/at286-port`, the mr286) and the 386
      (Bochs): ARCADE-SPEC 14's tables. Against kern_big on the mr286,
      protected mode costs nothing measurable. Q\*bert's live sound stays
      gated to a 386 (`CPU_386`), as on kern_big.

### M5 - verification rows

- [ ] `arcmega` (Bochs, VGA, `--sb`): ARCADE from kern_mega's apps disk on
      a ROM disk. Open, import a zip, play each of the seven games for N
      frames (counted), P and F1, a picture written, ARCADE.DAT
      read back. Faults counted: zero in play.
- [ ] `arcmega286` (86Box `286-mega`): the same at the 286 floor, frame
      rates reported.
- [ ] The shipped build on kern_mega still refuses, saying so.
- [ ] Break each on purpose: a CS store left in `n88_view` (traps
      counted), a sum left in `af_slot` (a wrong picture or a fault).

## 4. Costs and risks

- **Bytes.** M1 and M2 are shipped code: a few dozen bytes for `ar_seg`,
      the procs and the window walks. M3's marker code is the arm's alone.
      Each will be measured, never estimated against a rung.
- **The import's speed.** A window re-aim per zip read (`SEG_AT`) is an API
      call. `ar_zat` is called per entry and per chip, not per byte, so
      this is cheap. M1 measures it on `arcimport`.
- **Q\*bert's program** runs in protected mode with its segment registers
      never reloaded, so no trap. Its NMI and yield are the only far
      transfers, once a frame each.
- **Translated boards** in protected mode: DS = SS = the view as a
      writable data selector, the dispatch read through CS. No traps, by
      the audit. M4's measurement confirms it.
- **Open:** whether kern_mega's apps disk has room for the arm beside
      what it carries (189KB). If not, a kern_mega twin of `arcrel`.

## 5. Not in this plan

- Q*bert's sound on a 286: still needs the faster sound path
  (ARCADE-RELEASE-PLAN, R5's open items).
- Running ARCADE's region above 1MB on kern_big's XMS: kern_big has no such
  mode.
