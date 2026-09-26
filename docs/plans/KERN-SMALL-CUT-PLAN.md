# `kern_small` — what a 128KB machine can stop carrying

> **THIS IS THE OPEN HALF. What has been BUILT is
> `docs/plans/completed/KERN-SMALL-CUT-BUILT.md`**, and it is worth reading
> first: it carries A3, A4, A2, C3, B5 and the D rows, and the three findings
> that bind every row still on this page - sections are not heap, a byte's
> value depends on where it is, and a claim is not a section.

> **RE-MEASURED AT BUILD 556 (2026-09-09), AND THE HEADLINE MOVED.** A
> `kern_small` desktop has **50.0 KB** of free heap, `KERN_SIZE` **78,336**,
> heap floor 78.0 KB - confirmed on a machine with 128KB in it
> (`tests/small128.py`, `os8088_5150_cga_128k`: *"HEAP_SEG 79872 = 78.0 KB ->
> 51200 bytes free = 50.0 KB, 50.0 KB usable"*, no pinned claim standing).
> The completed companion's 50.5 KB was taken at `KERN_SIZE` 77,824; the tree
> has since grown one image rung to work that landed elsewhere. **Every figure
> below is this tree's**, and the pre-merge build-376 figures the previous
> revision carried are gone rather than annotated - a stale number in a table
> reads exactly like a current one.

> **The 70KB target is RETIRED and the brief is open-ended.** It was derived
> from SHEET's region, and SHEET claims ~100KB of heap on open - more than the
> machine has - so no row in this document ever ran it (§0.1, §8.2). What is
> asked now is: take what can be taken, and weigh each feature against what
> losing it does to the machine. PAINT, the other program behind the original
> ask, was solved at the APPLICATION layer instead (SPEC.md 42.23), which is
> the shape worth noticing.

**Research document, not a contract.** SPEC.md is the binding contract for what
the kernel *is*; this is the study of what `kern_small` could stop being, and
the arithmetic that says how far each answer gets. Nothing STILL ON THIS PAGE
has been built - what was is in the completed companion above.

The ask, in the requester's words:

> `kern_small` needs to run on a system with 128KB of RAM. It does, now, but
> not with enough free heap to run almost any program. I'd like there to be
> 70KB of free heap. That means `kern_small` booted in 58KB. Research what we
> can gate. What would a 128KB system not likely have hardware wise? … For
> `kern_small` everything is on the table.

---

## 0. The verdict, up front

**The whole list, taken, lands at 66.5 KB of free heap — and it costs the
ability to write a file.** That is 1.5 KB better than the previous revision of
this document predicted, and none of the improvement is new cutting: the
baseline moved under it while the rows were being re-priced.

```
today       KERN_SIZE 78,336   heap floor 78.0 KB   free heap on 128KB = 50.0 KB
CLEAN rows  KERN_SIZE 69,632   heap floor 69.5 KB   free heap on 128KB = 58.5 KB
everything  KERN_SIZE 61,440   heap floor 61.5 KB   free heap on 128KB = 66.5 KB
                               ------------------------------------------------
the clean cut  8,704 bytes  |  everything 16,896 = 21.6% of the footprint
```

**AND THE FIRST NUMBER IS NOT THE BINDING ONE.** 66.5 KB is what the
*kernel* arithmetic allows. What a row actually costs is decided on the APPS
disk, and §10 is that audit: five of the rows below have published API slots
that packages shipped on the small floppies CALL AND DO NOT TEST — so the
refusing stub they would become is not a graceful refusal, it is silent wrong
output. **Taking only the rows that break nothing reaches 58.5 KB.**

Seven findings, of which the first four are new at this reading.

1. **A ROW'S COST IS NOT WHAT THE DESKTOP LOSES, IT IS WHAT THE SHIPPED SMALL
   PACKAGES LOSE — and no revision of this document had checked.** §10. Every
   table here priced the kernel side alone, and the "refusing stub" idiom that
   makes gating look cheap only works when the caller tests CF. Swept across
   `apps/`, filtered to what `make smallapps` actually writes: **B4 is dead**
   (Paint's pencil stroke IS `OSAPI_GFX_LINE`, and so is the menu checkmark in
   `os88ui.inc`, which ~20 packages include), and B3, C6, C8 and C1 all have
   untested callers on the small disks.

2. **The measurement method is now VALIDATED against a real gate, and it is
   exact.** §9.1 is the check: the `gfx_line` family's symbol span is 1,503
   bytes, and a build with that family actually gated out measures `.text`
   38,756 → 37,261, which is **1,495 = 1,503 less the 8 bytes of `stc`/`ret`
   stubs the gate adds**. Not close — equal. So the tables below are what a
   gate returns, not an estimate of it, wherever the row's symbols are
   contiguous.

3. **§7 IS BUILT, and the cap it reported was the wrong quantity.** It read
   *"288 bytes now, not 2,816 — D2 spent it"*, off an arithmetic that still
   had `disk_icons` in the region. The real slack was **64 bytes**, and the
   more important half is that the *cap* is not a property of the region at
   all: it is one end of a guard whose other end is `.ovlw`. Move 910 bytes of
   the overlay into the blob (SPEC.md 2.5.3.2) and the listing is free to shed
   800 (SPEC.md 22.6.2) — **1,024 bytes of heap, `HEAP_SEG` 0x12c0 → 0x1280**.
   The `%error` is still real and still fires if the listing is cut alone,
   which is what makes the two halves one change.

4. **D3 is not capped, it is IMPOSSIBLE**, and it should be struck rather than
   deferred. `files.inc:662` requires `DSK_NENT * DSK_DE_STRIDE` to be a
   multiple of 256, because `FS_IOFH` holds an icon base in one byte. At a
   stride of 24 that makes `DSK_NENT` a multiple of **32**, so the only legal
   value below today's is zero. §5.1.

5. **There is still no big single win.** The largest symbol in the kernel is
   `osapi_table` at 1,312 bytes and the second is `sch_stacks` at 1,280, which
   is the task slices and not code. Below that it is a long tail of 40–200
   byte procedures. The cut has to come from removing whole *features*.

6. **The hardware question is SPENT.** It was worth ~4,700 bytes when this
   document opened and it is worth **1,110** — the sound layer, and nothing
   else. A3, A4 and A2 are built, and A2r, the residue this revision first
   carried as a live row, turns out to be the SOFTWARE clock: §2.2.

7. **The heap COMPACTOR is not on this list**, and was costed rather than
   assumed: **docs/plans/KERN-SMALL-NOCOMPACT.md**. What looks like a nicety
   on a small machine is what makes the small machine work.

---

## 0.1 A PROGRAM'S SIZE IS ITS REGION *PLUS* THE CLAIMS IT MAKES TO RUN

This document sized programs by their `.o88`, which is the number `ls` gives
and the number a disk catalogue prints. It is not the number that decides
whether a program runs.

**SHEET makes almost 100KB of heap claims on open** — its grid, its cell
store, its undo — which is more RAM than a 128KB machine has in total, before
its 48,352-byte region is counted at all. So **SHEET will never run on
`kern_small` in its current form**, at 70KB of free heap or at any other
figure this document could reach.

**The 70KB target is retired.** It was reverse-engineered from "the heap at
which SHEET loads", and that premise is gone. The other program behind it was
PAINT, and **that one has been solved at the APPLICATION layer instead** —
SPEC.md 42.23's 1bpp canvas and SPEC.md 42.6.5's claim-first sizing. The
kernel was being asked to make room that the program was better placed to stop
needing.

**And the shape has held up.** SPEC.md 28.12 is the same move on the Task
Manager: gating the heap page and the memory view out of the `APP_SMALL` arm
takes one instance from **11,138 bytes to 6,693**, 39.9%, against a 128KB
machine's whole free heap of 50 KB. **It cost the kernel nothing at all**,
which is what makes it worth preferring to any row in §2–§5: no `KERN_SIZE`,
no rung, no feature that a 640KB machine loses.

### 0.1.1 …and a claim is INVISIBLE to every measurement in this document

`kernsize` reports sections. A heap claim is not a section — it appears in no
column of any table here, and no `%if` in `kernel.asm` can see it. Every
figure in §1–§8 is therefore a *footprint* figure.

**Two of the three claim-shaped levers this document pointed at are now
SPENT**, and the next reader should not re-derive them:

- **The pinned-claim audit is empty, and re-confirmed at this reading.**
  `tests/small128.py` walks `mem_tab` on the floor machine at a bare desktop
  and reads **0 bytes pinned, 0 purgeable**. There is no second `ASC_KB`
  hiding behind the 50.0 KB headline.
- **The menu save-under is no longer a 20KB resident claim.** `MENU_SAVE_KB`
  is 20 and reads like 20KB of a 128KB machine's heap; it is now only the
  build-time *ceiling* asserted in `kernel.asm:7442`. `menu_save_kb` sizes the
  claim from the rect and `[vid_planes]`, takes it when a menu drops and
  releases it before the routine returns — *"~4KB on VGA and ~1KB on
  Hercules"*. Three quarters of the old fixed figure was plane-count that a
  1bpp adapter was never going to write.
- **What is left is the per-instance claim**, and B5's second half is the only
  one anybody has counted: `VIEW_KB` is 2, so an open Disk window costs 2,048
  bytes of heap and four of them cost 8,192 — against a 50.0 KB arena. It
  appears in no table here because a claim never does.

---

## 1. The arithmetic, and where the bytes are now

`kern_small`'s footprint is one contiguous span from `KERNEL_SEG` (linear
0x00600) to `KERN_END`, and it is four rungs, each rounded up to 512 bytes:

| rung | holds | measured | rung | slack |
|---|---|---:|---:|---:|
| image | `.text` 38,756 + `.bss` 4,819 | 43,575 | **44,032** | 457 |
| cold | `.cold` | 26,483 | **26,624** | 141 |
| FAT | `DSK_FAT_SECS` = 2 sectors | 1,024 | **1,024** | — |
| low | `.lowbss` 6,060 + `STK0_SIZE` 512 | 6,572 | **6,656** | 84 |
| vgabuf | the planar decoder's buffers — **already zero on small** | 0 | **0** | — |
| | | | **78,336** | |

The heap starts where the kernel actually ends:

```
free heap = int 12h  -  (KERNEL_SEG*16 + KERN_SIZE)
          = 131,072  -  (1,536 + 78,336)  =  51,200 bytes  =  50.0 KB
```

**`.ovl` (1,333) and `.ovlw` (1,910) are NOT in that sum and buy nothing when
cut.** They are boot-overlay code loaded onto memory the machine reuses once
it is up. They matter here for one reason only, and it is §7's: `.ovlw` lands
on the FAT window, so it is what *caps* the FAT and mount-buffer rows —
which is not the same as buying nothing, because a byte MOVED from `.ovlw`
into `.ovl` raises that cap for free. §7 is that done once; the figures here
are after it.

**Two things that are not levers.** The rungs waste 682 bytes in rounding —
that is noise, not headroom, and CLAUDE.md's rung rule refuses it as an
argument in either direction. And `KERN_SMALL_BUDGET` has 29,184 bytes spare:
that is the *guard*, not the machine. Lowering the guard saves nothing; only
lowering `KERN_SIZE` moves the heap.

### 1.1 What the theme table says about where to look

| theme | bytes | share |
|---|---:|---:|
| the file system, end to end | 23,282 | 35.7% |
| the window system and its furniture | 21,292 | 32.6% |
| drawing: adapters, primitives, glyphs, icons | 7,337 | 11.2% |
| the kernel proper: API table, heap, scheduler, events | 6,623 | 10.2% |
| hardware: drivers, clock, mouse, sound, CPU, XMS | 4,865 | 7.5% |
| the three built-in kinds | 1,356 | 2.1% |
| the Control Panel | 484 | 0.7% |

**The file system and the window system are 68% of the code**, and the
hardware column has fallen from 9.6% to 7.5% because most of it has already
been taken. The requester's own list is drawn almost entirely from the two
themes that are 10% of it together.

### 1.2 The twelve files that hold it

Heap-bearing sections only, this tree, attributed by where each symbol is
defined. `.ovlw` is shown because §7 makes it the constraint, not because it
is worth cutting.

| file | HEAP | `.text` | `.cold` | `.bss` | `.lowbss` | `.ovlw` |
|---|---:|---:|---:|---:|---:|---:|
| `wm.inc` — window manager | **11,106** | 10,083 | 47 | 976 | — | 47 |
| `files.inc` — the Disk window | **9,104** | 1,004 | 7,770 | 330 | — | 83 |
| `disk.inc` — volumes, mount, FAT read | **6,816** | 265 | 5,929 | 622 | — | 762 |
| `diskw.inc` — the FAT write path | **5,077** | 179 | 4,740 | 158 | — | — |
| `vga12.inc` — the drawing primitives | **3,573** | 2,939 | — | 122 | 512 | — |
| `mouse.inc` — serial mouse and cursor | **3,538** | 3,259 | — | 151 | 128 | 646 |
| `menu.inc` — the menu bar | **3,175** | 2,717 | 177 | 197 | 84 | 34 |
| `ui.inc` — the UI task | **2,893** | 2,840 | — | 53 | — | — |
| `kernel.asm` — API table, `kmain`, shims | **2,883** | 2,865 | 18 | — | — | — |
| `sched.inc` — scheduling and the slices | **2,847** | 1,316 | — | 123 | 1,408 | 225 |
| `instance.inc` — instances | **2,476** | 1,566 | 236 | 674 | — | 27 |
| `dskwin.inc` — the mount-owned buffers | **2,336** | — | — | — | 2,336 | — |

`vga12.inc`'s name is misleading on this build: the VGA planar half is gated
out, and what is left is the primitive layer every adapter goes through —
of which **1,503 bytes is the `gfx_line` family alone** (§3, B4).

---

## 2. Hardware a 128KB machine has not got

The requester's first question, and **most of it has now been taken**. A3
(loadable drivers), A4 (`DVOL_MAX` 8→4) and A2 (the clock ladder's probes) are
all built; the record is the completed companion's §1 and §3.

Already gated out, with nothing further to win: the **VGA** (`GFX_VGA`), the
**planar row decoder** (SPEC.md 5.4.1.3), the **whole-column store**
(SPEC.md 39.25), the **PS/2 mouse** (SPEC.md 9.9), **memory above 1MB**
(SPEC.md 41.4 — `xmem.inc` is down to 20 bytes), the **theme** (SPEC.md 76),
the **scrollbar thumb drag** (SPEC.md 13.10.5), the **band composer**
(SPEC.md 5.9), **SAVER.DRV** (SPEC.md 64) and **loadable drivers of every
kind** (SPEC.md 51.0 — `driver.inc` is down to 231 bytes from 2,550).

What is actually still on the table:

| # | option | HEAP | `.text` | `.bss` | what it costs |
|---|---|---:|---:|---:|---|
| A1 | **Sound layer** SPEC.md 34 (`snd.inc`) | **1,110** | 834 | 276 | no PC-speaker tone or PCM at all. 256 of the `.bss` is `snd_xlat` |
| A2r | ~~**Clock residue** SPEC.md 37 (`clock.inc`)~~ | ~~541~~ | — | — | **DEAD — §2.2.** There is no hardware left in it: all four rungs are already out of the assembly, and the 507 bytes are the SOFTWARE clock — the menu bar's cell, file timestamps and toast placement |
| | **subtotal** | **1,110** | | | of which A2r is unavailable — A1 is the whole group |

**A1 is deferred at the owner's instruction** — *"keep pc speaker for this
round - we may cut it later, but for now."* Worth recording for whoever picks
it up: A3 has already made **part of it dead**, because the FM and Sound
Blaster tiers are reached through `SOUND.DRV` and no driver can be loaded
here. What the speaker actually needs is the tone path and `snd_xlat`'s 256
bytes of PCM rescale, so A1 splits and the already-unreachable half is the
cheaper one to take.

### 2.2 A2r is DEAD, and it is this document's own stale sentence

A2r was carried into this revision as *"the per-rung read and write bodies,
which are dispatched at run time and are gated by nothing"*, and recommended
as *"the cheapest row in the document with a contract already behind it"*.
**That sentence was true at build 376 and A2's own build made it false.** It
was re-published rather than re-checked, which is the failure this document
warns about in its own header.

`clock.inc` compiles `%define CLK_TRY(n) 0` when `OS88_RTC` is undefined, and
the comment beside it says what that costs: *"every rung's body is out of the
assembly too"*. Checked against the symbol map rather than the source —
`clk_at_read`, `clk_at_get`, `clk_ns_read`, `clk_ns_get`, `clk_rp_get`,
`clk_bios_read` and all three probes are **ABSENT from the kern_small
build**. There is no hardware clock code left to gate.

**What the 507 bytes of `.text` actually are is the SOFTWARE clock**, and
three things render off it:

| symbol | bytes | who needs it |
|---|---:|---|
| `clk_fmt` + the formatters (`clk_put_mon`, `clk_put2/4`, `clk_h12h`, `clk_ampm`, `clk_mnames`, `str_len`) | 240 | **`menu.inc:1513` — the menu bar's clock cell**, and `toast.inc:428`, whose gap arithmetic is the clock's own length |
| `clk_tick` + `clk_inc_sec` + `clk_mlen` | 201 | `ui.inc:709` — advancing the clock off the **BIOS tick**, which is rung 0 and is the one that still works |
| `clk_snapshot` | 30 | **`diskw.inc:3654` (`dskw_now`) — every file's FAT timestamp**, and `ctrl.inc:3510`, the Control Panel's Date & Time page |

So gating it does not remove a dead hardware path. It removes the clock from
the menu bar, breaks toast placement, and puts a garbage date on every file
the machine saves. **0 bytes available.**

Even the RTC-only `.bss` is not free: all eight bytes (`clk_cent`, `clk_rb`,
`clk_rbin`, `clk_rpep`, `clk_rp24`, `clk_rtc`, `clk_tier`, `clk_dirty`) are
still read from `ctrl.inc` and `hiber.inc`, so taking them means editing the
Control Panel page as well — five bytes for a page edit.

**The general lesson is §10's, one document earlier than §10.** A2r was priced
off a *sentence in this file* rather than off the tree, exactly as B4 was
priced off the kernel and not the apps disk. The rule that catches both:
**re-derive a row before re-publishing it, especially when the row's own
companion says the thing it depends on was built.**

---

## 3. Display niceties

**The distinction that matters is between a *nicety* and *the optimised
path*, and §3.1's three are the second thing.**

| # | option | HEAP | what it costs |
|---|---|---:|---|
| B1 | **Raise cache / save-under** SPEC.md 11.96 (`wm_su*`) | **2,451** | raising a covered window goes from ~10 ms back to the **1,026 ms** SPEC.md 11.96 was written to fix. The buffer is a purgeable claim, so the saving is code only |
| B2 | **Drag cache** SPEC.md 11.96.12 (`wm_dc*`, `wm_cov*`) | **484** | a window drag repaints what it uncovers |
| B3 | **Icon renderer** SPEC.md 10 (`icons.inc`) | *1,060* | **BLOCKED — §10.** `OSAPI_ICON_DRAW`/`_PEN` are called untested by `os88ui.inc`, Paint and Solitaire. `disk_icons` is GONE (SPEC.md 25.9) and what stood in its place, 800 bytes of listing, has been taken — §7 |
| B4 | ~~**`gfx_line` family**~~ | ~~1,503~~ | **DEAD — §10.1.** Paint's stroke and the menu checkmark are both `OSAPI_GFX_LINE`, neither tests CF. The 1,503 is still the honest *size*; it is simply not available |
| B5 | **Toast** SPEC.md 59 (`toast.inc`) | **458** | SPEC.md 47 rule 3 wants every refusal to say something the user can act on, and SPEC.md 59 is where three of them say it |
| B6 | **Progress widget** SPEC.md 12.8 (`fprog.inc`) | **725** | long file operations go silent |
| B7 | **Screen blanker** SPEC.md 64 (`blank.inc`) | **148** | |
| | **subtotal** | **6,829** | of which **B4 is unavailable and B3 is blocked** — the clean total is **4,266** |

**B4 WAS RECOMMENDED AS THE ROW TO TAKE FIRST, AND IT IS DEAD.** The
reasoning was that no *kernel* drawing path calls it — every caller of
`gfx_ls_*` and `gfx_line_*` is inside the family itself — so the gate would be
four `stc`/`ret` stubs and nothing else. That is true and it is the wrong
question: the family exists for PACKAGES, and §10.1 is the sweep that should
have come first. Paint's freehand stroke *is* one `OSAPI_GFX_LINE` per segment
(SPEC.md 42.8), and Paint ships on both small floppies. The remaining rows in
this group are graded against the same sweep now.

### 3.1 Three things that look like niceties and are not — do not cut these

- **Damage rects and the clip region** SPEC.md 11.90/11.91 (`wm_dmg*` 836 +
  `wm_clip*` 864 = **1,700**). This is not an optimisation layered over the
  redraw path, it *is* the redraw path — PERFORMANCE.md part 5 makes a change
  that reintroduces a full repaint a regression against a documented number.
  Cutting it makes every window operation cost the whole screen on the slowest
  machine that runs this build.
- **`gfx_pairtab0`/`gfx_pairtab1`** (512 bytes of `.lowbss`) and
  **`vid_rowtab`** (256). These *are* the optimised path the requester asked
  to keep, and **they cannot be combined or overlaid** — asked and answered,
  so that nobody derives it a third time. Both are build-once permanent lookup
  tables read **on the same call, five instructions apart**: `gfx_blit4`'s row
  loop calls `gfx_rowbase` (which reads `vid_rowtab`) and then picks a pair
  table on `(x+y)&1`. Different domains, different value widths, and neither
  is ever dead while the machine is drawing.

  Two facts that look like openings and are not. **They are lazily built and
  usually are not** — `gfx_pairbuilt` reads 0 on a desktop with a Disk window
  full of icons open, because icons go through `icon_draw`'s sprite engine —
  but they are `resb`, so the bytes are spent either way. And **Paint going
  1bpp does not free them**: `OSAPI_GFX_BLIT4` is a published slot and CHART
  is on the small apps disk and blits its canvas through it.
- **`softgfx.inc`** (1,200). On `kern_small` this is the *only* renderer — the
  VGA path is already gone — so there is no second path left to collapse into.

### 3.2 And a standing objection to B1 worth keeping on the record

`KERN_SMALL_BUDGET`'s twenty-first move raised this build's budget *for* the
window redraw optimisations, with the reasoning attached: **"a redraw
optimisation is worth most on the slowest machine, so this is not a figure
that work may be kept out of."** B1 is that decision run backwards. It is
re-decidable — a machine that cannot start a second program has a worse
problem than a slow raise — but it should be re-decided explicitly rather than
swept up with the blanker.

**And B1 does not price the way its symbol count suggests.** Its own symbols
are 2,451 bytes, but its address hull is 3,250: `wm_dc_take`, `wm_cov_add` and
`wm_draw_win.paint` are interleaved with it. So a gate on B1 alone returns
2,451 and not the hull — the row is priced at its own symbols throughout this
document, and B4's is the only row where the two coincide.

---

## 4. Features — product decisions rather than build ones

**Three of this group's four biggest rows have been taken since the previous
revision**, which is why the subtotal has fallen from ~16,700 to 8,335: C3
(associations) was gated, and C2 and C4 became on-demand modules (§6). What is
left of those two is their resident stubs.

| # | option | HEAP | what it costs |
|---|---|---:|---|
| C1 | **FAT write path** SPEC.md 18.4–18.6 (`diskw.inc`) | **5,077** | a **read-only OS**: nothing saves, formats, renames or deletes |
| C5 | ~~**Built-in kinds** SPEC.md 14 (`apps.inc`)~~ | ~~1,594~~ | **BUILT — SPEC.md 14.6.** Timer, Bounce and the Builtins menu are gated; `KERN_SIZE` 78,336 → **76,800**, free heap 50.0 → **51.5 KB** measured on the floor machine. About stays and was priced by gating it: **298 bytes that cross no rung** (14.6.3) |
| C6 | **Fullscreen exclusive** SPEC.md 53 (`fsx.inc`) | *788* | **BLOCKED — §10.** Cyclone, Missile and Paint call `OSAPI_FSX_RUN`/`_CAPS` untested; a package that believes it took the screen and did not is worse than one that cannot |
| C7 | **The dock** SPEC.md 30 (`dock.inc`) | **717** | |
| C8 | **Clipboard** SPEC.md 55 (`clip.inc`) | *159* | **CONDITIONAL — §10.** Every small caller tests `OSAPI_CLIP_SIZE`, but Sheet and TexPad do not test `_PUT` and five do not test `_GET`; wants a per-caller read before it is taken |
| C2r | **File-dialog residue** SPEC.md 38 (`fdlg.inc`) | *330* | the module's resident stub. **Not separately takeable** — deleting it deletes the feature the module already made cheap |
| C4r | **Copy/paste residue** SPEC.md 22.3 (`filecp.inc`) | *328* | as above |
| | **subtotal (C1, C5–C8)** | **8,335** | of which only **C5 and C7 (2,311) are clean** |

**C1 is 61% of the group on its own**, and it is the row that decides whether
this build is an operating system or a viewer. It is also **refused by the
module mechanism** and so cannot be moved instead of deleted — §6.

### 4.1 Trimming the Disk window rather than deleting it

`files.inc` is 9,104 bytes and is how a program gets launched, so it cannot go.
It can be thinned, and the separable behaviour re-prices higher than the
previous revision claimed:

| item | `.cold` | note |
|---|---:|---|
| inline rename (`fm_edit*`) | **711** | was priced at 451 |
| drag-and-drop (`fm_drag*`, `fm_dg*`) | **365** | contiguous — a clean gate |
| clone (`fm_clone*`) | **132** | |
| the more-files marker (`fm_more*`) | **125** | contiguous |
| | **1,333** | |

A harder pass over the scroll and view caches (`fmv_*` 967) could plausibly
find as much again, at the cost of a Disk window that lists and launches and
does nothing else. Call it **~2,300 bytes**, up from the ~1,800 this section
used to claim.

---

## 5. Sizing constants — data, with no feature lost

**Every row here was MEASURED at this reading by changing the constant and
re-assembling**, which is why three of them moved and one of them died.

| # | option | HEAP | note |
|---|---|---:|---|
| D5 | ~~**`MAX_WIN` 12 → 6**~~ | ~~414~~ | **BUILT — SPEC.md 11.102** |
| D1b | ~~**Partition** −128 −192 (4 slices)~~ | ~~320~~ | **BUILT.** Priced at −448 by dropping the 256 and a 192, and that was WRONG: the Task Manager declares `OS88_STACK_256` and Paint takes the 384 default, so `128/192/384` would have refused that pair. Ships as `128/192/256/384` (SPEC.md 11.102.3) |
| D6 | ~~**`INST_MAX` 12 → 6**~~ | ~~114~~ | **BUILT — SPEC.md 11.102** |
| D7b | ~~**`MEM_MAX` 20 → 16**~~ | ~~40~~ | **BUILT — SPEC.md 11.102** |
| D3 | ~~**`disk_dir` 32 → 16**~~ | — | **IMPOSSIBLE — §5.1** |
| | **the batch, measured** | **1,024** | `KERN_SIZE` 76,800 → **75,776**, free heap 51.5 → **52.5 KB** on the floor machine |

~~D5 and D6 are worth the least and cost the most process: they are published to
packages, so moving them means every `.o88` is built against a different bound
and the "one `.o88` serves both kernels" property is at risk.~~ **WRONG, and
§5.2 is the correction**: the property is not at risk in the direction these
rows go, and the project has already taken this decision twice — `MAX_TASKS`
is 7 here against the SDK's 14 (D1) and `MEM_MAX` 20 against 32 (D7), both
built, both live entries in `tests/unit/t_mirror.py`'s `DIVERGENT` block.

### 5.1 D3 is impossible, and the reason is not the boot overlay

D3 has been carried as *"capped by §7"* since this document opened. It is
worse than capped. Setting `DSK_NENT` to 16 produces **two** errors, and the
first one is fatal to the row at any value:

```
kernel/files.inc:662: error: FS_IOFH holds an icon base in ONE byte:
    nmax*DSK_DE_STRIDE must be a multiple of 256.
kernel/kernel.asm:7090: error: the boot overlay's window half has outgrown
    the FAT window plus the mount buffers
```

`DSK_DE_STRIDE` is 24. `n * 24 ≡ 0 (mod 256)` reduces to `n * 3 ≡ 0 (mod 32)`,
and since `gcd(3, 32) = 1` that means **`DSK_NENT` must be a multiple of 32**.
The only legal value below 32 is 0. The row is struck rather than deferred;
listing more than 32 files per volume would be legal, listing fewer is not.

### 5.2 What a mirrored constant actually breaks, and which way

The rows above were carried as *"an ABI change"* on the strength of the word
**mirrored**. Read against the code, the mirrored constants are **three
different risk classes**, and only one of them is about the ABI at all.

**Class 1 — it sizes a buffer the PACKAGE allocates and the KERNEL fills.**
`INST_MAX` and `MEM_MAX` are inputs to equs the package compiles into itself:

```
SYS_SNAPSHOT_SIZE   equ SS_INST + INST_MAX * SSI_RECSZ
CLAIM_SNAPSHOT_SIZE equ MEM_MAX * CLS_RECSZ
```

`osapi_sys_snapshot_x` then fills that buffer bounded by **the kernel's own**
`INST_MAX`. So the failure is a write past the end of a buffer *in the
package's segment*, and it is **directional**:

| | effect |
|---|---|
| kernel value **larger** than the SDK's | records the package never reserved — **overrun** |
| kernel value **smaller** | short write; untouched records stay 0, which reads as `SSI_STATE` = free — **benign** |

`tests/unit/t_mirror.py`'s `DIVERGENT` block states the rule and already
carries two of these: *"The SDK carries the LARGER value and the kernel may be
smaller… Shrinking the SDK's copy to match kern_small would overflow that
buffer on kern_big."* **Every D row here shrinks the kernel and leaves the SDK
alone, which is the safe direction by construction.**

**Class 2 — `MAX_TASKS` is additionally pinned.** `SS_TMAX equ 16` fixes the
layout so the count can move without moving an offset, and it exists because
the unpinned version already bit: *"8 -> 14 moved `SS_INST` by 30 bytes, and a
TASKMGR built against the old SDK was written past its own buffer by exactly
that."*

**Class 3 — `MAX_WIN` sizes nothing.** It appears in `apps/os88api.inc`
exactly once, as a bare `equ`, and the file says why there is no `WIN_SIZE`
beside it: *"the stride is 34 on one shipping kernel and 28 on the other, and
a window INDEX never leaves the kernel anyway."* Its one code user in the tree
is Telnet's `tz_wmap` (and Telnet is in `SMALLOMIT`), which polls
`OSAPI_WM_OWNSEG` per slot — and the kernel bounds-checks against **its own**
`MAX_WIN` before touching the table:

```nasm
wm_ownseg:  cmp al, MAX_WIN
            jae .no                 ; .no: stc
```

Six kernel sites validate a package-supplied window reference that way. So a
package built at 12 asking a 6-slot kernel for slot 11 gets a clean refusal.

#### 5.2.1 THERE IS NO REFUSAL ON THE SNAPSHOT PATH — on either side

The SDK advertises one. `OSAPI_SYS_SNAPSHOT` answers `AX = MAX_TASKS,
BX = INST_MAX`, the buffer carries `SS_NTASK`/`SS_NINST`, and the comment
beside them says the point is *"so a stale SDK can SEE the mismatch"*.

**Nothing reads any of it.** `SS_NTASK` and `SS_NINST` have no reader anywhere
outside their own definition, and both call sites ignore the registers and
walk with their own compiled-in bound. `apps/audio/apengine.inc` is the
specimen, because the comment and the next instruction disagree in one line:

```nasm
    call OSAPI_SYS_SNAPSHOT        ; AX = MAX_TASKS, BX = INST_MAX
    mov cx, INST_MAX               ; ...and uses its own constant anyway
```

TaskMgr is the same (`mov cx, MEM_MAX`, `cmp bx, INST_MAX`, `mov cx,
MAX_TASKS`). **So the safety here is structural over-allocation and not a
check** — a mismatch in the safe direction is invisible, and one in the unsafe
direction is silent memory corruption rather than a refusal. That is the same
finding as §10's one layer down: the mechanism that makes a change look safe
is only safe because of what the callers do, and nobody had looked.

**What each row therefore owes** is a `DIVERGENT` entry with its reason — the
gate pairs by name, so an intended divergence must be declared or the build
fails — and nothing else. D5's reason is class 3, D6's is class 1.

---

## 6. The lever that keeps the features: more on-demand modules

`mod.inc` (SPEC.md 2.8) is *"`.cold` with the address changed, and nothing
else"*. On this build it already carries five modules cut out of the binary
and read into a heap claim when the feature is asked for:

```
ctrl.drv    4,580   format.drv  1,129   clone.drv   5,810
filecp.drv  2,161   fdlg.drv    3,243              = 16,923 bytes NOT in the footprint
```

**W0–W2 of docs/plans/completed/KERN-SMALL-MODULE-SPLIT.md are built** —
`assoc` gated, Cut/Copy/Paste became `FILECP.DRV`, the Standard File dialog
became `FDLG.DRV` — and **there is no fourth wave**. The two candidates left
are refused by the mechanism itself:

| module | verdict |
|---|---|
| `assoc.inc` | **refused**: `mod_need → drv_mounted → dsk_chdir_q_x → dsk_chdir_x → disk_mount_x → asc_lookup_x`. Loading any module can mount, and a mount calls associations. Gated instead (SPEC.md 54.0) |
| `diskw.inc` | **refused**: it is the by-name file I/O layer, not the write path. `mod.inc` calls `dskw_read_x` *to load a module*; `driver.inc` and `loader.inc` call it to load a driver and a package; CTRL.DRV and CLONE.DRV far-call `dwf_dskw_*` from inside their own images. 33 entry points against `MOD_NENT`'s 8 |

**So C1 can be deleted and cannot be moved**, and that is the whole of why §8's
last tier is the only one that reaches 67 KB.

### 6.1 The rule it runs into

docs/plans/completed/ONDEMAND-PLAN.md §1 states the test:

> A feature may be loaded on demand only if the **system disk is already
> required** to do it, or can be required **without interrupting what the user
> was doing** — because on a one-floppy machine every load is a disk swap.

That verdict was correct for the machine it was written against, and W1/W2
were taken past it deliberately for `kern_small` alone, on the argument that
**the alternative on the table is deletion, not the status quo**: a dialog
that sometimes will not open is worse than one that always does, and better
than one that does not exist.

### 6.2 Two structural options that are worse than they look

- **Break ABI parity for `kern_small`.** `osapi_table` is 1,312 bytes (164
  slots at 8 apiece) and it is the largest single symbol in the kernel, which
  makes it look like the answer. It is not: collapsing the refused slots saves
  a few hundred bytes and costs the property docs/history/KERN-SPLIT-PLAN.md
  §0 calls the one everything else depends on — **one `.o88` serves both
  kernels**. A slot number that exists in one build and not another is an ABI
  that depends on a knob (SPEC.md 20.8 rule 4). Bad trade, and the refusing
  stubs are nearly free anyway: the cells are in both tables already, and the
  bodies can share **one** `stc`/`ret` between them.
- **A single-adapter `kern_small`** (one binary for CGA, one for Hercules).
  `viddet.inc` 1,002 + `vidsel.inc` 211 ≈ **1,213**, and it doubles the
  shipped small images and the test matrix. Marginal.

---

## 7. The floor: `.ovlw` sits on the FAT window — and the region under it is the LISTING

> **BUILT, and the section is rewritten on the measurement rather than
> amended.** What was here priced the *shrink available* at **288 bytes** off
> an arithmetic that still had `disk_icons` in the region (SPEC.md 25.9 took
> the icon bodies out) and had not noticed that the region and the overlay are
> two ENDS of one guard. Both have since moved, in the same change: SPEC.md
> 2.5.3.2 cut `.ovlw` by 910 bytes and SPEC.md 22.6.2 spent the room on the
> listing. The row is closed and the arithmetic below is what it came to.

The boot overlay's window half (`.ovlw`, SPEC.md 2.5.3) is loaded onto
`FAT_SEG` and spills through the mount-owned buffers immediately above it —
one contiguous region that is dead until the first mount. `kernel.asm` guards
it:

```nasm
%if ((OVLW_SIZE + 511) / 512) * 512 > FAT_PARA * 16 + DSK_WIN_BYTES
%error "the boot overlay's window half has outgrown the FAT window plus the mount buffers"
%endif
```

### 7.1 What it was, and why the old figure read like a dead end

At the top of this work the guard stood like this:

```
.ovlw            2,820  ->  3,072  rounded up to whole sectors
FAT window       1,024   (DSK_FAT_SECS = 2, and AT ITS FLOOR - see below)
dsk_secbuf         512
disk_dir         1,536   (DSK_NENT 64 x DSK_DE_STRIDE 24)
dsk_icoix           64
DSK_OVLPAD           0
region           3,136
                 -----
slack               64
```

**Sixty-four bytes, and `DSK_OVLPAD` at zero.** Those two facts together are
what made the row look like nothing: no byte of the region was dead padding,
so the arithmetic reads as if the listing were sized by the listing. It was
not. `.ovlw` occupied 3,072 of the 3,136, so the listing was **not free to
shrink** — cut `DSK_NENT` to 32 and the region falls to 2,336, the rounded
overlay no longer fits, and `DSK_OVLPAD` has to come back at ~736 bytes of
dead `.lowbss` to hold the overlay up. That is a shrink that buys nothing, and
it is exactly the wall an earlier session hit and reported as *"kern_small
doesn't get smaller because I can't shrink the icon space, `.ovlw` is
there."*

**The FAT window is not the half that moves.** `DSK_FAT_SECS` is 2, and 2 is a
floor rather than a trim: the smallest geometry this OS boots is a 360KB
floppy, which declares a 2-sector FAT, and SPEC.md 18.2 rule 10 is an
ACCEPTANCE threshold — a value of 1 would refuse every volume the kernel can
mount rather than merely list less of one. So the whole of the region that can
move is `DSK_WIN_BYTES`.

### 7.2 What it came to

```
.ovlw            1,910  ->  2,048  rounded up to whole sectors   (SPEC.md 2.5.3.2)
FAT window       1,024   (unchanged, and at its floor)
dsk_secbuf         512
disk_dir           768   (DSK_NENT 32 x DSK_DE_STRIDE 24)        (SPEC.md 22.6.2)
dsk_icoix           32
DSK_OVLPAD           0   (and it STAYED zero, which is the point)
region           2,336
                 -----
slack              288
```

| | before | after |
|---|---:|---:|
| `.ovlw` | 2,820 | **1,910** |
| `.ovl` | 423 | **1,333** (of 1,984 — 651 free) |
| `.text` | 37,263 | 37,271 (**+8**, two `OVBCALL` sites) |
| `.lowbss` | 5,236 | **4,436** (−800) |
| `LOW_PARA` | 384 para | **320** |
| `KERN_SIZE` | 75,264 | **74,240** |
| `HEAP_SEG` | `0x12c0` = 75.0 KB | **`0x1280` = 74.0 KB** |

**1,024 bytes of heap on every `kern_small` machine**, for 8 resident bytes of
`.text`. The 800 bytes of `.lowbss` are what the change is worth; the 1,024 is
the rung falling, and it fell two steps of 512 rather than one because
`LOW_PARA` is `((KLOW_SIZE + STK0_SIZE + 511) / 512) * 32` and 5,748 was 116
bytes over a step.

### 7.3 THE ORDER IS THE WHOLE LESSON, and neither half can land alone

- **`.ovlw` → `.ovl` on its own moves `HEAP_SEG` by ZERO.** Neither half of
  the overlay is on the memory ladder: `.ovl` rides inside a blob that is
  `BOOT2_SECS` sectors whatever it contains, and `.ovlw` lands on a window the
  ladder reserved anyway. This is
  docs/plans/completed/KERN-SMALL-CUT-BUILT.md's *SECTIONS ARE NOT HEAP*
  arriving a third time, and it is the trap a reader of this row will fall
  into: the section totals move 910 bytes and the scoreboard does not twitch.
- **`DSK_NENT` on its own is a `%error`.** Verified by building it: with the
  listing cut and the probes left in `.ovlw`, `nasm` stops at
  `kernel/kernel.asm` with *"the boot overlay's window half has outgrown the
  FAT window plus the mount buffers"*.
- So the pair is the change, and the overlay goes first.

**What it cost the blob is nothing**, which is the part that makes this a
better trade than it was proposed as. `.ovl` is at `OVL_AT` inside a
9-sector blob, and on `kern_small` 1,561 of those bytes were zero padding
stage 1 was already reading. The move spends 910 of them. `.ovlw`, by
contrast, is real file bytes at the end of the kernel image, so
`KERNEL.SYS` went **122 sectors to 120** and `build/small360.img` 250
clusters to 249.

### 7.4 What is left, and which side of the guard to spend it on

`.ovlw` has **138 bytes** before it rounds up a sector and `.ovl` has **651**
before the blob is full — but they are ONE pool of **789 bytes**, because
moving a body across grows one by exactly what it takes from the other. The
split point is therefore a judgement about which side is more likely to grow,
not an optimisation:

- **`.ovl` is shared with `kern_big`**, which has 473 bytes free there. Keep
  `kern_small` above that and `kern_big` stays the build that binds the blob;
  let it fall below and an `.ovl` addition starts breaking one kernel and not
  the other, with no cheap way back. **(It has fallen below, deliberately:
  SPEC.md 2.5.3.3 put `kmain`'s boot half in the blob, and kern_small binds it
  now at 42 bytes free against kern_big's 152.)**
- **`.ovlw` growing is the self-correcting direction**: the fix is to move
  another body across, and SPEC.md 2.5.3.2's `OVBCALL` is what makes that a
  two-line change. The candidates are sized there; the next-best pair is
  `desk.inc`'s 137 and `disk.inc`'s floppy probe at 472, which travel together
  because `desk_init` near-calls it.

**And 32 is the floor of the listing, not a dial.** `files.inc`'s `FS_IOFH`
holds a listing's icon base in one byte, so `DSK_NENT × DSK_DE_STRIDE` must be
a multiple of 256 — at stride 24 the only legal value below 64 is 32, and the
only one below 32 is zero. Whatever the guard's slack becomes, there is no
further heap on this row.

---

## 8. The whole list, added up

```
A  hardware                        1,110     clean (A1 only; A2r is dead)
B  display niceties                6,829     4,266 clean, 1,503 dead, 1,060 blocked
C  features (C1, C5-C8)            8,335     2,311 clean, 6,024 blocked
D  sizing constants                1,016     clean
                                  ------
   raw                            17,290     of which 8,703 is CLEAN
```

Rungs round, so the tiers below are computed from the sections rather than
from that sum.

### 8.1 What each tier buys, for choosing a stopping point

| take | `KERN_SIZE` | free heap | what still works |
|---|---:|---:|---|
| ~~today~~ | ~~78,336~~ | ~~50.0 KB~~ | superseded by the row below |
| **today (C5 + the D batch built)** | **75,776** | **52.5 KB** | measured on the floor machine, `tests/small128.py` |
| A | 76,800 | **51.5 KB** | everything, minus the sound layer (A2r is dead — §2.2) |
| A + D | 75,776 | **52.5 KB** | …with smaller tables and six windows |
| **every CLEAN row** (§10) | **69,632** | **58.5 KB** | …and no save-under, toast, progress, blanker, dock or built-in apps. **Nothing on the small floppies breaks** |
| + the blocked rows, callers swept first | 66,048 | **62.0 KB** | …and no icons, fullscreen or clipboard — each conditional on §10's work |
| + C1 deleted | 61,440 | **66.5 KB** | a read-only OS, and **five packages that think a save succeeded** |

**58.5 KB is the number to plan against**, and it is the one this document did
not have before: it is everything that can be taken without a package on the
small disks going quietly wrong. The rows between 58.5 and 62.0 are not
refused — they are *unpriced*, because their real cost includes a sweep of
their callers that nobody has done.

**And the last row is worse than "a read-only OS" makes it sound.** §6
establishes that `diskw.inc` cannot become a module, so C1 is delete-or-keep —
but `OSAPI_FILE_WRITE` is called by Artful, Cyclone, Frotz, `os88chart.inc`
and Paint **without testing CF**, so the failure mode is not a refusal the
user can see, it is a save that reports success. Deleting C1 means a caller
sweep first, exactly as B4 would have.

### 8.2 Where the last bytes would have to come from

The retired 70KB ask is now **3 KB** past the bottom row rather than 20, and
there are only three candidates for it: §4.1's Disk window trim (~2,300), the
damage-rect layer §3.1 refuses (1,700), or a further pass over `.ovlw` (§7).
**None of them is cheap, and the first is the only one that is not actively
unwise.**

**THE BUILT POSITION, for anything decided off the rows above.** W0–W2, A3, A4,
A2's probes, C3, the D batch and B5 are in, and they reach **50.0 KB measured
on a 128KB machine**. What remains unbuilt is group B, group C, A1, A2r and
what is left of group D.

**And the shape worth carrying out of this reading is not in any table here.**
The two programs the ask was about both moved out from under it: PAINT was
solved at the APPLICATION layer (SPEC.md 42.23), and the Task Manager's
`APP_SMALL` arm gave back 4,445 bytes an instance for **no kernel bytes at
all**. Every row in §2–§5 costs a feature on every machine that runs this
build; those cost none. **Before taking anything here, ask whether the package
could stop needing it instead.**

---

## 9. How these figures were taken

Sections and the ladder, from the project's own instrument:

```sh
make small
python3 tools/kernsize.py --modules --build build/smallk -DKERN_SMALL
```

Sub-file, from nasm's `[map all]` on a temporary copy of `kernel/kernel.asm`
assembled with `-DKERN_SMALL`, with each symbol's size taken as the distance to
the next symbol in its section. **The method reconciles exactly** — the summed
spans equal the section lengths for all four heap-bearing sections (`.text`
38,756, `.cold` 26,483, `.bss` 4,819, `.lowbss` 6,060), which is what makes a
per-feature figure quotable rather than indicative.

Group D and the two refusals in §5.1 were taken by **changing the constant and
re-assembling**, which is why three of those rows moved at this reading.

Two cautions for whoever takes the next reading. `tools/kernsize.py --modules`
reports **`kern_big`** unless `-DKERN_SMALL` is passed after the flags. And
**committing invalidates `build/kernel.bin` for the symbol reader**: the About
box's build number is the commit count, so `make` again before re-measuring.

### 9.1 The method was CHECKED against a real gate, and it is exact

Every previous revision of this document priced rows off the symbol map and
said so. It had never been checked that a symbol span is what a *gate* returns
— the difference being the refusing stubs a gate has to leave behind, and any
caller that has to be patched. So one row was built.

`gfx_line`'s family is contiguous in `vga12.inc` from `gfx_linit` to the byte
before `gfx_blit4`. Its span is **1,503 bytes**. Wrapping exactly that range in
`%ifdef KERN_BIG` and giving the four API slots `stc`/`ret` stubs measures:

```
                     .text     delta
baseline            38,756
gfx_line gated      37,261    -1,495   = 1,503 - 8 bytes of stubs
```

**Equal, not close.** Two things follow for the tables above. A row whose
symbols are contiguous is priced to the byte, and the only correction is 2
bytes per slot kept — less if they share one stub, which §6.2 notes they can.
And a row whose symbols are **interleaved with code that stays** — B1 is the
one that matters, hull 3,250 against own 2,451 — must be priced at its own
symbols, because the hull contains things a gate would have to keep. Every row
above is priced at own symbols for that reason; B4's is the one place the two
numbers agree, which is what made it the row worth building.

---

## 10. THE AUDIT THIS DOCUMENT NEVER DID: who CALLS the thing being gated

Every revision of this document, including the re-pricing in §9, measured the
**kernel** side of a row and stopped there. §6.2 even records the reassuring
half of it — the refusing stub is nearly free, because the API cell is in both
tables already and the bodies can share one `stc`/`ret`.

**That is only true when the caller tests CF, and on the small floppies it
mostly does not.**

The sweep is mechanical: take the slots a candidate owns, find every
`call OSAPI_*` in `apps/`, filter to what `make smallapps` actually writes
(`SMALLOMIT` drops Browser, FTPD, Telnet, The Wire, ModPlug, Tracker, Audio,
Tank, Skies), and look at whether a `jc`/`jnc` follows before anything clobbers
the flags — `push`/`pop`/`mov` do not, which matters, because the register
restore between the call and the test is this codebase's normal idiom.

| slot | small callers that do NOT test CF | consequence |
|---|---|---|
| `OSAPI_GFX_LINE` | `os88ui.inc`, Paint, Sheet, Missile, cc | **no ink** |
| `OSAPI_GFX_LINIT`/`_LSTEP`/`_LSTEPV` | Cyclone, Missile | **no ink** |
| `OSAPI_ICON_DRAW` / `_PEN` | `os88ui.inc`, Paint, Solitaire | **no icon** |
| `OSAPI_FSX_RUN` / `_CAPS` | Cyclone, Missile, Paint | believes it took the screen |
| `OSAPI_FILE_WRITE` | Artful, Cyclone, Frotz, `os88chart.inc`, Paint | **a save that reports success** |
| `OSAPI_CLIP_GET` | cc, Note Pad, Sheet, TexPad, Word | a paste of nothing, or of stale bytes |
| `OSAPI_CLIP_PUT` | Sheet, TexPad | a copy that did not happen |

**Three slots come out CLEAN, and the reason each is clean is worth keeping.**

- **`OSAPI_WM_SAVEU` (B1) is not a refusal slot at all.** It is a package
  *declaring* that its content does not change while it is not drawing
  (SPEC.md 11.96.1), and `wm_saveu` **preserves the flags deliberately** —
  *"Preserves the flags for wm_snap's reason: an entry proc calls this after
  wm_create and the carry riding in is its own return value."* So the twelve
  small callers that do not test CF are **correct**, not lucky: gated out, the
  slot becomes a no-op and every package still works. B1 costs the **1,026 ms
  raise** §3.2 argues about and nothing else, exactly as its row says.
- **`OSAPI_TOAST` (B5) and `OSAPI_SND_TONE` (A1) degrade to the thing being
  removed.** A toast that does not appear is what "no toast" means; silence is
  what "no sound" means. An untested CF there costs nothing the row was not
  already charging for.

### 10.1 B4 is the worked example, and it was recommended

The previous revision called `gfx_line` *"the best-value row in the document
and the one to take first"*, on the grounds that no kernel path calls it. Both
halves of that sentence are true and the conclusion is wrong, because the
family is published API that exists **for packages**:

- **Paint's freehand stroke is `OSAPI_GFX_LINE`** (`apps/paint/paint.asm:7520`,
  ungated in the `APP_SMALL` arm), and it is SPEC.md 42.8's whole point —
  *"on the field machine it is the difference between a pencil that follows the
  mouse and one that cannot"*. It does not test CF, so the pencil would not
  slow down, it would **stop leaving ink**. Paint is on the small SYSTEM disk
  *and* the small APPS disk.
- **The menu checkmark is two `OSAPI_GFX_LINE` calls** in `apps/os88ui.inc`,
  which about twenty packages include — Task Manager and Note Pad among them,
  both on the small disks. Every checked menu item loses its tick.
- Missile and Cyclone drive the resumable walker (`_LINIT`/`_LSTEP`/`_LSTEPV`)
  and both ship on small; only Tank, which also uses it, is in `SMALLOMIT`.

So the row is **0 bytes available**, and its 1,503 stays in the tables only as
the honest size of a body that cannot go.

### 10.2 What this changes about the method

§9.1 established that the *size* of a row is exact. §10 establishes that the
size was never the binding quantity:

> **A `kern_small` row is priced on the APPS disk, not in `kernsize`.** The
> kernel arithmetic says what a gate returns; the caller sweep says whether the
> gate may be built at all.

Two rules for the next reading, and they cost minutes rather than a rebuild:

1. **Before pricing a row, list the API slots it owns and grep `apps/` for
   them**, filtered by `SMALLOMIT`. A row with no published slot (B2, B6, B7,
   C5, C7 and every D row) is clean by construction and needs no sweep.
2. **A slot whose callers do not test CF cannot become a refusing stub**
   without a caller sweep landing first — and that sweep is package work in a
   different tree from the kernel change, which is why it belongs in the
   estimate rather than in the follow-up.

## 11. The resumable walker, moved into the APPS — priced and REFUSED

The proposal, and it is a good one: `gfx_linit`/`gfx_lstep`/`gfx_lstepv` are
driven by **games**, the shipped programs that monopolise the machine anyway,
so duplicate the walker into a shared app-side include the way `os88ui.inc`
already shares the widgets — **pay the RAM in the one program that is running
instead of in every kernel for ever.** It sidesteps §10 completely: an app that
carries its own walker does not call the slot, so there is no untested CF and
no caller sweep to land first.

**It is refused on measurement, and the arithmetic below is the whole of why.**
The shape is right and this document says so; what defeats it is that the slot
does not sell the thing the proposal would duplicate.

### 11.1 The size, and the customer list

Span `gfx_linit` → `gfx_ls_addr`, §9.1's method (the family is contiguous, so
this is exact):

| | `.text` | `.bss` | total |
|---|---|---|---|
| `kern_small` | **505** | 32 | **537** |
| `kern_big` | **609** | 32 | **641** |

`gfx_ls_addr` itself **stays**: `gfx_line`'s own `GFX_LFWALK` setup calls it,
and `gfx_line` cannot go (§10.1). The three API cells stay too — the table is
offset-addressed, so a removed slot keeps its cell pointing at the shared
`stc`/`ret`, which is §6.2's usual near-zero.

The customers are fewer than the grep suggests:

| | `kern_small` | `kern_big` |
|---|---|---|
| Missile Command, Cyclone | yes | yes |
| Tank (attract mode) | **no** — `SMALLOMIT_GAMES` | yes |
| `SAVER.DRV`'s web | **no** — `SMALLDRIVERS = $(KMODS)` | yes |

So on the floor machine the entire customer list is **two games**, which is
what makes the proposal look so strong.

### 11.2 Tank is not the precedent

`apps/tank/tkraster.inc` is not a walker Tank chose to embed. Its own header
says why it exists — there is *"NOT ONE kernel drawing slot among them, because
SPEC.md 53.7 makes every one of them illegal the moment `fsx_mode` returns"* —
and it writes the framebuffer directly (`add di, 80`), which is legal only for
a program that has taken the whole screen. Tank's **windowed** half,
`apps/tank/tkattr.inc`, calls `OSAPI_GFX_LINIT`/`_LSTEP` like everybody else.

A windowed package cannot copy `tkraster.inc`. The clip region, the save-under
and the display translation are all in the path it skips.

### 11.3 What the slot sells is the MARGINAL PIXEL, not the Bresenham

The arithmetic is about thirty bytes and free to duplicate — Missile already
computes with the block's own x and y (SPEC.md 48.14), and its erase already
depends on replaying its own walk exactly, which stays true if the walk is its
own. **The correctness half of the proposal is fine.**

What `gfx_lstep` sells is that it read-modify-writes the framebuffer inside one
staging, so the second and subsequent pixels of a step cost almost nothing.
SPEC.md 5.6.8 measured all three terms on the 5150:

| | |
|---|---|
| arrival, removed by the batch | 128.7 µs × (N−1) |
| block setup, which the batch does not remove | ~480 µs × N |
| **marginal pixel** | **~175 µs** × pixels |
| `OSAPI_GFX_PIXEL`, for comparison | **640.87 µs** |

An app-side walker has to plot through a published slot, and **the marginal
pixel is the one thing no published slot sells**:

- **`OSAPI_GFX_PIXEL`** — 640.87 µs, **3.66×** the walker's marginal pixel.
- **`OSAPI_GFX_SPANS`** is the primitive that *would* sell it, and it is
  unreachable from both directions: `stc`/`ret` with no body on `kern_small`
  (gfx_blit1's precedent, SPEC.md 5.4.2), and on `kern_big` it refuses outright
  when a clip region is armed — which **every windowed package arms before it
  draws**.
- **`gfx_fill` on a 1×1 rect** is worse than `GFX_PIXEL`, and at the angles
  these two draw a Bresenham's runs are one or two pixels.
- **`gfx_blit1`** would need the app to compose a band, and both programs
  *accumulate* — Cyclone erases nothing during a warp, Missile's erase replays
  the identical walk — so a rectangular blit would wipe what it is drawing on.

### 11.4 The bill, per frame, on the 5150

| | kernel walker | app-side + `GFX_PIXEL` |
|---|---|---|
| Missile, 8 live blocks × ~3 px | 128.7 + 480×8 + 175×24 = **8.2 ms** | 640.87 × 24 = **15.4 ms** |
| Missile's drain, `MC_DRNBUD` = 64 px over ~4 blocks | 128.7 + 480×4 + 175×64 = **13.2 ms** | 640.87 × 64 = **41.0 ms** |

The drain alone is **+27.8 ms a frame against a 54.9 ms tick**. That is not a
program that got slower; it is an effect that becomes a stall.

### 11.5 The row that would unlock it, and why it does not pay either

The blocker is one primitive, and it names itself: **give `gfx_spans` a
`kern_small` body and make it survive an armed clip.** It is the right shape
for a walk — a steep line is one interval per consecutive row, a shallow one is
`dy+1` rows with long intervals, which is exactly the record layout.

Measured on `kern_big`, the `gfx_spans` family is **320 bytes**. So `kern_small`
would spend 320 to save 537, before any of the clip work — and the clip work is
the part `gfx_spans` refuses on its own terms: *"an armed clip region and a
second display both want the run re-cut per fragment, which is the whole of
`gfx_clip_run` and `gfx_disp_run` again."*

**Net at best ~200 bytes on `kern_small`, for a rewrite of two shipped games
and a new kernel body on the build that has the least room for one.**

### 11.6 And the `kern_big`-only variant, which is the tempting one

`kern_big` already has the `gfx_spans` body, so its blocker is only the armed
clip — and a walk is a *good* fit for spans, three consecutive rows of one
interval being one call where the kernel walker charges a block setup. It is
entirely possible that an app-side walker over a clip-aware `gfx_spans` is both
**smaller and faster** on `kern_big`. That is not the objection.

The objection is that **the app cannot ship only that path.** The same binary
runs on both kernels — SPEC.md 11.102's rule, and `make smallapps` is a
different *build* of a package, never a different ABI — so a package that walks
app-side needs a `kern_small` fallback, and the fallback is §11.3's pixel loop
at 41 ms a frame. So the choice is:

- keep the kernel walker for `kern_small` and add an app-side one for
  `kern_big` — **537 bytes still resident on the build that needs them**, two
  code paths in each of two games, and a new clip-aware `gfx_spans`; or
- take the pixel loop everywhere — 641 bytes off `kern_big`, 537 off
  `kern_small`, and Missile's drain at 41 ms.

Neither is worth it. The walker stays. It is not duplicate code — it is the
only marginal pixel on the machine.

## 12. …and the WHOLE line machinery app-side — the better row, and what it is really blocked on

> **This section has been superseded by a plan of its own:
> [docs/plans/completed/GFX-EMBEDDABLE-PLAN.md](GFX-EMBEDDABLE-PLAN.md).** What is below
> is the reading that opened it and two of its findings have since been
> corrected there — `gfx_blit1` **does** honour the clip region (§2.3), so it is
> a legal windowed commit, and its `kern_small` body is **not this row's cost**
> (§2.4): nine shipped small-disk packages already call the slot and take a
> fallback, so that decision stands on its own. Read §12 for the arithmetic that
> made the case and the plan for the design.

§11 answered the walker alone. The larger proposal is to move **all** of it —
`gfx_line` included — into an embeddable library a package takes as much of as
it uses, on the grounds that the per-call floor is then removable and a
performance-critical caller gets the chance to optimise.

**Two halves of that are right, and one is measured right in this repository
already.** PERFORMANCE.md priced a candidate app-side rasteriser writing a 1bpp
mask at **24.6 µs a pixel** against `gfx_line`'s **31.6** with the arrival
removed — **1.29× faster** — because what it drops is *"everything `gfx_line`
does that a caller compositing its own figure does not need: clipping, the ink,
the dither table, the per-row `gfx_rowbase`."* The library is not a worse
rasteriser. It is a better one.

### 12.1 The size on the table

Span `gfx_linit` → `gfx_blit4`, §9.1's method:

| | `.text` | `.bss` | total |
|---|---|---|---|
| `kern_small` | **1,368** | 69 | **1,437** |
| `kern_big` | **2,511** | 80 | **2,591** |

3.6% of `kern_small`'s `.text`, 4.9% of `kern_big`'s. This is a bigger prize
than every row in §8 except B1.

### 12.2 `os88ui.inc` is NOT the obstacle it looks like

37 files across **25 packages** include `os88ui.inc`, which is where the
"everyone would have to embed it" objection comes from. It does not hold: the
include's only line use is the **menu checkmark**, and that is two *fixed* ±45°
strokes 4 and 5 pixels long (`apps/os88ui.inc:3986`, `:3993`). A glyph, or six
`OSAPI_GFX_PIXEL` calls, replaces it — no library.

The real general-line users on the small disks are **Paint, Sheet, Missile and
Cyclone**, plus any C package through `apps/cc/os88thunk.asm`. Four programs
and a thunk.

**Nor is the floppy binding**, which the older reading assumed: `smallapps360.img`
is **212 of 354 clusters** and `apps360.img` **317** — 142 and 37 clusters free.
Compression has moved that number since §8 was written.

### 12.3 What it IS blocked on: `kern_small` has no commit primitive

A windowed package has **no framebuffer**. The only published framebuffer
address on the machine is `FSI_SEG` in the fsx info block, and SPEC.md 53.7
makes every kernel drawing slot illegal the moment `fsx_mode` returns — the two
are mutually exclusive *by design*. `apps/tank/tkraster.inc` is what that
permission looks like when it is granted, and Tank pays for it by owning the
whole screen.

So an app-side rasteriser has to hand its result back to a slot, and on
`kern_small` **both batch slots are `stc`/`ret` with no body**:

- `gfx_spans` — `kernel/vga12.inc`, *"kern_small carries the slot and no body"*
- `gfx_blit1` — `kernel/kernel.asm:6542`, *"kern_small carries the SLOT and not
  the body… The body was measured on this build and refused"*

What is left is `OSAPI_GFX_PIXEL` at **640.87 µs** against `gfx_line`'s
**37.1 µs a pixel**. **17.3×.**

> **`kern_small` has no batch pixel primitive at all.** Removing `gfx_line`
> from that build does not move the work into the apps — it removes the
> capability from the machine.

### 12.4 The row that unlocks it is already priced, and it INVERTS a standing refusal

`gfx_blit1`'s lean body on `kern_small` is **+419 bytes**, built, measured and
refused (SPEC.md 5.4.2.5, docs/plans/completed/PAINT-1BPP-PLAN.md Option B), on
the owner's decision that *"the small build may stay slower"*.

That refusal was weighed against Paint's canvas alone. Against this row it
reads differently:

| | bytes |
|---|---:|
| `gfx_blit1` lean body on `kern_small` | **+419** |
| the line machinery it lets go | **−1,368** |
| its `.bss` with it | **−69** |
| **net** | **−1,018** |

…and the rasteriser gets *faster*, and Paint's 1bpp canvas gets its fast path
back on the build where it currently takes the 24× expansion fallback
(SPEC.md 42.23.4). **If it survives §12.5 it is the best-value row in this
document.** What is measured today is the rasterise half and the sizes; the
commit half is arithmetic, so it is a candidate and not a finding.

### 12.5 The four things to settle before building it

1. **The commit is OPAQUE.** `gfx_blit1` writes the band's ground as well as
   its ink. Paint (its own bitmap) and Sheet (a grid on fresh ground) are fine.
   **Cyclone accumulates during a warp and Missile's trails sit over terrain** —
   those two need a transparent commit or a read-modify-write, which
   `gfx_blit1` does not do, so they may have to keep a walker and that is §11
   again. **Price the row on Paint and Sheet; do not assume all four.**
2. **The commit is priced by BAND AREA, not by pixel count.** A 127×32 line's
   band is 508 bytes whether it holds 127 pixels or 4,064. A dense figure
   amortises it; one long thin line does not. Measure a real figure.
3. **Clip and ink come back.** The 24.6 µs is a rasteriser with none of them.
   An app that needs them pays them back, and `gfx_blit1` still refuses an x or
   a width off the byte grid (SPEC.md 5.4.2).
4. **`os88ui.inc`'s checkmark has to land first**, or 25 packages still reach a
   slot that is no longer there. It is a glyph, and it is a far smaller change
   than this one — it can go on its own.

### 12.6 On `kern_big` there is no enabling row at all

`gfx_blit1`'s body already exists there, and it is in `.cold`
(`kernel/kernel.asm:6537`, `call COLD_SEG:gbz_gfx_blit1`). So the commit
primitive is present, the 2,511 + 80 is available with nothing to build first,
and only §12.5's four questions stand between it and the row.
