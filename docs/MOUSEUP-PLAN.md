# The chrome fires on mouse-UP — investigation and plan

> **STATUS: IMPLEMENTED.** SPEC.md §13.5 and §13.6; `kernel/ui.inc`. Measured
> at **`.text` +102, `.bss` +3 (§8) and gated on CGA,
> Hercules and VGA mode 12h (§12.1). Phase B — pressed-state feedback — is
> deliberately **not** built (§7).

**Tier 1 of the mouse-up work.** Self-contained: no API slot, no package
rebuild, no `.o88` invalidated, no SDK change. It touches `kernel/ui.inc` and
nothing else.

The other tiers (`OSAPI_DBLCLK`, the `W_ONMOUSEUP` callback and the WM surface
revision, the UI element helpers) are separate documents and separate work.
This one is first because it is the smallest and because it is the one where
firing on the press actually costs the user something.

---

## 1. What fires where today

`ui_task`'s ladder (`ui.inc` step 2) pops one event per pass and acts on
`EVT_MDOWN`. **`EVT_MUP` falls through to `.yield` and is discarded** — SPEC.md
§11.95 already says so in as many words, about the zoom: *"the `EVT_MUP` behind
it is popped by the UI loop and ignored, which is what that loop already does
with every mouse-up."* So the release channel is free and nothing has to be
taken away from anybody to use it.

| element | `wm_hit` | fires on | where |
|---|---|---|---|
| menu title | — | **release** | `menu_drop` polls the level, commits at the hovered item |
| title bar | AL=1 | **release** (drag) | `ui_drag` commits geometry on release |
| **grow box** | **AL=4** | **release** | `ui_grow` — *see §1.1* |
| file row / icon | AL=0 | press | double-click detectors, §22/§26/§38 |
| **close box** | **AL=2** | **press** | `ui.inc` `.close_box` |
| **minimize box** | **AL=3** | **press** | `ui.inc` `.min_box` |

### 1.1 The grow box is already correct and is NOT in scope

This is a correction to the first pass of the investigation, which listed
close/minimize/**grow**. `.grow_box` calls `ui_grow`, which is a tracking loop
that commits `W_W`/`W_H` on release and repaints only if the size changed
(`di` stays 0 otherwise). It already has release semantics *and* a slide-back —
you can drag the corner back to where it started and release, and nothing
happens.

**Scope is therefore exactly two regions: `AL=2` and `AL=3`.** Narrower than
first stated, and better: two regions is a smaller change than three and the
third would have been a no-op wrapped around a working tracker.

## 2. Why the press is the wrong edge here

Not style. The close box is an **11x11 rect** — `wm_hit` reports `AL=2` for
rows `W_Y+4 .. W_Y+14`, columns `W_X+8 .. W_X+18` — and it is the control that
**destroys the user's work**. Firing on the press means a mis-aimed press is
unrecoverable: there is no gesture between "pressed" and "happened" in which to
notice and slide off. Every other GUI of the era this OS models put that gap
there, and it is the one place in this system where the gap pays for itself.

The minimize box is the same 11x11 target mirrored to `W_X+W_W-19 .. W_X+W_W-9`
and is included for consistency rather than for danger: a chrome box that
cancels and one that does not, sitting 8 pixels apart on the same title bar, is
worse than either rule applied to both.

It also earns something on the target machine specifically. A 1200-baud serial
mouse and a 4.77MHz redraw make a mis-aimed press *more* likely here than on
the hardware these conventions came from, not less — and `tools/os88mouse.py`
exists at all because dead-reckoning onto a small control drifts.

## 3. The mechanism: arm and return, never spin and track

Two shapes are possible and only one of them is right.

**Spin-and-track** — dispatch on the press, then loop on `mouse_btn` until it
comes up, drawing a pressed state and watching for slide-off. This is what
`ui_drag`, `ui_grow`, `menu_drop` and `fm_drag` do, and it is what a first
draft reaches for because it is how live feedback is drawn.

**Arm-and-return** — the press records *"armed on window W, region R"* and
returns. The release, a later pass, re-tests the point and fires if it agrees.

**Take arm-and-return.** Three reasons, in order of how expensive getting it
wrong would be:

1. **The keyboard mouse costs one keypress instead of two.** SPEC.md §9.6.1:
   `kbm_btn` latches `mouse_btn` and posts `EVT_MDOWN`; `kbm_ui`, at the end of
   the deferred ladder, releases the latch and posts `EVT_MUP` **once the pass
   that dispatched that mouse-down is over**. A handler that arms and returns
   ends its pass, so the release is posted automatically at an unmoved pointer
   and the next pass fires it. A handler that *does not return* holds the pass,
   `kbm_ui` never runs, and the latch stays down until the user presses again.
   Both work — the standing requirement is that the keyboard mouse can perform
   every action, and press-press does — but one of them is a click and the
   other is the thing §9.6.1 describes as reading *"as broken everywhere else,
   because a button that stays down is not what an icon, a button or a close
   box wants."*
2. **It holds no lock across a wait.** A tracking loop must
   `gfx_unlock`/`task_yield`/`gfx_lock` and pace itself to the tick, or it
   spends the machine (SPEC.md §7.1.3 — `fm_drag`'s `.wait` was 20,761
   lock/unlock pairs per second of held button before it was paced). Arm-and-
   return adds no loop, so there is no pacing to get wrong and no cursor to
   blink.
3. **It is smaller.** No loop, no XOR overlay, no save/restore.

What arm-and-return cannot do is un-draw a pressed state as the pointer slides
off, because it never observes the pointer between the two edges. That is
§7's subject and it is deliberately not in Phase A.

## 4. The four guards

Each of these is a way the naive version breaks, and each is cheap.

### 4.1 The release must re-test, not trust

Fire only if `wm_hit` at the **release** point answers the same window *and*
the same region as the arm. This is the whole feature — "released over the same
element" — and it is one `wm_hit` call the ladder already knows how to make.

Both halves are needed. Same region, different window: two stacked windows'
close boxes can overlap in screen space. Same window, different region: the
close and minimize boxes are on the same 11 rows of the same title bar.

### 4.2 The armed window may be gone

Between the two passes a package worker can reach `inst_task_die` and tear its
window down, or a menu command can destroy it. Re-check `W_FLAGS` bit 1 under
the lock before firing — the guard `ui_dispatch` already makes twice, at
`ui.inc:1105` and `:1128`, for exactly this reason and with the reasoning
written out there: `wm_destroy` clears the bit under the lock we hold, and only
the UI task calls `wm_create`, so the slot cannot be recycled behind the test.

~~In practice `wm_hit` in §4.1 answers this already — a destroyed window is not
in `wm_zord` and cannot be hit — so this is belt-and-braces on the pointer
comparison rather than a second mechanism.~~ **That is true of the CHROME half
and false of the package half, and the difference is the whole of §13.7.**

The chrome path (`AL` 2/3) does call `wm_hit` at the release, so there the
flags test really is belt-and-braces. `.mup_pkg` deliberately calls neither —
a release outside the window is the contract, so there is no `wm_hit` and no
pointer comparison to be second to. **The flags test is the only guard on that
path**, and `wm_destroy` clears `W_FLAGS` and *not* `W_ONMOUSEUP`, so a freed
slot still holds a live-looking callback pointer.

Measured, and this is what case 7 was for: with the two lines deleted, the
release dispatches through the freed record and the package's proc runs
(`tests/mouseup.py` fails on exactly that check, and passes with them back).
So: keep it, and not for the reason first written down.

### 4.3 A dropped `EVT_MUP` must not leave the arm standing

`evq_push` drops silently when the ring is full (16 records). `ui_drag`,
`ui_grow` and `fm_drag` each carry the same fallback and say so — *"the
`EVT_MUP` was dropped (queue full) — the level still says so."* Here the
failure is worse than a stuck drag: a stale arm would be fired by the *next*
unrelated release.

The fallback is a level test in the deferred ladder, and it needs **two**
conditions, not one:

```
arm set  AND  mouse_btn bit 0 clear  AND  evq_count == 0   ->  disarm
```

The queue test is not decoration. Without it:

- **The keyboard mouse breaks outright.** `kbm_ui` posts the `EVT_MUP` at the
  end of the very pass that armed. A disarm check sitting *after* `kbm_ui` sees
  bit 0 already clear and throws the arm away before the release it just
  queued can be popped — the close never happens, on a machine with no mouse.
- **A real mouse breaks intermittently.** If anything was queued ahead of the
  release, the pass that pops *that* event runs the deferred ladder with bit 0
  already clear and the `EVT_MUP` still in the ring.

With the queue test, both are covered and — worth having — **the check becomes
insensitive to where in the ladder it sits**, because the case that would
depend on ordering is exactly the case the queue is non-empty in. Put it near
`kbm_ui`; do not rely on it being there.

`evq_pending` (`events.inc:161`, API 0x0338) already reads that word, and reads
it with no lock for reasons its own header sets out.

### 4.4 `[ui_click_t]` stays stamped on the DOWN edge only

It is stamped at `ui.inc:118-119` from the dispatched `EVT_MDOWN`'s `EV_C`, and
four independent detectors compare against it — `ui_tdbl`, `desk_click`,
`fm_onclick` (per window, `FS_CLKT`) and `fdlg`. Double-click stays on the down
edge (§5), so **the new `EVT_MUP` branch must not stamp it.**

That is automatic if the branch is written separately and falls out if a later
edit "tidies" the two branches into one. It is called out here and in the
proposed SPEC text because the failure is quiet: double-click spacing would
silently become release-to-release, and `tools/os88mouse.py dblclick` measures
press-to-press and would keep passing.

`ui_rdown` already carries the mirror-image of this rule with its reasoning
spelled out at `ui.inc:424` — *"stamp `[ui_click_t]`. Binding."*

## 5. The boundary, and why the two features never meet

The rule this settles on, from surveying every double-click site in the tree:

> **The single-click action must be a *prefix* of the double-click action.**

Select is a prefix of open. Raise is a prefix of zoom — you must raise before
you can zoom. Where that holds, the first click's action is safe to perform
unconditionally, so the detector may sit on the **down** edge, which is also
where it is most responsive on a slow machine.

Checked against all six sites and both package workarounds — the title bar
(raise → zoom), drive zones (select → open), Disk rows (select → launch),
dialog rows (select → commit), ModPlug's playlist (select → play, no timer at
all) and Solitaire's foundation send. **No counter-example exists in the tree.**

The consequence is the useful part:

> Anything that fires on mouse-**up** has no double-click. Anything with a
> double-click fires its first action on mouse-**down**.

Buttons, checkboxes, radios and the two chrome boxes have no prefix action —
there is nothing safe to do on the press — which is exactly why they want the
release. Rows, icons and title bars have one. **The two boundaries coincide, so
this work and any future double-click work cannot collide**, and Tier 2's
`OSAPI_DBLCLK` can be designed without reference to this document.

## 6. Phase A — dispatch (recommended, and the whole of Tier 1)

State, in `.bss`:

```
ui_armw  resw 1      ; the armed window, 0 = nothing armed
ui_armr  resb 1      ; its wm_hit region: 2 close, 3 minimize
```

A window pointer is never 0, so 0 is the "nothing armed" sentinel — `ui_tdbl`'s
`ui_tclkw` uses the identical convention and says why.

Four edits to `ui.inc`, and nothing outside it:

1. `.close_box` / `.min_box` — record `(BX, AL)` and fall to `.yield` instead
   of calling `app_close_win` / `inst_minimize`.
2. A new `EVT_MUP` branch in step 2, ahead of the `.yield` the type test
   currently falls into. Loads `EV_A`/`EV_B`, and **does not touch
   `[ui_click_t]`** (§4.4). If nothing is armed it falls straight through, so
   the common case is one compare.
3. The fire path: `wm_hit`, compare against the arm, clear the arm
   unconditionally, and on agreement take `gfx_lock` and make the call the
   press used to make. Clearing before the call, not after, so a handler that
   reaches back into the ladder cannot find the arm still standing.
4. The level fallback of §4.3 in the deferred ladder.

A press that is swallowed by `fdlg_grab` never arms, so the modal case needs no
new gate: **the arm is the gate.** A `WF_FULL` window has no chrome (`wm_hit`
reports every point as content, SPEC.md §11.2), so fullscreen needs nothing
either.

Billing does not change. Both paths are unbilled today and `ui.inc:1107` gives
the reason it has to stay that way for the close: *"the record the cycles would
go to is the one being freed."*

## 7. Phase B — pressed-state feedback — TAKEN (SPEC.md §13.8.1)

> **STATUS: LANDED.** Read this section for the *objection*, which was exact
> and is still the thing to defend against — and then read why neither way out
> it offers was the one taken. There is a third, and it is the one the UI
> task's existing shape already pays for: **the pointer is observed once per
> UI PASS**, in the deferred ladder beside `ui_arm_chk` (`ui_arm_trk`). That
> is not §3's tracking loop — no `gfx_unlock`/`task_yield`/`gfx_lock` round
> trip, nothing held across a wait, no pacing to get wrong, and the keyboard
> mouse still costs one keypress because the handler still arms and returns.
> It costs **one compare per pass** when nothing is armed, which is the same
> compare and the same argument as its neighbour.
>
> So the recommendation below — ship Phase A alone and revisit — was right,
> and this is the revisit. The mono check it asks for was done: `chromedown.py`
> passes ten cases on CGA, Hercules **and** VGA mode 12h, and the 1bpp answer
> to *"is a 50%-dither XOR over a pinstriped title bar legible"* turned out
> not to arise, because what inverts is the box's **9x9 white interior** and
> the pinstripes are broken around it two pixels out by `wm_draw_title`
> already. It reads as a solid black square, which is what a Macintosh close
> box does.
>
> docs/UIHELPERS-PLAN.md §15 is the landed work and the survey of what still
> has no down state.

## 7.0 The original text

The System 1 behaviour is that the box inverts while held and un-inverts if the
pointer leaves. Two ways to get part of it:

- **Static invert.** XOR the 11x11 rect on the arm, XOR it back on fire or
  disarm. XOR is its own inverse, so this is the dock's mark trick (§30.3) and
  the menu highlight's. Two `gfx_xor_fill` calls per click ≈ 2ms on the field
  machine at PERFORMANCE.md's ~756µs floor plus 11 rows. Cheap and correct *as
  far as it goes* — but the invert **stays on while the pointer slides off**,
  because arm-and-return never observes the pointer in between. That reads as
  "held", not as "will not fire", which is arguably worse than no feedback:
  it says the opposite of what is true.
- **Live invert**, which needs the tracking loop of §3 and pays all three of
  its costs.

**Recommendation: ship Phase A alone and revisit.** The safety fix is the
valuable half and it is ~100 bytes; feedback is a look-at-it-on-the-glass
decision that wants a 1bpp adapter and a person, not an estimate. If it is
taken later, static invert on a **1bpp** adapter should be checked first — a
50%-dither XOR over a pinstriped title bar is not obviously legible, and
CLAUDE.md's standing rule is that a greying or contrast change is not done
until it has been looked at on mono.

## 8. Budget — MEASURED

Built and measured (`tools/kernsize.py`), not estimated:

```
kernsize[big]: sections   text 55,434 +102  bss 4,919 +3   (sum +105)
kernsize[big]: rungs      image 60,416 +0 (63 left, was 168)
kernsize[big]: footprint  KERN_SIZE 96,768 of 98,304 -> 1,536 spare (3 steps), was 1,536  [+0]
kernsize[big]: segment    .text+.bss 60,353 of 65,536 -> 5,183 left
```

**`.text` +102, `.bss` +3 — the estimate above was ~115
and 3. The footprint is unchanged at 96,768 and the spare is still **1,536
bytes, three 512-byte steps**. (Two earlier drafts of this document said 1,024
and two steps, taken from `docs/KERNEL-MEMORY.md` rather than from a build;
that was wrong and is corrected here and in the other three plans.)

**The number to carry forward is the IMAGE RUNG: 63 bytes left, was 168.**
That is the constraint CLAUDE.md warns is independent of the reported spare,
and this change spent 105 of the 168 that were there. **The next addition to
`.text` anywhere in the kernel — Tier 3's ~37 bytes included — very likely
buys a whole 512-byte step**, taking the footprint spare from three rungs to
two. That is not this change's fault and it is not free either; it is the next
author's first fact.

kern_small is a separate `KERN_SMALL=1` build and was **not** measured here.

## 9. Testing

MartyPC first (`make marty`), per the standing rule — this is 8088 code on all
three adapters and nothing in it needs QEMU.

Drive it with `tools/os88mouse.py`, which closes the loop against the published
`mouse_x` rather than dead-reckoning. The verb that matters is **`menu`** —
press, drag, release — which is press-and-slide-off under another name:

| # | gesture | expected |
|---|---|---|
| 1 | `click` on the close box | window closes |
| 2 | `menu` from close box → content, release | **nothing happens** |
| 3 | `menu` from content → close box, release | **nothing happens** |
| 4 | `menu` from close box → minimize box, release | **nothing happens** (§4.1, same window, different region) |
| 5 | 1–4 for the minimize box | as above |
| 6 | two stacked windows, press front close box → drag over the back window's close box → release | **nothing happens** (§4.1, region matches, window does not) |
| 7 | press close box, close the window from its own menu bar mid-gesture, release | nothing, no fault (§4.2) |
| 8 | keyboard mouse, NumLock off: arrow onto the close box, **one** keypad-0 | window closes (§3, §4.3) |
| 9 | `dblclick` a Disk row, a drive zone, a dialog row, a title bar | unchanged — all four still open/zoom (§4.4) |

Cases 2, 3 and 6 should leave the framebuffer **byte-identical to not having
clicked at all**. That is the check worth automating: capture before the press,
capture after the release, diff. `os88marty.py shot --rendered` and the
`pixcheck` approach §59 used are both already in the tree.

Case 8 is the one no emulator-agnostic reasoning can replace, and it is cheap:
MartyPC's keyboard goes through the 8255 and int 09h, so `kbm_key` really runs.

Case 9 is a regression test, not a feature test, and it is the one most likely
to be skipped and most likely to break — §4.4's failure is silent.

**Not needed:** a `REDRAWFULL=1`-style reference build. That knob exists for
changes claiming *"the picture is the same, only the number of times it was
drawn changed"*, and this change is not making that claim — the picture is the
same for cases 2/3/6 and is *supposed* to differ for case 1.

## 10. What this does NOT change

- **No API slot.** Nothing in the table moves, nothing is added, no `.o88` is
  invalidated, no image outside `build/` is reissued. The §20.8 rule 4 unfreeze
  is irrelevant to this tier — worth stating, because the other three tiers all
  depend on it.
- **No package sees any difference.** `W_ONCLICK` is a *content* callback; the
  chrome never reached it. A package cannot tell this happened.
- **The grow box, the title bar, menus and drags** — already release-driven
  (§1.1), untouched.
- **Every double-click** stays on the down edge (§4.4, §5).
- **The right button.** `ui_rdown` has no chrome path at all — `ui.inc:476`
  returns on any non-zero region, *"the chrome has no context menu."*
- **`app_close_win` and `inst_minimize`** are called with the same arguments,
  under the same lock, from the same task. Only *when* moves.

## 11. SPEC.md — LANDED

The text is in SPEC.md as **§13.5** (the release rule) and **§13.6** (which
edge an element fires on), landed in the same commit as the code per CLAUDE.md's
"update SPEC.md before changing any interface". They are no longer proposals,
so this document cites them with a `§` like anything else.

> **The convention for a plan document that proposes a section, and the trap
> under it.** `tools/checkdocs.py` resolves every `§n.n` against SPEC.md's real
> headings and fails `make` on one that does not resolve — which a document
> proposing a *new* section trips by construction. So write a not-yet-existing
> section bare (`13.7`) and add the `§` when the text lands; the same applies
> to a not-yet-existing cell, which is a *cell* until `os88api.inc` has it.
>
> And checkdocs takes its file list from `git ls-files`, so it **cannot see an
> untracked file.** Running it on a new plan document before the first
> `git add` reports a clean tree it never opened — which is exactly how the
> first version of this document was committed with three broken citations in
> it and `make` failing for anyone who pulled. **`git add` first, then run the
> gate.**

## 12. Order of work — DONE

1. ~~§6's four edits, no feedback.~~ **Done** — `kernel/ui.inc` only.
2. ~~`make`; report the per-section delta and any rung crossed.~~ **Done** —
   §8, and no rung was crossed.
3. ~~`make marty`; cases 1–9 on CGA, then Hercules, then VGA.~~ **Done** — all
   pass on `os8088_5150_cga_gla`, `os8088_5150_herc_gla` and `os8088_xt_vga`,
   with case 8 (the keyboard mouse, one keypress) additionally re-run on VGA.
   The IBM-ROM machines are unavailable in a container — no ROM set ships —
   so the GLaBIOS variants are what a CI or container run gets.
4. ~~Land §11's SPEC text in the same commit.~~ **Done** — §13.5 and §13.6.
5. Phase B (pressed-state feedback) is **not** part of this and remains
   unbuilt (§7).

### 12.1 What the gate actually proved, and two corrections to it

The harness lives in the session scratchpad rather than in `tests/`, because
it drives the kernel from outside and needs no guest code — `tests/` is for
packages. Cases run: 1 (click closes), 2 (press close → release on content →
nothing), 3 (press content → release on close → nothing), 4 (press close →
release on minimize → nothing), 5 (both for the minimize box), 6 (two windows,
press A's close → release on B's close → neither closes), 8 (keyboard mouse,
one keypress), 9 (drive-zone and title-bar double-clicks still work).

Two things went wrong in the *apparatus* first, and both are worth recording
because both produced a confident wrong answer about working code:

- **`m.sym()` returns a FLAT address**, and feeding it to `m.readseg(0x0060,
  …)` adds the segment twice. The window table then read as all-zero and the
  first run reported "no window is open" about a machine that plainly had one
  on screen. `m.read(m.sym(x), n)` is the idiom, and `os88marty.py`'s own
  docstring says so.
- **A keyboard-mouse walk must not require pixel-exact arrival.** The step
  accelerates on a run of repeats and resets on a change of direction
  (§9.6), so the pointer oscillates about a target it cannot divide into.
  Demanding equality tests the ramp; the controls are 11x11, so a 3px
  tolerance tests the feature. The first run reported three failures against a
  kernel whose actual outcomes — including the one that matters — had all
  passed.

**Case 7 IS RUN NOW** — `tests/mouseup.py`, and §4.2 above is what it found.
`tests/muptest` grew a second, unbound window whose `W_ONCLICK` calls
`OSAPI_WM_DESTROY` on itself, so the kernel arms the release against a record
it has just freed; the main window's caption is the signal, because by then
the window under test cannot report on itself. Five checks, and three of them
exist to stop it passing vacuously: the press must have been DISPATCHED, it
must have ARMED (`ui_armw` non-zero, read between the two edges), and the
window must really have gone. The first run passed the release check while
failing all three — a background window's press is spent on `wm_front`
(§13), so nothing had armed at all.

The paragraph this replaces said:

> Setting it up needs a package worker tearing its own window down between
> the two passes, which is a gate package rather than a scripted click. The
> guard is in the code (`test word [bx+W_FLAGS], 2`) and `wm_hit` answers it
> first anyway, so this is an untested belt behind a tested braces.

Both halves of that turned out to be wrong. It did not need a worker — a
`W_ONCLICK` that destroys its own window is synchronous and deterministic,
and better for a test. And `wm_hit` does **not** answer it first on this
path, which is §4.2.

**And nothing here was run on the field machine.** Nothing in this change
touches a disk, so CLAUDE.md's 5150 rule is not engaged, but the two 1bpp
adapters have only been seen through an emulator's framebuffer.
