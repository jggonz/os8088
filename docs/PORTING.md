# Bringing work forward from pre-merge `main`

A long-lived branch was merged into this tree at **`b401eda`**. If you have a
package, a patch or a branch written against that commit or an earlier one,
this is how to bring it forward — one direction only, because there is nowhere
to go back to.

The binding contract is [SPEC.md](../SPEC.md), and §20.8 in particular is the
list of things a package may not do.

**Nothing built for `b401eda` runs here.** The loader refuses a stale `.o88`
as "Bad package" — this tree's v3 header carries the three dispatcher bytes
at +12 (§5/§6) and `ld_check_hdr` demands them — so the failure is loud, not
wrong. Bringing work forward always means rebuilding from source.

---

## Contents

1. [Doing it in git](#1-doing-it-in-git)
2. [The five-minute version](#2-the-five-minute-version)
3. [Why a rebuild is required](#3-why-a-rebuild-is-required)
4. [The API slot table, then and now](#4-the-api-slot-table-then-and-now)
5. [The callback convention](#5-the-callback-convention)
6. [The package header](#6-the-package-header)
7. [Window geometry](#7-window-geometry)
8. [Memory](#8-memory)
9. [Sound](#9-sound)
10. [Menus and About](#10-menus-and-about)
11. [Files, folders and the dialog](#11-files-folders-and-the-dialog)
12. [Worker tasks](#12-worker-tasks)
13. [Repaint obligations](#13-repaint-obligations)
14. [Things that did not change](#14-things-that-did-not-change)
15. [What you gain](#15-what-you-gain)
16. [Checklist](#16-checklist)
17. [Worked example: `hello`](#17-worked-example-hello)

---

## 1. Doing it in git

The merge commit has both lines as parents, so `b401eda` is an ancestor of
`HEAD` and git can reason about the range normally. What it *cannot* do is
resolve the content: the merge deliberately kept this tree's files
(`-s ours`), every capability of the other line having been checked off
first. So a plain `git merge b401eda` will report "Already up to date" and
change nothing — correct, and not what you want.

Bring the work forward by replaying **your** commits, not by merging:

```sh
git fetch origin
git log --oneline b401eda..my-branch      # what is actually yours
git checkout -b my-branch-forward origin/main
git cherry-pick <first>^..<last>          # or: git rebase --onto origin/main b401eda my-branch
```

Expect conflicts in proportion to how much kernel you touched. Three files are
where they concentrate, and in each case **take this tree's side and re-apply
your intent on top** rather than resolving hunk by hunk:

| file | why it conflicts |
|---|---|
| `kernel/kernel.asm` | the memory ladder is derived here, not a list of constants, and the API table grew new slots at `0x0200`+ |
| `apps/os88api.inc` | new `OSAPI_MEM_*` names carry the KB contract; the paragraph trio lives on at its old numbers, wrapper-answered |
| `SPEC.md` | renumbered sections, and §45 arrived by a different route |

If your work is a **package only**, none of that applies: copy the `.asm` into
`apps/<name>/`, apply §16's checklist, add it to `APPS_TOOLS` or `APPS_GAMES`
in the Makefile, and rebuild. That is the common case and it is an afternoon.

Whatever you do, finish with `make && make check-images && python3
tools/checkdocs.py`. The second catches a stale shipped binary and the third
catches a SPEC citation that pointed somewhere that has since moved — both are
failure modes a forward-port produces routinely.

---

## 2. The five-minute version

If you only read one section, read this one.

1. **Delete every `retf`.** They all become `ret`. This is the big one and
   §5 explains why.
2. **Delete every `push cs`** that exists only to fake a far return before
   calling one of your own kernel-called procs.
3. **`OSAPI_MEM_ALLOC` → `OSAPI_MEM_CLAIM`**: the unit is KB, not paragraphs,
   and the answer comes back in `DX`, not `AX`. `OSAPI_MEM_AVAIL` likewise
   answers KB, with the total in `BX`.
4. **Set `ES = DS`** before any `ES:BX` file buffer or `ES:SI` sound buffer.
   You are called with `ES = KERNEL_SEG` here, so it is no longer free.
5. **Drop the fourth `OS88_HEADER` argument** (the RAM requirement) and any
   `--needs` in the Makefile rule. There is no load-time RAM gate; size
   yourself at runtime instead.
6. **Rebuild.** Slot numbers are all `%define`s and every one of `b401eda`'s
   kept its number, so re-assembly asks nothing of you here.

What you do **not** have to change, though the older version of this document
said otherwise: `OSAPI_WM_GEOM`, `OSAPI_ABOUT_SET`, `OSAPI_CPU_INFO`, every
`OSAPI_XMEM_*`, `OSAPI_GFX_DBUF`, `OSAPI_GFX_SCROLL` and `OSAPI_FILE_READBIG`
all exist here with `main`'s contracts. The two sound slots exist too, but
answer CF=1 unless the **sound driver** is loaded (SPEC.md §51) — which is what
`OSAPI_SND_CAPS` is for, and what a well-written app already checked.

---

## 3. Why a rebuild is required

Three things make a `b401eda` binary unrunnable here, and only the first is
obvious.

**`KERNEL_SEG` moved**, `0x1000` → `0x0060`. It is baked into every `OSAPI_*`
far-call target, so a stale binary calls into empty memory on its first API
call. Re-assembly fixes it; nothing else can.

**The callback mechanism inverted.** `b401eda` far-called your procedure
directly, so every kernel-called proc ended in `retf`. Here the kernel calls a
three-byte dispatcher inside your own header, which calls you *near* — so every
one of those procs is a near proc with a near `ret`. A stale `retf` returns into
the loader's stack frame and hangs the machine at the first paint. See §5.

**Slot numbers did NOT move.** Every slot `b401eda` publishes keeps its number
and its contract here — the three paragraph-counting memory slots at
`0x01B8`..`0x01C8` included, answered by wrappers over the claim heap — and
everything this tree added starts at `0x0200`, `b401eda`'s first free number.
So a stale binary's API *calls* land on the right routines; it is the two
items above, not the table, that force the rebuild.

`SS != DS` on both, so `[bp+disp]` addresses `SS` and a pointer held in `BP`
still needs an explicit `ds:` override. `LOW_SEG` itself is no longer a constant
you can quote — the memory ladder is derived from the kernel's measured size, so
it moves whenever the kernel does.

---

## 4. The API slot table, then and now

Slots `0x0010`..`0x00F0` are **unchanged**:

```
0x0010 GFX_LOCK      0x0038 GFX_FILL       0x0060 FONT_CHAR    0x0090 WM_FRONT
0x0018 GFX_UNLOCK    0x0040 GFX_FRAME      0x0068 FONT_STR     0x0098 WM_CONTENT
0x0020 GFX_PIXEL     0x0048 GFX_FILL_GRAY  0x0070 FONT_WIDTH   0x00A0 WM_OBSCURED
0x0028 GFX_HLINE     0x0050 GFX_XOR_RECT   0x0078 WM_CREATE    0x00A8 TASK_YIELD
0x0030 GFX_VLINE     0x0058 GFX_XOR_FILL   0x0080 WM_SHOW      0x00B0 TASK_SLEEP
                                           0x0088 WM_HIDE      0x00B8 GET_TICKS
0x00C0 SET_COLOR     0x00D0 SRAND          0x00E0 SND_CAPS     0x00F0 SND_PLAY
0x00C8 MOUSE         0x00D8 RAND           0x00E8 SND_TONE
```

`0x00F8` to `0x01F8` are unchanged too, routine for routine — **including the
three paragraph-counting memory slots**:

| slot | at `b401eda` | here |
|---|---|---|
| `0x01B8` | `MEM_ALLOC` (paragraphs, answers in `AX`) | the same contract, answered by the claim heap through `osapi_cm_alloc` |
| `0x01C0` | `MEM_FREE` (`AX`) | the same, through `osapi_cm_free` |
| `0x01C8` | `MEM_AVAIL` (paragraphs) | the same, through `osapi_cm_caps` |
| `0x01D0`..`0x01F8` | `WM_RESIZE`, `GFX_BLIT4`, `ABOUT_SET`, `FILE_READBIG`, `GFX_DBUF`, `GFX_SCROLL` | identical, number for number |
| `0x0200`+ | — | this tree's additions: `MEM_CLAIM` / `MEM_FREE` / `MEM_AVAIL` (the KB shapes, §8), `FONT_GLYPHS`, `WM_ONSIZE`, `FILE_HERE`, `FILE_GOTO`, `MEM_REGROW`, `WM_TITLE`, `DRV_TASK` (drivers only), `MEM_CLAIM_DMA` |

**Every contract `b401eda` had, this tree has, at `b401eda`'s number.** The
paragraph slots are wrappers now — a paragraph count rounds up to whole KB, so
a grant is never smaller than asked — and new code should prefer the KB slots
at `0x0200`+ (§8): they are the native shape, they work from the entry proc,
and only they carry the DMA-page and regrow contracts.

You never write these numbers by hand — `%include "os88api.inc"` supplies
them — and this tree's SDK names the KB shapes `OSAPI_MEM_*`, so a `b401eda`
source that used `OSAPI_MEM_ALLOC` (paragraphs) must either convert to KB at
the new name or call the old slot by number. Everything else re-assembles
with no thought given to the table at all.

---

## 5. The callback convention

### 5.1 What the kernel does

At `b401eda` the kernel built a far pointer to your procedure and `call far`ed
it. Your procedure was the far target.

Here the kernel builds a far pointer to `PKG_DISP` = offset **12** in your own
package header, loads `BP` with the near offset of your procedure, and
`call far`s the header. The four bytes there are:

```
+12  FF D5     call bp
+14  CB        retf
+15  00        pad
```

So the dispatcher calls you near, you `ret` to it, and it `retf`s to the kernel.
`wm_pkgcall` is the single site, and dispatch is re-entrant across packages
because the far pointer comes out of the window record (`W_DISP`/`W_SEG`), not a
global.

The consequence worth internalising: **a package author never writes `retf`, so
a missing one cannot exist.** SPEC.md §20.8 rule 5 is the binding form.

### 5.2 What you write

Every routine the kernel calls — the entry proc, `W_PAINT`, `W_ONKEY`,
`W_ONCLICK`, the `AM_ONCMD` menu handler, the file-dialog completion proc, and
the `W_ONSIZE` negotiator:

| | at `b401eda` | here |
|---|---|---|
| returns with | `retf` | `ret` |
| calling one of them yourself | `push cs` then `call` | plain `call` |

The worker task entry is the exception: it never returns at all, on either.

```nasm
; before
hl_paint:
    ...
    retf                    ; far-called W_PAINT

hl_repaint:
    push cs                 ; hl_paint returns with retf: give it the CS
    call hl_paint           ; a far call would have pushed
```

```nasm
; after
hl_paint:
    ...
    ret

hl_repaint:
    call hl_paint
```

A leftover `retf` is the failure mode that assembles cleanly and hangs at the
first paint. A leftover `push cs` unwinds the stack by two bytes per call and
survives longer, which makes it worse.

### 5.3 Segment registers on entry

| | at `b401eda` | here |
|---|---|---|
| `CS` | your segment | your segment |
| `DS` | your segment | your segment |
| `ES` | your segment | **`KERNEL_SEG`** |
| `SS` | `LOW_SEG` | `LOW_SEG` (derived, not a constant) |

The `ES` difference is the one that bites. Here `ES` already points at kernel
memory when you are called, which is what makes `[es:bx+W_W]` and the file
dialog's name buffer work with no segment load. You may clobber `ES` freely
afterwards — but the two ES-relative APIs (`OSAPI_FILE_*` data buffers,
`OSAPI_SND_PLAY` samples) read *your* memory, and at `b401eda` that was free
because `ES = DS` already. **Set `ES = DS` before those calls.** Code that
worked for years without doing so will read the kernel's memory instead, and
what it finds there is plausible enough to be confusing.

---

## 6. The package header

`OS88_HEADER` emits 32 bytes. Unchanged:

```
+0   'O','8'          magic
+2   3                format version
+3   flags            bit 0 = embedded 16x16 icon follows
+4   dw 0             link base (retired; must be zero)
+6   dw entry         image-relative entry offset
+8   dw image size
+10  dw bss size
+16  16 bytes         NUL-padded program name, max 15 chars
```

Bytes `+12`..`+15` are the whole disagreement:

| | at `b401eda` | here |
|---|---|---|
| `+12` | `dw 0` — retired v2 relocation count | `db 0FFh, 0D5h, 0CBh, 0` — the dispatcher |
| `+14` | `dw` minimum conventional RAM, **KB** | — |
| macro arity | `OS88_HEADER name, entry [, flags [, needs_kb]]` | `OS88_HEADER name, entry [, flags]` |
| image + bss cap | 65,520 B | **61,440 B** (`APP_MAX_SIZE` = `0xF000`) |
| `os88pkg.py` | `--needs KB` overrides `+14` | validates the four dispatcher bytes |

`b401eda` used the `+14` word to refuse a load on a machine with less RAM than
the package declared. **There is no such gate here, and there cannot be one** —
the dispatcher occupies those bytes. An app sizes itself at runtime from
`OSAPI_MEM_AVAIL` and puts up a notice window if the answer is too small;
`apps/paint` is the reference, giving up features tier by tier.

So: drop the fourth macro argument, drop any `--needs` from the Makefile rule,
add runtime sizing if the app relied on the gate, and check the image against
the smaller 61,440-byte cap.

---

## 7. Window geometry

**No conversion needed.** `OSAPI_WM_GEOM` is here at `0x01B0` with `main`'s
contract: `CX` = content width, `DX` = content height, `CF=1` if hidden. It is
the recommended idiom and the one an app brought forward should keep.

This tree *also* lets you read the record through `ES`, and several of its own
apps still do. If you are porting, ignore that: those are **frame** dimensions,
so every caller repeats the same `-2` / `-TITLE_H-1`, and `WM_GEOM` is that
subtraction done once. The record is readable, not idiomatic.

`OSAPI_WM_RESIZE` is unchanged in contract (`BX` = win, `CX` = w, `DX` = h,
lock held) and in number (`0x01D0`).

---

## 8. Memory

This is the real work, and the one place a mechanical substitution is not
enough.

### 8.1 The conversion

| | at `b401eda` | here |
|---|---|---|
| unit | paragraphs (16 B) | **KB** |
| claim | `MEM_ALLOC`, `AX` = paragraphs → `AX` = segment | `MEM_CLAIM`, `AX` = KB → **`DX`** = segment |
| free | `MEM_FREE`, `AX` = segment | `MEM_FREE`, **`DX`** = segment |
| available | `MEM_AVAIL` → `AX` largest, `DX` total | `MEM_AVAIL` → `AX` largest KB, **`BX`** total KB |
| refusal | CF=1 | CF=1 |

`paragraphs >> 6` is KB. The register move is the part that assembles cleanly
and misbehaves: a claim whose answer you read from `AX` gets whatever `AX`
happened to hold.

**Or skip the conversion**: `b401eda`'s three paragraph slots are still live
at their own numbers (`0x01B8`/`0x01C0`/`0x01C8`, §4), answered by wrappers
over the claim heap with `main`'s register contracts intact — call them by
number and a port's memory code needs no change at all. The KB names are
still the better target: only they carry the regrow and DMA-page contracts,
and the SDK has no `%define` for the old shapes on purpose.

### 8.2 The model changed underneath it

`b401eda` handed packages memory out of a fixed **arena** — a reservation
carved out at boot whether or not anything used it. Here it is a **claim heap**
(SPEC.md §50): everything above the kernel, handed out on demand and given back
on teardown. Three consequences for a port:

- **There is much more of it.** A 640KB machine measures 566KB of heap. Code
  that worked around a ~107KB arena — refusing large files, staging in pieces,
  declining a feature — may not need to any more. `apps/tracker`'s §45.8 is a
  worked example: the same player refuses a 116KB module at `b401eda` on a
  512KB machine and loads it here.
- **Refusal is still normal** and every claim needs its fallback. The heap is
  shared, so "there was room a moment ago" is not a guarantee.
- **Your claims are freed at teardown**, stamped with the segment you run in.
  You need no close hook, and freeing early is only for handing memory back
  mid-session.

### 8.3 What is new

- **`OSAPI_MEM_REGROW`** resizes a claim you already hold, **in place** when
  the paragraphs above it are free. There was no shrink primitive at
  `b401eda`, and code that over-claimed because re-claiming might relocate the
  base can stop doing that.
- **`OSAPI_MEM_CLAIM_DMA`** takes the 64KB physical-page rule as a parameter,
  for a buffer an ISA DMA controller will read or write. `CX` is the KB of the
  block's **head** the chip sees, not the whole block.
- **Extended memory** (`OSAPI_CPU_INFO`, `OSAPI_XMEM_*`) is here at `main`'s
  numbers and unchanged. On the 8086 this OS targets it is all zero KB.

---

## 9. Sound

The slots are `main`'s and the contracts are `main`'s. What changed is where
the hardware lives: the OPL2 and Sound Blaster tiers are a **loadable driver**
(`SOUND.DRV`, SPEC.md §51), not resident kernel code. So:

- `OSAPI_SND_FM` and `OSAPI_SND_STREAM` answer CF=1 with no driver loaded.
  Branch on `OSAPI_SND_CAPS` — which a correct app already did, because the
  card could always be absent.
- The staging pool a stream ring is granted from **belongs to the driver**, so
  its size is not a constant to assume. Ask in tiers and record into what you
  got; `apps/recorder` is the reference.
- `OSAPI_SND_TONE` is worker-safe; the blocking `OSAPI_SND_PLAY` is UI-task
  only. Unchanged, and now enforced more carefully: a grant's stamp is
  qualified by the task that made it, so a worker cannot inherit a foreign
  callback's instance.

---

## 10. Menus and About

`OSAPI_MENU_SET` and the `OS88_MENUSET` / `OS88_MENU` macros are unchanged.
`OSAPI_ABOUT_SET` is here (still `0x01E0`) and is the right way to publish
an About item: it appends a one-item pull-down under your app's name.

If your app used **the empty-menu-label trick** — an `AM_NAME` of `""` and a
first menu titled with the app's name, so the pull-down lands where the label
would be — replace it with `OSAPI_ABOUT_SET` and give the set its real name.
`main`'s Paint did this; this tree's does not.

Menu sets are read **live** here through the owning window's segment, not
copied into kernel buffers. A set whose item strings you rewrite in place takes
effect on the next drop with no re-registration — which is how Paint's
`(NoRam)` labels switch both ways.

---

## 11. Files, folders and the dialog

Unchanged: every `dskw_*` slot, all eleven `FERR_*` codes, the Standard File
dialog, and `OSAPI_FILE_READBIG` (still `0x01E8`) for reads with no 64KB
ceiling.

New here: `OSAPI_FILE_HERE` and `OSAPI_FILE_GOTO`, which read and restore the
directory names resolve in. Also `dskw_rmtree` — delete recurses into folders.

The apps disk is foldered on both sides, so a package still goes in
`APPS_TOOLS` or `APPS_GAMES` in the Makefile. **Append it**, so existing row
indices in the scripted tests hold.

---

## 12. Worker tasks

Unchanged in every respect: `OSAPI_TASK_SPAWN` from a callback, one worker per
instance, and the worker dies inside `OSAPI_TASK_ALIVE` — which it must call
every loop or it leaks its instance record for the session.

Two things a forward-ported worker should know. `OSAPI_TASK_SLEEP` is
**relative** — the deadline is `[ticks] + AX` at the call — so a worker that
sleeps 1 tick per frame runs at `ceil(work)+1` ticks per frame, not 1.
`OSAPI_GET_TICKS` lets you pace against an absolute deadline instead;
`apps/arkanoid` is the reference. And a worker must re-check visibility under
the gfx lock and arm a clip region, which has not changed but matters more now
that damage-rect repainting means fewer full repaints will paper over a mistake.

---

## 13. Repaint obligations

The rules are the same and there is more machinery behind them, most of which
you get for free. Two that can bite a port:

- **`W_PAINT` does not run on a wholly covered window.** A paint proc must be
  a repaint and nothing else — no state changes, no side effects.
- **The granularity rule.** Fills clip per pixel, glyphs per whole 8x8 cell.
  Anything that erases a rect and then draws text into it must not let the two
  disagree, or a window cut by another window's edge goes *blank* rather than
  stale. Erase per cell behind a `wm_clip_test`, or gate the erase-and-draw
  pair on one test of the whole rect and skip both.

`OSAPI_WM_TITLE` is new and repaints only the caption strip; `OSAPI_GFX_SCROLL`
is new and moves a rect instead of redrawing it.

---

## 14. Things that did not change

Do not spend porting effort here — these are contract-identical:

- The whole graphics API `0x0010`..`0x0058` and the font API `0x0060`..`0x0070`.
- `OSAPI_WM_CREATE`, `WM_SHOW`, `WM_HIDE`, `WM_FRONT`, `WM_CONTENT`,
  `WM_OBSCURED`, and the `WT_*` template layout.
- `OSAPI_TASK_YIELD`, `TASK_SLEEP`, `GET_TICKS`, `SET_COLOR`, `MOUSE`, `SRAND`,
  `RAND`.
- `OSAPI_VIDEO` and the three-adapter rule: `SCREEN_W`/`SCREEN_H` are VGA
  reference values, the live screen is what `OSAPI_VIDEO` reports, and colours
  reduce to black / white / 50% dither on the mono adapters.
- `OSAPI_GFX_BLIT4` and `OSAPI_WM_RESIZE` contracts — only the numbers moved.
- The 16 EGA colour indices, `MBAR_H` = 20, `TITLE_H` = 18.
- The clip region API and its granularity rule.
- `OS88_ICON16` / `OS88_BSS` / `OS88_IMAGE_END`, and the icon format.
- 8086-only assembly: `cpu 8086`, `-w+error`, no `pusha`, no `push imm`, no
  `shl reg, imm` other than 1, no `movzx`, no 32-bit registers.

---

## 15. What you gain

Worth a second pass over ported code, because these did not exist to be used:

| | |
|---|---|
| `OSAPI_MEM_REGROW` | resize a claim in place — retires the over-claim workaround |
| `OSAPI_MEM_CLAIM_DMA` | the 64KB page rule inside the allocator |
| `OSAPI_WM_ONSIZE` | negotiate a resize instead of accepting it |
| `OSAPI_WM_TITLE` | retitle without repainting the window |
| `OSAPI_FONT_GLYPHS` | the kernel's 8x8 glyph table |
| `OSAPI_FILE_HERE` / `GOTO` | read and restore the current directory |
| `OSAPI_GFX_SCROLL` | move a rect rather than redraw it |
| a much larger heap | see §8.2 |

---

## 16. Checklist

**Mechanical**

- [ ] Replace every `retf` with `ret`.
- [ ] Remove every `push cs` that precedes an internal call to a kernel-called
      proc.
- [ ] Drop the fourth `OS88_HEADER` argument and any `--needs` in the Makefile
      rule; add runtime sizing if the app relied on the load-time gate.
- [ ] Check image + bss against the **61,440-byte** cap.
- [ ] Append the package to `APPS_TOOLS` or `APPS_GAMES`.

**Segments**

- [ ] Set `ES = DS` before any `ES:BX` file buffer or `ES:SI` sound buffer.
- [ ] If you read the window record, make sure `ES` still holds `KERNEL_SEG` at
      that point — or use `OSAPI_WM_GEOM`, which needs no `ES` at all.

**Memory**

- [ ] `OSAPI_MEM_ALLOC` → `OSAPI_MEM_CLAIM`: paragraphs `>> 6` to KB, and the
      answer moves from `AX` to `DX`.
- [ ] `OSAPI_MEM_FREE`: argument moves from `AX` to `DX`.
- [ ] `OSAPI_MEM_AVAIL`: `AX` is KB now, and the total moves from `DX` to `BX`.
- [ ] Revisit anything that worked around the arena's size (§8.2).

**Sound**

- [ ] Keep the `OSAPI_SND_CAPS` branch — the card is a loadable driver and may
      be absent at runtime.
- [ ] Do not assume the staging pool's size.

**Verify**

- [ ] `make && make test`, then drive it: the window opens, paints, menus
      track, the close box works, and the app survives being covered and
      revealed. **Pay attention to the first callback after load** — a missed
      `retf` conversion shows up there and nowhere earlier.
- [ ] `make check-images` before committing anything under `build/`.
- [ ] `python3 tools/checkdocs.py` if you touched prose.

---

## 17. Worked example: `hello`

`apps/hello/hello.asm` is the smallest package in the tree, and the whole
forward-port is four hunks. It is the clearest possible illustration of §5.

```diff
 ; hl_entry - package entry point
-; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
+; in:  CS=DS = our own segment, ES=KERNEL_SEG, IF=1, gfx lock NOT held
 ; out: BX = window ptr, CF clear (CF set = abort)
 hl_entry:
     ...
-    retf                            ; far-called by the loader
+    ret

 hl_paint:
     ...
-    retf                            ; far-called W_PAINT
+    ret

 hl_oncmd:
     ...
-    retf                            ; far-called menu handler
+    ret

 hl_repaint:
-    push cs                         ; returns with retf: give it the CS a
-    call hl_paint                   ; far call would have pushed
+    call hl_paint
```

Three `retf` → `ret`, one `push cs` deleted. Everything else in the file — the
window template, the menu set, the drawing, the icon — is untouched.

Counting `retf` per source file is a quick way to size a port before starting.
