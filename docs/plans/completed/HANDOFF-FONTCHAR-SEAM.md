# Handoff — a straddled cell is dropped, so unaligned text loses one character

> ## BUILT. **SPEC.md §39.14.11 is the contract**; `make NOSEAMCUT=1` is the
> A/B and `tests/dispseam.py` (soak) is the gate. The owner's call in §3 was
> taken and it was *build it*: **0 differing pixels straddling against 18 lost,
> on both seam orientations**, and eight identical framebuffer hashes on the
> one-card machines. The cost was **299 bytes of `.text`, 1 of `.bss` and none
> of `.lowbss`** — the two 8-byte scratch cells came out of the alignment pad
> `sch_stacks` already carried.
>
> **This file is kept for its DIAGNOSIS, which is what was hard.** §1, §2 and
> §4 below are exactly right and are the reason the fix took an afternoon
> rather than a week. §5 and §6 are what changed in contact with the machine
> and are corrected in place; §7's evidence is now in SPEC.md §39.14.11.

**Reported as:** *"on an extended desktop, if you drag a window into a straddle,
unaligned text like the title bar will lose a character."*

---

## 1. What is happening

`kernel/font.inc`, `font_char`, lines 228–237:

```
font_char:
    ...
    GFXDENTERCD                 ; the cell goes WHOLE onto the display holding
                                ; its top-left corner (SPEC.md 39.14.2), and
                                ; the two edge tests below are that display's
    cmp al, FONT_FIRST
    jb .done
    cmp al, FONT_LAST
    ja .done
    cmp cx, [vid_cwm8]          ; unsigned: negative x fails this too
    ja .done                    ; <-- HERE
    cmp dx, [vid_chm8]
    ja .done
```

`GFXDENTERCD` makes current the display containing the cell's **top-left
corner** and translates the coordinates into it. `[vid_cwm8]` is that display's
width less 8 — it is the **last word of the per-display context run**
(`kernel/viddet.inc:126`, swapped by `vid_ctx`, and `kernel/vidsel.inc` asserts
its position). So a cell whose top-left is on display A but whose right-hand
columns fall past A's right edge fails that compare and takes `.done`.

**`.done` draws nothing.** The cell is not clipped to A and it is not re-issued
on B. It is dropped, whole.

## 2. Why it is exactly one character, and only when the text is unaligned

The seam is at the **primary display's own width** — 640 on a CGA/VGA primary,
720 on a Hercules. Both are multiples of 8. Cells are 8 pixels wide and a run
advances 8 per cell, so every cell of a run sits at `x₀ + 8n`:

* **`x₀` aligned** (`x₀ & 7 == 0`) — every cell boundary is a multiple of 8, the
  seam falls exactly *between* two cells, nothing straddles, **nothing is lost**.
* **`x₀` unaligned** — every cell boundary is at `x₀ mod 8 ≠ 0`, so **exactly one
  cell straddles the seam** and **exactly one character disappears.**

That is the whole report, and it is also why it has stayed rare: `wm_snap` is on
by default and most applications have been converted to draw text at aligned x,
so unaligned runs are now the exception.

## 3. Why the code is like this — read before "fixing" it

**This is a decision that was taken deliberately, and then partly reversed. Do
not re-take it from scratch.**

* **SPEC.md §39.14.2** is the rule: `font_char`, `font_run`, `ico_core`,
  `gfx_scroll`, `gfx_save`/`gfx_restore` and `gfx_line` cannot be split into rect
  fragments, so each takes `GFXDENTER`/`GFXDLEAVE` at its public entry and *"a
  shape that straddles the seam is therefore drawn on one display and cut at its
  edge"*. Its justification is explicit: *"on two physically separate monitors
  with a bezel between them there is nothing to see."*
* **SPEC.md §39.14.6 already reversed that for the RUN**, after the field found
  it: *"A RUN IS NOT A SHAPE … a straddled Note Pad showed exactly that in the
  field: typing landed on one card only, and because SPEC.md §27.2 makes the
  run's own padding the ERASE, backspace did not erase on the other."* A run that
  does not fit one display now goes **per cell, untranslated**, and each
  `font_char` picks its own display.
* **The CELL was left whole-shape.** So §39.14.6 fixed the run and left the one
  cell the seam crosses.

**So the change proposed here is the same concession one level down**, and the
argument against it is §39.14.2's own: a bezel means there is nothing to see. That
argument is strong for a rect and weak for a letter — a missing rect edge reads
as a bezel, a missing letter reads as a bug. **Whether it is worth the bytes is
the owner's call, not the implementer's**, and this file exists so that call is
made against the real cause.

## 4. What it is NOT

* **It is not a speed problem, and no glyph cache fixes it.** A per-phase
  pre-shifted glyph table (docs/plans/MONO-RECLAIM-PLAN.md §7) makes an unaligned cell
  *cheaper*. This cell is not drawn slowly — it is not drawn at all, and the
  `ja .done` is above everything a cache would touch.
* **`TITLESNAP=1` and `wm_snap` are not fixes.** They move the text so the case
  does not arise (docs/plans/completed/TEXT-PLAN.md §6.1). They are why this is rare, and they
  are the reason a fix can be judged calmly rather than urgently.
* **It is not `disptitle.py`'s bug.** That one is SPEC.md §5.4.2.4 — a straddling
  composed title bar drawn with one polarity for both displays, also reported
  from the field. Different mechanism, same window, and the two will be confused
  if the next reader is not told they are different. `disptitle.py` builds
  `make BAND=1` itself because the composer is a knob; **this defect is on the
  default kernel's fifteen-call bar as well**, because it is in `font_char`.

## 5. Reproducing it

> **CORRECTION, and it is the one thing in this file that was wrong.**
> *"MartyPC cannot host this: it models one adapter"* is **false**, and acting
> on it would have sent this fix to an emulator with no debugger for no reason.
> MartyPC has had two-card machines all along and a dozen `disp*.py` rows drive
> them: **`os8088_5150_both_gla`** is a CGA primary beside a Hercules (seam at
> 640) and **`os8088_5150_both_gla_mono`** the same pair the other way up (seam
> at 720), which is §7.1's "both ways round" in two machine names.
> `tests/dispstrad.py` is the template — launch, `dispcp.set_mode(…, "right")`,
> read `vid_ctx`. 86Box's `xt-multimon` is still the two-card *86Box* machine
> and the rest of this section stands; it is simply not the only one, and it is
> the one without a debugger.

**The 86Box machine is `vm/xt-multimon`** — the two-card XT, a CGA and a
Hercules with a monitor window each. It boots Single; **Control Panel →
Display → Desktop is what extends it** (SPEC.md §39.19.1).

Then:

1. Extend the desktop.
2. Open any window and give it a title long enough to reach the seam.
3. Drag it so the title bar crosses the seam. **Nothing special is needed to get
   an unaligned pen** — a window title is CENTRED on the exact pixel by default,
   so most captions land at an `x` that is not a multiple of 8 on their own.
4. One character is missing where the seam falls.

**`TITLESNAP=1` is the cleanest A/B in the tree for this**, and it is already a
knob: it centres the caption on the nearest 8px **cell** instead of the exact
pixel (docs/plans/completed/TEXT-PLAN.md §6.1), so the same build, same window and same drag
loses a character without it and none with it. That is one knob between a
reproduction and a control, and it is also the proof that the cause is the pen's
alignment and not the drag, the window or the composer.

Note what `wm_snap` does and does not cover: SPEC.md §11.94 snaps a window's
**content origin** to a multiple of 8, which is why converted applications draw
*their* text aligned. It says nothing about the title bar's caption, which the
window manager centres itself — so the chrome is exactly where this defect
survives.

**A cheaper reproduction that needs no drag** is a package that calls
`OSAPI_FONT_RUN` at an odd x spanning the seam; `tests/` already has the shape
in `tests/dispcells.py`'s breakpoint pump, which services `font_run_x` and
attributes calls by return address.

## 6. The fix, and what it costs

The precedent is **in the same routine, eight lines below the defect**. For the
**clip region** `font_char` already degrades instead of refusing:

```
    cmp word [wm_clip_n], 0     ; ...and against the clip region, which
    je .noclip                  ; answers in ROWS (SPEC.md 11.3.2): a cell an
    ...                         ; edge crosses horizontally draws the rows on
                                ; our side of it rather than nothing at all,
                                ; which is what left a half-covered Timer
                                ; frozen at the second it was covered
```

The display seam is the one edge that still drops the whole cell.

**The shape of the fix:** issue the straddling cell **twice, once per display**,
each pass writing only the columns on its own side. SPEC.md §39.14.7.2 is the
precedent for the cut itself — a straddling `gfx_blit4` is cut at the seam and
the routine run once per half, and there is exactly **one** seam because
displays are edge to edge.

**Two things make the text case easier than the blit case:**

* The seam is a **multiple of 8**, so each half is a sub-byte masked write. There
  is no nibble-phase problem — §39.14.7.2 has to refuse an odd cut and this
  cannot produce one.
* `font_char` **already has a masked path**: the one it takes on a planar target
  or at an unaligned x. The pass exists; only the column mask is new.

> **What was actually built is one step simpler than this, and it is worth
> saying where.** *"Only the column mask is new"* turned out to be *nothing is
> new*: neither renderer gained an instruction. Both already compute a
> first-byte and a second-byte mask with `shr ax, cl` and already skip an empty
> one, so masking the **glyph** — into two 8-byte scratch cells beside
> `font_zero` — makes each half's unwanted byte come out zero on its own. The
> seam being a multiple of 8 is what guarantees it, and a width that is not one
> is refused (SPEC.md §39.14.11). Each half is then an ordinary `font_char`
> call at ordinary virtual coordinates, addressed by a reserved character code
> so that even the glyph lookup is the arithmetic already in the body.

**Two things to get right:**

* **`[gfx_dnest]`.** `GFXDENTERCD` raises a nesting count and every hook under it
  is a compare and a branch (SPEC.md §39.14.2). A second enter for the second
  half must not translate twice. Read `GFXDISP`/`gfx_disp_run` in
  `kernel/vga12.inc` before writing the second pass — `gfx_blit4`'s cut is the
  worked example of getting this right. *(It was dropped to 0 around the two
  recursive calls, so each half takes its own enter; and §39.14.3 leaves the
  FAR display current, so `[vid_cur]` has to be banked and re-activated before
  the near half is drawn.)*
* **The transparent-text ratchet.** `font_char` is one of SPEC.md §6.6's closed
  list of transparent call sites and `tests/textsites.txt` is the ratchet: the
  count can only go down. A fix that adds a *call site* has to be registered with
  a reason; a fix inside the existing body does not.

**Bound the cost before building it.** At most one cell per run straddles, and
only while a window is dragged across a seam — which §39.14.2 itself calls a
transient state a user creates rather than a configuration anything runs in. The
per-call cost must be **zero when `[vid_ndisp] == 1`**, which is every machine
but one: the test is already there in the entry's `GFXDENTERCD`, so the second
pass belongs behind the compare that already exists rather than in front of it.

## 7. Evidence owed

1. **The two-card XT, both ways round.** A CGA primary (seam at 640) and a
   Hercules primary (seam at 720), because the seam moves with the primary and
   the arithmetic in §2 depends on it.
2. **The aligned control.** The same drag at an aligned x must be
   **byte-identical before and after** — this fix must not touch the common path.
   PERFORMANCE.md Part 4's rule applies: `settle` before each grab, and diff the
   old kernel against *itself* first, because a capture taken mid-repaint
   reported 31 differing pixels on a build that had not changed.
3. **A single-display machine, unchanged.** `[vid_ndisp] == 1` is every ordinary
   machine; `gfxbench`'s `FONT_CHAR one cell` and `FONT_RUN 10 aligned` rows must
   not move.
4. **A gate, on the soak tier, beside `disptitle.py`** — registered in
   `tests/suite.py` (`make test-soak`). `dispstrad.py` is the model for the
   assertion style: *"the assertion is arithmetic and not a screenshot"*, which
   applies here too — count the cells that reached the glass, do not photograph a
   title bar and squint at it.

## 8. Files

| file | what |
|---|---|
| `kernel/font.inc:228–237` | `font_char`'s enter and the `ja .done` that is the defect |
| `kernel/font.inc:~740–790` | `font_run`'s §39.14.6 per-cell split — the precedent that fixed the run |
| `kernel/viddet.inc:126` | `vid_cwm8`, and that it is the last word of the per-display run |
| `kernel/vidsel.inc:~1229` | `VID_CTX_*` and the assertion that pins that layout |
| `kernel/vga12.inc` | `GFXDISP` / `gfx_disp_run` / `gfx_blit4`'s §39.14.7.2 cut |
| SPEC.md §39.14.2, §39.14.6, §39.14.7.2, §11.3.2, §6.6 | the contract |
| `tests/disptitle.py`, `tests/dispstrad.py`, `tests/dispcells.py` | the neighbouring gates |
| docs/plans/MONO-RECLAIM-PLAN.md §6 | where this was found, and why the glyph cache is not the answer |
