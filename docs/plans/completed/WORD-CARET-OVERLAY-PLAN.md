# WORD-CARET-OVERLAY-PLAN — take the caret out of the row, and out of the signature

**Status: BUILT AND SHIPPED** (SPEC.md §27.17–§27.19). It was built, measured,
**reverted**, and then built again at a wider scope, and §8 is the record of
the first attempt — **read it before re-deriving any of this**, because its
central finding is what the second attempt was designed around.

That finding is one sentence: **the caret's DRAW was never the cost, the
layout WALK is.** Removing a row's lettering and leaving its walk in place
moved the clock by *nothing* — 99.7 ms before and after at separation 1 — and
that is why the first cut was reverted rather than tuned. What shipped takes
the walk away too: the row the caret left owes neither a draw nor a walk, and
the two callers that could not say so (a keystroke, and a click whose
departing row is off the view) are §27.19.9.

Four defects were found by building it, and every one of them was in code the
overlay only *exposed*: the dirty range gating the glyph store (§27.19.8), the
auto-scroll threshold treating the last visible line as outside the view
(§27.8.2.2), a visible row being signed where `wd_clickcm` compares unsigned,
and `[wd_ckok]` = 0 meaning two different things (§27.8.4). The measurements
that name them are in those sections rather than here.

**Original status line: PLAN. Nothing built.** The measurements in §1 are on the tree at
`ac15ae8f` + SPEC.md 27.4.10, taken on a cycle-accurate 5150 (MartyPC) with
`WELCOME.DOC`; every byte figure is an **estimate** unless it says measured.

The ask that produced it: *"removing [the caret] from the old spot should just
be visual. Can we save under the single character wherever we put the cursor,
making that redraw — if it's even still on screen — be nothing but a visual
flip?"*

The answer is **yes, and it needs no kernel call to bank with** — Word already
composes the row into a 1bpp band, so the pixels under the bar are in its own
RAM before the bar goes down (§2). That is the easy half. **The hard half is
that the caret is part of the row's SIGNATURE**, and that is what makes moving
it dirty a row at all — so the fold has to come out, and that is the work
(§3).

---

## 1. Where the remaining cost is

SPEC.md 27.4.7 through 27.4.10 took a click from "the whole view, twice" to
**two rows, whatever it crossed**. What is left is those two rows:

| click | rows walked | `wd_redraw` |
|---|---:|---:|
| same row | 1 | ~40 ms |
| any separation, 41-row window | 2 | 99–232 ms |
| any separation, 6-row window | 2 | 109–173 ms |

A row costs ~40 ms of layout and the spread above is row **content** — a
wrapped paragraph against `Ctrl-F<tab>Search`.

**The two rows are drawn for one reason each**: the row the caret left has to
lose its bar, and the row it arrived on has to gain one. Nothing else about
either row changed. So the ceiling for this work is **one row instead of two**,
plus whatever the overlay itself costs — call it ~40–50 ms flat against
today's 99–232.

That is ~2–3x, on top of the 6x already taken. It is worth having and it is
**not** a small change; §4 is the honest bill.

---

## 2. The bank is a BAND Word composes, not a framebuffer read

The obvious primitive is `OSAPI_GFX_SAVE`/`OSAPI_GFX_REST` (0x0508/0x0510,
SPEC.md 5.3) — published, in both builds, 1bpp twins and all. **It is the wrong
one**, and the owner named the better one: Word already composes every
proportional row into a 1bpp band and puts it up with one `OSAPI_GFX_BLIT1`
(`ty_band` / `ty_flush`, SPEC.md 6.3). So the pixels under the caret are
already in Word's own RAM at the moment the bar goes down — **bank the band's
byte column, and blit it back to erase.**

Four things fall out, and every one of them is a reason to prefer it:

- **No framebuffer read at all.** The bytes are the row Word *intended*, not
  whatever happened to be on the glass. A `gfx_save` under an overlapping
  window banks the overlapping window's pixels and then writes them back into
  Word's content — the bank can be poisoned. A composed band cannot.
- **16 bytes, not 64.** A band is 1bpp on every adapter, so the column is one
  byte a row and `TY_BROWS = 16` caps the rows: **16 bytes of Word's bss**,
  against `((x2/8)-(x1/8)+1) × H × [vid_planes]` = 1 × 16 × **4** on VGA.
  Adapter-independent as a bonus, where `gfx_save`'s buffer is not.
- **No new correctness argument.** `gfx_blit1` is **not** on §11.3's clipped
  set — that list is seven entries (`gfx_fill`, `gfx_fill_gray`,
  `gfx_fill_pat`, `gfx_xor_fill`, `gfx_xor_rect`, `font_char`,
  `icon_draw16`) — but *neither is `gfx_restore`* (§5.8 says so in as many
  words). So both are unclipped, and Word's whole row path is **already** an
  unclipped `gfx_blit1`. Whatever makes that correct makes the caret band
  correct: `OSAPI_WM_OBSCURED` at the four background-draw sites, plus the
  invariant stated at `word.asm:799` — *everything that draws goes through
  `wd_redraw` or `wd_paint`*. The overlay adds no site that is not already
  inside one of those two.
- **The alignment is free.** `gfx_blit1` wants `x` a multiple of 8 and the
  width a multiple of 8 (SPEC.md 5.4.2). The caret's byte column *is* a
  multiple of 8 — it is a framebuffer byte — so the band is 8 px wide at a
  legal x with no rounding rule to obey.

### 2.1 Where the column comes from

At `wd_rflush`'s `.caret`, the row's band is still composed in `ty_band` and
the bar has not been drawn yet — `wd_carets` deliberately banks `[wd_rcx]`
rather than drawing, *because the row's `font_run` has not happened yet and
would paint over it*. That comment is now load-bearing in a second way: it is
exactly the window in which the clean column exists.

So the bank is a copy of `[wd_gh]` bytes out of the band at column
`([wd_rcx] − [wd_bx0]) >> 3`, stepping `TY_STRIDE` a row — a loop of at most
16 `mov`/`add` pairs, well under the ~40 ms a row this is bought against.

#### 2.2 And the CELL face is out of scope, correctly

A non-proportional face draws through `font_run` and composes no band, so
there is nothing to copy. That is not a gap: the cell path is the 8-px kernel
cell and is not the slow case — the owner's report is Courier and Pica, both
proportional. The overlay is armed on `[wd_pxon]` and the cell path keeps
today's mechanism, which halves the work and leaves the fast path alone.

## 3. The hard half: the caret is in the signature

`wd_ask` folds the caret's pen into the row being accumulated:

```asm
    mov ax, di
    xor ax, 0x5A5A                  ; the xor keeps a column from folding the
    call wd_fold                    ; way a character code would
```

and SPEC.md's own note under the signature header says why: *"The caret is part
of the signature and has to be. Moving it off a row has to dirty that row, or
it stays drawn there."* That sentence is exactly true of a caret drawn **by the
row**, and it is what an overlay retires.

### 3.1 Why the fold cannot simply be patched out

`wd_append` already patches a signature surgically rather than re-walking:

```asm
    mov dx, [bx+wd_sig]
    sub dx, ax                      ; undo the caret that was folded at C
    ...
    rol dx, 1
```

That works because the caret it is unpicking was folded **last** — it is a
caret at the end of a row, and the fold is rotate-then-add, so the tail is
reconstructible. A caret in the **middle** of a row has every later glyph's
rotate applied on top of it, so there is no cheap inverse. Setting the row's
signature to a "definitely stale" sentinel is the alternative and it is worse
than the disease: it forces a full row redraw the next time that row is walked,
which is the cost this plan is removing.

**So the fold has to go, not be undone.** That is the change, and everything in
§4 follows from it.

### 3.2 The sites

Eight, and they are all in two routines plus the redraw's fast path:

| site | what it does | after |
|---|---|---|
| `wd_ask`, the `xor 0x5A5A` fold | puts the caret in the signature | **deleted** |
| `wd_carets` | banks `[wd_rcx]` = the caret's pen for this row | becomes the overlay's *position* record, not a draw order |
| `wd_rflush` `.caret` | `SET_COLOR` + `GFX_VLINE` on top of the run | **deleted** — the overlay draws it |
| `wd_rflush` `.dfold`, `[wd_prcc]`/`[wd_fcc]` | makes the caret's two cells count as changed in the **diff** | **deleted** |
| `wd_rflush` `[wd_prcc]` store | the cached row's caret column | **deleted** |
| `wd_append`'s three signature terms | the end-of-note typed character | **simplified** — the caret terms come out |
| `wd_append`'s `[wd_prcc]` store | ditto | deleted |
| `[wd_fcc]`, `[wd_rcx]`, `[wd_prcc]` | three words of bss | one survives, two go |

Note the second column of row four: `[wd_prcc]`/`[wd_fcc]` are folded into the
**delta cache** as well as the signature, so that a row whose characters did
not move still re-letters the two cells the bar moved between. With an overlay
neither cell needs lettering at all, which is the same deletion seen from the
other end.

---

## 4. The bill

Two new routines and a discipline. Every number is an **estimate** against a
measured comparable in this tree (`wd_clickcm` is 79 bytes measured,
`wd_rowofi` 102, `wd_append`'s signature patch ~120).

| piece | est. `.text` | est. `.bss` |
|---|---:|---:|
| `wd_curbank` — copy `[wd_gh]` bytes out of the band at the caret's column | ~55 | — |
| `wd_curshow` — `SET_COLOR` + `GFX_VLINE` at the banked position | ~40 | — |
| `wd_curhide` — one `OSAPI_GFX_BLIT1` of the banked column, guarded by the shown flag | ~55 | — |
| the banked column | — | **16** |
| `[wd_curshown]`, `[wd_cursx]`, `[wd_cursy]`, `[wd_curbanked]` | — | 7 |
| the cell-face arm (`[wd_pxon]` clear keeps today's mechanism) | ~25 | 1 |
| deletions from `wd_ask`, `wd_rflush`, `wd_append` | **−130** | **−6** |
| the hide/show calls at each draw site (§4.1) | ~60 | — |
| **net** | **~+105** | **~+18** |

All of it is Word's `.o88` image and its bss — **nothing resident**, nothing in
`KERN_BUDGET`, no kernel byte at all. That is the one genuinely comfortable
thing about this plan: SPEC.md 5.3 already spent the kernel side.

### 4.1 The discipline is the risk, not the byte count

An overlay has to come down before anything draws under it and go back up
after. Word's draw sites, from a sweep of `OSAPI_GFX_*`/`OSAPI_FONT_*` and
`ty_flush` callers inside the text band:

1. **`wd_redraw`** — hide at the top, show at the end. This is most of it, and
   it is one pair. It is also where Word's existing occlusion argument lives,
   which is why the overlay needs no new one (§2).
2. **`wd_scrollpaint`** — the blit moves pixels the saved column describes, so
   hide *before* the `GFX_SCROLL` and show after. Getting this wrong leaves a
   bar smeared at the old offset, which is the classic save-under failure.
3. **`wd_rflush`'s `[wd_selonly]` XOR arm** — `GFX_XOR_FILL` over a span that
   may contain the caret's column. A selection *replaces* the caret
   (`wd_carets` returns early while one is up), so the correct action is that
   the overlay is **down for the whole of a selection** — which is a state
   rule, not a call pair, and is the one place the design is simpler than
   today's.
4. **`wd_brkdraw`** — the visual break paints the tail; SPEC.md 27.3 already
   says the break owns the screen while it is up, so the overlay is down there
   too.
5. **`wd_paint` / `.fullpaint`** — the white fill takes the caret with it, so
   the banked column is void afterwards: the show must re-bank rather than
   restore.

Item 5 is the shape of every bug this will have: **the banked pixels have a
lifetime, and every repaint ends it.** `[wd_curbanked]` is that fact, and the
rule is that anything which invalidates the glass under the caret clears it —
a geometry change, a face change, a scroll that refused its blit, a
`W_PAINT` from the kernel. Those are the same events that already clear
`[wd_sigok]`, so the clear belongs beside it and not at a new list of sites.

---

## 5. What it buys, stated as a prediction

- A caret move dirties **no row**. Neither the row it left nor the row it
  arrived on needs lettering, because the only thing that changed is a column
  of pixels the overlay owns.
- The walk that remains is **one row**, to find the new caret's pen — and that
  is irreducible without caching per-cell pens for the whole view, which
  `[wd_px]` does only for the row being composed.
- **Estimate: ~40–50 ms a click, against 99–232 today.** The erase is one
  `OSAPI_GFX_BLIT1` of an 8×16 band — a far call plus 16 bytes — and the bank
  is ≤16 `mov`/`add` pairs out of a band already in hand. Single-digit
  milliseconds, and no framebuffer read anywhere in it.
- A **blink** becomes possible for the first time, at near-zero cost — two far
  calls on a timer rather than a row redraw. Not proposed here, but it is the
  reason this shape is worth more than the milliseconds: it is what the caret
  wants to be.

---

## 6. What would kill it

1. **Item 5 of §4.1 in practice.** A banked column whose lifetime is wrong is a
   bar left standing in the wrong place — a *visible* defect, where everything
   this plan replaces was merely slow. SPEC.md 7.1.4.3's history is the warning:
   this project has shipped a lit-but-frozen pointer and reverted it.
2. **1bpp.** The band is 1bpp everywhere, so the copy is identical on every
   adapter — but a 1 px black bar on a Hercules is the case where a *wrong*
   restore is most visible against white paper. The gate has to run on both
   1bpp adapters, not just VGA.
3. **The band's lifetime inside one redraw.** `ty_band` is ONE band reused per
   row (SPEC.md 6.3), so the column must be copied out at `.caret` and not
   referenced later. Holding a pointer into the band instead of a copy is the
   mistake this row exists to name; 16 bytes is cheaper than being clever.

---

## 7. Sequencing

Three waves, each independently revertible, and wave 1 answers §6.1 before any
of the hard work:

- **W1 — bank and prove the bytes.** `wd_curbank` at `.caret`, and nothing
  else: keep drawing the bar exactly as today, and assert in the guest that
  blitting the banked column back where the bar is produces the same pixels as
  a full row redraw. That is the whole risk of the scheme, isolated, with no
  behaviour change to review.
- **W2 — the overlay, with the caret still in the signature.** `wd_curshow` /
  `wd_curhide` and the discipline of §4.1, with `wd_rflush` still drawing the
  bar. The screen must be **pixel-identical** to today at every step — this
  wave changes nothing a user can see, and that is exactly what makes it
  testable.
- **W3 — the fold comes out.** §3.2's eight sites, and only now does the row
  count drop to one. `tests/wdclick.py` leg A becomes `== 1`.

The gate is the one that already exists: `tests/wdclick.py` counts rows walked
and puts four click shapes against a full repaint, and `soak -k 'wd*'` is the
eight rows around it. W3 needs one leg added — the caret's **own** pixels
against a full repaint, on both 1bpp adapters, because every other leg in that
file would pass with the bar missing entirely.


---

## 8. What the three waves found, and why they were reverted

All three were built, gated green, and taken out again. Every claim below is
measured on a cycle-accurate 5150 with `WELCOME.DOC`; the code is in the
session's stash and in the reverted commits `2ddd8719` and `c674c140`.

### 8.1 The mechanism works. That was never in doubt after W1.

`tests/wdclick.py` leg G asserted the bank against the glass with no guest-side
hook: the eight pixels the bar stands in equal the banked byte with the caret's
own bit cleared, **0 of 12 rows differing**. The band-bank is sound, it is 16
bytes, it is adapter-independent, and it needs no `gfx_save` and no clip test.

W2's leg H put the overlay alone against `wd_rflush` at the same caret index —
**0 differing pixels**. Hide and show are correct.

### 8.2 …and the CELL face needs no save-under either, which was a real find

§2.2 assumed the kernel's 8×8 would have to keep the old mechanism, leaving the
win to chosen faces only — and `Pica` *is* the cell face. It does not:
**`wd_append` has erased a caret by redrawing one cell opaquely since the day
it was written**, so the bank there is the *character*, one byte, and the erase
is one `font_run`. That arm was built and gated green (leg G, `[wd_cbkind]`
= 2).

### 8.3 THE DRAW WAS NOT THE COST. This is the finding.

W3 took the caret out of the row signature, and the gate proved it: a caret
move dirtied rows **2..2** where it had dirtied **1..2**. One row lettered
instead of two.

**The clock did not move.** Rows walked per click, and `wd_redraw` for it:

| click | before W3 | after W3 |
|---|---|---|
| separation 1 | 2 rows, 99.7 ms | 2 rows, 99.7 ms |
| separation 4 | 2 rows, 185.0 ms | 2 rows, 186.6 ms |
| separation 10 | 2 rows, 232.2 ms | 2 rows, 232.6 ms |
| separation 20 | 2 rows, 140.5 ms | 2 rows, 142.1 ms |

Because **`wd_rflush` only decides whether to BLIT** — the row is laid out by
the walk either way, and the walk is the ~40 ms. §1's arithmetic priced "one
row instead of two" as 2x and it is not: it is the blit, which is noise.

The 2x would need the departure row's **walk** dropped as well, so the
`wd_clickcm` pair walks one row instead of two. That is the step that trusts
the overlay's erase completely — and if the erase ever declines, the stray bar
is on the glass until the next redraw. §6's first risk, reached.

### 8.4 And it breaks two shipped optimisations

`soak -k 'wd*'` went 8/8 → 6/8:

- **`wdcaret` leg D** — a caret move that SCROLLS: **198 differing pixels**.
  The bank's lifetime across a scroll is not covered by `wd_curvoid`'s five
  sites, which is §6's second risk arriving exactly where it was predicted.
- **`wdenter` leg F** — the Enter push (§27.4.5) stops firing, `[wd_nlrow]`
  staying `0xFFFF`. Its reconvergence test reads row signatures, and the caret
  was *in* them; taking it out changed what "this row did not move" means.

Neither is unfixable. Both are more work than the whole plan was costed at, for
a win of 0 ms until §8.3's further step is also taken.

### 8.5 Two rules the waves paid for, worth more than the code

- **An overlay's two halves run at different times, so neither may read
  anything that moves in between.** The hide first compared the bank against
  `[wd_curx]`, which `wd_vmove`'s measure walk has *already* moved before
  `wd_redraw` is reached — so the test failed on every caret move and every
  erase silently fell back. The position, the row and the bank must all be
  recorded where the bar is *drawn*. The same asymmetry bit twice: the hide was
  unconditional while the show was gated on `[wd_curseen]`, a fact about *this
  walk*, so an idle reconcile erased the bar and declined to redraw it — 8
  differing pixels, one cell-face bar.
- **A reference that moves is not a reference.** Leg H first compared an
  overlay-only screen against a page round-trip — a different *screen* — and
  read 411 differing pixels three scanlines above the caret's row, which was
  text and had nothing to do with the overlay. The sound reference is the other
  *drawer* at the same caret index.

### 8.6 What to do instead, if this is picked up again

The cost is the **walk**, so that is where to look. A caret move lays out two
rows to move a bar; the floor is one, and getting there means trusting an erase
the glass cannot check. Before writing any of it again, price what that
actually buys: on the numbers above it is ~100 ms → ~70 ms at separation 1 and
~140 ms → ~100 ms at 20 — real, but a fraction of the 6x §27.4.7–§27.4.10
already took, and paid for with a class of defect this project has shipped and
reverted before (§7.1.4.3).
