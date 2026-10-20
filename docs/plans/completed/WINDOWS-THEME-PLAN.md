# WINDOWS-THEME-PLAN.md — a "Windows 95" theme

**STATUS: COMPLETED AT PHASE 1, which shipped (SPEC.md 76.14 is the contract).
Phases 2, 3 and 4 are NOT BUILT and nobody is working on them** - the owner
shipped phase 1 alone for the first release, the rest being good-to-have
rather than needed (SPEC.md 76.14.12). They are kept here, costed, so that
wanting one is a matter of moving this file back to `docs/plans/` and picking
it up rather than starting again. Every owner question is answered (§0.1).
This file is the investigation behind the plan, written on `windows-theme`
off `elendilon` at `d65b91e9`, 2026-10-12; §10 is what the build found and
where it departed from what is below, and §11 is what was left open.

- **MEASURED** figures come from the build at that commit: `kernsize[big]`
  reports footprint 97,792 of `KERN_BUDGET` 100,864, which is 3,072 spare (6
  steps), and `.text`+`.bss` of 48,524 leaves 17,012 of `KERN_CODE_MAX`.
- **ESTIMATE** figures are marked as such where they are used. Every one of
  them is a count of instructions or calls, not a listing.
- §9 lists what has to be measured before any of this is taken.

## 0. The ask, and how this file reads it

> *"a 'windows 95 like' theme. This would be color - but also moving the close
> button to the right side of the titlebar, re-theming it to an X, adding a
> maximize button, changing dropdown menus to open on click and activate (or
> close, if its not on them) on another click. And anything else that is 'mac
> like' in the general, not-per-app interface. The chip menu and menu bar can
> stay as they are, changing that might be a stretch goal but not phase 1 (the
> menus should still click and hold open there)."*
>
> *"This could take one of two forms: A completely separate build, or an on
> demand module/driver when the user picks it in the Theme page."*

**One reading has to be stated, because the plan depends on it.** The plan
reads *"the menus should still click and hold open there"* as follows: the
chip menu and the menu bar keep their LOOK and their PLACE in phase 1 (no
Start button, no per-window menu bars), but the pull-downs they drop **also**
open on a click and stay open.

The plan does not lean on that reading as hard as it seems to. The click-open
mode proposed in §3.3 is a **superset** of today's press-drag-release:

- a release ON an item still fires it, exactly as now;
- only a release that lands on NO item leaves the menu up.

So applying it to the bar costs no Mac habit and breaks no test.

**The owner confirmed this reading** (§0.1): the bar's pull-downs open on a
click too in phase 1, and only the bar's look and place wait for phase 4.

### 0.1 The owner's answers, 2026-10-12

Every question this file asked is answered, and each answer is folded into
the section it changes. This table is the record, so a later reader can tell
a decision from a default.

| | question | answer | where it lands |
|---|---|---|---|
| R | §0's reading of the bar | **Confirmed**: the bar and the chip menu keep their look and place, and their pull-downs go click-open like every other menu | §3.3 |
| D1 | Separate build or module | **Module**, picked on the Theme page | §1 |
| D2 | Offered on Hercules / CGA | **Yes**, on every `kern_big` adapter, with a 1bpp pen row | §3.8 |
| D3 | The Theme page has no room for a fourth row | **Move the Screen Saver button** to the right-hand column | §7 |
| Q1 | The 1bpp desktop: dither or black | **Prototype BOTH**, and the owner picks on the glass - **picked: the DITHER** (2026-10-13) - black first, reversed after two more viewers found the title bars read better on the dither; the knob is deleted | §3.8 |
| Q2 | Buttons on unfocused windows | **Always drawn.** Keep a fallback in case the speed actually feels bad. A click on any window's button **acts at once**, as Windows does — no raise-first | §3.1, §3.7 |
| Q3 | A click-opened menu freezes the machine | **Accepted for phase 1.** Ending the freeze is **its own phase** (phase 3, §5), ahead of the furniture | §3.3, §5 |
| Q4 | The module cannot load at boot | **Come up in Color** (Bright on 1bpp, where Color is refused). A missing or corrupt file means bigger problems, so `DOCK.DRV`'s rule is fine | §1.3 |
| Q5 | The keyboard in an open menu | **Esc, Up/Down and Enter** in phase 1. Left/Right needs the bar to slide, so it goes with the furniture (phase 4) | §3.3 |
| S1 | Navy | **VGA's standard blue (0,0,170) stays.** A consistent palette beats a matching app look, and the Video Player and others assume the standard 16 | §3 |
| S2 | Bold caption | **Not now.** Perhaps once the band composer is downsized and in | §3.1 |
| S3 | The XOR menu highlight | **Fine.** This is a general clone of the look, not a pixel-perfect one | §3.6 |
| S4 | A bevel inside today's border, not a 4 px one | **Fine**, for the same reason | §3.4 |
| S5 | The default theme | **Bright, unchanged** | §8 |

**The principle behind S3 and S4 binds the rest of the work:** the target is
the general look of Windows 95, on this machine's palette and geometry. It is
not pixel parity. A choice that buys fidelity at the cost of an ABI change,
a palette write or a new class of redraw is the wrong one.

## 1. The answer to "separate build or module": a MODULE, `WIN95.DRV`

**Recommendation: an on-demand module on `kern_big`.**

- Shape: it is `DOCK.DRV`'s shape exactly (SPEC.md 30.5). The kernel keeps a
  small far-pointer table whose values at rest are the kernel's own bodies.
- Load: the Theme page's fourth radio `mod_need`s the module, and the module's
  `hook` entry takes the table over. Any other theme unhooks it and
  `mod_drop`s it.
- `kern_small` has no theme at all (SPEC.md 76.13) and stays exactly as it is.

### 1.1 Why not a separate build

`kern_emu` (SPEC.md 9.11.7) is the precedent for a third kernel. Its reason
does not carry over here, and its costs all would.

**What justified `kern_emu` was that the XT cannot run its code at all.** The
VMware backdoor is 32-bit, so the honest cost of it on an 8088 is zero bytes.
A Windows look is 8086 code, and every `kern_big` machine can draw it. So the
build would only be buying "no resident bytes for people who never pick it".
§2 prices that at roughly 150-200 bytes (ESTIMATE). That is not worth a
kernel.

**What a fourth kernel costs, from what `kern_emu` has already cost:**

- its own `build/<x>k/` tree;
- its own system disks in all four geometries;
- its own module set, because `MOD_STAMP` differs per build (that is
  `KMODDIR`'s trap in the Makefile);
- its own blessed `kernsize` baseline and a `budget-check` arm;
- an entry in `os88sym`'s `_SHIPPED_DEFS`;
- every gate that walks "every shipped image" learning a new list.

The `vmmouse` row *"died twice in one session"* on prerequisites alone
(Makefile, the `vmmousetest` note).

**It also cannot do what the ask describes second.** The ask was for the user
to pick it *in the Theme page*. A separate build is picked by booting a
different floppy.

**The one thing a separate build would buy is geometry, and it is not free.**
A separate build could change `TITLE_H` or the frame width without vectoring
anything. But `TITLE_H` = 18 and `MBAR_H` = 20 are in `apps/os88api.inc` and
are compiled into **42 package sources** (28 files use `TITLE_H`, 23 use
`MBAR_H`). Every package would then need a second build per kernel, which is
the SDK forking.

§3.1 shows the geometry does not need to change: Windows 95's own caption is
18 px at 640x480. So the one advantage is not wanted.

### 1.2 Why the module qualifies (CLAUDE.md's test, ONDEMAND-PLAN §1)

**"The system disk is already required to use it."** The pick happens on the
Control Panel's Theme page. The page is itself `CTRL.DRV` module code, loaded
off the system disk when the panel opened. The same page's Screen Saver button
already `mod_need`s `SAVER.DRV` in place and toasts a refusal (SPEC.md 79.7),
so this is the in-place precedent on the very page.

**At boot it loads where `DOCK.DRV` and `EXTD.DRV` load.** The settings reader
calls `thm_set` beside `dkf_apply` and `exf_sync` (`kernel/driver.inc`
~3816-3829).

- The boot volume IS the system disk at that moment.
- The image lands at the heap ceiling, which is benign.

### 1.3 What the module costs that a resident theme does not

- **A heap wall when it is picked mid-session.** HEAP-UNPIN-PLAN §2.0: a
  module image is claimed top-down with its base as a CS. One loaded while
  packages are open lands at whatever depth the heap has then, and stands
  there until it is dropped.
  - A theme is normally picked once and then carried by `SYSTEM.CFG`, so the
    steady state is the boot-time load at the ceiling.
  - Recorded, not solved: it is the same wall `DOCK.DRV` and `EXTD.DRV`
    already make.
- **A refusal path.** If the system disk is absent at boot, the module cannot
  load. The fallback is `DOCK.DRV`'s: *"live controls describe the fallback"*,
  meaning `[thm_kind]` becomes Color on a colour adapter and Bright on 1bpp,
  with no toast.
  - From the panel, the refusal is a toast and the radio does not move.
  - **Decided (Q4): this stands.** The boot volume is the disk the module is
    on, so the only ways to reach this path are a missing or corrupt file or
    one copied from a different build. A machine in that state has bigger
    problems than its theme.
- **Image RAM while the theme is on.** About 2 KB of claim (ESTIMATE, §2.2).
  This comes out of the heap and not the kernel, and only on machines that
  chose it.

## 2. Where the bytes go

### 2.1 Resident (`kern_big` only; `kern_small` +0)

Every row is an ESTIMATE against a measured comparable. The comparables are
`dkv` (`kernel/dock.inc:352`, 14 slots, each operation a 4-byte
`jmp far [dkv+i]`) and `dkf_apply`.

| item | bytes | comparable |
|---|---:|---|
| `thv`: 6 far pointers at rest on the kernel bodies (§2.3) | 24 | `dkv`, 4 a slot |
| 6 `jmp far [thv+i]` stubs (the old entry names) | 24 | `DKCALL` |
| `mod_tab` row + `'WIN95.DRV'` + the 2-entry `mod_fp` block | ~22 | §2.8.1: 4 + name + 4/entry |
| `thm_set`'s kind-3 arm: `mod_need` / hook, or unhook / `mod_drop` (`.cold`) | ~45 | `dkf_apply` |
| `thm_tab` row 3 (the shared 8 pens) | 8 | §76.12's row |
| `ui.inc .mup`: region 5 → `wm_zoom` under `ui_lcall` | ~12 | `.mup_min` |
| `ui.inc .mdown`: a box on a NON-front window arms rather than raises (§3.1, Q2) | ~10 | `.chrome` |
| `menu_drop`: one far call at `.up` to the sticky phase (§3.3) | ~8 | — |
| **total** | **~153** | |

Against 3,072 bytes of `KERN_BUDGET` spare, this is resident.

- The stubs are in `.text` and the arm is in `.cold`. **`.cold` is resident
  too** (CLAUDE.md), so the whole ~153 is billed.
- Per the rung rule, the number to quote at the build is `kernsize`'s sum and
  not a step count.

### 2.2 The module image (`WIN95.DRV`, not resident)

| part | ESTIMATE |
|---|---:|
| hook / unhook (the table both ways, the extended pens) | ~100 |
| title bar: ground, left caption, three bevelled buttons and their glyphs | ~700 |
| the title-row hit test (four regions) and the lit-box rect | ~120 |
| frame bevel inside today's outline + shadow (§3.4) | ~150 |
| size grip (§3.5) | ~90 |
| sticky menu phase (§3.3): hover without a button, the second click, Esc, draining the queue | ~300 |
| pull-down face and etched separators (§3.6) | ~150 |
| **total** | **~1.6 KB** → a 2 KB claim |

### 2.3 The six hook points

These are the **only** places the kernel's chrome needs a door. Each is a
routine that already exists, renamed to its basic body, with a
`jmp far [thv+i]` standing at the old name.

1. **`wm_draw_title`** (`wm.inc:7535`). The whole bar. The hook sits ABOVE
   §11.101's `wm_title_band` dispatch, so a `BAND=1` kernel's composer is
   bypassed under the theme rather than taught it. `BAND` is a knob, and
   nothing shipped compiles `band.inc`.
2. **The title-row arm of `wm_hit`** (`wm.inc:4600-4627`). It answers
   AL = 1 title, 2 close, 3 minimize and a **new 5, maximize**. Only the
   title-row branch is vectored, so the content, grow-box and z-order walk
   stay resident and unchanged.
   - **The door must stand ABOVE `test bp, bp / jz .title`**
     (`wm.inc:4602`). That test is what makes the boxes frontmost-only, and
     Q2's answer is that every window's buttons are live.
   - `wm_hit` is reached from `ui_task` and from `cur_shape_pass`, which is
     the UI ladder and not the mouse ISR (`mouse.inc:4840`). So a far call
     there never lands in an interrupt, which was the one place `EXTD.DRV`
     refused to go.
3. **`wm_chrome_rect_x`** (`wm.inc:7945`). The rect a held box lights. It is
   the third copy of the box geometry, and today it knows AH = 2/3 only.
4. **The outline + shadow in `wm_draw_win`** (`wm.inc:10627`, `wm_draw_shadow`
   at 10565).
5. **`wm_grow_paint`** (`wm.inc:10997`). The grow box's RECT stays the
   kernel's, so hit testing is unchanged.
6. **The sticky phase of `menu_drop`** (§3.3). At rest this is a `retf` with
   CF = 0, meaning "released: close as today".

**The cost on the hot path is one far indirect jump.** `DOCK.DRV` measured it
at **42 cycles** (SPEC.md 30.5). That is set against a title bar priced at
~40 ms of primitive calls on the 5150 (GFX-REWORK-PLAN, 40.0 ms for the
fifteen-call bar). With the module unhooked, every theme draws exactly what it
draws today.

**§76.9's rule holds and is the first gate:** Bright with the table at rest
must be pixel-identical to the kernel before the table existed.

## 3. Phase 1 — the kernel's chrome

What Windows 95 looks like here, in the 16 standard indices only. That is all
an EGA or VGA palette has, and §76.12's argument for Color holds here too.

| surface | pens |
|---|---|
| desktop | `CTEAL` (3) |
| 3D face | `CLGRAY` (7), lit edge `CWHITE`, shadow `CDGRAY` (8), dark edge `CBLACK` |
| title bar, focused | `CBLUE` (1) ground, `CWHITE` caption, no pinstripes |
| title bar, unfocused | `CDGRAY` ground, `CLGRAY` caption |
| menu bar, dock | `CLGRAY` ground, `CBLACK` ink (Color's pens already) |
| pull-down | `CLGRAY` face, `CBLACK` ink, `CDGRAY` disabled |
| content | unchanged — white paper, black ink (§76's boundary) |

`CBLUE` is 0,0,170 against Windows' 0,0,128. **Decided (S1): that is close
enough, and it stays.** A real navy needs a DAC write, and §76.12 never writes
one for a reason that goes beyond the theme: a consistent palette across the
machine is worth more than an exact match, and packages such as the Video
Player assume the standard 16.

**The 8-byte live block keeps its shape.** Row 3 of `thm_tab` holds the shared
pens (`bg`=`CLGRAY`, `ink`=`CBLACK`, `dis`=`CDGRAY`, `tbg`=`CBLUE`,
`tink`=`CWHITE`, `desk`=`CTEAL`, `strp`=0). The pens only the module's painters
read live in the module's own image. MODULE-SELFCONTAIN-PLAN establishes that
a module may write its own image. That covers the face, lit, shadow, dark and
unfocused-caption pens.

### 3.1 The title bar, and why its height does not change

`TITLE_H` is 18, and so is Windows 95's caption at 640x480. **The bar keeps
its height, so every package's content rect is untouched.** That is the
property that makes this a theme rather than an ABI change.

Inside the 18 rows, left to right:

- **Caption.** Left-aligned at `W_X+4` instead of centred. It is a
  `font_run` on the bar's ground, which is one opaque pass (§6.1).
  - Windows 95's caption is bold. A second transparent pass is §6.6's
    double-draw and a new `textsites` line, so phase 1 is NOT bold.
  - **Decided (S2): not now.** A bold glyph needs the band composer
    (§3.7). It is worth revisiting once that composer is downsized and
    shipped; until then the caption is the ordinary face.
- **Buttons.** Three 16x14 bevelled buttons at `y+2`, right-aligned:
  - **minimize** `_`, an hline;
  - **maximize** `□`, a frame plus a thick top rule, and `⧉` (restore) when
    zoomed;
  - a 2 px gap;
  - **close** `X`.
  - All three are drawn on **every** window, focused or not, as Windows does
    (**decided, Q2**). Today the boxes are frontmost-only (`cmp di, bp`), so
    this is a cost the Mac look never paid. §3.7 prices it and keeps a
    fallback.
  - **A click on any window's button acts at once** (Q2). Today a press on
    a window that is not in front raises it and nothing else
    (`ui.inc .mdown` → `ui_raise`). Under the theme, a press on a
    button ARMS it the way a front window's box is armed today, and the
    release fires it. **The window is not raised first**:
    - a raise is a repaint of the whole window, and closing it straight
      afterwards repaints what was under it, so raise-then-close pays twice
      on the 5150 for one click;
    - the cost is that the held-box light (`ui_lit_on`'s XOR,
      `wm_chrome_rect_x`) can now land on a title bar that another window
      partly covers. The light is drawn through that window's clip region
      (§11.3), which is the same machinery a background repaint already
      uses;
    - if clipping the light proves awkward in practice, the fallback is to
      raise on the press and fire on the release. That is still one click,
      just a slower one. §9 M5 decides.
    - **Close and minimize** act without a raise. **Maximize raises as part
      of its action**, as Windows does: a window grown to fill the desktop
      underneath the others would show almost nothing of itself, and
      `wm_zoom` repaints the window anyway, so the raise rides a repaint
      that was already owed.
- **The X.** It is two diagonals. `gfx_line` is a refusing stub in every
  build (GFX-EMBEDDABLE-PLAN 5.12.7), so the X is one `gfx_points` call: a
  table of ~16 points, inked through the existing slot. It is not 16
  `gfx_pixel` calls, which would be 16 call floors.
- **The hit regions follow the drawing**, through hook 2.
  - **Close is at the RIGHT.**
  - **A press on the old left close position is a title drag.** That is the
    safety property worth stating: muscle memory cannot close a window by
    accident under the new layout, because that position no longer closes
    anything.

### 3.2 Maximize already exists — this is a button, not a feature

`wm_zoom` (SPEC.md 11.95, `wm.inc:2869`) is a complete maximize/restore:

- the desktop band from `MBAR_H` to above the dock;
- the user rect banked in `W_ZOOMR`;
- both directions negotiated through `W_ONSIZE`.

Today it is reachable only by double-clicking the title bar of a `WF_SIZABLE`
window (`ui_tdbl`, `ui.inc:1415`), which is also Windows 95's double-click
behaviour.

**So region 5 is one branch in `ui.inc`'s release path.** Today
`.mup_min` (`ui.inc:309`) assumes "not close means minimize". It needs one
`cmp ah, 5` that sends 5 to `wm_zoom` through the same `ui_lcall` the
double-click uses.

**A non-sizable window draws its maximize button GREYED** and refuses the
click. Windows does exactly this, and §47 rule 3 is the same rule: grey a
fact (`WF_SIZABLE`), never a guess.

The button's glyph is a function of the record: zoomed means `W_ZOOMR` is
armed (§11.95 derives the state, it stores no flag). So there is **no new
window state**, no `W_*` field and no `WF_*` bit.

### 3.3 Menus: click-open as a superset

**The kernel already has ONE tracker.** `menu_track` (the bar and the chip
menu) and `menu_popup` (the Disk window's context menu, the dock tile menu,
the desktop shortcut menu and the Control Panel's Floppy drop-downs) all end
in `menu_drop` (`menu.inc:1928`). So one change reaches every kernel
drop-down at once.

**The package side already did this**, and it is the model:

- `os88ui_mntrack` / `os88ui_mnclickopen` (`apps/os88ui.inc:5878-5985`,
  SPEC.md 13.16.4): a release back on the title leaves the menu STICKY, and
  the next click fires the item, closes the menu, or slides to another title.
- Word ships it (*"both period styles at once"*, `word.asm:17683`).
- `tests/wdmenusu.py` is its gate and the template for this one.
- `OS88UI_DROP` (`os88ui_drup`, 4815-4837) is the same idea for a drop-down
  list.

**The rule, kernel-side.** `menu_drop .poll` runs exactly as today. At `.up`,
with the module hooked (hook 6):

- **Release on an item** → fire it. This is today's path, so press-drag-release
  is untouched and `tools/os88ui.py`'s `menu_pick` keeps working under every
  theme.
- **Release on no item** (on the title, in the frame, or outside), on the
  FIRST release only → **stay open**. Then:
  - **Hover with no button**: the highlight follows `menu_hover`. That is the
    same `menu_item_xor` pair the held loop uses.
  - **Next press on an item** → fire it on that press's release.
  - **Next press off every item** → close, and consume that click (below).
    In Windows a click outside a menu dismisses it and does nothing else.
  - **Keyboard (decided, Q5):** **Esc** closes, **Up/Down** step the
    highlight, and **Enter** fires the highlighted item. Left/Right to the
    neighbouring menu needs the bar to slide between titles, so it goes with
    the furniture (phase 4, §6).

Three things the sticky phase owes, each found in the code rather than
guessed:

1. **The pointer.** `gfx_lock` sets `[cur_lazy]` and the ISR freezes the arrow
   for the whole hold (`vga12.inc:4646`). The comment at `menu.inc:23` says
   *"the cursor stays hidden - the highlight is the feedback"*.
   - That is right for a held drag. A click-click user is AIMING, and needs
     the arrow.
   - SPEC.md 7.4 already solved *"the arrow tracks while we hold the lock"*
     for disk transfers (`[cur_barok]`, `cur_unlazy`, re-arming
     `[cur_lazy]` afterwards). The sticky phase applies that pattern around
     each `menu_item_xor`: hide the arrow, XOR, let it track again.
   - This is the largest single risk in phase 1 and the first thing to build
     and measure (§9 M3).
2. **The queue.** The held loop reads `[mouse_btn]` levels and ignores the
   queue: its own `EVT_MUP` is dropped later because `[ui_armw]` = 0
   (`ui.inc:250`).
   - A second click would leave its `EVT_MDOWN` in the ring, and `ui_task`
     would then deliver the dismissing click to whatever window is under it.
   - The sticky phase must drain mouse records with `evq_mup` / `evq_drain`
     (`events.inc:195-233`), owing `wm_wake_eaten` for any `EVT_WAKE` it
     passes over. It also must not stamp `[ui_click_t]`, or the dismissing
     click becomes half of a double-click.
3. **The save-under lives longer.** `MEM_P_MSAVE` is TRIVIAL-purgeable for the
   menu's life (SPEC.md 12.4.2). A sticky menu stays up for seconds rather
   than a drag, but **the lock is held throughout** (this design keeps the
   whole interaction inside `menu_drop`'s one hold), so nothing can claim
   against it.
   - The consequence is that the machine is frozen while a menu is open, as it
     is today during a held drag (§12.8.3: *the freeze IS a gfx-lock hold*).
   - The clock does not tick and background windows do not draw, which is
     what Windows 3.x did too.
   - **The alternative** is the package style: release the lock between
     passes, as Word does with its `W_ONCLICK` state machine. That makes the
     save-under stale the first time a background painter draws over it
     (`os88ui.inc:4205` documents that exact risk).
   - **Decided (Q3): the freeze is accepted for phase 1.** It is today's
     behaviour stretched rather than a new one. Ending it is phase 3 (§5),
     its own investigation, ahead of the furniture.

**Keyboard inside the phase.** `kbm_pollm` eats every unclaimed key today
(`mouse.inc:3137`). The sticky loop reads Esc, Up, Down and Enter before it
does. On a machine driving the pointer from the keyboard, the arrows already
step by item through §9.6.3's `menu_kbnav` while `[menu_dropd]` is set, so
the two agree on what an arrow means inside a menu. The module code is
~100-150 bytes of IMAGE (ESTIMATE), which costs no `KERN_BUDGET`.

**What the bar keeps:** its look, its place, the chip logo, and sliding NOT
switching menus (`.poll` hit-tests items only). Windows slides across titles
while a menu is open. That is a bar change, and phase 4 (§6).

### 3.4 The window frame: a bevel inside the geometry that exists

Windows 95 has a 4 px 3D border and no drop shadow. os8088 has a 1 px outline
and a 1 px shadow at (+1,+1). The shadow is built into the damage and overlap
arithmetic: `wm_win_rect`, `wm_dmg_shadowed`, `wm_ovl` and `wm_dock_snap` all
know the extra pixel.

**Phase 1 does not change the geometry.** It recolours the two pixels the
window already owns:

- the outline's **top and left** in `CWHITE`;
- its **bottom and right** in `CDGRAY`;
- the shadow L in `CBLACK`.

That is a 2 px raised bevel on the bottom-right and 1 px on the top-left. It
reads as Windows' raised edge, and the damage arithmetic sees the same
rectangle it always did. A true 4 px border moves every content origin, so it
is an ABI question and refused here. **Decided (S4)**: a general likeness is
the target, not the same pixels.

On a window that is flush with a screen edge (`wm_flush`, §11.95.2), the side
borders stay absent, exactly as today.

### 3.5 The grow box becomes a size grip

`wm_grow_rect` (13x13 at the bottom-right) keeps its rect and its hit test.
Hook 5 paints Windows' three diagonal ridges in it instead of two overlapping
squares, as one `gfx_points` call per pen (two pens, lit and shadow).

### 3.6 The pull-down

- **Face, ink and greying.** `menu_drop` already draws its ground and frame
  through `thm_pen_live` / `thm_pen_dis` (§76.6). Row 3's pens make the face
  light grey with black ink and `CDGRAY` greying, with no new code.
- **Raised frame.** The frame becomes a raised bevel (the module paints over
  the 1 px frame after the fill: two lines lit, two shadowed).
- **Etched separators.** `menu_s_msep`'s greyed hyphens become a 2-line etched
  rule.
- **The highlight stays XOR in phase 1.** On a 4-plane adapter
  `vga_xor_fill_vram` XORs the index with 15, which maps the item as follows:
  - `CLGRAY` (7) → `CDGRAY` (8);
  - `CBLACK` text → `CWHITE`.

  The highlighted item is therefore a dark-grey bar with white text. That is
  not Windows' navy, but it is legible and needs no drawn state.
  **Decided (S3): this stays.**
- **A navy highlight** needs the item redrawn rather than inverted. That is
  phase 2 (§4), along with the Disk window's selection bar, which is the same
  question.

### 3.7 The cost on the target machine, which is the real constraint

PERFORMANCE.md's sentence applies: *a redraw is priced by how many primitive
calls it makes.*

- **Today's bar.** About fifteen calls: the ground, six stripes, two boxes at
  two calls each, the caption, the gap fills and the rule. Measured at
  **40.0 ms** on the 5150.
- **Phase 1's bar, drawn naively** (ESTIMATE):
  - the ground and the caption;
  - three buttons, each a face fill plus four bevel lines plus a glyph, so
    ~6 calls each;
  - the outline bevel.

  That is **~25 calls ≈ 65-70 ms** a bar. Drawn on every window rather than
  only the front one, a full repaint with five windows pays it five times.

**That is a regression the module must not ship unmeasured.** The design has
three answers, in order of preference:

1. **One `gfx_blit4` for all three buttons.** They are a fixed 52x14 picture
   (four variants: focused/unfocused × zoomed/not). The module carries them
   as 4bpp art and paints them in one call.
   - On VGA the §5.4.1.3 planar decoder is priced per pixel: 728 px × ~107
     cycles ≈ **16 ms** (ESTIMATE). That is better than ~18 calls, but not
     free.
   - On 1bpp it is a `gfx_blit1` with the pen pair, in one pass.
2. **Buttons on the focused window only.** **Decided (Q2): buttons are
   always drawn**, and this stays available as the FALLBACK rather than the
   design. It was built as a module build knob (`W95FRONT=1`) so the two arms could be
   compared on the 5150, and DELETED once the owner had: the always-drawn arm
   is fine on the Hercules (§10).
3. **The band composer** (`kernel/band.inc`, §11.101). It composes the whole
   bar and blits it once, but it is only on a knob build today and costs 1,634
   bytes. It is the right answer for a bold caption once it is downsized and
   shipped (S2), and too big a dependency for phase 1.

§9 M1 is the measurement that decides between primitives and option 1, and
prices option 2 as the A/B, with a counter in `gfx_fill` (PERFORMANCE.md
rule 4).

### 3.8 1bpp — Hercules and CGA

**Color is refused on 1bpp** (§76.12.1), because there it would be Bright
with extra steps. **This theme's point is structural, not colour.**

- Close on the right, the X, maximize, click-open menus and the size grip all
  mean the same thing on a Hercules.
- The fork owner's own machine is a Hercules 5150 (docs/FIELD-MACHINES.md).

The plan therefore **offers Windows on every `kern_big` adapter**, with a
second pen row for depth 1 that uses black and white only:

| | 1bpp pens |
|---|---|
| title bar, focused | `CBLACK` ground, `CWHITE` caption (Dark's bar) |
| title bar, unfocused | `CWHITE` ground, `CBLACK` caption |
| 3D face / lit / shadow / dark | `CWHITE` / `CWHITE` / `CBLACK` / `CBLACK` — the bevel becomes a flat framed button, which is how Windows' own monochrome scheme draws it |
| desktop | **both, as a prototype** — see below |

**The 1bpp desktop is prototyped BOTH ways (decided, Q1), and the owner picks
on the glass.**

- **The dither** (Bright's ground) keeps window outlines and drive icons crisp,
  because both are drawn in black.
- **Black** (Dark's ground) is the closer likeness. It costs the drive icons
  their outline and makes the pointer read hollow (§76.8), and the focused
  title bar, being black too, merges with the desktop at a window's edge.

Both are one pen byte in the 1bpp row (`desk` = `THM_DITHER` or `CBLACK`), so
the prototype is a module build knob (`W95MDESK=dither|black`) and two sets of
360 KB disks. Nothing in the design moves either way, and the loser is deleted
when the pick is made.

`thm_set` already owns the depth test (`[vid_kind]`, §76.12.1). It picks the
row, and `vid_switch`'s re-assert (§76.12.1) carries the theme across a Display
page switch for free.

**A 1bpp SECOND monitor on a VGA-primary extended desktop** reduces the colour
row through §39.4. That is §76.12.4's accepted case, and nothing new.

## 4. Phase 2 — the shared controls, and the boundary §76 drew — NOT BUILT

§76's contract is that **chrome is the machine's and content is the
application's**. The kernel's chrome is phase 1.

Windows 95 also changes how CONTROLS look: push buttons, scroll bars, check
boxes, radios, edit fields, the default-button ring. Those are drawn by
`apps/os88ui.inc`, which is **source compiled into each package** (it is not
an API slot, deliberately, per its own header):

- 51 package directories include it;
- the kernel carries a second copy in `.cold` for the Disk window and the file
  dialog.

So there are two levers, and only the second reaches running packages:

- **Rebuild, and branch at runtime.** One new query, either an `OSAPI_THEME`
  slot or a published byte. Each control asks it, `%ifdef`-gated by a new
  `OS88UI_BEVEL` capability so a package opts in.
  - Every in-tree package picks it up at its next `make`.
  - An old `.o88` keeps the Mac look, which is correct and harmless.
  - The cost is package image bytes (compressed disk), not resident kernel
    bytes.
- **The kernel's own copy** (the Disk window and the file dialog) changes when
  the kernel changes.

What phase 2 contains:

- the bevelled push button, a sunken edge for pressed, and the black outer
  frame for the default button;
- the bevelled scroll bar and the grey checker trough;
- the sunken check box with a tick, and the sunken round radio;
- the sunken edit field (`os88line.inc`);
- the navy item highlight in the pull-down;
- the Disk window's navy selection bar in place of XOR (`fm_sel_bar`,
  `files.inc:8111`);
- the bold caption (band composer);
- the hourglass busy pointer.

**The hourglass is cheap.** It fits `CUR_GW` x `CUR_GH` = 8x12, so it is a
`cur_shtab` row the module repoints. An 11x19 Windows arrow does not fit the
cell, so the arrow is refused.

**XOR is the blocker running through this phase.** Every selection in the
machine is an inversion (the Disk window, desktop zones, menus, the dock).
Inversion cannot produce navy with white text. Each one becomes a drawn state,
which is a separate change per site.

## 5. Phase 3 — a menu that does not freeze the machine — NOT BUILT

**Owner's ordering (Q3): its own phase, before the furniture.**

Phase 1 keeps the whole menu interaction inside one gfx-lock hold, so a
click-opened menu freezes every painter for as long as it stays open: the
clock, Task Manager, a video in a window, and anything waiting on the UI task
(FTPD's commits, a package's file save). A held menu does the same today; a
click-opened one can stay up indefinitely.

The problem to solve is the save-under. Once the lock is released between
passes (the way Word's own menu works, SPEC.md 13.16.4), any background painter
can draw under or over the open menu, and closing it then puts back stale
pixels. The candidates, to be costed in this phase and not before:

- **Fence the menu's rect out of every other painter.** It would be a
  machine-wide clip exclusion while a menu is up. The clip region (§11.3) is
  per hold, so this is new machinery.
- **Discard the save-under and repaint on close** (`wm_paint_dmg` over the
  rect). That path already exists as the refused-claim fallback in
  `menu_drop`, so it is correct by construction and costs a repaint per close.
- **An idle timeout as a cap.** Close a menu that has seen no pointer movement
  for ~30 s. It does not end the freeze but bounds it, and it is cheap enough
  to take on its own if the other two prove expensive.

## 6. Phase 4 — stretch: the furniture — NOT BUILT

Each of these is a replacement of furniture, not a reskin. Each is its own
plan when it is wanted.

- **Chip menu → Start button.** It is bar cell 0, and its title is a logo
  bitmap (`menu_logo_glyph`, `menu.inc:2556`). A "Start" title is a bitmap
  swap the module can do. What Windows does beyond that is
  - a taskbar at the BOTTOM,
  - the menu opening UPWARD,

  and both of those are the dock's geometry, not the bar's.
- **Dock → taskbar.** The dock is already vectored (`dkv`, SPEC.md 30.5). A
  taskbar painter is a `DOCK.DRV` sibling: wide bevelled tiles with a caption,
  a sunken tile for the active app, and the clock moved into a tray.
  - That last item moves the bar's clock, and the bar is this phase's other
    half.
- **The global menu bar → per-window menu bars.** This is a structural
  change: every window's content would move down 20 px, which is an ABI
  change for 23 package files using `MBAR_H`. It is refused unless the owner
  wants the SDK changed.
- **Menu bar sliding across titles** while a menu is open (§3.3), and with it
  **Left/Right** in an open menu (Q5).
- **Desktop icons in a left-hand column** (`desk.inc`, `DESK_ZXOFF`).
- **`Ctrl+S` in place of `^S`.** This text is package DATA (each item string
  carries its own), so it is a rebuild of every package. It is per-app, and
  out of scope by the ask's own terms.
- **A proportional caption font.** Refused: every layout in the tree assumes
  8 px cells.

## 7. The Control Panel page

The Theme page has three rows (Bright 18, Dark 34, Color 50), the animation
pair at 82/98, and the Screen Saver button at 114..129 inside `CP_CH` = 132
(`ctrl.inc:157-202`). There is no room left. §76.12.2 already spent the
caption to fit Color.

**Decided (D3): the Screen Saver button moves to the right column, beside
the animation radios.** Their labels are `On` and `Off`, and the pane is 221 wide.

- A fourth theme row then sits at 66.
- The animation group moves down 16.
- The page still ends inside 132, and the `%error` guard at `ctrl.inc:200`
  stays the assertion.

**Control ids and greying:**

- The ids renumber: 1..4 are the theme rows, 5..6 the animation pair, and 7
  the Screen Saver button. `CPH_CSAVE` and the click bands follow.
- **Windows is greyed only where the module cannot exist**: `kern_small`,
  where the whole page is absent anyway. It is NOT greyed for "the file might
  not be on the disk". §47 rule 3's only honest test of a readable file is
  reading it, and SPEC.md 76.4 makes exactly that argument for Screen Saver.
- A refused load is a toast, and the radio stays where it was.

**`SYSTEM.CFG`.** It is `'TH'` byte 0 = 3, and **no format change** is needed.

- `thm_set` clamps any value of `THM_NKIND` or more to Bright, so an older
  kernel reading a 3 gets Bright.
- `kern_small` round-trips the byte untouched, because `[thm_kind]` is outside
  the gate (§76.13).

## 8. Tests and tools

**The suite is unaffected by default, by construction.**

- Bright stays the default (decided, S5).
- With the table at rest, every pixel is the kernel's as before (§2.3 gate 1).
- Press-drag-release menus work under every theme (§3.3).

What needs to learn the theme:

- **`tools/os88geom.close_xy()`** (`os88geom.py:810`) assumes the close box is
  on the LEFT. It and `os88ui.close()` / `_raise_point`
  (`os88ui.py:415-439`) should read `[thm_kind]` off the guest and aim right
  under Windows.
  - That is one function each, and it makes every `ui.close()` test runnable
    in both themes.
  - The ~48 hand-rolled close clicks in 39 test files are left alone. They are
    Bright tests, and Bright is unchanged.
- **`tools/chromedown.py`** mirrors the box columns, and will mirror the new
  ones.
- **`tests/unit/t_mirror.py`** guards `WM_BOX_*`. The module's own geometry
  constants should come from one `%define` block the mirror can read.

New rows, all soak and all MartyPC. VGA is MartyPC's too: `os8088_xt_vga`.

| row | what it asserts | how it goes red on purpose |
|---|---|---|
| `thm95pix` | Bright with the table at rest is 0 differing pixels against the base kernel, §76.9's protocol | hook one slot to a stub that draws nothing |
| `thm95chrome` | under Windows: the close press at the right closes, the left position drags, maximize zooms and restores, a non-sizable window's maximize is greyed and refuses | swap regions 2 and 5 in the module's hit test |
| `thm95menu` | the bar, a context menu and a dock menu each: click-open stays up, the second click fires, a click off dismisses AND reaches nothing, Esc closes, press-drag-release still fires | stop draining the queue: the dismissing click opens what is under it |
| `thm95cost` | `flicker` and a `gfx_fill` counter on one title bar, for each adapter class | the number is the gate: §3.7's budget, set once M1 is measured |
| `thm95mono` | Hercules: the 1bpp row, legible (adjacent-pair test, as in §76.12.4's table), in BOTH desktop arms until Q1's pick | force the colour row onto 1bpp |
| `thm95inact` | a press on an unfocused window's X closes it without raising it first, and its held light stays inside the visible part of the title bar | arm the light unclipped: it paints over the covering window |
| `thm95fall` | the module unreadable at boot: Color on a colour adapter, Bright on 1bpp (Q4); and the panel's refusal toasts | rename `WIN95.DRV` on the image |

## 9. What to measure first

Every owner question is answered (§0.1). What remains is measurement, and it
comes before the first byte of `WIN95.DRV`:

- **M1. The title bar's call count and time**, on the 5150 profile and on VGA,
  for **every window** (Q2):
  - (a) primitives;
  - (b) one `gfx_blit4` for the buttons;
  - (c) the `W95FRONT=1` fallback, as the A/B (built, measured, deleted -
    §10).

  This decides §3.7, and it is what the owner will be told if the always-drawn
  arm turns out slow.
- **M2. The resident ~153 bytes**, by building the six stubs, the table and
  the two `ui.inc` branches alone, with no module. Quote `kernsize`'s sum, and
  check that Bright stays pixel-identical (`thm95pix`).
- **M3. The tracked arrow inside a held lock** (§3.3 item 1). Build the sticky
  phase with the §7.4 pattern and run `flicker` over a hover across five
  items: the transient count, and that `settled` is true. This is the riskiest
  piece and is cheap to try in isolation.
- **M4. That `vga_xor_fill_vram` maps 7 → 8 and 0 → 15 on EGA as well as
  VGA**, by capture rather than by argument.
- **M5. The held light on a partly covered title bar** (§3.1, Q2): whether
  clipping `ui_lit_on` through the window's clip region is as small as it
  looks, or whether raise-on-press is the honest shape.

## 10. Phase 1 as built, and what it changed in this plan

SPEC.md 76.14 is the contract and 76.14.11 the measurements; this is the
record of where the build and the plan part company, so the plan above is not
re-read as the design.

- **Five doors, not six.** The frame bevel (§3.4, hook 4) was not built: the
  black outline and the black drop shadow already read as the dark side of a
  raised edge, and a door there would duplicate `wm_draw_win`'s flush and
  region logic for a look the owner called general-not-pixel-perfect (S4).
  The pull-down's raised frame and etched separators (§3.6) were not built
  either: row 3's pens make the face light grey, and that is the look.
- **A button is a 1bpp BAND, one `gfx_blit1`, plus two lines** - not the
  primitive bevel §3.7 priced, and not the `gfx_blit4` it preferred. Eleven
  calls a bar on every window. Measured: 28.6 ms front / 31.8 ms back on VGA,
  27.7 / 29.8 on CGA, against Bright's 18.0 / 10.0 and 26.0 / 11.8 (M1).
  `W95FRONT=1` took a background bar back to 10.8 ms; the owner then drove
  the always arm on the 5150's Hercules (2026-10-13), found it fine to the eye
  and the background buttons right, and the knob was deleted.
- **It found a kernel defect, and the fix is in the kernel** (SPEC.md
  5.4.2.9): `gfx_blit1` merged a partial byte with the CPU, which on a planar
  card copies plane 0 onto every plane, so a band at an x off the byte grid
  over any colour but black or white turned its neighbours light grey. Every
  band in the tree had landed on black or white until the caption buttons
  landed on navy. 51 bytes of `.cold`, through the Bit Mask.
- **Resident: 181 bytes for the theme** (M2's ~153 estimate) plus the 51 of
  the fix; `kern_small` +0. `WIN95.DRV` is 1,560 bytes, claimed as 2 KB.
- **The arrow in a held lock (M3) is the loop's own `cursor_show`/`hide`**, not
  SPEC.md 7.4's ISR-tracking bracket: the loop is already polling, so it moves
  the arrow itself - one or two frames a move, 0-9 transient pixels.
- **M4 is answered by construction**: EGA here is SPEC.md 39.24's geometry on
  a VGA card, so `vga_xor_fill_vram`'s mapping is the VGA's, and the VGA
  capture shows it (a dark-grey bar, white text).
- **M5: the held light is clipped through `wm_chrome_clip`** in a 12-byte
  `ui_lit_xor` - 0 pixels of a covering window touched, 5 with it removed.
- **A click on a background window's button needed no `ui.inc` change on
  the press**: the `.chrome` arm always armed without a raise; what it
  needed was `wn_hit` answering on every window and the clip above.
- **The gate is one row, `tests/thm95.py`**, with five legs in place of §8's
  six tables, and §76.9's identity claim checked once by hand against
  `d65b91e9` on VGA and CGA (SPEC.md 76.14.11) rather than as a row, because a
  row would have to build the old tree.
- **Q1 was picked on the glass: the DITHER** (2026-10-13). Black was chosen
  first and reversed the same day: two more viewers found the title bars far
  easier to see against the dither, where a black focused bar on a black
  desktop has no edge. `THM_WINMONO`'s desktop is `THM_DITHER`, the knob
  (`W95MDESK`) is gone, and a background customizer may come later as its
  own feature.
- **The click test had to come first** (SPEC.md 76.14.7.1, found by the
  owner after the merge). §3.3's superset asked about an item before asking
  about a click, and a context menu opens AT the pointer, over its first
  item - so a right-click let go a pixel off its press ran that item and a
  context menu could never stay up. A click now wins and leaves nothing lit.

## 11. What was left open when phase 1 shipped

None of it is in progress. To pick any of it up, move this file back to
`docs/plans/` and start at the item.

- **Phase 2 (§4)** — the controls inside windows. Decisions it needs first:
  - **the boundary**: §76.1 says content is the application's, and themed
    controls move that line; which themes theme them;
  - **how a package asks**: a new slot returning the theme kind (recommended)
    or a published byte;
  - **opt-in or default**: on by default at each package's next rebuild, with
    an opt-out, was the recommendation; old `.o88` files keep the Mac look
    either way;
  - **disk**: `apps360.img` had 34 of 354 clusters free and `os8088-360.img`
    39 at the end of phase 1, and `os88ui.inc` growth is paid once per
    package (43 assembly sources include it, 27 use buttons or scroll bars);
  - **draw cost**: a bevelled button is ~5 calls against 2; the Calculator's
    keypad is the case to measure, and a 1bpp band per button (the title
    bar's trick) is the candidate;
  - the C SDK draws through `apps/cc/os88thunk.asm` and needs the same change;
  - every XOR selection (menu item, Disk-window row, desktop icon) is its own
    change to a drawn navy bar.
- **Phase 3 (§5)** — a menu that does not freeze. Three candidates are costed
  there; the save-under is the hard part.
- **Phase 4 (§6)** — the furniture: Start button, taskbar, per-window menu
  bars (the `MBAR_H` ABI question), desktop icons on the left, and Left/Right
  between pull-downs.
- **Phase 1's leftovers** — a bold caption (waits on the band composer being
  downsized and shipped), "..." on a cut caption, the 3D window frame (§3.4)
  and the bevelled pull-down with etched separators (§3.6).
- **Measurements not taken**: the title bar was timed on VGA and CGA under
  MartyPC, never on Hercules; the owner's 5150 is the Hercules evidence, and it
  is by eye.
- **A background customizer** for the 1bpp desktop was floated as a separate
  feature someday (§10, Q1).
