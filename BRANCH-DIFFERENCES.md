# `experimental` vs `main` — what diverged, and how it was resolved

**The fork is over.** Two lines of os8088 shared history up to **`2558ac0`**
("The same binary on a 286 and two 386s") and diverged for six squash-merged
pull requests on `main` and a long line of development here. `main` ended at
**`b401eda`** and has been merged into this tree — not mechanically: the merge
kept this tree's files wholesale, and what justifies that is [the parity
audit](#the-parity-audit--everything-main-has-that-this-fork-does-not) below,
which went through `main` capability by capability and found seven things
missing. All seven were ported first.

This file is now the **record of what diverged and what the merge did with each
difference**, which is worth keeping for two reasons: half the differences are
still live design decisions in this tree, and anyone bringing forward work
written against `b401eda` needs to know which of them will bite. That
procedure is [docs/PORTING.md](docs/PORTING.md).

Three parts: [the short list](#the-short-list), the facts you need before
touching this tree; [the long list](#the-long-list), the same material with the
reasoning; and [the parity audit](#the-parity-audit--everything-main-has-that-this-fork-does-not),
the sweep that made the merge safe.

Throughout, **`main` means `main` as it stood at `b401eda`** — a past state,
not a branch someone is still committing to. The present tense in the long list
is the tense it was written in and has been left alone, because the comparisons
are still the reason this tree looks the way it does.

---

## The short list

> **Resynced, then merged.** This tree took back the API slot numbering,
> `OSAPI_WM_GEOM`, `OSAPI_ABOUT_SET`, the §41 extended-memory store and the
> foldered apps disk; then the audit found and closed the last seven gaps; then
> `main` was merged in. What is below is what still *differs*, which is now a
> list of this tree's own decisions rather than a list of things to reconcile.

**ABI — a `.o88` still cannot cross**

- `KERNEL_SEG` is `0x1000` on `main`, `0x0060` on `experimental`.
- Callbacks return `retf` on `main`, near `ret` on `experimental` (via a
  dispatcher at `+12` in the package header).
- Package header `+12`..`+15`: a RAM-requirement word on `main`, the dispatcher
  bytes on `experimental`.
- **Slot numbers agree up to 0x01B0 and part above it.** They agreed
  throughout, with five cells held empty so a number could never mean two
  contracts; the branches are merging, so the holes were closed and this
  fork's block moved down 88 bytes. Every contract `main` has, this fork now
  has — `GFX_DBUF` and `GFX_SCROLL` were the last two gaps — just at
  different numbers above 0x01B0.

**Kernel**

- Conventional memory: arena + `cmem` (paragraphs) on `main`; one claim heap
  (KB) on `experimental`. **Extended memory is now on both**, at the same
  slots.
- Sound: speaker + OPL2 + Sound Blaster on `main`; speaker only on
  `experimental`.
- Repaint: whole screen on `main`; damage rectangles on `experimental`.
- Clock: `int 1Ah` on `main`; a four-rung RTC ladder on `experimental`.
- Menus: set copied, capped, needs re-registering on `main`; read live on
  `experimental`.
- Kernel budget: one 64KB span on `experimental` grew to 70KB to fund the
  resync; `main` has no equivalent single-span rule.

**Application-visible**

- Window geometry: `OSAPI_WM_GEOM` **on both, at 0x01B0**. `experimental` also
  still lets a package read the record directly through `ES` (frame px).
- `main` only: `MEM_ALLOC`/`MEM_FREE`/`MEM_AVAIL` (paragraph-based). `SND_FM`
  and `SND_STREAM` are on both now (the loadable sound driver), as are
  `GFX_DBUF` and `GFX_SCROLL`.
- `experimental` only: `MEM_CLAIM`/`MEM_FREE`/`MEM_AVAIL` (KB-based),
  `FONT_GLYPHS`, `WM_ONSIZE`, `FILE_HERE`, `FILE_GOTO`, `MEM_REGROW`,
  `WM_TITLE`, and `DRV_TASK` in the driver SDK.
- **The file API is identical on both**, including `dskw_readbig`,
  the one op with no 64KB ceiling, and every `FERR_*` code. `experimental`
  additionally recurses into folders on delete (`dskw_rmtree`).

**Build and layout**

- Apps disk: `APPS/` and `GAMES/` folders **on both** now.
- `experimental` adds `make check-images` and the `RTC=` knob; `main` keeps the
  FAT16 / 2.88MB test geometry.
- SPEC.md §0–§44 agree; `experimental`-only material lives at §50+ and in the
  `.90` subsection band.

---

## The long list

### 1. Where the kernel lives, and why every binary cares

`main` runs the kernel at `KERNEL_SEG = 0x1000` (linear `0x10000`), which is
where it has always been. `experimental` measured what was actually below it,
moved the kernel down twice — first to `0x0800`, then to **`0x0060`**, the
first paragraph above the BIOS data area — and made the whole kernel one
contiguous span, buffers and stacks included, held to `KERN_BUDGET` by a single
build guard. The boot sector no longer floors it: `boot/boot.asm` relocates
*itself* out of the landing zone before it reads a sector.

That constant is baked into **every** far-call target in **every** package,
because `OSAPI_X` is a `%define` of `KERNEL_SEG:offset`. It appears in three
files on each branch — `kernel/kernel.asm`, `boot/boot.asm` and
`apps/os88api.inc` — and moving it means rebuilding every `.o88` and both apps
floppies.

The rest of the ladder follows. `experimental` derives each rung from the one
below it, so nothing above the kernel has a fixed address any more:

| | `main` | `experimental` |
|---|---|---|
| far code | `.fartext` at `FAR_SEG` `0x0060` | **retired** — there is no far code |
| kernel image + `.bss` | `0x1000`, whole 64KB window | `0x0060`, first rung of one span |
| FAT snapshot | `FAT_SEG` `0x0300`, 32 sectors | derived, right above the image, 9 sectors |
| task stacks / disk buffers | `LOW_SEG` `0x0800` | derived, right above the FAT |
| the whole kernel | no single-span rule | **one span**, `KERN_BUDGET` = 70KB, guard 1 |
| packages | arena from `0x6580` to the `int 12h` top | `PKG_SEG` = wherever the kernel ends, 60KB |
| everything else | pinned: `SAVE_SEG`, `VIEW_SEG`, `SND_SEG`, `BB_SEG` | the claim heap, above the pool, on demand |

### 2. How the kernel calls into a package

This is the difference that shows up on the first line of every ported file.

On `main`, the kernel far-calls your procedure at your segment, so **every
routine the kernel calls returns with `retf`** — the entry proc, `W_PAINT`,
`W_ONKEY`, `W_ONCLICK`, the `AM_ONCMD` menu handler and the file-dialog
completion proc. A near `ret` there pops only IP and resumes at a garbage offset
inside your own segment. The one thing this costs an author is that calling your
own paint proc internally needs a `push cs` first, to supply the CS a far call
would have pushed.

On `experimental`, the package header carries a three-byte dispatcher at offset
`+12` — `call bp` (`FF D5`) then `retf` (`CB`) — and the window record carries
one far pointer (`W_DISP`/`W_SEG`) aimed at it. The kernel loads `BP` with the
near target and far-calls the dispatcher; the dispatcher calls your procedure
near and `retf`s on its way back. So **a package author never writes `retf`, and
a missing one cannot exist**. `tools/os88pkg.py` enforces the three bytes,
because a package without them would send the kernel into its own data on the
first paint.

The second half is what `ES` holds. `main` gives you `CS = DS = ES =` your own
segment and tells you the window pointer is an opaque handle. `experimental`
gives you `CS = DS =` your segment and **`ES = KERNEL_SEG`**, because the two
things the kernel hands you live there: the window record and the file dialog's
name buffer. `[es:bx+W_W]` is the supported idiom, and `experimental`'s window
record is six bytes longer (`WIN_SIZE` 26 vs 20) for `W_DISP`, `W_SEG` and
`W_ONSIZE`.

> One inaccuracy worth knowing about: `apps/hello/hello.asm` on `experimental`
> documents its entry as `DS=ES=KERNEL_SEG`. The loader actually sets
> `DS =` the package segment and `ES = KERNEL_SEG`; the comment is stale.
> `kernel/loader.inc` is authoritative.

### 3. Window geometry

**`OSAPI_WM_GEOM` is on both now, at slot `0x01B0`**: `CX` = content width,
`DX` = content height, `CF=1` if the window is hidden. On `main` it is the
*only* way — the record is an opaque handle and `mov ax,[bx+W_W]` is
explicitly dead.

`experimental` also kept the record readable through `ES`, and most of its apps
still read it. The numbers there are **frame** dimensions, so an app that does
converts for itself — which is exactly the subtraction `wm_geom` now does once:

```
content width  = [es:bx+W_W] - 2
content height = [es:bx+W_H] - TITLE_H - 1
```

Both branches agree that a resizable window's paint and click procs must lay out
from live geometry on every call, never from a cached value.

### 4. Memory

Extended memory is now on **both** forks, at the same five slots — see §41 in
either SPEC. What still differs is *conventional* memory. `main` grew two
allocators and a CPU probe, in that order:

- **`cpudet.inc`** publishes `[cpu_tier]` (8086 / 286 / 386) and three verified
  feature bits. The tier is information, never permission — code branches on the
  feature bits.
- **`xmem.inc`** manages RAM above 1MB. What it hands out is an **opaque 32-bit
  token**, not an address: real mode cannot name a byte above `0x10FFEF`, so
  every byte crosses through `OSAPI_XMEM_COPY`, which carries one ABI over two
  transports (`int 15h AH=87h` on a 286, unreal mode on a 386). It is a data
  store only — no code, no jump table, no callback, ever. On an 8088 it
  publishes zero KB and does nothing else.
- **`cmem.inc`** is a second claim map over the same conventional arena the
  loader carves package regions from, allocating in **paragraphs**, capped at 8
  entries for the whole machine.

`experimental` took `cpudet.inc` and `xmem.inc` back verbatim, and replaced
`cmem.inc` alone with **`memory.inc`**, a
single claim heap covering every paragraph above the package pool, allocating in
**KB**, capped at 16 records. The kernel is a client too: the menu save-under
and the double-buffer back buffer are tagged claims, which is why the Control
Panel's Display row greys out with "Not Enough Ram" when the heap cannot fund
the buffer right now — open Paint and the row greys, close it and it returns.

Both allocators stamp blocks with the calling instance and force-free them at
teardown, so neither needs a close hook. Both refuse as a normal path. The
differences that bite a port are the **unit** (paragraphs vs KB), the **result
register** (`AX` vs `DX`), and the **shape** — `main` tells you to take one big
block and subdivide it because the table is small; `experimental`'s Paint takes
four separate claims.

### 5. Sound

`main` implements the whole of the original sound plan: the PC speaker for tones
and exclusive PWM clips, an OPL2 driver for FM, and a Sound Blaster driver for
background PCM streams *and recording*, behind a driver table with per-tier route
overrides and a Control Panel page to set them. Packages reach the last two
through `OSAPI_SND_FM` and `OSAPI_SND_STREAM`, and the capability word can carry
five bits.

`experimental` removed the Sound Blaster and OPL2 drivers, the driver table, the
route overrides, the Control Panel page, the 64KB `SND_SEG` those buffers needed,
and the `recorder`, `sbtest` and `fmtest` packages. What is left needs no buffer
at all: `osapi_snd_play` paces samples out of the caller's own `ES:SI`, which is
the shape a play-a-WAV package wants anyway. `SND_CAPS` can only answer
`SND_CAP_TONE | SND_CAP_PCM_EXCL`.

`OSAPI_SND_TONE` behaves the same on both and is worker-task-safe on both;
`OSAPI_SND_PLAY` freezes the desktop for the length of the clip on both.

### 6. Repainting

On `main`, raising a window, showing one, hiding one, destroying one or finishing
a drag all end in `wm_paint_all` — a whole-screen planar dither plus every
visible window's frame and `W_PAINT`.

`experimental` split that into two arguments:

- **Coming to the front reveals nothing** (§11.90). The window moves up, so for
  every other window the covered area can only grow. `wm_raise` draws the menu
  bar, the dock, the outgoing front window's title bar, and then this window.
  A click on a background window's title bar costs two title bars and the
  chrome; raising a window that is already frontmost repaints no window at all.
- **Going away reveals a rectangle** (§11.91). `wm_paint_dmg` takes an inclusive
  damage rect, repaints the desktop dither clipped to it, the drive zones it
  touches, the chrome, and then only the windows that overlap it — or that
  overlap a window already marked below them, since a marked window is redrawn
  whole. A window closing on the left no longer redraws a window on the right.

Two consequences a package can observe. `W_PAINT` does not run on a **wholly
covered** window (`wm_covered`), so a paint proc must be a repaint and nothing
else. And an empty damage rect is legal, meaning "nothing was revealed but the
chrome changed" — which is what closing a task-owned app passes on its second
pass.

### 7. The clock

`main` seeds the system clock from `int 1Ah` AH=02h/04h and writes it back the
same way. That is the last rung of `experimental`'s ladder, not the first.

An XT BIOS implements AH=00h/01h and nothing else, so on a 5150 with an AST
SixPakPlus the BIOS knows nothing about a clock sitting right there; and a BIOS
that implements the two *read* functions may still `iret` out of the two *write*
ones. `experimental`'s `clk_probe` walks four rungs — MC146818 at `70h/71h`,
then RP5C01/TC8521 at `2C0h`, then MM58167 at `2C0h`, then the BIOS — and
`clk_rtc_write` dispatches on `[clk_tier]`. The Control Panel's Date/Time page
names the rung that answered.

Probe order is load-bearing: two different parts live at `2C0h`, and the RP5C01
rung is claimed only when its digits agree with what `int 1Ah` just reported —
one test, no writes — so it runs before the MM58167 rung, which does write. Every
loop is bounded, so a machine where every read is `0FFh` cannot hang the boot.
The `RTC=` build knob exists because QEMU has an MC146818 and nothing else, which
makes the other three rungs untestable otherwise.

### 8. Menus

Both branches give the menu bar to the frontmost window's app, and both use the
same `OS88_MENUSET` macros and the same `MENU_DIS` disabled-item marker.

`main` **copies** your set out of your segment when the bar is laid out, clamped
to `MENU_APPMAX` 4 menus, `MENU_ITEMMAX` 8 items and `MENU_STRMAX` 19 characters.
The copy is why you call `OSAPI_MENU_SET` twice: repointing an item at a
different string changes *your* set, which the kernel is no longer reading, so
you re-register to have the copy remade.

`experimental` reads the set live. `menu_bar` carries a segment word per cell
and `[menu_dseg]` names the dropped one, so a relabelled item takes effect at the
next draw with no second call. `MENU_MAXCH` is 24 display glyphs and there is no
item-count or string-length copy cap. (The per-cell segment matters: with a
single "active app's segment" instead, the System menu's own items got read out
of the package's segment and every one drew as `O8` — the first two bytes of the
package header.)

`main` additionally has **`OSAPI_ABOUT_SET`**, which turns the app's name in the
bar from a label into a real one-item pull-down carrying `About <Name>`.
`experimental` has no equivalent.

### 9. Files, folders and the dialog

Both branches have the seven write operations, the current-directory rule
(`[dsk_cwd]`, not the root), identical `FERR_*` codes and the modal Standard File
dialog.

`experimental` adds:

- **`OSAPI_FILE_HERE` / `OSAPI_FILE_GOTO`** — read and restore the current
  directory as a (cluster, drive) pair. The current directory is one global
  shared with every Disk window and the dialog, so "save to the same place my
  Save As chose" is only expressible if you can store and restore it. Note Pad
  uses exactly this.
- **New Folder in the Save dialog**, a movable caret in the name field, and
  `File > Delete` on a folder removing what is inside it (`dskw_rmtree`).
- **`fm_status_only`** — a Disk window that posted a load repaints one *line*,
  not its whole content.

`main` adds **folders on the apps disk itself**: `tools/os88disk.py` takes a
`DIR:` prefix per package and the Makefile sorts them into `APPS/` and `GAMES/`.
`experimental` keeps a flat root whose order is pinned in the Makefile, because
the scripted tests click by row index.

### 10. Packages, tooling and the build

The `.o88` header is 32 bytes on both branches and agrees on `+0` magic, `+2`
version 3, `+3` flags, `+4` link base 0, `+6` entry, `+8` image size, `+10` bss
size and `+16` the 16-byte name. **Bytes `+12`..`+15` are the whole
disagreement**:

| | `main` | `experimental` |
|---|---|---|
| `+12` | `dw 0` (retired v2 reloc count) | `db FF D5 CB 00` — the dispatcher |
| `+14` | `dw` minimum conventional RAM, KB | *(part of the dispatcher block)* |
| size cap | 65,520 B (`PKG_MAX_PARA` × 16) | 61,440 B (`APP_MAX_SIZE`) |
| `os88pkg.py` | `--needs KB` overrides `+14`; validates 256–640 | validates the four dispatcher bytes |

So `main` lets a package declare how much RAM it needs and refuses to load it on
a machine that has less (`ld_needkb` / `ld_ramkb`). `experimental` has no such
field; an app sizes itself at runtime from `OSAPI_MEM_AVAIL` instead.

`main` also carries a **DMA bounce buffer** in `disk.inc` (`dsk_bounce_in/out`),
because a package's region can land anywhere in the arena and a 512-byte int 13h
transfer may not cross a 64KB physical boundary. `experimental`'s pool is a
fixed 60KB block and does not carry one.

Build-side: `experimental` adds **`make check-images`**, which rebuilds
everything a second time into `build/.check` and compares the ~21 force-added
binaries byte for byte, failing as STALE, ORPHAN or SCRATCH. It also adds the
`RTC=` knob. `main` keeps `--size 2880` and the FAT16 path; `experimental` cut
`DSK_FAT_SECS` from 32 to 10, which puts every FAT16 volume below mount rule 10's
threshold and makes the FAT16 decode structurally unreachable dead code.

### 11. Everything else, briefly

- **Task Manager.** `experimental` gives MEM its own column, groups the process
  list, bills heap claims to the instance holding them, and reports the claim
  heap. `main` reports the arena, the XMS pool and the CPU tier instead.
- **Icons.** `experimental` gives every built-in kind — About, Bounce, Clock,
  Control Panel, Disk, Task Manager — and Note Pad a real icon
  (`inst_ico_*`).
- **`OSAPI_WM_ONSIZE`** (`experimental` only) lets an app *negotiate* a size the
  user dragged out of the grow box: the kernel calls your proc with the proposed
  frame size before committing, and you answer with the size you will accept.
- **`OSAPI_FONT_GLYPHS`** (`experimental` only) hands back the offset of the
  kernel's 8x8 glyph table, for an app that needs a character's bitmap rather
  than a drawn one.
- **`GFX_BLIT4` and `WM_RESIZE` exist on both**, but at swapped slot numbers.
- **Solitaire and Arkanoid exist on both**, with `experimental` carrying two
  later Solitaire fixes (a tableau column keeping its buried backs; not
  redrawing an unchanged card back) and a different Arkanoid paddle-reflection
  behaviour.
- **Paint exists on both**, sized from the respective allocator — one
  subdivided block on `main`, four claims on `experimental`.

---

## The parity audit — everything `main` has that this fork does not

Run against `main` at **`b401eda`** and `experimental` at **`cdc1c59`**, and
re-checked against **`6c1ee99`** — the Sound Blaster tier — after it landed.
That commit closes none of the gaps below (it is a driver, not a kernel
change): `snd_req_inst` is still unqualified, and its one new cell,
`OSAPI_DRV_TASK`, is in the *driver* SDK
(`drivers/os88drv.inc`), not the application table this audit compares.

The question it answers is narrow and one-directional: *what capability, fix or
call does `main` have that we are missing?* Divergences where the two forks do
the same job differently are recorded as such and are **not** gaps.

**Out of scope by instruction:** the Sound Blaster tier and everything that
rides on it — `kernel/sndsb.inc`, `kernel/sndfm.inc`, `apps/sbtest`,
`apps/recorder`, `apps/tracker`, `tools/mkmod.py`, SPEC §45, and the
`xt-sound` / `286-sound` / `386-sound` machines. Another worker owns that port.
One sound-adjacent item is flagged below anyway because it is a kernel
correctness bug rather than a device feature.

**What was compared.** The tracked file inventory; both API tables slot by
slot; the routine inventory of all 19 shared kernel files; the routine *and
user-visible string* inventory of all 10 shared applications; the SPEC section
tree; the Makefile targets; and each of `main`'s six commits since the merge
base `2558ac0`.

### A. The API surface — 2 real gaps

57 slot numbers are shared and **every one carries the same name on both
forks**; there is not a single number meaning two contracts. Five slots existed
only on `main`, nine only here.

| slot | `main` | verdict |
|---|---|---|
| 0x01B8 / 0x01C0 / 0x01C8 | `MEM_ALLOC` / `MEM_FREE` / `MEM_AVAIL` | **Not a gap — a different contract.** `main` counts paragraphs, we count KB; the same number would mean two contracts and fail by a factor of 64. These three were held empty for that reason and are now reused by our own block, which is safe for exactly the reason the holding was needed and no longer is: the trees are merging. |
| **0x01F0** | **`OSAPI_GFX_DBUF`** | **GAP.** Arm/disarm the §32 back buffer from a lock-held callback, so one lock hold is one flush and no erase-then-draw state reaches VRAM. We have the back buffer; a package cannot ask for it. |
| **0x01F8** | **`OSAPI_GFX_SCROLL`** | **GAP.** Vertical scroll blit (§5.5): move an existing rect by ±dy instead of redrawing it. A general primitive — `main`'s Tracker is its first client, not its only possible one. |

**Both are now implemented, and the table has since been compacted.** They
landed first at `main`'s numbers, in the band this fork reserved for exactly
that; then the held and reserved cells were dropped altogether — the branches
are merging, so a number meaning the same thing on both trees stopped being
worth 88 bytes of `stc`/`retf`. Everything above 0x01B0 moved down. `main`'s
0x01F0/0x01F8 are `GFX_DBUF`/`GFX_SCROLL`; ours are at 0x01D8/0x01E0, with the
same contracts. See docs/PORTING.md §4 for the two tables side by side.

### B. Application by application

| app | `main`-only routines | `main`-only strings | verdict |
|---|---|---|---|
| `hello` | — | — | parity |
| `mines` | — | — | parity |
| `piano` | — | — | parity |
| `fractal` | — | — | parity |
| `notepad` | — | — | parity (and we are ahead: §27.2 row signatures) |
| `filetest` | — | — | parity |
| `fmtest` | — | — | parity |
| `solitaire` | — | — | parity (we carry two later fixes) |
| `arkanoid` | `ark_onclick` | — | **GAP (small).** `main` wires `W_ONCLICK`, so a *click* takes the credit panel down. Ours dismisses on a key or a menu pick only; our template's click slot is 0. |
| `paint` | `pt_about`, `pt_about_paint`, `pt_about_tpl`, `pt_ab_1..6`, `pt_ab_center`, `pt_i_about`, `pt_it_app`, `pt_s_abttl`, `pt_s_app` | `About Paint`, `Paint for os8088`, `a bitmap editor for the 8086`, the three credit lines | **GAP.** `main`'s Paint has an About panel; ours has none at all. |
| `paint` | `pt_runend` | — | **Not a gap — we are ahead.** That is `main`'s in-package run-scan blit loop; ours is the same algorithm moved *into the kernel* as `gfx_blit4`, which is one far call instead of hundreds. |
| `paint` | — | `Paint is already running.` | **Not a gap.** A single-instance notice; this fork allows multiple Paints. |
| `paint` | — | `Cut (no RAM)`, `Copy (no RAM)`, `Paste (no RAM)`, `Undo/Redo (no RAM)`, `Save Gif (no RAM)`, `Save as Gif(no RAM)` | **Not a gap — different wording, better mechanism.** We carry `MENU_DIS` + `(NoRam)` variants and `pt_menufix` switches them **both ways** live; `main` relabels one way only. |

The five applications that predate the split (`hello`, `mines`, `piano`,
`fractal`, `notepad`) were re-read line by line against `main`'s deltas since
`2558ac0`: every added line there is v3 ABI conversion — `wm_geom` in place of
a record read, org-0 offsets, `retf` — which this fork made independently. **No
behavioural fix is hiding in them.**

### C. Kernel file by kernel file

| file | `main`-only symbols | verdict |
|---|---|---|
| `kernel.asm` | `osapi_w_*` (11), `osapi_*buf` (7), `osapi_copy_n/_str`, `kernel_far_end` | structural — `main`'s far-call marshalling; we use X/N stubs |
| `instance.inc` | `inst_cb_call`, `inst_cb_call_es`, `inst_cb_seg` | structural — we dispatch through the package's own header stub |
| `wm.inc` | `wm_destroy_seg`, `wm_tbuf`, `wm_wseg` | structural — per-window segment bookkeeping the dispatcher makes unnecessary |
| `menu.inc` | `menu_copy_set`, `menu_kset`, `menu_kitems`, `menu_kstr`, `menu_nbuf` | structural — `main` copies a menu set into kernel buffers; we read it live through `MB_SEG`, which is why `pt_menufix` can relabel without re-registering |
| `loader.inc` | `ld_alloc`, `ld_arena_top`, `ld_ramkb` | structural — arena vs `mem_claim_hi` |
| `loader.inc` | **`ld_needkb` / `LD_ENEED`** | **GAP, but blocked.** `main` refuses a package with the precise `Needs nnnK`. Its v3 header carries a RAM-requirement word at **+12** — the same four bytes this fork spends on the `call bp / retf` dispatcher. Not portable without a header field. |
| `files.inc` | `fm_s_needs`, `fm_s_kaych` | the display half of the above |
| `files.inc` | `fm_is_fmwin` | not a gap — we do the same `W_MENUS` test inline |
| `files.inc` | `fm_full` | not a gap — deliberately replaced by `[fm_tdirty]`, a pointer rather than a flag (§11.92) |
| `disk.inc` | `dsk_dmabuf`, `dsk_bounce_in/out`, `dsk_try` | **not a gap — different solution, and ours is checked at build time.** `main` detects a 64KB DMA-page straddle at run time and stages through a fixed 512-byte buffer. We make it unrepresentable: every int 13h destination is 512-aligned by construction, asserted by guards 6/6b in `kernel.asm`, so a claim base, a package image and a `readbig` destination all start aligned and a one-sector transfer cannot straddle. Costs 512 bytes and a copy less. **If we ever let a package name a non-aligned destination, we need `main`'s bounce.** |
| `clock.inc` | `clk_rtc_read` | not a gap — naming. Our clock is the 4-rung ladder (§37.90); `main` has one rung |
| `ctrl.inc` | `cp_snd*` (10), `cpf_*` (3), `cp_swdir`, `cp_sine` | sound — out of scope |
| `ctrl.inc` | `cp_s_trtc` (`Hardware clock: yes`) | not a gap — we are ahead: our Date/Time page names *which rung* answered |
| `ctrl.inc` | `cp_putu16` | not a gap — utility we have an equivalent of |
| `snd.inc` | `snd_drv`, `snd_route_set`, `snd_pcm_route`, `snd_tone_route`, `snd_pref_*`, `snd_s_opl`, `snd_s_sb` | sound routing — out of scope, and superseded here by §51 loadable drivers |
| `snd.inc` | **`snd_req_inst`'s task qualification** | **GAP — a real latent bug.** See below. |
| `taskmgr.inc` | `tm_cap_arena`, `tm_map_seg`, `tm_memcol_par`, `tm_off2x`, `tm_igp`, `tm_igr`, `tm_put4`, `tm_s_cap/cap2` | structural — paragraph-based memory view vs our KB-based one |
| `taskmgr.inc` | `tmf_task`, `tmf_paint`, `tmf_click`, `tm_ylim_set` | not a gap — naming (`tm_task`/`tm_paint`/`tm_click`/`tm_view_begin`) |
| `taskmgr.inc` | **`tm_txt_xm`, `tm_s_t86/t286/t386`** | **GAP (small).** `main` prints the detected CPU tier — `CPU 8086` / `286` / `386+`. We run `cpudet.inc` and never show the answer anywhere. |
| `vgabb.inc` | **`gfx_scroll`, `osapi_gfx_dbuf`, `bbs_bankcopy`, `bbs_lincopy`** | **GAP** — the implementations behind the two missing slots |
| `ui.inc` | `ui_cbofs` | structural — far-call plumbing |
| `cmem.inc`, `farcall.inc` | whole files | structural — arena grants and far-call thunks, neither of which this model has |

**`snd_req_inst`, spelled out**, because it is the one out-of-scope-looking
item that is genuinely ours to worry about. `main` made `[snd_inst]`
task-qualified: the stamp's low byte is the instance whose callback is running
and the **high byte is the task that stamped it**, and `snd_req_inst` only
honours the stamp when that task is the running one. Ours compares the low byte
alone, so **a worker that pre-empts a foreign callback mid-flight inherits that
callback's instance** — its tone is billed to, and released at the teardown of,
the wrong app. Arkanoid's worker calls `SND_TONE` while UI callbacks run, so
this is reachable today. It is six lines and independent of any device.

`main` applied the same qualification to `inst_pkg_spawn`'s ownership fence.
**That half we do not need**: our fence tests `ES == I_SPTR`, an identity test
on the package's own segment, which is exact where `main`'s stamp comparison
was an approximation.

### D. Documentation

| `main` § | subject | verdict |
|---|---|---|
| §2.5 / §2.6 / §20.7 | the package arena and its grant map | structural — ours is §50 |
| §41.7 / §41.10 | xmem testing and acceptance | we have the code, not the two prose sections |
| §45 (12 subsections) | Tracker | out of scope |
| **§5.5** | `gfx_scroll` | comes with the port |
| **§20.8 Forbidden (binding)** | six binding package rules | **CLOSED.** Written, with the `retf` rule inverted: `main` forbids a near `ret` from a package proc because it far-calls them; this fork *requires* one, because the kernel arrives through the package's own dispatcher and a stray `retf` returns into the loader's stack frame. Rule 4 also had to be rewritten rather than copied — the numbers no longer track `main`'s. |

### E. Recommendation — all seven done

Every gap the audit found has been closed. What each one turned into:

| # | port | outcome |
|---|---|---|
| 1 | `snd_req_inst` task qualification | Done. The stamp carries the stamping task in its high byte and is spent only by that task. `osapi_snd_play` had open-coded the same fallback and now calls the one routine — **10 bytes smaller** than the copy it replaced. |
| 2 | `OSAPI_GFX_SCROLL` + `gfx_scroll` (§5.5) | Done, all three backends. Verified against byte-exact references: the mono path diffed over CGA's bank boundary, the buffered path against the direct one across 291,840 pixels with zero mismatches. |
| 3 | `OSAPI_GFX_DBUF` (§32) | Done. Unlike `main`'s it returns `bb_set`'s CF rather than swallowing it — here arming can also fail on a heap that cannot fund the 150KB claim, and the caller has to see that. |
| 4 | Paint's About panel (§42.1) | Done, but **not** `main`'s design. A second window is never bound to its instance record, so nothing destroys it at teardown while teardown frees the region — the orphan's `W_SEG` then dangles. It is a card on Paint's own content, the shape Solitaire and Arkanoid already use. |
| 5 | CPU tier in the Task Manager (§41.6) | Done, sharing the XMS line. `TM_STRMAX` now takes the max of its two candidate longest lines instead of naming the winner. |
| 6 | Arkanoid `W_ONCLICK` (§44.7) | Done. |
| 7 | SPEC §20.8 "Forbidden (binding)" | Done, six rules. Five apply verbatim; the `retf` rule **inverts** — `main` forbids a near `ret` from a package proc, this fork requires one, because the kernel arrives through the package's own dispatcher. |

Two things changed that the audit did not ask for and the merge made
possible. The **held and reserved API cells are gone** (§A) — 88 bytes of
`stc`/`retf` whose only job was keeping a number free on a branch that is
merging. And SPEC §20.8 rule 4 now says what a slot number does and does not
promise, which is the rule that made those cells exist in the first place.

**Not recommended, with reasons:** the paragraph memory API (a second contract
for the same job), `main`'s DMA bounce buffer (our alignment guard is
stronger and cheaper), `LD_ENEED` / `Needs nnnK` (needs a header field the
dispatcher occupies — revisit only if the header ever gains a spare word),
`fm_is_fmwin`, `clk_rtc_read`, `pt_runend` and `cp_s_trtc` (all cases where
this fork already does the same thing or better).

---

## Where to go next

- **Porting an application between the branches:**
  [docs/PORTING.md](docs/PORTING.md) — the full slot table, the callback
  conversion, the geometry and memory conversions, and a checklist in each
  direction.
- **The binding contract for either branch:** that branch's own `SPEC.md`.
  §0-§44 now mean the same topic on both; `experimental`-only material is at
  §50+ and in the `.90` subsection band, and numbers `main` uses for what this
  fork lacks are held empty (§34.5, §34.6, §41).
