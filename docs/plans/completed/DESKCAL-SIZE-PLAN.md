# DESKCAL size plan

**BUILT - A, B and D; C left out on the measurement; E refused by the
owner.** SPEC.md 110.6's size-pass table is the ledger and SPEC.md 110.3 /
110.5.2 / 110.7 the contract; this file is the design record. `DESKCAL.O88`
went **18,976 -> 13,680 bytes of image (-27.9%) and 16,171 -> 12,535 on
disk (16 -> 13 clusters)**, against this plan's own A-D estimate of
14.6-15.5 KB. Read *What happened* below before the tiers - three of them
landed somewhere other than where they were priced.

Its original framing follows. It was built on
docs/reports/DESKCAL-SIZE-2026-10-16.md, which has the per-routine
figures. **Owner's brief**: functionality may change if the core look and
features survive.

## What happened

- **A** measured -1,344 and shipped as priced, with two changes the owner
  asked for or the build forced. The greyed items' *why* twins went as well
  (the owner: "just not show them"): one `MENU_DIS` byte in front of each
  greyable string makes the greyed form `pointer - 1`, and Show Tasks is
  dropped from the menu where both lists show (-240). The damaged-file
  answer was first a RENAME to `DESKCAL.BAD`, and `deskcaledit` B showed
  that copy's three clusters were exactly what the 360 KB system disk's
  second save needed - so it is a DELETE before the write now
  (`STIK_DMGREPLACE`), which is the old Yes without the question.
- **The small build** was the owner's suggestion and is a tier this plan
  did not have: `STIK_DESKONLY` takes the titled fallback and the worker arm
  out of kern_big's build (-336), and `make small` ships the `-DAPP_SMALL`
  build that keeps them (SPEC.md 110.7). On kern_big a refused QUERY now
  refuses the launch, said (`stiknodesk`).
- **B was worth more than estimated**: -2,996 against -1,500 to -2,200,
  from four agents in parallel worktrees, one per file group, each held to
  the cross-file contracts. Two of them (`dcdraw.inc`; `dcdate`/`dcdata`/
  `dcrem`) checked their code against the old on a host 8086 emulator
  (unicorn) before anything booted - 0 mismatches across ~123,000 compared
  states; the other two were assembled and read, and the `deskcal*`,
  `stik*` and desk-probe rows are their gate. The register convention was the smaller half;
  folding single-caller routines, shared epilogues reached by `jmp`, table
  dispatch for keys and card roles, and shared string tails were the rest.
- **D** shipped as priced (-352 with `OS88UI_MACROS`, which lets a package
  carry `os88line.inc` without any `os88ui.inc` control).
- **C was not built.** B's `dcdraw.inc` pass had already shared the grid,
  slot and dispatch arithmetic (`dc_hgrid`, `dc_hslot`, `dc_rolej`), and
  what is left of the hit tests - ~700 bytes - is special cases rather
  than repeated layout: the open field, COMPACT's page, the pagers, a label
  whose width is its text's and a number whose width is its digits' times
  its scale. A table would need an escape for each, for 100-200 bytes at
  real risk to every press.
- **E** the owner refused.
- **The defect this found**: on the shipped 360 KB system disk the
  calendar went READ-ONLY after its first save - a replace claims its new
  chain before freeing the old, and 6 free clusters held SYSTEM.CFG and
  one copy of the three-cluster state file but not two. It was red on
  `elendilon-next` in four rows; three clusters of this pass fixed it.

## 0. What it is worth

A byte of image is roughly 0.85 of a byte on disk, because the image is 90%
code and code barely compresses. The file is 16 clusters on the 360 KB
system disk, which stands at 343 of 354. So about **1.2 KB of image is one
cluster back**.

| tier | what | image (est.) | visible? |
|---|---|---:|---|
| A | the yes/no alert out: Delete becomes a press-twice, and a damaged file is replaced by the first save | **-1,250** (the cut alone is MEASURED at -1,344; ~+90 back for what replaces it) | yes, slightly |
| B | an internal register convention and a size pass, calendar and `os88stik.inc` | -1,500 to -2,200 | no |
| C | one geometry walk for both draw and hit test | -400 to -600 | no |
| D | About as a toast | -330 | yes, slightly |
| E | the tag always a handle (no hover); no press-lighting on buttons | -550 to -650 | yes: the owner's call |
| | **A-D** | **~-3.5 to -4.4 KB** | |
| | **A-E** | **~-4.0 to -5.0 KB** | |

A-D lands at ~14.6-15.5 KB of image and ~12.5-13.2 KB on disk: **3 clusters
back**. Adding E makes it 4. Only A's figure is measured. The rest are
estimates from the per-routine table, and each stage measures its own
result.

## 1. Tier A: drop the alert (MEASURED)

The alert and its button kit (`OS88UI_ALERT`, which pulls in the
`os88ui_btn*` code) cost **1,238 bytes** for exactly two questions:

- *Delete this appointment/task?* (`dc_askdel` / `dc_delans`).
- *Replace damaged calendar?* (`os88stik.inc`'s `STIK_DAMAGEDQ` /
  `stik_dans`).

A throwaway build was made with `%define OS88UI_NOBTN` in place of
`OS88UI_ALERT`, no `STIK_DAMAGEDQ`, and the ask call stubbed. It assembled
to **17,632 bytes, -1,344**. The About card still assembles without the
button kit.

What replaces each question:

- **Delete: press twice.** The first Del (or *Delete Item*) arms it and
  toasts `Del again to delete` through `OSAPI_TOAST`. That call already
  backs `dc_toast`. A second Del on the same selection deletes. Any other
  key, press, selection change or beat disarms. That is one byte of
  state, two compares and a 20-byte string, so ~+50 bytes.
  `dc_delans`'s delete body stays as it is. The `dc_ask` flag stays too:
  it is what `dc_busy` and `dc_tick` read. Only the window it guarded
  goes.
- **Damaged file: set it aside.** On a damaged load the include toasts
  `Damaged calendar kept as DESKCAL.BAD`. At the first save it calls
  `OSAPI_FILE_RENAME` on the old file inside `stik_enter`'s walk, then
  writes fresh. Today *No* makes the session read-only, and *Yes*
  overwrites the file. This version loses nothing and asks nothing, for
  ~+40 bytes. The `STIK_S_DAMAGED` branch in `stik_save` becomes the
  rename.

**Tests that drive the questions** and so must change: `deskcaledit`,
`deskcalremind`, `deskcalsnooze` and `stiksmall`. SPEC.md 110.3 and 110.5.2
also change: the question is what they document. `tests/stikprob` still
has to assemble (the `t_stik` fast row).

## 2. Tier B: the register convention and a size pass

The calendar's sources hold **862 `push`/`pop` instructions** and the
include another **420**, so about 1,280 bytes. Nearly every routine is
written *"every register kept"*. But a window callback may clobber AX-DI
and ES (SPEC.md 13; SPEC.md 49.8 is a size pass doing exactly this), and
most of these routines are internal with one to three callers. What was
done to Calc (apps size pass 1, -24%) and in SPEC.md 49.8 (6,280 -> 3,943 bytes) applies:

1. **The callbacks save, the helpers do not.** Do this per routine and
   document it in the routine's header as *"clobbers ..."*. Only the
   kernel-facing callbacks keep a save, and only for what the kernel reads
   back after them (W_PAINT's SI; check each against SPEC.md 13). Estimate: half
   the pushes, so ~600 bytes.
2. **The hooks between `os88stik.inc` and the calendar** are
   `STIK_ONCARD`, `STIK_COMPOSE`, `STIK_PAINTCARD` and the rest. Each is a
   "keeps every register" contract on both sides. Re-contract them as a
   set, in the include's header. `stikprob` is the other side of that
   contract and moves with it.
3. **The usual idioms.** Many geometry bytes are read with
   `mov al, [di+X]` / `xor ah, ah` or `and si, 0FFh`. The pattern
   `mov cl, 3` / `shl` repeats. Several frame setups recompute
   `[dc_geo]` / `[stik_li]` tests that the caller already did. Tails can
   share an epilogue through `jmp`, which costs no stack (the kernel size
   passes' finding).
4. **Do not touch the bss offsets** (`DC_BSS_*`, `stik_*` and their
   `tools/os88stik.py` mirrors). The tests read the guest at those
   offsets, and moving them is churn that buys nothing.

Gate: every `deskcal*` and `stik*` soak row. Run `deskcalperf` first and
last, because its call counts and milliseconds are SPEC.md 110.6's
budgets, and a size pass may not make a select or a paint slower.

## 3. Tier C: one geometry walk for both draw and hit test

Hit testing is **999 bytes**. That is `dc_hit`, `dc_hbtn`, `dc_hmon` 303,
`dc_hday`, `dc_hagenda`, `dc_htask`, `dc_hrem` and `stik_hit`. Every card
is laid out three times by hand: to draw it (`dc_cmon` 325), to map a
press back (`dc_hmon` 303), and to light a button. The proposal is one
routine per card that walks its elements, each with a rect from
`dc_geos`, an action id and a mode byte.

- **Draw mode** composes the element if it meets the band's rows. That
  test is what `dc_vis` does today.
- **Hit mode** stops at the first rect that contains the press and
  answers `dc_hk` / `dc_hi` / `dc_hb` and the button rect `dc_hbtn` wants.

The press-lighting rect then falls out of the same answer.

**The risk is speed, not bytes.** The composer is already 1.5-2.5x v1's
time (SPEC.md 110.6's two regressions), and a mode test inside the draw
loop must not add per-glyph work. Keep the test at the ELEMENT level and
never per glyph. The month grid stays arithmetic in both modes: 42 days by
division, as `dc_hmon` does now. Measure with `deskcalperf` before
deciding. If the draw path gets slower, take only the button and line
lists and leave the month card as it is.

## 4. Tier D: About as a toast

The About card costs ~330 bytes: `os88ui_about_d` 175 and `os88ui_about`,
`dc_about`, `dc_abdismiss` 78, `dc_ablines` and its strings, and the
`[dc_abon]` tests in the paint and the tick. The replacement is one toast:
`Desk Calendar - SPEC.md 110`, ~+15 bytes. It keeps the
`OSAPI_ABOUT_SET` handler (docs/INDEX.md, 12.2), so the menu item exists
and answers. What goes is the card drawn on the face and its take-down
redraw (6 calls in `deskcalperf`).

## 5. Tier E: look changes (the owner's call)

- **The tag is always the handle.** Today the tag has three states:
  REST plate, FOCUS inverse plate, and HOVER handle with a close box and
  a grip (SPEC.md 110.11). It reveals after `DOCK_HOVER_T` and hides
  after `DOCK_LEAVE_T`. Drawing the handle all the time removes the dwell
  and linger timers (`stik_hvtick` 106, `stik_hdue`/`stik_hset`, their
  share of `stik_arm` and `stik_wake`) and the plate arm of
  `stik_tagdraw`. Focus could become the handle's name inverted or
  underlined. Estimate: -350 to -400. The cost is the look: a close box
  is permanently on the desk. A Macintosh desk accessory had exactly
  that, but SPEC.md 110.11 chose otherwise.
- **No press-lighting on card buttons.** `dc_litinv` 101, `dc_lpdone` 58,
  and the lit-state halves of `dc_oncardup` and `stik_mup`. A button
  would act on release without inverting while held. Estimate: -200 to
  -250. That is the cost of losing SPEC.md 13.5's press-and-slide-off
  feedback on the cards, which is why it is here and not in Tier B.

## 6. Considered and NOT recommended

- **A smaller payload** (`DCP_MAXAPPT` 48 / `DCP_MAXTASK` 32). That is
  2,456 bytes of bss and a 5-sector file, and it is the biggest single
  memory item. But it is a file-format version, and 48 appointments
  across all dates is already modest. It is not on the disk image.
- **Dropping MID or COMPACT.** Hercules (the owner's 5150) and CGA would
  lose their layouts, and the tables are only 174 bytes. The cost is in
  the branches, and Tiers B and C reach those.
- **A slimmer composer straight path** (`gfxe_glyph`'s four copies, about
  120 bytes). Those copies are what the composer's speed was bought with
  (SPEC.md 5.12.8), and select and paint are already the regressions.
- **The greyed menu items' *why* strings** (about 170 bytes). SPEC.md 47
  is a system convention, not a calendar choice.
- **The titled fallback** (`stik_tgap` and friends). The calendar ships
  on the small system disk too, and that is what the fallback serves
  (110.7).

## 7. Order

1. **A** first: it is measured, independent and the largest.
2. **B**, routine by routine, with the deskcal rows after each file.
3. **C** behind a `deskcalperf` A/B.
4. **D**.
5. **E** only on the owner's word.

Each stage writes its measured image, bss and file into SPEC.md 110.6's
package table. That table is the ledger.
