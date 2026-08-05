# os8088 memory expansion plan

**Status board for the work that buys the kernel room to grow.** SPEC.md is
the binding contract for what the kernel *is*; this document is the standing
plan for how it gets more space, why each step is shaped the way it is, and
what is deliberately still on the shelf. Steps A through F have landed. Read
this before assuming the kernel is out of room again — and read
`docs/KERNEL-MEMORY.md`, which is the standing account of where the kernel's
64KB actually goes; this document is the history of how it got there.

## The constraint

The kernel runs in one real-mode segment (`KERNEL_SEG`, 0x1000 then and
0x0800 since step E), so 64KB is the hard ceiling on everything addressable through CS/DS at once. Inside that
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

## After A + B + C + D + E

| region                        | bytes  |
|-------------------------------|--------|
| kernel window (0x0000–`KERN_MAX`) | 45,056 |
| — .text + .bss                | see `make` — measure, do not guess |
| — **free**                    | the rest |
| package pool @ `PKG_SEG`      | 61,440 |
| `.fartext` @ `FAR_SEG`        | `FAR_PARA`×16 = 10,752 |
| FAT snapshot @ `FAT_SEG`      | `DSK_FAT_SECS`×512 = 5,120 |
| `.lowbss` @ `LOW_SEG`         | 9,216, then 6,142 for task 0's stack |
| the claim heap @ `HEAP_SEG`   | everything above, on demand |

Headroom went from 1,089 bytes to five figures, and the package pool from
19,968 bytes inside the kernel's own segment to 61,440 outside it. What
changed most is not any single number: it is that **nothing above the pool
has a fixed address any more**. The four pinned blocks that used to sit up
there — `SND_SEG`, `SAVE_SEG`, `VIEW_SEG`, `BB_SEG`, 278KB of a 256KB
machine's address space spoken for by constants — are gone or are claims
(SPEC.md §50), and the ladder below is derived rung by rung so a size change
slides everything above it with no gaps to lose track of.

---

## Step A — evict `.bss` from the kernel segment ✅ done

Linear 0x00600–0x0FFFF is free on every PC once the boot sector has handed
off: the BIOS data area ends at 0x004FF and the kernel image starts at
0x10000. That is ~62KB nobody was using. Three segments now carve it up
(SPEC.md §2.1): `FAR_SEG` = 0x0060 for far code, `FAT_SEG` = 0x0300 for
the FAT driver's mount-time FAT snapshot, and `LOW_SEG` for stacks and disk
buffers. `FAT_SEG` was claimed after this step by the FAT12/16 migration,
reached through ES only, and size guard 5 in `kernel.asm` fences the
`.fartext` blob below it so far-code growth cannot collide. Step E later
sized all three against what they were measured to use and moved the kernel
down onto them; the addresses in this paragraph are the step-A ones.

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

## Step D — packages out of the kernel segment ✅ done

The last big lever, and it landed with the claim heap rather than before it.
The kernel now has its full `KERN_MAX` budget to itself and every package
owns a segment out of `PKG_SEG`'s 60KB pool.

**What it cost, against what was predicted.** Packages did stop sharing the
tiny model, and the boundary crossings all needed mechanism — but less of it
than this section feared, because two of the three crossings were solved
once each rather than per call site:

- *OSAPI near calls became far calls* — but the SDK turns `OSAPI_X` into a
  `%define KERNEL_SEG:offset`, so `call OSAPI_X` is a far call and **not one
  package call site changed**. The table's cells went 4 bytes to 8 (SPEC.md
  §20.3) and that was the whole of it.
- *The paint/key/click pointers going word → dword* did not happen. The
  record carries **one** far pointer, `W_DISP`/`W_SEG`, aimed at a three-byte
  **dispatcher** in the package's own header — `call bp / retf`. Every
  callback stays a near proc with a near `ret`, so a package author never
  writes `retf` and a missing one cannot exist. It also makes dispatch
  re-entrant across packages for free, because the pointer is read out of the
  record and not a global.
- *DS switching in both directions* is `wm_pkgcall` going in and the table
  cells coming out. What was genuinely new is **data** crossing: the X stub
  family (ES = the caller's DS, for a template / string / fence) and the N
  stub family (stage a file name into the kernel's own buffer), plus the
  standing rule that **ES = KERNEL_SEG on entry to every callback** so a
  package can read the window record it was handed.

**What it bought beyond the space** was exactly what was predicted, and more
of it: relocation did not get simpler, it **disappeared**. Packages assemble
at org 0 and load on a paragraph boundary, so there are no fixups of either
class, no dual assembly, no diff scan, no byte-exact reconstruction check,
and no author rule about whole-word package addresses — and with that rule
went the class of bug where an address folded into a constant assembled
cleanly and relocated wrong. `tools/os88pkg.py` is now a validator, not a
generator.

**Where the segments came from** is not where this section expected. The
plan was to carve them out of a block shared with the save-under heap on a
256KB floor machine, and to worry about `VIEW_SEG` having taken 16KB off the
top of it. None of that survived: `SND_SEG` went with the sound cards,
`SAVE_SEG` and `VIEW_SEG` became **claims** (SPEC.md §50) taken when they
are used, and the pool is simply the paragraph after the kernel's ceiling —
`PKG_SEG` = `KERNEL_SEG` + `KERN_MAX`/16, with the heap starting the
paragraph after the pool. The ladder has no gaps and nothing above the pool
has a fixed address at all. The floor machine's budget is not 48KB shared
with a heap; it is 60KB of pool plus whatever the heap can fund on the day.

**What is left.** `APP_MAX_SIZE` is the whole pool, so a single package may
be 60KB — but the pool is shared, so a package that takes all of it is a
package nothing else can run beside. The next lever, if one is ever needed,
is a second pool segment; nothing today wants it.

## Step E — size low memory, then move the kernel down onto it ✅ done

Steps A–D treated linear 0x00600–0x0FFFF as *somewhere to put things*. Nobody
had asked the opposite question: how much of it is actually used? The answer,
measured rather than argued, is that most of it was not.

The measurement is a 0xCC fill of every low byte at the top of `kmain`, then
the machine driven as hard as it goes — Clock, two Bounces, About, the
Control Panel on both its pages, the Task Manager with a window drag, a Disk
window, the Fractal with its worker task, and Paint saving a GIF into a
folder it created from the file dialog — then `pmemsave` and read the deepest
mark in each region. ISR frames are included, because the tick and mouse
handlers run on whichever stack they interrupt.

| region             | reserved before | high-water | reserved after |
|--------------------|-----------------|------------|----------------|
| `.fartext` blob    | 10,752          | 5,455      | 10,752 (kept)  |
| FAT snapshot       | 12,288 (24 sec) | 4,608 (9)  | 5,120 (10 sec) |
| `sch_stacks`       | 16,896 (11×1536)| 150        | 5,632 (11×512) |
| disk buffers       | 3,584           | 3,584      | 3,584          |
| task 0's stack     | 20,478          | 246        | 6,142          |

The `.fartext` reserve is deliberately **not** cut. Its 5,297 spare bytes are
where the next cold module goes when the kernel segment needs relief again —
worth more than 2.5KB of heap. Everything else was sized to a multiple of the
measurement and the total came to 30,464 bytes, so `KERNEL_SEG` moved from
0x1000 to **0x0800** and the 32,768 bytes between them went to the claim
heap. On the 639K test machine the Task Manager's kernel figure fell from
107K to 75K and the heap rose from 471K to 503K; a 256KB floor machine gains
the same 32KB.

**The floor is the boot sector, not the ladder.** The BIOS loads
`boot/boot.asm` to 0000:7C00 and that code is still executing while the
kernel's sectors arrive — it far-calls the splash at `KERNEL_SEG:0008` after
every one — so `KERNEL_SEG:0000` cannot begin below 0x7E00. 0x0800 is the
first round paragraph that clears it, and guard 8 in `kernel.asm` asserts it.
Three files carry the constant: `kernel/kernel.asm`, `boot/boot.asm` (it is
assembled separately) and `apps/os88api.inc` (it is baked into every
package's far-call targets, so **every `.o88` must be rebuilt** — a package
built against the old value far-calls into empty memory).

**What it cost.** FAT16. `DSK_FAT_SECS` = 10 is below the 16 FAT sectors a
volume must have before it can be FAT16 at all, so mount rule 10 (SPEC.md
§18.2) now rejects the whole class structurally. The 2.88MB test geometry
that existed to give the FAT16 encoding a positive test is gone from
`tools/os88disk.py` and the `filetest-fat16.img` target is gone from the
Makefile; the FAT16 halves of `dsk_next_clus` and `dskw_setfat` stay in the
tree, unreachable. Every geometry this OS boots or builds still mounts with a
sector to spare: 360KB = 2, 720KB = 3, 1.2MB = 7, 1.44MB = 9.

**What to redo before trusting a smaller number.** Guard 3 only catches
`.lowbss` crowding task 0; nothing catches a task stack that outgrows its own
512-byte slice. Re-run the fill probe.

## Step F — the kernel is 64KB, buffers and all ✅ done

Steps A–E steered by the kernel's 64KB **window** — what CS/DS can address —
and treated everything outside it as free. Step F changes what is being
measured: the kernel's whole **footprint**, code and scratch alike, against
64KB of physical memory. `docs/KERNEL-MEMORY.md` is the maintained account of
it; this is what the change was.

Three things fell out of that switch, and the first is the interesting one.

**`.fartext` inverted.** Step B moved cold modules out of the window into a
segment of their own — 5,455 bytes of code, and a 10,752-byte reservation to
land it in. That was a clear win against the window and a 5,297-byte **loss**
against the footprint. It is retired: the modules are ordinary `.text`, and
the shims went with it — three `FARSHIM` stubs, twenty-seven `FARK` wrappers,
`far_init`, and two bytes on each of 91 `KCALL` sites, which is why the image
grew by less than the blob it absorbed.

**"Whatever is left" was hiding the savings.** Step E measured the buffers and
shrank them, and the machine came out exactly the same size. The reason was
`STK0_TOP equ LOW_LIMIT - 2`: task 0's stack was defined as the gap between
the top of `.lowbss` and the kernel segment, so every byte saved below it
simply made that gap bigger. The FAT snapshot gave up 7KB and task 0's stack
grew by 7KB. `STK0_SIZE` is a named constant now (1,024, against a measured
246), and that one edit is what turned step E's arithmetic into memory.

**The boot sector was the floor, so it moved.** With the kernel at 0x00600 and
up to 64KB long, its landing zone covers 0x7C00 — where the BIOS puts the
sector that is still reading it. The sector now copies itself to linear
0x11000 and far-jumps there **keeping its own offset**, so every `org 0x7C00`
label still resolves and only the segment registers change.

| region | step E | step F |
|---|---:|---:|
| `.fartext` reserve | 10,752 | — |
| FAT snapshot | 5,120 | 4,608 |
| `.lowbss` | 9,216 | 9,216 |
| task 0's stack | 6,142 | 1,024 |
| image + `.bss` | 44,549 | 50,176 |
| **kernel footprint** | **75,779** | **65,024** |

`KERN_MAX` is retired with the rest: a fixed ceiling with slack under it is
memory nothing can use. Every base is derived from the measured sizes, so the
package pool starts where this build's kernel ends and the heap after the
pool — and on the 639K test machine the heap went from 503K to 515K.

**One bug came with it, and it is worth remembering.** Making the bases
derived made them arbitrary, and three of them stopped being 512-byte
aligned. int 13h moves one sector per call, which bounds a transfer to 512
bytes but does *not* stop one straddling a 64KB physical boundary — only
starting 512-aligned does. Every base here is an int 13h target, so the
symptom was a **"Disk error" on any save large enough to reach the next 64KB
boundary**: Paint's 63KB BMP hit it on the first try, a Note Pad file never
would. `KIMG_PARA` rounds the image to a whole 512 bytes now and guard 6
asserts the whole ladder. It had held by luck, because every base used to be
a round constant.

## Rejected

- ~~**Shrinking `SCH_STACK` below 1,536.**~~ Rejected at step A1 on the
  grounds that the stacks no longer competed with anything. They did: they
  competed with how far down the kernel segment could move. Done in step E,
  measured rather than guessed — 512 bytes, 3.4× the observed high-water
  mark.
- **Moving `font_glyphs` (764 bytes) to `LOW_SEG`.** It is read per glyph on
  the drawing hot path; an inner-loop segment override is not worth 764
  bytes while 28KB is free.
- **Overlays paged from floppy.** Far code gets the same win with no disk
  I/O, no eviction policy, and no failure mode.
