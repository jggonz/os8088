# CTRL-GLYPH-PLAN.md — the Control Panel draws nothing of its own

**Status: BUILT, and the census is CLOSED (§6.4).** Set by the owner after
`docs/plans/completed/GFX-EMBEDDABLE-PLAN.md` §8.8 turned up the two shared
controls disagreeing, and it is that section's follow-on made into work with a
wider scope than §8.8 had.

> we'll make a UI radio button if one doesn't exist, and convert control panel
> so NO ui element it has is drawn from hand rolled unique code — buying us
> expensive kernel space in exchange for cheaper disk and control panel space

**That last clause is the whole trade and it is the opposite of what §8.8's
summary implied.** `KERN_BUDGET` bytes are the scarce ones — every machine
carries them for ever, resident, and docs/KERNEL-MEMORY.md's ladder bills them
in 512-byte rungs. A package image is disk, read once, compressed on the way
(docs/plans/O88-COMPRESSION-PLAN.md), and present only while the program runs.
So a byte that moves from `.cold` into a shared UI routine is not a wash even
when the byte count is flat: it moved from the expensive account to the cheap
one.

### What it came to, before you read the rest

**The brief expected to find controls that wanted naming and found a rule
being broken instead.** Twenty-one call sites was the count of *primitives
named*; the count of *calls made* was thirteen, in six routines, and §6.4 is
the verdict: three were already right, one is **refused** (`cp_drv_arrow1` is
not the same widget as `OS88_SCROLL`, §6.2), and the three that were wrong
were wrong **the same way** — a pane, a band and a line that each **blanked
before they drew**. §13.14.6 is one rule and the Control Panel broke it three
ways.

**No shared control was invented.** `cp_listrow`, `cp_time_fld`'s third mode
and `cp_drv_line` are local and each is smaller than a shared routine plus its
record. The one place a shared control *was* the answer — the glyph — was
already shared and only needed its body changed (§13.15.1), and the radio
(§13.17) is the sixth shared element.

**What it cost: +130 bytes of `CTRL.DRV`**, an on-demand module (§2.8), so
none of it is resident. What it bought is in
`docs/reports/GLYPH-AND-LINE-COST-2026-09-10.md`, which is §4's measurement
brief answered.

**§3 item 3 is ANSWERED by the build, not left open.** `os88ui_glyph` is
inside `%ifndef OS88UI_NOBTN`, so converting its BODY converted every carrier
that emits it, at their next build, with no per-package work at all — the
measurement counted **116 bytes a copy out of eleven packages and five
drivers** in this build alone, and more in the tree these disks do not carry
(`WORD`, `CWORD`, `WEAVE`, `LOOM`, `PACCMAN`, `C64`, `FROTZ`). That is where
the **−1,407 bytes of app disk** came from; it is the brief's own trade paying
out.

What is genuinely new rather than converted is the **radio** (§13.17):
`os88ui_rad` is a control that did not exist, so no package draws one yet —
`tests/radtest/` is its only caller outside the panel. Nothing is pending for
anybody; the control is simply available.

---

## 1. What the Control Panel draws today

`kernel/ctrl.inc`, counted:

| through | calls |
|---|---:|
| `os88ui_krect` (the shared pane rect) | 21 |
| `os88ui_glyph` / `_glyph_f` (the 12×12 bitmap control) | 15 |
| `os88ui_btn` / `_btn_f` | 6 |
| `os88ui_arm` / `_fire` / `_armed` and their far entries | 6 |
| **`gfx_fill` direct** | **10** |
| **`font_str` direct** | **5** |
| **`font_run` direct** | 3 |
| **`gfx_frame` / `gfx_vline` / `gfx_ink` direct** | 3 |

**The 21 direct kernel drawing calls are the "hand rolled unique code" the
brief is about**, and the 15 glyphs are the *shared* routine that happens to be
drawn the wrong way. Two different problems in one file:

1. **The glyph is shared but bitmap-drawn** — §8.8's subject, and it is a
   REMOVAL (see §2).
2. **Twenty-one call sites draw straight at the kernel** — panes, separators,
   labels, the theme swatches. Some of those are legitimately one-off; some are
   a control nobody named. **The census is owed before the work.**

The five direct `font_str` are their own flag: §6.6's registry has them with a
reason, but a *shared* labelled control would draw its own label with
`font_run` and remove the question.

## 2. The glyph: what comes OUT

**There is no radio control in `apps/os88ui.inc`.** The file has exactly two
control drawers:

- `os88ui_chk` (§13.15, `%define OS88UI_CHK`, opted into by `apps/skies` alone)
  — a **record-based** control at BX that draws its own ground, frame, mark
  **and label**. Its mark is a solid square, and its own comment carries the
  reason: *"which reads on one bit as a tick does not"* (§39.4).
- `os88ui_glyph` — a bare 12×12 at CX/DX, four bitmaps, a masked-sprite pass
  and a 44–64-call per-pixel fallback. It draws BOTH kinds, and the Control
  Panel letters its own labels beside it.

So the radio has to be **made**, and `os88ui_chk` is **not a drop-in** for the
Control Panel's call sites — different signature, and it owns a label the panel
already draws itself.

Measured on the current kernel (`.cold`, `kern_big`), per copy:

| symbol | bytes | fate |
|---|---:|---|
| `os88ui_glyph` | 244 | most goes — the dither compose, the record staging, `UI_ICON` and `.gpix` are all *put a bitmap on screen* |
| `os88ui_grec` + `_grec_d` | 50 | **goes** |
| four 12×12 bitmaps | 96 | **goes** |
| `os88ui_gdn` | 20 | stays — a fill-drawn glyph still asks "is it pressed?" |
| `os88ui_glyph_f` | 4 | stays — the far entry |
| **block today** | **414** | **146 an outright delete** before the body is touched |

**23 copies**: 22 packages and drivers carry the block, 10 opt out with
`OS88UI_NOBTN`, plus the kernel's `.cold`.

**The radio must convert with the check box.** The panel is **ten radio rows
against three check rows** — Display's three, Sound's two, Scheduling's two,
the adapter row, the desktop row and `cp_dngly`'s shared arm are radios; the
checks are the 12-hour clock, the menu-bar seconds and one driver row. A
check-only pass touches under a quarter of the panel and leaves two mark styles
on one page, which is worse than doing nothing.

**And §47's greying gets simpler, not just smaller.** `os88ui_glyph` composes
the disabled grey **row by row** with a screen-absolute parity term, because in
its own words *"on a 1bpp adapter a grey is a 50% STIPPLE that a mask pass has
nowhere to put"*. A fill takes its grey from the pen, so all of that becomes
`stc` / `UI_PEN` — which is what `os88ui_chk` already does.

**The radio's look is the open question and it is the owner's.** §39.4 says a
ring is dotted on 1bpp, so the radio is already the glyph that renders worst; a
diamond, a smaller square, a frame with a different inset are all candidates and
none of them is a measurement.

## 3. What is NOT settled

1. ~~**The 21 direct calls have not been classified.**~~ **DONE — §6.**
2. **`os88ui_chk`'s record shape against `os88ui_glyph`'s registers.** The
   panel holds the pane's left edge in DI at every call site and carries the id
   in BH through `cp_dngly` precisely so DI stays free — a record-based control
   changes that, and the ~35–50 ms per glyph (PERFORMANCE.md Part 2) says a
   redraw must stay per-ROW and never per-page.
3. ~~**Whether the other 22 carriers want the same change.**~~ **ANSWERED —
   it was never a decision.** `os88ui_glyph` sits inside `%ifndef
   OS88UI_NOBTN`, so the body change reached every carrier that emits it
   automatically: **116 bytes a copy off eleven packages and five drivers**,
   measured. The radio is the part that is new, and a package gets one only
   by calling `os88ui_rad`, which none does yet.

## 4. The measurement this is done to produce

Set by the owner, and the reason a couple of conversions land before the rest:

> * How many bytes came out of kernel?
> * How many bytes went into each app? (ram usage and compressed disk size increase)
> * How much performance was gained/lost in the converted calls?

Three columns, and each has a right instrument here rather than an estimate:

| question | how |
|---|---|
| **bytes out of the kernel** | `tools/kernsize.py` — quote the **sum** and the **accrued** line, never the step count (CLAUDE.md's rung rule). §10's per-symbol map is what attributes it to a routine |
| **bytes into each app — RAM** | `tools/os88pkgsize.py` for the image, and the package's `.bss` beside it. RAM is image + bss while the program runs, and **nothing while it does not** — which is the half of the trade that makes it cheap |
| **bytes into each app — DISK** | the **compressed** `.o88`, not the image: `PKGZ ?= lz4` means the file on the floppy is not what nasm emitted, and `os88pkg.image_unwrap` is what tells the two apart (docs/plans/O88-COMPRESSION-PLAN.md). A fill-drawn mark is *code*, where four bitmaps are *data that compresses well* — so the disk delta will not track the image delta and must be read rather than derived |
| **performance, the converted calls** | MartyPC exec-breakpoint brackets, guest cycles entry to exit, the way PERFORMANCE.md Set 141 was taken. The bar is `os88ui_glyph`'s own **44 set bits for an empty box and 64 for a crossed one, one drawing call each** — ~35–50 ms on the field machine. A fill-drawn check is 3 calls and a radio is not yet designed |

### 4.2 TAKEN, and the answer is in `docs/reports/GLYPH-AND-LINE-COST-2026-09-10.md`

**The measurement is done and this section's own expectation was wrong.** §2
priced the conversion as *"what it COSTS is calls - eight for a set radio
against the sprite pass's one"*; measured on a Hercules 5150, **three of the
four kinds got FASTER** - check-clear −30.9%, radio-clear −24.9%, check-set
−11.4% - and only the set radio is dearer, by **+6.5% / +0.39 ms**. VGA agrees
in shape. The masked sprite pass is one drawing call and composes twelve
mask-and-data rows to make it, so five fills beat it and eight roughly tie.

**The bar in the table below was also wrong twice over** and both are corrected
in the report: *"~35-50 ms on the field machine"* was `.gpix`'s arithmetic -
the CLIPPED fallback - and the normal path measured **6.0-6.2 ms** before and
**4.3-6.4 ms** after. Five comments in `kernel/ctrl.inc` and two in
`apps/os88ui.inc` carried that figure and now carry the measured one.

**And the report found something nobody asked for**: the fill-drawn radio is
**not the shape the bitmap drew** - a square with its corners nipped, against a
circle - and 13.17.1's *corners must be clear* rule is too weak to tell them
apart, so `tests/radio.py` passes on either. That is a look question with a
price attached (report section 3.3: ~7 ms for a rounder outline, ~11 for the circle,
against today's 4.5) and **nothing has been changed on the strength of it**.

`tests/glyphbn` is the instrument and `tests/glyphcost.py` the row - both
implementations in ONE package, so the A/B is one kernel and one boot and the
`gfx_line` family this arc removed from that kernel cannot get into the answer.

**A drawn-once control is not priced like a hot loop**, and the owner has said
so: *"between 1ms and 2ms is not huge — this is not a live drawing, it's drawn
once then it sits there until they interact with it. So we can pick the best
form, not simply the most performant."* The performance column is there to catch
a **regression**, not to choose the design.

## 4.1 OWED, and the owner has asked for it: `os88ui_chk` breaks the same two rules

`os88ui_rad` was written wrong by copying `os88ui_chk`, so naming the rules
(§13.14.6) is only half the fix — **the routine that gets copied still does it
the other way**, and the next author will copy it too.

- **Rule 1.** `os88ui_chk` does `UI_WHITE` + `UI_FILL` of its **whole rect** and
  then draws the frame, the mark and the label over it: ~4 ms of blank row on a
  4.77 MHz 8088, every repaint. Fix: drop the ground fill (the caller owns the
  pane, §13.14's contract), let the opaque `font_run` carry the label's ground,
  and always draw the mark area — ink when on, ground when off — so it
  self-clears.
- **Rule 2.** `os88ui_chkhit` calls `os88ui_chk` to redraw the **whole control**
  on a toggle, re-lettering a label that did not change. Fix: an
  `os88ui_chkmark` twin of `os88ui_raddot` — the mark area and nothing else.

**Callers to check before it lands**: `apps/skies` is the only `OS88UI_CHK`
opt-in today, and a pane relying on the control to clear its ground needs its
own fill. The gate is `tests/radio.py`'s shape — every `gfx_fill`'s rect and
every `font_run_x`'s y across a toggle — **verified red first**.

## 6. The census — every direct drawing call in `ctrl.inc`, classified

Thirteen call sites, in six routines. The count in §1 is of *primitives named*;
this is of *calls made*, and it is the one that decides work.

| routine | sites | what it draws | verdict |
|---|---:|---|---|
| `cp_page` | 1 | the page pane's ground | **KEEP — this is the caller's job.** §13.14.6 says a control does not lay its own ground precisely so the pane can |
| `cp_run` | 1 | a `font_run` wrapper | **KEEP.** It is already the shared spelling, and opaque |
| `cp_divider` | 1 | the 1px rule between the two panes | **KEEP — genuinely one-off.** `gfx_vline` is already the primitive; wrapping one call in a name is not consolidation |
| `cp_list` | 3 | **the category LIST BOX** — rows with the selected one inverted | **A CONTROL.** And it breaks §13.14.6 rule 1 today, in its own words: *"Erases the whole pane first, so this doubles as the redraw path when the selection moves"* — the exact defect §13.17.4 fixed in the radio, a pane-wide blank to move one highlight |
| `cp_time_fld` (+`cp_time_rows`) | 3 | **the time/date SPIN FIELD** — a value with the selected one barred | **FIXED (§31.5.3)** — moving the caret erased the whole date-and-time band to take one box off one field. A field draws its own box in the ground now, and a selection is two fields |
| `cp_drv_arrow1` / `_arrfill` / `_tri` | 3 | **scroll arrows and a triangle** for the driver list | ~~A CONTROL THAT ALREADY EXISTS~~ — **REFUSED, and this row was WRONG. See §6.2** |
| ~~`cp_drv_wipe`~~ → `cp_drv_line` | 1 | a variable-length text line | **FIXED (§31.9.3)** — the run goes first and only the TAIL it did not cover is cleared. The rule-1 smell was right |

### 6.1 What the census actually found

**Three of the six are already right**, which is worth saying before the work is
sized: the pane ground, the `font_run` wrapper and the divider are not
hand-rolled controls, they are a caller doing its job and one genuine one-off.
The brief's *"no UI element drawn from hand rolled unique code"* is **ten sites
in three routines**, not twenty-one.

~~**And the sharpest one needs no design at all.**~~ **That claim was wrong and
§6.2 is what checking it found.** The driver list's arrows and `OS88UI_SCROLL`
are not one widget.

**Two of the three carry §13.14.6 defects into the bargain.** `cp_list` blanks
its whole pane to move a highlight and says so in its own comment; `cp_drv_wipe`
exists to blank a line. Converting them is not only consolidation — it is the
same fix §13.17.4 made, applied where a user actually sees it.

### 6.2 The driver page's arrows are REFUSED — three mismatches, and the code said so

§6's first version called this *"the clearest hit in the census"* on the
strength of the words *scroll arrows* and the existence of `OS88UI_SCROLL`.
Reading either widget's contract kills it, and **the driver page had the answer
written down in its own source**:

> NO THUMB and no track: a thumb reports a POSITION IN A LIST, and a list of
> five rows showing four has one bit of position in it that the two arrows'
> greying already carries (§47 rule 1 — disabled is a FACT here, not a guess).

Three ways they are not the same control:

| | driver page | `os88ui_sbar` |
|---|---|---|
| thumb and track | **none, deliberately** | a 50% grey track and a proportional thumb |
| state | per-arrow **greying at the end stops** (§47 rule 1) and a **held/down inversion** (§13.8.3) | `UI_BLACK` throughout — neither |
| the glyph | 4 rows, widths 1·3·5·7 | 5 rows, widths 1·3·5·7·9 |

…and structurally, two separate framed squares with a gap against one
continuous frame with two `UI_HLINE` rules.

**So the conversion is not a call-site change, it is a new widget**: an
arrows-only mode, per-arrow disable, a per-arrow down state and separate cell
frames, added to a routine **five other callers** already scroll with
(`files.inc`, `fdlg.inc`, Note Pad, Frotz, Artful). That is well past the
~500-byte budget it was offered, and it would put a look question through every
scroll bar in the OS to save ~250 bytes in one module.

**The reusable lesson is docs/plans/completed/GFX-EMBEDDABLE-PLAN.md 9.1's, one
more time.** *Read what a thing REFUSES,
not what its name suggests.* A census matched on a NAME twice in this file now —
`gfx_line`'s callers, and this — and both times the refusal was already written
in the source being classified.

**`os88ui.inc`'s own header names this exact failure**: *"WHAT IT IS NOT FOR. A
skinned control… converting those would be undoing intended design, not
consolidating it."* A widget that deliberately drops a thumb is in that class.

### 6.3 …so the next one to take is `cp_list`, which has a DEFECT and not just a shape

It blanks its whole pane to move one highlight and says so in its own comment,
which is §13.14.6 rule 1 in the Control Panel where a user sees it — the same
fix §13.17.4 and §13.15.2 have now made twice. It needs no new shared control
and no look decision.

### 6.4 The census is CLOSED, and what it came to

| routine | verdict |
|---|---|
| `cp_page`, `cp_run`, `cp_divider` | **already right** — a caller laying its pane's ground, the shared opaque-run wrapper, and one genuine one-off |
| `cp_list` | **fixed**, §31.1.4 — a selection redraws two rows, not the pane |
| `cp_time_fld` | **fixed**, §31.5.3 — a caret move redraws two fields, not the band |
| `cp_drv_wipe` → `cp_drv_line` | **fixed**, §31.9.3 — the run first, the tail after |
| `cp_drv_arrow1` / `_arrfill` / `_tri` | **refused**, §6.2 — not the same widget as `OS88_SCROLL` |

**Three of the six were already right and one is refused, so the work was
three routines.** The brief read as twenty-one call sites; it was ten, in three
routines, and every one of them turned out to be the *same defect* rather than
a missing shared control: **something blanked before it drew.**

**That is the finding worth keeping.** The census was written expecting to find
controls that wanted naming — a list box, a spin field, a scroll bar. What it
actually found is that the Control Panel's hand-rolled code is fine *as code*
and was wrong about *when to erase*, three times, in three different shapes:
a pane, a band, and a line. §13.14.6 is one rule and it was broken three ways.

**No shared control was invented.** `cp_listrow`, `cp_time_fld`'s third mode
and `cp_drv_line` are all local, and each is smaller than a shared routine plus
its record would have been. The one place a shared control *was* the answer —
the glyph — was already shared and only needed its body changed (§13.15.1).

**Total: +130 bytes of `CTRL.DRV`** — 6,102 → 6,178 for the list and → 6,232
for the other two. None of it is resident: `CTRL.DRV` is an on-demand module
(§2.8), read into a heap claim when the panel opens and given back when it
closes, so this is 130 bytes of a floppy rather than of every machine's RAM.

## 5. Sequencing

1. Census the 21 direct calls (§3 item 1) — this is reading, and it decides the
   size of everything after it.
2. Design the radio's look. Owner's call; §39.4 is the constraint.
3. Build the fill-drawn body in `os88ui_glyph`'s existing signature, both kinds,
   behind the existing `%ifndef OS88UI_NOBTN`.
4. Convert the Control Panel's 15 glyph sites, land it, measure §4's four rows.
5. Then the direct calls the census named.
