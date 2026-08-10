# UI element helpers — investigation and plan

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
| `KERN_BUDGET` | **zero** | ~350–450 bytes, against 1,024 spare (kern_big) / 512 (kern_small) |
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

**No measurement is possible in this container — there is no `nasm`.** The
per-package figure is from instruction shapes. It does not touch the kernel, so
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

## 7. Optional 7a: consolidate the kernel's own helpers

Independent of everything above, no ABI, no package impact: `fdlg_btn` /
`fdlg_btn2` / `fdlg_defbtn` / `cp_vid_btn` / `cp_timebtn` / `app_tmr_btn` are
close enough in shape to share a body, and `cp_glyph`'s four 12x12 bitmaps are
already one mechanism with one caller.

Worth maybe 200–300 bytes of `.text`, which is real money against kern_small's
512-byte spare — **and it is the only part of Tier 4 that helps the budget at
all.** It can be done at any time, in any order, and does not need this
document's SDK work to land first. If the budget gets tight before Tier 4 is
scheduled, do this and skip the rest.

## 8. Order of work

1. **Tier 3 first**, or the arm has nothing to talk to.
2. `apps/os88ui.inc` — button draw + hit + arm, and the glyph pair.
3. Convert **one** package (Recorder or Piano: small, plain buttons, already
   pen-correct) and prove byte-identity on all three adapters before touching a
   second.
4. Convert the rest of the standard-look set. Each its own commit with its own
   capture, because a visual regression here is per-site and a batch commit
   hides which one.
5. §7a whenever convenient, independently.

## 9. Recommendation

**Do it, and do it in the SDK.** But it is the weakest-value tier of the four
and it should be scheduled last:

- Tier 1 is a safety fix, ~115 bytes, no ABI.
- Tier 2 is a capability a package genuinely cannot have (twelve bytes, and no
  consumer yet — `DBLCLICK-PLAN.md` §9).
- Tier 3 is a mechanism.
- **Tier 4 is ergonomics** — it makes Tier 3 usable and makes a recurring bug
  class structural, and it changes nothing a user can see.

Its value is entirely conditional on Tier 3 landing. Built before it, the arm
half has no callback to hang off and the tier reduces to a shared button
painter, which is worth having and is not worth prioritising.
