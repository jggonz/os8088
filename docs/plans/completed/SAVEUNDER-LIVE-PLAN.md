# A live answer to "can I save under?"

> **COMPLETED. The saveunder work landed after this was written and the
> document was never updated** - which is why it sat under "nothing built"
> while the thing it asked for had been shipped, published and given a second
> consumer.
>
> **Both verbs this study concluded were needed are published API:**
> `OSAPI_WM_SAVEU` (slot 0x0378) - the live "may I be banked?" answer that §1
> argues `WF_SAVEU` should have been all along - and `OSAPI_WM_BAND`
> (slot 0x03B8), which §3 names as *"what is left for a colour game that wants
> a paused cache"*. SPEC.md §11.96.11.4 is the fence they share, and
> SPEC.md §11.96.11.2 and §11.96.11.3 are the band's lifetime and the 1bpp
> case. **Paint is the worked consumer**: `pt_sucache` calls both
> (SPEC.md §42.13.4).
>
> So the *"0 kernel bytes"* verdict below is the one thing here that did not
> survive - the mechanism was already there for the question as first put, and
> the packages then needed a way to reach it, which cost two slots. Everything
> else in the file stands, and §3 is why the number was never the whole story.

**Status (as written, and superseded by the box above): investigated, nothing
built.** This is a design record, not a
handoff for work in flight. Its conclusion is that the kernel already does
what was asked and the work is in the packages — so the number this document
exists to produce is **0 kernel bytes**, and §3 is why that number is not the
whole story.

Subject: SPEC.md §11.96's raise cache, and its opt-in `WF_SAVEU` (SPEC.md
§11.96.1). The question put was whether a window with a background worker
could hold a save-under outside a drag, by answering "may I be banked?" as a
**mode it is in now** rather than as a property of the package.

## 1. The mechanism asked for is already there

`WF_SAVEU` reads like a classification and is not one. SPEC.md §11.96.1 ends
with a list — "Marked today: Note Pad, Minesweeper, Piano, Solitaire … and
the Disk window" — and that list is what makes it look decided once per
package. The code underneath it is a plain live flag:

* **It is per WINDOW, not per package.** The bit is `W_FLAGS` bit 5 in the
  window record, so two instances of one package can differ, and a pooled
  slot is reset by `wm_create` (`W_FLAGS = 1`) rather than inherited.
* **The API is a bare set/clear with no latch.** `wm_saveu` (0x0378) tests
  `AL` and does one `or` or one `and`. There is no "already promised" test to
  trip over and no once-only door.
* **The clear already destroys the cache.** `wm_saveu`'s `.clear` path is
  `and word [bx+W_FLAGS], ~WF_SAVEU` followed immediately by
  `call wm_su_drop`. That is the third bullet of the proposal, shipped.
* **The take reads the flag at the moment it takes.** `wm_su_take` asks
  `wm_dc_ok`, which tests `WF_SAVEU`; `wm_su_precover` tests it again as it
  scans. Neither consults anything cached from window creation. That is the
  first and second bullets, shipped.
* **The restore does not read the flag at all.** `wm_su_try` validates by
  rect through `wm_su_ck`. It does not need the flag, because a cleared flag
  has already freed the claim.
* **A shipped application already toggles it at run time.** `apps/paint`'s
  `pt_sucache` both sets and clears `WF_SAVEU`, and it is called from
  `pt_onresize` (SPEC.md §11.98) as well as from entry — so the live toggle
  is not theoretical, it is on a path that runs when a two-card machine drags
  Paint from one adapter to the other.

The ordering inside `wm_saveu` is the one that has to be right, and it is:
the flag is cleared **before** the drop, so a take that pre-empts between
them refuses on the flag rather than racing the free.

So the answer to "how much would this cost in kernel bytes" is **zero**. No
new flag, no new API slot, no new hook. What the proposal changes is SPEC.md
§11.96.1's framing, and then each application.

## 2. The asymmetry that makes it safe

The transition matters in only one direction, and it happens to be the
direction that is already on the safe task.

* **not-live → live** must reach the kernel promptly, because from that
  instant the app's model can move without the glass moving. Getting it late
  is a **correctness** bug.
* **live → not-live** can be arbitrarily late. A cache not taken is a repaint
  not skipped. Getting it late is a **speed** bug.

Every live-entering transition in the current application set is raised by
the **UI task**, usually from a callback that already holds the gfx lock:

| app | enters live at | context |
|---|---|---|
| Browser | `br_go` — a menu command, Return in the location bar, a form's Submit | UI task, **gfx lock already held** |
| Cyclone / Missile / Arkanoid | unpause, new game | `W_ONKEY` / menu command |
| Timer | the Start button | `app_tmr_act`, off `W_ONMOUSEUP` |

`apps/browser/brnet.inc`'s own banner says it: "the UI task claims the three
buffers and starts the fetch (`br_go`) … all of which already run with the
gfx lock held". That is exactly the context in which `wm_su_drop` → `mem_free`
is safe, and it is the context `wm_clip_set` already calls it from.

The other direction — a fetch completing, a game ending, a timer expiring — is
discovered by the worker, and is the direction where being late costs nothing.
So a worker can stage a byte and let the UI task set the flag on
`OSAPI_WM_ONWAKE` (0x0458), which is `apps/ftpd`'s shape (SPEC.md §20.6 rule 7)
and needs no new rule.

### 2.1 …and if a worker sets it directly, it must hold the lock

`wm_saveu` is **not** on SPEC.md §20.6 rule 7's list of what a worker may
call, so today it is forbidden by omission. Adding it is a documentation
change with a condition already written for a neighbour: `osapi_set_color` is
on the list "only inside the same lock hold as the drawing it colours",
because `[gfx_color]` is a global with no owner. `W_FLAGS` is the same shape —
`or word [bx+W_FLAGS], WF_SAVEU` is a non-atomic read-modify-write over a word
whose high byte carries the cursor shape (SPEC.md §7.2.1) — and every other
writer of `W_FLAGS` in the kernel runs under the gfx lock. No ISR writes it;
the mouse ISR only reads the high byte. So the amendment is one clause, and
still 0 bytes.

## 3. What actually blocks it, and none of it is the flag

### 3.1 The apps drop their own cache twice a second

This is the finding that matters, and it defeats the proposal on its own if
it is not addressed.

`wm_clip_set` drops the cache of the window that is about to draw — that is
SPEC.md §11.96's blanket safety net, "one call covers every app at once, with
no rule for an app to remember". The drop sits **before** the occlusion walk
and after the hidden test:

```
wm_clip_set:
    test word [bx+W_FLAGS], 2
    jz .gone                    ; hidden: out, no drop
    push ax … si
    call wm_su_drop             ; <-- unconditional for a VISIBLE window
    …
    call wm_clip_occl           ; and only NOW does it discover CF = 1
```

So a window that is **visible but wholly covered** — which is precisely the
state a save-under exists for — drops its cache the moment it arms a clip,
and is then told there was nothing to draw on.

Every live-drawing app arms that clip unconditionally, whether or not it has
anything to draw:

* **`app_tmr_task`** (`kernel/apps.inc`) wakes every 9 ticks and jumps to
  `.draw` even when `TMR_RUN` is 0. Its own banner says "on roughly every
  other wake the displayed second has not moved and `app_tmr_render` returns
  having touched no pixel" — but the lock and the clip were taken first. A
  **stopped** Timer therefore arms a clip region ~2×/second for ever.
* **`cy_worker`** (`apps/cyclone`) calls `cy_render` on every frame, paused or
  not, and `cy_render` calls `OSAPI_WM_CLIP_SET` whenever the window is
  visible. That is ~18 arms a second by a paused game.

Flipping `WF_SAVEU` on such an app produces **no cache at all**: the take
happens at the next `wm_draw_win`, and the app's own next poll destroys it.

**The fix is app-side, it is the decision moved above the arm, and it pays
for itself independently.** Arming a clip is not free — `wm_clip_seed` plus
`wm_clip_occl` walks the z-order and builds rects. It is measured:
PERFORMANCE.md prices Missile Command's `lok` stage (gfx lock + `mc_track` +
`wm_clip_set`) at **9.68 s of a 77 s session, 12.6%**, and SPEC.md §48.13's
row at **6.2 ms of `gfx_lock`+`wm_clip_set` per frame**. At 18.2 Hz a paused
game is spending on the order of a tenth of a 4.77 MHz 8088 arming a region
to draw nothing.

So: **ask "have I anything to draw" before taking the lock, not after.** That
is a win for a paused game whether or not it ever holds a save-under, and it
is the precondition for holding one.

### 3.2 The kernel cannot make that decision, and should not try

Two kernel-side alternatives were considered and both are worse:

* **Drop only when `wm_clip_set` returns CF = 0.** Nearly free in bytes, and
  wrong: a covered window that armed a clip *because its model moved* is
  exactly §11.96.1's hazard, and the kernel cannot tell it from one that
  armed speculatively. Only the app knows.
* **Defer the drop until a primitive actually writes.** Correct, and it fixes
  every app at once the way the current hook does — but there is no existing
  choke point to hang it on. `cur_lazyck` is called from `wm_clip_set`, not
  per primitive, so a new test would land on the hot path of every `gfx_*`
  call on the machine, to serve the frames that draw nothing. That is the
  trade PERFORMANCE.md exists to refuse.

The app-side answer needs no kernel byte and is faster.

### 3.3 The Browser cannot be banked whole on VGA

`wm_su_kb` refuses any claim over `0xFC00` (64,512 bytes) — the buffer is
addressed by a 16-bit offset from one segment, so the ceiling is structural
and not a tunable. The Browser's default window is derived from the adapter
(`br_size`: 90% of the desktop band on VGA and Hercules, the whole band on
CGA), and estimated from that geometry:

| adapter | window | content | planes | claim | |
|---|---|---|---|---|---|
| VGA | 496×392 | 494×373 | 4 | ~92 KB | **refused** |
| Hercules | 496×273 | 494×254 | 1 | ~16 KB | ok |
| CGA | 496×179 | 494×160 | 1 | ~10 KB | ok |

(Estimated from the geometry, not measured on a guest.) A refusal is a safe
no-op — `wm_su_take` just leaves no cache — so a Browser that opted in would
silently get the feature on both 1bpp adapters and nothing on VGA, which is
the machine with the memory to spare.

This is Paint's problem with the adapters reversed (SPEC.md §11.96.11.3), and
it has Paint's answer: `OSAPI_WM_BAND` (0x03A8). A **top** band of ~250 rows
fits under the ceiling on VGA (494 px ≈ 63 bytes a row × 4 planes = 252 bytes
a row, so ~250 rows of the 373), and the app owes the bottom ~123 rows through
`OSAPI_WM_DAMAGE`. That is still two thirds of the page body coming back as a
blit. Like Paint, the arm must be re-asked when the adapter moves under the
window, and a band that is not retired outlives the decision.

## 4. Which windows are actually subjects

SPEC.md §11.96.1's third question — "is its repaint actually expensive?" —
still applies, and it disqualifies both kernel-resident candidates:

* **Bounce** (150×130) is live by definition and never opts in. No change.
* **Timer** (160×60) is ~3 KB banked on VGA against a repaint of eight digit
  cells and three buttons. Not worth the claim even while stopped, and the
  §3.1 rework of its poll loop is worth doing on its own account.

So **no kernel-resident window is a subject**, which is the second reason the
byte count is 0: every application this design is for — Browser, Cyclone,
Missile Command, Arkanoid — is a package, and a package's bytes are on the
disk, not in `KERN_BUDGET`.

## 5. The order to do it in

1. **SPEC.md §11.96.1**: say that the promise is a MODE and not a
   classification, and that the list of marked applications is a list of
   windows that are *always* in it. 0 bytes.
2. **SPEC.md §20.6 rule 7**: `wm_saveu` joins the worker's surface under
   `osapi_set_color`'s condition — inside a gfx lock hold only. 0 bytes.
3. **Per app, and this is the work**: move the "have I anything to draw"
   decision above the `OSAPI_GFX_LOCK` / `OSAPI_WM_CLIP_SET` pair (§3.1).
   Standalone win; do it whether or not step 4 follows.
4. **Per app**: clear `WF_SAVEU` where the app enters live drawing (UI task,
   lock held — §2) and set it where it leaves, staging through
   `OSAPI_WM_ONWAKE` if the worker is what noticed.
5. **Browser only**: name a top band on VGA so the claim fits (§3.3).

## 6. What has not been checked

* No guest has run any of this. The claim sizes in §3.3 are arithmetic off
  `br_size` and `wm_su_rect`, not a reading from a machine.
* The games' window geometry is fixed up at run time (`mc_tpl`/`ark_tpl` are
  `dw 0,0,0,0`), so whether they clear the ceiling on VGA is unmeasured.
* Whether a Browser page body's repaint is expensive enough to be worth a
  banded cache is asserted from SPEC.md §11.96's 578 ms of lettering on a
  Note Pad-sized window, not measured on a page.

## 7. A window that declares its content 1bpp

Asked after §3.3: could a window register "I am always 1bpp" and have the
kernel skip the other three planes — or does black on VGA need all four
written?

### 7.1 Black needs all four planes SET, and that is one write

The pixel value really is 0 in every plane, and 15 in every plane — but the
CPU writes it **once**. `kernel/vga12.inc`'s own banner says so, about
`gfx_blit1`'s default pen (SPEC.md §5.4.2.2):

> Map Mask 0Fh with Set/Reset disabled puts one CPU byte in all four planes,
> so a set bit is colour 15 and a clear one 0 — which is why the default costs
> nothing

That is the framebuffer's **resting** state, and a 1bpp band blitted onto a
VGA already ships on it. So a 1bpp cache is not a trade of memory against
write traffic: it is **4× less of both**. Today's `vga_restore_vram` sets
`SEQ2` Map Mask to one plane and `rep movsb`s four times; a 1bpp restore sets
it once to 0Fh and moves a quarter of the bytes.

### 7.2 The plumbing is already depth-parameterised

`wm_su_pw` (SPEC.md §39.14.8, docs/plans/completed/DUAL-DISPLAY-VGA.md §4.3) is a **per-cache**
plane count, read by `wm_su_bytes`, `wm_su_scrset`, `wm_su_edge` and
`wm_su_merge` — the sizing, the scratch, the restore stride and the merge. It
exists because a window banked on a Hercules is already a depth-1 cache. What
is missing is only that `gfx_save`/`gfx_restore` take their depth from the
**display** rather than from the caller.

Both loops need less than they look. The restore's already terminates
correctly on a broadcast: seed `vga_plane` with 0Fh and one pass runs, then
`add al, al` gives 1Eh, which fails `cmp al, 0x10 / jb .plane`. The save's
`cmp byte [vga_plane], 4` becomes a compare against a byte.

**Measured, not estimated**: two knobs at rest (`4` and `1`), both loop
changes and an arm/disarm pair come to **+27 bytes of `.text`**
(55,301 → 55,328 on `kernsize[big]`, prototyped and reverted). The policy half
— the promise, the alignment gate below, and arming it at the three
`gfx_save`/`gfx_restore` sites in `wm_su_take`, `wm_su_try` and `wm_su_edge` —
is **not** measured and is guessed at 60–100 more.

The declaration wants no new API slot: `wm_saveu`'s `AL` is tested `or al, al`
today, so bit 1 can carry "and my content is only colours 0 and 15" beside
bit 0's promise. The two belong together — the 1bpp claim means nothing
without the save-under it is for.

A bonus falls out: `wm_dc_take` refuses a drag that crosses a depth change
(a Hercules-banked window replayed onto a VGA arrives as magenta and cyan
noise). A cache that is 1bpp **by declaration** rather than by which card it
came off can cross that seam, because the bytes mean the same thing on both.

### 7.3 The gate is the EDGE, and byte alignment is the cheap answer

`gfx_restore` writes whole bytes — x1 rounded down, x2 up — so a restore
overhangs up to 6 px left of the window's frame and 5 px right of its drop
shadow. §11.96.2's `wm_su_edge` handles that by reading the screen **now** in
those two byte columns and patching the outside bits into the buffer before
the restore.

A 1bpp buffer cannot carry those bits. They belong to the desktop, another
window or the dock, and on VGA they can be any of sixteen colours; broadcast
back through Map Mask 0Fh, a neighbour's colour 9 becomes 15 and its colour 6
becomes 0 — an 11 px stripe of flattened colour down the full height of the
window.

The cheap fix is to offer the deal only on a rect with **no overhang**:
`x1 & 7 == 0` and `x2 & 7 == 7`. Then `vga_lmtab`/`vga_rmtab` both answer FFh,
`wm_su_merge` keeps every bit from the buffer, and the whole edge path is a
correct no-op.

**Half of that is already true for free.** SPEC.md §11.94 snaps a window's
content origin onto a multiple of 8 unless it sets `WF_NOSNAP`, and
`wm_su_rect`'s x1 *is* that origin — so the left edge aligns on every ordinary
window. Only the right needs the app's help, and it is the app's own width:
the Browser at 496 gives a content of 494, six pixels into its last byte;
498 would give 496 and land exactly.

The alternative — keeping the two edge columns in four planes and the interior
in one — is correct and costs a second path through `vga_restore_vram`, which
is the routine the mouse cursor reaches out of IRQ4. Not worth it against an
app choosing an even width.

### 7.4 …but the Browser is not 1bpp on VGA today

It uses three colours in its content, not two. `browser.asm` sets
`OS88UI_DIS` on Back and Forward, `apps/os88ui.inc`'s pen is SPEC.md §47
rule 1's pair — `CDGRAY` **and** `[gfx_dis]` — and `font_ink` clears
`[font_dith]` on the VGA path (`cmp byte [vid_mono], 0 / je .out`). So a
greyed caption is a **checkerboard on Hercules and CGA and solid colour 8 on
VGA**, which is exactly SPEC.md §39.4's asymmetry working as designed. A
disabled *icon* takes the same fork in `os88ui` at its `UI_ISMONO`.

Back is greyed on the first page of a session, so this is the ordinary state
rather than a corner.

Two ways out, and they are different features:

* **Cache-only 1bpp** (what §7.1–7.3 costs). The app must genuinely draw two
  colours, so the Browser would have to stop greying, or grey by stippling
  itself. It qualifies only after that.
* **Render-as-1bpp**: a window declares it and the *renderers* take their
  mono paths on VGA. `font_ink` is nearly free — its mono reduction already
  produces 00h/FFh, which the glyph loop turns into colour 0 and 15 across
  four planes — so text would be one extra test. But solid fills, the icon
  stipple and every `gfx_*` colour would each need the same fork, and the
  blast radius is the whole graphics layer rather than the cache.

The first is the honest scope. The second is a different plan and should not
be smuggled in as an implementation detail of this one.

### 7.5 Missile Command is rejected on size, and 1bpp does not save it

Its window is seven eighths of the desktop band (`mc_entry`), so on VGA the
content is about 558×362 and the four-plane claim about **100 KB** — past
`wm_su_kb`'s 64,512 ceiling with room to spare, paused or not. At 1bpp it
would be ~25 KB and fit, but the game is genuinely colour, so the declaration
is not one it can make.

| | content (est.) | 4bpp claim | 1bpp claim |
|---|---|---|---|
| Browser, VGA | 494×373 | ~92 KB — refused | ~23 KB — fits |
| Missile Command, VGA | 558×362 | ~100 KB — refused | ~25 KB, but it is colour |

(Both estimated from `br_size`/`mc_entry` and `wm_su_rect`, not read off a
guest.)

What is left for a colour game that wants a paused cache is `OSAPI_WM_BAND`:
about 198 of Missile's 362 rows fit under the ceiling as a top band, and the
app owes the rest through `OSAPI_WM_DAMAGE`. Whether replaying half a paused
game board is cheaper than redrawing it is the app's question and is not
measured here.

## 8. It is ONE edge, and there are three ways to pay for it

### 8.1 There is no vertical overhang

A framebuffer byte is eight pixels of ONE row, so rounding only ever happens
on x. `vga_rect_setup` bears it out: `[vga_rows]` is an exact `y2 - y1 + 1`
and rows advance by `ROW_BYTES`, while only x1 and x2 are put through
`vga_lmtab`/`vga_rmtab`. `wm_su_edge`'s own banner says the same thing in
pixels — "up to 6 pixels to the left of the window's frame and up to 5 to the
right of its drop shadow".

Top and bottom therefore need nothing. The left is already handled: SPEC.md
§11.94's snap is on by default and `wm_snap_win` puts `W_X` on a multiple of
8, which is `wm_su_rect`'s x1. **What is left is one byte column on the
right.**

### 8.2 Splitting it out works, and it is the worst shape on an 8088

A 1-byte-wide, full-height, four-plane rect is four row-walks moving one byte
each. `vga_restore_vram`'s row body — `mov cx, bp`, `rep movsb`,
`add si, [cs:gfx_sub_rs]`, `add di, ROW_BYTES`, `sub di, bp`, `dec dx`,
`jnz` — costs about **86 cycles fixed** against **17 a byte** under
PERFORMANCE.md Part 2's `max(clocks, 4.34 × instruction bytes)` fetch floor.
At one byte a row that is **83% loop overhead**.

Browser on VGA, content 494×373 (62 byte columns, 61 of them whole):

| | bytes moved | est. restore |
|---|---|---|
| today, four planes (refused on size anyway) | 92,504 | 356.5 ms |
| 1bpp, byte-aligned width — no edge work at all | 23,126 | **89.1 ms** |
| 1bpp interior + right column as its own 4bpp rect | 24,245 | 120.0 ms |
| 1bpp interior + right column through GC8's Bit Mask | 22,753 | 95.6 ms |

(Arithmetic from the model above, not measured on a guest. The ratios are
what to trust, not the absolute figures.)

So the split is **viable and costs +32 ms on an 89 ms restore — a 36% tax to
carry six pixels of one column.** It also cannot use the fragment machinery as
it stands: `wm_su_flay`'s four fragments are all one depth, read through a
single `WM_SU_PW`, so a mixed-depth claim means `wm_su_bytes`, `wm_su_scrset`,
`wm_su_edge` and `wm_su_merge` each need to know which fragment they are on.

### 8.3 The hardware already has the answer, and it is the Bit Mask

VGA write mode 0 takes each bit either from the CPU byte or, where the Bit
Mask (GC8) is clear, **from the latches** — which a read of the target byte
fills with all four planes at once. So for that one column: read the byte, set
GC8 to the bits the rect owns, Map Mask 0Fh, write the buffer byte. The owned
bits get the broadcast; the rest come back **exactly as they were, in colour,
in every plane**. One read and one write a row instead of four writes.

It also deletes `wm_su_edge` for that cache — no `gfx_save` of the column, no
`wm_su_merge` — and SPEC.md §11.96.8 measures that work at **18.22 ms of a
47.86 ms restore** on a 318×136 CGA Disk window. 38%.

§11.96.2 already considered it and said no:

> A masked write would be the other fix, and it would mean edge read-modify-
> writes in `sw_xfer` and a Bit Mask plus a latch load in `vga_restore_vram` —
> two primitives the cursor is on, to serve one caller.

Both halves of that are weaker now. `sw_xfer` is **not** involved: a 1bpp
adapter's buffer can already carry its neighbour's bits, so nothing changes
there and only the VGA half is in scope. And "a primitive the cursor is on"
is what `gfx_sub_st`/`rs`/`ps` already are — "ALL THREE ARE 0 AT REST, which is
what makes the whole feature additive" — so an armed Bit Mask makes the
cursor's path one `cmp`/`jne` longer rather than forking it.

### 8.4 …but the cheapest fix is to not have a ragged edge

**`wm_snap_win` moves `W_X` and never touches `W_W`.** SPEC.md §11.94's
argument for the origin — quoted in the kernel at `wm_snap_want`, "MODE 12h IS
EIGHT PIXELS TO A BYTE… an unaligned 8×8 cell spills into a second framebuffer
byte there exactly as it does on a Hercules" — applies to the far edge word
for word. Snapping the SIZE as well as the origin is the missing half of a
feature that already exists, and it makes this whole section moot: both masks
come out FFh, `wm_su_edge` is a correct no-op, and the restore is the 89.1 ms
row of the table.

It is not free of consequences. Content width is `W_W - 2` normally and
`W_W - 1` under `wm_flush` (§11.95.2), so the rounding has to ask; and a
resizable window's grow box would move in 8px steps on the horizontal, which
is a **look** decision of exactly §11.94's kind and wants the same treatment —
a knob until somebody has looked at it.

There is a fourth way and it needs no graphics-layer change at all:
`wm_su_flay` already turns a LEFT extent into a full-height fragment
`(x1, y1, x1+l-1, y2)` and bills the app the inset through
`OSAPI_WM_DAMAGE`. Clamping that extent internally to the byte-aligned width
gives an aligned banked rect for free — at the price of the app repainting a
1–7 px column of cells on every raise (about 47 cells, ~42 ms for the
Browser). It collides with an app that named its own bands, so it would apply
only where the left extent is 0.

### 8.5 Recommended order

1. **Snap the width** (§8.4). It is the smallest change, it makes the fastest
   restore, and it is the half of §11.94 that was never written.
2. **1bpp cache** (§7), which is then a clean +27 bytes in the graphics layer
   with no edge case to carry.
3. **Bit Mask** (§8.3) only if a window turns up that must keep a ragged
   width. Do not build the split rect (§8.2): it is more code than the masked
   write and slower than both alternatives.

### 8.6 The Browser's checkerboard needs about eight kernel bytes

Adopting the stipple in the app is right, and the app cannot quite do it
alone. The Browser's toolbar buttons carry **text** captions — `br_s_back`,
`br_s_fwd`, `br_s_rel` through `br_btn1` → `os88ui_btn` — so the greying goes
through `font_ink`, which is the kernel's and gates the stipple on
`[vid_mono]`. A package has no way to reach that.

The hook is one test: take the mono path when the window has declared its
content 1bpp, not only when the adapter is. That is coherent rather than a
special case — a window that promised two colours should have **every** colour
reduced to 00h/FFh, which is exactly what `font_ink`'s mono branch already
does, and the glyph loop turns those into colour 0 and 15 across four planes.
Disabled *icons* fork separately in `os88ui` at its `UI_ISMONO`, and that one
is app-side.

## 9. `SIZESNAP=1` — the double snap, built as a knob

§8.4's "snap the size as well as the origin", behind `make SIZESNAP=1`, on
TITLESNAP's reasoning: it moves pixels the user will notice and nothing here
can tell whether they like it, so it is a knob until somebody has looked.
**Default off; a plain build is byte-identical** (`.text` 55,301 either way).

**+64 bytes of `.text` with the knob on** (55,301 → 55,365), measured.

Three pieces:

* **`wm_snap_w`** beside `wm_snap_ax`, and shaped like it: takes a candidate
  frame width in AX, returns the snapped one, refuses rather than producing an
  illegal rect. Content width is `W_W - 2`, or `W_W - 1` where `wm_flush_ck`
  says the left border is suppressed (SPEC.md §11.95.2), so the rounding asks.
  **Down and never up** — growing could hang the frame off the right edge.
* **`wm_snap_win`** calls it after the x snap. That is the whole of the reach:
  the three existing chokepoints (`wm_fit`, `wm_resize_nb`, the zoom restore)
  and `ui_grow`'s release all end there already, so nothing new is hooked.
  Second and not first, because narrowing can never make a snapped x illegal
  while moving x *can* change `wm_flush_ck`'s answer about the width.
* **`ui_grow`'s tracked outline** snaps too. Without it the XOR rectangle
  follows the mouse freely and the window lands up to 7px inside where the
  hand let go, because the release already ends in `wm_snap_win`. What you see
  is what you get, at the price of the outline stepping 8px horizontally —
  and that step is the thing to look at.

### 9.1 What to look at

1. **The grow box.** It steps 8px across and 1px down. Does the asymmetry
   read as broken or as deliberate? Disk, Note Pad and the Browser are the
   resizable subjects.
2. **Window widths at launch.** `wm_fit` runs `wm_snap_win`, so every window
   can come up to 7px narrower than its template asks for. The Browser is 90%
   of the band (`br_size`) and the games are fractions of the screen, so none
   of them has a width that means anything — but a window whose layout is
   arithmetic off `W_W` may end up with a different last column.
3. **The zoom box** (SPEC.md §11.95), which restores through the same call.

### 9.2 Two known gaps, both deliberate for a prototype

* **The floor tested is `WMIN_W`, not the window's own declared minimum**
  (SPEC.md §11.100.2). A window that asked for more than 96 can still be
  narrowed up to 7px below what it asked for. Reading `wm_minw[slot]` here
  is a few more bytes and wants doing before this stops being a knob.
* **It runs after `wm_ask_size`.** `ui_grow` lets the window have a say
  before the record changes, and then `wm_snap_win` may take up to 7px off
  the answer. An app that returns a size meaning something — a whole number
  of rows or columns — gets overruled on the horizontal.

### 9.3 A knob build cannot make a disk without `OS88_DEFINES`

Not this knob's doing, and worth writing down because it is invisible until
it bites: `make TITLESNAP=1` fails the same way on a clean tree.
`$(BUILD)/boothd.bin` shells out to `kernsize.py --json` and
`os88sym.syms()`, both of which re-assemble the kernel and refuse unless the
result is byte-identical to `build/kernel.bin` — which a knob build never is.
The failure reads `symbol 'BLOB_SEG' not defined`, three times, pointing at
`boot/boothd.asm`.

The sanctioned answer is already there and is the environment variable
`OS88_DEFINES` (bare names, comma or space separated):

```
OS88_DEFINES=SIZESNAP make SIZESNAP=1
```

Threading `$(VIDDEF)` into `BOOTHD_DEFS` would fix it in the Makefile, but the
rule's own comment says BLOB_SEG deliberately follows the SHIPPED kernel's
ladder, so that is a decision rather than a typo and is left alone here.

## 10. The zoom defect `SIZESNAP=1` found

Reported from the prototype: **double-clicking a title bar to zoom now snaps
the right side, and must not.**

Confirmed, and the reason is worth getting exactly right because it decides
what the fix is. **Only the LEFT border is suppressed** — SPEC.md §11.95.2 is
explicit ("The other three sides are untouched"), and `wm_geom`'s content
width for such a window is `W_W - 1`, not `W_W`. So a zoomed window's content
does **not** already align:

| | zoom `W_W` | content | `& 7` | snapped to | columns lost |
|---|---|---|---|---|---|
| VGA | 640 | 639 | 7 | 633 | **7** |
| Hercules | 720 | 719 | 7 | 713 | **7** |
| CGA | 640 | 639 | 7 | 633 | **7** |

`wm_snap_w` is doing exactly what it was told and the instruction is wrong.

**And it is wrong against a decision §11.95.2 already took.** That section
exists because the zoom rect used to be `x = 7, w = screen − 7` — §11.94's
snap moving *right* to buy an aligned origin — which left "seven columns of
desktop dither showing down its left, plus a border column: eight columns of
content given up to land the origin on a byte boundary that x = 0 already is."
It made the standard rect `x = 0, w = [vid_pw]` so the alignment was **kept,
not traded for**. A width snap hands seven of those columns straight back, on
the other side.

`wm_zoom` itself already refuses for this reason, one routine over:

```
    call wm_flush_ck            ; ...spanning the SCREEN? Then x = 0 has no
    jc .nosnap                  ; left border and IS the aligned origin, so
                                ; the rect stands as it is (SPEC.md 11.95.2)
```

…and then `wm_ask_size` → `wm_resize_nb` → `wm_snap_win` undoes it.

**The fix is that `wm_snap_w` refuses a flush window**, the same predicate
`wm_zoom` uses. It is a simplification rather than an addition: the prototype
already calls `wm_flush_ck`, to choose between `-2` and `-1`, and the choice
becomes a refusal. Net roughly **−5 bytes** on the +64.

Its scope is "any frame that spans the screen", not "the zoom", and that is
right: a window dragged and resized to span by hand is in the same state and
owes the same answer.

## 11. What is outstanding before work can begin

### 11.1 Settled — no further decision needed

* The live `WF_SAVEU` toggle is the kernel's already (§1). **0 bytes.**
* The 1bpp cache's graphics half is measured at **+27 bytes** (§7.2).
* Snapping the width is wanted and graduates from a knob to the default (§9).
  Measured at **+64**, less the ~5 §10 takes back.
* The Browser adopts the checkerboard rather than the graphics layer learning
  to render 1bpp (§7.4); disabled **icons** are deferred until something needs
  them; `font_ink` gets the ~8-byte hook.
* Do **not** build the split-rect edge (§8.2). The Bit Mask (§8.3) is held in
  reserve for a window that must keep a ragged width.

### 11.2 Must be fixed before the width snap ships

1. **The zoom defect** (§10). Refuse a flush window in `wm_snap_w`.
2. **The floor is `WMIN_W`, not the window's own minimum** (§9.2). A window
   that declared more than 96 through SPEC.md §11.100.2 can still be narrowed
   up to 7px below what it asked for. `wm_minw[slot]` is reachable from
   `wm_snap_w` through `wm_pm_slot`.
3. **The snap runs after `wm_ask_size`** (§9.2). `ui_grow` gives the window a
   say and then takes up to 7px off the answer. Either ask before snapping, or
   snap the candidate before offering it — the second keeps one negotiation.

### 11.3 Ordering, because three things depend on each other

The 1bpp declaration has to exist before two other pieces have anything to
hang off, which fixes the sequence:

1. **Width snap to default** (§9 + §11.2). Independent of everything else, and
   it removes the ragged edge that §7.3 would otherwise have to gate on.
2. **The 1bpp declaration** — `wm_saveu`'s `AL` bit 1, no new API slot (§7.2).
3. **The 1bpp cache** — `wm_su_flay` sets `WM_SU_PW`, `gfx_save`/`gfx_restore`
   take the arm (§7.1–7.2).
4. **`font_ink`'s hook**, gated on that declaration (§7.4). It cannot be built
   before step 2 without inventing a second flag step 2 would supersede.
5. **The Browser**: declare 1bpp, stipple its own greying, toggle `WF_SAVEU`
   in `br_go` and at fetch completion (§2).
6. **The other apps** — the poll-loop rework of §3.1, one at a time.

### 11.4 The per-app work, which is the bulk of it

Each is a package, so **0 kernel bytes**, and each needs the same two things:
the decision moved above `OSAPI_GFX_LOCK`/`OSAPI_WM_CLIP_SET` (§3.1), and the
`WF_SAVEU` toggle on its live/not-live edges (§2).

| app | enters live at | leaves live at | notes |
|---|---|---|---|
| Browser | `br_go` (UI, lock held) | fetch completes, worker | wants the 1bpp cache too; ~23 KB on VGA |
| Cyclone | unpause / new game | game over | `cy_render` runs every frame paused or not |
| Missile Command | unpause / new game | game over | colour, and ~100 KB on VGA — **needs a band or nothing** (§7.5) |
| Arkanoid | `ark_cmd_pause` | game over | unmeasured |
| Timer (kernel) | Start | Stop | `.cold`; §7's question 3 probably disqualifies it anyway (§4) |

### 11.5 What is unmeasured, and what has no test

* **Nothing has run on a guest.** Every figure in §7 and §8 is arithmetic off
  the geometry and the 8088's fetch floor. The one measured number is the
  +27/+64 of `.text`, and `kernsize` is not an emulator.
* **The games' claim sizes are unknown** — `mc_tpl`/`ark_tpl` are `dw 0,0,0,0`
  and sized at run time, so whether any of them clears `wm_su_kb`'s 64,512 is
  a guess. Missile is estimated past it; the other two are not estimated at all.
* **No test row exists for any of this.** At minimum: that a 1bpp window's
  banked claim really is a quarter the size, that a restore of one leaves no
  flattened column beside the window, and that a zoomed window's width is
  untouched by the snap. `tests/dispcorner.py`'s shape — do the thing, force a
  full repaint, diff — is the model.
* **`OSAPI_WM_ONWAKE` staging is asserted, not exercised** (§2). `apps/ftpd`
  is the worked example it is modelled on.

### 11.6 The budget, stated as SUM and ACCRUED

Baseline `.text` **55,301**, image ACCRUED **304/512**. The whole feature is
about **+160 to +200** bytes: 64 for the snap (less ~5 from §10), 27 measured
for the 1bpp graphics half, 60–100 guessed for its policy half, ~8 for
`font_ink`, plus §11.2's two corrections.

Neither guard is threatened. `KERN_SIZE` is 117,760 of `KERN_BUDGET` 122,368 —
**4,608 spare** — and `.text`+`.bss` is 61,232 of `KERN_CODE_MAX` 65,536,
**4,304 left**. The number to watch is the SUM, not where the next 512 falls.

### 11.7 SPEC.md owes four amendments

None of them is optional — SPEC.md is the contract and CLAUDE.md says it is
updated **before** the change:

1. **§11.96.1**: the promise is a MODE, not a classification. Its "Marked
   today" list becomes a list of windows that are *always* in it.
2. **§20.6 rule 7**: `wm_saveu` joins the worker surface under
   `osapi_set_color`'s condition — inside a gfx lock hold only.
3. **§11.94**: the snap covers the SIZE as well as the origin, and refuses a
   window that spans the screen (§11.95.2's rect stands).
4. **A new subsection under §11.96** for the 1bpp cache: the declaration, what
   it promises, `WM_SU_PW`, and the alignment it depends on.

## 12. Step 1 is done

`SIZESNAP=1` is gone; the size snap is the **default**, and `NOSIZESNAP=1` is
the A/B. SPEC.md **§11.94.5** is the contract and was written before the code.

**Measured: `.text` 55,301 → 55,379, +78 bytes.** The prototype was +64; the
three §11.2 fixes are the other 14 net — the flush refusal is cheaper than
the `-2`/`-1` choice it replaced, and `wm_min_axd` plus the pre-ask snap cost
more than it gave back. ACCRUED image **382/512**. Neither guard moved:
`KERN_SIZE` 117,760 of 122,368, `.text`+`.bss` 61,232 of 65,536.

All three §11.2 fixes are in:

1. **The zoom is untouched.** `wm_snap_w` refuses a frame that spans the
   screen, on `wm_zoom`'s own `wm_flush_ck`.
2. **The floor is `wm_min_axd`'s**, so a window that declared more than
   `WMIN_W` through SPEC.md §11.100.2 cannot be narrowed below its ask.
3. **The candidate is snapped before `wm_ask_size`**, between `ui_grow`'s two
   clamps and the ask, because either clamp can move an already-snapped
   outline off the grid. `wm_snap_win` below stays the backstop.

### 12.1 `tests/sizesnap.py`, and it is a gate with two controls

Registered soak, `needs=("marty",)`. It reads the window records rather than
pixels, because a 7px difference at the right edge of a full-screen window is
not something an eye finds in a dump — which is exactly how the defect got
shipped in the prototype.

Three assertions: **ALIGNED** (every visible window's content width and origin
are multiples of 8), **ZOOMED** (a maximized window is `x = 0, w = [vid_pw]`),
**FLOORED** (nothing narrowed below its declared minimum).

Both controls were run, and neither is theoretical:

| build | ALIGNED | ZOOMED |
|---|---|---|
| as shipped | Disk content 312 @ x 104 — **ok** | 0, 640 — **ok** |
| `NOSIZESNAP=1` | content **318** — fails | 0, 640 — passes |
| flush refusal removed | content 312 — ok | **0, 634** — fails |

The second row is why ZOOMED alone would not do, and the third is why ALIGNED
alone would not: each control fails exactly one assertion, so neither
assertion is carrying the other.

`make test-full` passes 19/1 skipped in 92s.

### 12.2 What step 1 changed for everyone

Every window that has not set `WF_NOSNAP` now comes up with a content width
that is a multiple of 8. On the CGA machine the gate boots, the Disk window
goes from **320 wide to 314** — two content columns given up to align the
right edge, on top of the one already given to align the left.

> **Both halves of that sentence are wrong, and §20.1 is the correction.** It
> is SIX content columns, not two — and taking them off at all was the defect:
> eleven of the thirteen templates in this tree size their layout from the
> width they asked for, so a shrunk window draws that layout over its own
> right border. `wm_snap_w` rounds UP now (SPEC.md §11.94.5.1).

The next step is §11.3's step 2: the 1bpp declaration as `wm_saveu`'s `AL`
bit 1.

## 13. Step 2 is done — the two-colour declaration

`WF_1BPP`, bit 13, carried on `wm_saveu`'s `AL` bit 1. SPEC.md **§11.96.17**
is the contract and was written before the code. **Measured: `.text`
55,379 → 55,392, +13 bytes.** ACCRUED image 395/512; neither guard moved.

`apps/os88api.inc` publishes `OSAPI_SAVEU_ON` (1) and `OSAPI_SAVEU_1BPP` (2)
rather than the flag itself, which is how `WF_SAVEU` is already handled — a
package sets it through the call, never by writing `W_FLAGS`.

### 13.1 The high byte's bits are named once now

The bit had to go in the high byte (bits 0..7 are all taken), which is
§7.2.1's cursor shape. `WF_NOANIM`'s banner already carried the rule in prose
— *"the two places that touch the byte must mask this bit off with
`WF_STALE`"* — and prose is what a third flag gets half-added to: `wm_cursor`
KEEPS the kernel's bits when it writes a shape and `mou_apply` DROPS them
when it reads one, so a bit added to one mask and not the other either eats
the shape or is read as one. That is not hypothetical; `WF_STALE`'s banner
records the day it sat at bit 8, where it *was* `CUR_CROSSSH`.

Both masks derive from `WF_HIBITS` now, with an assembly-time guard that it
does not overlap the shape field. **0 bytes** — both were immediates.

### 13.2 Note Pad declares it, and is the only window that can

Its content is `CBLACK` on `CWHITE`, and the only other thing inside it is
`os88ui_sbar`'s track — `gfx_fill_gray`, which the kernel's own banner
describes as *"50% dither: (x+y) even = white, odd = black"* landing "in all
four planes, giving color 15/0 per bit directly". A dither of 15 and 0 is two
colours.

Nothing else in the tree qualifies. Minesweeper draws in eleven colours,
Piano in nine, Solitaire in eight; the Browser fails on `os88ui`'s disabled
pen alone. It is also the right first subject on its merits: §11.96's
headline — 1,026 ms to raise, 578 ms of it lettering — was measured on Note
Pad.

(One dead store went with it: `wm_sizable` preserves AL, so the second
`mov al, 1` in `np_entry` was writing a register that already held 1.)

### 13.3 `tests/win1bpp.py`, and what it does not prove

Registered soak, two launches — the subjects are in different folders and a
window opened over the Disk window swallows the next click, which is how the
first draft silently opened nothing.

* **DECLARED** — Note Pad carries `WF_SAVEU` and `WF_1BPP` (flags `2027`),
  and no other window carries the second.
* **SHAPE** — Missile Command still reads shape 1, the crosshair (flags
  `0103`), so defining a third kernel bit in that byte cost no shape its
  value. Nothing anywhere reads back a shape at or past `CUR_NSHAPE`.

**The control that fires** is Note Pad passing `OSAPI_SAVEU_ON` alone:
DECLARED fails with "Note Pad does not carry WF_1BPP".

**The control that cannot fire, said out loud.** The sharp form of the
coupling — one mask gaining `WF_1BPP` and not the other — needs a window with
a shape AND a depth claim at once, and no shipped window has both. This gate
would not catch it. `WF_HIBITS` plus the `%error` is what makes that
unwritable, and the gate covers what an expression cannot: that the value in
the record is the one the app asked for.

`make test-full`: 19 passed, 1 skipped, 92s.

### 13.4 One thing step 3 inherits, and it is binding

**`wm_saveu` does not drop the cache when the depth claim changes, and must
not learn to.** A buffer taken one plane deep means something else the moment
the claim goes. §11.96.3 already settled where that belongs — *"the rect
travels with the pixels it describes and cannot get out of step with them"* —
so **the depth goes in the claim's header beside `WSU_X1..Y2`**, and
`wm_su_ck` invalidates by disagreeing. There is nothing to invalidate yet,
which is exactly why it has to be written down now rather than remembered.

## 14. Step 3 is done — the cache holds one plane

**Measured: `.text` 55,392 → 55,550, +158 bytes.** `KERN_SIZE` 118,272 of
`KERN_BUDGET` 122,368 (4,096 spare); `.text`+`.bss` 61,481 of 65,536 (4,055
left). ACCRUED image 41/512 — **the image rung crossed on this change**, so
it is 512 bytes of every machine's RAM, taken deliberately rather than
noticed later.

Six pieces:

1. **`WSU_PW`** in the claim's header at offset 12, `WSU_IMG` 12 → 14. The
   depth travels with the pixels, per §11.96.3's rule for the rect, and
   `wm_su_ck` compares it. It subsumes the dual-display hazard as a bonus: a
   window banked on a Hercules and checked on a VGA now disagrees here.
2. **`wm_su_1bpp`** — the predicate. `WF_1BPP`, **and** whole-byte edges,
   **and** no band. The last is not obvious: §11.96.11.1's LEFT and RIGHT
   fragments are sub-spans of the content and can end mid-byte even when the
   content does not, so a banded window could hand `wm_su_edge` a ragged
   fragment over a one-plane buffer. Nothing wants both yet (a band needs
   `WF_OWNBG`) and refusing still leaves the ordinary cache.
3. **`wm_su_flay`** sets `WM_SU_PW` to 1, and does it *after* the band
   extents are clamped rather than beside `vid_disp_planes`, because the
   answer needs `[wm_su_kany]`.
4. **`vga_pn` / `vga_pm`** in vga12.inc, 4 and 1 at rest — `gfx_sub_*`'s
   discipline, for its reason. `gfx_save`/`gfx_restore` spend the arm, so it
   is good for one call and cannot leak onto the pointer, which reaches
   `vga_save_vram`/`vga_restore_vram` directly out of IRQ4 below them. The
   restore's loop needed no new terminator: seeded 0Fh it runs once and
   `add al, al` gives 1Eh.
5. **`SU1ARM`** at the four `gfx_save`/`gfx_restore` sites — a macro, so
   kern_small (which has no `WF_1BPP` path) pays nothing. Two of the four are
   `wm_su_edge`'s and are unreachable for a 1bpp cache by construction; they
   are armed anyway because the failure there is a **heap overrun**, which is
   the bug `wm_su_pw` was introduced to close.
6. **`wm_su_edge` returns immediately on a whole-byte rect.** Both masks are
   FFh, so `wm_su_merge` writes the buffer back over itself after two
   `gfx_save`s that read the screen for nothing. §11.96.8 prices that at
   18.22 ms of a 47.86 ms restore *(SPEC.md's figure, not re-measured here)*.
   §11.94.5 made whole bytes the ordinary case, so this is where step 1 pays
   off — and it is also what makes one plane safe, the merge being the only
   place a colour neighbour could reach a 1bpp buffer.

### 14.1 `tests/su1bpp.py` — VGA, three assertions, and a control that bites

On `os8088_xt_vga`, because on a 1bpp adapter every cache is already one
plane and there is nothing to show.

| | measured |
|---|---|
| DEPTH | Note Pad's claim carries `WSU_PW=1`, rect x 56..311 (`x1&7=0`, `x2&7=7`); the Disk window's carries 4 |
| QUARTER | content 256×161, 32 bytes a row → **5,488 bytes at one plane against 21,910 at four** |
| PIXELS | cover, raise, then force a full repaint and diff the rendered framebuffer: **0 subpixels differ** |

**The control**: seed the restore's Map Mask with 1 instead of 0Fh, so only
plane 0 is written. PIXELS goes to **99,267 subpixels**, and the wrong ones
read `0000aa` — colour 1, plane 0 alone — where they should be `ffffff`.
Exactly the predicted failure, so the comparison is not vacuous.

Two things the run taught the test, both about the SUBJECT rather than the
kernel: the pointer is drawn into the framebuffer, so both captures park it
(267 subpixels otherwise), and §37's clock redraws on its own at the bar's
right end, so the diff starts below `MBAR_H` (18 subpixels otherwise, at
x 624..629, y 10..12).

### 14.2 Step 1 had broken three soak tests, and this found them

Not step 3's doing, and worth recording as its own finding: the size snap
changes the width a window comes up at, and **three tests identify a window
by its size**. All three failed and none is in `test-full`, so nothing said
so at the time.

| test | expected | got |
|---|---|---|
| `dispcorner` | `hl_tpl` 240×90 | 234×90 — "that is not HELLO.O88" |
| `dispclose` | `os88ui_ask` 288×92 | 282×92 |
| `fdlggrey` | the dialog at width 300 | 298 — `dlg()` returns None |

Fixed by mirroring the arithmetic **once**, as `os88geom.snapw()`, beside the
constants that module already mirrors — rather than retyping 234, 282 and 298,
which is the same brittleness one snap later. `dispcorner` A and C and
`dispclose` all pass again; `fdlggrey` is fixed but **unrun**, it needing
`build/muptest.img`.

`make test-full`: 19 passed, 1 skipped. `dispcorner --only c` passes with its
1,416 dither-phase pixels and 0 real, so the edge skip did not disturb the
drag cache.

### 14.3 What is still not known

* **Nothing is measured in TIME.** The +158 bytes and the 5,488-vs-21,910 are
  real; the 18.22 ms the edge skip removes is SPEC.md's figure for a
  different window on a different adapter, and this change has not been put
  on a stopwatch. A `gfxbench`-style row for a raise would be the honest way.
* **Only Note Pad exercises it.** One window, one adapter, one geometry.
* **The soak tier has not been run whole.** Step 1's casualties were found by
  running three tests that happened to be adjacent; there may be others that
  identify a window by its width.

## 15. Step 4 — the hook is in, and it is HALF of what I said it was

**Measured: `.text` 55,550 → 55,591, +41 bytes**, not the ~8 I quoted. The
quote was for `font_ink`'s test alone and left out the setting side, which is
most of it.

And the scope was wrong in a way the quote hid. §11.96.17.1 now says what is
true; this is how it was found.

### 15.1 What went in

* **`[gfx_mono1]`**, a per-lock-hold word of `[gfx_dis]`'s kind, cleared by
  `gfx_unlock`.
* **`wm_pkgcall`** sets it from the window for every callback there is —
  `W_PAINT`, `W_ONCLICK`, `W_ONKEY`, `W_ONWAKE`, `W_ONSIZE` — that being the
  one dispatcher all of them go through. **Stacked**, for the reason
  `SNAPAUDIT`'s `[snap_cur]` is stacked one line away: a repaint pass calls
  several windows' callbacks inside one lock hold. A word rather than a byte,
  because `push word`/`pop word` would otherwise read and write the
  neighbouring byte.
* **`wm_clip_set`** sets it from `BX`, a background painter being a worker
  that reaches no dispatcher.
* **`font_ink`** takes its mono path on `[vid_mono]` **or** `[gfx_mono1]`.

### 15.2 What it does not reach, and how that surfaced

`font_char` sends a VGA to its **`.vram`** path — Set/Reset straight from
`[gfx_color]` — which calls `font_ink` not at all and reads `[font_dith]` not
at all. `font_char_bb`, the only caller, is the *software* renderer, i.e. the
1bpp one. So the hook is inert for `font_str` on the adapter it was written
for.

That is not how I found it. `tests/monoink.py` poked `WF_1BPP` onto the
Browser, forced a repaint, and reported **379 colour-8 pixels before and 379
after**. Forcing the mono path unconditionally changed nothing either, which
is what ruled out the flag and pointed at the renderer.

`font_run` is the exception and is why the hook is worth having: §6.1's opaque
renderer calls `font_ink` **explicitly on every adapter** and computes its own
checkerboard without asking `[vid_mono]`. So a declared window's greyed text
is right through `font_run` and wrong through `font_str` — which is §6.6's
rule arriving at the same place from the other end.

### 15.3 Reducing the pen: tried, measured, withdrawn

The obvious completion is `gfx_pen_dis` picking `CBLACK` for a declared
window, which takes the colour out of the frame and the caption together. It
works — **379 odd pixels → 0**. And it is wrong:

```
    y=67   before |gg..gg..gggg....gggg....gg..gg..|   after |##..##..####....####....##..##..|
```

The Browser's greyed `Back` is **solid** colour 8 and becomes **solid**
black — 215 ink pixels either way, no stipple in either. That is
pixel-identical to a live caption, which is §47 rule 1's own failure with a
new way in: *"a menu item that greyed out simply stopped saying so"*.

So the reduction waits on `font_char`'s `.vram` path carrying a checkerboard.
That is a change to the hottest text path in the system — PERFORMANCE.md says
measure it, and I have not. A per-glyph pre-mask of the eight glyph bytes
(rather than a per-row AND) looks like the shape that costs live text one
`cmp`, but that is a guess.

### 15.4 `tests/monoink.py`, registered soak

It asserts the one thing that is true — **INERT**: poking the bit changes not
one pixel of a `font_str` path, so the hook does not perturb what it does not
serve — and it **measures the gap** at 379 pixels, naming both causes:
`os88ui_btn`'s frame is `UI_FRAME` under the disabled pen, and its caption is
`UI_STR`. It is the reproducer step 5 has to turn green.

### 15.5 What step 5 inherits

Bigger than §11.4 said. Before the Browser can declare two colours:

1. **`os88ui_btn`'s caption must use `UI_RUN`, not `UI_STR`.** The macro
   already exists. This is §6.6's transparent-text registry too, so it is a
   change that has to be argued there as well as here.
2. **Its frame must not be `CDGRAY` for a declared window.** Either the app
   picks the pen, or `gfx_pen_dis` reduces — and the reduction needs §15.3
   first.
3. Only then the `WF_SAVEU` toggle and the poll-loop rework of §3.1.

`make test-full`: 19 passed, 1 skipped. `su1bpp`, `win1bpp` and `sizesnap`
all still pass.

## 16. Step 5 — the Browser's promise follows the FETCH

**0 kernel bytes.** SPEC.md **§71.11** is the record, plus the two amendments
§11.7 owed and this pays: **§11.96.1** now says the promise is a MODE and not
a classification (and that a window which polls must decide *before* it arms),
and **§20.6 rule 7** puts `wm_saveu` on the worker's surface under
`osapi_set_color`'s condition — inside a gfx lock hold, because the clear
calls `mem_free` on the cache.

This is the thing the whole investigation was for, and it is small: one
routine and three call sites.

* `br_promise` reads `[br_nstate]` — `BN_OPEN`..`BN_BODY` is a fetch,
  `BN_IDLE`/`BN_DONE`/`BN_ERR` is a page standing still — and hands the answer
  to `OSAPI_WM_SAVEU`. Called from the `BN_DONE` block and from `br_nshow`,
  both already inside a lock hold.
* `br_go` withdraws **unconditionally, at the `BN_OPEN` transition**.
* The entry proc makes the starting promise, like every other app.

**Its poll loop needed no change** — the finding of §3.1 turned out not to
apply here. `br_nstep` falls straight through on the settled states, taking no
lock and arming no region, so an idle Browser does not destroy its own cache
the way a worker that arms `wm_clip_set` every tick does.

**What it gets, and where.** ~16KB banked on Hercules and ~10KB on CGA, so the
promise is worth a cache on both of the adapters this OS is for. On VGA the
content is ~92KB against the 64,512 ceiling and the claim is refused — a safe
no-op, and what §7's one-plane cache would fix if the Browser could make that
claim. It cannot yet: §15.5.

### 16.1 The gate found two bugs in the code it was written for

`tests/brpromise.py` asserts a **sequence** read out of the window record:
IDLE promising → LIVE withdrawn → BACK promising. MartyPC has no NIC, so the
fetch settles as `BN_ERR`, which exercises the same two edges a `BN_DONE`
would.

1. **The withdrawal was state-driven and at the wrong place.** A `br_promise`
   at `br_go`'s exit re-reads `[br_nstate]` — and the worker needs the lock
   only to *draw*, not to move its own state machine, so it had already
   written `BN_ERR` while `br_go` held the lock. The exit read a settled state
   and *set* the promise; the withdrawal never happened.
2. **`BX` was not the window.** `wm_saveu` bounds-checks its argument
   (§11.96.11.4), so the call was silently refused and did nothing. That guard
   is the only reason a wrong `BX` was a no-op rather than a bit set somewhere
   in the kernel's own data.

Both presented as **one flaky run in seven**, which is the worst way to find
anything. Three things about the harness had to be fixed before the code's own
faults were visible, and each is worth writing down:

* `os88geom.windows()` reads every record **and every title out of its
  package's segment**, so a loop built on it samples every few tens of
  milliseconds however short its sleep — against a window about fourteen wide.
  The gate reads one two-byte `W_FLAGS` now.
* **Pausing between samples made it worse**, not better: a paused guest does
  not advance, so 120 samples went by before the UI task had seen the
  keystroke.
* The URL has to start with `http://` or `br_go` answers *"Not an http URL"*
  and no fetch ever starts — on which every assertion passes and the run tests
  nothing. A **picture** is what settled that: the status line read *"No link
  driver"*, which only a started fetch can say.

Control: point `BX` at 0 and LIVE fails. `make test-full` 19 passed, 1
skipped; `su1bpp`, `win1bpp`, `sizesnap` and `monoink` all still pass.

### 16.2 Where the plan stands

| step | state |
|---|---|
| 1 — width snap | **done**, +78 bytes, `tests/sizesnap.py` |
| 2 — the declaration | **done**, +13 bytes, `tests/win1bpp.py` |
| 3 — the one-plane cache | **done**, +158 bytes, `tests/su1bpp.py` |
| 4 — `font_ink`'s hook | **done for `font_run`**, +41 bytes, `tests/monoink.py`; §15 is what it does not reach |
| 5 — the Browser's live promise | **done**, 0 bytes, `tests/brpromise.py` |

Kernel total **+290 bytes** of `.text` (55,301 → 55,591).

What is left is not in this plan's original shape. §15.5's three items are the
Browser's route to a VGA cache, and §11.4's other apps — Cyclone, Missile
Command, Arkanoid — still need §3.1's decide-before-you-arm rework, which is
where the measured 6.2 ms a frame is.

## 17. The Browser on a VGA — §15.5 closed

**Measured: `.text` 55,591 → 55,641, +50 bytes**; `.cold` +43. Kernel total for
the whole plan is now **+340 bytes** of `.text` (55,301 → 55,641).

The Browser's default VGA content is 94,010 bytes at four planes against
`wm_su_kb`'s 64,512 — the refusal §7 was written to lift. It now takes a
**23,513-byte, one-plane** cache. Three things had to happen and only the
first was in the plan.

### 17.1 `os88ui_btn` draws its caption with a RUN where the interior is filled

`font_char` sends a VGA to its `.vram` path and never asks about `[gfx_dis]`;
`font_run` reads it, substitutes `[gfx_disink]` and lays §6.1.12's mask over
the result. So the shared button's caption had to become a run — which §6.6
asks for anyway, and which the registry's own reason left open ("they go when
`font_str` does", no drawing argument).

**Not unconditionally.** A button with neither `OS88UI_DOWN` nor
`OS88UI_FILL` never fills its interior, so the ground under the caption's
cells belongs to whoever drew it and an opaque run would repaint it. That one
keeps the transparent call, and `tests/textsites.txt`'s reason now says so
instead of naming the whole routine.

The greyed case needs nothing from the caller: `font_run` overrides the ink it
is handed with `[gfx_disink]` when `[gfx_dis]` is set. So the caller passes
only the ordinary pair — black on white, white on black when pressed.

### 17.2 `font_run`'s planar prologue was throwing the checkerboard away

§6.1.10's single-store prologue sets `[font_rn_xm]` to FFh — *"the compose is
a pass-through either way"* — which discards §6.1.12's mask. Right while
`CDGRAY` is doing the talking; wrong the moment the pen is reduced.

For a declared window with `[gfx_dis]` set it now writes the phase mask and
`[font_rn_dt]` = FFh instead. `xm` is a mask over the **glyph** there rather
than an ink-XOR-background, so it halves the ink for either `bm`.

**The test is `[gfx_dis]`, not `[font_rn_dt]`** — and getting that wrong cost a
build: `[font_rn_dt]` is 0 at `.planar` by construction, because §6.1.12's
mask is computed in the *mono* prologue and this branch leaves before it. The
symptom was a caption that stayed stubbornly solid with every flag set right.

### 17.3 …so the pen reduction, withdrawn in step 4, is back

`gfx_pen_dis` picks `CBLACK` for a declared window, taking colour 8 out of the
frame and the caption together. §15.3 withdrew exactly this because on its own
it makes a greyed caption pixel-identical to a live one. What changed is that
the checkerboard now reaches a VGA, so the **shape** carries §47 rule 1's
signal and the colour no longer has to. Gated on `[gfx_mono1]`, so an ordinary
VGA window's greying is untouched.

### 17.4 `tests/monoink.py`, re-aimed from a gap into a gate

It used to measure a 379-pixel gap. It now asserts the three things that
closed it, on `os8088_xt_vga`:

| | measured |
|---|---|
| TWO | content **0 pixels** that are neither colour 0 nor colour 15 |
| LEGIBLE | `Back` and `Fwd` lit on **one** parity of (x + y); `Reload` on **both** |
| FITS | claim `WSU_PW=1`, **23,513** bytes; four planes would be **94,010**, past 64,512 |

LEGIBLE is the assertion worth keeping. Rounding the pen is easy and losing
§47's signal with it is easier, so the gate checks the ink's **shape** rather
than its colour: a 0AAh/055h mask means every lit pixel of a greyed caption
falls on one parity, which a solid glyph can never do, and the live `Reload`
beside it is the control. Two harness faults had to go first — the caption
groups were being found by their own ink, which finds eleven things because
each button's vertical frame edge is a solid one-column group, so it finds the
**frames** now and looks inside them.

### 17.5 Regression sweep

The `os88ui_btn` change touches every button in the system, so: `dispclose`
(os88ui_ask's alert) 0 failures, `calcflick` PASS, `dispcorner --only a` PASS,
`make test-full` 19 passed 1 skipped, and `su1bpp` / `win1bpp` / `sizesnap` /
`brpromise` all still pass.

### 17.6 Where the plan stands now

| step | state | `.text` |
|---|---|---|
| 1 — width snap | done | +78 |
| 2 — the declaration | done | +13 |
| 3 — the one-plane cache | done | +158 |
| 4 — the renderer hook | done | +41 |
| 5 — the Browser's live promise | done | 0 |
| §15.5 — the Browser on VGA | **done** | +50 |

**+340 bytes** for: a live per-window save-under promise, a quarter-size cache
for a two-colour window, and a Browser that holds one on every adapter.

What is left is §11.4's other apps — Cyclone, Missile Command, Arkanoid —
which still need §3.1's decide-before-you-arm rework. Missile Command is
colour and ~100KB on VGA, so a band (§8) is its only route there; all three
get a cache on the 1bpp adapters for the price of the toggle alone.

## 18. Who else is refusing one, and what each would take

A survey rather than a guess: every shipped package, asked whether it
promises, whether it has a WORKER at all, and whether its content is two
colours. The claim sizes are computed from each template against
`wm_su_kb`'s 64,512 ceiling.

**The colour column is only as good as the grep behind it.** It counts NAMED
constants (`CBLUE` and its kin), so an application that COMPUTES a colour
index reads as two-colour and is not. Fractal is exactly that — `FR_CAP` is
48, the palette index falls out of the iteration count, and each run word
carries its colour in bits 15..12 — so it is a sixteen-colour window that the
survey first called two. Only TeXPad's has been checked beyond the grep
(`CBLACK` and `CWHITE`, 43 uses, nothing else); treat the rest as a shortlist
to verify, not an answer.

**Six packages have no worker at all**, and §11.96.1's first question — *does
its content change while it is merely covered?* — passes for them by
construction: "An app with no worker at all cannot fail this, because the
window ABI has no periodic hook." Those are the cheap wins.

### 18.1 The ranking

| | window | worker | colours | VGA claim | what it needs |
|---|---|---|---|---|---|
| **1. TeXPad** | **628×400** | **none** | 2 | **123,458 at 4 — REFUSED; 30,875 at 1** | two flags |
| 2. Fractal | 322×199 | yes, Browser-shaped | **16** | 30,254 fits | toggle + §3.1 |
| 3. Telnet | 528×190 | yes, Browser-shaped | 2 | 46,526 fits | toggle + §3.1 |
| 4. FTP server | 400×176 | yes, Browser-shaped | 2 | 32,670 fits | toggle + §3.1 |
| 5. Control Panel | 320×151 | none (kernel) | — | 22,190 fits | one flag, `.cold` bytes |
| — Task Manager | — | yes, live BY DESIGN | 2 | — | nothing; it is the load meter |
| — the games | — | yes, live | colour | — | toggle only; no VGA cache |
| — Hello | 234×90 | none | 2 | tiny | nothing — §11.96.1 q3 disqualifies it |

### 18.2 TeXPad is worth more than everything below it combined

* **The largest window in the tree**, and the densest. Source text on the left
  and a typeset preview on the right — its repaint is more lettering than any
  other window in the system, which is §11.96's entire argument.
* **No worker.** Its only asynchronous thing is one `OSAPI_WM_ONWAKE` at
  launch to typeset the document it was opened with — a UI-task callback, not
  a background painter. So questions 1 and 2 pass with nothing to promise
  about and nothing to get wrong.
* **`CBLACK` and `CWHITE` only** — 24 and 19 uses, nothing else. Its own
  banner says "black-and-white TeX pad".
* **It is the only window left that four planes cannot fund**: 123,458 bytes
  against the 64,512 ceiling, and 30,875 at one plane. §7's one-plane cache
  exists for this window more than it did for the Browser.

Cost: `OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP` in its entry proc. No state
machine, no `br_promise`, no poll-loop rework — there is no loop.

### 18.3 The three Browser-shaped ones

Fractal, Telnet and the FTP server are all §71.11 again: a worker that is live
only while something is in flight. Each wants a `*_promise` reading its own
state, the withdrawal **at the transition** and unconditional (§16.1's first
bug), and §3.1's decide-before-you-arm check on its poll loop — the Browser
happened not to need that one and these may.

**Fractal is the interesting one of the three.** Its window is small and its
claim already fits, so the 1bpp work buys it only memory — but its `W_PAINT`
either replays a cached render or calls `fr_kick`, which **restarts the render
from row 0**. That is the most expensive repaint in the tree by a long way,
and it is the one place where a save-under saves seconds rather than
milliseconds.

### 18.4 Not worth it, and why

* **Task Manager** — its worker redraws twice a second because it *is* the
  load meter (SPEC.md §8.1.1 stage 4). Genuinely live; there is no mode to
  toggle.
* **modplug, tracker** — live meters, same shape.
* **Arkanoid, Cyclone, Missile Command, Tamegram** — live while playing, and
  all four draw in real colours, so even paused they get no VGA cache.
  Missile Command is ~100KB at four planes, so a band (§8) would be its only
  route there.
* **Hello** — §11.96.1's third question already names it: a fixed string is
  not worth a claim.
* **c64, cword, RunCPM** are not on the standard apps disk (their own
  floppies). `cword` is worker-free and two-colour and would be a good target
  on the disk it ships on.

## 19. TeXPad — the free one, and it was free

**0 kernel bytes.** Two flags in its entry proc:
`OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP`. No state machine, no `*_promise`, no
poll-loop rework — there is no loop.

Measured on both adapters:

| | VGA | CGA |
|---|---|---|
| window | 626×400 | 626×155 |
| content, colours other than 0 and 15 | **0** | — |
| claim | `WSU_PW=1`, **30,494** bytes | `WSU_PW=1`, 10,894 |
| the same at four planes | **121,934** — past 64,512 by nearly double | 43,534 |
| cached raise vs a forced full repaint | **0 subpixels differ** | 0 |

So TeXPad is the window §7's one-plane cache exists for more than the Browser
was: at four planes it is not a tight fit, it is a refusal by 89%.

### 19.1 The test spent three runs discovering the SUBJECT, not the code

Every early run reported NO CACHE, on both adapters, with the flags plainly
set in the record. Nothing was wrong: **TeXPad covers the whole desktop,
including the drive icons**, so the double-click meant for one landed on
TeXPad and the window that was supposed to cover it never opened. The
kernel was refusing to bank an unobscured front window, which is correct.

Two things ruled it out, and the order was luck rather than method: the same
NO CACHE on a **CGA**, where the depth is 1 by adapter and the claim is 10,894
bytes, took the 1bpp path out of it; and printing **every** window's flags and
cache word showed the Disk window holding one (`3300`) while TeXPad held none,
which said the machinery worked and the subject had never been covered.

The cover is the **Control Panel off the chip menu** now. The menu bar is the
one thing TeXPad never covers, and `tests/tpsaveu.py` says so in its banner so
the next person does not re-derive it.

### 19.2 …and the survey's colour column needed correcting

It counts NAMED constants, so an application that COMPUTES a colour index
reads as two-colour and is not. **Fractal** is exactly that — `FR_CAP` 48, the
palette index out of the iteration count, the colour in bits 15..12 of each
run word — a sixteen-colour window the survey first called two. Only TeXPad's
column has been checked beyond the grep. §18's table now says so.

Fractal still wants the promise; it just wants it without the depth claim,
and it does not need one: 30,254 at four planes already fits. Its value was
never the memory — it is that `fr_redraw` either replays a cached render or
calls `fr_kick`, which restarts it from row 0.

## 20. The Control Panel — the one that was almost free, and the bug it found

**Four bytes of `.text`**, all of them `cw_wm_saveu`, the thunk `ctrl.inc`
calls through — beside `cw_wm_snap` and for the same reason. `cp_promise`
itself is 23 bytes of `CTRL.DRV`, which is module space and outside
`KERN_BUDGET`. SPEC.md **§31.12**.

The panel had no single answer to §11.96.1 and that is why it went so long
without one. On five of its six pages nothing moves without a click, and a
click cannot reach a window that is not frontmost; on Date & Time, `ui_task`
calls `cp_tick` once a second and `cp_tick_x` **skips the draw when
`wm_obscured` says so** — the clock advances and the glass does not, which is
the disqualifier written out. So the promise is `[cp_sel] != CP_ITIME`,
answered at `cp_page`, the one routine every path that can change the answer
goes through.

No depth claim: the panel draws in `CDGRAY` (§47), so a §11.96.17 two-colour
claim would be a lie, and it does not need one — 322×151 is 320×132 of
content, **22,190 bytes at four planes** against a 64,512 ceiling.

`tests/cppromise.py`, registered soak, five legs. **`CLOCK` is the half that
bites**: with `cp_promise` removed the panel simply never promises, so
`PROMISE`, `BANKED` and `PIXELS` all pass on a kernel that does nothing.

Two harness notes worth keeping:

* **`settle` cannot be used on the Date & Time page.** The seconds field
  redraws once a guest second, so the screen is never still: `settle` waits
  out its whole limit and then blames the boot. Every wait past the page
  switch is an `until` on `[cp_sel]` or on `wm_zord`'s front entry.
* **The panel and the B: drive window overlap almost entirely.** The raise
  has to click the panel's title bar *right* of the drive window's right
  edge, and nothing fails if it does not — the cache stays banked and every
  reading afterwards is of a window that was never raised. The gate asserts
  the raise spent the cache rather than trusting the click.

### 20.1 …and `PIXELS` found that step 1 had been shrinking eleven windows

The gate reported **792 differing subpixels** — 264 pixels, exactly two
columns by the panel's 132 content rows — between a raise off the cache and a
forced full repaint. The cache was right and the repaint was wrong: the
Control Panel was coming up with **no right border down its content rows**,
white content running straight into the desktop dither, while the title bar
and the bottom edge kept theirs.

**§11.94.5's width snap rounded DOWN.** `cp_tpl` asks for a 320-wide frame,
`CP_CW` is 318, and `cp_page` erases its pane with `gfx_fill di .. di +
CP_CW − 1`. Snapped down, the window was 314 — content 312 — so the erase ran
six pixels past the content, the clip region stopped it at the shadow column
and let the rest through, and the border and shadow went white.

**Eleven of the thirteen window templates in this tree are shrunk by the
down-snap**, most of them by six pixels, and every one of them carries a
layout constant derived from the width it asked for:

| template | frame | content | DOWN | what it gets now |
|---|---|---|---|---|
| Word | 600 | 598 | 592 (−6) | 600 |
| Browser | 496 | 494 | 488 (−6) | 496 |
| Control Panel, File Manager | 320 | 318 | 312 (−6) | 320 |
| Artful | 360 | 358 | 352 (−6) | 360 |
| Note Pad | 260 | 258 | 256 (−2) | 264 |
| Task Manager | 232 | 230 | 224 (−6) | 232 |
| Piano | 224 | 222 | 216 (−6) | 224 |
| Recorder | 220 | 218 | 216 (−2) | 224 |
| Hello | 240 | 238 | 232 (−6) | 240 |
| `ui_note` | 288 | 286 | 280 (−6) | 288 |
| file dialog | 300 | 298 | 296 (−2) | 304 |
| Fractal | 322 | 320 | 320 | 320 |
| Minesweeper | 146 | 144 | 144 | 144 |
| TeXPad | 628 | 626 | 624 (−2) | 624 — the DOWN branch |

**`wm_snap_w` rounds UP where it fits now** (SPEC.md §11.94.5.1). `W_X` is
the kernel's to move; `W_W` is a number the package asked for, and taking
columns off it does not make a package draw a narrower layout — it makes it
draw the same layout over its own chrome. Growing cannot do that:
`wm_draw_win` white-fills the whole content before `W_PAINT` runs, so surplus
columns a package never draws into look exactly like the empty content they
are.

**Refusing outright when UP does not fit was tried, and `tests/tpsaveu.py`
failed within one run of it.** TeXPad is 628 wide at x = 6, its snapped x is
7, and 634 is one column past a 640 screen — so an unsnapped TeXPad has a
ragged right edge, `wm_su_1bpp` refuses the one-plane claim over one, and
four planes is 121,934 bytes against a 64,512 ceiling. The largest window in
the tree, and the one §7 exists for, lost its cache entirely: `FITS: TeXPad
was covered and got no cache at all`.

So **UP first, DOWN when it will not fit.** The invariant is the narrower one
the evidence supports: a window is handed less content than it asked for only
when it is within 7px of the glass, and all three windows in this tree that
wide — TeXPad, the Browser and Word — size themselves from `OSAPI_WM_GEOM`
rather than from a constant, because a fixed layout that wide could not fit a
CGA at all.

Cost: **+31 bytes** (`.text` 55,645 → 55,676; branch accrued 55,301 →
55,676, **+375**). `tools/os88geom.py`'s `snapw` mirror models both branches,
which is what keeps `dispcorner`, `dispclose` and `fdlggrey` honest; §14.2 is
the record of those three being broken by this same function the first time.
It also fixed a latent bug of the original: a floor refusal returned
`wm_min_axd`'s answer in AX instead of the candidate width, so a refused snap
handed back the **minimum**.

**§12.2 was wrong and is corrected here**: it said the Disk window went "320
wide to 314 — two content columns given up". It was six, and the consequence
was not counted at all.

## 21. Fractal — the promise without the depth claim

**Zero kernel bytes.** `fr_promise` is 24 bytes of `FRACTAL.O88` and three
call sites; `kernel.bin` does not move. SPEC.md **§40.4**.

While it renders, Fractal is §11.96.1's disqualifier in its purest form:
`fr_emit_body` makes only the *painting* conditional on visibility and caches
the row and steps the state machine either way — that is what makes
uncovering it cheap — so a buried fractal goes on changing its content for
the minute or two a frame takes. At pass 3 the worker takes `.idle`, sleeps
four ticks at a time and touches nothing: no lock, no clip, no pixel.

So the promise is `[fr_pass] >= 3`, and the three sites are every place that
word moves under a lock: `fr_kick` (reset to 0, **withdraw**), `fr_redraw`
(republish a resume point, **withdraw**) and `fr_emit_body` after
`fr_advance` (**grant**, and only the last row can reach it).

**A repaint withdrawing is right, and the case that reads like a
contradiction does not arise.** When the cache is valid `wm_su_try` restores
the pixels and never calls `W_PAINT`, so `fr_redraw` does not run and the
promise stands; `fr_redraw` runs when there was no cache to spend, which is
exactly when there is drawing to do.

**No depth claim** — sixteen colours by construction — and none needed:
322×199 is 320×180 of content, 30,254 bytes at four planes.

**§3.1's trap does not bite, and that was checked rather than assumed.** The
finding that defeats most candidates is an app arming a clip region on every
poll and `wm_clip_set` dropping the cache of a *visible* window before it
discovers the window is covered. `fr_worker` never does: the visibility test
and the `OSAPI_WM_CLIP_SET` live inside `fr_emit_body`, reached only from
`.work`, reached only when `[fr_pass] < 3`. The decision is already above the
arm — which is the shape §3.1 asks every other candidate to be refactored
into.

`tests/frpromise.py`, registered soak, six legs. **`LIVE` and `COVERED` are
the halves that bite**: a build that simply set `OSAPI_SAVEU_ON` in the
package header would pass `ATREST`, `BANKED` and `PIXELS` and be wrong for
the whole of a render.

## 22. Telnet and the FTP server — the promise per DEBT

**Zero kernel bytes.** `te_promise` is 52 bytes of `TELNET.O88` and `fd_promise`
40 of `FTPD.O88`; `kernel.bin` does not move. SPEC.md **§70.7** and **§77.47**.

These two are §18.3's last pair, and neither takes the Browser's per-fetch
answer. **The reason is worth keeping**, because it is the first time the
survey's own shape was wrong: §71.11 promises while nothing is in flight, and
for these two the in-flight state is the one they are in nearly all the time.
A telnet window spends its life connected at a prompt; a file server spends
its life running and quiet. A per-session promise would have been correct,
gated, shipped, and worth nothing.

**Both already kept the finer answer, and had before any of this started.**

| | the debt word | set by | periodic? |
|---|---|---|---|
| Telnet | `te_owed` — `[te_dirty]`, `[te_scrl]`, `[te_dr0]`..`[te_dr1]` | arriving text, a scroll, a state change | no |
| FTP server | `[fd_dirty]` — `FDD_BTN`/`RO`/`STAT`/`LOG`/`PAGE` | a log line, a start, a stop, a page change | no |

Nothing owed means the glass matches, and a cache taken then is exactly what
a repaint would draw. So the promise is that word inverted, called at both
edges of the one lock hold each app already had — `te_show` and
`fd_paint_now` — plus each `W_PAINT`, which settles everything by
construction. `te_toggle` gets a fourth call, at the `TS_OPEN` transition,
because §16.1's first bug was withdrawing a tick late.

**This is the live answer the feature was asked for in the first place**: a
connected terminal at an idle prompt holds a cache and loses it the moment
the host prints, and a listening FTP server holds one until a client says
something.

Both claim two colours and both are true — `CBLACK`/`CWHITE` only, checked at
the framebuffer rather than by grep. Telnet's cache is **11,642 bytes at one
plane against 46,526 at four**, the FTP server's **8,178 against 32,670**.

### 22.1 What it does not close, and why that is the right trade

`te_feed` and `fd_log` write with **no gfx lock held** — rule 3, because a
`NETV_RECV` is up to 274 ms on the cable and the cursor may not stop for it —
so between a byte landing and the flush taking the lock there is a window in
which a raise restores the frame before it.

It self-heals in both: the debt survives the raise and the next pass draws it.
So the cost is **up to one tick of one stale line**, against a full repaint —
1,152 glyph cells for Telnet, about a second on a 4.77 MHz 8088 — every time
the window is uncovered. Closing it needs the withdrawal at `te_feed`, and
§20.6 rule 7 forbids a worker touching `wm_saveu` without the lock: the clear
frees the cache and the UI task may be blitting out of it.

### 22.2 The FTP server already had §3.1's shape, and that is why it was free

`fd_paint_now` opens with `cmp byte [fd_dirty], 0 / je .out`, so the clip is
armed only when there is something to draw — the decision is already above the
arm. And §77.33's rule is what makes the withdrawal sufficient: a clip that
answers CF=1 leaves `[fd_dirty]` **set** and the next flush tries again, so
the covered path leaves with the promise withdrawn and the debt still owed.

Telnet needed the same and had it: `te_step` calls `te_owed` before `te_show`.

### 22.3 Two defects found on the way

* **`te_screen` was not clearing `[te_scrl]`** (SPEC.md §70.7.2). It draws all
  eighteen rows from the buffer, so the glass is right — but the pending
  "blit up by N" survived, and `te_scrollpaint` then moves the whole terminal
  up N rows and marks only the N it vacated. Rows 0..17−N are left showing
  rows N..17. Reachable with no cache involved: text scrolls while the window
  is covered, the window is uncovered into a full `te_paint`, and the next
  worker pass blits a screen that was already correct.
* **`[te_dirty]` was not spent after the chrome was drawn.** `te_step` zeroes
  it every pass so nothing depended on it, but `te_owed` could not be read as
  the whole truth until it was — which is the precondition `te_promise` needs.

### 22.4 `tests/netpromise.py`, and the leg that is not in it

Five legs each — REST, TWO, BANKED, PIXELS, OWED — and **OWED is the one that
bites**: a build that set the flag once in its entry proc passes the other
four and is wrong for the whole of a session. Telnet passes all five.

**The FTP server's OWED leg is skipped, and the reason is the machine.**
`fd_hire` is called from `fd_start`'s success path only, and `fd_start` opens
with `net_find` — so on MartyPC, which has no NIC of any kind
(docs/TESTING.md's entry 5), pressing Start fails at the first call and the
worker is never spawned. With no worker nothing calls `fd_flush_glass`, so a
debt poked in from the host is never flushed. The four legs above are exactly
the ones that need no wire.

Two harness notes it left behind, both worth keeping:

* **`tests/netpromise.py` is VGA-only geometry, and refuses rather than
  failing at the drag.** It drops the app at y = 240 so the drive window's
  title bar is clear of it; a 640×200 CGA has a 155-row desktop band and no
  second row of window to be had, and the mouse then reports a clamp, which
  reads like a broken harness. Nothing is lost: `WSU_PW` is 1 by *adapter* on
  both 1bpp screens, so the depth claim is a VGA question, and the promise
  itself is adapter-independent.
* **`tests/telnet.py` takes a `--machine` now.** Its two adapters name the
  machines that want the licensed IBM ROM (`ibm5150_82_v4`), which a fresh
  container has no copy of — MartyPC then exits at once with *"ROM set not
  found in ROM set map"*, which also reads like a broken harness rather than
  a missing file. `os8088_5150_cga_gla` and `os8088_5150_herc_gla` are the
  GLaBIOS twins and already existed; this gate takes no timing, so they
  answer every question it asks. It passes on both.

Its home is `tests/ftpd.py`, which boots QEMU with `ETHER.DRV`, drives a real
client and can read guest memory through `ethernet.py`'s `pmemsave`. **That
gate does not run in this container and the branch is not why.** Three runs:
at the branch base it fails twice at `the card never bound an address` before
reaching any window; on the branch build the card binds and then `FTPD.O88
never opened a window in 4 attempts`, and that reproduces with
`apps/ftpd/ftpd.asm` stashed out. The package itself is fine — `ftpdflick`
and `ftpdfocus` open it on MartyPC and both pass — so what is failing is that
gate's own launch on the ether machine, in a container whose QEMU networking
could not even bind an address twice running. Two different failures and no
clean baseline, so **no claim either way about `tests/ftpd.py` on a machine
where it works**; it wants running somewhere that has one, and the OWED leg
added there when it does.

## 23. The Task Manager — looked at, and an ORDINARY cache cannot

> **§25 is where this ends up.** Everything below stands as measured and the
> conclusion is superseded: the window holds a cache now, by making a
> *weaker* promise than §11.96.1's. Read this for why the obvious route does
> not get there.

**§18.4 was right and for the wrong reason**, which is worth more than being
right for the right one. It said the load meter is live and there is no mode
to toggle; the memory and heap pages are not live, and the promise for them
was built, measured, and taken back out.

SPEC.md **§28.8** is the record. In one sentence: **the raise cache is a
purgeable heap claim, and these two pages are the tree's only reporters of
purgeable heap claims, on purpose.** Taking a cache changes what the page must
show; showing it frees the cache.

The shape was the best in the tree — better than Telnet's and the FTP
server's. `tm_paintdue` and `tm_quiet` already answer §11.96.1's two questions
before the lock is taken, and `tm_quiet` names its three spans exactly: the
claim table, `[tm_kb]` and the instance block. So the promise was
`[tm_view] != 0` AND `tm_quiet` quiet, withdrawn at the top of the one lock
hold and granted after the draw — the Control Panel's per-page answer and
Telnet's per-debt answer in one window, and it needed no restructuring at all.

**The measurement is what killed it**, and it took fine sampling to see:
`mo.click`'s own 1.5 s settle hides the whole event, so the first two probes
reported "no cache is ever banked" and the truth is "banked and destroyed
0.7 seconds later".

```
  cache=0000 saveu=True   x1        the press
  cache=3880 saveu=True   x14       banked at the raise - about 0.7s
  cache=0000 saveu=False  x1        withdrawn, and the claim freed with it
  cache=0000 saveu=True   x104      re-granted, and nothing re-banks it
```

The control is the same run with no cache in play: `saveu` stays `True` and
`tm_quiet` never fires across eight seconds. **The only variable between quiet
and not quiet is whether a cache exists.**

### 23.1 Why this one is different from every other candidate

Every earlier refusal in this document is an app whose *worker* changes its
content. This one is an app whose **content includes the memory its own cache
is made of**, and that is a class §11.96.1 does not name:

> a window may not hold a raise cache if the cache's own existence changes
> what the window draws.

The Task Manager is the only instance in the tree, and it is the only one
there can be — nothing else reports the heap.

### 23.2 The mask, and why the obvious one is wrong

Mask the cache out of `tm_quiet`'s **hash** — out of what forces a redraw,
not out of what is drawn. `tm_sumclaim` is the shape: hash a purgeable record
as the six ZERO bytes the same slot already hashes as while it is free,
rather than skipping it, because `tm_sumb` is `rol`/`add` per byte and is
position-sensitive. Written, measured, and taken back out with the rest.

**Masking every `MEM_P_WSAVE` record is the wrong mask.** My first argument
for it was that the page is never wrong when anybody can see it — the cache
exists only while the window is covered, and the raise that uncovers it frees
it. That holds for *this window's own* cache and for nothing else:

* **A partially covered window is still visible.** Covered on one edge, it
  shows most of its content, holds a raise cache the whole time, and is being
  looked at — and so is the heap page beside it.
* **The mask hides every window's cache, not this one's.** A raise cache is
  not bounded in time; a window can sit covered for hours. A fully visible
  heap page would report `PURGE` short by that claim for all of it, and go on
  doing so after the covered window is uncovered if nothing else moves the
  table.

**The drag cache is the one that is safe to mask**, for the property the raise
cache lacks: it exists only for the length of one bounded gesture somebody is
actively performing, and `wm_dc_done` spends it at the drop.

**It is not separately maskable today, and that is the finding under the
finding.** `wm_dc_take` banks *through* `wm_su_take`, and `wm_su_slot` gives
both the same owner tag — `MEM_P_WSAVE + slot` — so nothing in the claim table
tells a drag cache from a raise cache, and `[wm_dc_win]` is a kernel word no
package can see. Telling them apart needs a kernel-side distinction first: a
second purge tag, an owner bit, or an API that answers the question.

**And masking the drag cache alone does not unblock this window**, because the
loop is driven by the Task Manager's own RAISE cache. The drag mask is worth
having on its own — one fewer needless repaint of the densest list in the tree
per drag — but it is a separate change with a separate justification, and the
Task Manager's promise still waits on a rule for raise caches.

### 23.3 Where the list stands now

| | window | promise | banked |
|---|---|---|---|
| TeXPad | 628×400 | header, always | 30,875 at 1 plane |
| Browser | 496×150 | per FETCH (§71.11) | one plane |
| Control Panel | 322×151 | per PAGE (§31.12) | 22,190 at 4 |
| Fractal | 322×199 | per FRAME (§40.4) | 30,254 at 4 |
| Telnet | 530×190 | per DEBT (§70.7) | 11,642 at 1 |
| FTP server | 402×176 | per DEBT (§77.47) | 8,178 at 1 |
| **Task Manager** | 234×284 | **cannot** — §28.8, until §25 | — |
| Note Pad | 266×180 | header | one plane |

What is left is §11.4's games — Cyclone, Missile Command, Arkanoid — which
still want §3.1's decide-before-you-arm rework, and Missile Command needs a
band (§8) to fit VGA at all. All three get a cache on the 1bpp adapters for
the price of the toggle alone.

## 24. §3.1's blocker was a kernel reorder, not a rework of every app

This document's §3.1 is the finding that nearly ended the whole proposal:
`wm_clip_set` drops the drawer's raise cache **before** the occlusion walk, so
a window that is visible but wholly covered destroys its own cache and is then
told there was nothing to draw. It costed the fix as app-side and per-app —
"ask *have I anything to draw* before taking the lock, not after" — and listed
`app_tmr_task` and `cy_worker` as the first two of many.

**It is six bytes in one place.** SPEC.md §11.96.18: move the drop to `.done`,
on the path where the region really was armed. Nothing between the seed and
the walk draws, and `.none` arms nothing at all, so the move is safe by
inspection. Every app is fixed at once, and none of them has to remember
anything — which was §11.96's design intent for that call in the first place.

### 24.1 The cache word is not the instrument, and that cost four probes

The obvious gate is "cover it wholly and watch `wm_su_segs` survive". Three
readings of the same build:

| how it was watched | what it reported |
|---|---|
| `mo.dblclick`, then one read 1.8 s later | the cache **alive** |
| a tight `advance`/`run` sampling loop | **dead** from ~130 ms, for 5.9 s |
| a breakpoint armed just after the gesture | **no bank at all** |

Whether the bank lands moves with the act of looking. The gate asserts the
**call** instead — an exec breakpoint on `wm_su_drop` with BX compared against
the window record — which is stable in every run, needs no `WF_SAVEU` forced
into the record, and is the change's claim word for word: **4 calls when
partly covered, 0 when wholly covered.**

### 24.2 Two wrong answers on the way, and what killed each

* **"Something else drops it."** `wm_su_drop` has ZERO hits for a wholly
  covered window across minutes of guest time. Nothing drops it.
* **"The heap sheds it."** `MEM_P_WSAVE` is `MEM_PG_TRIV`, the cheapest
  purgeable rank, so this was the strong hypothesis — and `mem_pg_forget`
  never fires. The heap had **415,744 bytes free** against a 32,874-byte need,
  identical on every attempt, and it still banked only 3 of 7. Memory was
  never the variable.

What was: **`wm_front` banks the OUTGOING FRONT and nothing else.** A raise
click that did not land means the subject was never in front, so the cover
banked nothing — and a gate reading `wm_su_segs` afterwards sees 0000 and
calls it a cache that died. `tests/clipkeep.py` proves the raise through
`wm_zord` now, and so should anything else that covers a window to watch what
happens to it.

## 25. The Task Manager holds one after all, by promising something weaker

§23 measured a loop and called it a refusal. The loop is real and the refusal
was one step too far: what forced the withdrawal was **§11.96.1's promise**,
*my content does not change while I am not drawing*, and this window does not
have to make that one. SPEC.md **§28.11** is the record; the promise it makes
instead is

> my content may change while I am covered, and I will make the restored
> pixels right before you see them.

**The kernel already routes it and it costs no kernel bytes.** §11.96.11's
band contract is "the kernel banks the band, `wm_damage` tells you you owe the
rest", and `wm_draw_win` sends a *banded* restore through W_PAINT where a
plain one skips it. A band covering the **whole content** is the degenerate
case — everything banked, nothing owed — so `wm_su_orect` insets `x1` past
`x2` and `wm_damage` answers the **empty** rect §11.90.2 already documents as
legal. The app is called, told it owes nothing, and spends its own debt.

`tm_clear_owed` already asked `OSAPI_WM_DAMAGE`, so it answers CF now and
`tm_paint` branches on that alone: CF=1 or a real rect → the fill it always
did, then `tm_draw_full`; **empty → `tm_update`**, and nothing else.

**+99 package bytes, zero kernel bytes.** `tm_promise` names the band and
makes the promise per page; `tm_qpeek` (13 bytes) is `tm_elchk`'s compare
without the store, so `tm_quiet` stops eating the debt on intervals that were
refused for being covered — a consumed debt cannot be replayed, and the debt
is the only record of what moved.

### 25.1 The three things that had to already be true

None of them was built for this and all three were needed:

* **§11.96.18** — the six-byte reorder of §24. A wholly covered window that
  destroys its own cache has nothing to restore.
* **§28.10's `WF_OWNBG`** — `wm_damage` answers "whole" without it, so the
  window could never be *told* it owed nothing; and `wm_band` refuses without
  it as well.
* **§28.11.1's `tm_qpeek`** — §28.6.1 had worked around the consuming problem
  by ordering `tm_paintdue` before `tm_quiet`. That is enough while the answer
  to "cannot draw" is "the next raise repaints everything"; it is not enough
  once the raise is a restore.

### 25.2 `tests/tmrepair.py`, and the leg written so it cannot pass vacuously

```
PROMISE : Task Manager (247,100) 234x284 view=2 saveu=True band=[232,0,0,0]
KEPT    : wholly=True - 0 wm_su_drop calls for us across 8 polls
REPAIR  : 1 wm_su_occl call(s) for us at the uncover - 0 subpixels differ
          against a forced full repaint (6192 moved while it was covered)
LIVE    : view=0 saveu=False band=[0,0,0,0]
```

REPAIR covers the window wholly, opens and closes a package under it so the
claim table and the instance block both move, then closes the cover. **Without
a live cache that leg passes anyway** — `wm_damage` would answer WHOLE,
`tm_paint` would draw everything, and the pixels would match for the wrong
reason. So it counts `wm_su_occl` with DI on the window record: that call sits
in `wm_su_try` past every refusal in `wm_su_ck`, so reaching it *is* the
restore going ahead, and it is the one instrument §24.1 leaves standing.

Servicing a breakpoint and driving the mouse are the same loop there, which is
why the file has its own `edge`: `os88mouse` waits on the guest's own
published `mouse_btn`, and a guest parked at a breakpoint never publishes it.

**And the leg earned its keep immediately.** Folding the damage question into
`tm_clear_owed` let `tm_paint` drop its four `push`es — at which point
`tm_promise`'s unsaved DX, an output of `OSAPI_WM_GEOM` it never reads,
reached the kernel. W_PAINT's *"clobbers: nothing (flags only)"* is
load-bearing, and the symptom was not a wrong pixel anywhere: it was **REPAIR
reporting 0 restores** on a build where the previous one reported 1, with the
picture correct both times because a cache miss draws everything. Two `push`es
fixed it. A leg that only compared pixels would have called that build good.

### 25.3 The loop came back in the HARNESS, and that is worth writing down

`tests/tmground.py`'s PIXELS leg — *cover it, uncover it, and the glass must
equal a forced full repaint* — started failing at **396 subpixels one run and
0 the next**. Not the app: every one of those subpixels was `PURGE nnK( 3)`
against `( 2)` and the claim rows the count shifts.

**Forcing a full repaint moves a purgeable claim, and this page reports
purgeable claims.** §28.8's loop, one level up: the instrument perturbs the
thing it is measuring, and it is not this window's own cache that does it —
with the drive window left up, it is *that* window's claim crossing the
comparison. Three changes fix the leg, and the second is the one that matters:

1. **The restore has to miss**, or the ground fill never runs and the leg
   tests §28.11 instead of §28.10. `WF_SAVEU` cleared in the record while the
   window is covered is the deterministic half — `wm_su_ck` asks `wm_dc_ok`
   first, and a poke is not `wm_saveu`, so nothing is freed and no figure
   moves.
2. **Nothing else may be on the desktop.** PIXELS runs last now and opens the
   cover it closes. With the Task Manager alone on the glass: **0 subpixels**,
   in each of the three runs taken since.
3. **Both captures settle rather than tick**, because a claim taken and
   dropped again is two changes to this page and a fixed frame count lands
   wherever that transient happens to be.

The general lesson is the one §24.1 already half-states: **on this window,
every instrument that changes the machine changes the picture.** The cache
word cannot be read; a forced repaint cannot be used as a reference while
anything else is up; and the only stable assertions left are the *calls*.

### 25.4 Where the list stands now

| | window | promise | banked |
|---|---|---|---|
| TeXPad | 628×400 | header, always | 30,875 at 1 plane |
| Browser | 496×150 | per FETCH (§71.11) | one plane |
| Control Panel | 322×151 | per PAGE (§31.12) | 22,190 at 4 |
| Fractal | 322×199 | per FRAME (§40.4) | 30,254 at 4 |
| Telnet | 530×190 | per DEBT (§70.7) | 11,642 at 1 |
| FTP server | 402×176 | per DEBT (§77.47) | 8,178 at 1 |
| **Task Manager** | 234×284 | **per PAGE, as a REPAIR (§28.11)** | 32,874 at 4 |
| Note Pad | 266×180 | header | one plane |

Seven of eight. §11.4's games are what is left — Cyclone, Missile Command,
Arkanoid — and they still want §3.1's decide-before-you-arm rework, with
Missile Command needing a band (§8) to fit VGA at all.

**And §28.11 is a doorway rather than one app's fix.** Any window that can
replay what moved while it was covered can hold a cache now, on the same
whole-content band, without making §11.96.1's promise at all. The Task Manager
is the hardest case in the tree and it is the one that proves the route.

## 26. The field report: it never USES one, and the two reasons are different

The owner drove four steps with the Task Manager on the **memory** page and a
file browser beside it, and reported that the save-under is never used and may
be making things worse:

1. drag the browser partly over it — repaints in the background, correctly;
2. drag it back off — **repaints completely**, no damage rect honoured;
3. maximize the browser over it;
4. bring the Task Manager to the front — **repaints completely**.

Measured, with `wm_su_take` / `wm_su_drop` / `wm_su_occl` armed and the window
record compared:

```
idle        : drop/us=1                          per worker interval
1 DRAG ON   : take/us=1  drop/us=5               partial cover
2 DRAG OFF  : take/us=0  drop/us=3  occl/us=0    -> full repaint
3 MAXIMIZE  : take/us=0              wholly=True -> NOTHING banked
4 RAISE     : nothing to restore
```

…and the drops are attributed: **every one on an idle, visible Task Manager is
`wm_clip_set.done+8`**, 14 of them over ~1,500 frames.

**§28.8's purge loop is not what they are seeing.** It is measurably gone —
`tmrepair`'s KEPT leg still shows 0 drops across 8 polls while wholly covered,
and the restore+repair works there. What drops the cache is §11.96.18's own
call, and it is correct: the window is visible, it is about to draw, the glass
is being kept current, so the banked copy is the frame before.

The cache is never *used* for two unrelated reasons.

### 26.1 Only a RAISE and a NEW WINDOW bank — a zoom banks nothing

`grep` gives `wm_su_precover` exactly one caller, `wm_show_b`. So the two
banking transitions are `wm_raise` (the outgoing front) and a window
appearing. **Zooming or resizing the already-front window over something banks
nothing**, which is step 3 measured: `take/us=0` on a cover that is
`wholly=True`. It is the most natural way to cover a window completely and it
is the one route with no bank on it.

Wiring `wm_su_precover` into `wm_zoom`/`wm_resize` is the fix, and it is not
the six-byte one it looks like. `wm_show_b` gets away with clearing the
visible bit because a new window has no old pixels; a **grow** needs
`wm_win_over` against the *new* rect and `wm_obscured` against the *old* one,
or it cheerfully banks the coverer's own pixels as the covered window's
content for anything it already overlapped. Two rects, ~20-30 kernel bytes.
**Open.**

### 26.2 A partly covered window can never hold one, so step 2 is a different fix

It is visible, it draws every interval, and it drops. The covered strip is the
only stale part and the pixels to put back were never banked, because the
coverer owned them. The saveu is the wrong tool for step 2 — and §28.10.2 was
already the right one, sitting unbuilt.

**Built now.** §28.2 had made a row five independently-checked chunks, all of
them through `tm_elchk`, so the rule is one clause wider: *draw this chunk if
its key changed, **or if the damage took its pixels***. `[tm_dmg]` is that
rect, published by `tm_clear_owed` because what it filled is exactly what has
to be lettered again, and `tm_dmg_hit` is the clause.

The seven `TMC_*` elements have no chunk loop to derive a band from, so each
site names the y range it is about to draw at — a constant it already had —
through `tm_elchk_y`. Only the scroll bar is still forced, and deliberately.

**+242 package bytes, no kernel bytes.** A/B on the memory page with a window
dragged off part of it, counting this window's own primitive calls:

| the memory page | `font_run` | `gfx_fill` |
|---|---|---|
| the partial repaint, before | 97 | 40 |
| the partial repaint, after | **52** | **21** |
| a forced WHOLE repaint of the same page | 97 | 41 |

**The partial repaint was issuing every call a whole one did**, and the third
row is the second's own reference taken in the same run. `tests/tmdmg.py` is
the gate; SPEC.md §28.10.2 carries the three things it has to get right, of
which the sharpest is that **pixels cannot see this defect at all** — a redraw
of the same characters changes nothing on the glass and `m.flicker` reports a
still screen either way.

## 27. Where this stands, and the three things still open

Four commits on `damage-repair`, in order, each with its own gate:

| | what | measured |
|---|---|---|
| §28.10.2 | the Task Manager letters only the damaged part | 225 cells → **0**, against 549 for a whole repaint |
| §11.3.3 | **the cull** — a marked window does not paint where something above it is about to | 452 cells under the mover → **26** |
| — | the memory rule set, rewritten (below) | — |
| §11.96.16.2 | a window **zoomed** over another banks it | 0 caches at a maximize → banked, and the reveal answered off it |

**The instrument lesson runs through all of it, and it changed twice.** Pixels
cannot see a repaint that draws the same characters again — `m.flicker`
reports a still screen either way, which is how §28.10.2's cost went unnoticed
for so long. So the gates counted CALLS. Then §11.3.3 landed and broke that
too: a culled cell is still a call, and a chunk the region cut retries
(§28.2), so an armed region drives the call count *up* while the pixels go
down. `tests/dispcells.py` is where it settled — it reads the armed region at
every run and counts the cells that actually got through — and both `tmdmg`
and `dmgcull` are on it.

### 27.1 The memory rule set, because the guard nobody printed was the binding one

Not part of the save-under work, but it is what the zoom precover ran into and
it is the reason a 30-byte feature became a conversation. Measured on the
shipped kernel: **3,584 bytes before `KERN_BUDGET`** and **1,670 before
`MIN_RAM_KB`** — and `KERN_BUDGET` sat *above* guard 5's ceiling, so it could
not be reached and a ~5KB recovery read as slack under a ceiling that was not
one. There are three rules now (kernel.asm's `MIN_RAM_KB` note,
docs/KERNEL-MEMORY.md's opening): kern_small boots on 128KB, kern_big boots on
196KB, kern_big fully **resides** in 128KB at a bare desktop. Knob builds are
bound by the 64KB segment and nothing else. `kernsize` prints both ceilings.

### 27.2 Open, in the order to take them

1. **A DRAG still does not precover.** §11.96.16.2 hooks `wm_rz_paint`, which
   catches the zoom, the zoom's restore and the grow box — every *geometry*
   change. `ui_drag` moves a window by writing x/y and never goes through it,
   so dragging one wholly over another still banks nothing and the raise back
   is a full repaint. Same shape as the fix that landed: the drop knows both
   rects, and the two tests want the rect at two different moments
   (`wm_win_over` the new, `wm_obscured` the glass). `wm_rz_swap` and
   `wm_su_pcrp` already exist.
2. **`tools/kernsize.py --bless` is broken, and was before any of this.** Its
   per-module pass fails on an unmodified tree and `--bless` returns 1 on that
   failure without writing the baseline — so `t_kernbudget` demands a bless
   that cannot be performed the moment `KERN_BUDGET` moves. The gate's own
   advice ("One command: `tools/kernsize.py --bless`") is currently false. The
   baseline was updated by hand for §27.1's change.
3. **The drag cache refresh.** Reported from the field: dragging a window over
   the Task Manager's memory page makes it redraw half of itself on the next
   worker interval. That is **§28.8's loop in a new place** — `wm_dc_take`
   banks through `wm_su_take` and its claim is purgeable, the memory page
   draws purgeable bands, so the claim appearing and going again is two
   changes to what the page must show. §28.8.1 already records that the drag
   cache is the one that *would* be safe to mask and that it is not
   separately maskable: `wm_su_slot` gives the drag cache and the raise cache
   the same owner tag, `MEM_P_WSAVE + slot`. Telling them apart needs a
   kernel-side distinction that does not exist yet.

## 28. §11.94.5's snap grows Paint's DOCUMENT, not just its window

**Open, and a decision rather than a fix.** `a15a446` put a window's *size* on
the byte grid as well as its origin, which is right and which §11.94.5.1
already corrected once. What nothing has looked at is what it does to the one
window in the tree that treats its content area as a **document**.

Paint's canvas is not a view of a document; it *is* the document, and
`pt_track` says so in two instructions at the top of every `W_PAINT`:

```
    mov ax, [pt_contw]              ; the canvas the content asks for
    sub ax, PT_CV_X
```

So the chain runs: `pt_adopt` sets the canvas to the picture's width →
`pt_wfollow`/`pt_wsize` ask for a frame of `cw + PT_CHROME_W` →
`OSAPI_WM_RESIZE` → `wm_snap_win` rounds the content width **up** to the next
multiple of 8 → the next `W_PAINT` reaches `pt_track`, which finds a content
area 6px wider than the canvas and grows the canvas to match. Paint cannot
tell that growth from a grow-box drag, because in the ABI there is nothing to
tell it with (§11.1 has no resize callback; `pt_track` exists *because* of
that).

Measured on `os8088_xt_vga`, opening `build/OS8088.GIF` (466×110):

| | plain | `NOSIZESNAP=1` |
|---|---|---|
| `[pt_pw]` — the picture | 466 | 466 |
| `[pt_cw]` — the canvas | **472** | 466 |
| `[pt_stride]` | 236 | 236 |
| `biWidth` in the live DIB header | **472** | 466 |
| `[pt_trunc]`, `[pt_fitcut]` | 0, 0 | 0, 0 |

**The last row is why this is not cosmetic.** `pt_bmp_hdr` writes
`biWidth` from `[pt_cw]`, so opening a 466-wide picture and saving it writes a
472-wide file with six columns of white welded to the right-hand edge. Nothing
warns: `[pt_trunc]` is for the opposite case and is 0 here, and the six columns
are the same colour as a blank canvas, so no screenshot shows it either. It is
`tests/paintrow.py` that catches it, and that row is **correctly red** — it
asserts `cw == iw`, which is the property that broke. Do not relax it to 472;
that would bless the defect.

`build/OS8088.GIF` is 466 wide, which is 2 mod 8, so this is not a corner
case — seven widths in eight reach it.

### 28.1 The three places it could be fixed, and what each costs

1. **The kernel stops snapping a size the owner asked for by name.**
   `OSAPI_WM_RESIZE` with an exact frame is not a hand on a grow box, and
   §11.94.5's own argument — the fast path, `gfx_restore`'s whole bytes,
   `wm_su_edge`'s read-back — is about where a window *ends up*, not about who
   asked. The cost is that Paint, the window whose blit is measured in
   seconds and which therefore wants the alignment most, is the one that
   stops getting it.
2. **Paint lets its content area be wider than its canvas.** The honest fix
   and the expensive one: the canvas becomes a thing *inside* the content
   rather than equal to it, which touches `pt_track`, `pt_org`, `pt_wfix`,
   `pt_clip` and every click ladder derived from the four words `pt_org`
   latches. It also buys something real — a window that can be bigger than a
   small picture without inventing document.
3. **Paint makes the frame follow the canvas instead.** `pt_wfix` already
   writes `W_W`/`W_H` directly and so **bypasses the snap** — it is the
   refusal path, "the frame follows the canvas, not the drag". Routing the
   adopt through it keeps the document honest for the price of an unsnapped
   Paint window, which is option 1's cost arriving by another road and
   without the kernel change.

Nothing here is obviously right, which is why this section records the
measurement and stops. What it must not become is a test expectation of 472.
