# `kern_small` — what a 128KB machine can stop carrying

> **The tree moved under this document, twice.** `elendilon` merged kernel
> size pass 3 and APP_SMALL, taking `kern_small` from `KERN_SIZE` **96,256 to
> 94,720** and the free heap from 32.5 KB to **34.0**; then **§4's C3 was
> BUILT** — associations are gated out of `kern_small` (SPEC.md §54.0), taking
> it to **92,160 and 36.5 KB**, and returning the 3,072-byte pinned claim
> docs/KERN-SMALL-MODULE-SPLIT.md §9.1 found, so a bare desktop went from
> ~31.0 KB usable to 36.5. **The per-feature rows below are pre-merge
> measurements** and are within ~1.5% of the merged tree; the tier table in
> §8.1 is the one to re-derive before anything is decided off it.
> docs/KERN-SMALL-MODULE-SPLIT.md §9.2 carries the current arithmetic.

**Research document, not a contract.** SPEC.md is the binding contract for what
the kernel *is*; this is the study of what `kern_small` could stop being, and
the arithmetic that says how far each answer gets. Nothing here has been built.
Every figure was measured on this tree at build 376 by the method in §8, and
the ones that are derived rather than measured say so.

The ask, in the requester's words:

> `kern_small` needs to run on a system with 128KB of RAM. It does, now, but
> not with enough free heap to run almost any program. I'd like there to be
> 70KB of free heap. That means `kern_small` booted in 58KB. Research what we
> can gate. What would a 128KB system not likely have hardware wise? … For
> `kern_small` everything is on the table.

---

## 0. The verdict, up front

**The whole list, taken, lands at ~66KB of free heap and not 70KB — and it
costs the ability to write a file.** The gap is not in any one place: the
kernel is dense and evenly spread, and there is no fat symbol to delete.

```
today      KERN_SIZE 96,256   heap floor 95.5 KB   free heap on 128KB = 32.5 KB
asked for  KERN_SIZE 57,856   heap floor 58.0 KB   free heap on 128KB = 70.0 KB
                              ------------------------------------------------
the cut    38,400 bytes = 39.9% of the footprint
```

Five findings:

1. **There is no big single win.** The largest symbol in the kernel is
   `osapi_table` at 1,256 bytes and the second is `kmain` at 217. Below that it
   is a long tail of 40–200 byte procedures. The cut has to come from removing
   whole *features*, and §2–§5 is that list, priced.

2. **The best lever keeps the features rather than deleting them.** `.cold` is
   34,531 bytes — 36% of the footprint — and `mod.inc` (§2.8) already moves
   10,536 bytes of it out of RAM entirely by shipping it as on-demand modules.
   Two more `.cold` residents can follow it, for **4,649 bytes net** (§6).
   **That figure was 12,997 when this document was written, and
   docs/KERN-SMALL-MODULE-SPLIT.md is the correction**: two of the four
   candidates are refused by the mechanism itself — `assoc.inc` sits inside
   `mod_need`'s own dependency cone and `diskw.inc` is the file I/O layer
   three loaders call — and `fdlg.inc`'s row includes 1,140 bytes of shared
   code that stays either way. Moving is no longer a substitute for deleting;
   §6 is rewritten and §6.1's product decision still stands.

3. **The hardware question the requester asked yields less than it looks.**
   Sound, hard disk, Ethernet, XMS, PS/2 mouse and the VGA are worth about
   **4,700 bytes** together — and most of that is not the hardware at all, it
   is `driver.inc`'s loadable-driver machinery (2,550). Every card is already
   a `.DRV` costing zero resident bytes, and the VGA, the planar decoder, the
   PS/2 mouse and XMS were gated out of `kern_small` some time ago. §2.

4. **A hard floor nobody has had to notice: the boot overlay lands on the FAT
   window.** `.ovlw` is 4,700 bytes and spills through the mount buffers above
   it, so **the FAT window and the mount buffers may shed 2,816 bytes between
   them and not one more** — which caps three of the most attractive data cuts
   at half what they would otherwise give. §7.

5. ~~**32.5KB is the number that makes the request reasonable.**~~ **THIS
   FINDING IS REFUTED, AND IT WAS THE JUSTIFICATION FOR THE WHOLE ASK — see
   §0.1.** It reasoned that `SHEET.O88` is 48,352 bytes, that 66KB would run
   it with 14KB left over, and that 70KB is therefore "roughly where a second
   program becomes possible". **A package's image is not what it needs to
   run.** SHEET makes nearly **100KB of heap claims when it opens** — more RAM
   than the machine has in total, before its region is counted at all — so no
   amount of kernel cutting was ever going to load it. The number the ask was
   anchored to was measuring the wrong thing.

---

## 0.1 A PROGRAM'S SIZE IS ITS REGION *PLUS* THE CLAIMS IT MAKES TO RUN

This document sized programs by their `.o88`, which is the number `ls` gives
and the number a disk catalogue prints. It is not the number that decides
whether a program runs.

**SHEET makes almost 100KB of heap claims on open** — its grid, its cell
store, its undo — which is more RAM than a 128KB machine has in total, before
its 48,352-byte region is counted at all. So **SHEET will never run on
`kern_small` in its current form**, at 70KB of free heap or at any other
figure this document could reach, and the parallel apps session has taken it
off the small disks for that reason. It joins §24.5's list on a new ground:
not "it cannot reach its driver" but "it cannot fund itself".

Two things follow, and the second is the one to carry forward.

**The 70KB target is retired.** It was reverse-engineered from "the heap at
which SHEET loads", and that premise is gone. The other program behind it was
PAINT, and **that one has been solved at the APPLICATION layer instead** —
SPEC.md 42.23's 1bpp canvas and SPEC.md 42.6.5's claim-first sizing, in the
parallel session, not here. The kernel was being asked to make room that the
program was better placed to stop needing.

**The goal is now: as much more as we can get, weighing each feature's cost
against what cutting it does to the machine.** Not a number to hit. That is a
weaker brief and a more honest one — every row left in §2–§5 is a feature, and
a row is worth taking only when the bytes are worth what the user loses. The
rows do not become more attractive because a target is close.

### 0.1.1 …and a claim is INVISIBLE to every measurement in this document

`kernsize` reports sections. A heap claim is not a section — it appears in no
column of any table here, and no `%if` in `kernel.asm` can see it. Every
figure in §1–§8 is therefore a *footprint* figure, and the machine's actual
free memory is that minus whatever the running software claims.

This has now cost twice in opposite directions:

- **It hid a defect.** `ASC_KB` held 3,072 bytes of a bare desktop before the
  user had done anything (docs/KERN-SMALL-MODULE-SPLIT.md §9.1), found by
  accident. `tests/small128.py` exists because of it and walks `mem_tab` on
  the machine.
- **It hid a WIN.** The per-window view cache was 3KB an open Disk window, 2KB
  of which was one icon body per entry — duplicated again in every window's
  private copy. Pooling it (SPEC.md 25.8.5) cost `.cold` +34 and gave back
  **1,024 bytes per open window, 4,096 with all four up**. It appears nowhere
  in this document's tables, because a claim never does.

**So the next lever is per-instance claims, and nothing here has counted
them.** `VIEW_KB`, the directory read-ahead, each package's own claims: a
walk of what a *working* machine holds, rather than what the kernel assembles
to, is a measurement this document has never taken.

---

## 1. The arithmetic, and where the bytes are now

`kern_small`'s footprint is one contiguous span from `KERNEL_SEG` (linear
0x00600) to `KERN_END`, and it is four rungs, each rounded up to 512 bytes:

| rung | holds | measured | rung |
|---|---|---:|---:|
| image | `.text` 40,614 + `.bss` 5,512 | 46,126 | **46,592** |
| cold | `.cold` | 34,531 | **34,816** |
| FAT | `DSK_FAT_SECS` = 9 sectors | 4,608 | **4,608** |
| low | `.lowbss` 8,712 + `STK0_SIZE` 1,024 | 9,736 | **10,240** |
| vgabuf | the planar decoder's buffers — **already zero on small** | 0 | **0** |
| | | | **96,256** |

The heap starts where the kernel actually ends, and runs to whatever `int 12h`
reports:

```
free heap = int 12h  -  (KERNEL_SEG*16 + KERN_SIZE)
          = 131,072  -  (1,536 + 96,256)  =  33,280 bytes  =  32.5 KB
```

For 70KB free the span must end at 59,392, so `KERN_SIZE` must be **57,856**
and the cut is **38,400 bytes**.

**Two things that are not levers.** The rungs currently waste 1,255 bytes in
rounding (image 466, cold 285, low 504) — that is noise, not headroom, and
CLAUDE.md's rung rule refuses it as an argument in either direction. And
`KERN_SMALL_BUDGET` has 11,264 bytes spare: that is the *guard*, not the
machine. Lowering the guard saves nothing; only lowering `KERN_SIZE` moves the
heap.

### 1.1 What the theme table says about where to look

| theme | bytes | share |
|---|---:|---:|
| the file system, end to end | 29,581 | 39.4% |
| the window system and its furniture | 21,945 | 29.2% |
| drawing: adapters, primitives, glyphs, icons | 7,619 | 10.1% |
| hardware: drivers, clock, mouse, sound, CPU, XMS | 7,179 | 9.6% |
| the kernel proper: API table, heap, scheduler, events | 6,937 | 9.2% |
| the three built-in kinds | 1,392 | 1.9% |
| the Control Panel | 492 | 0.7% |

**The file system and the window system are 69% of the code.** The requester's
own list — sound, hard disk, Ethernet, display niceties — is drawn almost
entirely from the two themes that are 20% of it together. That is the finding
that shapes everything below: a 40% cut cannot be taken out of the hardware
column, because the hardware column is not 40% of anything.

---

## 2. Hardware a 128KB machine has not got

The requester's first question, answered: **most of it is already gone, and
what is left is the machinery rather than the devices.**

Already gated out of `kern_small`, with nothing further to win: the **VGA**
(`GFX_VGA`, an adapter rather than a feature — a VGA card in a `kern_small`
machine runs as a CGA at 640x200), the **planar row decoder** (§5.4.1.3), the
**whole-column store** (§39.25), the **PS/2 mouse** (§9.9, ~690 bytes),
**memory above 1MB** (§41.4, `xmem.inc` is down to 21 bytes), the **theme**
(§76), the **zoom animation** (§11.99), the **scrollbar thumb drag** (§13.10.5),
the **band composer** (§5.9) and **SAVER.DRV** (§64). Every sound card, disk
controller and NIC is a `.DRV` and already costs zero resident bytes.

What is actually still on the table:

| # | option | `.text` | `.cold` | `.bss` | total | what it costs |
|---|---|---:|---:|---:|---:|---|
| A1 | **Sound layer** §34 (`snd.inc`) | 1,035 | — | 287 | **1,322** | no PC-speaker tone or PCM at all. 256 of the `.bss` is `snd_xlat` |
| A2 | **Clock ladder** §37 rungs 1–3 (`clock.inc`) | ~450 | — | ~60 | **~510** | a 5150 has no RTC — MC146818 arrived with the AT — so rungs 1–3 are for machines this build is not for. Keep rung 0, the BIOS tick. **BUILT, all four rungs, and worth far more than this row — §2.3** |
| A3 | **Loadable drivers + `SYSTEM.CFG`** §51 (`driver.inc`) | 453 | 1,794 | 303 | **2,550** | no `.DRV` of any kind can be loaded. `mod.inc`'s modules (Control Panel, Format, Clone) are a different mechanism and survive |
| A4 | **Volume table 8 → 4** (`DVOL_MAX`) | 64 | — | 256 | **320** | four mounted volumes instead of eight; `dsk_bpbv` is 512 bytes of `.bss` |
| | **subtotal** | | | | **~4,700** | |

### 2.1 A3 and A4 ARE BUILT, A2 IS REFUSED, A1 IS DEFERRED

**A3 is SPEC.md §51.0 and A4 is in `disk.inc`.** Measured tree-to-tree across
both:

```
             before    after    delta
.text        39,731   39,272     -459
.bss          5,417    4,848     -569
.cold        27,215   25,602   -1,613     } -2,641 IN THE LADDER
.ovl          1,226      423     -803     } -991 boot overlay
.ovlw         4,516    4,328     -188     }
                                -------
                                 -3,632   ->  HEAP -2,560 = 2.5 KB

KERN_SIZE   88,064 -> 85,504          heap floor 87.5 KB -> 85.0 KB
free heap on a 128KB machine, MEASURED on one:   40.5 KB -> 43.0 KB
kern_big                                          byte-identical
```

### 2.2 The finding this row exists for: SECTIONS ARE NOT HEAP

**3,632 bytes of sections bought 2,560 bytes of heap, and the difference is
not rounding.** 991 of them are `.ovl`/`.ovlw` — boot-overlay code loaded into
memory the machine reuses once it is up — so they move `KERN_SIZE` and the
boot-time minimum and move `HEAP_SEG` by *nothing*. Only 81 bytes went to the
512-byte rung rounding.

**That makes this document's whole method optimistic**, and it is worth saying
plainly before anything else here is decided off it. §2–§5 price features by
adding up `.text`, `.cold` and `.bss` off a symbol map. That is the right
answer for **footprint**, and it is the wrong answer for **free heap** whenever
any of the bytes are in an overlay: the ladder is `.text+.bss`, then `.cold`,
and nothing else in the sum reaches it. A row should be read as an upper
bound on heap, not an estimate of it.

The resident half of the estimate was **good**: A3's `.text`+`.bss`+`.cold`
was priced at 2,550 and came in at **2,277**, 12% *high*. What the row missed
was the 991 bytes of overlay — and those are exactly the bytes that buy
nothing.

**And there is a reporting trap behind it worth not repeating.** `kernsize`'s
`sum` is a delta against the **blessed baseline**, not against the tree you
started from. The baseline had not been blessed since before W1, so a reading
taken after A3 reported `-6,529` — W1 and W2 included — and it reads exactly
like an increment. Bless after a wave lands, and measure a change
tree-to-tree when the number is going in a document.

**A2 is refused, on a measurement and on a judgement.**

The measurement first, because it settles the row on its own. `clock.inc`
already has the mechanism this row describes — `CLK_FORCE`, the `RTC=` knob —
and forcing a single rung is worth **44 to 51 bytes**, not ~510:

```
full ladder      .ovlw 4,328        (-DCLK_FORCE=n, kern_small, .text/.bss identical in all five)
rung 1 only      .ovlw 4,277   -51
rung 2 only      .ovlw 4,277   -51
rung 3 only      .ovlw 4,284   -44
rung 4 only      .ovlw 4,279   -49
```

`CLK_TRY` appears five times and all five are inside `clk_probe`: it gates the
**probes**, in the boot overlay. The per-rung **read and write** bodies are in
`.text` and are selected at run time off `[clk_tier]`, so they are not gated
by anything and this row's `~450 .text` has no mechanism behind it. Getting
that 450 would mean building a second gate over ~20 bodies.

And it should not be built, because the row picks the wrong rungs. **Rungs 2
and 3 are XT clock cards** — a National MM58167 or a Ricoh RP5C01 on a card at
2C0h — which is precisely the add-on the machine this build exists for would
have. Rung 1 is the MC146818, and *that* is the AT-only part. So the row's own
reasoning ("a 5150 has no RTC") argues for dropping rung 1 and keeping 2 and
3, which is the opposite of what it proposes and is worth ~51 bytes.

**A1 is deferred at the owner's instruction** — *"keep pc speaker for this
round - we may cut it later, but for now."* Worth recording for whoever picks
it up: A3 has already made **part of it dead**. The FM and Sound Blaster tiers
are reached through `SOUND.DRV`, so with no driver loadable the `SND_RT_FM`
and `SND_RT_SB` routes, `snd_str_busy` and the stream API can never be
selected on `kern_small`. What the speaker actually needs is the tone path and
`snd_xlat`'s 256 bytes of PCM rescale — so A1 splits, and the half that is
already unreachable is the cheaper half to take.

**A3 is the one to think hardest about.** It is the largest item here and it is
not a device — it is the ability to load one. A `kern_small` machine with no
drivers cannot gain a RAM disk, a hard disk, a screen saver or a network later,
and `SYSTEM.CFG` stops being read. Against that: on a 128KB machine there is no
heap to host a driver in anyway, which is close to an argument that the
mechanism is already unusable there rather than merely unused.

---

## 3. Display niceties

The requester's second question. **The distinction that matters is between a
*nicety* and *the optimised path*, and two of these are the second thing.**

| # | option | bytes | what it costs |
|---|---|---:|---|
| B1 | **Raise cache / save-under** §11.96 (`wm_su*`) | **2,433** | raising a covered window goes from ~10 ms back to the **1,026 ms** §11.96 was written to fix. The memory is a purgeable claim, so the saving is code only |
| B2 | **Drag cache** §11.96.12 (`wm_dc*`, `wm_cov*`) | **415** | a window drag repaints what it uncovers |
| B3 | **Icon renderer + harvested icons** §10 (`icons.inc`, `ico_stage`, `disk_icons`, `dsk_ico`) | **3,173** | 2,048 of it is `disk_icons` in `.lowbss` — see §7, it is capped. Files get generic glyphs |
| B4 | **`gfx_line` family** (`gfx_line`/`gfx_ls`/`gfx_lstep`) | **1,012** | an API slot, so it becomes a refusing stub. docs/GFX-FSX-PLAN.md notes three apps already carry their own Bresenham |
| B5 | **Toast** §59 (`toast.inc`) | **488** | SPEC.md §47 rule 3 wants every refusal to say something the user can act on, and §59 is where three of them say it |
| B6 | **Progress widget** §12.8 (`fprog.inc`) | **661** | long file operations go silent |
| B7 | **Screen blanker** §64 (`blank.inc`) | **158** | |
| | **subtotal** | **~8,340** | |

### 3.1 Three things that look like niceties and are not — do not cut these

- **Damage rects and the clip region** §11.90/§11.91 (`wm_dmg*` 868 +
  `wm_clip*` 862 = **1,730**). This is not an optimisation layered over the
  redraw path, it *is* the redraw path — PERFORMANCE.md part 5 makes a change
  that reintroduces a full repaint a regression against a documented number
  rather than a neutral trade. Cutting it makes every window operation cost
  the whole screen on the slowest machine that runs this build.
- **`gfx_pairtab0`/`gfx_pairtab1`** (512 bytes of `.lowbss`) and
  **`vid_rowtab`** (256). These *are* the optimised path the requester asked
  to keep. docs/MONO-RECLAIM-PLAN.md measured the row table paying for itself
  three times over on CGA (saver 4.69% → 2.85% of the whole machine).

  **AND THEY CANNOT BE COMBINED OR OVERLAID — asked and answered, so that
  nobody derives it a third time.** Both are *build-once permanent lookup
  tables*, not scratch caches with disjoint lifetimes, and they are read **on
  the same call, five instructions apart**: `gfx_blit4`'s row loop calls
  `gfx_rowbase` (which reads `vid_rowtab`, and is *"THE FIXED COST OF EVERY
  DRAWING CALL IN THE MACHINE"*) at `vga12.inc:2750`, then picks a pair table
  on `(x+y)&1` at 2757. Different domains — row index → byte offset against
  source byte → reduced byte — different value widths, and neither is ever
  dead while the machine is drawing. `vid_rowtab` is already cut per arm: 696
  bytes on `kern_big` (348 rows), 256 here (128).

  The two pair tables *are* related — `tab1[B]` is exactly
  `swapbits(tab0[swapnibbles(B)])` — but both operations land in
  `sw_blit_row`'s inner loop, which PERFORMANCE.md Set 102 measures at **27.7%
  of the whole machine in Paint**. The second table exists precisely so "the
  dither then costs the loop nothing"; two instructions a pixel-pair to save
  256 bytes is the wrong direction.

  Two facts that look like openings and are not. **They are lazily built and
  usually are not**: `gfx_pairbuilt` reads 0 on a desktop with a Disk window
  full of icons open, because icons go through `icon_draw`'s sprite engine and
  not `gfx_blit4` — but they are `resb`, so the bytes are spent either way.
  And **Paint going 1bpp does not free them**: `OSAPI_GFX_BLIT4` is a
  published slot, and **CHART is on the small apps disk** (walked, `APPS/`)
  and blits its canvas through it. The only way to reclaim the 512 is to
  delete the row decoder and leave the per-run path, which is `NOPLANE=1` —
  PERFORMANCE.md Set 107 prices that at **7,146 ms against 1,148** for
  `OS8088.GIF`, because a dithered image becomes one run per pixel.
- **`softgfx.inc`** (1,180). On `kern_small` this is the *only* renderer —
  the VGA path is already gone — so there is no second path left to collapse
  into. The "keep only an optimised path" work the requester is thinking of
  was done when `GFX_VGA` was gated.

### 3.2 And a standing objection to B1 worth putting on the record

`KERN_SMALL_BUDGET`'s twenty-first move raised this build's budget *for* the
window redraw optimisations, with the reasoning attached: **"a redraw
optimisation is worth most on the slowest machine, so this is not a figure
that work may be kept out of."** B1 is that decision run backwards. It is
re-decidable — a machine that cannot start a second program has a worse
problem than a slow raise — but it should be re-decided explicitly rather
than swept up with the blanker.

---

## 4. Features — product decisions rather than build ones

| # | option | bytes | what it costs |
|---|---|---:|---|
| C1 | **FAT write path** §18.4–18.6 (`diskw.inc`) | **4,899** | a **read-only OS**: nothing saves, formats, renames or deletes |
| C2 | **Standard File dialog** §38 (`fdlg.inc`) | **3,514** | no application can Open or Save. **Not 4,654**: ~1,140 of `fdlg.inc`'s `.cold` is `apps/os88ui.inc`, which five other files need and which survives deletion (docs/KERN-SMALL-MODULE-SPLIT.md §0) |
| C3 | **File associations** §54 (`assoc.inc`) | **2,526** + **3,072 of heap** | double-clicking a document no longer finds its program, and files get the generic icon. **DECIDED — gated.** The second figure is `asc_use_x`'s `ASC_KB` claim, a kernel tag rather than a purgeable class, taken at the boot mount and never freed: docs/KERN-SMALL-MODULE-SPLIT.md §9.1 |
| C4 | **Cut/Copy/Paste** §22.3–22.5 (`filecp.inc`) | **2,281** | no file management in the Disk window |
| C5 | **Built-in kinds** §14 (`apps.inc` + pools) | **1,540** | Timer, About, Ball, Bounce |
| C6 | **Fullscreen exclusive** §53 (`fsx.inc` + `fsx_mtab`) | **913** | no game or demo can take the screen |
| C7 | **The dock** §30 (`dock.inc`) | **814** | |
| C8 | **Clipboard** §55 (`clip.inc`) | **212** | |
| | **subtotal** | **~16,700** | |

**C1–C4 are 13,220 of the 16,700 — and §6 no longer argues that all four can
be moved instead.** Two of them cannot be.

### 4.1 Trimming the Disk window rather than deleting it

`files.inc` is 8,694 bytes and is how a program gets launched, so it cannot go.
It can be thinned: inline rename (`fm_edit` 314 + `fm_editkey` 137),
drag-and-drop (`fm_drag`/`fm_dgdrop` 116), clone (132), paste (63) and the
more-files marker (126) are **~890 bytes** of clearly separable behaviour, and
a harder pass over the scroll and view caches (`fmv_*` 901, `fm_pool` 96) could
plausibly find as much again. Call it **~1,800 bytes**, at the cost of a Disk
window that lists and launches and does nothing else.

---

## 5. Sizing constants — data, with no feature lost

| # | option | bytes | note |
|---|---|---:|---|
| D1 | **Task partition 13 slots → 6** (`SCH_PARTITION`, `MAX_TASKS`) | **~1,590** | `sch_stacks` is 2,816 of `.lowbss`. Thirteen slices is a 640KB machine's number; 70KB of heap holds about three packages |
| D2 | **FAT window 9 → 3 sectors** (`DSK_FAT_SECS`) | **3,072** | refuses any volume above 720KB. **Capped — §7** |
| D3 | **`disk_dir` 32 → 16 entries** (`DSK_NENT`) | **384** | sixteen files listed per floppy. **Capped — §7** |
| D4 | **`STK0_SIZE` 1,024 → 512** | **512** | **BUILT, ON BOTH KERNELS.** `tests/stk0water.py` is the fill probe SPEC.md 15.1 asks for, automated, and it re-reads **238** against that section's 246. Two things this row said were wrong: task 0's canary is NOT skipped (SPEC.md 8.7 put slot 0 in `sch_stkbase` and `sch_switch` checks it every switch), and the margin standard is not 4x - docs/STACK-SLOTS-PLAN.md 12 accepts **1.26x** for Frotz, because SPEC.md 9.10 and 8.5 moved both mouse ISRs and the ROM tick chain onto private stacks and a slice's depth is now the program's own chain. 512 is 2.08x |
| D5 | **`MAX_WIN` 12 → 6** | **~264** | **mirrored in `apps/os88api.inc`** — an ABI change, gated by `tests/unit/t_mirror.py` |
| D6 | **`INST_MAX` 12 → 6** | **~270** | same mirror, same gate |
| D7 | **`MEM_MAX` 32 → 20** | **120** | twenty heap claims |
| | **subtotal** | **~6,210** | of which D2 and D3 are capped |

D5 and D6 are worth the least and cost the most process: they are published to
packages, so moving them means every `.o88` is built against a different bound
and the "one `.o88` serves both kernels" property is at risk. **Take them last,
or not at all.**

---

## 6. The lever that keeps the features: more on-demand modules

`mod.inc` (SPEC.md §2.8) is *"`.cold` with the address changed, and nothing
else"* — a module is assembled as part of this kernel, cut out of the binary by
`tools/os88mod.py`, read into a heap claim when the feature is asked for and
freed when it is done. It already carries **CTRL.DRV 5,866, FORMAT.DRV 1,131
and CLONE.DRV 3,539 — 10,536 bytes that are not in the footprint.**

**Which of them can follow is docs/KERN-SMALL-MODULE-SPLIT.md**, and the
answer is two of four:

| module | `.cold` | entries | verdict |
|---|---:|---:|---|
| `filecp.inc` — Cut/Copy/Paste | 2,141 | **5** | **possible**, and clean |
| `fdlg.inc` — Standard File dialog | 3,152 | **9** | **possible**, after lifting `os88ui.inc` out and raising `MOD_NENT` to 16 |
| `assoc.inc` — file associations | 2,003 | 9 | **refused**: `mod_need → drv_mounted → dsk_chdir_q_x → dsk_chdir_x → disk_mount_x → asc_lookup_x`. Loading any module can mount, and a mount calls associations |
| `diskw.inc` — the FAT write path | 4,565 | **33** | **refused**: it is the by-name file I/O layer. `mod.inc` calls `dskw_read_x` *to load a module*; `driver.inc` and `loader.inc` call it to load a driver and a package; CTRL.DRV and CLONE.DRV far-call `dwf_dskw_*` from inside their own images |

Net of the resident stubs, the 43 new far entries and `mod_fp`: **4,649
bytes**, taking `KERN_SIZE` 96,256 → 91,136 as the rungs fall today.

**This section read "12,997 more by the same route … within 1,400 bytes of
what deleting them outright would give", and that was wrong.** It was the four
files' `.cold` added up, with no check on entry counts, on the module loader's
own dependency cone, or on what `%include` sits inside `fdlg.inc`. The honest
gap between moving and deleting is **8.4 KB**, because the two largest
candidates can be deleted and cannot be moved.

**It is still worth doing** — 5.0 KB of heap, +15% on a 128KB machine, with
both features intact — but it is not a substitute for the deletions, and the
last row of §8.1 is the only one that reaches 65 KB.

### 6.1 The rule it runs into

docs/ONDEMAND-PLAN.md §1 states the test and it is a good one:

> A feature may be loaded on demand only if the **system disk is already
> required** to do it, or can be required **without interrupting what the user
> was doing** — because on a one-floppy machine every load is a disk swap.

and it runs the test explicitly against two of the four:

> | Standard File dialog | **No, and the requirement is perverse** | fails |
> | Cut/Copy/Paste | **No** | fails |

That verdict is correct for the machine it was written against and it is not to
be waved away: `mod_need` calls `drv_vol_bank` → `drv_mounted`, so on a
one-drive machine with a data disk in A: the dialog refuses. Two things make
`kern_small` the one build where it is worth putting back to the requester:

1. **The alternative on the table is deletion, not the status quo.** §4's C2
   removes the file dialog from this build entirely. A dialog that sometimes
   will not open is worse than one that always does, and better than one that
   does not exist — a comparison that was not available when the rule was
   written.
2. **The rule's constraint is the swap, not the read.** A machine that leaves
   the system disk in A: — which is what a machine with 32.5KB of heap does
   anyway — pays a read.

**This is a decision for whoever owns the product, not a change to make
quietly.** A third argument stood here and is **withdrawn**: that the write
path was never tested against the rule and might pass on its own terms.
`diskw.inc` is refused by the mechanism before the rule is reached.

### 6.2 Two structural options that are worse than they look

- **Break ABI parity for `kern_small`.** `osapi_table` is 1,256 bytes (157
  slots at 8 apiece) and the refusing stubs for big-only slots are 8 bytes
  each. Collapsing them saves a few hundred bytes and costs the property
  docs/KERN-SPLIT-PLAN.md §0 calls the one everything else depends on: **one
  `.o88` serves both kernels.** Bad trade.
- **A single-adapter `kern_small`** (one binary for CGA, one for Hercules).
  `viddet.inc` 918 + `vid_rowtab` 256 + `vidsel.inc` 281 ≈ **1,455**, and it
  doubles the shipped small images and the test matrix. Marginal.

---

## 7. The floor nobody has had to notice: `.ovlw` sits on the FAT window

The boot overlay's window half (`.ovlw`, SPEC.md §2.5.3) is loaded onto
`FAT_SEG` and spills through the mount-owned buffers immediately above it —
one contiguous region that is dead until the first mount. `kernel.asm` guards
it:

```nasm
%if ((OVLW_SIZE + 511) / 512) * 512 > FAT_PARA * 16 + DSK_WIN_BYTES
%error "the boot overlay's window half has outgrown the FAT window plus the mount buffers"
%endif
```

On this tree:

```
.ovlw            4,700  ->  5,120  rounded up to whole sectors
FAT window       4,608
disk_dir           768
disk_icons       2,048
dsk_secbuf         512
region           7,936
                 -----
shrink available 2,816   before the overlay has nowhere to land
```

**So D2 (3,072) + D3 (384) + B3's `disk_icons` (2,048) want 5,504 bytes
between them and may only have 2,816.** Three of the most attractive
data-only cuts in this document are, together, worth half what their rows say.

**It is not a permanent floor.** `.ovlw` carries the boot halves of the very
features §3 and §4 propose gating, so a build that takes those cuts has a
smaller overlay and a lower floor. The right order is therefore **features
first, buffers second, and re-measure `OVLW_SIZE` in between** — sizing the
FAT window against today's overlay would leave bytes on the table.

---

## 8. The whole list, added up

Taking §2 through §5 in full, with §7's cap applied once:

```
A  hardware                        4,700
B  display niceties                8,340
C  features                       16,700
D  sizing constants                6,210
                                  ------
   raw                            35,950
   less the .ovlw cap (§7)        -2,688
                                  ------
   cut                            33,262

KERN_SIZE   96,256 - 33,262  =  62,994
heap floor   1,536 + 62,994  =  64,530  =  63.0 KB
free heap  131,072 - 64,530  =  66,542  =  65.0 KB
```

**~65KB of footprint-derived heap, against 70KB asked for** — and **~68KB
usable** once C3's pinned 3,072-byte association cache is counted
(docs/KERN-SMALL-MODULE-SPLIT.md §9.1), which is within 2KB of the ask — and that is with no file writing, no file
dialog, no copy/paste, no associations, no icons, no sound, no clock beyond the
BIOS tick, no loadable drivers, no dock, no fullscreen, no built-in apps and no
raise cache.

Substituting §6's modules for C1–C4 does **not** land near the same number:
two of the four cannot be moved at all, so the two routes stopped being
alternatives. §8.1 gives both.

### 8.1 What each tier buys, for choosing a stopping point

**The cap in §7 applies to each combination separately**, because it bites only
on the rows that take B3, D2 and D3 together — so a tier is not the sum of the
tiers above it:

> **The first two rows of this table are HISTORY now.** W0-W2 of
> docs/KERN-SMALL-MODULE-SPLIT.md and group A's A3+A4 are all built, and the
> machine measures **43.0 KB free on a machine with 128KB in it**
> (`tests/small128.py`, `KERN_SIZE` 85,504). Read the rows below as *what is
> still on the table*, and §2.1 for what the A row actually cost.

| take | cut | free heap | what still works |
|---|---:|---:|---|
| today | — | **32.5 KB** | one mid-size program (SHEET cannot load at ANY row: §0.1) |
| A | 4,700 | **37.1 KB** | everything, minus sound and loadable drivers |
| A + D | 10,270 | **42.5 KB** | as above, with smaller tables and a 720KB volume cap |
| A + B + D | 16,562 | **48.7 KB** | …and no save-under, icons or `gfx_line` |
| A + B + D + §6 | 21,211 | **53.2 KB** | …and the file dialog and Cut/Copy/Paste intact, loaded on demand |
| A + B + D + §6 + C5–C8 | 24,690 | **56.6 KB** | …and no dock, fullscreen, clipboard or built-in apps |
| everything, C1–C4 deleted (§8) | 33,262 | **65.0 KB** (68.0 usable) | a read-only browser with windows |

**The last row is now the only one that reaches 65 KB, and the gap to the row
above it is the correction.** §6's module route keeps the file dialog and
Cut/Copy/Paste for 4,649 bytes, but `diskw.inc` and `assoc.inc` can only be
deleted — so the two middle rows keep file *writing* and associations and pay
8.4 KB of heap for them. That is the trade to put to the owner, and it is a
different one from the trade this table gave before
docs/KERN-SMALL-MODULE-SPLIT.md was written.

### 2.3 A2 was refused and then TAKEN, for a completely different reason

§2.1 refused it on a measurement that stands: `CLK_FORCE` already exists, and
forcing one rung is worth **44–51 bytes**, not ~510. That measurement was of
the wrong thing.

**The owner settled the hardware question, and it settles all four rungs:**

> *"if they have a sixpakplus then they have more than 128kb ram. The
> sixpakplus is a ram expansion card. And the first thing that had the toshiba
> clock shipped with 256kb ram so its not really valid either."*

That is right, and it is stronger than this document's reasoning was. Rung 1
is AT-only; rung 4 is `int 1Ah AH=02h`, which §37.90's own opening says an XT
BIOS does not implement; and rungs 2 and 3 — the add-on cards that looked like
*exactly* the XT upgrade path — are on boards that came with the RAM that
takes the machine off this build's floor. **No rung is reachable on a 128KB
machine**, so the ladder is not unlikely there, it is dead code. SPEC.md
§37.0.1 is the contract.

**And what it is worth is not its own bytes.** `.ovlw` went **4,328 → 2,789**,
and `.ovlw` is what §7 caps three of the most attractive data cuts against:

```
                              .ovlw   rounded   region at 2 FAT sectors
before gating the ladder      4,328     4,608   4,352   -> D2 REFUSED by 256
after                         2,789     3,072   4,352   -> D2 fits, 1,280 spare
```

So the clock unlocked **D2**, which is 3,584 bytes of `FAT_SEG` — seven times
what A2's own row claimed — and the measurement in §2.1 could not have seen
that because it was measuring footprint and this is a **placement**
constraint. Worth keeping as a caution against the next row that looks small:
in a kernel with overlays, a byte's value depends on where it is, not only on
how big it is.

### 2.4 The batch, measured

A3 + A4 + A1's dead half + A2 + D1 + D2 + D4 + D7, all built:

```
                        KERN_SIZE   heap floor   free heap on 128KB
before this work           96,256      95.5 KB         32.5 KB
after W0-W2 (modules)      88,064      87.5 KB         40.5 KB
+ A3, A4                   85,504      85.0 KB         43.0 KB
+ A1 dead half             84,992      84.5 KB         43.5 KB
+ A2, D1, D4, D7           82,432      82.0 KB         46.0 KB
+ D2                       78,848      78.5 KB         49.5 KB
+ the icon pool (B5)       77,824      77.5 KB       * 50.5 KB *
```

**...and B5 has a second half that is not in that column at all**, because it
is HEAP rather than footprint: the per-window view cache (SPEC.md 22.6.1)
carried the same duplication one layer out - 2KB of its 3KB was one icon body
per entry, in EVERY open Disk window's private claim. Pooling it too takes
`VIEW_KB` 3 -> 2 for `.cold` +34, which is **1,024 bytes per open window and
4,096 with all four up** (SPEC.md 25.8.5). Measured on the 128KB machine: a
Disk window's claim reads 2,048 bytes where it read 3,072.

That one is worth more than its size suggests and is easy to miss in a table
of section deltas, because a claim is not a section: it never appears in
`kernsize` at all. The lever it points at for the rest of this document is
**per-instance claims**, which nothing here has counted.

**50.5 KB, measured on a machine with 128KB in it** (`tests/small128.py`), and
`kern_big` moved by 512 bytes — D4's, the only item here it shares.

**B5 is the row that was not on the list**, and it is the shape §3 was looking
for and did not find: `disk_icons` is 2,048 bytes of `.lowbss` holding one
64-byte body per directory entry, and a listing does not have 32 distinct
icons — every folder is the same picture and most entries have none at all.
SPEC.md 25.8 makes it a **16-body pool with a 32-byte index**, 1,056 against
2,048, and the low rung uncrosses twice: `.lowbss −992`, `.cold +148`,
`.text +2`, **KERN_SIZE −1,024**.

It is the only cut in this document so far that **loses no feature and no
picture**: the seventeenth icon in one listing falls back to the generic icon
§25 already draws, and the A/B against the kernel before it is **0 differing
pixels of 2,740** in both a folder window and a package window. What made it
non-trivial is not the pool, it is that `files.inc` is a **second reader** of
the same array with a slot-per-entry layout baked into a per-window cache —
so the pool stops at `fmv_store`, which expands into that cache rather than
copying it (SPEC.md 25.8.2). Getting that wrong was 301 differing pixels in
exactly one of the two windows.

### 8.2 If 70KB is firm — IT IS NOT, and §0.1 is why

The last ~4KB has to come from somewhere structural, and there are only three
candidates: §4.1's Disk window trim (~1,800), the damage-rect layer §3.1
refuses (1,730), or a fifth and sixth on-demand module out of what `.cold` has
left (`memory.inc` 2,388 and `disk.inc` 5,771 — the second of which is the
mount path itself and cannot be on the disk it mounts). **None of them is
cheap, and the first is the only one that is not actively unwise.**

**THE BUILT POSITION, for anything decided off the rows above.** W0 (assoc
gated), W1 (`FILECP.DRV`), W2 (`FDLG.DRV`), A3 (no loadable drivers), A4
(four volumes), the D batch and B5 (the icon pool) are in, and they reach
**50.5 KB measured on a 128KB machine**
— past the `A + D` row and most of the way to `A + B + D`. What remains
unbuilt in the list is group B (display niceties, ~8,340), group D (sizing
constants, ~6,210, capped by §7), C5–C8 (~3,479), A1's residue (the sound
layer minus the speaker) and A2 (refused, §2.1). The gap to the 70 KB ask is
**27,648 bytes** and every one of them is now a feature.

**And there is a fourth candidate that costs no feature at all: audit the
pinned boot claims.** docs/KERN-SMALL-MODULE-SPLIT.md §9.1 found one by
accident — the association cache holds 3,072 bytes of a 128KB machine's heap
before the user has done anything, and no assembler can see it. Nothing had
ever pointed a `mem_tab` walk at `kern_small`.

**THAT AUDIT HAS NOW BEEN DONE, on a machine with 128KB in it, and it is
EMPTY — so this lever is spent.** `tests/small128.py` boots the floor machine
(`os8088_5150_cga_128k`, the only profile in this tree that is not 640KB) and
walks the table at a bare desktop:

```
int 12h   131,072 bytes (128.0 KB)
HEAP_SEG   89,600 bytes  (87.5 KB)  ->  41,472 free = 40.5 KB
  16E0  1,152 para = 18,432 bytes  owner FE02  purgeable
PINNED on a bare desktop: 0 bytes
USABLE for a program    : 41,472 bytes = 40.5 KB
```

Three things follow. **The 40.5 KB headline is honest** — there is no second
`ASC_KB` hiding behind it, so nothing here can be recovered without giving up
a feature. **The one claim standing is purgeable** (`0xFE` = rank *high*, the
directory read-ahead), 18KB here against 64KB on a 640KB machine, and it goes
back to whoever asks. And **`MIN_RAM_KB` has stopped being arithmetic**: guard
5 compared two constants at assembly time and no machine in this tree had ever
been asked to run the result. It runs, and it reaches a desktop with four
drive zones on it.

So the remaining gap to the ask is the whole gap: **19,968 bytes** at the
built position above, and every byte of it is a feature in §2–§5 — **except
whatever else has B5's shape**, which is the one lever this document
under-weighted. B5 came out of `.lowbss` rather than `.cold`, cost no
feature, and was not on any list here; the question it raises for §5 is which
of the remaining sizing constants are storing the same thing more than once
rather than merely storing it generously.

~~Worth putting back to the requester: **65KB runs SHEET with 13KB spare**.~~
**WRONG, and refuted in §0.1**: that counted SHEET's region and not the ~100KB
of heap it claims when it opens, which is more than the machine has. No row in
this document runs SHEET, and the parallel apps session has taken it off the
small disks.

**The target is retired and the brief is now open-ended** (§0.1): take what
can be taken, and weigh each feature against what losing it does to the
machine. Nothing below is owed to a number.

The two programs the ask was really about have both moved out from under it.
PAINT was solved at the APPLICATION layer — SPEC.md 42.23's 1bpp canvas and
42.6.5's claim-first sizing — which is the shape worth noticing: **a program
that needs less is worth more than a kernel that is smaller**, because the
kernel's remaining rows all cost a feature and the program's did not. Before
taking anything from §2–§5, ask whether the package could stop needing it
instead.

---

## 9. How these figures were taken

Per-file, from the project's own instrument:

```sh
make kernsplit
python3 tools/kernsize.py --modules --build build/smallk -DKERN_SMALL
```

Sub-file, from nasm's `[map all]` on a temporary copy of `kernel/kernel.asm`
assembled with `-DKERN_SMALL`, with each symbol's size taken as the distance to
the next symbol in its section. **The method reconciles exactly** — the summed
spans equal the section lengths for all four sections (`.text` 40,614, `.cold`
34,531, `.bss` 5,512, `.lowbss` 8,712), which is what makes a per-feature figure
quotable rather than indicative.

Two cautions for whoever takes the next reading. `tools/kernsize.py --modules`
reports **`kern_big`** unless `-DKERN_SMALL` is passed after the flags — the
module and theme tables in docs/KERNEL-MEMORY.md are the default variant's by
design. And **committing invalidates `build/kernel.bin` for the symbol
reader**: the About box's build number is the commit count, so `make` again
before re-measuring.

**Nothing in this document has been built or gated.** Every figure is what the
code costs today; what a gate actually returns is that figure less its call
sites, rounded down to the 512-byte rung it lands in.
