# `experimental` vs `main` — what diverged

Two forks of os8088 that share history up to **`2558ac0`** ("The same binary on
a 286 and two 386s") and have not been reconciled since. `main` carries five
squash-merged pull requests; `experimental` carries 53 commits. Neither is a
subset of the other, and **a `.o88` package built for one branch will not run on
the other** — see [docs/PORTING.md](docs/PORTING.md) for the conversion.

This file has two halves: [the short list](#the-short-list), which is the set of
facts you need before touching either tree, and [the long
list](#the-long-list), which is the same material with the reasoning attached.

Written from `experimental`. `main` is not modified by this document.

---

## The short list

1. **The package ABI is incompatible in three independent ways.** `KERNEL_SEG`
   is `0x1000` on `main` and `0x0800` on `experimental`; kernel-called
   procedures return with `retf` on `main` and near `ret` on `experimental`;
   and every API slot from `0x00F8` upward has a different number. Any one of
   these alone would break every binary.

2. **Callbacks are reached differently.** `main` far-calls your procedure
   directly, so it must `retf`. `experimental` far-calls a three-byte
   **dispatcher** at `+12` in your package header (`call bp` / `retf`) with
   `BP` = the real target, so every callback stays an ordinary near proc. The
   shipped apps carry **zero** `retf` on `experimental` and 3–9 each on `main`.

3. **The window record is public on `experimental` and opaque on `main`.**
   `experimental` sets `ES = KERNEL_SEG` on entry to every callback and you read
   `[es:bx+W_W]` — **frame** pixels. `main` forbids dereferencing the handle and
   answers `OSAPI_WM_GEOM` instead — **content** pixels. Different accessor,
   different units.

4. **Two different memory models above the kernel.** `main`: a conventional
   *arena* at linear `0x65800`, a paragraph allocator (`cmem.inc`), extended
   memory above 1MB (`xmem.inc`) and CPU tiers (`cpudet.inc`).
   `experimental`: one KB-granular *claim heap* (`memory.inc`) above a fixed
   60KB package pool, and no extended memory or CPU detection at all.

5. **`main` has three sound tiers; `experimental` has one.** `main` ships the
   OPL2 (AdLib) and Sound Blaster drivers, a 64KB `SND_SEG`, a Control Panel
   Sound page and the `recorder`/`sbtest`/`fmtest` packages.
   `experimental` deleted all of it — the PC speaker is the only sink.

6. **`experimental` repaints damage rectangles; `main` repaints the screen.**
   `wm_raise` (§11.4) and `wm_paint_dmg` (§11.5) make show/front/hide/destroy/
   drag cost one window or one rectangle. On `main` those paths call
   `wm_paint_all`.

7. **`experimental` has a four-rung RTC ladder** (§37.1: MC146818 → RP5C01 →
   MM58167 → `int 1Ah`) with an `RTC=` build knob. `main` calls `int 1Ah` and
   nothing else.

8. **Menu sets are copied on `main` and read live on `experimental`.** `main`
   caps at 8 items per menu and 19 characters per string and **requires a second
   `OSAPI_MENU_SET`** after you change any string. `experimental` reads through
   a per-cell segment word, shows 24 characters, and needs no re-registration.

9. **Slots exclusive to one side.** `main` only: `OSAPI_ABOUT_SET`,
   `OSAPI_CPU_INFO`, `OSAPI_XMEM_CAPS/ALLOC/FREE/COPY`, `OSAPI_WM_GEOM`,
   `OSAPI_SND_FM`, `OSAPI_SND_STREAM`. `experimental` only: `OSAPI_MEM_CLAIM`,
   `OSAPI_FONT_GLYPHS`, `OSAPI_WM_ONSIZE`, `OSAPI_FILE_HERE`,
   `OSAPI_FILE_GOTO`.

10. **The apps disk has folders on `main`** (`APPS/` and `GAMES/`, via a
    `DIR:` prefix in the Makefile) and a flat, order-pinned root on
    `experimental`.

11. **SPEC.md section numbers do not agree.** §41 is *CPU tiers / xmem* on
    `main` and *Paint* on `experimental`; §42 is *Paint* on `main` and *the
    claim heap* on `experimental`. §35 (Recorder) is retired on `experimental`.
    Cross-branch citations by number are wrong more often than right.

12. **Build differences.** `experimental` adds `make check-images` and the
    `RTC=` knob. `main` keeps `--size 2880` and the FAT16 test geometry, which
    `experimental` made structurally unreachable by cutting `DSK_FAT_SECS` to
    10.

---

## The long list

### 1. Where the kernel lives, and why every binary cares

`main` runs the kernel at `KERNEL_SEG = 0x1000` (linear `0x10000`), which is
where it has always been. `experimental` measured what was actually below it —
the far-code blob, the FAT snapshot, the task stacks and the disk buffers come
to 30,464 bytes, not the 62,976 that had been set aside — and moved the kernel
down to `0x0800` (linear `0x08000`), handing the 32KB difference to the heap.
The floor is the boot sector at `0000:7C00`, which is still executing while the
kernel's sectors land, so `0x07E0` is the hard limit and a build guard asserts
it.

That constant is baked into **every** far-call target in **every** package,
because `OSAPI_X` is a `%define` of `KERNEL_SEG:offset`. It appears in three
files on each branch — `kernel/kernel.asm`, `boot/boot.asm` and
`apps/os88api.inc` — and moving it means rebuilding every `.o88` and both apps
floppies.

The rest of the ladder follows. `experimental` derives each rung from the one
below it, so nothing above the kernel has a fixed address any more:

| | `main` | `experimental` |
|---|---|---|
| `.fartext` blob | `FAR_SEG` `0x0060` | `FAR_SEG` `0x0060`, `FAR_PARA` = 10,752 B |
| FAT snapshot | `FAT_SEG` `0x0300`, 32 sectors | `FAT_SEG` `0x0300`, 10 sectors |
| task stacks / disk buffers | `LOW_SEG` `0x0800` | `LOW_SEG` `0x0440` |
| kernel image + `.bss` | `0x1000`, whole 64KB window | `0x0800`, capped at `KERN_MAX` = `0xB000` |
| packages | arena from `0x6580` to the `int 12h` top | `PKG_SEG` `0x1300`, fixed 60KB pool |
| everything else | pinned: `SAVE_SEG`, `VIEW_SEG`, `SND_SEG`, `BB_SEG` | `HEAP_SEG` `0x2200`, claimed on demand |

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

`main` closed the window record off. `OSAPI_WM_GEOM` (slot `0x01B0`) answers
`CX` = content width, `DX` = content height, `CF=1` if the window is hidden —
and the old idioms `mov ax,[bx+W_W]` and `test word [bx+W_FLAGS],2` are
explicitly dead.

`experimental` kept the record readable through `ES` and publishes the field
offsets in the SDK. The numbers there are **frame** dimensions, so an app
converts for itself:

```
content width  = [es:bx+W_W] - 2
content height = [es:bx+W_H] - TITLE_H - 1
```

Both branches agree that a resizable window's paint and click procs must lay out
from live geometry on every call, never from a cached value.

### 4. Memory

`main` grew two allocators and a CPU probe, in that order:

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

`experimental` deleted all three and replaced them with **`memory.inc`**, a
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

- **Coming to the front reveals nothing** (§11.4). The window moves up, so for
  every other window the covered area can only grow. `wm_raise` draws the menu
  bar, the dock, the outgoing front window's title bar, and then this window.
  A click on a background window's title bar costs two title bars and the
  chrome; raising a window that is already frontmost repaints no window at all.
- **Going away reveals a rectangle** (§11.5). `wm_paint_dmg` takes an inclusive
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

## Where to go next

- **Porting an application between the branches:**
  [docs/PORTING.md](docs/PORTING.md) — the full slot table, the callback
  conversion, the geometry and memory conversions, and a checklist in each
  direction.
- **The binding contract for either branch:** that branch's own `SPEC.md`.
  Do not cite section numbers across branches; they disagree from §35 up.
