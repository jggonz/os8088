# os8088 memory expansion plan

**Status board for the work that buys the kernel room to grow.** SPEC.md is
the binding contract for what the kernel *is*; this document is the standing
plan for how it gets more space, why each step is shaped the way it is, and
what is deliberately still on the shelf. Steps A, B and C have landed; step D
has not. Read this before assuming the kernel is out of room again.

## The constraint

The kernel runs in one real-mode segment, `KERNEL_SEG` = 0x1000, so 64KB is
the hard ceiling on everything addressable through CS/DS at once. Inside that
ceiling the kernel never had 64KB to spend — the package pool took the top
20KB and task 0's stack the 4KB above it, leaving a 40KB window for image +
`.bss`, enforced by the assertion at the end of `kernel/kernel.asm`.

Before this work (release v1.0.20260728, commit 698b587):

| region                        | bytes  |
|-------------------------------|--------|
| kernel window (0x0000–0xA000) | 40,960 |
| — .text                       | 16,281 |
| — .bss                        | 23,590 |
| — **free**                    | **1,089** |
| package pool (0xA000–0xEFFF)  | 20,480 |
| gap + task 0 stack            |  4,096 |

`.bss` was 59% of the budget and the task stacks alone (11 × 1,536 = 16,896)
were 41% of the entire window. The kernel was not short of segment; it was
spending its segment on scratch that never needed to be addressable by DS.

## After A + B + C

| region                        | bytes  |
|-------------------------------|--------|
| kernel window (0x0000–0xB000) | 45,056 |
| — .text                       | 13,691 |
| — .bss                        |  3,238 |
| — **free**                    | **28,127** |
| package pool (0xB000–0xFDFF)  | 19,968 |
| `.fartext` @ FAR_SEG          |  2,922 |
| `.lowbss` @ LOW_SEG           | 20,480 |

Headroom went from 1,089 bytes to 28,127 — room for roughly 25× the code the
next feature is likely to need. Nothing moved out of conventional memory:
every byte still lives below 0x10000 or inside the kernel segment, so a
256KB machine is unaffected.

---

## Step A — evict `.bss` from the kernel segment ✅ done

Linear 0x00600–0x0FFFF is free on every PC once the boot sector has handed
off: the BIOS data area ends at 0x004FF and the kernel image starts at
0x10000. That is ~62KB nobody was using. Three segments now carve it up
(SPEC.md §2.1): `FAR_SEG` = 0x0060 for far code, `FAT_SEG` = 0x0300 for
the FAT driver's mount-time FAT snapshot, and `LOW_SEG` = 0x0800 for
stacks and disk buffers. `FAT_SEG` was claimed after this step by the
FAT12/16 migration: its snapshot owns linear 0x03000–0x07FFF (16KB used,
4KB reserve), reached through ES only, and size guard 5 in `kernel.asm`
fences the `.fartext` blob below 0x03000 so far-code growth cannot collide
with it. The former gap between the blob and `LOW_SEG` is therefore no
longer free memory.

### A1 — task stacks → `LOW_SEG` (−16,896 bytes)

`sch_stacks` moved to the `.lowbss` section and SS became `LOW_SEG` for
every task including task 0, whose stack now grows down from `STK0_TOP`.
All tasks still share one SS, so a context switch is still nothing but an SP
swap and SS is still absent from the saved frame.

The cost was that **SS ≠ DS**, which changes what `[bp+disp]` means. The
audit found exactly 17 sites using BP as a pointer into a kernel structure
(`apps.inc`, `instance.inc`, `taskmgr.inc` — all indexing `inst_tab`); each
took a one-byte `ds:` override. `wm.inc:325`'s `mov bp, sp` genuinely wants
SS and was correct as written. `vgabb.inc`'s `push ss / pop ds` became
`push cs / pop ds`. No package uses BP-relative addressing at all, so the
SDK was untouched.

### A2 — disk buffers → `LOW_SEG` (−3,584 bytes)

`disk_dir` (1024), `disk_icons` (2048) and `dsk_secbuf` (512) moved to
`.lowbss`. int 13h already writes through ES:BX and needed no help. Readers
go through `dsk_get_dir` / `dsk_get_icon`, which stage a single entry back
into the kernel segment, so every consumer keeps the plain DS:SI pointer it
always had and no drawing or parsing code learned about segments. The
staging buffers cost 128 bytes, against 3,584 recovered.

The FAT driver keeps the same discipline: the boot sector's 0xAA55
signature is the one check made with an `es:` override against
`dsk_secbuf`; the BPB's first 64 bytes are then staged into `dsk_bpb` in
the kernel segment, so the other sixteen validation rules run on plain DS
reads (SPEC.md §18.2).

## Step B — slide the package pool up ✅ done

With task 0's stack gone from the top of the segment, the 4KB gap above the
pool was free. `APP_LOAD_OFF` moved 0xA000 → 0xB000, handing that 4KB to the
kernel window. The pool keeps its place at the top of the segment.

It gave up 512 bytes doing so: an exclusive pool end of 0x10000 is not a
16-bit immediate, so `APP_MAX_SIZE` is 0x4E00 and the pool stops at 0xFE00,
leaving the segment's last half-sector deliberately unused. Net: +4,096 to
the kernel, −512 from the pool.

Packages were already base-relocatable, so this was a constant change — but
it is mirrored in four places that must move together: `kernel/kernel.asm`,
`apps/os88api.inc`, `tools/os88pkg.py` and the Makefile's `-DOS88_ORG` probe
org. It invalidates previously built `.o88` files, whose header records the
link base; rebuild them.

## Step C — far code modules ✅ done (mechanism + two modules)

The step that scales. `kernel/farcall.inc` adds a `.fartext` section
assembled at `vstart=0`, shipped at the tail of the kernel image, and copied
to `FAR_SEG:0000` by `far_init` — kmain's first act. Its code costs the
kernel window nothing.

The trick that makes it free: `.bss` is declared `vfollows=.text`, *not*
following `.fartext`, so `.bss` deliberately overlaps the blob's landing
zone. The blob is copied out before anything writes `.bss`, and `.bss` is
uninitialised by definition, so the same addresses serve both in turn. This
is exactly why `splash.inc` has always kept its state in `.text` rather than
`.bss` — it runs in that same window, before the copy.

Migrated so far: `ctrl.inc` (840 bytes) and `taskmgr.inc` (2,082 bytes).

### Recipe for migrating the next module

1. Check it is cold. Not the boot path (it runs before `far_init`), not an
   ISR (vectors are seg:off into `KERNEL_SEG`), not a hot inner loop.
2. Split the file: data above, code below. **All data stays in `.text`** —
   strings, tables, templates, bitmaps — because far code still reads it
   through DS. Insert `section .fartext` at the boundary and `section .text`
   again before any trailing data.
3. For each entry point the kernel dispatches to by near pointer, rename the
   body (`xx_foo` → `xxf_foo`), emit `FARSHIM xx_foo, xxf_foo`, and end the
   body in `retf` instead of `ret`. Window templates and kind tables keep
   naming the shim, so no dispatch site changes.
4. Rewrite each call out to the kernel as `KCALL routine`, and add
   `FARK routine` to the list in `farcall.inc` if it is not there. A tail
   `jmp` to a routine that never returns becomes `jmp far KERNEL_SEG:...`
   and needs no wrapper.
5. Calls within the module stay near. An indirect near call through a table
   of `.fartext` labels is fine *only* from far code — a near pointer means
   nothing without knowing which CS will run it.

Costs: 6 bytes per shim, 4 bytes per distinct kernel routine called, 2 extra
bytes per call site (in far space, which is free). Both migrated modules
together pay 123 bytes to move 2,922.

### Still worth migrating

`icons.inc` (962), `files.inc` (1,027), `desk.inc` (477), `menu.inc` (734)
— roughly 3KB more, all cold. `splash.inc` (1,006) never can be. Do these
only when the window is actually tight again; each one is churn in a working
module.

---

## Step D — packages out of the kernel segment ⏸ not started

The last big lever, deliberately deferred. It would give the kernel the full
64KB and give every package its own segment instead of a shared 20KB pool.

**What it costs.** Packages stop sharing the tiny model. OSAPI near calls
become far calls; the paint/key/click pointers the kernel stores go from
word to dword; DS has to switch on every crossing of the boundary in both
directions. It rewrites `wm.inc`'s dispatch, `apps/os88api.inc`,
`tools/os88pkg.py` and all three packages.

**What it buys beyond the space.** Relocation gets simpler, not harder:
class 1 (the `call OSAPI_*` rel16 fixups) disappears entirely, and if
packages are assembled at org 0 and loaded on a paragraph boundary, class 0
goes too — a package becomes a flat image plus a segment, with no fixups at
all. `tools/os88pkg.py`'s dual-assembly diff would no longer be needed.

**Do it when** a package needs more than ~19KB, or several large packages
need to be resident at once. Not before: the kernel has 28KB of headroom and
the current packages are under 2KB each.

**Where the segments come from.** Decided in docs/SOUND-PLAN.md: the sound
layer claims `SND_SEG` = linear 0x30000–0x3FFFF (the last fully-free 64KB on
the 256KB floor), and the same SPEC §2 amendment pins the menu save-unders
to 0x20000–0x2FFFF. So on the floor, Step D's per-package segments carve
from that block (shared with the save-under heap); on bigger machines
they can range above BB_SEG at 0x40000 instead. Settled now so the conflict
is not discovered mid-migration.

**Since then that block has lost its top 16KB.** SPEC §2.3's `VIEW_SEG`
(linear 0x2C000–0x2FFFF) holds the file manager's four per-window listing
caches, and §2.2 narrowed `SAVE_SEG`'s pinned extent to 0x20000–0x2BFFF to
make room. So Step D's floor-machine budget is **48KB**, not 64KB, shared
with a save-under heap whose measured high-water is ~11KB. That is room for
roughly two per-package segments where Step D's own trigger is "a package
needs more than ~19KB" — enough once, not twice. If Step D ever needs the
16KB back, the honest move is to cap the file manager at two windows
(`VIEW_SLOTS`, `KD_CAP` and `fm_pool` are one number wearing three hats,
SPEC §29.3), not to overlap the two users. And there is no slack to steal below
0x10000 either: `FAT_SEG`'s snapshot owns linear 0x03000–0x07FFF (Step A),
so Step D — or anything else hunting for low memory — must not assume the
old gap between the `.fartext` blob and `LOW_SEG` is free.

## Rejected

- **Shrinking `SCH_STACK` below 1,536.** Would have saved ~5.6KB with no
  model change, and was the fallback if A1 proved too invasive. A1 saved 3×
  that and the stacks no longer compete with anything, so there is no reason
  to run them tighter.
- **Moving `font_glyphs` (764 bytes) to `LOW_SEG`.** It is read per glyph on
  the drawing hot path; an inner-loop segment override is not worth 764
  bytes while 28KB is free.
- **Overlays paged from floppy.** Far code gets the same win with no disk
  I/O, no eviction policy, and no failure mode.
