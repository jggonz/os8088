# The IN-WINDOW MENU as a shared element (`OS88UI_MENU`)

**Status: ALL THREE WAVES LANDED. Word's in-window menu is the shared
element.** SPEC.md 13.16 is the contract; Sheet is the open item (§1).
**And the element got SMALLER afterwards**: the three combos this file treated
as anchored lists are `os88ui_drop` records now (SPEC.md 68.2.3), so the
anchored path and its two record words are gone - the note under §3's record
table is the correction, and that table is the proposal rather than the tree.
SPEC.md 13.16 is the contract for what exists. SPEC.md 13.14.4 is the entry point;
this is the arithmetic behind it and the questions it cannot answer from the
outside.

## 0. Where it came from

Word was asked whether it could use `os88ui_drop` (SPEC.md 13.14), CLEAR SKIES'
drop-down, for its ribbon combos. Taken literally the answer is no, and the
first version of 13.14.4 said so on bytes alone — which was the wrong argument
about the wrong unit. `apps/os88ui.inc`'s own header had already settled the
first half:

> Saving bytes was never the argument … The argument is that a feature added
> to the button lands in the Standard File dialog, the Control Panel, the
> Timer AND every package at once, because there is one body rather than ten
> that agree by hand. SPEC.md 47's greying rule was fixed FIVE separate times
> in this tree, each as its own bug.

And the second half is that the unit Word shares is not a list. It is a menu.

## 1. What is actually duplicated today

Three independent implementations of one control:

| | code | table | keyboard | save-under |
|---|---:|---:|---|---|
| `apps/word` (`wd_m*`) | 3,486 | 96 | yes (~375 b) | **yes** (§68.2.1) |
| `apps/sheet` (`sh_m*`) | 1,382 | 54 | no | **no** |
| `kernel/menu.inc` | — | — | yes | §12.4's own |

Word's and Sheet's are measured by symbol span, to the next GLOBAL label, on
the shipped source. The kernel's is not comparable: `menu.inc` also owns the
desktop bar, the Apple menu, the clock and the app-menu protocol (§12, §13.10),
so only part of it is the same control.

**Sheet's is a hand copy of Word's.** `sh_mtrack`'s comment says so outright —
*"word.asm's `wd_mtrack` pattern"* — and it shows in what the copy did not
take, because §68.2.1 landed afterwards: **`sh_mclose` sets two bytes and calls
`sh_repaint`**, which white-fills `[sh_ox],[sh_oy]` to the content's full width
and height and draws it again. That is exactly the shape Word measured at
**521.4 ms** and replaced with a blit at **19.7**. Sheet's own figure is NOT
measured here — two attempts to bracket it failed to land a click on its bar
and the run was abandoned rather than guessed at; the source is what says it,
and the source is unambiguous.

## 2. What would move, and what would not

Word's 3,486 bytes split cleanly:

| | bytes |
|---|---:|
| **the CONTROL** — `wd_mbar`, `wd_mtxor`, `wd_mtitler`, `wd_mgeo`, `wd_mdraw`, `wd_mfind`, `wd_mhl`, `wd_mbarhit`, `wd_mopenm`, `wd_mclose`, `wd_mtrack`, `wd_mclick_open`, `wd_minrect`, `wd_mfire`, `wd_mgeti`, `wd_mitemp`, `wd_subank`, `wd_surest` | **1,895** |
| **WORD'S OWN** — `wd_mact`, `wd_mchk`, `wd_mrepair`, `wd_mroute`, `wd_mkey`, `wd_mstep`, `wd_suab`, `wd_sudlg` (what a pick MEANS, the greying predicates, key routing, the repair) | 1,591 |

So the trade inverts against the one 13.14.4 first evaluated:

* adopting `os88ui_drop` alone: Word **deletes ~150, adds 1,779**;
* `OS88UI_MENU`: Word **deletes ~1,895**, Sheet **deletes ~1,382**, and both
  add one element.

Whether Word breaks even depends on the element's size, and **nobody knows
that until it is built** — an element serving two looks is normally bigger
than either. What does not depend on it: Sheet is missing a fix Word has, and
a shared body is how it stops being missing.

## 3. What the element would have to carry

* **The bar**: titles laid out from a table, the highlight, the hit test.
  Word's bar is Word 1.1a's and Sheet's is its own; the geometry is already a
  pointer in this file's idiom (a 4-word rect), so the difference is table
  data rather than code — **to be proved, not assumed**.
* **The drop**: geometry, the cells, the check marks, the disabled treatment
  (§47, and the whole reason the file exists).
* **The gesture**: `wd_mtrack` and `sh_mtrack` are the same tight
  `OSAPI_MOUSE` poll with an unlock/yield/relock between reads, and neither
  uses `W_ONDRAG`. They already agree; they just agree twice.
* **The bank**: §68.2.1 and §13.14.1 are the same trade published twice
  already. This is the third and it should be the last.
* **The keyboard**, `%ifdef`-gated. Word has Alt+mnemonic traversal
  (`wd_mkey`, `wd_mstep`); Sheet has none, and must not pay for it. That is
  the file's own idiom — `OS88UI_DROP`, `OS88UI_CHK`, `OS88UI_BARONLY`.

## 3.5 THE SEAM, measured

Question 1 was *"is the bar difference really data?"*, and with Sheet tabled it
becomes the narrower and answerable *"how tightly is Word's control half bound
to Word?"*. Counted over the eighteen routines of the control half, on the
shipped source:

* **22 Word globals** touched, and they group into one record with nothing left
  over:

  | | |
  |---|---|
  | `wd_cl` `wd_ct` `wd_cw` `wd_ch` | the CONTENT rect |
  | `wd_mrx1` `wd_mry1` `wd_mrx2` `wd_mry2` | the open panel's rect |
  | `wd_surx1` `wd_sury1` `wd_surx2` `wd_sury2` `wd_suseg` `wd_sukb` | the bank |
  | `wd_mopen` `wd_mhi` `wd_mink` | which menu, which item, what ink |
  | `wd_max` `wd_may` `wd_mabox` | a combo's anchor, and the gesture's (the first two are DELETED - see the note under the record) |
  | `wd_win` `wd_win1` | the window |

* **4 calls out**, and only one of them is a hook the element would need:
  `wd_mact` (what a pick MEANS — stays in Word, the element returns the pick),
  `wd_mrepair` (the caller's repaint, which is already `os88ui_drclose`'s
  `CF = 1` contract), `wd_selpace` (the unlock/yield/relock the tracking poll
  uses — generic, moves with the gesture), and **`wd_mchk`**, which answers
  *is this item checked, and is it enabled* live. That one is the callback.

**So it is parameterisation, not a rewrite.** The seam is clean because Word
already followed §22's `fm_hit` discipline — geometry is banked in one place
and the painter, the hit test and the close all read those same words — which
is the discipline `os88ui.inc` states in its own header. Two files arrived at
it independently, which is the best evidence that the shape is right.

### 3.5.1 The record

```
OS88UI_MN_RECT    0   ; 4 words: the CONTENT rect {x1,y1,x2,y2}, screen -
                      ; the caller's painter fills it, a window moves
OS88UI_MN_TAB     8   ; the menu table: N rows of MN_ROW bytes
OS88UI_MN_N      10   ; menus ON THE BAR (rows past it were anchored lists -
                      ; RETIRED, see the note under this table)
OS88UI_MN_BAR    12   ; the bar's ONE string - Word draws all nine titles as
                      ; a single opaque run and the table indexes into it
OS88UI_MN_WIN    14   ; the window, for the clip
OS88UI_MN_OPEN   16   ; byte: which menu is down, or 0FFh
OS88UI_MN_HOT    17   ; byte: which item is lit
OS88UI_MN_INK    18   ; byte: the ink its runs letter in
OS88UI_MN_MRECT  20   ; 4 words: the open panel
OS88UI_MN_ABOX   28   ; 4 words: the gesture's anchor - the title's own box
OS88UI_MN_AX     36   ; word } where an ANCHORED list hangs (a ribbon combo):
OS88UI_MN_AY     38   ; word } its box's left edge and the strip's bottom
OS88UI_MN_SEG    40   ; word } the banked pixels under the open panel
OS88UI_MN_KB     42   ; word }
OS88UI_MN_CHK    44   ; near ptr: AL = item -> CF/flags. The ONE hook
OS88UI_MN_SIZE   46
```

**THE ANCHORED-LIST ROWS ARE GONE, and the record is smaller than the plan
that designed it.** The three things this file called anchored lists - Word's
Font, Pts and Style combos - were never menus: no bar cell, no mnemonic, no
separator, no greying, one column of strings and a pick that is remembered.
Sharing the menu is what made that visible and what made it affordable to act
on: with `os88ui_bhit` already in the build the drop-down costs 996 bytes
rather than 1,779, and a `wd_mtab` row stops being free the moment the
anchored path exists only for it. SPEC.md 68.2.3 is that conversion.

So `MN_AX`/`MN_AY`, `os88ui_mngeo`'s `.combo` branch and the SLIDE built for
an eleven-item face list are all deleted, `os88ui_drfit` doing the same
arithmetic; `OS88UI_MN_SIZE` shipped at 62 with the bank, the check hook, the
open hook and the repaint hook added, and is **58** now. Read the layout in
`apps/os88ui.inc`, not here: this table is what the wave plan proposed.

## 4. Open questions, in the order that decides the work

1. ~~Is the bar difference really data?~~ **ANSWERED for Word by 3.5** — 22
   globals into one record and one hook. Sheet is TABLED (the owner's call,
   and the right one: two shipped packages at once was question 4). The
   element must not be *shaped* so as to block it, which costs nothing here
   because Sheet does strictly less.
2. **What does the kernel do?** `os88ui.inc` is ONE SOURCE FOR TWO WORLDS and
   already assembles into `.cold` with `OS88UI_KERNEL`. If the element can
   serve `menu_draw_bar`/`menu_drop` too, the third copy goes and the
   argument is settled; if it cannot, say why here.
3. **Does Word's save-under generalise?** `wd_subank` takes the rect and the
   window; `os88ui_drbank` takes a record. One of the two shapes wins.
4. **Two shipped packages change at once.** This is not a Word branch's work
   and should not ride on one.

## 5. What is NOT proposed

Converting a *skinned* control. The header's own exclusion stands: ModPlug's
bevelled well and Minesweeper's cells are intended design, not duplication.
Sheet's and Word's bars are the kernel's pull-down drawn twice, which is the
opposite case.

## 6. The waves

**W1 — LANDED.** The record (`OS88UI_MN_*`, 54 bytes), Word's sixteen fields
aliased onto it, the two table accessors, and `os88ui_mngeo`. Word's `wd_mgeo`
is an eight-byte shim that loads `BP` and calls it. Proved by an A/B: all nine
menus' panel rects, on the build before the move and after, **identical**.
Gated by `tests/wdmenusu.py`'s new leg, which checks every rect against
`wd_mtab` rather than against a remembered one.

Cost so far **+83 bytes** of Word, and that is the shape of an unfinished
conversion rather than the answer: the element carries `mngeo` and two
accessors that only `mngeo` uses, while Word still carries every other routine.
The accessors will not have a second customer until W2, and the shim goes when
its callers do.

**W2 — LANDED.** `mnbar`, `mntxor`, `mntitler`, `mndraw`, `mnfind`, `mnhl`,
`mnbarhit`, `mninrect`. Three things needed a hand and SPEC.md 13.16.3 has
them: the content WIDTH against a rect that carries edges, `OS88UI_MN_BBUF`
for the truncated bar, and `OS88UI_MN_CHK`. Proved by the same A/B one level
up — all nine menus' drawn PANELS pixel-identical, which is every byte
`mndraw`, `mnbar` and `mngeo` produce between them.

**W3 — LANDED.** `mnopen`, `mnclose`, `mnbank`, `mnback`, `mnpace`,
`mntrack`, `mnclickopen`, and the two hooks SPEC.md 13.16.4 describes. It took
two attempts; the first was reverted, and all three of its defects were the
same kind of thing — **a contract the lifted body had been keeping by
accident**:

1. `os88ui_mnopen` inherited `wd_mopenm`'s `push cx`/`si`/`di`, which were
   there for a string copy. The copy moved to `OPENH` and the pushes went with
   it, so the handler returned with **`SI` clobbered** and `ui_task` armed the
   release to it. One click worked and nothing after it did.
2. `os88ui_mnclose`'s `CF` was not enough, because the element closes menus
   itself — the repaint became the `RPNTH` hook.
3. The Window menu's composition had to be a hook and not a caller
   pre-step, because the gesture opens menus too.

`wd_mfire` and `wd_mact` are not in the element and never were: the wave list
had them wrong.

## 7. What the conversion actually costs, measured

**+226 bytes of Word after W2**, and W3 as attempted took it to **+300** with
every dead shim deleted. It does not break even, and the reason is
mechanical rather than a mistake to find: the record lives in DS and the
element addresses it as `[ds:bp+n]`, which is a segment override and a
displacement where Word's own `[wd_xxx]` was one direct 16-bit address. That
is about a byte per access, and the control half has hundreds.

So the honest position is the one `apps/os88ui.inc`'s header takes: **saving
bytes was never the argument.** What the conversion buys is one body instead
of two, for a control the tree already has two of — and the payoff is not in
Word at all, it is that **Sheet can then delete ~1,382 bytes and stop
repainting its whole content on every menu close**. Across the two packages
that is about -1,080 and one fewer body; in Word alone it is +300 and a
standard control. Both numbers should be quoted, and the second one is the
one that decides.
