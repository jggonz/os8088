# Porting os8088 applications between `main` and `experimental`

A working reference for moving a `.o88` package source from one fork to the
other. It assumes you know the branch you are coming *from*; everything the
other branch does differently is spelled out.

For the narrative version of why the forks differ, see
[BRANCH-DIFFERENCES.md](../BRANCH-DIFFERENCES.md). For the binding contract on
either side, read that branch's own `SPEC.md` — and **do not cite section
numbers across branches** without checking: `experimental` has since aligned
§0-§44 with `main`, so most now match, but `experimental`-only material sits at
§50+ (the claim heap) and in a reserved `.90` subsection band (§11.90, §11.91,
§18.90, §37.90).

**Nothing here is binary-compatible.** There is no way to run a `.o88` built for
one branch on the other, and no loader on either side will diagnose it usefully —
the header magic and version match, so it loads and then jumps into the wrong
place. Porting means rebuilding from source.

---

## Contents

1. [The five-minute version](#1-the-five-minute-version)
2. [Why a binary cannot cross](#2-why-a-binary-cannot-cross)
3. [The API slot table, side by side](#3-the-api-slot-table-side-by-side)
4. [The callback convention](#4-the-callback-convention)
5. [The package header](#5-the-package-header)
6. [Window geometry](#6-window-geometry)
7. [Memory](#7-memory)
8. [Sound](#8-sound)
9. [Menus and About](#9-menus-and-about)
10. [Files, folders and the dialog](#10-files-folders-and-the-dialog)
11. [Worker tasks](#11-worker-tasks)
12. [Repaint obligations](#12-repaint-obligations)
13. [Things that did not change](#13-things-that-did-not-change)
14. [Checklist: `main` → `experimental`](#14-checklist-main--experimental)
15. [Checklist: `experimental` → `main`](#15-checklist-experimental--main)
16. [Worked example: `hello`](#16-worked-example-hello)
17. [Feature availability matrix](#17-feature-availability-matrix)

---

## 1. The five-minute version

If you only read one section, read this one. Porting **`main` → `experimental`**:

1. Delete every `retf` in the file. They all become `ret`.
2. Delete every `push cs` that exists only to fake a far return before calling
   one of your own kernel-called procs.
3. Replace `OSAPI_WM_GEOM` with `[es:bx+W_W]` / `[es:bx+W_H]`, and subtract the
   frame: content is `W_W - 2` wide and `W_H - TITLE_H - 1` tall.
4. Replace `OSAPI_MEM_ALLOC` (paragraphs, returns `AX`) with `OSAPI_MEM_CLAIM`
   (KB, returns `DX`), and `OSAPI_MEM_AVAIL`'s paragraphs with KB.
5. Delete `OSAPI_CPU_INFO` and every `OSAPI_XMEM_*` — neither exists.
   `OSAPI_ABOUT_SET`, `OSAPI_SND_FM` and `OSAPI_SND_STREAM` all exist and are
   at `main`'s numbers, but the two sound slots answer CF=1 unless the
   **sound driver** is loaded (SPEC.md §51) — which is exactly what
   `OSAPI_SND_CAPS` is for, and what a well-written `main` app already checks.
6. Drop the fourth `OS88_HEADER` argument (the RAM requirement) and any
   `--needs` in the Makefile rule.
7. Rebuild. The slot numbers are all `%define`s, so re-assembly fixes them.

Porting **`experimental` → `main`** is the same list inverted, plus the one thing
that has no mechanical answer: `OSAPI_WM_ONSIZE`, `OSAPI_FONT_GLYPHS`,
`OSAPI_FILE_HERE` and `OSAPI_FILE_GOTO` do not exist on `main` and each needs a
design decision (§17).

---

## 2. Why a binary cannot cross

Three independent breaks, any one of which is fatal.

### 2.1 `KERNEL_SEG`

| | `main` | `experimental` |
|---|---|---|
| `KERNEL_SEG` | `0x1000` (linear `0x10000`) | `0x0060` (linear `0x00600`) |

Every `OSAPI_*` symbol is `%define OSAPI_X KERNEL_SEG:0xNNNN`, so `call OSAPI_X`
assembles to a far call with that segment as an immediate. A `main` binary run on
`experimental` calls into `0x1000:...`, which is somewhere above the kernel
entirely.

The constant appears in three files per branch — `kernel/kernel.asm`,
`boot/boot.asm`, `apps/os88api.inc` — and changing it requires rebuilding every
package and both apps floppies.

### 2.2 The slot numbers — no longer a break

They agreed for a while, once `experimental` renumbered onto `main`'s table,
and they no longer do above `0x01B0` (§3). This is the subtlest of the three
breaks, because the collisions are *silent*: `0x01D0` means `WM_RESIZE` on
`main` and `FILE_READBIG` here. Both forks obey the rule that a number never
means two contracts WITHIN a tree; neither promises it across two.

### 2.3 The callback mechanism

`main` far-calls your procedure; it must `retf`. `experimental` far-calls a
dispatcher in your header which calls your procedure *near*; it must `ret`. A
binary built for either one, run under the other, returns to the wrong place on
the first callback. See §4.

---

## 3. The API slot table, side by side

Slots `0x0010`..`0x00F0` are **identical on both branches**:

```
0x0010 GFX_LOCK      0x0038 GFX_FILL       0x0060 FONT_CHAR    0x0090 WM_FRONT
0x0018 GFX_UNLOCK    0x0040 GFX_FRAME      0x0068 FONT_STR     0x0098 WM_CONTENT
0x0020 GFX_PIXEL     0x0048 GFX_FILL_GRAY  0x0070 FONT_WIDTH   0x00A0 WM_OBSCURED
0x0028 GFX_HLINE     0x0050 GFX_XOR_RECT   0x0078 WM_CREATE    0x00A8 TASK_YIELD
0x0030 GFX_VLINE     0x0058 GFX_XOR_FILL   0x0080 WM_SHOW      0x00B0 TASK_SLEEP
                                            0x0088 WM_HIDE     0x00B8 GET_TICKS
0x00C0 SET_COLOR     0x00D0 SRAND          0x00E0 SND_CAPS     0x00F0 SND_PLAY
0x00C8 MOUSE         0x00D8 RAND           0x00E8 SND_TONE
```

Everything from `0x00F8` to `0x01B0` **still agrees**, routine for routine.
Above that the two tables have parted: `experimental` used to hold five cells
empty so that a number could never mean two contracts, and closed those holes
when the branches began merging — so its own block sits 88 bytes lower than
`main`'s numbering would put it.

| slot | `main` | `experimental` |
|---|---|---|
| `0x00F8` | `SND_FM` | `SND_FM` — refused unless the sound driver is loaded |
| `0x0100` | `SND_STREAM` | `SND_STREAM` — likewise |
| `0x0108` | `WM_SIZABLE` | `WM_SIZABLE` |
| `0x0110` | `FULLSCREEN` | `FULLSCREEN` |
| `0x0118` | `WM_GROW` | `WM_GROW` |
| `0x0120`–`0x0140` | `FILE_WRITE`/`READ`/`DELETE`/`RENAME`/`DFREE` | identical |
| `0x0148` | `MENU_SET` | `MENU_SET` |
| `0x0150` | `FILE_DLG` | `FILE_DLG` |
| `0x0158` | `VIDEO` | `VIDEO` |
| `0x0160` / `0x0168` | `TASK_SPAWN` / `TASK_ALIVE` | identical |
| `0x0170`–`0x0180` | `WM_CLIP_SET`/`CLEAR`/`TEST` | identical |
| `0x0188` | `CPU_INFO` | `CPU_INFO` |
| `0x0190`–`0x01A8` | `XMEM_CAPS`/`ALLOC`/`FREE`/`COPY` | identical |
| `0x01B0` | `WM_GEOM` | `WM_GEOM` — **the last shared number** |
| `0x01B8` | `MEM_ALLOC` (paragraphs) | `WM_RESIZE` |
| `0x01C0` | `MEM_FREE` (`AX`) | `GFX_BLIT4` |
| `0x01C8` | `MEM_AVAIL` (paragraphs) | `ABOUT_SET` |
| `0x01D0` | `WM_RESIZE` | `FILE_READBIG` |
| `0x01D8` | `GFX_BLIT4` | `GFX_DBUF` |
| `0x01E0` | `ABOUT_SET` | `GFX_SCROLL` |
| `0x01E8` | `FILE_READBIG` | `MEM_CLAIM` (KB) |
| `0x01F0` / `0x01F8` | `GFX_DBUF` / `GFX_SCROLL` | `MEM_FREE` / `MEM_AVAIL` (KB) |
| `0x0200`+ | — | `FONT_GLYPHS`, `WM_ONSIZE`, `FILE_HERE`, `FILE_GOTO`, `MEM_REGROW`, `WM_TITLE`, `DRV_TASK` (drivers only), `MEM_CLAIM_DMA` |

**Every contract `main` has, `experimental` now has too** — `GFX_DBUF` and
`GFX_SCROLL` were its last two gaps and are implemented, just at different
numbers. What differs is the memory API, and deliberately: `main` counts
paragraphs and answers in `AX`, `experimental` counts KB and answers in `DX`.
Putting those at one number would fail silently and by a factor of 64. That
is still the rule the layout follows — **a slot number never means two
different contracts** — it just no longer extends across the two trees.

**So a package binary is not portable between the branches, and never was.**
Re-assembling it is, and that is all that was ever claimed: the numbers live
in one `%include`d file on each side.

You never write these numbers by hand — `%include "os88api.inc"` supplies them —
so re-assembling against the target branch's SDK fixes every one. The table is
here for reading disassembly and for judging whether an unported binary could
possibly work (it cannot).

---

## 4. The callback convention

### 4.1 What the kernel does

**`main`** builds a far pointer to your procedure directly (`inst_cb_call`,
`inst_cb_seg`) and `call far`s it. Your procedure is the far target.

**`experimental`** builds a far pointer to `PKG_DISP` = offset **12** in your
package header, loads `BP` with the near offset of your procedure, and
`call far`s the header. The four bytes there are:

```
+12  FF D5     call bp
+14  CB        retf
+15  00        pad
```

So the dispatcher calls you near, you `ret` to it, and it `retf`s to the kernel.
`wm_pkgcall` is the single site; dispatch is re-entrant across packages because
the far pointer comes out of the window record (`W_DISP`/`W_SEG`), not a global.

### 4.2 What you write

Every routine the kernel calls — the entry proc, `W_PAINT`, `W_ONKEY`,
`W_ONCLICK`, the `AM_ONCMD` menu handler, the file-dialog completion proc, and on
`experimental` also the `W_ONSIZE` negotiator:

| | `main` | `experimental` |
|---|---|---|
| returns with | `retf` | `ret` |
| calling one of them yourself | `push cs` then `call` | plain `call` |

The worker task entry is the exception on both branches: it never returns at all.

**`main` → `experimental`:**

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

Going the other way, add the `retf`s **and** the `push cs` before every internal
call to one of them. Forgetting the `push cs` is the failure mode that assembles
cleanly and unwinds the stack by two bytes per call.

### 4.3 Segment registers on entry

| | `main` | `experimental` |
|---|---|---|
| `CS` | your segment | your segment |
| `DS` | your segment | your segment |
| `ES` | your segment | **`KERNEL_SEG`** |
| `SS` | `LOW_SEG` (`0x0800`) | `LOW_SEG` (`0x0440`) |

`SS != DS` on both, so `[bp+disp]` addresses `SS` and a pointer held in `BP`
needs an explicit `ds:` override — unchanged, and true inside a worker task too.

The `ES` difference is the one to internalise. On `experimental`, `ES` already
points at kernel memory when you are called, which is what makes `[es:bx+W_W]`
and the file dialog's `DI` name buffer work without a segment load. You may
clobber `ES` freely afterwards, but if you do, reload it before touching the
window record again. On `main`, `ES = DS`, and the two ES-relative APIs
(`OSAPI_FILE_*` data buffers, `OSAPI_SND_PLAY`) therefore read your own segment
with no setup — which is also true on `experimental` *once you set `ES = DS`
yourself*.

---

## 5. The package header

`OS88_HEADER` emits 32 bytes. Both branches agree on:

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

| | `main` | `experimental` |
|---|---|---|
| `+12` | `dw 0` — retired v2 relocation count | `db 0FFh, 0D5h, 0CBh, 0` — the dispatcher |
| `+14` | `dw` minimum conventional RAM, **KB** | — |
| macro arity | `OS88_HEADER name, entry [, flags [, needs_kb]]` | `OS88_HEADER name, entry [, flags]` |
| default | 500 KB if omitted | n/a |
| image + bss cap | 65,520 B (`PKG_MAX_PARA` × 16) | 61,440 B (`APP_MAX_SIZE` = `0xF000`) |
| `os88pkg.py` | `--needs KB` overrides `+14`; validates 256–640 | validates the four dispatcher bytes |

`main` uses the `+14` word to refuse a load on a machine with less RAM than the
package declares (`ld_needkb` / `ld_ramkb`). `experimental` has no such gate — an
app sizes itself at runtime from `OSAPI_MEM_AVAIL` and puts up a notice window if
the answer is too small, which is what Paint does.

**`main` → `experimental`:** drop the fourth macro argument, drop any `--needs`
from the Makefile rule, and add the runtime sizing if the app relied on the gate.
Check the image against the smaller 61,440-byte cap.

**`experimental` → `main`:** decide a floor and pass it as the fourth argument.

---

## 6. Window geometry

### 6.1 The two idioms

**`main`** — the window pointer is an **opaque handle**. Dereferencing it is
explicitly forbidden; the SDK still publishes `W_*` offsets, but only for the
`wm_create` template.

```nasm
    mov bx, [my_win]
    call OSAPI_WM_GEOM      ; CX = CONTENT width, DX = CONTENT height
    jc  .hidden             ; CF = 1: the window is not visible
```

**`experimental`** — the record is kernel memory and `ES` points at it:

```nasm
    mov bx, [my_win]
    mov ax, [es:bx+W_W]     ; FRAME width
    sub ax, 2               ; -> content width
    mov dx, [es:bx+W_H]     ; FRAME height
    sub dx, TITLE_H+1       ; -> content height
```

Visibility is `test word [es:bx+W_FLAGS], 2` on `experimental`; on `main` it is
the `CF` that `OSAPI_WM_GEOM` returns.

### 6.2 The conversion

```
content width  = W_W - 2
content height = W_H - TITLE_H - 1
```

Getting this wrong is a silent off-by-19-pixels that only shows on a resize, so
convert once into a helper and call it everywhere — which is what
`apps/fractal/fractal.asm` does on both branches.

### 6.3 Record layout

| | `main` | `experimental` |
|---|---|---|
| `W_FLAGS` … `W_MENUS` | 0 … 18 | 0 … 18 (identical) |
| `W_DISP` / `W_SEG` | — | 20 / 22 |
| `W_ONSIZE` | — | 24 |
| `WIN_SIZE` | 20 | 26 |

The `wm_create` template (`WT_X` … `WT_SIZE` = 16) is **identical on both
branches**, so the template you pass to `OSAPI_WM_CREATE` needs no change.

### 6.4 Resizing

`OSAPI_WM_RESIZE` exists on both with the same contract (`BX` = window, `CX`/`DX`
= new **frame** size; do not hold the gfx lock). `OSAPI_WM_SIZABLE`,
`OSAPI_WM_GROW` and `OSAPI_FULLSCREEN` likewise.

`OSAPI_WM_ONSIZE` is **`experimental` only**. It registers a near proc the kernel
calls *before* committing a size the user dragged out of the grow box, with
`SI` = your window and `CX`/`DX` = the proposed frame size; you answer in
`CX`/`DX` with the size you will accept. Answer with the size you already have to
refuse outright, or change one axis and leave the other. It runs under the gfx
lock with nothing drawn at either size — **do not draw in it**.

Porting an app that uses it to `main` means giving up the negotiation: let the
resize happen and clamp or reflow in `W_PAINT` instead.

---

## 7. Memory

### 7.1 Your own segment

Both branches give a package one segment holding its image and bss, loaded on a
paragraph boundary, with no relocation of any kind. `main` allocates it from a
conventional **arena** running from linear `0x65800` to whatever `int 12h`
reports; `experimental` allocates it from the **claim heap** (SPEC.md §50), from
the top downward, while data claims grow up from the bottom. Neither is visible
to package code.

`experimental` had a fixed 60KB pool of its own until the heap absorbed it. The
practical difference now: `main`'s arena starts at a constant, so it is **empty
on a 256KB machine** and package loads refuse with a message; `experimental`'s
heap starts where the kernel actually ends, so it is ~187KB on a 256KB machine
and ~59KB on a **128KB** one — which is the floor `experimental` targets.

### 7.2 Asking for more

| | `main` `OSAPI_MEM_ALLOC` | `experimental` `OSAPI_MEM_CLAIM` |
|---|---|---|
| slot | `0x01B8` | `0x0178` |
| unit in | **paragraphs** in `AX` (0 → 1) | **KB** in `AX` |
| result | `CF=0`, `AX` = base segment | `CF=0`, `DX` = base segment |
| failure | `CF=1`, `AX` = 0 no arena / 1 no table entry / 2 no run that big | `CF=1`, no code |
| free | `OSAPI_MEM_FREE`, `AX` = base | `OSAPI_MEM_FREE`, `DX` = base |
| survey | `OSAPI_MEM_AVAIL` → `AX` largest run in **paragraphs**, `DX` total, `BL` free entries | `OSAPI_MEM_AVAIL` → `AX` largest run in **KB**, `BX` total KB |
| table size | 8 entries, whole machine | 16 records |
| context | UI task only | UI task in practice; callable from the entry proc |
| identity | the calling instance | the segment you run in |

Both stamp blocks with an owner and force-free at teardown, so neither needs a
close hook, and on both a refusal is a normal path that every caller must handle.

**Conversion:** paragraphs → KB is `>> 6`; KB → paragraphs is `<< 6`. Watch the
result register — `AX` on `main`, `DX` on `experimental` — and watch that
`OSAPI_MEM_AVAIL`'s second output is `DX` (total paragraphs) on `main` and `BX`
(total KB) on `experimental`.

**Shape:** `main` tells you to take **one** block and subdivide it, because eight
entries serve the whole machine. `experimental`'s 16 records are looser — Paint
takes four separate claims there and one subdivided block on `main`. Either shape
works on `experimental`; only the single-block shape is safe on `main`.

A block larger than 64KB is a paragraph *count*, not an offset, on both: walk it
as `seg:off` with the segment advancing, never as one flat 16-bit offset.

### 7.3 Memory above 1MB

**`main` only**, and it comes with hard rules:

- `OSAPI_CPU_INFO` → `AL` = `CPU_8086` / `CPU_286` / `CPU_386`, `AH` = feature
  bits (A20 verified, HMA claimed, unreal armed).
- `OSAPI_XMEM_CAPS` → `AX` = KB available, `DX:CX` = pool base, `BL` = free
  entries.
- `OSAPI_XMEM_ALLOC` / `FREE` / `COPY`.

**Branch on the caps, never on the tier.** `OSAPI_XMEM_CAPS` returning 0 KB is
the only answer that means "do not try", and it is the answer on a 386 whose A20
gate never verified just as much as on an 8088.

What `ALLOC` returns is an **opaque 32-bit token, not an address**. Real mode
cannot name a byte above `0x10FFEF`, so extended memory is a **data store only** —
no code, no jump table, no callback, no second copy of your image, on any tier.
Every byte moves through `OSAPI_XMEM_COPY` (`CX` even and ≤ 32768). These five
slots are UI-task-only: on a 286 the copy goes through `int 15h AH=87h`, and some
BIOSes implement that by resetting the CPU.

`experimental` has none of it. An app that used extended memory as a scratch
store must either shrink to what the claim heap can fund or give up the feature —
there is no equivalent, and no CPU detection either.

---

## 8. Sound

| capability | `main` | `experimental` |
|---|---|---|
| `SND_CAP_TONE` | yes | yes |
| `SND_CAP_PCM_EXCL` | yes | yes |
| `SND_CAP_FM` | yes (OPL2) | yes (OPL2) — **driver** |
| `SND_CAP_PCM_BG` | yes (Sound Blaster) | yes (Sound Blaster) — **driver** |
| `SND_CAP_PCM_IN` | yes (recording) | yes (recording) — **driver** |
| `OSAPI_SND_CAPS` | `BL` = route (0 speaker / 1 OPL2), `DX` = present drivers | identical |
| `OSAPI_SND_TONE` | identical | identical |
| `OSAPI_SND_PLAY` | identical | identical |
| `OSAPI_SND_FM` | `0x00F8` | `0x00F8` — CF=1 with no driver |
| `OSAPI_SND_STREAM` | `0x0100` | `0x0100` — CF=1 with no driver |
| Control Panel Sound page | yes | yes (source select + Test) |
| where the hardware lives | in the kernel | in a **loadable `.DRV`** (SPEC.md §51) |

`OSAPI_SND_TONE` is worker-task-safe on both: `snd_req_inst` stamps the grant with
the running task's own instance when no callback is being dispatched. Only the
blocking `OSAPI_SND_PLAY` is UI-task-only, and for the different reason that it
freezes the desktop for the length of the clip.

**The one real difference is that here the hardware is optional at runtime.**
On `main` the OPL2 and Sound Blaster code is in the kernel and probes at boot;
here it is `SOUND.DRV`, loaded from the system disk if the Control Panel's
Drivers page says so, and a machine that never loads it reports
`SND_CAP_TONE | SND_CAP_PCM_EXCL` and refuses both slots. The user can also
route tones to the **PC speaker** with a card present (Sound page), which
`OSAPI_SND_CAPS` reports in `BL`.

**`main` → `experimental`:** nothing mechanical. Query `OSAPI_SND_CAPS` and
branch on the bits rather than assuming — that is already the documented rule
on `main`, so a well-written app degrades on its own, and here it has to,
because the answer changes when a driver is loaded or unloaded. `fmtest` and
`sbtest` are ported and are the gate packages for the two tiers
(`make test-snd ADLIB=1 TESTAPPS=build/fmtest.img`, `SB16=1` /
`build/sbtest.img`).

**`experimental` → `main`:** nothing to do. Every slot behaves identically once
the driver is attached; `main` has no equivalent of *not* having it.

---

## 9. Menus and About

The `OS88_MENUSET` / `OS88_MENU` / `OS88_MENUSET_END` macros, the `AM_*` and
`AMENU_*` offsets, `MENU_APPMAX` = 4 and the `MENU_DIS` disabled-item marker are
**the same on both branches**. The command handler is a window callback reached
through the bar on both: UI task, under the gfx lock, billed to your instance,
may draw, may call the file API, must not take the lock, and **must repaint
itself**.

The difference is what the kernel does with your set:

| | `main` | `experimental` |
|---|---|---|
| storage | **copied** at layout time | **read live** through a per-cell segment word |
| items per menu kept | `MENU_ITEMMAX` = 8 | uncapped |
| chars kept / shown | `MENU_STRMAX` = 19 | `MENU_MAXCH` = 24 shown |
| after changing a string | **call `OSAPI_MENU_SET` again** | nothing |

So `main` code that relabels an item — or disables one by repointing it at a
string beginning with `MENU_DIS` — follows the change with a second
`OSAPI_MENU_SET` call. That call is a store and a relayout, draws nothing, and
is safe from inside a menu handler. Carrying it to `experimental` is harmless;
*omitting* it on `main` leaves the bar drawing from a stale copy.

`OSAPI_MENU_SET` preserves every register **and the flags** on both branches, so
it can sit between `OSAPI_WM_CREATE` and the entry proc's return without eating
the `CF` the loader is owed.

**`OSAPI_ABOUT_SET` is `main` only** (slot `0x01E0`). It turns the app's name in
the bar from a plain label into a one-item pull-down carrying `About <Name>`;
picking it calls your handler exactly like `W_ONCLICK`. Porting to
`experimental` means dropping the call and, if the About content matters, moving
it into one of your own menus.

---

## 10. Files, folders and the dialog

Identical on both branches: the five package-visible operations (`WRITE`, `READ`,
`DELETE`, `RENAME`, `DFREE`), all eleven `FERR_*` codes, the name-staging rule
(a name longer than 12 chars is **refused** with `FERR_NAME`, never truncated),
the `ES:BX` data buffer convention, and the rule that names resolve in the
volume's **current directory** rather than the root.

The Standard File dialog is the same shape on both: modal, non-blocking,
`OSAPI_FILE_DLG` with `AL` = `FDLG_OPEN`/`FDLG_SAVE`, `BX` = your window, `DI` =
a completion offset in your segment, `SI` = a default name or 0. The completion
proc runs on the UI task with the gfx lock held after the dialog is gone, with
`AL` = the mode, `SI` = your window and the chosen name at `ES:DI` where `ES` =
`KERNEL_SEG`. **The name buffer is the kernel's and valid for the call only —
copy it.** (`main` states `ES:DI` explicitly because `ES` is otherwise your own
segment; `experimental` says `DI` because `ES` is already `KERNEL_SEG`. Same
buffer, same rule.)

`experimental` adds two slots with no `main` equivalent:

```nasm
    call OSAPI_FILE_HERE    ; out DX = current directory's first cluster
                            ;     BL = the drive. No disk I/O.
    ...
    call OSAPI_FILE_GOTO    ; in  DX = a cluster from FILE_HERE, BL = its drive
                            ; out CF=1 could not list; volume back at the root
                            ; A REMOUNT: real floppy I/O, UI-task only.
```

They exist because the current directory is **one global** shared with every Disk
window and the dialog. Right after the dialog closes it still names the folder
the user picked, so a write lands there — but later it does not, because anything
else that navigated has moved it. An app whose `Save` means "the same place my
`Save As` chose" stores the (cluster, drive) pair beside the name. Note Pad on
`experimental` does exactly this.

Porting such an app to `main`: there is no way to express it. Either re-prompt
with the dialog on every save, or accept that `Save` writes to wherever the
volume currently sits.

The other `experimental`-side file work is kernel-internal and needs no package
change: New Folder in the Save dialog, a movable caret in the name field,
`File > Delete` recursing into a folder, and the Disk window repainting one
status line instead of its whole content.

**Folders on the apps disk itself are `main` only.** `tools/os88disk.py` there
takes a `[DIR:]PKG.o88` argument and the Makefile sorts packages into `APPS/`
and `GAMES/`. On `experimental` the root is flat and its order is pinned in the
Makefile's `APPS` list, because the scripted tests click by row index and new
packages must append at the end.

---

## 11. Worker tasks

**The same on both branches.** An instance may claim one worker task from a
callback:

- `OSAPI_TASK_SPAWN` — `main` `0x0160`, `experimental` `0x0150`
- `OSAPI_TASK_ALIVE` — `main` `0x0168`, `experimental` `0x0158`

The binding rule is identical and is the one most easily got wrong: a worker that
returns or exits on its own **leaks its instance record and its region for the
session**, because the close path then sets a die flag nobody reads. It must call
`OSAPI_TASK_ALIVE` every loop, and that call is where it dies — it does not
return.

The worker's stack is 1536 bytes in `LOW_SEG` on both, so `SS != DS` there too.

On `experimental`, `ES` = `KERNEL_SEG` holds in the worker's frame as well as in
callbacks, which is what lets `apps/fractal` read `[es:bx+W_W]` from its worker.
If you port such a worker to `main`, it must call `OSAPI_WM_GEOM` instead — and
`OSAPI_WM_GEOM` is safe from any context there, lock held or not.

Neither branch permits the file API, `OSAPI_SND_PLAY`, or (on `main`) the
`XMEM`/`MEM` slots from a worker.

---

## 12. Repaint obligations

Both branches: a `W_PAINT` proc draws **content only** and must be a pure
repaint. A menu handler and a file-dialog completion proc must repaint whatever
they drew over, because the kernel does not repaint after they return.

The clip region is the same on both (`OSAPI_WM_CLIP_SET` / `CLEAR` / `TEST`,
16 rects, `CF=1` = draw nothing this frame, dies at the next `OSAPI_GFX_UNLOCK`),
and so is the granularity rule that makes `WM_CLIP_TEST` necessary: **fills clip
per pixel but glyphs clip per whole 8x8 cell**, so anything that erases a rect
and then draws text into it must either erase per cell behind a `WM_CLIP_TEST`
or gate the whole erase-and-draw pair on one.

What `experimental` changes is when the kernel calls you:

- **`W_PAINT` does not run on a wholly covered window.** `wm_covered` seeds the
  §11.3 region arithmetic with the frame rect; if nothing it would write survives,
  the window is skipped entirely. A partly covered window is still redrawn in
  full. An app that used `W_PAINT` as a heartbeat — anything other than a
  repaint — breaks here.
- **Fewer paints overall.** Raising a window repaints that window and the chrome,
  not the screen; hiding, destroying or dragging repaints only the vacated
  rectangle and whatever overlaps it.

Neither change alters what a correct paint proc does. Both are reasons an
*incorrect* one stops getting away with it.

---

## 13. Things that did not change

Do not spend porting effort on any of this — it is byte-identical or
contract-identical across the two branches:

- The whole graphics API, `0x0010`..`0x0058`, and the font API `0x0060`..`0x0070`.
- `OSAPI_WM_CREATE`, `WM_SHOW`, `WM_HIDE`, `WM_FRONT`, `WM_CONTENT`,
  `WM_OBSCURED`, and the `WT_*` template layout.
- `OSAPI_TASK_YIELD`, `TASK_SLEEP`, `GET_TICKS`, `SET_COLOR`, `MOUSE`, `SRAND`,
  `RAND`.
- `OSAPI_VIDEO` and the three-adapter rule: `SCREEN_W`/`SCREEN_H` are VGA
  reference values, the live screen is what `OSAPI_VIDEO` reports, and colours
  reduce to black / white / 50% dither on the mono adapters.
- `OSAPI_GFX_BLIT4` and `OSAPI_WM_RESIZE` contracts (only the slot numbers move).
- The 16 EGA colour indices, `MBAR_H` = 20, `TITLE_H` = 18.
- All eleven `FERR_*` codes.
- The clip region API and its granularity rule.
- Worker-task lifecycle.
- `OS88_ICON16` / `OS88_BSS` / `OS88_IMAGE_END`, and the icon format.
- 8086-only assembly: `cpu 8086`, `-w+error`, no `pusha`, no `push imm`, no
  `shl reg, imm` other than 1, no `movzx`, no 32-bit registers, no `FS`/`GS`.

---

## 14. Checklist: `main` → `experimental`

**Mechanical**

- [ ] Replace every `retf` with `ret`.
- [ ] Remove every `push cs` that precedes an internal call to a kernel-called
      proc.
- [ ] Drop the fourth `OS88_HEADER` argument (RAM requirement) and any `--needs`
      in the Makefile rule.
- [ ] Check image + bss against the smaller **61,440-byte** cap.
- [ ] Add the package to the Makefile's flat `APPS` list — **at the end**, so
      existing row indices in the scripted tests hold.

**Geometry**

- [ ] Replace `OSAPI_WM_GEOM` with `[es:bx+W_W]` / `[es:bx+W_H]` **minus the
      frame** (`-2`, `-TITLE_H-1`).
- [ ] Replace the `CF` visibility answer with
      `test word [es:bx+W_FLAGS], 2`.
- [ ] Make sure `ES` still holds `KERNEL_SEG` at every record read; reload it if
      the code clobbered `ES` in between.
- [ ] Set `ES = DS` before any `ES:BX` file buffer or `ES:SI` sound buffer —
      `main` got this free, `experimental` does not.

**Memory**

- [ ] `OSAPI_MEM_ALLOC` → `OSAPI_MEM_CLAIM`: paragraphs `>> 6` to KB, result
      moves from `AX` to `DX`.
- [ ] `OSAPI_MEM_FREE`: argument moves from `AX` to `DX`.
- [ ] `OSAPI_MEM_AVAIL`: `AX` is now KB, and total moves from `DX` to `BX`.
- [ ] Delete `OSAPI_CPU_INFO` and every `OSAPI_XMEM_*`; redesign whatever used
      extended memory.

**Features with no counterpart**

- [ ] Nothing left in the API surface — `OSAPI_ABOUT_SET`, `OSAPI_SND_FM` and
      `OSAPI_SND_STREAM` all exist here at `main`'s numbers.
- [ ] `OSAPI_SND_FM` / `OSAPI_SND_STREAM` still need the `OSAPI_SND_CAPS`
      branch, because the hardware is a **loadable driver** and may be absent
      at runtime (§8).

**Verify**

- [ ] `make && make test`, then drive it: window opens, paints, menus track,
      close box works, and the app survives being covered and revealed.
- [ ] `make check-images` before committing anything under `build/`.

---

## 15. Checklist: `experimental` → `main`

**Mechanical**

- [ ] Add `retf` to the entry proc, `W_PAINT`, `W_ONKEY`, `W_ONCLICK`, the
      `AM_ONCMD` handler and the file-dialog completion proc.
- [ ] Add `push cs` before **every** internal call to one of those.
- [ ] Add the fourth `OS88_HEADER` argument (minimum conventional RAM, KB,
      256–640) or accept the 500 KB default.
- [ ] Add the package to `APPS_TOOLS` or `APPS_GAMES` in the Makefile, which
      decides which folder it lands in.

**Geometry**

- [ ] Replace every `[es:bx+W_*]` read with `OSAPI_WM_GEOM` — and remember it
      answers **content** dimensions, so delete the `-2` / `-TITLE_H-1`
      arithmetic rather than applying it twice.
- [ ] Replace `test word [es:bx+W_FLAGS], 2` with the `CF` from
      `OSAPI_WM_GEOM`.
- [ ] A worker task that read the record must call `OSAPI_WM_GEOM` instead
      (safe from any context).

**Memory**

- [ ] `OSAPI_MEM_CLAIM` → `OSAPI_MEM_ALLOC`: KB `<< 6` to paragraphs, result
      moves from `DX` to `AX`.
- [ ] `OSAPI_MEM_FREE`: argument moves from `DX` to `AX`.
- [ ] `OSAPI_MEM_AVAIL`: `AX` is now paragraphs, total moves from `BX` to `DX`.
- [ ] **Collapse many claims into one block and subdivide it** — the `main`
      table holds 8 entries for the whole machine.
- [ ] Handle the arena being **empty on a 256KB machine**: `OSAPI_MEM_AVAIL`
      answering zero is a normal, shipping outcome there.

**Features with no counterpart**

- [ ] `OSAPI_WM_ONSIZE` — no negotiation; let the resize land and reflow in
      `W_PAINT`.
- [ ] `OSAPI_FONT_GLYPHS` — no glyph-table access; embed the bitmaps you need
      or draw with `OSAPI_FONT_CHAR`.
- [ ] `OSAPI_FILE_HERE` / `OSAPI_FILE_GOTO` — no way to restore a directory;
      re-prompt with the dialog or write wherever the volume sits.

**Verify**

- [ ] `make && make test`. Pay particular attention to the first callback after
      load: a missed `retf` shows up there and nowhere earlier.

---

## 16. Worked example: `hello`

`apps/hello/hello.asm` is the smallest package in the tree and the whole
`main` → `experimental` diff is four hunks. It is the clearest possible
illustration of §4.

```diff
 ; hl_entry - package entry point
-; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
+; in:  CS=DS = our own segment, ES=KERNEL_SEG, IF=1, gfx lock NOT held
 ; out: BX = window ptr, CF clear (CF set = abort)
 hl_entry:
     ...
 .out:
     pop si
-    retf                            ; far-called by the loader
+    ret

 hl_paint:
     ...
-    retf                            ; far-called W_PAINT
+    ret

 hl_oncmd:
     mov [hl_page], al
     call hl_repaint
-    retf                            ; far-called menu handler
+    ret

 hl_repaint:
     ...
     call OSAPI_GFX_FILL
-    push cs                         ; hl_paint is a kernel-called proc and
-    call hl_paint                   ; returns with retf: give it the CS a
-                                    ; far call would have pushed
+    call hl_paint                   ; SI = window ptr still
```

Three `retf` → `ret`, one `push cs` deleted. Everything else in the file — the
window template, the menu set, `OSAPI_FONT_STR`, `OSAPI_GFX_FILL`, the org-0
`dw` table indexed by page — is unchanged, because none of it crosses a boundary
that moved.

Counting `retf` per app is a quick way to size a port. On `main`: `mines` 7,
`hello` 3, `notepad` 6, `piano` 9, `fractal` 4, `paint` 7, `solitaire` 6,
`arkanoid` 6, `filetest` 2. On `experimental`: **zero, in all of them.**

---

## 17. Feature availability matrix

| Feature | `main` | `experimental` | Porting note |
|---|:---:|:---:|---|
| Near `ret` callbacks via header dispatcher | — | ✓ | §4, mechanical |
| Direct window-record reads (`ES` = `KERNEL_SEG`) | — | ✓ | ↔ `OSAPI_WM_GEOM` |
| `OSAPI_WM_GEOM` (content px, opaque handle) | ✓ | — | ↔ record reads |
| `OSAPI_WM_ONSIZE` resize negotiation | — | ✓ | no equivalent; reflow in `W_PAINT` |
| `OSAPI_FONT_GLYPHS` | — | ✓ | no equivalent |
| `OSAPI_FILE_HERE` / `FILE_GOTO` | — | ✓ | no equivalent |
| `OSAPI_ABOUT_SET` | ✓ | — | fold into a menu |
| `OSAPI_MEM_*` claim/alloc | ✓ paragraphs | ✓ KB | unit + register conversion |
| `OSAPI_XMEM_*` (RAM above 1MB) | ✓ | — | no equivalent |
| `OSAPI_CPU_INFO` (CPU tiers) | ✓ | — | no equivalent |
| Header declares minimum RAM (`+14`) | ✓ | — | size at runtime instead |
| OPL2 / FM (`OSAPI_SND_FM`) | ✓ | ✓ | driver-borne here; check `SND_CAPS` |
| Sound Blaster streams / record (`SND_STREAM`) | ✓ | ✓ | driver-borne here; check `SND_CAPS` |
| Loadable drivers (`.DRV`, SPEC.md §51) | — | ✓ | kernel-side |
| PC speaker tone + exclusive PCM | ✓ | ✓ | identical |
| Menu set copied (needs re-`MENU_SET`) | ✓ | — | extra call is harmless the other way |
| Live menu set, 24 shown chars | — | ✓ | |
| Damage-rect repaint (`wm_paint_dmg`) | — | ✓ | kernel-side; affects `W_PAINT` frequency |
| `W_PAINT` skipped on a wholly covered window | — | ✓ | a paint proc must be a pure repaint |
| RTC ladder (4 rungs) | — | ✓ | kernel-side |
| Folders on the apps disk | ✓ | — | build-side |
| `make check-images` | — | ✓ | build-side |
| FAT16 / `--size 2880` test geometry | ✓ | — | dead code on `experimental` |
| Clip region (`WM_CLIP_SET`/`CLEAR`/`TEST`) | ✓ | ✓ | identical |
| Worker tasks (`TASK_SPAWN`/`TASK_ALIVE`) | ✓ | ✓ | identical |
| `GFX_BLIT4`, `WM_RESIZE`, `WM_SIZABLE`, `WM_GROW`, `FULLSCREEN` | ✓ | ✓ | identical contracts, moved slots |
| Standard File dialog | ✓ | ✓ | identical |
| Three video adapters (VGA / Hercules / CGA) | ✓ | ✓ | identical |
