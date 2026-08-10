# A kernel toast API — the plan

Five places in this tree draw a transient one-line message and no two of them
agree about where it goes, how long it lives, what it looks like or who erases
it. This is the plan for one kernel routine they can share.

**BUILT. This document is kept as the reasoning, and SPEC.md §60 is the
contract.** Every decision §8 left to the owner was taken the way this
recommended — the menu bar, the inverse strip, three seconds — with one
addition the plan did not foresee and §60.4 records: a message put up *before*
a long operation has to reach the glass before the machine goes quiet, so
`toast_show` draws on the spot when the caller provably holds the gfx lock.
Paint's `Encoding...` is the case that forced it.

What it cost and what it bought is in §6 below, replacing the estimate that
was there.

Everything marked *measured* was read out of the running tree; everything
marked *estimate* is arithmetic against PERFORMANCE.md.

---

## 1. What is actually there today

Five implementations, and the first useful result of the survey is that **only
two of them are toasts.**

| | where | lifetime | erased by | look |
|---|---|---|---|---|
| Note Pad `np_toast` | top-**right** of content, clamped to the content left on a narrow window | until the next keystroke | a **full content repaint** forced by `np_sigsame` | white fill + black frame + `OSAPI_FONT_STR` |
| Paint `pt_msg_show` | top-**left** of the canvas, `(2,2)`..`(w+4,15)` | until the next message, or a repaint | `pt_msg_hide`: a blit of the canvas rows it covered | white fill + black frame + `OSAPI_FONT_STR` |
| Tracker `tui_msg_draw` | its own status **line** | until replaced | the line is rewritten | plain text in the app's own frame |
| ModPlug | the skin's green **LCD** status line | until replaced | the LCD line is recomposed | part of the skin |
| Missile `mc_draw_msg` | centred **banner** in the sky | while the game state says so | the content fill | one opaque centred `font_run` |

**Tracker's and ModPlug's are status lines, not toasts.** A status line says
what is true *now* and is permanent furniture in the app's own frame; it has no
lifetime because it is never *finished*. **Missile's is a banner** — part of a
game's presentation, drawn on a fullscreen exclusive surface where there is no
menu bar and no window manager to borrow anything from. None of the three
should move, and saying so is worth as much as the API is: "these five look
alike" is the observation, "two of them are the same thing" is the finding.

So the migration set is **Note Pad and Paint**, plus the kernel itself, which
today has `ui_note` (a *window*, SPEC.md §54.4.1) and nothing lighter than a
window.

### 1.1 The line between a toast and `ui_note`

`ui_note` is the kernel's one-line notice **window**: it has a title bar, a
close box, and it stays until the user dismisses it. It exists for a **failure
the user has to acknowledge** — the program for a document is not on the disk,
the Task Manager could not be loaded.

A toast is the other half: **a statement about an operation that just
finished**, usually a success, that nobody needs to acknowledge. `Saved
NOTES.TXT`. `Opened SUNSET.BMP`. `Copied 3 files`. The user's framing settles
its lifetime — *"the toast currently stays until an action is taken, but this
is not optimal"* — so it self-expires.

The two do not merge. A notice you can miss is not a notice; a toast you have
to dismiss is a dialog.

---

## 2. The bug this removes, and it is a measured one

`tools/notepad/pixcheck.py` compares Note Pad's incrementally-drawn content
against a forced full repaint of the same state and reports **227 differing
pixels on row 0, at the toast's rectangle, with `[np_msg]` = 0 and unchanged
over 720 frames.** That was the last unexplained residue of the latency round
and the survey explains it exactly:

`np_toast` **clears `[np_msg]` as it draws** (notepad.asm ~3838), because a
toast belongs to the operation that raised it and `np_toast` is reached from
`np_paint` — so every later repaint was putting it back, and dragging the
window re-showed `Loaded README.TXT`, which reads as the file being loaded
again and was reported as exactly that. Clearing at draw time fixed *that* and
created *this*: the toast is on the glass, `[np_msg]` is 0, and a full repaint
does not put it back. **The incremental path and the full path legitimately
disagree**, and the only thing that reconciles them is `np_sigsame`'s fifth
test refusing the fast path on the next keystroke and repainting the whole
content.

That is the cost, priced against PERFORMANCE.md: a full `np_paint` on a 16×29
window is ~20 rows of lettering at ~1 ms per 8x8 cell, so **the first keystroke
after every save and every load pays a full-content repaint** — tens of
milliseconds on the field machine, for a message the user has already read.
Plus the two shadow words (`np_smsg`, `np_smsgn`) and the generation counter
that exists only because "Saved X" and "Loaded X" are composed into the same
buffer.

Paint's version costs the artwork: the toast sits at `(2,2)` **on the
picture**, and hiding it is a canvas blit.

A kernel toast that is not in anybody's content removes both, and removes the
whole *class* — an app cannot have an incremental-vs-full divergence in a
rectangle it does not draw.

---

## 3. Where it goes

Three candidates. The recommendation is the third and the reasoning is what
matters, because the first two are what a reader would reach for.

### 3.1 Content overlay with a save-under — **rejected**

Draw over the owning window's content, bank what was there, restore on expiry.

**Wrong, and not marginally.** A save-under is only valid if nothing else drew
in that rectangle in between. `menu_drop`'s save-under is safe because a menu
is drawn and erased inside **one held lock** (SPEC.md §50, `MEM_K_SAVE`). A
toast lives for seconds across many lock holds, and in that time a background
task can paint, a window can be raised, the user can drag something across it.
The restore would put stale pixels back over new ones — a permanent smear, the
`cur_move` failure mode (SPEC.md §7.1.2) with a bigger rectangle.

### 3.2 Content overlay repaired by a clipped `W_PAINT` — **rejected**

Draw over the content; on expiry arm `wm_clip_set` on the owner intersected
with the toast rect and call `W_PAINT`.

Correct, and it costs the thing we just spent a round removing. A clipped
`W_PAINT` on Note Pad **is** `np_paint` — the full walk — so expiry costs
exactly what §2's forced repaint costs today. It also inherits three rules the
app has to keep: SPEC.md §11.3's granularity rule (the rect must round
**outward** to 8px cells or the app's fill and the app's glyphs disagree and
the strip goes blank rather than stale), SPEC.md §11.96's raise cache (a
kernel-drawn overlay changes a window's pixels without the app drawing, which
is exactly the promise `WF_SAVEU` makes, so the raise cache must be dropped on
every show and every expiry), and the fullscreen-surface case (SPEC.md §53: a
bracket owns the machine and the kernel must not draw at all).

Three rules, one of which every app would have to be told about, to buy back
the repaint we are trying to avoid.

### 3.3 The menu bar's right end — **recommended**

Borrow the tail of the menus segment, exactly as SPEC.md §12.8's file-activity
widget already borrows it, and give it back the same way.

Every hard problem above disappears, and not by being handled — by not
existing:

1. **The bar can never be covered.** Windows clamp to `y >= MBAR_H`; that is
   SPEC.md §12.9's own licence for composing the segment whole and
   `fm_sel_bar`'s in another place. No clip region, no occlusion, no
   `WF_SAVEU`, no raise cache, no granularity rule.
2. **The bar has ONE painter.** `menu_draw_bar` is the only thing that draws
   there, so a toast on it *cannot* diverge between an incremental path and a
   full one — there is no second path to diverge from. §2's whole bug class is
   structurally unreachable.
3. **The erase is already written and costs nothing new.** SPEC.md §12.9's
   composer holds `menu_bcell`, one byte per glyph cell, which is **both** the
   composition buffer **and** the record of what is on the glass — so composing
   *is* the diff, and the first and last differing cells bound one `font_run`.
   A toast expiring is "stop composing it"; the diff turns that into
   re-lettering exactly the cells it covered, with whatever the menus say
   there. **No fill, no `menu_inval`, no second drawing routine.** `.tail`
   already pads to `[menu_bn]` with spaces — "this is what erases the menus of
   the app that just went away" — and a toast is the same operation.
4. **It is where this machine already talks.** The file-activity widget reports
   a file operation in flight from that space; a toast is what that operation
   says when it finishes. Same conversation, same place. (And it is the period
   answer: the Macintosh Notification Manager put things in the menu bar.)

The costs, stated:

- **A toast covers the rightmost menus while it is up.** On a 640-wide screen
  the menus segment is 50 cells (`([vid_clk_hx] - MENU_TXT_X0) / 8` with
  `vid_clk_hx = (640-206) & ~7 = 432`), so a 32-character toast leaves 18 cells
  of menus visible. For ~3 seconds. Hercules gets 62 (`MENU_BCMAX`).
- **A toast cannot say *whose* it is** beyond the app name sitting next to it.
  Acceptable: the bar belongs to the active application (SPEC.md §12) and a
  background save finishing is still the machine talking, not the window.
- **It conflicts with the file-activity widget** for the same pixels. Settled
  in §5.

---

## 4. The API

```
%define OSAPI_TOAST   KERNEL_SEG:0x0380   ; ES:SI = NUL text, CX = ticks to
                                          ; live (0 = TOAST_TICKS); out CF=1
                                          ; refused
```

0x0380 is the next free cell — the highest published slot is 0x0378
(`OSAPI_WM_SAVEU`). Nothing above it moves.

**`ES:SI` and not an X stub**, for `clip_put`'s reason (SPEC.md §55): the text
an app wants to say is very often not in the app's own image. Note Pad composes
into its own bss today, but a name taken out of a heap-claimed document is not
reachable through the caller's DS, and a far pointer the caller builds costs
one `mov es` at the call site and reaches anything.

**The string is COPIED into the kernel**, into `TOAST_MAX` bytes of `.bss`.
That is not an optimisation; it is what makes the lifetime safe. A toast
outlives the operation that raised it by design, and an app may close in that
window — a retained pointer into a freed region is a dangling far pointer the
bar would letter from. `TOAST_MAX = 40` characters plus the NUL: 41 bytes,
sized so it cannot overrun the narrowest adapter's segment, and **truncated**
rather than refused when it does not fit `[menu_bn]` less a left margin. A
caller can be terse.

### 4.1 It never draws

`toast_show` stages the string, records the deadline, and returns. **The
drawing happens in `ui_task`'s idle pass**, under that task's own lock, and so
does the expiry.

This is the decision that makes the context rule "any". A callback holds the
gfx lock; a worker task holds nothing; the kernel's own file paths hold it too.
Making the raiser draw would need every caller to know which it is — the
`snd_*` problem — and `gfx_lock` is not reentrant. Deferring gives one drawing
site, which is also the one expiry site, and puts it exactly where this tree
already spends deferred flags (`[fm_fchk]`, `[cp_dirty]`, `[desk_zdirty]`,
`[inst_launch]`). The cost is up to one pass of latency — a tick — on a
three-second message.

The stage runs under `pushf`/`cli`, because a worker and the UI task can both
raise. `rep movsb` of ≤40 bytes is ~825 clocks ≈ **173 µs** with interrupts off
(estimate), against a 55 ms tick.

Idle cost when nothing is up: `cmp word [toast_die], 0 / je` — one compare per
pass, `fpg_step`'s bargain.

### 4.2 State

All the flag words in `.text` with real initialisers, **not `.bss`** — `-f bin`
zeroes nothing and `drv_boot` reads the disk before any init routine of ours
could run, so a garbage `[toast_die]` would put a toast inside the splash
(`[fpg_on]`'s and `[fdlg_win]`'s precedent, and `fpg_on`'s comment is the
written form). The text buffer may live in `.bss`: it is read only when
`toast_die` is non-zero, which only a completed stage can set.

```
toast_die   dw 0        ; the tick it expires at; 0 = nothing up
toast_want  db 0        ; staged and not yet drawn
toast_c0    dw 0        ; first cell of the composed run, for the emit split
toast_c1    dw 0        ; last
toast_inst  db 0FFh     ; the raiser's instance slot, or 0FFh = the kernel's
toast_buf   resb TOAST_MAX + 1
```

`ticks` is a word and wraps at 65536, so the deadline test is `mov ax,[ticks] /
sub ax,[toast_die] / jns .expire` — the sign of the difference, `sch_isr`'s
wake-scan idiom and Tracker SPEC.md §45.15's, **not** a `jg`. That distinction
has already cost this tree six seconds of frozen display once.

### 4.3 Lifetime

`TOAST_TICKS equ 55` ≈ **3.0 s** at 18.2 Hz. Long enough to read eight words on
the machine this is for, short enough that it is gone before it is furniture.

**A click anywhere dismisses it early.** One test in `ui_task`'s event branch,
before anything else looks at the press — and the press is **not** swallowed,
because a toast is not modal and stealing the click that dismisses it is the
`menu_track` flashing-menu failure from the other end (SPEC.md §9.6.1).

### 4.4 Ownership and teardown

The string is copied, so **teardown is not required for correctness** — nothing
dangles. `[toast_inst]` is recorded anyway, for free, from `inst_caller`
(SPEC.md §19.2.1's "who is asking", which `snd_req_inst` is already a jump to),
and `inst_free` retires a toast belonging to the slot it is freeing. One
compare at teardown, and it stops `Saved NOTES.TXT` hanging over a bar that now
belongs to Locator.

---

## 5. What has to change in the kernel

One new file and **one** structural edit; everything else is the mechanism that
is already there.

- **`kernel/toast.inc`** — `toast_show` (stage), `toast_pass` (draw/expire from
  the idle pass), `toast_kill` (retire now), `toast_compose` (called by
  `menu_bar_text`). Estimated ~150–200 bytes of `.text`.
- **`menu_bar_text`'s `.tail`** composes the toast into the last `n` cells
  instead of padding them with spaces. That is the whole of the draw.
- **`menu_bar_text`'s emit** has to split its one `font_run`. See §5.1.
- **`ui_task`'s `.yield` chain** gains one step. Every step in that ladder
  names the *next* step in its own "nothing to do" jump, so inserting one means
  re-pointing the jump above it — get that wrong and the whole catch-all below
  becomes unreachable, which is a bug that half-works convincingly (SPEC.md
  §22.8's own warning).
- **`fpg_begin` calls `toast_kill` first.** The widget is about to say
  something more urgent about the same conversation and wants the same pixels;
  retiring the toast removes the overlap question rather than arbitrating it.
  One call.
- **`inst_free` calls `toast_kill`** when the dying slot is `[toast_inst]`.

Refusals, gated exactly as `fpg_begin` is: while `[spl_live]` (the splash owns
the screen and the mode, SPEC.md §15.3), while a visible fullscreen window
covers the bar (`wm_fs_vis` — and `menu_draw_bar` already tests this at the
top, so the toast inherits it for free), and inside an fsx bracket (SPEC.md §53
— the kernel does not run while the mode is foreign, so this cannot even be
reached; noted so nobody adds a gate for it).

### 5.1 The one structural edit, and the trap in it

A toast should be visually distinct from a menu title, or it reads as an app
having grown a menu called `Saved NOTES.TXT`. The 1bpp-safe answer is **inverse
video** — `font_run` already takes an ink *and* a background, so "draw this run
inverted" is `AL = CWHITE, AH = CBLACK` and costs nothing extra. It survives
SPEC.md §39.4's reduction exactly (white/black class, no dither, no `CDGRAY`
rounding to black), and it is Note Pad SPEC.md §27.8.2's own observation: the
cost of an inverted run was never the inversion.

The trap: **`menu_bcell` is one byte per cell and carries no attribute**, and
it is the record of what is on the glass. A cell whose *character* is unchanged
but whose *inversion* changed — a space at the toast's edge, both before and
after — would not be re-emitted, and the highlight would be left behind or
torn.

So the inversion goes in **bit 7 of the cell byte** (the bar's characters are
7-bit ASCII), which makes it part of the diff by construction. That is Note Pad
SPEC.md §27.8's trick — a selected cell hashes with bit 15 set, so moving the
selection dirties exactly the rows it left and arrived at — and it is the right
one here for the identical reason. The emit then splits at the bit-7 boundary:
**at most two runs**, because the toast is always a contiguous block at the
right end.

If that edit is not wanted in the first cut, the fallback is a **plain**
non-inverted toast — no bit 7, no split, `.tail` composes text instead of
spaces and nothing else in `menu.inc` changes at all. It looks like a menu
title. Recommend building it inverted; the diff is small and the plain version
would have to be revisited.

---

## 6. What it cost — measured

The estimate here was ~200 bytes of `.text` and ~45 of `.bss`. The kernel side
came in at **`.text` +531, `.bss` +43**, and it **crossed one 512-byte image
rung**, exactly as this section warned it might.

(`kernsize`'s own `+786 / +51` on the final build are against a **stale
blessed baseline** — the tree had 255 bytes of unblessed `.text` in it before
this work started. The figures below are the difference between the two runs,
which is what this change cost. Reading the `+N` column as "what I just did"
is a trap worth knowing about; `--bless` is what closes it.)

```
before:  text 51,115  bss 4,469   image rung 55,808 (224 left)
         KERN_SIZE 91,648 of 94,208 -> 2,560 spare (5 steps)
after:   text 51,646  bss 4,512   image rung 56,320 (162 left)
         KERN_SIZE 92,160 of 94,208 -> 2,048 spare (4 steps)
```

So the machine pays **512 bytes of RAM, on every machine, forever**, and the
budget stands at four steps of slack — the standard the fifth move settled on,
rather than the five it happened to be sitting at. `.text` overran the estimate
by about 2.6x, and the honest reason is that six `toast_*` routines carry full
register discipline (three of them preserve the flags as well) and the
`menu_bemit` split cost more than the inline emit it replaced — it banks and
restores twice and masks bit 7 across the run. `toast_now`, which the plan did
not foresee at all, is a further ~40 of it.

The two apps gave back:

| | before | after | |
|---|---|---|---|
| `notepad.bin` | 15,654 | 15,415 | **−239** |
| `paint.bin` | 16,927 | 16,789 | **−138** |

That is **heap while those apps are open**, not budget, so it does not offset
the 512 — the kernel pays and the apps save, exactly as this section said it
would. Three other packages were considered and correctly left alone (§1), so
there is no further saving waiting to be collected.

What the *machine* gets back is the case for the trade: the first keystroke
after every Note Pad save and load stops paying a full-content repaint (§2),
Paint's toast stops sitting on the artwork and stops costing a canvas blit to
hide, two "a toast sits over the text" gates in Note Pad's scroll and band
paths stop refusing their fast paths, and the incremental-vs-full divergence
class is gone.

---

## 7. Migration

In this order, each its own commit, each verifiable on its own:

1. **`toast.inc` + the slot + the composer edit.** Verified the way SPEC.md
   §12.9 was — `make REDRAWFULL=1` builds the old bar paths as a reference
   kernel, and the same scripted session through both must agree pixel for
   pixel with no toast raised. A toast changes the picture, so that leg proves
   only that nothing *else* moved; the toast's own pixels are checked by
   capture and eye on all three adapters, because the inverse strip is the
   thing most likely to be wrong on 1bpp.
2. **Note Pad.** Delete `np_toast`, `np_bx1/bx2/by1/by2`, `np_msg`, `np_msgn`,
   `np_smsg`, `np_smsgn`, and `np_sigsame`'s fifth and sixth tests. Every `mov
   word [np_msg], <s>` becomes an `OSAPI_TOAST`. **`pixcheck.py` must go from
   227 differing pixels to 0** — that is the acceptance test and it already
   exists.
3. **Paint.** Delete `pt_msg_show`/`pt_msg_hide`/`pt_msgon`/`pt_msgw`;
   `pt_msgp` stays as the *choice* of message and is handed to `OSAPI_TOAST`.
   The repaint path stops needing `mov byte [pt_msgon], 0`.
4. **Kernel callers, if wanted.** Candidates, none of which say anything today:
   a `SYSTEM.CFG` save on panel close, a volume mounted or unmounted, a
   screenshot. Listed, not recommended — each is a separate judgement about
   whether the machine should speak.

Tracker, ModPlug and Missile are **out of scope**, per §1.

---

## 8. What is the owner's to decide

1. **Placement.** The menu bar (§3.3) against a content overlay (§3.2). The
   recommendation is the bar and the case is strong, but it is a look-and-feel
   decision as much as a technical one: a toast in the bar covers the app's
   rightmost menus for three seconds, and a toast in the content covers the
   user's work instead.
2. **Inverse strip or plain text** (§5.1). Recommend inverse; plain is the
   smaller diff.
3. **Three seconds**, and whether a click dismisses early (§4.3). Recommend
   both.
4. **Whether the kernel spends a 512-byte rung on this** (§6) — measure first,
   then decide.
5. **Whether any kernel operation should start speaking** (§7 step 4).
