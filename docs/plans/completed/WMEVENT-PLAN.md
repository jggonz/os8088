# The package-side mouse-up, and what the WM surface is carrying — plan

> **STATUS: `W_ONMOUSEUP` IMPLEMENTED** (SPEC.md §13.7; `kernel/wm.inc`,
> `kernel/ui.inc`, `kernel/kernel.asm`, `apps/os88api.inc`), gated by
> `tests/muptest` on all three adapters, at **API 0x01F0 — a REUSED retired
> cell** (SPEC.md §20.3.1), which is what kept it small: `.text` +141 against
> 168 left in the image rung, so **the footprint spare is still three steps
> and that rung has 27 bytes left**. **The §2 withdrawals were NOT done** —
> §2.4 is why, and it is a reversal of what §2 recommends.

**Tier 3 of the mouse-up work.** One new window callback, plus the API audit
the §20.8 alpha unfreeze makes actionable.

It **depends on Tier 1** (`docs/plans/completed/MOUSEUP-PLAN.md`) in a way Tier 2 does not —
see §5. Land Tier 1 first, or this builds Tier 1's machinery and Tier 1 becomes
two lines on top of it.

---

## 1. A correction: the CONTENT/GEOM merge is dropped

When I ranked the tiers I called merging `OSAPI_WM_CONTENT` (0x0098) and
`OSAPI_WM_GEOM` (0x01B0) *"the real prize"* — 42 + 28 call sites, most of them
paying two crossings for origin-and-size, and a merge that the old freeze
specifically prevented. **Measured, that is wrong, and it is wrong twice.**

Counting call sites across every tracked `.asm`/`.inc` and treating two calls
within 8 lines as a pair:

| | sites |
|---|---|
| **adjacent pairs** — the only sites a merge helps | **15** |
| `WM_CONTENT` **alone** | **48** |
| `WM_GEOM` alone | 14 — **9 of them a CF branch**, i.e. a visibility gate, not a size query |

So a merge saves **15 crossings in the entire tree**, about 450µs total at
~30µs a crossing (§6). That is not a number worth rebuilding every `.o88` for.

And the register story makes it actively worse. `WM_CONTENT` answers
`AX`=left, `DX`=top; `WM_GEOM` answers `CX`=width, `DX`=height — **`DX`
collides**, so a merged call must spend four registers. The 48 sites that want
only the origin would start paying `CX` they did not spend before, on a machine
with eight of them. `wm_content` is *eight bytes of body*, so there is no
`.text` saving either.

**The two calls are not a redundant pair. They are three different questions** —
where is my content, how big is it, and am I visible — that happen to share a
prefix in their names. Left alone.

## 2. The audit, with real numbers

Sweeping every `OSAPI_*` in the SDK for callers outside `os88api.inc`, across
`apps/`, `drivers/` **and** `tests/`:

| slot | callers | verdict |
|---|---|---|
| `OSAPI_XMEM_ALLOC` | **0** | withdraw — but see §2.4 |
| `OSAPI_XMEM_FREE` | **0** | withdraw — but see §2.4 |
| `OSAPI_XMEM_COPY` | **0** | withdraw — but see §2.4 |
| `OSAPI_VOL_PAINT` | **0** | withdraw — its one grep hit is a *comment* in `drivers/os88drv.inc:283`, not a call |
| `OSAPI_BATCH_END` | 0 | **keep** — dead *by design*, see §2.2 |
| `OSAPI_WM_TITLE` | 1 (`tests/gfxbench`) | **keep** — see §2.3 |
| `OSAPI_XMEM_CAPS` | 2 (Task Manager, `sysbench`) | keep |

That corrects the tier-ranking sweep, which counted `apps/` and `drivers/`
separately from `tests/` and so reported `OSAPI_WM_TITLE` as having zero
consumers. It has one. The genuinely dead surface is somewhere else entirely.

### 2.1 The XMS store is four-fifths dead, and that is a §53.6.1 leftover

**Correction first: there are FOUR `xm_*` cells, not five, and THREE are
dead.** An earlier draft of this table listed an `OSAPI_XMEM_INFO`; **there is
no such slot.** The sweep that found the dead surface read its names out of
`os88api.inc`; the verification pass that followed used a list I typed by
hand, and a name that does not exist trivially has zero callers. The table
holds `xm_caps`, `xm_alloc`, `xm_free`, `xm_copy` — and `CAPS` is the live one.

`kernel/xmem.inc` is **625 lines of code** (of 1,013 with comments). Of the
four slots §41 publishes, **one is called** — `OSAPI_XMEM_CAPS`, by the Task
Manager and `sysbench`, and in both cases only to *print* a "XMS used/sizeK"
line. Nothing in the tree ever allocates from the store.

Inside the kernel it is the same picture: the only internal callers are
`xm_init` (boot overlay) and `xm_arm`. **Nothing calls `xm_alloc`, `xm_copy`,
`xm_free` or `xm_info` at all** — they are reachable *only* through four slots
that nobody calls.

This is the residue of SPEC.md §53.6.1, which removed the XMS desktop
snapshot — the one kernel-side consumer — and left the store standing on an
explicit justification: *"§41 stays (the `xm_*` slots are a published package
ABI, §20.8 rule 4 … this was one kernel-side consumer of the store, not the
store)."* **That justification was the freeze**, and the freeze is gone.

**This was in scope for Tier 3 and was not done — §2.4.**

**Out of scope, and flagged rather than done: whether §41 should exist at
all.** That is a bigger question — it touches SPEC.md §41 whole, the Task
Manager's memory display, `sysbench`'s row, and the A20 handling — and it
deserves its own investigation rather than being smuggled in behind a mouse-up
change. It is worth taking, though, and **the reason to take it is Tier 4**:
the UI element helpers need most of `KERN_BUDGET`'s remaining spare, and this
is the largest block of provably unreferenced code in the kernel.

### 2.4 The withdrawals were NOT done, and the reason is renumbering

§2 says withdraw five cells. **They are all mid-table** — the `xm_*` three at
0x0198..0x01A8 and `VOL_PAINT` at 0x0288, against a tail of 0x03A0 — and the
table is 8 bytes per cell at fixed offsets, so removing any of them **shifts
roughly ninety cells below it**. Every `%define` in `os88api.inc` under the
hole changes value, every `.o88` is rebuilt, and the whole of that is bought
for **24 bytes of table**.

Under §20.8's alpha unfreeze that is *legal*. It is not *worth it*, and this
is exactly the case rule 4 still binds on: renumbering is "expensive and
deliberate", and a 90-slot shift on my own initiative, for 24 bytes, in the
same commit as a feature, is not deliberate — it is a large silent-failure
surface attached to a change that has nothing to do with it.

The alternative that keeps the numbering — pointing the dead cells at a
refusing stub — reclaims nothing at all: the cell still costs 8 bytes and the
bodies stay reachable through it.

**So the real prize was never the cells; it is the BODIES**, and those belong
to the §41 question §2.1 already flags as out of scope. `xm_alloc`, `xm_free`
and `xm_copy` are hundreds of lines and are reachable only through three cells
nobody calls — but removing them orphans `xm_ucopy`, `xm_chk` and `xm_bios`
in turn, and touches what the Task Manager and `sysbench` display. That is its
own investigation with its own gate, and it is now **the obvious place to look
for the 512 bytes this tier's rung crossing spent** (§6).

### 2.2 `OSAPI_BATCH_END` is dead by design — keep it

Zero callers, and that is correct rather than stale. SPEC.md §18.9.3 makes it
**optional**: `gfx_unlock` ends any open batch, so *"an unclosed batch is
impossible rather than merely rare."* The slot exists so a caller that wants to
close one early can. Withdrawing it would save 8 bytes and remove the only way
to express something the design deliberately allows.

Worth re-asking once — *if the only way to close a batch is `gfx_unlock`, is
the verb needed?* — but the answer is not obviously yes, so it is not a Tier 3
action.

### 2.3 `OSAPI_WM_TITLE`, and `W_ONSIZE` — both keep

`OSAPI_WM_TITLE` has one caller and it is a benchmark. A test is a legitimate
caller: `gfxbench` times the retitle path because SPEC.md §11.92 exists to make
it cheap, and that measurement is how anyone knows it still is. The kernel body
stays regardless — `wm_title_set` is used internally by the file manager
(`files.inc:2661` through the cold thunk). Withdrawing the cell would reclaim
**8 bytes** and cost a benchmark row.

`W_ONSIZE` has one consumer (Paint) and is load-bearing for it. Moving it to a
side table to free a record slot saves nothing — the record grows into `.bss`
either way at 12 × 2 bytes (§6) — and costs a `wm_ptr2idx` on every resize
negotiation. Leave it.

## 3. What `W_ONMOUSEUP` actually buys, honestly

Less than "packages cannot do mouse-up", which is what the tier ranking
implied. **A package can do it today**, by polling `OSAPI_MOUSE` in a loop from
its `W_ONCLICK` — the `sol_drag` / `pt_wait` shape, which several packages
already use for dragging. `osapi_mouse` calls `kbm_poll`, so the keyboard mouse
works there too.

What the callback buys is what Tier 1 buys the kernel, for the same three
reasons (`MOUSEUP-PLAN.md` §3):

1. **One keypress instead of two on the keyboard mouse.** A spin loop does not
   return, so `kbm_ui` never runs and the latch stays down until the user
   presses again. An arm-and-return handler ends its pass and the release is
   posted for it.
2. **No lock-held polling.** A tracking loop must unlock/yield/lock and pace to
   the tick or it spends the machine (SPEC.md §7.1.3).
3. **Kernel and packages get the same shape**, so `MOUSEUP-PLAN.md`'s guards
   are written once and mean one thing.

Modest, real, and worth stating plainly so nobody builds it expecting a
capability that already exists in a worse form.

## 4. The contract

**The split is: the kernel guarantees delivery, the package decides identity.**
The kernel cannot know what a package's "elements" are — there is no widget
layer (that is Tier 4) — so it must not try to answer "the same control". It
answers *"here is the release for the press you were given"* and the package
hit-tests it against whatever it drew.

```
W_ONMOUSEUP  equ 26          ; word: near ptr or 0. CX = x, DX = y, SI = win.
WIN_SIZE     equ 28          ; was 26
```

- **Not a template word.** `wm_create` copies `mov cx, 8` words from the
  caller's template; growing that count would read one word past **every
  existing package's template**. So it is set after `wm_create` through a
  setter cell, and `wm_create` zeroes it explicitly — the `W_MENUS` /
  `W_ONSIZE` line at `wm.inc:606-607`, one more of the same.
- **`OSAPI_WM_ONMOUSEUP`** — the next free cell. The tail is `0x03A0` today, so
  `0x03A8`, unless Tier 2 lands first and takes it (`DBLCLICK-PLAN.md` §4.1),
  in which case `0x03B0`. Modelled on `OSAPI_WM_ONSIZE` (0x0220), which
  installs a callback the same way for the same reason.
- **Delivered only if this window's `W_ONCLICK` ran for the matching press.**
  A press that merely raised a background window, or landed on chrome, or was
  swallowed by `fdlg_grab`, produces no release callback.
- **Delivered even when the release is outside the window** — that is the whole
  point. The package needs to un-draw its pressed state and *not* fire, and it
  cannot do either if the kernel silently drops the release. Coordinates are
  screen coordinates and may fall outside the content box; converting and
  range-testing is the package's job.
- **Same environment as `W_ONCLICK`**: UI task, under the gfx lock, billed to
  the owning instance via `inst_win_owner` / `task_cycles` / `inst_charge`,
  with `snd_disp_set` stamped. It is the same block of code and should be
  factored, not copied.
- **Opting into this and spin-polling in `W_ONCLICK` are contradictory.** The
  loop consumes its own release; do one or the other.

Registers grow no wider: `WIN_SIZE` is unconsumed by any package (verified
across `apps/`, `drivers/`, `tests/` — zero references outside the SDK's own
`%define`), so appending at 26 invalidates nothing.

## 5. It reuses Tier 1's arm — this is the sequencing constraint

Tier 1 records *"armed on window W, region R"* on the press and re-tests on the
release. `wm_hit` already answers `AL` = 0 content / 1 title / 2 close / 3
minimize / 4 grow. **Tier 1 arms on `AL` 2 and 3; Tier 3 arms on `AL` 0.** Same
word, same guards, same disarm, one mechanism.

So the four guards of `MOUSEUP-PLAN.md` §4 are inherited rather than rebuilt:
identity re-test at the release, the `W_FLAGS` bit 1 recheck, the
`arm && !button && evq_count == 0` disarm, and `[ui_click_t]` staying on the
down edge. **The third of those is the one that has to be right** — a package
callback is a far call into someone else's segment, and firing a stale arm at
one is worse than firing a stale arm at `app_close_win`.

One difference: Tier 1's arm fires a *kernel* routine, this fires a *package*
one, so the fire path needs the billing block. That is the only new code.

**If Tier 3 is built before Tier 1**, it builds the arm and Tier 1 reduces to
adding `AL` 2 and 3 to it. Either order works; building them as two independent
mechanisms does not, and would be the thing to catch in review.

## 6. Budget — MEASURED

```
kernsize[big]: sections   text 55,473 +141  bss 4,943 +27   (sum +168)
kernsize[big]: rungs      image 60,416 +0 (0 left, was 168)
kernsize[big]: footprint  KERN_SIZE 96,768 of 98,304 -> 1,536 spare (3 steps), was 1,536  [+0]
```

**It very nearly crossed a rung.** The first build appended the
cell at 0x03A8 and came to `.text` +151 / `.bss` +27 = **178 against the 168
that were left**, crossing by ten bytes and taking the footprint spare from
three steps to two. Putting the slot on the **reused retired cell** at 0x01F0
instead (SPEC.md §20.3.1) removed the appended cell's eight bytes and two more
of stub, landing on **168 of 168 — exactly zero left in the image rung.**

So §20.3.1's free list paid for this tier on its first use, and that is luck
in the size of the margin rather than in the direction: an append costs eight
bytes that a reuse does not, every time.

**THE IMAGE RUNG NOW HAS ZERO BYTES LEFT.** The next byte of `.text` or
`.bss` anywhere in the kernel buys a whole 512-byte step. That is the first
fact for whoever touches it next, and §2.4 says where to look for room.

**It crossed a rung on the first build, exactly as predicted.** `.bss` +27 is the record growth
(12 windows × 2 for `W_ONMOUSEUP`, plus 3 that were Tier 1's). `.text` +151 is
four times the ~37 estimated, and the estimate was wrong because it counted
only the new code: the API cell (8), the setter, the `wm_create` zero, the
`.mup_pkg` branch, **and `ui_bill`** — the billing block factored out of
`.content_front` so `W_ONCLICK` and `W_ONMOUSEUP` share one body. Factoring
was supposed to pay for itself and roughly did; the estimate simply never
included the branch or the cell.

Footprint spare is **1,024 bytes, two 512-byte steps**, down from three. That
is inside normal operating range (CLAUDE.md's standard is four) but it is a
step spent, and §2.4 says where the next one is if it is wanted back.

kern_small (`KERN_SMALL=1`) is a separate build and remains unmeasured.

## 7. Testing

The kernel half is Tier 1's test matrix with a package in place of the chrome,
so it needs a **gate package in `tests/`** — one window, one drawn rect, a
`W_ONMOUSEUP` that reports what it was given.

| # | gesture | expected |
|---|---|---|
| 1 | click inside the rect | `W_ONCLICK` then `W_ONMOUSEUP`, both inside |
| 2 | press inside, release outside the rect | both fire; the release reports a point outside — package cancels |
| 3 | press inside, release **outside the window** | both fire; coordinates outside the content box |
| 4 | press on the title bar, release over the rect | **neither** fires (§4: no `W_ONCLICK`, no release) |
| 5 | press on a background window's content, release | raise only, no callbacks |
| 6 | press inside, window destroyed by its worker, release | nothing, no fault |
| 7 | keyboard mouse, NumLock off: **one** keypress over the rect | both fire, same point |
| 8 | a window that registers **no** `W_ONMOUSEUP` | unchanged in every case |

Case 8 is the regression test for every shipped package and the one that must
not be skipped: none of them registers the callback, so none of their
behaviour may move.

Case 3 is the one a naive implementation fails by "helpfully" clamping or
dropping the out-of-window release.

MartyPC (`make marty`), `tools/os88mouse.py` — the `menu` verb is
press-drag-release and is what cases 2–4 need.

## 8. Proposed SPEC.md text

§13.5 and §13.6 are Tier 1's and are LANDED; 13.7 is Tier 2's. **13.8 is free.** Section numbers
that do not exist yet are written without a `§`, and a not-yet-real cell as a
*cell* — `MOUSEUP-PLAN.md` §11 has the note and the untracked-file trap.

This also **edits §11's record table** (adding `W_ONMOUSEUP`, `WIN_SIZE`
26 → 28) rather than only adding a section, and **§41's slot list** to drop the
four withdrawn cells.

> ### 13.8 A package's mouse-up — `W_ONMOUSEUP`
>
> Set with `OSAPI_WM_ONMOUSEUP` after `wm_create`, never in the template: the
> template copy is eight words and growing it would read past every existing
> package's. `CX` = x, `DX` = y, `SI` = window, under the gfx lock on the UI
> task, billed exactly as `W_ONCLICK` is.
>
> **Delivered when, and only when, this window's `W_ONCLICK` ran for the
> matching press** — so a raise, a chrome press or a press a modal dialog
> swallowed produce nothing — **and delivered even if the release lands
> outside the window.** That is the point rather than an edge case: a package
> must be able to un-draw a pressed state and decline to act, and it can do
> neither if the release is dropped. The coordinates are the screen's and may
> be outside the content box.
>
> **The kernel guarantees delivery; the package decides identity.** There is
> no widget layer, so the kernel cannot know what "the same control" means
> here and does not try — it answers *the release for the press you were
> given* and the package hit-tests it against what it drew.
>
> It shares §13.5's arm: `wm_hit`'s `AL` 2 and 3 fire the chrome, `AL` 0 fires
> this, one word of state and one set of guards. A package that opts into this
> **and** polls `OSAPI_MOUSE` in a tracking loop from its `W_ONCLICK` is doing
> the same job twice; the loop consumes its own release.

## 9. What this does NOT change

- **No shipped package's behaviour.** `W_ONMOUSEUP` is opt-in and nothing
  registers it. `WIN_SIZE` is unconsumed outside the SDK, so appending breaks
  nothing — but **every `.o88` is rebuilt anyway**, because the withdrawn cells
  in §2 renumber nothing yet still change the table the SDK publishes. That is
  the fixed price of a table revision and the reason to batch it (§10).
- **`OSAPI_WM_CONTENT` and `OSAPI_WM_GEOM`** — both stay, both unchanged (§1).
- **The XMS store's `CAPS` cell and everything the Task Manager displays.**
  Only the four uncalled cells go.
- **Which edge anything fires on for existing code.** `W_ONCLICK` is still
  dispatched on the press, and §13.6's boundary rule is untouched.

## 10. Order of work — DONE (except §2)

1. ~~Tier 1 first, so the arm exists.~~ **Done** — and it was reused rather
   than rebuilt: `wm_hit`'s `AL` 2/3 fire the chrome, `AL` 0 fires this, one
   `[ui_armw]`/`[ui_armr]` pair and one `ui_arm_chk`.
2. ~~The withdrawals of §2 as their own commit.~~ **NOT done — §2.4.**
3. ~~`W_ONMOUSEUP`, the setter cell, the `wm_create` zero, the factored
   billing block.~~ **Done.** `W_ONMOUSEUP` at record offset 26 (`WIN_SIZE`
   26 → 28), `OSAPI_WM_ONMOUSEUP` appended at **0x03A8** — an append, so
   nothing renumbered — and `ui_bill` is the shared dispatcher.
4. ~~The gate package and cases 1–8, CGA first.~~ **Done** — `tests/muptest`,
   `make build/muptest.img`, four cases on CGA, Hercules and VGA.
5. ~~SPEC edits.~~ §13.7 landed. §11's record table and §41's slot list were
   **not** touched, the latter because §2 did not happen.

### 10.1 What the gate proved

`tests/muptest` is 165 bytes and answers every question as *a window that is
there or not* — its `W_ONMOUSEUP` hides itself — so the harness reads
`wm_wins` and nothing is screen-scraped.

| case | expected | |
|---|---|---|
| A | press inside, release inside | release arrives |
| B | press inside, release **outside the window** | still delivered — §4's second rule |
| C | press the **title bar**, release | **no** release: no `W_ONCLICK` ran |
| D | a window that never registered one (About) | wholly unaffected |

**C is the case only a package can prove**, and it is why this needed a gate
package rather than more scripted clicking: the kernel cannot test it from its
own side, having no way to know a package expected nothing. **D is the
regression test for every shipped package** — none registers the callback, so
none of their behaviour may move.

Tier 1's full matrix was re-run afterwards on all three adapters, because
`.content_front` and `.mup` were both refactored under it: all pass, keyboard
mouse included.

**One apparatus note:** the harness must derive the Disk window's first row
from the window's **own rect**, not a fixed coordinate. The window lands
differently on each adapter — CGA clamps it, Hercules is 720x348 — so a
hardcoded row silently double-clicks empty content and the test reports "the
package did not launch" about a working kernel. That cost a run on two
adapters before it was noticed.

**Not run:** anything on the field machine. Nothing here touches a disk.
