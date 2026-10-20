# A package cannot detect a double-click — investigation and plan

**Tier 2 of the mouse-up work**, and the smallest thing in it: one API cell
and about four bytes of body. It publishes a word the kernel already keeps.

It is independent of Tier 1 (`docs/plans/completed/MOUSEUP-PLAN.md`) in both directions —
different edge, different elements, no shared state — which is §13.6's boundary
rule doing its job. Either may land first.

---

## 1. Two corrections to the investigation

Both are against claims I made when ranking the tiers, and both weaken the
case. They are first because they change what this work is *for*.

### 1.1 Solitaire is not a waiting consumer

I listed it as one. It is not. `apps/solitaire/solitaire.asm:54`:

> *A press and release WITHOUT moving auto-plays that one card to a foundation
> if it will go, which is the double-click every version of this game has,
> without needing a double-click timer.*

That is **click versus drag**, not a double-click substitute — the gesture is
distinguished by whether the pointer *moved*, which the package can already
see. It needs nothing from the kernel, and it is arguably the better gesture:
it is discoverable, it has no timing, and it cannot fail on a slow machine.

### 1.2 ModPlug declined for two reasons, and only one of them is the kernel's

I quoted the first half. `apps/modplug/mpplist.inc:20`, in full:

> *There is no DOUBLE-CLICK. The kernel delivers one `W_ONCLICK` per press and
> a package detecting a double-click would have to compare tick counts itself
> (SPEC.md §13), which on a 4.77 MHz machine under a 1200-baud serial mouse is
> a timing guess. Clicking a row SELECTS it; clicking the row that is already
> selected PLAYS it. **No timing, and no gesture that works on a fast machine
> and not on a slow one.**"*

The first reason is a missing capability and this document removes it. **The
second is a design opinion that survives it intact** — and it is a good one,
made by someone who had the target machine in mind. Handing ModPlug this slot
would not oblige it to change, and it should not.

### 1.3 So the honest demand is: nobody is waiting

Nothing else in the tree wants it either. I checked the two text editors for
double-click word-select, the obvious remaining candidate — **Note Pad has
none and ArtfulType has none**, and neither has a TODO asking for one. The
Task Manager is a viewer with nothing to open. Paint's palette and tool
cells are single-click by design.

**On day one this slot has zero callers.** That is the finding, and §9 is what
to do about it.

## 2. What the gap actually is

Not demand — **consistency**. The kernel uses double-click as its *primary
open gesture* in four places:

| site | first click | second click | detector |
|---|---|---|---|
| Disk row | select | launch / dive | `fm_onclick`, `FS_CLKT` per window |
| drive zone | select | open drive | `desk_click`, `desk_clkt` |
| dialog row | select | commit / dive | `fdlg`, `fdlg_clkt` |
| title bar | raise | zoom | `ui_tdbl`, `ui_tclkw`+`ui_tclkt` |

All four compare **birth ticks** against a **9-tick** window
(`FM_DBLCLK` / `DESK_DBLT` / `FD_DBLCLK` / `UI_TDBLT`, four constants
deliberately not shared — `ui.inc:41` says *"they are four independent
detectors and one of them may one day want to differ"*).

A package cannot have that gesture. Not "should not" — **cannot**, correctly.
Which means the decision about whether a package's list should open on a
double-click is currently being made for the package author by an absent
twelve bytes, rather than by them.

## 3. Why a package cannot do it today

The birth tick. `EV_C` of the event record (§10) is the tick at which the ISR
saw the press, and the whole double-click mechanism in this OS is built on it:

> *double-click detection compares birth ticks, never processing time, so
> clicks queued behind a slow disk mount cannot collapse into a double-click.*
> — SPEC.md §9, on the mouse ISR

`ui_task` publishes it into `[ui_click_t]` before the ladder runs
(`ui.inc:118`), and `ui.inc:1698` already records the contract in as many
words: *"the click's birth tick (valid during dispatch, which always stores
before handlers read)."*

**A package has no way to read it.** The only clock it can reach is
`OSAPI_GET_TICKS` (0x00B8, `out AX = [ticks]`), which answers *now* — the
processing time, at the moment its `W_ONCLICK` happens to run. That is exactly
the quantity SPEC.md forbids comparing, and the error is not theoretical on
this hardware: a package load, a floppy mount or a `SYSTEM.CFG` write is
**seconds** on the field machine, all of it under the gfx lock, with presses
queueing behind it. Two clicks a user made a second apart can arrive at
`W_ONCLICK` microseconds apart.

So a package rolling its own detector gets one that is **wrong in the
direction that fires when it should not** — which is the worse direction for a
gesture whose second half opens files.

## 4. Two shapes, and the cheap one is better

### 4.1 Option A — publish the tick (**recommended**)

```
OSAPI_CLICK_TICK   0x03A8    out AX = the birth tick of the click being
                             dispatched. Valid inside W_ONCLICK only.
```

Body is a `mov ax, [ui_click_t]` and a `ret` — the slot cell owns the `retf`,
like every other `OSAPI_SLOT`. The package keeps its own `(identity, tick)`
pair and does the compare, which is four instructions.

### 4.2 Option B — a full detector with caller-owned state

```
OSAPI_DBLCLK   AX = identity word, ES:BX = caller's 4-byte state block,
               AL = 0 restamp / 1 spend    ->  CF = 1 double-click
```

Needs an `OSAPI_XSTUB` (the state block is in the caller's segment, so ES must
carry the caller's DS — the `wm_create` / `font_str` idiom), a policy byte, and
~50 bytes of body.

### 4.3 Take Option A

Four reasons:

1. **It makes no policy decision.** Option B has to pick, or parameterise,
   things the kernel has no opinion about: the window length, and
   spend-versus-restamp — where `ui_tdbl` *spends* the completed pair so a held
   triple does not zoom twice, and the other three *restamp*. Encoding a
   choice nobody has asked for, on behalf of callers who do not exist, is how
   a slot ends up carried forever.
2. **It is a quarter of the size** (§7).
3. **The compare it would centralise is genuinely easy.** See §5 — this is not
   the trap it looks like.
4. **Option B remains available.** Under §20.8 rule 4's alpha unfreeze, if
   three packages appear and all write the same compare, promoting A to B —
   or replacing it outright — is a rebuild, not a compatibility event. Under
   the old freeze this argument would have run the other way and A would have
   had to be right the first time.

## 5. The compare, and a trap that does NOT apply

The idiom, identical in all four kernel detectors:

```asm
    call OSAPI_CLICK_TICK       ; AX = this click's birth tick
    cmp  bx, [my_last_ident]
    jne  .first
    sub  ax, [my_last_tick]     ; wrap-safe: the DIFFERENCE is what matters
    cmp  ax, OS88_DBLCLK        ; 9 ticks, ~0.5 s
    jae  .first                 ; too slow - restamp and treat as a first click
    ; ...double-click
```

`sub` then `cmp`/`jae` on unsigned words **is** wrap-safe for this question:
`then = 0FFFFh`, `now = 0002h` gives `0003h`, which is the true elapsed
distance. Every one of the four detectors is written this way and each says so.

**This is not §45.15's `js`-not-`jg` trap.** That one is a question of
*ordering* between two free-running counters — *is the mixer ahead of the
card?* — where the sign of the difference is the answer and `jg` honours the
overflow flag and gets it wrong. This is a question of *distance within a small
window*, where the unsigned difference is the answer directly. Worth stating
because the two look alike and citing the wrong precedent would argue for
Option B on a reason that is not real.

Publish the window as an SDK `%define` beside the slot — free, no cell:

```asm
OS88_DBLCLK  equ 9          ; ticks: the window the kernel's own four
                            ; detectors use (SPEC.md 22/26/38). Match it,
                            ; or your list feels different from every
                            ; other list in the system.
```

## 6. The contract, precisely

- **Valid inside `W_ONCLICK` only.** `[ui_click_t]` is stamped on every
  dispatched `EVT_MDOWN`, and `W_ONCLICK` is only ever reached from one. Read
  from a **worker task** it is whatever the last click in the system was —
  stale, and belonging to another window. Read from a **menu handler** or an
  About handler it is the press that opened the menu, which is real but
  meaningless. The slot cannot police this and does not try: it answers a word.
- **Left button only**, which is automatic rather than arranged. `ui_rdown`
  deliberately never stamps `[ui_click_t]` (`ui.inc:424`, *"Binding"*) so that
  a right press cannot compose with a following left click into a pair the
  user never made. `W_ONCLICK` is left-only anyway.
- **Tier 1 does not disturb it.** The `EVT_MUP` branch must not stamp
  (`MOUSEUP-PLAN.md` §4.4), so after Tier 1 lands `[ui_click_t]` still means
  *the birth tick of the last dispatched press* and `W_ONCLICK` is still
  press-driven.
- **No lock, no state, no failure mode.** One word read. It cannot refuse, so
  there is no CF contract.

## 7. Budget

| | estimate |
|---|---|
| `.text` | **~12 bytes** — 8 for the `OSAPI_SLOT` cell, ~4 for `mov ax,[ui_click_t]` / `ret` |
| `.bss` | **0** — `ui_click_t` already exists |
| `.o88` | **none invalidated** — an append at `0x03A8`, nothing renumbered |

`0x03A8` is the next free cell; `0x03A0` (`api_file_append_sys`) is the current
tail. Verify with `kernsize.py` at implementation as always, but twelve bytes
crosses a rung only if the image was already within twelve bytes of one — which
`docs/KERNEL-MEMORY.md` warns is possible independently of the reported spare,
so it is still worth reporting rather than assuming.

## 8. Testing

There is nothing to test in the kernel — the slot returns a word that four
existing detectors already read, and none of them changes. What needs proving
is that a **package** gets the right answer, and that needs a consumer.

`tests/` is where that belongs (a gate package, not shipped software). The test
worth writing is the one that fails today:

1. A window with one clickable row and an `OSAPI_CLICK_TICK` detector.
2. `tools/os88mouse.py dblclick` on it → detector fires. That verb already
   proves all four button edges and measures the gap in the guest's own 18.2 Hz
   ticks, raising if an edge never arrived or the 9-tick window was missed.
3. Two separate `click` invocations → detector does **not** fire (each ends in
   a 1.5 s settle, so they are ~27 ticks apart).
4. **The one that matters**: `dblclick`, with a multi-second floppy operation
   forced between the two presses so they queue. Birth-tick comparison fires;
   an `OSAPI_GET_TICKS` comparison does not. This is the whole justification
   for the slot and it is the only test that distinguishes it from what a
   package can already do.

Case 4 wants MartyPC (`make marty`) and a real read — and note that MartyPC is
**not disk-accurate** (30x fast on a 16KB read), so the *delay* it produces is
not the field machine's. That does not matter here: the test needs the presses
to queue behind something, not to queue behind something for a realistic
length of time.

## 9. Recommendation: build it with its first consumer, not before

The design is settled and it is twelve bytes, so there is no engineering reason
to wait. The reason to wait is §1.3: **nothing calls it.**

A slot with no caller cannot be tested by anything but a gate package written
to test it, and it locks in a contract against use it has never seen. That was
a serious objection under the old §20.8 and it is a mild one now — the unfreeze
means a wrong contract here is a rebuild — but "mild" is not "none", and twelve
bytes of value delivered to nobody is still twelve bytes.

So: **land this in the same commit as the first package that wants a
double-click.** The design below is done; the work at that point is the cell,
the `%define`, the SPEC section and the gate test, which is an afternoon.

What would trigger it, in rough order of likelihood:

- A package with a **list that opens things**, which is where the kernel's own
  four detectors all are. ModPlug's playlist is the shape, though it has made
  its choice and should keep it.
- **Word-select in a text editor.** Note Pad and ArtfulType both have selection
  (§27.8, §46) and neither has double-click-to-select-word; it is the standard
  gesture and the one a user is most likely to try and find missing.
- **Any package that starts to grow its own timer** and gets it wrong. Worth
  watching for in review: `OSAPI_GET_TICKS` stored across two `W_ONCLICK`
  calls is the signature, and it is a bug every time.

If you would rather have it on the shelf regardless — it is twelve bytes and it
is append-only, so that is a defensible call and not one I would argue against.
It just should be made knowingly.

## 10. Proposed SPEC.md text

§13.5 and §13.6 are Tier 1's and are LANDED. **13.7 is free.**

Section numbers that do not exist yet are written **without a `§`**, and the
new cell is called a *cell* rather than a *slot* — `tools/checkdocs.py`
resolves `§n.n` against SPEC.md's headings and the word *slot* followed by a
hex address against `os88api.inc`, and fails `make` on anything that does not
resolve. Restore both spellings when this lands.
`MOUSEUP-PLAN.md` §11 has the longer note, including the trap that the gate
reads `git ls-files` and so cannot see an untracked file.

> ### 13.7 A package's double-click — `OSAPI_CLICK_TICK` (cell 0x03A8)
>
> `out AX` = the **birth tick** (`EV_C`, §10) of the click being dispatched.
> Valid inside `W_ONCLICK`; stale anywhere else, because `[ui_click_t]` is
> stamped on every dispatched `EVT_MDOWN` and `W_ONCLICK` is the only package
> callback reached from one.
>
> It exists because **a package cannot otherwise detect a double-click
> correctly.** The only clock it can reach is `OSAPI_GET_TICKS`, which answers
> the *processing* time — and §9's rule is that detection compares birth ticks
> *"so clicks queued behind a slow disk mount cannot collapse into a
> double-click."* On this hardware that is not a corner case: a mount or a
> package load is seconds, under the gfx lock, with presses queueing behind it.
> A package rolling its own detector fires when it should not.
>
> The kernel keeps its own four detectors (§22.2, §26.2, §38.3, §13.5's
> neighbour `ui_tdbl`) rather than routing them through this slot: three of
> them fold the identity into a selection word they already keep, and
> `ui_tdbl` *spends* a completed pair where the others *restamp*. The slot
> publishes the **fact**, not the policy — the window (`OS88_DBLCLK` = 9
> ticks) and the spend-or-restamp choice are the caller's, and the compare is
> `sub` then `cmp`/`jae`, wrap-safe on unsigned words because the difference
> is the answer.
>
> A right press never stamps `[ui_click_t]` (§12.4), so this can only ever
> answer about a left one.

## 11. What this does NOT change

- **No kernel detector moves.** All four keep their own state, their own
  constants and their own spend-or-restamp behaviour.
- **No package behaviour changes** — the slot has no callers until one is
  written. ModPlug's playlist keeps click-to-select / click-again-to-play,
  which is a good gesture on this machine and not a workaround to be undone.
- **Nothing about which edge anything fires on.** §13.6's rule stands: the
  double-click stays on the **down** edge, where it is most responsive, and
  this slot serves only elements on that side of the boundary.
- **No `.o88` invalidated**, no renumbering, no SDK breakage — an append.
