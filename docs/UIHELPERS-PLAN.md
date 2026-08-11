# UI element helpers — investigation and plan

> **STATUS: IMPLEMENTED** as `apps/os88ui.inc` — and it is now **ONE SOURCE
> FOR TWO WORLDS** (§13): the same text assembles into the kernel's own
> dialogs and into a package, picked by `OS88UI_KERNEL`. Recorder is the
> first package adopter and its buttons fire on the RELEASE now; the file
> dialog, the Control Panel and the Timer are the kernel's. `tests/muptest`
> gates the shared control on all three adapters. §11 is what was and was
> not proved; §13.3 is what it cost, which is **not** what §9 predicted, and
> §13.4/§13.6 are the two defects the conversion turned up — the second of
> them a live corruption of `sch_isr`.

**Tier 4 of the mouse-up work**, and the one whose framing changed most under
investigation. I ranked it *"biggest win and biggest risk — most of
`KERN_BUDGET`'s remaining spare."* Measured, **it is not a performance change
at all and it need not cost the kernel a byte.**

It is what makes Tier 3 (`docs/WMEVENT-PLAN.md`) worth having: a package with a
mouse-up callback still has to write the arm/fire state machine itself, and
this is where that gets written once.

---

## 1. Three corrections to the ranking

### 1.1 The ~25 private helpers are not one control

I counted 25 hand-rolled control helpers and called a shared one a
*consolidation*. Reading them, **they are not drawing the same thing:**

| helper | what it draws |
|---|---|
| `fdlg_btn` | frame + label + optional outer default ring. **No fill** — the pane was filled once by `fdlg_paint` |
| `at_button` | fill + frame + default ring + centred label, returns its width |
| `pn_btn` / `rc_btn` | frame + centred label through `pn_cframe`/`pn_cstr`, which **clamp to the content box** |
| `cp_glyph` | a **12x12 bitmap** blitted over a white box — `cp_radio_on/off`, `cp_chk_on/off` |
| `mpps_draw_box` | a **bevelled well** with a screened-outline greyed variant and a white mark |
| `mppu_btn_led` | ModPlug Player's LED transport button |
| mines' cell | a Windows-style 3D bevel, its own colour table |

The bottom three are **deliberately** not the standard control. SPEC.md §56 is
explicit that ModPlug's interface is a *port of ModPlugPlayer's look*, and
Minesweeper's bevel is its own game's chrome. Converting them would be
undoing intended design.

**The genuinely shareable set is the System-1-standard button** — frame,
centred label, greying, optional default ring — which is roughly **ten** of the
twenty-five, plus the check/radio glyph pair, which today has exactly one
consumer (`ctrl.inc`). "Consolidate 25 sites" was wrong.

### 1.2 It is not a performance change

The 1.5x I modelled came from replacing `gfx_fill` + `font_str` with an opaque
`font_run`. That is real (`font_run` is 905µs/cell measured against ~1ms, and
it subsumes the fill), but **it is a *layout* change, not a helper change** —
any of the ten sites could make it today. A helper makes it *likely*, not
*possible*, and it comes with a constraint: `font_run`'s fast path needs
`x & 7 == 0`, so the label band has to be 8-aligned, which fights centring a
label in a fixed-width button. That is a design decision with a visible
consequence, not a free win.

The other half — API crossings — is smaller than it looked, and **the SDK
shape in §3 saves none of it at all**:

| | crossings per button | µs |
|---|---|---|
| hand-rolled `at_button` | 6 (`SET_COLOR`×2, `FILL`, `FRAME`, `FONT_WIDTH`×2, `FONT_STR`) | ~180 |
| kernel-API helper | 1 | ~30 |
| **SDK-include helper** | **6 — identical** | ~180 |

An SDK helper is near calls *inside* the package; the primitives it invokes are
the same far calls in the same order. The ~150µs a kernel-API helper would save
is **1.5% of a ~10ms button draw** and is not a reason to do anything.

**So the honest answer to "does this cause a performance loss?" is: no loss,
and no meaningful gain either.** It is a consistency and code-reuse change.

### 1.3 The strongest argument is one I under-weighted

**§47 greying has been fixed five separate times, each as its own bug.**
`recorder.asm:965`, `mppui.inc:315` (*"rule 1's own failure"*), `hdd/tool.inc:47`,
`hdd/inst.inc:329`, `hdd/page.inc:34`, and `fdlg_btn` forcing `CBLACK` — every
one the same mistake, setting `CDGRAY` without `[gfx_dis]`, which is a real
grey on VGA and **solid black on the two 1bpp adapters**, so a greyed caption
was pixel-identical to a live one on the target machine. CLAUDE.md calls it *"a
misconception this tree carried in six places at once."*

The tree is clean today — every one of those sites now takes the pen through
`OSAPI_GFX_PEN` / `gfx_pen_cf`. So this is not a live defect; it is **evidence
about what hand-rolled controls cost over time.** A helper that takes the pen
correctly makes rule 1 structural instead of remembered, and that is worth more
than the microseconds in §1.2.

## 2. What this is really for: it completes Tier 3

Tier 3 gives a package `W_ONMOUSEUP`. That is a *callback*, not a button — the
package still has to:

- record which of its controls the press landed on,
- re-hit-test at the release and compare,
- clear the arm on any path that does not fire,
- draw and un-draw whatever pressed state it wants.

That is `MOUSEUP-PLAN.md` §4's four guards, per package, written by hand, in
fifteen places. **A helper that owns draw + hit + arm is what turns Tier 3 from
a callback into a working button**, and it is the reason to do Tier 4 at all.
Without it, Tier 3 ships a mechanism nobody will use correctly.

## 3. The key finding: put it in the SDK, not the kernel

`apps/os88api.inc` contains **zero instructions** — it is `%define`s and
data-emitting macros (`OS88_HEADER`, `OS88_MENUSET`, `OS88_ICON16`…). And it is
the **only** shared include in `apps/`: every other `.inc` there is
package-private (`mppui.inc`, `atui.inc`, `trkui.inc`).

So the shape is a **new, optional, code-carrying include — `apps/os88ui.inc`** —
that a package pulls in if it wants the widgets. Near procs, assembled into the
package's own image.

| | SDK include | kernel API slot |
|---|---|---|
| `KERN_BUDGET` | **zero** | ~350–450 bytes, against 1,536 spare (kern_big, three steps) — but only **63 bytes** left in the image rung after Tier 1, so it would cross at least one |
| API table | untouched | 3–4 new cells, a table revision, every `.o88` rebuilt |
| ABI | **none** — it is source | a published contract |
| revision cost | edit and rebuild adopters | a table revision |
| per-package image | +~250 B of a 60KB `APP_MAX_SIZE`, and it *replaces* a private helper | 0 |
| kernel's own dialogs can use it | **no** | yes |
| crossings saved | **none** (§1.2) | ~150µs/button, i.e. nothing |

**Take the SDK include.** The one thing the kernel-API version buys that the
include does not is letting `fdlg.inc` and `ctrl.inc` share the same body — and
those already have working private helpers, so that is a dedup opportunity
rather than a need. Set against spending most of the remaining kernel budget on
a convenience, it is not close.

This inverts the tier's headline risk. Tier 4 was *"biggest win, biggest risk,
needs the `KERN_BUDGET` conversation up front."* As an SDK include there is
**no budget conversation** — the guard is a statement about the kernel's
footprint and this adds nothing to it.

Two consequences worth stating plainly:

- **It establishes a second shared include, and the first carrying code.** A
  small new pattern. Natural, but it should be a deliberate choice rather than
  something noticed later — and it means `os88api.inc` stays code-free, which
  is worth preserving: every package includes it unconditionally.
- **N copies on the floppy.** Roughly 250 bytes per adopter, minus the private
  helper it deletes — near a wash per package, and a few clusters across
  `combo.img` (304 of 354 used). Not a constraint at this size, but it is the
  cost the kernel-API version would not have.

## 4. Scope

**In:** a standard button (draw, hit-test, and the Tier-3 arm state machine),
and the check/radio glyph pair.

```
os88ui_btn_draw    x, y, w, label, flags(default|disabled|pressed)
os88ui_btn_hit     x, y -> CF                (the rect test, shared with draw)
os88ui_arm         press  -> record which control
os88ui_fire        release -> CF = "same control", clears the arm
os88ui_glyph       radio/check, on/off, at x,y
```

`btn_draw` and `btn_hit` must derive the rect from **one** place — the
`fm_hit` discipline, so the drawn control and the clickable control cannot
drift.

**Out, and deliberately:**

- ModPlug's skinned controls (SPEC.md §56 ports ModPlugPlayer's look on
  purpose), Minesweeper's bevelled cell, Tamegram's HUD.
- The kernel's own `fdlg_btn` / `cp_glyph` / `app_tmr_btn`. They are not
  packages and cannot include this. Consolidating *them* is a separate,
  purely-internal refactor with no ABI at all — see §7.
- `font_run` opacity as a *requirement*. Offer it as a flag on `btn_draw` for
  a caller whose layout can take an 8-aligned label band (§1.2); do not force
  the layout.

## 5. Budget

| | |
|---|---|
| `KERN_BUDGET` | **0** |
| `KERN_CODE_MAX` | **0** |
| API table | unchanged, no `.o88` invalidated by this tier |
| per adopting package | **+~250 B** of `APP_MAX_SIZE` = 0xF000 (60KB), *minus* the private helper deleted — near a wash |
| floppy | a few clusters across both app images |

The per-package figure is from instruction shapes; the kernel baseline is
measured (Tier 1 installed `nasm`), and this tier adds nothing to it. It does not touch the kernel, so
`kernsize.py` has nothing to report either way; what wants checking at
implementation is that `combo.img` still fits (CLAUDE.md: 304 of 354 clusters).

## 6. Testing

The helper draws pixels, so the test is pixel identity, per site.

1. **Per converted site, byte-identical framebuffers** against the build before
   conversion — unless the conversion deliberately changes the look, in which
   case the change is named in the commit and looked at. `os88marty.py shot
   --rendered` and a diff; this is the discipline `REDRAWFULL=1` exists for
   elsewhere.
2. **On CGA first, then Hercules, then VGA.** Non-negotiable for this tier: the
   entire §1.3 bug class is invisible on VGA and obvious on 1bpp. A greyed
   button that looks right in colour and is solid black at one bit is exactly
   what five previous fixes were about, and CLAUDE.md's standing rule is that a
   greying change is not done until it has been looked at on mono.
3. **A greyed and a live button side by side**, both adapters, in one capture —
   the check that catches rule 1 directly.
4. **The arm, via Tier 3's gate package**: press inside → release outside → no
   fire, pressed state un-drawn.
5. **`APP_MAX_SIZE` and the disk.** Every adopter's `.o88` still assembles under
   60KB and `make combo` still fits 354 clusters.

## 7. 7a: consolidate the kernel's own helpers — DONE, and it is 12 bytes

> **RESULT: `.cold` −12 bytes.** The estimate below said 200–300 and was
> **20x too high**. See §7.1 — the finding is that these modules are already
> well factored, and that is worth knowing precisely because it stops the
> next person spending a day here looking for room.



Independent of everything above, no ABI, no package impact: `fdlg_btn` /
`fdlg_btn2` / `fdlg_defbtn` / `cp_vid_btn` / `cp_timebtn` / `app_tmr_btn` are
close enough in shape to share a body, and `cp_glyph`'s four 12x12 bitmaps are
already one mechanism with one caller.

Worth maybe 200-300 bytes of `.text`, which is real money against kern_small's
512-byte spare — **and that was wrong; see the result above and §7.1.**

### 7.1 What was actually there, and why it is only twelve bytes

**Everything in `fdlg.inc` and `ctrl.inc` is in `.cold`; only `app_tmr_btn`
is in `.text`.** That decided the scope on its own: a helper serving both
sections needs a shim, and `.text` had *zero* bytes of rung left after
Tier 3, so `app_tmr_btn` was left alone.

Measured, the duplication is not there:

- **`cp_glyph`, `fdlg_off`, `fdlg_text`, `fdlg_blabel` already ARE the shared
  bodies.** What is left at each call site is loading four constants into
  `AX/BX/CX/DX` — which does not consolidate: a table-driven
  `frame(SI = a rect)` saves ~11 bytes of code per site and costs 8 bytes of
  table per site, against a ~20-byte helper, so five sites come out
  **negative**.
- **`fdlg_btn` does not fill and the `ctrl` buttons do**, so they are two
  shapes rather than one. `fdlg_paint` fills its pane once; a button there
  draws a frame on ground that is already right.
- The one real overlap is the `ctrl` pair — `cp_vid_btn` (the Display page's
  Activate) and `cp_timebtn` (Date/Time's `+`/`-`) — which both draw frame,
  inset white fill, then a label. That was `cp_boxbtn`; **§13 has since
  deleted it**, because `OS88UI_FILL` subsumes it.

**A pleasant surprise inside it:** both callers *centre* their labels, and
neither looked like it. `cp_vid_btn` adds a literal `+6` and `cp_timebtn`
computes `(CPT_BW - 8) / 2` — but `CPV_BTNW` is 116 and its label is 13
glyphs = 104px, so `+6` **is** `(116 - 104) / 2`, and the source comment
already said so. One helper serves both with no centring flag and no pixel
moves.

**Four more bytes came from deleting work nobody wanted:** the first draft
put `AX..DX` back to the rect it was passed, and neither caller reads them —
both recompute the label position from the pane origin. `cp_boxbtn` left
the interior in them and said so. (That whole register question **dissolved**
at §13: the shared control takes a rect *pointer* and preserves everything,
so there is nothing for a caller to read back or not read back.)

Verified by eye on the adapters where each is reachable, which is not the
same machine: the Date/Time page on `os8088_5150_cga_gla`, and the Display
page on **`os8088_5150_both_gla`**, because §31.10.1 does not draw a Display
page at all on a one-adapter machine — so `cp_vid_btn` is unreachable on the
default test box. Activate Mode comes up correctly **greyed, frame and label
dithering together**, which is §47 rule 1 surviving the refactor.

### 7.2 Where the room actually is

Not here. The Control Panel is **4,120 bytes, 5.3% of the kernel**
(`kernsize.py --modules`), and its buttons are a small fraction of that. The
kernel's big blocks are the file system at 36.8% and the window system at
21.5%, and the one *provably unreferenced* block is the XMS store —
`kernel/xmem.inc`, 625 lines of code, three of its four slots with no caller
anywhere (`WMEVENT-PLAN.md` §2.1). That is where a step comes from, and it
wants its own investigation rather than a refactor.

## 8. Order of work — DONE

1. ~~Tier 3 first, or the arm has nothing to talk to.~~ Done.
2. ~~`apps/os88ui.inc` — button draw + hit + arm.~~ Done: `os88ui_btn`,
   `os88ui_bhit`, `os88ui_bfind`, `os88ui_arm`/`os88ui_fire`. The
   check/radio glyph pair was **deferred here and built in §13.7** — the
   reason it was deferred (its only consumer was kernel code that could not
   include an SDK file) is exactly what §13's unified source removed.
3. ~~Convert one package and prove byte-identity before touching a second.~~
   Done — Recorder, **0 differing pixels of 128,000**.
4. ~~Convert the rest of the standard-look set.~~ Done — §14, seven helpers,
   every one pixel-diffed.
5. ~~§7a (the kernel's own helpers).~~ Done — §7.1, and then subsumed by
   §13's unified source, which deleted the `cp_boxbtn` it produced.

### 8.1 Two design points that only appeared in the writing

**The rect is a POINTER, not four registers.** `os88ui_btn` takes
`BX = {x1,y1,x2,y2}` and so does `os88ui_bhit`, so the drawn control and the
clickable control read the same four words — SPEC.md §22's `fm_hit`
discipline. Recorder is the argument: its drawing used `4 / 58 / 112 / 166`
and `RC_BTN_W`, while its hit test used `4/56, 58/110, 112/164, 166` — two
descriptions of one row of buttons, agreeing by hand. They cannot disagree
now.

**The include goes at the END of your source, just before `OS88_BSS`.** Not
up beside `os88api.inc`: the header and an `OS88_ICON16` block are at fixed
image offsets (SPEC.md §20.2) and code emitted between them fails the icon
macro's own assertion — which is how the first build of the Recorder
conversion failed.

## 9. Measured cost

| | |
|---|---|
| `KERN_BUDGET` | **0** — it is source in the package, not a slot |
| the include, assembled | **~190 bytes** of a 60KB `APP_MAX_SIZE` |
| Recorder, `.o88` | 2,422 → 2,704 bytes (**+282**) |

**Adoption GROWS a small package, and §5 said it would be "near a wash".**
That was wrong for the first adopter: Recorder deleted a ~50-byte private
helper and gained ~190 bytes of shared code plus the rect table and the
release handler. The wash arrives for a package with several controls, or
never — **and it does not matter**, because the value is §1.3's greying and
§2's arm, not size, and a package's image is heap rather than
`KERN_BUDGET`.

## 10. What it does NOT cost

The kernel: nothing at all. No API cell, no table revision, no `.o88`
invalidated by *this* tier, and `os88api.inc` stays code-free — which is
worth keeping, because every package includes that one unconditionally.

## 11. What the gate proved, and what it did not

`tests/muptest` carries the shared control now as well as SPEC.md §13.7: two
buttons drawn by `os88ui_btn`, hit by `os88ui_bfind`, armed and fired by the
pair. Four gestures, **all passing on CGA, Hercules and VGA**:

| | |
|---|---|
| press off both buttons, release on One | **not fired** — the press armed nothing |
| press One, release on **Two** | not fired, **and the caption changed** — so the release arrived and the arm refused it, which are different facts |
| press One, release **outside the window** | not fired, caption changed again — §13.7's second rule |
| press One, release **One** | **fired** (the window hides) |

The caption **toggles** between two strings rather than latching, because a
harness has to see every refused release and not just the first. And the
hiding case runs **last**, so nothing relaunches — a double-click on a
background Disk window spends its first click on `wm_front`, so the row
never opens, and that cost two adapters' worth of false failures before it
was noticed.

**Recorder: the drawing is proved and the FIRE is not.** Pixels first — the
converted `rc_btn` was diffed against the pre-conversion build through a full
launch on CGA: **0 differing pixels of 128,000**. Then the cancel: press REC,
slide off, release — the button row is untouched, which is the gesture this
whole tier exists for. What is **not** automated is Recorder's *fire*:
`rc_do_rec` opens a stream that `rc_poll` retires within a second, so
`M.settle`'s two-identical-frames-a-second-apart always outlives the state it
is trying to observe. It was confirmed by eye in a capture taken without the
settle (REC greyed, STOP live), and the mechanism itself is gated by
muptest's fourth case. Stated rather than contrived.

## 12. What is left

~~The other nine standard-look buttons~~ — done, §14. And the list in this
paragraph was wrong twice, which is worth keeping rather than editing away:
**`pt_btn_xy` is not a button at all** — it is pure geometry, returning
`CX`/`DX` for a tool cell and drawing nothing. What the survey then concluded
from that — *"Paint has no standard labelled button anywhere"* — **was wrong
in its turn**: Paint has exactly one, the size group's **Apply** (§14.4),
which a grep for a labelled button missed because everything else in that
module's chrome is `pt_cwell`. And **`mppl_btn_rect` is skinned**
(SPEC.md §56), so it was never a candidate. What the list missed, on the
other hand, is the whole of `drivers/hdd` — three helpers, not one.

~~The check/radio glyphs~~ — done, §13.7: `os88ui_glyph`, with the whole
Control Panel as its first caller. A **package** consumer is still wanted;
the widget is published and nothing outside the kernel draws one yet.

~~And §7a~~ — done, and it is twelve bytes (§7.1). The budget room is still
not here; §7.2 is where it is.

## 13. The unified source — one text, two worlds

§3 chose the SDK include over a kernel API slot and listed the one thing the
slot bought that an include did not: *"letting `fdlg.inc` and `ctrl.inc`
share the same body"*. That turned out to be a false choice. **An include is
source, so it can be included twice** — once into `.cold` with
`OS88UI_KERNEL` defined, once into each package without — and the six
primitive calls that differ between the two worlds are six one-line macros.
`-I apps/` on the kernel's `nasm` line is the whole build change.

The kernel's three standard buttons are `os88ui_btn` now: `fdlg_btn` (and
`fdlg_btn2`/`fdlg_defbtn` through it), `cp_vid_btn`, `cp_timebtn`, and
`app_tmr_btn` — the last through `os88ui_btn_f`, a `retf` shim, because
`apps.inc` is `.text` and the body is `.cold`. `cp_boxbtn`, §7.1's own
twelve-byte consolidation, is **gone**: `OS88UI_FILL` subsumes it.

### 13.1 Why it was possible at all

Two facts, neither obvious until the bodies were side by side:

- **All four kernel buttons already centre their labels**, and none of them
  looked like it — §7.1 found this for the `ctrl` pair (`+6` *is*
  `(116-104)/2`) and it holds for the other two: `fdlg`'s `+3` is
  `(14-8)/2`, and the Timer writes it out as `APP_TMR_LT = (APP_TMR_BH-8)/2`.
  Three literals and one formula, all the same number, so one body serves
  them with no centring flag and no pixel moves.
- **A whole-rect fill and an interior fill are the same picture** once the
  frame is drawn over the border. That is what let the Timer's whole-rect
  version and the `ctrl` pair's inset version share one `OS88UI_FILL`, and
  it is why §7.1's *"`fdlg_btn` does not fill and the `ctrl` buttons do, so
  they are two shapes rather than one"* stopped being true — it is one shape
  and a flag.

### 13.2 What it is for, restated

The user's framing, and it is the right one: *"Saving space isn't the only
benefit; if we decide to add features to the UI element then they get them
for free and we stay consistent."* A pressed state, a focus ring, a
different disabled treatment — one edit, and the Standard File dialog, the
Control Panel, the Timer and every package have it. §1.3's five separate
greying fixes are the evidence for what the alternative costs.

### 13.3 What it cost, measured, and it is not what §9 said

```
sections   text 55,470 +138   bss 4,943 +27   cold 22,584 +121
rungs      image 60,416 +0 (3 left)     cold 23,040 +512 (456 left, was 65)
footprint  KERN_SIZE 97,280 of 98,304 -> 1,024 spare (2 steps), was 1,536
```

**One 512-byte rung of `.cold`, so the footprint spare goes three steps to
two.** I estimated *"net −150 to −190 bytes, and — this is the part that
matters more than the sign — it likely frees `.text`"*, and that was wrong
in both halves: `.text` grew 138 and `.cold` grew 121. **Generalising costs
more than the four specialised bodies saved.** The reason is visible in the
diff — a body taking a rect pointer and a flags word does register shuffling
that a body with four constants baked into it does not, and each call site
now builds a four-word rect it used to pass in registers.

That is the honest price of §13.2, and it is the trade the user asked for
explicitly. It is also **the second-to-last step** — 1,024 bytes of
`KERN_BUDGET` spare, against the four-step standard the fifth move settled
on. The next feature has to ask.

### 13.4 The greyed caption — FIXED, and the first diagnosis was wrong

**The finding, corrected.** The file dialog's greyed default button draws a
dithered frame around a **solid** caption. I recorded that as a §47
mechanism failure specific to `fdlg`'s path, and it is not: **`font_char` is
transparent.** It ORs ink in and erases nothing, so when the button is
*redrawn in place* the new checkerboard caption lands on top of the old
solid one and the union is the solid one. The frame is a `gfx_frame`, which
*writes* rather than ORs, so it dithers correctly — and that split is
exactly what makes the failure read as "rule 1 is broken" when rule 1 is
working perfectly.

Measured, on `os8088_5150_cga_gla`, the same button in the same state
reached two ways:

| | ink |
|---|---|
| REFUSED, redrawn in place (backspace the name box empty) | **158** |
| REFUSED, freshly painted (drag it → `fdlg_paint` white-fills first) | **116** |

116 is a proper checkerboard `Save`; 158 is a solid one with the
checkerboard OR'd invisibly into it. After the fix both are 116 and they are
**0 pixels apart**.

**The fix is `OS88UI_FILL` from `fdlg_defbtn`** — one `gfx_fill`, on the one
button in the kernel that is redrawn in place, because it is the one whose
state can move. Cancel, Drive and New Folder are drawn once onto a pane
`wm_paint_all` already whited and can never change what they say, so they
still pay nothing.

**Two earlier claims of mine were wrong and are withdrawn.** It was never
`fdlg`-specific — any button redrawn without an erase has it, and the reason
the Control Panel's `Activate Mode` *does* checkerboard is simply that
`cp_vid_btn` passes `OS88UI_FILL`. And the "83 ink greyed against 83 live"
measurement was taken on a button that **was not greyed at all**:
`fdlg_actok` answers LIVE while the name box holds anything, and the box
arrives with a remembered name in it (§38.10), so both dumps were of a live
button. Emptying the box is what makes the state reachable.

`tests/fdlggrey.py` is the gate, and it discriminates: **42 pixels differ
without the fix, 0 with it.** It checks both states, because a fix that made
the greyed one right by breaking the live one would pass a one-sided test.

### 13.5 …and the rule that follows is about the QUESTION, not the colour

`OS88UI_FILL`'s question is **not** "is my background white". It is: *can
this control be drawn a second time without the ground being repainted
first?* A control whose greying can move always can be. That is a fact about
the control, checkable at the call site, and it is written into the flag's
own definition in `apps/os88ui.inc` rather than into a document.

Recorder is the counter-example that proves the rule is about the caller and
not the widget: `rc_repaint` white-fills its whole content before
`rc_draw_btns`, so its four buttons are always on clean ground and need no
fill — which is why the conversion diffed at 0 pixels there.

### 13.6 A `.cold` word reached through `DS` is not that word

Found by a symbol dump opened for a different question, and it is the worst
thing in this branch's history. The three rect scratches the conversion
added — `fdlg_brect`, `cp_brect`, `app_tmr_rect` — were declared where their
callers were, and two of those callers are in `.cold`. **A cold module has a
CS of its own and `DS` is still `KERNEL_SEG`** (SPEC.md §2.6), so a word
declared in `.cold` and reached through `DS` is not that word at all: it is
whatever lives at the same offset in `.text`.

The write and the read used the same wrong address, so **the buttons drew
perfectly** — and the eight bytes went somewhere else:

| scratch | wrote 8 bytes of screen coordinates into |
|---|---|
| `fdlg_brect` (`COLD_SEG:4290`) | `KERNEL_SEG:4290` — the middle of **`sch_isr`**, the PIT tick handler |
| `cp_brect` (`COLD_SEG:4F99`) | `KERNEL_SEG:4F99` — the middle of **`wm_destroy`** |

Nothing failed. Every gate passed. `make` was clean, `-w+error` was clean,
and `os88ovlchk.py` — which exists to catch cold/text boundary errors —
passed, because it checks *calls and branches* and this is a *data*
reference.

There is **one** kernel scratch now, `os88ui_krect`, declared by
`os88ui.inc` in `.bss` with the rule in its own comment, and no kernel
caller declares a rect any more. `ctrl.inc`'s radio bitmaps carry the same
rule in *their* comment and always had it right — which makes this a lesson
about forgetting a written rule rather than about not having one.

### 13.7 The check box and the radio button

§4 scoped these in and §8 did not build them, on the reasoning that their
only consumer was `ctrl.inc`, which is kernel code and could not include an
SDK file. §13 removed that reason, so `os88ui_glyph` exists and the Control
Panel is its first caller — every radio and every check in the machine, six
sites across the Scheduler, Display, Date/Time, Sound and Drivers pages,
plus the Sound page's Test button, which was the last framed control in the
file still drawing itself.

`cp_glyph` and its four bitmaps are gone. What changed at each site is that
**the pen travels as a flag now instead of as a colour the caller sets
first** — the Sound page's `cp_snd_rowok` result goes straight into `AH`,
where it used to become a `gfx_pen_cf` call and a hope that nothing between
there and the bitmap walk disturbed it.

**Verified by byte identity against the previous commit's kernel**, on
`os8088_5150_cga_gla`, six pages captured through the same scripted click
sequence:

| | |
|---|---|
| Scheduler, Display, Drivers, Sound | **0 differing pixels of 128,000** each |
| Date/Time | 25 px, and see below |

**The Date/Time difference is the clock, and that is measured rather than
argued.** The same reference kernel captured twice gives the *identical*
difference — 25 pixels, bounding box x 321..327, y 101..107, one digit cell
in the time field. A control run is the only thing that separates "the clock
moved" from "my change moved a digit", and it cost one more capture.

The one deliberate pixel change is the Sound page's **Test** button, whose
label sat at `CPS_BTX1 + 11` in a 45px box — 32px of glyph flush against the
right frame, 1px clear. Through the shared control it is cell-centred at +6
like every other button in the machine, so it moves 5px left. Named here
because §6 rule 1 says a conversion that changes the look says so.

### 13.8 The bug the diff caught, and the two the diff could not

**`mov ax, cx` destroyed the flag.** The first `os88ui_glyph` took the
disabled byte in `AH` and then used `AX` as the white box's `x1` — so `AH`
became the high byte of a screen x, which is non-zero for any x above 255,
and **every glyph on every page came out greyed**. It assembles, it boots,
and it draws a perfectly plausible dithered ring. The pixel diff found it in
one run (24 px per glyph, exactly one 12x12 cell); nothing else would have.
The flag is banked in `DI` now.

Two others were structural rather than visual and are worth stating because
neither shows on a screen:

- **`apps/os88ui.inc` was not a prerequisite of `build/kernel.bin`.**
  `KERNEL_INC` is `$(wildcard kernel/*.inc)`, so editing the shared control
  alone left the kernel and every image **stale**, while `kernsize.py` — which
  re-assembles for itself — cheerfully reported the new sizes. That is a
  change that appears to do nothing, and it is only luck that `os88sym.py`'s
  byte-identity check refused first and said so in as many words.
- **Eight bytes of lookup table cost a 512-byte rung.** `os88ui_glyph`
  indexed four bitmaps through a table of pointers, which is two bytes
  shorter than the arithmetic that replaced it — and a table is DATA, so it
  lands in `.text`, which had **three bytes** of rung left. The arithmetic is
  CODE and lands in `.cold` beside the body, where 479 were going spare. Same
  kernel, same pixels, one rung cheaper. The contiguity the arithmetic rests
  on is asserted at assembly time rather than trusted.

Net: `.text` −8, `.bss` +8, `.cold` +112, **no rung moved and the footprint
is unchanged** at 97,280 of 98,304.

### 13.9 Traps

- **The include must not go beside `os88api.inc` in a package** (§8.1), and
  in the kernel it must go in `.cold`, where its callers are. A `.text`
  caller crosses through `os88ui_btn_f`.
- **`BP` addresses `SS` by default**, and in a package `SS != DS`
  (SPEC.md §20.1), so every rect read in `os88ui_btn` carries a `ds:`
  override. Free in the kernel and mandatory in a package, from one text.
- **`tools/kernsize.py` and `tools/os88sym.py` re-assemble the kernel
  themselves** and needed `-I apps/` too. Without it they fail in a way that
  looks like a broken kernel rather than a broken tool.

## 14. The package conversions — done, and one feature had to land first

Six helpers across four packages and one driver, plus the survey's two
corrections to §12's list (above).

**Every one is pixel-diffed against a build from the commit before it.**

| helper | what changed on screen |
|---|---|
| `hd_iw_button` (hdd installer) | **0 differing pixels of 45,000** — geometry, centring and greying all matched already |
| `pn_btn` (Piano) | **0 of 34,720** — including the three coloured buttons |
| `np_pbutton` (Note Pad) | panel closed **0 of 40,300**; panel open, labels move **up 1px** |
| `pt_szdraw`'s Apply (Paint) | 54 px: the label moves **up 1px**. Frame untouched |
| `at_button` (ArtfulType) | 304 px: the default ring, **3px out → 2px**. Frame and label untouched |
| `hd_tw_button` (partition tool) | 480 px: `Format` and `Delete` **centred**, 4px right each |
| `hd_page_button` / `hd_page_pm` (Disks page) | 322 px: `Format` +4px, `Mount` +12px, and the `+`/`-` glyph up 1px |

The Drivers page — the same Control Panel window, one item row away — is
**0 of 44,800** both before and after the hard-disk row is ticked, which is
the control that says the harness is measuring the page and not the window.

### 14.1 `OS88UI_INK` — the first feature the shared control gained

Piano was the one conversion that could not be a conversion. Three of its
five buttons are `CGREEN` / `CMAGENTA` / `CRED`, and SPEC.md §47 names that
case explicitly — *"Piano's coloured letters want contrast"* — so it is
decoration rather than state, and the shared control knew only live and
disabled. Converting as-is would have turned three buttons black on VGA.

`OS88UI_INK` is that: a flag, with the colour in **DI's high byte**. Not
`AL = the colour`, because every existing caller reaches `os88ui_btn` with
arbitrary `AX` and a silent reinterpretation of a live register is the exact
shape of bug this file has already produced twice (§13.8). `DI` is the flag
word — every caller either builds it from `OS88UI_*` constants or clears it —
so its high byte is 0 by construction and no existing call site had to change.

**Disabled wins.** A greyed control is `CDGRAY` and dithered whatever ink it
asked for: rule 1 is about state, this is about decoration, and state is not
negotiable.

It cost about a dozen bytes and it is the thing §13.2 predicted — one edit,
and the file dialog, the Control Panel, the Timer and every package can have
a coloured caption. Piano is the only caller today.

### 14.2 The two labels that were never centred

`hd_tw_button` and `hd_page_button` put their labels at a flush-left `x+4`.
Everything else in the system centres, so they now do — `Format` and `Delete`
(48px in 64) and `Close` (40px in 56) each move 4px right. It is the §13.7
Sound-page `Test` case a second time, and the same answer: a literal that was
standing in for arithmetic, in a box whose width had moved on.

### 14.3 The pen stopped being the caller's

`hd_tw_button`'s header explained at length why **the pen belonged to the
caller**: `AX` is the x, `OSAPI_SET_COLOR` took its colour in `AL`, and
setting it inside drew both buttons at the same place. That is a real
constraint and it is simply gone — the shared control's rect arrives as a
*pointer*, so `AX` is free. The caller keeps the half that mattered, which is
the predicate: `hd_tw_delok` still greys the button and refuses the click, and
its `CF` becomes `OS88UI_DIS` with no test in between, because that flag is 1
and `mov` does not touch the carry.

`hd_iw_button` lost its `hd_ibl` latch the same way — it existed to carry the
label across the predicate call, and `SI` now goes straight through.

### 14.4 Paint had one after all

§12's list named `pt_btn_xy`, which is pure geometry and draws nothing, and
the survey concluded Paint had no standard button anywhere. It has exactly
one: **Apply**, under the `W` and `H` fields (SPEC.md §42). Everything else
in Paint's chrome is `pt_cwell` — a filled well with a 16x16 icon and no
caption — which is why a grep for a labelled button missed it and reading
the size group found it.

It is the third literal in this work that was standing in for the same
arithmetic and the second that was already right in one axis: `'Apply'` is
40px in a 42px button, so its x of 1 **is** `(42-40)/2`, and only the y
moves — `+3` in a 13-row box against `(13-8)/2 = 2`.

Reaching it needs **Hercules**, not CGA: `pt_szon` refuses to draw the size
group at all on a 110-row CGA canvas (SPEC.md §42) and the two-line readout
shows instead, so a CGA capture would have compared two pictures with no
button in them and reported 0 pixels — a pass that measured nothing.

### 14.5 Deliberately NOT converted

- **ModPlug** (`mppl_btn_rect`, `mppu_btn_led`) — SPEC.md §56 ports
  ModPlugPlayer's bevelled face and LED transport on purpose.
- **Tracker** (`tui_btn`) — FT2's bevel, SPEC.md §45; and its own header says
  it is decorative, because every action there is a key.
- **Minesweeper's cell** — SPEC.md §23, a Windows-style 3D bevel with its own
  colour table.
- **Paint** — nothing to convert (see §12's correction).

### 14.6 How each was reached

**Packages**: each alone in the root of its own 360KB image, so it resolves
by name out of a fresh mount. A **folder dive does not work** — a Disk window
paints from its own cache and leaves the global snapshot stale (SPEC.md
§18.9/§22.8), so `disk_dir` after one still answers about the root and the
harness reads it as the package not being on the disk.

**The hard-disk driver** took a harness of its own, `os8088_xt_hdd`:

1. open the Control Panel from the chip menu;
2. select the **Drivers** item row and tick the hard-disk row — SPEC.md
   §51.3 means nothing is loaded that `SYSTEM.CFG` did not ask for, so
   without this the driver's page does not exist at all (§31.9);
3. confirm it actually loaded by reading `drv_tab[1].DRVR_SEG` — 0 is "not
   loaded", and a page that is missing looks exactly like a page that is
   empty;
4. select item row `[cp_nst]`, which is where a driver's page is appended;
5. click **Format** and **Install**, each of which only *opens* a window —
   `hd_tool_open` reads the tool as a second image off the system volume and
   nothing writes to a disk until a button inside is clicked, and none is.

**Every coordinate is the module's own constant, and that is not fastidious
— it is what made the difference.** A first pass estimated the Drivers
page's row pitch, clicked the same row twice, and reported that the driver
would not load. `cp_drv_click` divides `(y - CP_DBY1)` by `CP_DROWH` rather
than laddering, so aiming at a band's middle is exact; and `cp_nitems` is a
**routine**, not a variable, so reading a byte at its symbol answers with an
opcode.
