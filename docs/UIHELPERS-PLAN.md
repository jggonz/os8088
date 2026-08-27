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

Net: `.text` −8, `.bss` +8, `.cold` +112 and the footprint
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

## 15. The DOWN state — LANDED, and the survey of what still cannot have one

SPEC.md §13.8. §13.2 predicted this exact shape — *"a feature added to the
button — **a pressed state**, a focus ring, a different disabled treatment —
lands in the Standard File dialog, the Control Panel, the Timer and every
package at once"* — and this is that prediction being cashed, so it is worth
recording how much of it came true and how much did not.

**It came true for the drawing and not for the wiring.** `OS88UI_DOWN` is
about a dozen bytes in one file and every caller of `os88ui_btn` can now draw
a pressed button. What almost none of them can do is *know when to*, because
a down state is not a property of a painter — it is the middle of a gesture,
and a control that acts on the **press** has no middle. That is the whole of
the survey below.

### 15.1 What was built

| | |
|---|---|
| `OS88UI_DOWN` | `os88ui_btn` paints interior black, label white, frame unchanged. Implies the fill; **disabled wins** |
| `OS88UI_GDOWN` | the same for `os88ui_glyph`'s check box and radio button — bit 2 of `AL`, beside the index, because `AH` is the disabled byte and the index is masked to two bits |
| `os88ui_gdn` | the predicate both halves of the glyph ask, so the box and the picture cannot disagree (which is white-on-white, i.e. invisible) |
| `wm_chrome_rect` / `wm_chrome_relit` | §13.8.1's kernel half: the close and minimize boxes, `.cold` behind two resident thunks |
| `ui_lit_on` / `ui_lit_off` / `ui_arm_trk` | the tracking, in the UI task's existing deferred pass |

### 15.2 Drawn, not XOR-ed — and Calculator is why

The obvious build is `os88ui_bflip`: one `gfx_xor_fill` of the interior, its
own inverse, ~3 ms cheaper an edge at PERFORMANCE.md Part 2's floor. It is
wrong, and the evidence was already in the tree twice:

- **`cal_onup`** put its control back by **redrawing** rather than by a second
  XOR, and its comment says exactly why: a repaint arriving between the two
  edges takes the inversion off the glass, and the release then inverts a
  control that was already upright and leaves it that way for the session.
- **SPEC.md §65.4** is the same finding with a measurement attached: a history
  row XOR-ed under a resting pointer came back with **the mouse arrow's top
  three rows missing**, because those three fell inside the XOR'd rect and the
  rest of the arrow did not.

So the state is drawn and therefore idempotent: a caller holding *this control
is down* passes the flag from its `W_PAINT` as well, and every repaint agrees
with the glass by construction. Calculator's keypad lost `cal_invert`'s
`OSAPI_GFX_XOR_FILL` for the flag; its history rows keep the two-colour
`font_run` they already had, which is the same idea in the primitive that
already took two colours.

### 15.3 The asymmetry that WAS here — closed by `W_ONDRAG` (SPEC.md 13.8.2)

> **SUPERSEDED, one round later.** This section said a package's down state
> was necessarily static and that the fix "should not be built until a
> package wants it". The chrome's tracking turned out to be worth much more
> than the down state it was built for — a close box going back up as the
> pointer slides off is what tells you the gesture is cancelled *before* you
> commit — so it was built: `W_ONDRAG`, API slot **0x0430**, `WIN_SIZE` 28 →
> 30, delivered from `ui_arm_trk` through the same `ui_ptcall` as
> `W_ONCLICK` and `W_ONMOUSEUP`. Calculator is the reference consumer
> (SPEC.md 65.8) and `tools/btndown.py` gates it.
>
> Two things from the original reasoning survived intact and are the rules to
> keep: **only when the point CHANGED** (a resting finger would otherwise wake
> the package eighteen times a second, which is the flicker the down state
> exists to avoid arriving by another door), and **the kernel decides nothing
> about identity** — it says "the pointer is here and your press is live", and
> which control that is remains the package's, there being no widget layer to
> ask.
>
> The original text follows.

#### 15.3.1 The original: the kernel can track, a package cannot

**This is the honest limitation and it is not a bug to be fixed by trying
harder.** docs/MOUSEUP-PLAN.md §7 refused a pressed state because a static one
*"stays lit while the pointer slides off… which says the opposite of what is
true"*. §13.8.1 answers that for the **chrome**, by sampling the pointer once
per UI pass — the third option that document did not consider, and the one
that costs neither a tracking loop nor the keyboard mouse's second keypress.

A **package** has no such pass. `W_ONCLICK` and `W_ONMOUSEUP` are the only two
edges it is given, and §13.7 forbids mixing them with a polling loop, so a
package's down state is necessarily the static one: it appears on the press
and is taken off on the release, wherever that release lands. What is lost is
only the *preview* of the cancel — the user still cancels by sliding off, and
the control still pops up and does nothing.

That was already the shipped behaviour (Calculator has done exactly this since
it was written) and it is accepted here rather than worked around. **The fix,
if it is ever wanted, is a kernel one**: a third edge — *the pointer moved
while your press was armed* — delivered to `W_ONMOUSEUP`'s owner. It is one
more word in the record and one more `ui_ptcall` from `ui_arm_trk`, and it
should not be built until a package wants it.

### 15.4 THE SURVEY: what is not showing a down state, and why

Every caller of `os88ui_btn` and `os88ui_glyph`, and every hand-rolled control
that is not one. **The blocker is nearly never the drawing.**

**A. Has the pair, has the state — done.**

| | |
|---|---|
| Calculator (`apps/calc`) | keypad converted off its private XOR; the header strip and history rows keep their own inversions, which are correct for what they are |
| Recorder (`apps/recorder`) | had the pair and drew nothing; `[rc_down]` is read by `rc_draw_btns`, so a repaint mid-press is consistent |
| the close and minimize boxes | §13.8.1, and the only tracked one |

**B. On the standard control, acting on the PRESS — the conversion is
`W_ONMOUSEUP`, and only then the flag.** These are ordered by how much a
user would notice:

| | controls | note |
|---|---|---|
| `kernel/fdlg.inc` | Open/Save, Cancel, Drive, New Folder | the most-used buttons in the system; §38's dialog is modal, so a press/release pair here has no re-entrancy question at all |
| `kernel/ctrl.inc` | the page buttons and **15 `os88ui_glyph` calls** | the radios and checks are where `OS88UI_GDOWN` earns its keep; note §31.8 — a page must still not write on a click |
| ~~`drivers/hdd/*`~~ | Mount/Unmount, Format, Install, the `+`/`-` pair, and the tool and installer windows' six | **DONE, §22** — SPEC.md §13.8.4 |
| ~~`drivers/net/netui.inc`~~ | Connect/Disconnect | **DONE, §22**, and it is where the missing `OS88UI_FILL` was caught |
| `apps/word` | 2 buttons, 3 glyphs | also keeps **its own** hit test rather than `os88ui_bhit` |
| `apps/texpad` | 7 buttons | already uses `os88ui_bhit` throughout — the closest to ready |
| `apps/paint` | Apply | §42.7's fullscreen path polls its own input, so this one needs care |
| `apps/notepad` | 2 | the find panel's |
| `apps/piano` | 2 | plus `OS88UI_INK`'s three coloured song buttons |
| `apps/artful/atui.inc` | 2 | keeps `at_btnhit`, its own x/y/width test — the rect-pointer drift §13 exists to stop, still live here |
| `kernel/apps.inc` | the Timer's Start/Stop | one button, and the cheapest place to prove a kernel-side arm |

**C. Not on the standard control at all — convert the control first.**

| | |
|---|---|
| ~~`kernel/files.inc` — the Disk window's **Refresh** and **view toggle**~~ | **DONE, §21** — SPEC.md §22.18. Drawn from `[fm_rgt]`-relative constants at `fm_draw_core` and hit-tested from *content-relative* ones in `fm_hit`'s caller: two descriptions of one pair of buttons agreeing by hand, plus the width threshold written out twice in each. `fm_btab` is the single description now |
| **scroll bars** — ~~`files.inc`~~, `notepad`, `frotz/zwin.inc`, `artful/atrend.inc`, ~~`fdlg.inc`~~ | **the ELEMENT is built and both KERNEL bars are on it, §23** — SPEC.md §13.10. The three package bars are the remaining conversions, and each is opt-in through `OS88UI_SCROLL`. The arrow cells are still the part that wants a down state, and nothing has one yet |

**B2. Reported from the field/by eye, and queued.** Added after §20 and §21
landed, in the order they were reported:

| | |
|---|---|
| ~~`apps/recorder`~~ — **the BUG** | **FIXED, §24** — `OS88UI_FILL`, exactly as os88net's Connect. The guess in this row was right about *where* and wrong about *what*: the state was not being left set, the upright redraw was landing on a black interior |
| ~~`kernel/apps.inc` — the Timer~~ | **DONE, §24** — SPEC.md §13.8.5 |
| ~~`apps/paint` — Apply~~ | **DONE, §24**, and the bracket owes the release edge too (§13.8.7) |
| ~~`apps/piano`~~ | **DONE, §24**. The KEYS keep the press — a note is the safe prefix action §13.6 is about |
| ~~`apps/texpad` — the top row~~ | **DONE, §24**. Its two scroll-bar rects keep the press |

**D. Deliberately excluded, and staying that way.** §14.5's list is unchanged —
ModPlug's bevelled LED transport (§56), Tracker's FT2 bevel (§45),
Minesweeper's cell (§23). Converting those undoes intended design. To it add
the **content-space frame helpers** that a grep for `GFX_FRAME` turns up and
which are not controls at all: `sol_framec`, `ark_framec`, `mc_framec`,
`tg_framec`, `cy_framec`, `tui_framec`, and the Task Manager's graph, bar and
memory-map frames — the Task Manager is a viewer and has **no buttons at all**.

### 15.5 Cost

`.text` **+160**, `.cold` **+163**, `.bss` **+1** —
the footprint stays at 107,520 of 108,544, so the machine pays nothing yet.
It did cross one on the first build: the two `wm_chrome_*` routines went to
`.cold` behind resident thunks precisely to get back under it, `.text`'s rung
having had 204 bytes left against a feature that wanted 232.

**The number to carry forward is the image rung: 43 bytes left, was 204.**
The next addition to `.text` anywhere in the kernel very likely buys a whole
512-byte step, taking the footprint spare from two rungs to one. `.cold` has
158 left of its own. That is not this change's fault and it is not free
either; it is the next author's first fact.

### 15.6 The bug the gate caught, and it is a NEW shape

A cold body keeps an ordinary near `ret`, so a resident thunk must far-call a
**cold-side `call`/`retf` shim** (`gbz_`, `cpf_`, and now `wmz_`) and never
the body. Far-calling the body directly assembles clean, passes `-w+error`,
and **`tools/os88ovlchk.py` does not see it** — that tool refuses *near* calls
crossing a boundary, and this is a far call to a near-`ret` body, the same
mistake from the other side.

What it looked like from outside is worth writing down, because nothing in it
points at a stack: the desktop still moved its mouse (`mou_isr` is an ISR and
kept drawing the cursor), `[ui_armlit]` and `[ui_armw]` read back **perfectly**
over the debugger, and nothing was ever drawn, released or closed. The UI task
had `ret`ed to a computed address past `cold_end`, in the FAT window. Four
`CS:IP` samples found it in one run; no amount of reading the flags would
have.

### 15.7 Testing

Two gates, both MartyPC, both on **CGA, Hercules and VGA mode 12h**:

- **`tools/chromedown.py`** — ten cases, and every one of them holds the
  button down and looks at the glass *before* letting go, because a test that
  only presses and releases passes on the static invert that says the
  opposite of what is true. It ends with a completed gesture, since a kernel
  that had stopped closing windows would pass all nine cancels.
- **`tools/btndown.py`** — the package half, driven through Calculator (drive
  zone → `APPS` → `CALC.O88`). Its last case is the one that matters: a
  **cancelled** press leaves the content **pixel-identical**, which is what
  says the state is idempotent.

Three harness traps cost a round each and are written into both files:
`os88mouse`'s `to` defaults to `l=False` and so **releases** the button
mid-gesture; `m.sym()` answers a **linear** address, not `readseg`'s offset;
and MartyPC's *rendered* Hercules frame has alternating blank scan lines, so
a solid white box reads 44% lit and an inversion measures the same before and
after — the 1bpp adapters are read through `vram`.

## 16. The tracking edge — `W_ONDRAG` (SPEC.md 13.8.2)

§15.3's asymmetry, closed. The kernel could track because it has a UI pass; a
package had two edges and nothing between them. `W_ONDRAG` is that pass made
available: **the pointer moved while your press was armed**, `CX`/`DX`/`SI`,
`W_ONCLICK`'s environment exactly, through the same `ui_ptcall` so the three
callbacks cannot drift.

### 16.1 Why it is a separate record word

The cheap-looking build is to deliver drags to `W_ONMOUSEUP` with a flag in a
register, saving 24 bytes of `.bss`. It is wrong on the failure mode: a
package that ignored the flag would treat **every pointer movement as the
release** and fire its control on the way past. Two words is two contracts a
package opts into one at a time, and a package that installs neither is
untouched — which is every shipped package but Calculator.

`WIN_SIZE` 28 → 30. The `wm_create` template stays **eight words**: `W_ONDRAG`
is zeroed by `wm_create` and set by `wm_ondrag`, exactly as `W_ONSIZE` and
`W_ONMOUSEUP` are, so no shipped `.o88`'s template changed.

### 16.2 What makes it affordable

Two compares per pass when nothing is armed — the same argument as its
neighbours `ui_arm_chk` and `ui_arm_trk` — and **nothing at all when the
pointer has not moved**. That second test is not an optimisation: without it a
finger resting on a held button wakes the package eighteen times a second, and
a handler that redraws is tens of milliseconds of repaint per call on a
4.77MHz machine. The point is banked *before* the dispatch, so a handler that
takes longer than a mouse packet does not come straight back on the same
point.

`[ui_dragx]`/`[ui_dragy]` are seeded at the press with the point `W_ONCLICK`
was **given**, so a package's first `W_ONDRAG` is the first place the pointer
is not.

### 16.3 The guard that is the only one

`.mup_pkg`'s `W_FLAGS` visible-bit test is documented (SPEC.md 13.7) as the
only thing between the release path and a dispatch through a freed record,
because that path deliberately has no `wm_hit` in front of it. **The drag path
is identical in that respect** and carries the same test for the same reason:
a package worker reaching `inst_task_die`, or a second window destroyed inside
`W_ONCLICK`, and `wm_destroy` clears `W_ONDRAG` no more than it clears
`W_ONMOUSEUP`.

### 16.4 Cost — and this one CROSSED

`.text` **+77**, `.bss` **+29** (24 of it the record word × `MAX_WIN`, 4 the
last point, 1 spare), `.cold` +0. **The image rung crossed**: `KERN_SIZE`
107,520 → **108,032 of 108,544, so the footprint spare is 512 — ONE step,
where it was two.**

That is the honest number and it is the tightest this kernel has been. It is
not avoidable by shuffling sections: the rung is `.text` + `.bss`, `.bss`
cannot move to `.cold`, and the 43 bytes of rung left after §15 were already
less than the record word alone. Moving `ui_lit_*` cold would have bought
about 63 bytes against a 105-byte cost — a one-byte margin, which is not a
margin.

**The next author's first fact, and it is now a decision rather than a
warning.** §15.4's conversion list is a dozen call sites that each want a
press/release pair, and the scroll-bar element is a second shared control.
Both are kernel-side. At one step of spare, `KERN_BUDGET` should be raised
before that work rather than during it — a raise is a conversation with
whoever owns the machine (docs/KERNEL-MEMORY.md), and it has been had eleven
times.

### 16.5 The harness lesson, and it worked

`tools/os88geom.py` exists because nine scripts once kept their own copy of
`WIN_SIZE` and the kernel moved under them. It **caught this one on the next
run** — a `GeomError` naming the old and new values — and the six files that
still had a local `28` are now reading it from that one place. `MBAR_H` has
three stale copies (`tests/dispcorner.py`, `dispfsx.py`, `dispmcfs.py`) which
predate this work and are left alone.

## 17. The one-shot timer — `W_ONTIMER` (SPEC.md 13.9)

The third piece, and the one that is not about the mouse at all. §16 gave a
package the middle of a *mouse* gesture; a control pressed from the
**keyboard** has no middle and no release edge, so nothing could ever put it
back up. A callback cannot sleep — it holds the gfx lock — so before this the
only answer was a worker task: a slot of twelve, a 256-byte stack, and the
task-owned close path, for a 165 ms flash.

`OSAPI_WM_TIMER` (BX = window, AX = ticks, 0 cancels) and `OSAPI_WM_ONTIMER`
are ~150 bytes of kernel that serve Calculator's key flash today, and
ArtfulType's caret blink and every settle-after-typing worker whenever those
are revisited. **Note Pad's caret does not blink** — this list named it and
should not have; SPEC.md 13.9 carries what it would cost and why it is still
a static bar.

### 17.1 Four decisions worth keeping

**Per-window, not one global slot.** The global was the first design and is
six bytes against forty-eight. It fails the moment two apps want a timer —
and worse, **a blinking caret holds its timer for ever**, so one ArtfulType
moved onto it would have locked every other app out of the facility
permanently.

**`[wm_tarm]` is a self-healing flag, not a count.** One compare per UI pass
on a machine with nothing armed; a twelve-window walk only when something
might be. A count would have to be right at an arm, at a cancel, at a fire and
at a `wm_destroy` — four places, one of which is not the timer's code — while
the flag is set only by arming and cleared only by the walk, so **stale costs
one wasted walk and missing is unreachable**. That is the direction that
matters.

**The flag is also its own accumulator inside the walk**, which is what keeps
a live value out of a register across the dispatch: `ui_ptcall` clobbers AX,
BX and DI and takes CX/DX as the point, and the loop already needs CX and SI
pushed around the callback. A handler that re-arms calls `wm_timer`, which
sets the flag itself — so the re-arm needs no test at all.

**USED, not visible.** A minimized app's timer still fires. The two failure
modes are not symmetric: a visible-gate stalls a background job silently and
leaves nothing pointing at why, where a used-gate costs one callback per
interval to an app that can ask `OSAPI_WM_GEOM` itself — §44.8's rule, the
kernel delivers and the package decides. `wm_destroy` writes `W_FLAGS` = 0, so
a dead record is skipped before its handler word is read and a freed package
segment cannot be dispatched into.

### 17.2 The trap Calculator sprang, and it generalises

`cal_ontimer` clears the down state **only when `os88ui_armed` answers 0**.
Without that: type a key, then press a mouse button on the pad inside the
165 ms window, and the expiring timer springs the *held* key up under a finger
that is still down. It is §13.8.2's defect arriving from the clock instead of
from a repaint, and the rule is worth stating generally — **a timed un-draw
must not outrank a gesture that is still running.**

Two behaviours fall out of the timer being one-shot and per-window rather than
being arranged: typematic **re-arms** rather than stacking, so a held key stays
lit and goes up `CAL_FLASH` ticks after the last repeat; and a key pressed
during another key's flash simply moves the state, because there is only one
deadline to move.

### 17.3 Cost

`.text` **+153**, `.bss` **+49** (48 the two record words × `MAX_WIN`, 1 the
flag), `.cold` +0 — **no new rung crossed**, the step §16 bought absorbing it.
`KERN_SIZE` stays 108,032 of 108,544 at **512 spare, one step**, and the image
rung has **248 bytes left**.

`WIN_SIZE` 30 → 34, and `tools/os88geom.py` caught it on the next run for the
second time in two commits.

## 18. The split — button interactions are kern_big's alone

`KERN_BUDGET`'s **twenty-fifth move, 108,544 → 109,568**, 1KB, asked for and
granted, attributed to **button interactions**. Its terms are narrower than
the four-step standard on purpose: if the remaining conversion work does not
fit in it, the work is reconsidered rather than the figure raised again.
kern_big lands at **1,536 spare, three steps**.

And from that move on this family is **kern_big's alone**. kern_small is the
guard the 128KB machine lives under and it cannot carry the niceties.

### 18.1 What is split, and how

| | |
|---|---|
| the three window-record words | `W_ONDRAG`, `W_ONTIMER`, `W_TIMER` — absent, so `WIN_SIZE` is **34** on big and **28** on small |
| `wm_ondrag` / `wm_timer` / `wm_ontimer` | absent |
| `wm_chrome_rect` / `wm_chrome_relit` and their cold shims | absent, so `wm_draw_title` drops one call |
| `ui_lit_*` / `ui_arm_trk` / `ui_timer_pass`, `[ui_armlit]`, `[ui_dragx/y]`, `[wm_tarm]` | absent, and the four call sites in the ladder with them |
| **the three API slots** | **present in BOTH**, refusing with CF=1 on small |

**Differing `WIN_SIZE` is safe because a window INDEX never leaves the
kernel** — a package is handed a pointer, and nothing outside `kernel/` strides
the table. Checked rather than assumed: no package names `WIN_SIZE` at all.

**The slots stay in both builds and that is not negotiable.** A slot number
that exists in one build and not another is an ABI that depends on a knob
(SPEC.md §20.8 rule 4); `gfx_blit1` is the precedent and answers CF=1 on small
for exactly this reason. A package tests CF and does without — no tracking, no
timer, and a static down state.

### 18.2 What it costs kern_small, measured — and the number needs a decision

The split removes everything it can. What is left is **the three API cells: 24
bytes of table plus one shared `stc`/`ret`** (three labels on one body — a
refusal has nothing to say about which of the three it is refusing).

**26 bytes, and kern_small's image rung had THREE left.** Measured at the
branch point in a worktree rather than inferred: `KERN_SIZE` **102,912 of
103,424 — 512 spare, 3 bytes of rung**. It is now **103,424 of 103,424, zero
spare**, with 489 bytes of rung headroom.

So kern_small was already one byte from a build failure before any of this,
and the 512 bytes it has spent buy the ABI floor of keeping **one SDK** rather
than any feature. It still builds and can absorb 489 more bytes of `.text`
before it fails — but it has no footprint spare at all, and **`all` never
builds it**, so the next author will find this by breaking it.

Three ways out, and the choice is not the implementer's:

1. **Raise kern_small by its standing 1KB unit.** It buys the ABI floor, not
   a nicety, which is the distinction that guard exists to police.
2. **Drop the three cells from kern_small** and let `osapi_table_end` differ.
   It is the last three slots, so nothing renumbers — but a kern_small
   package calling one lands past the table, and "undefined" is exactly what
   §20.8 rule 4 exists to prevent.
3. **Reclaim 26 bytes of kern_small `.text` elsewhere.** Real, and unrelated
   to this work.

Recommended: (1). (2) trades 512 bytes of a small machine's RAM for a class of
bug that assembles cleanly and jumps into data.

**RESOLVED — (1), and by 512 rather than 1KB.** `KERN_SMALL_BUDGET`'s
**eighteenth move, 103,424 → 103,936**, asked for and granted: *half* the 1KB
unit the standing rule sets, and the smallest that figure has ever moved by.
The grant's own words are worth keeping in the reader's way — **this kernel is
already too big and wants TRIMMING work**, which is a future thing, and until
it happens the guard is raised by the least that keeps the build alive.

**And it needed one more split to be worth anything.** Making
`os88ui_arm`/`fire`/`armed`/`bfind` unconditional (§19.1) cost kern_small a
whole rung on its own — measured, 103,424 → 103,936 with nothing else changed
— so the 512 would have been spent the moment it was granted. Those four are
gated on `OS88UI_ARM` now: every package always, the kernel only in the big
build. kern_small is back to **103,424 of 103,936, 512 spare, one step**.

Its **cold rung has 33 bytes left**, which is the number the trimming work
should start from.

## 19. The Standard File dialog fires on the release (SPEC.md 13.8.3)

§15.4's list, first entry, and the one to take first for the reason given
there: the most-used buttons in the system, and **modal** (SPEC.md §38.2), so
a press/release pair has no re-entrancy question at all.

Before this a press on `Cancel` fired Cancel. There was no way to think better
of a mis-aimed press — which is exactly what §13.6 says a control with no safe
prefix action wants the release for.

### 19.1 The kernel-side arm moved, it was not re-written

`os88ui.inc`'s arm block carried a note saying that the day a kernel dialog
wanted a release-fired button, *that gate* would move rather than a second
copy being written. It moved: `os88ui_arm`, `os88ui_fire`, `os88ui_armed` and
`os88ui_bfind` are unconditional now.

**Only the word differs, and it has to.** A package keeps its inline `dw 0`;
the kernel's `os88ui_armw` is in **`.bss`**, because this file is assembled
into `.cold` and a word declared there and reached through `DS` is not that
word (§2.6 rule 1) — the trap `os88ui_krect`'s own comment records costing
eight bytes through the middle of `sch_isr`.

### 19.2 The conversion is a HIT TEST before it is an event

`fdlg_onclick`'s button column was a ladder of inline coordinates, which can
answer *did this point hit a button* for one caller and cannot answer *which
button is this point in* for three. `fdlg_bhit` is that routine, and the three
edges read it: the press arms and draws down, `fdlg_ondrag` tracks, and
`fdlg_onup` re-asks and fires only on a match.

That is §22's `fm_hit` discipline arriving where §20.5.1's rect pointer could
not reach — these four buttons share an x span and differ only in y, so a
shared rect table would be four copies of the same two columns.

**And `New Folder` is not in `fdlg_bhit` in Open mode**, because it is not
drawn there (§38.3). The drawn control and the clickable control being the
same control is the whole point of having one routine.

### 19.3 One painter, so a repaint cannot disagree

`fdlg_btn1` draws button *n* down or up as `[fdlg_down]` says, and it is what
`fdlg_paint`'s column calls too — four `mov ax, n` / `call` where the four
buttons used to be open-coded a second time. Without that, a repaint arriving
mid-gesture draws the held button upright and the release then "restores"
something already up (§13.8.2's rule, which is the kernel's here as much as a
package's).

`fdlg_setdown` is the one writer, Calculator's `cal_setdown` exactly, and it
draws nothing when the state has not changed — which is what makes the
tracking edge affordable.

**The release puts the button up FIRST and unconditionally**, and here that
has teeth rather than being tidy: `fdlg_act` and `fdlg_close` both **destroy
this window**, so a restore after the action would draw into a dead one.

### 19.4 Two things the two-line button needed

`New / Folder` is `FD_BH2` tall and drawn by hand — frame plus two
`fdlg_blabel`s — because `os88ui_btn` centres **one** label in both axes. So
its down state is written out in `fdlg_btn2`: fill the interior black, frame,
both captions white. Same picture as the flag produces, same idempotence, and
it takes an `AL` flag word now where it took none.

`fdlg_defbtn` reads `[fdlg_down]` itself rather than being passed it, because
it has its own flag word to build (the `OS88UI_DEF` ring and `fdlg_actok`'s
greying) — and `os88ui_btn` settles the precedence: **`DIS` outranks `DOWN`**,
so a button that is going to refuse never says "pressed".

### 19.5 Cost and testing

`.text` +67, `.cold` +518, one cold rung — `KERN_SIZE` 108,032 → **108,544 of
109,568, 1,024 spare (two steps)** of the 1KB granted. The image rung has 212
bytes left and the cold rung 326.

`tests/fdlgup.py` is the gate, five cases on CGA, Hercules and VGA mode 12h,
and the shape is `tests/mouseup.py`'s — the signal is read out of the window
table wherever it can be, so a pass is a memory read rather than a picture.
The case that matters is **press-and-release ON Cancel still closes the
dialog**: a build that had simply stopped dispatching would pass the other
four. `tests/fdlggrey.py` still passes, which is the control that says the
greying did not move under the down state.

## 20. The Control Panel — BUILT (the firing half)

> **STATUS: BUILT, both halves — it fires on the release and it draws the
> pressed control.** Everything below was written as a design and is kept as
> written, because all of it survived contact — the probe mechanism, the twelve
> ids, the `CPE_*` entries and the `os88ui_*_f` far entries are what got built.
> Two things the design did not foresee are §20.7's page-painter trap and
> §20.8's gate.

## 20.0 The original design

§15.4's second entry, and the biggest. It is written up here rather than
started because the shape was worth establishing before any code was cut, and
because two parts of it are riskier than anything in §19.

**What is there today**: `cp_onclick_x` is one dispatch over five page
handlers — `cp_sched_click`, `cp_vid_click`, `cp_time_click`, `cp_snd_click`,
`cp_drv_click` — each an inline coordinate ladder that ACTS where it lands.

### 20.1 The cheap answer is a PROBE, not five hit/act splits

§19.2's route was to split the dialog's ladder into a hit test and an action.
Doing that five times is ~150 lines and, worse, **five second descriptions of
geometry** that then have to agree with the painters — the exact drift
`fm_hit` discipline exists to stop, reintroduced by the fix for it.

The cheaper and more honest mechanism keeps the ladders as the single
description and makes them answer a question instead:

```
[cp_probe]  byte   1 = identify only, 0 = act
[cp_ctlid]  byte   what the ladder resolved to (0 = none)

cp_ctl:                         ; AL = this site's control id
    mov [cp_ctlid], al
    cmp byte [cp_probe], 0
    je .act
    stc                         ; probing: the caller returns without acting
    ret
.act:
    clc
    ret
```

Every ACTION site in the five ladders gains three instructions —
`mov al, <id>` / `call cp_ctl` / `jc .done` — about seven bytes each. The
press runs the ladder with `[cp_probe]` set and arms `[cp_ctlid]`; the release
runs it again and, on a match, lets it act; `W_ONDRAG` runs it and moves the
down state.

**The page cannot change between the two edges**, because changing it needs a
click in the list and the button is already down — so the id alone identifies,
with no need to bank `[cp_sel]`.

### 20.2 Which controls convert, and which must not

§13.6's rule decides it, not uniformity. **A control with a safe prefix action
keeps the press**: the item list's rows (selecting a page IS the prefix) and
the Time page's field rows (same). Everything else converts —

| page | ids |
|---|---|
| Scheduler | the two mode rows |
| Display | `Activate`, and the three desktop-arrangement radios |
| Date/Time | the `+` and `-` buttons, the two option check boxes |
| Sound | `Test`, and the three tier rows |
| Drivers | one per driver row |

### 20.3 The two risky parts, and they are not the ladders

**`ctrl.inc` is an ON-DEMAND MODULE** (§2.8) with a CS of its own, reached
through an entry-point table in its image: `cpf_cp_onclick` is a resident cold
shim that far-calls `[CPFP + CPE_ONCLICK*4]`. So `W_ONMOUSEUP` and `W_ONDRAG`
need **two new `CPE_*` entries**, which is a versioned interface between the
resident kernel and a separately-loaded image — get the index wrong and the
panel far-calls into its own data. That is the part to build first and test
alone.

And the module reaches `os88ui.inc` through `COLD_SEG:` far entries
(`os88ui_btn_f`, `os88ui_glyph_f`), so the arm needs **`os88ui_arm_f`,
`os88ui_fire_f` and `os88ui_armed_f`** beside them.

### 20.4 The down state is the second half, and it is per painter

Firing on the release is the behaviour; drawing the control pressed is the
polish, and it costs more because **each page paints its own glyphs**
(`cp_radios`, `cp_snd_radios`, `cp_timechk`, the Display radios, the Drivers
rows). Each site consults `[cp_down]` and ORs `OS88UI_GDOWN` or
`OS88UI_DOWN` — about eight bytes each across ~15 sites.

`OS88UI_GDOWN` already exists and is unused: this is its first consumer, and
the check box and radio behaving like the button across the whole system is
the point of it.

### 20.5 Budget — this is where the 1KB gets decided

Estimated: `cp_ctl` ~15, twelve call sites ~85, the two `CPE_*` entries with
their shims and thunks ~40, the probe/arm/fire dispatch ~120, the three far
entries ~12, the down state ~120. **~400 bytes**, against **1,024 spare**.

It fits, but it is the largest single item left and it lands on a guard whose
grant was explicitly *"if the remaining steps take more than this we will
reconsider"*. The scroll-bar element (§15.4 C) is still after it and has not
been sized at all.

### 20.6 What must not regress

§31.8: **a page still may not write `SYSTEM.CFG` on a click** — only
`cp_flush_close` does. Moving the action from the press to the release does
not change which edge writes, but it does mean a page's `[cp_wdirty] = 1` now
happens on the release, and any test that ticks a driver and then resets the
machine from outside was already measuring nothing (CLAUDE.md's standing
note).

`tests/dispcp.py` and `tests/hddcp.py` are the existing gates and are the
regressions to run; a new `tests/cpup.py` in `tests/fdlgup.py`'s shape is the
gate for the conversion itself.


### 20.7 The pressed LOOK — BUILT, and the page painter is not what draws it

`cp_page` is the one routine that redraws a page and it is the wrong one:
it **erases the pane first**, being the redraw path for a selection *move*,
so a press and every slide across a control while held would white a 200x140
pane and letter it again — **PERFORMANCE.md's double-draw flash, introduced by
the feature whose whole argument is that it does not flash** (§15.2). Not a
trade worth 165 ms of pressed look on a 4.77MHz machine.

So `cp_ctlpaint` is a switch on `[cp_sel]` onto a per-page **control** redraw,
and two of the five pages already had one — `cp_radios`, `cp_snd_radios` and
`cp_timechk` each redraw their glyphs *only*, and their own comments say they
double as the redraw path after a change. What was added is the three that did
not: the Display page's arrangement radios, the Date/Time `+`/`-` pair, and
the Drivers rows.

The flag reaches the drawing through **two entries, not one**, because the two
controls take their flags in different registers (§20.5.1's asymmetry):
`cp_dnbtn` ORs `OS88UI_DOWN` into `DI` for a button and `cp_dngly` ORs
`OS88UI_GDOWN` into `AL` for a glyph — which is where `OS88UI_GDOWN` gets its
first consumer. The glyph's **id arrives in `BH`** for the reason its header
gives: `AL` is the argument, so it cannot also be the question.

Three things hold it up.

**`cp_setdown` is `[cp_down]`'s one writer and it draws only on a CHANGE**, so
a pointer moving *inside* the control it pressed costs one compare and no
pixels — which is what makes an edge arriving per mouse packet affordable at
all.

**Every painter asks, not just the edges.** `cp_isdown` is called from all
twelve drawing sites, so what is on the glass follows `[cp_down]` on a *full
page paint* as well as on a press — the drawn-state rule (SPEC.md §13.8) doing
what XOR could not.

**And the release edge is not the only one that has to put it back up.**
`cp_ondrag_x` re-runs the probe every packet and calls `cp_setdown` with 0 when
the pointer has left, which is the tracking half (§13.8.1). It shipped once
with the `cp_setdown` call missing — the ladder resolved correctly, `[cp_down]`
was correct, and nothing ever redrew — so the control stayed pressed until the
release. `tests/cpup.py`'s *"…and it comes back UP when the pointer slides
off"* is that case and it was the only one of the four that failed.

**kern_small emits none of it.** All nine drawing sites are `%ifdef KERN_BIG`
around the id *and* the call, so small does not even pay the register write —
which is the shape the user asked for, not a stub that returns.

### 20.8 The bug the conversion exposed, and it is general

**A gate that lived in the DISPATCH has to move to the probe.** `cp_onclick_x`
tested `cx >= CP_DIVX` before it ever reached a page, so no ladder tests its
own x unless it has two columns — `cp_sched_click` tests the y and nothing
else. Sound while the only caller was a click inside the window; **not** sound
once a release can land anywhere on the screen (SPEC.md §13.7).

Measured before the gate was added: press a mode row, slide left into the item
list, release — **the mode changed**. `tests/cpup.py`'s first case is exactly
that gesture, and it caught it on the first run.

The rule for every remaining conversion in §15.4: **ask what the old caller
had already established before the ladder ran**, because the release path
establishes none of it.

### 20.9 What it cost, and the two ABI changes

`MOD_NENT` **4 → 8** (`MODFP_SHIFT` 4 → 5): the module far-pointer stride is a
power of two because `mod_fpi` shifts by it while each module's base `equ`
multiplies, and the assertion between them has caught those disagreeing once
already. +32 bytes of `.bss`, 8 bytes of each module *file*, and every module
rebuilt — which a kernel change already forces, `MOD_H_BUILD` being the commit
count.

**`tools/os88mod.py` had its own bare `4`** and failed the build with a message
naming a constant it did not have (`6 entries, which is outside 1..MOD_NENT`).
It reads `MOD_NENT` out of `kernel/mod.inc` now — `os88geom.py`'s lesson in a
second place.

**`mod_live` is new**: `mod_need` without the loading. `cpf_cp_onup` and
`cpf_cp_ondrag` use it because a release or a drag can only follow a press the
panel itself took, so "not resident" means the arm is stale and there is
nothing to fire — and on the drag path `mod_need` would try to load the Control
Panel **off a floppy eighteen times a second**.

`cp_kinit` is the panel's first `KD_INIT` (a stateless task-less singleton
needed none): it installs the two edges, which are not template words, and
clears the arm — which the file dialog does not need because it is modal, and
this window is an instance closable from the dock, the bar and its own box.

Cost: `KERN_SIZE` unchanged at **108,544 of 109,568, 1,024 spare (two
steps)**. The image rung has **137 bytes left** and the cold rung 277. So of
the 1KB granted for button interactions, the file dialog and the Control Panel
between them have spent one 512-byte step.

**The pressed look cost `KERN_BUDGET` nothing at all**, which is worth stating
because it is a property of *where the code lives* rather than of how small it
is: `cp_ctlpaint`, `cp_setdown`, `cp_isdown`, `cp_dnbtn`, `cp_dngly`, the three
new per-page control painters and all twelve call sites are in **`section
.modc`** — the on-demand module's own image (SPEC.md §2.8), which is a file on
the disk and not kernel RAM. `CTRL.DRV` is 3,681 bytes and 8 sectors. What it
*did* spend is the ~150 bytes the user allowed, in a place the budget does not
measure; the two edges' resident thunks were already paid for by the firing
half.

### 20.10 Testing

`tests/cpup.py`, eight cases on CGA, Hercules and VGA mode 12h, driving the
**Scheduler** page because its whole effect is one kernel byte (`[sch_coop]`)
— so a pass is a memory read rather than a picture. The case that matters is
that press-and-release on the row still *changes the mode*: a panel that had
stopped dispatching would pass the two cancels.

Three of the eight are the pressed LOOK and those cannot be memory reads, so
they measure the two 12x12 glyph boxes and **nothing else** — the page's
labels and captions cannot be mistaken for the control, which is
PERFORMANCE.md Part 3.1's own trap. **1bpp reads `m.vram()` and mode 12h reads
`m.fbuf()`**, and neither can stand in for the other: MartyPC's *rendered*
Hercules frame has alternating blank scanlines, so a solid white box there
measures ~44% lit and an inversion measures **identical** either side of it.
Reading VGA out of VRAM is not an option at all — four planes behind the
Graphics Controller. A first draft skipped the three cases on VGA for that
reason and the skip was silent, which is the wrong shape: a third renderer
that nothing asserts about.

**A kern_small check has to use no symbols at all**, which is worth writing
down because it is not obvious and it cost a confused run here:
`os88marty.sym` re-assembles `kernel/kernel.asm` — the *big* kernel, whose
byte-identity it then asserts against `build/kernel.bin` — so pointed at a
small image it answers confidently about a kernel that is not running, and
`tools/os88geom.py`'s `WIN_SIZE` is big's 34 against small's 28 for the same
reason. The screen is the only honest instrument on that build. What it has to
answer is one question: the panel still opens, draws and acts, so none of the
nine `%ifdef`s removed a register write a caller wanted.

It is also the first test of the module's entry table growing, and that is a
real assertion rather than a formality — a wrong `CPE_*` index does not fault,
it far-calls into the module's own data (the `modc_e_paint` note records what
that looked like: a Control Panel painted over the desktop, once a minute,
with no window and no error).

`tests/hddcp.py` is the other one worth running, because it ticks a **Drivers
row** — a converted control — and then opens the tool and installer windows
behind it: end-to-end proof that a driver still loads from a release.

## 21. The Disk window's two header buttons — BUILT

> **STATUS: BUILT.** §15.4 C's first entry. SPEC.md §22.18 is the record; this
> is what it cost and what it taught.

`Refresh` and the view toggle were the last two controls in the kernel's own
UI drawn by hand — a `gfx_frame` and a `font_str` with the label pen worked out
on paper — and they acted on the press. Both halves are fixed and they are
separate problems.

### 21.1 The geometry was described twice, in two coordinate systems

`fm_draw_core` measured back from `[fm_rgt]` (absolute) and `fm_onclick_x`
forward from `[fm_cw]` (content-relative), for one rectangle — **and each of
them wrote out the drawn-only-if-it-fits threshold on its own** (`76` and
`142`, twice each). That is §22's `fm_hit` discipline broken inside
`files.inc`, and the failure it invites never announces itself: move a button
and the click keeps landing where it used to.

`fm_btab` is the single description — two words per button, the minimum
content width and the offset back from `[fm_rgt]` — and everything else is
derived. `fm_brect` answers `CF = 1` for a button too narrow to be drawn, so
*"nothing invisible is ever clickable"* is one test rather than two `cmp`
pairs carrying the same literal, and the label's pen is `os88ui_btn`'s
centring rather than the three hand-computed constants (`+3`, `+11`, `+15`)
that had to agree with the captions by hand.

**The label moves one pixel down and that is a fix**: the frame is 14 rows and
a glyph cell is 8, so centred is 3 rows of clearance each side; the
hand-written pen had 2 above and 4 below.

### 21.2 Two words of down state, not one byte

The Control Panel needed one byte because there is one panel. **There can be
four Disk windows.** `[fm_dbtn]` is which button and `[fm_dvp]` is whose, and
`fm_isdown` asks both — without the second, a damage repaint of a *different*
Disk window behind the drag draws the same button pressed in a window nobody
has touched.

The edges are installed in `fm_kinit`, because `W_ONMOUSEUP` and `W_ONDRAG`
are not template words — and that is also where `[fm_dbtn]` is cleared,
because a `KD_POOL` block is REUSED (§29.3) and a window closed mid-gesture
must not hand a pressed button to the next window in its slot.

**The calls are NEAR.** `files.inc` and `os88ui.inc` are both `.cold` and
share a segment, so `os88ui_btn`, `os88ui_bhit`, `os88ui_arm`, `os88ui_fire`
and `os88ui_armed` are reached with ordinary near calls — where `ctrl.inc`,
being `.modc`, has to far-call every one.

### 21.3 Cost

`.text` **+23**, `.cold` **+247**, `.bss` +3: the
footprint stays at **108,544 of 109,568, 1,024 spare (two steps)**. It is
cheaper than it looks because `os88ui_btn` does the frame *and* the centring,
so what was added is mostly the table and the two edges, not a painter.

**The numbers to carry forward are the rungs: the image has 114 bytes left and
the cold rung 97**, against 137 and 277 before. The next addition to either
buys a whole 512-byte step and takes the footprint spare from two rungs to
one. That is the next author's first fact, and it is the same warning §15.5
gave one round earlier.

kern_small keeps the hand-drawn pair and the press — the shared control's body
is `KERN_BIG`'s — so its two painters sit behind one `%ifdef` and it is
byte-identical at **103,424 of 103,936**.

### 21.4 Two bugs, and the second one is the lesson

**`fm_brect` clobbers `AX` and `fm_btn1` read the id after calling it.** The
id then chose the label and answered `fm_isdown`, both from garbage — so the
down state did not draw at all, on either button. `fm_bhit` had the `push
ax`/`pop ax` and `fm_btn1` did not.

**The gate PASSED against that build**, and that is worth more than the bug.
`a press draws the toggle DOWN` compared lit-pixel counts over the whole
button rect and asked only that they *differ*: pressing moved 15 pixels — **the
mouse arrow, parked on the button because that is where the press is** — and
15 ≠ 0, so it passed. So did *"comes back up when the pointer slides off"*,
because sliding off took the arrow with it. Two of the eleven cases were
measuring the pointer.

What found it was rendering the button as ASCII and *looking at it*: a
pressed 63x14 button should be a black field with a white caption, and it was
white with a black caption and a small diagonal artifact in the corner. The
assertion is now over the **interior** and is a *halving* rather than a
difference — ~700 lit upright against ~40 pressed, where the arrow is 15.

That is PERFORMANCE.md Part 3.1's own trap in a new place: *a count that
cannot tell the thing under test from the pointer sitting on it is not a
measurement of the thing under test.* Part 3.1 records 42 pixels of "text
flash" that turned out to be the mouse blinking under the gfx lock; this is
the same error with the sign reversed — the pointer manufacturing a pass
rather than a defect.

### 21.5 Testing

`tests/fmbtn.py`, eleven cases on CGA, Hercules and VGA mode 12h. The signal
for the firing cases is **`FS_VIEW`**, one bit of kernel memory rather than a
picture — `tests/mouseup.py`'s discipline. **Refresh is deliberately not the
button under test** there: a re-list of a folder that has not changed is
invisible in memory *and* on the glass, so a build that had stopped
dispatching would pass every case.

Three of the eleven are not about events at all. Two click one pixel inside
each corner of the **drawn** frame and require a fire; one clicks a pixel
outside and requires none — which is §21.1's claim tested rather than
asserted. Two more check the ladder still **falls through**: the button block
sits at its top, so a conversion that swallowed everything would pass every
case above while breaking row selection and the scroll bar.

`tools/subcheck.py` is the regression that matters most — the same scripted
session through this kernel and a `REDRAWFULL=1` reference, **0 differing
pixels in 11 steps** including drags and raises. **Rebuild the reference from
current source**: a stale one reported 129 and 258 differing pixels that were
entirely the bug above, in the reference.

**And `[fm_vp]` is a NEAR offset while `m.sym` answers a LINEAR address.**
Mixing them reads 0x600 bytes low, lands inside the kernel image, and answers
a plausible `0` forever — three cases read as *"the release does not fire"*
against a kernel that was firing correctly, and it took a guest probe printing
`bhit=2 armed=2` to see that the kernel was right and the gate was wrong.


## 22. The driver pages and their child windows — BUILT

> **STATUS: BUILT.** SPEC.md §13.8.4 is the record. Twelve controls across four
> surfaces, and the headline is that **§13.8.3 had silently skipped every one
> of them**.

### 22.1 They were not converted, they were unreachable

`cp_pgprobe` sets `[cp_probe]`, runs the selected page's ladder and reads back
what each site reported through `cp_ctl`. **`[cp_probe]` is a kernel byte and a
driver's ladder has never heard of it**, so a driver page told to *identify*
did the only thing it knows how to do: it **acted**. A press could still mount,
unmount, format or connect — from inside the probe — while every static page
had stopped. Nothing then armed, so the release did nothing, and the symptom
was simply that these pages had not changed.

That is the shape to expect at every ABI boundary this feature crosses: **a
protocol that works by setting a flag the callee reads cannot cross into code
that was compiled without it.**

### 22.2 Two cells, and only for the pages

The split is clean and it is the whole cost story.

**The two hdd CHILD windows needed no ABI change.** The disk tool and the
installer are `OSAPI_WM_CREATE`d windows, so `OSAPI_WM_ONMOUSEUP` and
`OSAPI_WM_ONDRAG` were already published. Six buttons, all inside the driver's
own image: **zero against `KERN_BUDGET`**.

**The two PAGES needed `DSV_CPUP` and `DSV_CPDRAG`** (`DSV_SIZE` 28 → 32),
carrying `DSV_CPCLICK`'s identical arguments. Cost: **20 bytes of `.bss`** —
`drv_svc` is `DSV_SIZE * DRVC_MAX` — with the kernel dispatch in `ctrl.inc`'s
`.modc`, which is a file on the disk. The image rung went 114 → 94 bytes left
and the footprint did not move.

### 22.3 A page ABI hands over ONE click entry, so the ladder needs THREE modes

A window can afford `W_ONCLICK` and `W_ONMOUSEUP` as separate ladders. A page
cannot. So `[hd_pgpr]` is 0 = act, 1 = identify only, **2 = the press** — where
a device row or an editor field still *selects*, because selecting is a safe
prefix action and keeps the press (§13.6), while a button on the same ladder
only reports its id.

That third mode is why `hd_ctl` takes an id of **0**: it is what a site that is
not an armable control passes, and it is the only thing that can tell the two
species apart inside one ladder.

### 22.4 Four bugs, and three of them are register discipline

**`mov bp, [bp]` addresses `SS`.** `cp_drv_ev` read the published proc out of
the kernel's `drv_svc` copy through BP, and the kernel's SS is `LOW_SEG` — so
it read a task stack, got 0, and took the "this driver did not publish the
cell" path. §1's own rule, and the failure is indistinguishable from a driver
that never asked for the verb. `[ds:bp]`.

**`AH` was `cp_pt`'s leftovers.** `cp_drv_ev` banks the wanted `DSV_*` cell in
SI from `AX`, and the callers set only `AL`.

**`CL` is the predicate on the way in and the DIS flag on the way out.**
`hd_page_button` and `hd_iw_button` both build `mov cx, 0 / adc cx, 0` from the
greying predicate, four instructions after CL arrived carrying *which button
this is*. Asking `hd_isdown` afterwards asked about the flag. The fix is to ask
first and bank the answer in DX; the first draft instead tried `pop cx / push
cx` to recover it, which popped the wrong slot.

**`pop bx` destroyed the ladder's own argument.** `hd_page_up` banked the armed
id in BX across the identify pass — and BX *is* the page ABI's pane-relative
y, so the second, acting pass ran against a click at y = 2. SI is free and is
what it uses.

The fourth is not a register bug and is the one worth carrying forward:
**`OS88UI_FILL` stops being optional the moment a control gains a down state**
(§13.8.4). Every one of these painters correctly left it off, because the page
or window painter whites the ground first — and a *release* redraws the same
button over a **pressed** one. Measured on Connect: **147 lit while held, 0
after**. It goes invisible, not stuck-down.

### 22.5 Testing

`tests/drvup.py`, two machines. The hard-disk half drives `os8088_xt_hdd` and
signals on the kernel's own volume table, because that is memory rather than a
picture; its last case presses an editor **field** and requires the highlight
to move *while the button is still down*, which is mode 2 tested rather than
asserted.

Two apparatus corrections, both the same trap and both worth recording. The
pressed-look case first compared *lit counts* over the whole button and asked
only that they differ — 23 pixels, which is the **mouse arrow parked on the
button**, exactly what `tests/fmbtn.py` sprang the day before; it measures the
interior and requires a *halving*. And the mode-2 case compared lit counts of
the editor row, where the highlight frame is the same size on every field, so
taking one off and putting another on is a net change of **about zero** — it
compares the band's pixels now, with the pointer parked so it is in both
samples, and reads 164 differing.

`python3 tests/drvup.py net` is the second machine — `os8088_5150_cga_lpt`, the
only one here with a port at 0x378, the driver refusing to attach without one.
It is worth its own run rather than being taken on trust from the hard disk's:
it is what caught §22.4's fourth bug.


## 23. The scroll bar — the element, and both kernel bars on it

> **STATUS: the ELEMENT is built and `kernel/files.inc` and `kernel/fdlg.inc`
> are converted.** SPEC.md §13.10 is the record. The three package bars —
> Note Pad, Frotz's `zwin.inc`, ArtfulType's `atrend.inc` — are still their
> own, and each is an opt-in `%define OS88UI_SCROLL` away.

### 23.1 It was costed at ~170 bytes and it saved 41

The *before* side was measured byte for byte out of a nasm listing and stands:
`files.inc` **387** (139 full paint, 74 thumb, 78 hit, 96 the incremental
move) and `fdlg.inc` **288** (136, 65, 72, 15) — **675 for one widget built
twice**. The *after* side was modelled at ~505, and came out at ~634.

**What the model missed is the price of being general**, roughly 130 bytes of
it: bounds tests on the point that neither private bar needed (each knew its
click was already in its own window), a thumb part neither caller had asked
for, a computed centre where both had a constant, and a seven-word block that
has to be *filled* where a private bar read `[fm_lscr]` or `[fdlg_scrl]`
directly. The `mov ax,[bx+n]`-is-as-cheap-as-`mov ax,imm16` observation was
right and is not the reason the estimate missed.

Measured, both builds: **`.cold` −41, `.bss` +14**, footprints
unchanged. Cold slack **97 → 138** on big and **21 → 62** on small — which was
the point on small, that being the build with 21 bytes to its name.

So: a wash plus a little. The reasons to do it were the other two, both named
in advance and both delivered — one description where there were five, and the
dialog gaining the incremental thumb move it never had.

### 23.2 The two bars were a pixel apart, in two places

Arrow cells **11 rows against 12**; arrow centre **x1+6 against x1+7**. That is
exactly the drift a shared element exists to end, so it unifies on
`files.inc`'s — whose arrow is symmetric in its cell where `fdlg`'s sat one row
high, and whose centre is now *computed* as `x1 + (x2-x1)/2`, right on any
width rather than right on 14.

`files.inc`'s bar is therefore **byte-identical by construction**, which the
gate checks against the old geometry mirrored into it rather than against a
screenshot of the same build; `fdlg.inc`'s moves by those two pixels and
`FD_TRKY1`/`FD_TRKY2`/`FD_TRKH` are gone, the track being derived now.

### 23.3 Geometry, not policy

`os88ui_sbhit` reports `OS88UI_SBTHUMB` as a part of its own rather than
folding it into a page. `files.inc` treats a click there as page-down and
`fdlg.inc` as nothing, and **both are right about their own window** — an
element that chose would have been a behaviour change wearing a refactor's
clothes. The gate asserts both behaviours by name.

### 23.4 Testing

`tests/sbar.py`, three adapters, plus `python3 tests/sbar.py dlg` for the
dialog. It checks the PICTURE against §13.10's derivation mirrored into the
file — corners, both rules at ±10, the arrow centred and widening 1..9 over
five rows, the thumb a contiguous run of solid white interior that is neither
absent nor the whole track — and then the PARTS by what each does to
`FS_SCRL`.

Three apparatus corrections, and all three are the same lesson as the last two
rounds: **know what your instrument is reading.**

- **The bar is dark on white**, so its ink is an *unlit* pixel. The first run
  failed all five picture cases against a bar that was drawn perfectly.
- **`B:\` has six entries against five rows**, so its maximum scroll is 1 —
  which is exactly what an *arrow* gives, making a page and a step
  indistinguishable. It navigates into `APPS` (eleven) first.
- **VGA is not readable through `m.vram()`** — four planes behind the Graphics
  Controller — so a VGA run passed every memory case and failed every picture
  one. `rows()` picks its renderer.

`tools/subcheck.py` against a `REDRAWFULL=1` reference is **0 differing pixels
in 11 steps**, which is the regression that matters: those steps drag and
raise Disk windows, so the bar is drawn, restored and re-drawn throughout.


## 24. The rest of the queue — the bug and four conversions

> **STATUS: BUILT.** SPEC.md §13.8.5–§13.8.8. One reported bug, the Timer, and
> three packages; plus Note Pad onto §13.10's shared scroll bar.

### 24.1 Recorder was §13.8.4's flag in a fourth place

Reported as *"the buttons stay black with no text visible after mouse-on
mouse-off"*. `rc_btn` passed `xor di, di` — right while `rc_draw_btns` ran
only from `W_PAINT` onto white content, wrong the moment a **release** started
redrawing a button over a **pressed** one. It is the identical defect os88net's
Connect had, measured there at **147 lit while held and 0 after**.

The row in §15.4's survey guessed *"something is leaving it set, or leaving the
caption unpainted over a filled interior"* — right about where, wrong about
what. Recorder also gained `W_ONDRAG`, which it never had: its button stayed
down until the release rather than tracking.

### 24.2 The Timer is where greying and the down state meet

Start and Stop grey each other, so all four combinations of the two flags are
reachable in one gesture. `OS88UI_DIS` **outranks** `OS88UI_DOWN` inside
`os88ui_btn`, so a greyed button pressed looks exactly like a greyed button
upright — and the press therefore does not test the predicate at all: it arms
and draws, and the refusal stays at the release.

**The gate asserted the opposite first and the kernel was right.** `tests/tmrup.py`'s
case E now reads *"a press on the GREYED Stop does NOT draw it pressed"*.

`app_tmr_act` is the acting body lifted out of the click ladder, so `kern_big`
reaches it from the release and `kern_small` from the press.

### 24.3 Three register bugs, one shape

`BX` is the window pointer through `app_tmr_hit`, `app_tmr_setdown` and
`app_tmr_btn`, and both new edges banked the armed control there — so the
button came back up drawn against garbage. It goes in `BP`.

That is the third of this shape in this feature (`cp_drv_ev`'s `AH`,
`hd_page_button`'s `CL`, and now this): **a routine's arguments used as scratch
by the code that calls it.** All three assembled cleanly and all three drew
something plausible.

`os88ovlchk` caught a fourth for free — `apps.inc` is `.text` and the arm is
`.cold`, so the three `os88ui_*` calls needed `COLD_SEG:` far entries.

### 24.4 What keeps the press, and why that is not inconsistency

§13.6's test is about the **control**, not the app. Piano's *keys* keep the
press (a note is the safe prefix action the rule is about, and one that waited
for the release would be unplayable); so do the Disk window's rows, the
hard-disk page's device rows and editor fields, TexPad's two scroll-bar rects,
and Paint's canvas. Everything else in those windows fires on the release.

### 24.5 Paint's bracket owes the release edge

`pt_fsx_main` polls its own input (§42.7) and called `pt_click` on the rising
edge only. It computes the falling edge beside it now and calls `pt_onup`,
because **a control that behaves differently in two modes is worse than one
that acts too early in both**.

### 24.6 Note Pad joins the shared scroll bar; Frotz and ArtfulType do not

Note Pad's bar was **already pixel-identical** to §13.10's element — 14 wide,
`NP_SB_ARR` = 11 putting the rule at `ty+10` and the track at `ty+11`, the
arrow centred on `x1+6` — so the conversion changes nothing on the glass. It
was simply the third copy of a picture the kernel now draws once.

The other two are **decisions, not omissions**:

- **Frotz** includes no `os88ui.inc` at all. Its bar is pixel-identical too,
  but pulling the element in brings `os88ui_btn`'s body with it — ~350 bytes
  of a control Frotz never draws, against ~250 of private scroll bar. A net
  loss, and the gate that would fix it (`OS88UI_NOBTN`) is more machinery than
  the saving.
- **ArtfulType**'s bar is a genuinely different widget: 16 pixels wide, arrow
  **boxes** rather than glyph arrows, on §11.2's fullscreen writer surface.
  Converting it would change how the app looks, which puts it in §15.4's
  category D beside ModPlug's LED transport and Tracker's FT2 bevel.

### 24.7 Cost, and the budget

`KERN_SIZE` **108,544 → 109,056**, spare 1,024 → **512 (one step)** — the
Timer crossed an image rung. `kern_small` is unchanged at 103,424 (the whole
Timer conversion is `KERN_BIG`). Everything else in this round is package
image and costs `KERN_BUDGET` nothing.

**That leaves 512 bytes of the 1KB granted for "Button interactions", and no
kernel-side items remain in §15.4.**

### 24.8 Testing, and what is NOT gated

`tests/tmrup.py`, three adapters, eight cases, signalling on `TMR_RUN`. It
needs `quiet()` rather than `M.settle` — **the Timer animates**, which is
`tools/subcheck.py`'s "NO ANIMATING WINDOW" note met from the other side — and
it finds the `Builtins` title's x by scanning the bar rather than hard-coding
it, the bar being composed at run time (§12.9).

Regressions all clean: `sbar`, `fmbtn`, `cpup`, `drvup`, `fdlgup`, `mouseup`,
`chromedown`, `btndown`, `dispcalc`, and `subcheck` at **0 differing pixels in
11 steps**.

**The four package conversions are NOT driven by a gate of their own**, and
that is worth stating plainly rather than leaving to be discovered. What
covers them is indirect: `tools/btndown.py` drives the same arm/down machinery
in a package (Calculator), `tests/drvup.py` drove the identical
`OS88UI_FILL` defect end-to-end on os88net, and `tests/dispapps.py` confirms
Piano still loads and runs after its table rewrite. A `tests/pkgbtn.py` was
started and abandoned: launching an app by folder-and-row from a five-row CGA
Disk window needs scrolling, and the row arithmetic kept landing on the wrong
entry (`ld_status = 2`, Bad package, about a file nobody meant to click).
Shipping a gate that cannot launch its app is worse than saying it is not
gated.

**`tests/dispapps.py` is stale and it is not this round's doing**: it launches
by GAMES row index and `CYCLONE` shifted them, so its rows 3 and 4 are
provably wrong now. Same shape as `tools/growraise.py`'s `ROW_MINES`, fixed
earlier in this series. It wants deriving the row by name.

## 25. The RAM disk page — the last one in, and the last one converted

The RAM disk (SPEC.md §62.9) landed on `elendilon` while this series was
running, and it arrived exactly as §22's two driver pages had: acting on the
press, with no down state and neither `DSV_CPUP` nor `DSV_CPDRAG` in its
service table. That is not a criticism of the incoming work — the cells did
not exist when it was written — but it is the shape to expect at every merge
for as long as this feature is newer than the branches around it, and the
cheapest way to find it is to read the kernel's own copy of the table out of a
running guest: `drv_svc + (class-1)*DSV_SIZE + 30`, which answered `0x0000`.

### 25.1 It arrived with the expensive half already right

One rect per control, `os88ui_btn`, `os88ui_glyph`, `os88ui_bhit` against the
*same four words* the painter takes — §22's `fm_hit` discipline, done. So the
conversion touched only the event half, and the id space fell out of what was
already there: **a button's id is its own rect pointer**, because the hit test
answers in one and every painter takes one. A radio has no rect, so the two of
them are 1 and 2, which no pointer into a package image can collide with.

Two structural changes came with it. `rp_blbl` gives every button a label and
a predicate in one table, so the full paint and the per-control redraw are the
same drawing rather than two that agree — and `-`/`+` joined the other five
there, having been drawn by a wrapper of their own up in the size row. And
`rp_state` came off the press: `RDS_ERR` is read-and-clear at the resident
(§59.5), so asking on a press that cannot repaint consumes a verdict nothing
will draw.

### 25.2 The size box keeps the press, and `rp_hit` does not know it exists

§13.6 again, and this page is the cleanest instance of it in the tree: seven
controls that do something irreversible on release, and one — the size box —
whose action is taking a caret. Rather than a flag, `rp_hit` simply does not
carry the size box's rect, so nothing downstream can arm it. `rp_click` tests
that one rect itself, first, and everything below the test arms.

### 25.3 Two bugs, both of which assembled and neither of which faulted

**`(rect - rp_r_minus) / 2`, written as two `shr`s.** The rect stride is 8 and
the table entry is 4, so the index arithmetic is one shift. With two, `+` read
its label out of the predicate column and then `call`ed the string next to it
— and what it looked like was a page that drew its first button and stopped,
with a live machine behind it.

**`DX` is the pane TOP on entry and the hit tests want the point.** The
relative y arrives in `BX`. The routine this was lifted out of reloaded both
from its own banked copies right before the first test, and dropping that one
line left every press comparing against a y of 42: a page that paints
perfectly and answers no click at all. Both are the same family as §22.4's
four — a register that means two things at two moments — and both were found
by the gate rather than by reading.

### 25.4 Testing

`tests/rdup.py`, eleven cases on a cycle-accurate 5150/CGA. The two cells are
asserted **in the kernel's copy** of the service table and not the driver's,
because a table that stopped short publishes whatever follows it — which
`rd_svc` was one commit away from doing, `rd_fsv` being the next label and the
file lister being what a release would have far-called. `RD_ABI_VER` goes
1 → 2 with the two new `RDT_*` verbs.

The size field is compared as a **bitmap and never as a lit-pixel count**: two
different numbers can weigh the same, and this is a four-character field. The
down state is measured on the button's INTERIOR and asserted as a *halving*,
which is `tests/fmbtn.py`'s hard-won rule — a `!=` passes on the ~15 lit
pixels of the mouse arrow parked on the control.

Regressions clean: `drvup` (both machines), `cpup`.

**`tests/rdmove.py` was fixed enough to run and is still inconclusive**, which
is not this round's doing either way. It re-assembles `ramdisk.asm` to find
`[rd_arena]` and did so **without `-DRAMPAGE_KB`**, so it has failed at the
nasm call for as long as that knob has existed and nobody had noticed; and it
ticked the driver row without ever pressing **Mount**, which is where
`rd_store_get` claims the arena — so `[rd_arena]` was 0 and there was nothing
on the heap to move. Both are fixed, and it now reports 2 of its 3 checks OK
with the third *inconclusive*: the arena did not move, because the heap it
builds leaves a 105KB hole above the store and nothing ever needs more. That
is a scenario problem in a §66 compaction gate, not a defect, and it wants its
own round.
