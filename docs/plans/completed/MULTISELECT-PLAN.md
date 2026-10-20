# Multi-select in the Disk window — the plan, and what it came to

**Status: BUILT, all three waves (SPEC.md 22.27 is the contract), and SIZED
DOWN: 1,418 -> 797 resident bytes (§A.1), and 755 after kernel size pass 11
(`docs/plans/completed/ksp11/multisel.md`).** This file is the design record.
§A below is what the build MEASURED and where it left the plan, §A.1 the size
pass that took it under the 800 the owner then set; everything after it is
the plan as it was costed before a line was written, kept because its
estimates were wrong by a factor of two and the reasons are worth having.

## A. What it came to

**1,418 resident bytes on `kern_big`** - `.cold` +1,327, `.text` +25, `.bss`
+66 - against the plan's ESTIMATE of ~680. `kern_small` is byte-identical,
and `make MSELOFF=1` assembles byte-identical to the kernel the branch was cut
from (both checked by re-assembling `f8ba8eb3`'s source at the same build
number). Measured by `kernsize` against the `MSELOFF=1` arm and broken down
by symbol span from the NASM listing:

| wave | what | bytes (code + data) |
|---|---|---|
| 1 | the bitmap, Ctrl/Shift click, Shift+arrows, Ctrl+A, right-click, drawing | ~560 (54 of them `.bss`: nine bytes a block x 5, `fm_mnew`, `fm_mrc`) |
| 2 | the rubber band | ~340 |
| 3 | Cut/Copy/Paste of a set, Delete of a set, drag of a set, Ctrl+drag, cross-drive copy | ~520 |

The `.cold` rung CROSSED twice (73 -> 75 steps of 512, `KERN_SIZE` 97,280 ->
98,304): the rung had 494 bytes of slack and the feature is 1,327.

**The owner's four decisions** (all three waves; band from right of the name
column; Ctrl+drag copies, plus a drop on another DRIVE copies "if it is ~50
bytes or less" - it was **7**; the list in its own heap reservation,
purgeable at MED and cleared on paste) are all built as stated.

**Why the estimate was half the truth.** Not the assembler: the listing has
seven more out-of-range conditional jumps than the base (21 bytes). It was
the per-routine scaffolding the estimate did not count - every helper banks
the registers its callers rely on, the band needs its geometry clamped in two
axes and computed in two views, and the paste needs its list protected from
its own copy buffer (the purge floor) and from a second arm while it is
suspended. One size pass took 138 bytes back: Copy, Delete's count and
Delete itself now share one iterator over the set (`fm_mwalk`), the band
lost its separate clamp routine, the bitmap is updated in place by the same
diff that draws it (`fm_mdiff` with DL), and the list's records are the
operation record's own shape so `fcp_lget` is one string move.

**What it changed from the plan:**

- **The list is cleared on paste, Copy as well as Cut** (the owner's call), so
  the plan's Copy-owns/paste-borrows ownership is gone: a paste spends the
  clipboard at its start and frees the list at its end.
- **Purgeable, not movable.** The plan proposed a movable 1KB claim; the
  owner chose purgeable at MED, which needs no proc - and which needed the
  purge FLOOR during a paste, because otherwise the copy buffer that each
  entry claims is exactly the claimant that would shed the list.
- **Delete walks through `fcp_goto`, not `fmv_sync_x`** - as planned - and
  ends through `fm_paste_res`, the paste's own end, rather than a second copy
  of "re-list what changed, say a failure, repaint those windows".
- **The band's outline is not clamped** to the row area; only the selection
  arithmetic is (`fm_bqcell`). The outline follows the pointer the way the
  drag ghost always has.

**Gate:** `tests/msel.py` (soak, `-k msel`), 32 s on MartyPC: every gesture
checked on `FS_MSEL` and on the glass, then Copy/Paste of a set, a drag of a
set, a Ctrl+drag copy and Delete of a set. RED against `MSELOFF=1`.
**Not covered by that row:** the cross-drive copy (two Disk windows on two
drives), Esc at an overwrite question part-way through a list, the icon
view's band, and the chooser's opt-out - each was read, none was driven.

## A.1 The size pass: 1,418 -> 797

The owner's brief: **under 800 bytes, without losing some form of drag
select; clicking or dragging an entry by its SIZE column is not required.**
Measured the same way as §A - `kernsize`'s section figures, `MSELOFF=1`
against the shipped arm, at one commit - and broken down by symbol span off
the NASM listing of both arms.

| | before | after |
|---|---|---|
| `.cold` | +1,327 | **+739** |
| `.text` | +25 | **+13** |
| `.bss` | +66 | **+45** |
| **resident** | **1,418** | **797** |
| `KERN_SIZE` against `MSELOFF=1` | +1,024 (two cold rungs) | +512 (one) |

`kern_small` and `MSELOFF=1` both still assemble byte-identical to the kernel
the branch was cut from (`af9e326^`), checked by assembling all three.

**Every feature survived.** Ctrl+click, Shift+click, Shift+arrows, Ctrl+A,
the right-click on a set, Cut/Copy/Paste of a set as a purgeable list cleared
on paste, Delete of a set with its "N items"/"+contents" line, a drag of a
set, Ctrl+drag copies and a drop on another drive copies. **One gesture
changed form and two changed detail**, each stated in SPEC.md 22.27:

- **The rubber band is a DRAG SELECT.** It was a rectangle with an XOR
  outline, tracked by its own loop with its geometry clamped in two axes and
  two views (~400 bytes). It is now the RUN from the pressed row to the row
  under the pointer - Shift+click's own routine, `fm_mto` - drawn live from
  the window's existing `W_ONDRAG` (SPEC.md 13.8.1): the press arms
  `FM_BBAND` in `os88ui_armw` and the release's `os88ui_fire` spends it, so
  there is no tracking loop, no outline (the bands are the feedback) and no
  per-view geometry at all (~60 bytes). A press right of the names on a row
  SELECTS that row and is the anchor; one off the entries clears and the
  first entry reached is.
- **A right-click with a set up keeps it wherever the press lands** (it kept
  it only on a member) and shows an entry's menu, which acts on the set.
- **A plain click on a set member** goes through the ordinary click after
  the collapse rather than stamping itself, so its double-click window is
  the ordinary one's.

**What paid for it, largest first:**

1. **The band** (above): ~340 bytes.
2. **The diff's target is a RUN, not a bitmap.** With the rectangle gone
   every target any gesture has is one run, so `fm_mnew` (8 bytes of `.bss`),
   `fm_mclr` and `fm_mrun`'s bit-setting loop are replaced by an `lo <= i <=
   hi` test inside `fm_mset`; the draw-only walk `fm_lsel_bar` needs is the
   reversed run, so there is no flag either.
3. **The single selection's band code BECAME the multi-selection's.** A
   click's 22.2 pair of bands, a plain arrow's (22.26) and a right-click's
   were all an "old band off, new band on" the diff already does, so on this
   build each is `fm_menter` + `fm_mone` and the base code is `%ifndef
   FM_MSEL`. That deleted the plain-click-on-a-set, plain-arrow-on-a-set and
   right-click-on-a-set special cases with it (`fm_mrclk`, 40 bytes, is
   gone). The arrow draws its new selection BEFORE the view follows, and the
   scroll's lift and relay carry it.
4. **Delete of a set rides the single Delete.** It went through `fcp_goto`,
   a write batch and `fm_paste_res`; it now uses the single Delete's own
   `fmv_sync_x` and `.verdict`/`.bcast`, and the single Delete's file-or-
   folder dispatch IS the walk's callee `fm_mw_del`. The batch was not buying
   anything: a delete finds its name on the disk, and each one's sync only
   marks and owes. That needed `fmv_sync_x` to keep the bits, and it does so
   with a sentinel instead of banking eight bytes: it stores `FM_MKEEP` in
   `FS_SEL` across the re-list (it restores `FS_SEL` after it anyway) and
   `fmv_store` skips the zero on seeing it - 24 bytes became 9. **A sync that
   lands in the ROOT** (the folder was deleted under it) **empties the bits**:
   they would name the root's entries, and a set Delete then walks them by
   index. The old code had that hazard in its banking too, and its Delete
   happened to dodge it through `fcp_goto`.
5. **The list.** Record 0 is armed by `fcp_arm` itself (a first walk that
   stops at the first member), so the clipboard needs no field-by-field copy;
   `fcp_lget` is gone, its one remaining caller inlining the move; the paste
   walks the records from the LAST down, so `fcp_lnxt` is gone (a listing is
   sorted, so the write order is not seen); `fcp_lcnt` counts BYTES, so
   neither end shifts; `fm_ftype` sits in front of `fm_onam` on this build so
   a record is one 15-byte move; `fcp_lfree` hands a 0 straight to
   `mem_free_x`, whose `mem_find_own` already refuses one; and `fcp_lbegin`
   answers a list purged before its paste with one `cmp [fcp_lseg], 1`,
   leaving the stale count to the next arm's free. A list purged UNDER a
   paste - another task's claim while a question is up, which the floor
   (being this task's) does not stop - ends it through `fcp_lend`, so the
   floor still comes down; the first cut of this pass returned instead and
   would have left the UI task's floor raised.
6. **The Shift anchor is ONE byte**, `fm_anch`, not one a block: only the
   front window takes a gesture (5 bytes of `.bss`), and `fm_mto` - which now
   enters the bitmap itself - validates it against the window's `FS_N`.
7. Smaller: `fm_dgarm`'s Ctrl test makes `FCP_COPY` with a `dec`; `fm_hit`
   answers 0FFFFh on a miss, which the drag select and the right-click hand
   straight to `fm_mone`, and in the LIST view leaves DI = the x it was handed,
   which the drag-select test reads instead of banking DX; `fm_armset` sits
   beside `fm_c_clip` so both jumps to it are short; the paste's purge floor is
   stored through DX, `fcp_paste`'s banked scratch.

**Gate:** `tests/msel.py` - every leg green, the band leg rewritten for the
drag select (down, up, and a press that does not move). RED against a build
with `fm_ondrag`'s `FM_BBAND` dispatch taken out, on the four drag-select
checks and nothing else.

---

# The plan as it was costed

**Status when written: PLAN.** Branch `multi-select`, cut from
`elendilon` at `f8ba8eb3`. Every byte figure below is an **ESTIMATE**, counted
by instruction from the shapes the existing code already uses, and none had
been assembled - §A above is what they turned out to be.

The ask, in the owner's words: Ctrl+click to add and remove single entries,
Shift+click for the run between the first selected entry and the clicked one,
Shift+arrow to select or unselect the next entry up or down, dragging out a
rectangle to select, and then Cut, Copy, Paste and the rest acting on the
whole set. Whether it merges is decided by what it costs `kern_big` in
**resident** bytes, and it cannot be an on-demand module: selecting files
must never ask for the system disk.

## 0. The answer, first

| wave | what | `.cold` (resident) | `.bss` |
|---|---|---|---|
| 1 | the selection itself: Ctrl+click, Shift+click, Shift+arrows, Ctrl+A, the drawing, the right-click | **~300** (250–400) | **55** |
| 2 | the rubber band (drag out a rectangle) | **~140** (110–180) | ~4 |
| 3 | the operations: Cut/Copy/Paste, Delete and drag-move on the set | **~240** (190–300) | ~6 |
| | **all three** | **~680** (550–880) | **~65** |

- **No ABI break, and none would make it smaller** (§8). Everything is
  internal to `kernel/files.inc` and `kernel/filecp.inc`. No `OSAPI_*` slot
  moves, no package is touched, `OSAPI_FILE_COPY` is untouched.
- **`kern_big` only; `kern_small` stays byte-identical** (§7). It is
  SPEC.md 22.26's arrangement for the same reason: on `kern_big` both files
  are resident `.cold` already, while on `kern_small` the copy engine is
  `FILECP.DRV` and so any multi-file operation would need the system disk
  there anyway.
- **The smallest useful cut is waves 1 and 3 without Shift+arrows and
  Ctrl+A: ~490.** The rubber band is the most expensive single gesture and,
  in list view, the least reachable one (§4.4) — it is the first thing to
  defer if the number has to come down.
- The cold rung has **494 bytes** left at `f8ba8eb3`, so all three waves cross
  one rung (+512 of footprint). That is reported, not designed against
  (CLAUDE.md, *Design for BYTES*).

## 1. What is there today

Read before believing any of the design below; every line of it leans on one
of these.

1. **The selection is one word per window**, `FS_SEL`, a directory index
   into that window's own cached listing, `0xFFFF` = none (SPEC.md 22,
   `kernel/files.inc` `FS_SEL equ 0`). `FS_CLKT` is the double-click stamp
   that moves with it.
2. **It is drawn as an XOR band** by one routine, `fm_sel_bar`
   (`files.inc`, "fm_sel_bar - XOR the selection highlight for ONE directory
   index"), which range-checks the index and its visibility itself. A
   selection change is two bands and no text (SPEC.md 22.2). `fm_lsel_bar`
   is the same routine on the layout's mirror `[fm_lsel]`, and it has **three
   callers that mean "the whole selection"**: the full painter's `.isel`, and
   `fm_scrollpaint`'s lift before the blit and relay after it (SPEC.md 22.11,
   22.26).
3. **`fmv_store` is the one place a listing is replaced** (SPEC.md 22.14) and
   it already resets `FS_SEL`; **`fmv_sync_x` banks `FS_SEL` across its
   re-list** because a sync is not navigation.
4. **The modifier keys are readable.** `kbm_shf` (`kernel/mouse.inc`) reads
   the BIOS's `KB_FLAG` at 0000:0417 — Shift is bits 0–1, Ctrl bit 2. Nothing
   in the click path samples it today: `fm_drag`'s header says a drag is always
   a move *"without a modifier key it has no way to report"*. It is readable
   at dispatch time, which is what every gesture here needs.
5. **Shift+arrow already arrives as an arrow.** On an XT keyboard Shift
   inverts NumLock, so int 16h hands over `'8'` with scan `48h`; on an
   enhanced keyboard the grey arrow arrives as scan `48h`, AL 0 or E0h. The
   scroll-key decode tests **AH alone** (`fm_onkey_x`, *THE FOUR SCROLL KEYS
   ARE ONE EXPRESSION*), and none of the ASCII tests above it can catch an
   `'8'`/`'2'`/`'9'`/`'3'`. So no key path changes; the handler asks the shift
   state once it has an arrow.
6. **The clipboard is one record, by NAME** (`fcp_cbop`, `fcp_cbdrv`,
   `fcp_cbcwd`, `fcp_cbtype`, `fcp_cbname`, SPEC.md 22.3), deliberately not
   an index: *"the whole point is that the listing changes between the Cut
   and the Paste"*. `fcp_paste` copies it whole into the operation record and
   runs `fcp_run`; a question suspends the operation in `.bss` and
   `fcp_answer` resumes it through `fcp_step`.
7. **A drag already IS a Cut-then-Paste**: `fm_drag` arms the clipboard as a
   Cut and `fm_dgdrop` hands the target to `fcp_paste` (SPEC.md 22.4).
8. **Delete captures the NAME at arm time** (`fm_onam`/`[fm_odir]`) and
   commits by name after `fmv_sync_x` (SPEC.md 22, `fm_edit_commit .del`).
9. **The copy engine already batches** (`[dskw_batch]`, `fcp_unbatch`) and
   already has a quiet volume switch, `fcp_goto`, that moves `[dsk_cwd]`
   without touching any window's cache (SPEC.md 22.3, *"fcp_goto is quiet
   inside one volume"*).
10. **A cached entry is 32 bytes**: the display name in a 16-byte field
    (≤ 12 characters and a NUL, so bytes 13–15 are spare), the type word at
    16, the first cluster at 18 (`fm_stage_name`).
11. **A listing is at most `DSK_NENT` = 64 entries on `kern_big`**
    (`kernel/dskwin.inc`), so a whole listing's selection is **8 bytes**.
12. **The Standard File chooser is a Disk window** (SPEC.md 38.1) and owns
    the fifth pool block, named by `[fdlg_blk]`. It picks ONE file.

## 2. The four facts that decide the shape

**A bitmap, not a list.** 64 entries is 8 bytes. Every question a gesture
asks — is this one selected, toggle it, which ones changed — is one bit, and
no gesture needs the names.

**The names are needed exactly once, by the clipboard**, and that is the only
place this design spends a heap claim (§6.1). Delete does not need them
(§6.2): it can run straight off the window's own cache, because `fcp_goto`
positions the volume without re-listing anything — which is the trap
`fmv_sync_x` would spring, since its re-list goes through `fmv_store` and
would wipe the very bits the delete is about to walk.

**Every redraw is a DIFF.** XOR is its own inverse, so "make the selection
this" is: invert every visible band whose bit differs, then store. One
routine (`fm_mset`, §3.2) serves Shift+click, Shift+arrows, Ctrl+A, the
rubber band on every tick, and collapsing a multi-selection back to one
entry — and it never draws a band it did not have to, so none of them flashes
a row that was already right (PERFORMANCE.md rule 2).

**The single selection is left exactly as it is.** The bitmap is empty
unless the user has asked for more than a single click gives them, every
existing path is the code it is today while it is empty, and an operation
asks one question — *are there any bits?* — to choose between the path it
has and the new one. That is what keeps the existing behaviour, its
measurements (SPEC.md 22.2's table, 22.26) and its gates true by
construction, and it is cheaper than re-expressing the single selection as
a one-bit set: the single paths already exist and cost nothing more.

## 3. The representation

### 3.1 Three fields per window

| field | size | meaning |
|---|---|---|
| `FS_MSEL` | 8 | the selection bitmap — **EMPTY in single mode**; when any bit is set it is the WHOLE selection, focus included |
| `FS_ANCH` | 1 | the Shift anchor, an index (`0xFF` = none) |
| `FS_SEL` | (exists) | the **focus**: the entry the arrows move, Enter opens and Rename names |

At `FS_SIZE` = 61 today that is 61 → 70 bytes a block, five blocks: **+45
`.bss`**, plus `fm_mnew`, the 8-byte target `fm_mset` diffs against: **53**
(§0 rounds it to 55).

Per window rather than one global bitmap with an owner, and the reason is the
screen and not the bytes: a global set would have to take its bands OFF the
window that last had it — a background window, which may be covered, so not
an XOR but a `wm_paint_dmg` — and that costs about what the 37 bytes of
`.bss` it saves.

### 3.2 The routines

| routine | does | est. |
|---|---|---|
| `fm_mbit` | AX = index → BX = byte, CH = mask | 19 |
| `fm_many` | ZF = 1 when the window's bitmap is empty | 13 |
| `fm_mset` | invert every band whose bit differs between `FS_MSEL` and `fm_mnew`, then copy | 40 |
| `fm_mrange` | `fm_mnew` = the run AX..DX, either order | 30 |
| `fm_mone` | collapse to single: `fm_mnew` = {AX} (or nothing), `fm_mset`, clear the bitmap without drawing, `FS_SEL` = AX | 18 |

`fm_mset` with `fm_mnew` zeroed and no copy draws every band in the set, so
the "whole selection" callers of item 2 above become: *bitmap empty →
`fm_lsel_bar` as today; else → draw the set*. That is one test at the top of
`fm_lsel_bar` (~14), and its three callers — the painter, the scroll's lift
and its relay — change not at all. The scroll stays correct for the reason it
is correct today: the lift runs at the OLD scroll and the relay at the new,
and the rows the blit exposes are painted unselected in between.

`fmv_store` clears the bitmap (~12), because every index is meaningless
there. `fmv_sync_x` banks it across its re-list the way it already banks
`FS_SEL` (~16): a sync re-lists the window where it already is, and dropping
the extra bits there without a repaint would leave their bands on the glass
with nothing behind them — the next Ctrl+click on one would then turn its band
OFF while setting its bit.

## 4. The gestures

The modifier is read once per click and once per arrow, from `KB_FLAG`
(~10). **In the chooser's block every modifier is ignored** (`cmp
bx,[fdlg_blk]`, ~6 a site, two sites): a chooser answers with one file, and a
second selected row would be a highlight that means nothing.

### 4.1 Ctrl+click toggles (~35)

The row's bit flips and its band is inverted — **one band, and no text**,
which is SPEC.md 22.2's cost for the cheapest case. Entering multi mode from a
single selection first sets the focus's bit, and its band is already on the
screen, so that costs no pixel. `FS_SEL` and the anchor move to the clicked
row; `FS_CLKT` is not stamped, so a Ctrl+click is never half a double-click.

Ctrl+click runs AFTER `fm_drag` has said it was not a drag, exactly where a
plain click is decided today; §6.3 is what a Ctrl+DRAG means.

### 4.2 Shift+click takes the run (~22)

`fm_mrange(anchor, clicked)` then `fm_mset`. The anchor is the focus when the
run starts from a single selection, and the last Ctrl+click's row inside a
multi-selection — Explorer's rule, and the reason the anchor is a field of its
own rather than `FS_SEL`: a second Shift+click must re-take the run from the
SAME end, not from wherever the last one landed. The run is in **index
order** in both views, which in the icon grid is reading order — the order
Explorer uses for its own grid.

### 4.3 Shift+arrows (~35), plain arrows (~8)

Shift+Up/Down/PgUp/PgDn move the focus by SPEC.md 22.26's own arithmetic and
its own scroll-follow, and the selection becomes `fm_mrange(anchor, focus)`:
moving away from the anchor selects, moving back towards it unselects, which
is the owner's *"selecting/unselecting the next file up/down"* — and it is
the same routine as Shift+click, so the two cannot disagree about a run. The
differences from 22.26's path are where the bands are drawn: the scroll runs
first (its lift and relay carry the OLD set) and `fm_mset` draws the
difference after it, instead of 22.26's band-off before and band-on after.

A plain arrow in multi mode collapses to the focus first (`fm_mone`) and then
is 22.26's path unchanged.

**With nothing selected, Shift+arrow scrolls, as a plain arrow does** —
22.26's rule that the first click is what turns the keys into a cursor.

### 4.4 The rubber band (~140)

A press that is not on an entry, and leaves the press point by
`FM_DRAGMIN`, draws an XOR outline from the press to the pointer and selects
every entry it touches, live, through `fm_mset` once per tick. XOR outline
and XOR bands commute, so the outline needs no lift around the selection
changes inside it. It is `fm_drag`'s loop — `fm_dgwait`, the
unlock-yield-lock round trip, `fm_linger` — with a different outline and a
different drop, so the loop is shared through a proc word rather than copied
(the copy would be ~40 bytes more).

The routine that turns a rectangle into indices maps its two corners to
(row, column) — `fm_hit`'s own arithmetic, clamped instead of refused — and
fills the grid between them.

**The hard part is WHERE it can start**, and it is the thing to decide:

- **A list row spans the whole width** (SPEC.md 22: the band is x 0..cw−16),
  so in list view every point of every row IS an entry, and a folder long
  enough to fill the window has **no empty space at all** to start a band in.
- So the plan proposes that **in list view a press right of the name column
  is background for the purpose of STARTING a drag** — names are at most
  twelve characters at x = 24, so they end by x = 120, and the default window
  has 120..302 of row to the right of that. A press there that does NOT move
  is still an ordinary click on that row: `fm_dgwait` has already told the
  two apart before anything is decided. What changes is that dragging a file
  by its SIZE column now draws a band instead of moving the file. ~12 bytes.
- In the icon grid a band can start in the margin right of the last column
  and below the last row, as it can in Explorer's.

**Scope cut: it does not auto-scroll** past the top or bottom of the row
area. A band covers what is on the screen; Shift+click and Shift+arrows reach
the rest. That is the same kind of cut SPEC.md 22 recorded for the scroll
thumb before 13.10.5 built one.

### 4.5 Right-click (~15)

On a row inside the multi-selection it changes **nothing**, so the context
menu's Cut, Copy and Delete act on the set; on any other row it collapses to
that row (`fm_mone`) and then is SPEC.md 22.2.1's path.

### 4.6 Ctrl+A selects everything (~14)

`fm_mrange(0, FS_N-1)`. Ctrl+A arrives as 01h, which nothing in
`fm_onkey_x` takes. Not in the ask — it is here because, once `fm_mrange`
exists, it is one call. It is the first thing to drop if the bytes matter.

### 4.7 A plain click (~12)

In multi mode it collapses to the clicked row (or to nothing, off the rows)
through `fm_mone`, AFTER `fm_drag`, and joins today's double-click logic
unchanged. Collapsing is a diff, so it inverts the bands that go away and
leaves the clicked row's band alone if it was already on.

## 5. What a repaint costs

A full repaint draws one band per visible selected row instead of one — an
XOR over a row the painter has just lettered, which is the same double write
the single selection has always made (§22.2 draws its band after the rows).
A selection change is one band per row that CHANGED. Shift+click across
twenty visible rows is twenty bands; at PERFORMANCE.md's fixed part of a
drawing call (~0.76 ms, *not* a floor) that is ~20 ms against the ~120 ms of
the whole-window repaint it is not. `fm_mset`'s own loop is 64 tests at most,
~8,000 cycles, ~1.7 ms — and the bands it draws dominate it.

## 6. The operations

The rule for every operation: **bitmap empty → the path it has today; any
bit → the set.** So `fm_c_clip`, `fm_arm_sel` and `fm_drag`'s `.begin` each
gain one test (~6 each), and Rename, Open, Enter, Paste Into, Compress,
Uncompress and Open in New Window keep acting on the **focus**, which is what
they mean in every file manager with one name box.

### 6.1 Cut, Copy and Paste (~150)

**The clipboard must hold NAMES** (§1 item 6, and SPEC.md 22.3's own
argument): a Copy in a window, Up One Folder in that same window, Paste — the
commonest way the feature is used — re-lists the source window, and every
index into it is gone. Three ways to keep them were weighed:

| | for | against |
|---|---|---|
| **a heap claim of name records** — PROPOSED | robust; the claim is the size of what is in it, ~1KB at most; nothing outlives the clipboard | ~45 bytes of claim/free/ownership code |
| the system TEXT clipboard (SPEC.md 55) as the store | its claim, refusal and teardown rules already exist, and Note Pad would paste a file list | a Copy of files destroys copied text; a generation stamp is needed to know the clipboard is still ours; names would need parsing back. About the same bytes, with a side effect |
| bits + the source window, cleared when that window re-lists | ~40 bytes cheaper | **breaks Copy, Up, Paste in one window**, the commonest shape of the operation. Refused |

**The list is 16-byte records, one per entry**, copied straight out of the
window's cache: the 16-byte name field as it is, with the type in byte 15
(spare: names are ≤ 12 and a NUL, §1 item 10). 64 records is 1,024 bytes, so
**the claim is always exactly 1 KB** (`mem_claim` counts in KB) and nothing
has to count the set first. It is a new kernel tag (`MEM_K_FLIST`), and it is
declared **movable** with a proc that rewrites the one word that names it,
because a clipboard can stand for a whole session and an unmovable 1 KB in the
middle of the heap is HEAP-UNPIN-PLAN §2.0's wall in miniature.

- **Arming** (`fm_mlist` + the multi arm, ~65): free the old list, claim,
  walk the bitmap copying records, skip `..` (type 3, SPEC.md 19.5). Record 0
  is ALSO written into `fcp_cbname`/`fcp_cbtype` — so the clipboard's existing
  single record is the first entry, and Paste starts exactly as it does now.
  A set of one is armed by today's `fcp_arm` and claims nothing. **Arming is
  refused while `[fcp_busy]`** — a paste suspended on a question reads the
  list, and freeing it under it is the failure this guard is for. Today a
  Copy during a suspended paste is harmless, so the refusal is a (small)
  behaviour change and is stated as one.
- **Pasting** (~40): after `fcp_run` or `fcp_step` answers `FCPS_DONE`, a
  wrapper loads the next record into `fcp_name`/`fcp_type` and enters
  `fcp_run2` again, until the list runs out. It is one wrapper on the two
  bodies that can end an entry, `fcp_paste` and `fcp_answer`. `[fcp_all]` is
  not cleared between entries, so "A = replace all" means the whole paste;
  **Esc at a question stops the whole paste**; an error stops it and is said
  once (SPEC.md 59); `fcp_here`'s no-op (FERR_EXIST, the folder it came
  from) carries on to the next. `fm_paste_res`'s one `fmv_reload_all` and
  `fmv_repaint_all` then cover the whole operation, not each file.
- **A Cut's list is freed when its paste ends** (~10); a Copy's stays for
  the next Paste.
- `OSAPI_FILE_COPY` (SPEC.md 22.24) fills the operation record from
  registers and never reads the clipboard, so it never sees a list — the door
  zeroes the wrapper's count on entry and is otherwise untouched.

**Cost per entry**, stated rather than optimised: each entry still claims and
frees the copy buffer and pays `fcp_unbatch`'s sync, because `fcp_stop` ends
every entry. On a floppy the `int 13h` dominates by orders of magnitude;
holding the buffer across entries is a follow-on if a measurement says so.

### 6.2 Delete (~85)

The confirmation becomes `Delete 5 items? Del=yes Esc=no` —
`fm_onam` holds `"5 items"` instead of a name (`fm_utoa` exists), and
`[fm_odir]` is set when any entry of the set is a folder, so the existing
`+contents` form appears exactly when it should. The line's composer is not
touched.

The commit does **not** go through `fmv_sync_x` (§2): it `fcp_goto`s the
window's own `(FS_DRV, FS_CWD)`, raises `[dskw_batch]`, walks the bitmap
staging each name **out of the window's own cache** — which nothing in the
batch rewrites, so every index stays good for the whole walk — and calls
`dskw_delete_x` or `dskw_rmtree` by name, skipping `..`. It stops at the
first failure with the failure said once. Then `fcp_unbatch` and the paste's
own end, `fmv_reload_all` + `fmv_repaint_all`, which already re-list every
window the write could have changed. One deferred remount for the whole set,
where N single deletes would have paid N.

The Delete key arms it exactly as it arms one entry today, and the
asymmetric confirmation — the Delete key says yes, every other key no — is
untouched.

### 6.3 Dragging the set (~12, and ~8 more for a COPY)

A press on a row **inside** the multi-selection that becomes a drag arms the
multi Cut instead of the single one; `fm_dgdrop` then pastes it through
`fcp_paste` unchanged, and its existing *dropped where it came from* test
reads the clipboard's drive and folder, which the multi arm sets. A press on
a row outside the set drags that row alone, as now.

**Ctrl+drag = copy** is offered for ~8 bytes: `fm_drag` arms `FCP_COPY`
instead of `FCP_CUT` when Ctrl is down. `fm_drag`'s header gives the absence
of a modifier as the reason a drag is always a move, and §1 item 4 removes
that reason. It is not in the ask and is listed separately so it can be
refused on its own.

## 7. `kern_small`

**Gated out, byte-identical.** `FM_MSEL` is defined under `KERN_BIG` in
`kernel.asm` beside `OS88UI_SBDRAG`, and resolved there for that symbol's
recorded reason (fdlg.inc is included after files.inc, so a define made later
is not made yet). On `kern_small` the operations would live in `FILECP.DRV`,
an on-demand image that already needs the system disk for every Cut, Copy and
Paste, so the resident half would be paying for a gesture whose payoff is
one disk swap away — SPEC.md 22.26 drew the same line for the same reason.

## 8. The ABI question

**No ABI break, and none would make this smaller.**

- No package can see a Disk window's selection; the chooser's protocol
  (`FDH_*`, SPEC.md 38.9) is kernel-internal, and the chooser opts out (§4).
- The one thing a break could have bought is the modifier state carried in
  the click EVENT, sampled at the moment of the press rather than read at
  dispatch. That is more exact, and it costs bytes in the event queue, in
  every producer and in the record format for every package — to fix a race
  nobody can produce by hand (a Ctrl released in the few milliseconds
  between a press and its dispatch). **Refused.**
- An API *addition* — publishing the shift state, or the selection — is
  possible and not proposed: nothing outside the file manager needs either.

## 9. How it is built and measured

- **Waves land in order** and each is gated alone. Wave 1 is useful by itself
  only as a picture, so 1 and 3 are the merge unit; wave 2 can follow or not.
- **`make MSELOFF=1`** compiles the whole feature out, `SBDRAGOFF`'s shape:
  the A/B every byte figure here is replaced by, and `kernsize`'s sum against
  it is the number to quote. It also has to be **byte-identical** to the
  `f8ba8eb3` kernel (modulo the build number, SPEC.md 14.2 — compare at one
  commit), which is the proof that the single selection is untouched.
- **`kern_small` byte-identical**, by the same `cmp`.
- **A soak row, `tests/msel.py`**, on MartyPC through `tools/os88ui.py`:
  `Marty.key(name, down=True, up=False)` holds Ctrl or Shift across a click.
  It asserts on the window's `FS_MSEL` bytes read out of guest memory, not on
  pixels, for each gesture; then copies a three-entry set across to a scratch
  B:, pastes it into a folder, and reads the files back with
  `tools/os88fat.py`; then deletes a set and reads the directory back. It is
  broken on purpose against `MSELOFF=1` before it is trusted (WRITING-TESTS.md
  §1). `m.flicker()` measures a Shift+click across a screenful: the
  claim to test is that the rows that did not change have **zero** transient
  pixels.
- SPEC.md gains a new subsection of 22 (the next free number after 22.26) with wave 1, written before the code (CLAUDE.md).
  docs/HEAP-CLAIMS.md gains the `MEM_K_FLIST` row with wave 3.

## 10. For the owner to decide

1. **Scope.** All three waves at ~680, or the ~490 cut (Ctrl+click,
   Shift+click and the operations; no Shift+arrows, no Ctrl+A, no rubber
   band), or something between. The rubber band is ~140 on its own.
2. **Where a rubber band may start in list view** (§4.4) — the size column,
   which means a file can no longer be dragged by its size; or only the empty
   space under the last row, which a full folder does not have; or not at
   all.
3. **Ctrl+drag as a copy** (§6.3), ~8 bytes, not asked for.
4. **The clipboard store** (§6.1): its own 1 KB claim as proposed, or the
   system text clipboard, which would make *Copy* of files paste as a list of
   names in Note Pad and would also throw away whatever text was on it.

## 11. Traps written down before they bite

- **The bitmap and the screen must never disagree**, because every change is
  an XOR. Every path that clears the bits without drawing (`fmv_store`, a
  collapse) must be one whose caller repaints the window or has already taken
  the bands off; `fmv_sync_x` is the one that does neither, which is why it
  banks the bits (§3.2).
- **A multi-selection is correct to draw only on the FRONT window**, for
  SPEC.md 22.2's reason; every gesture here arrives through `W_ONCLICK` or
  `W_ONKEY`, which only the front window receives.
- **A delete must not re-list before it walks** (§6.2), or the bits it walks
  are gone; `fmv_sync_x` is the call that looks right there and is wrong.
- **Freeing the list while a paste is suspended** on a question is a read of
  freed heap with no symptom until the next claim lands on it (§6.1's busy
  refusal).
- **An EMPTY bitmap means `FS_SEL` IS the selection** (§3.1), so any path
  that can empty the set must leave `FS_SEL` naming a row whose band is on,
  or `0xFFFF`. A Ctrl+click that deselects the last entry, and a rubber band
  that ends touching nothing, both clear `FS_SEL`; otherwise the focus
  silently becomes a single selection with no band, and the next Delete acts
  on a row the user can see is not selected.
- **`fm_mnew` is scratch shared by every gesture** and by the scroll's lift
  and relay, so a gesture that scrolls must build its target AFTER the
  scroll, not before (§4.3).
