# LOADALL on a 286 — reading extended memory where it lies

**Status: BUILT where it pays - L0 the field instrument (VIDDISK `L`, run on
the Victory 286, docs/reports/LOADALL-VICTORY286-2026-10-14.md), L1 the hold
decoded in place (SPEC.md 98.3.18.11), L3 XMEM.DRV's LOADALL transport
(SPEC.md 41.13) and L4 the XMS bank decoded in place (SPEC.md 98.3.18.13),
all 2026-10-15. L2 (the fill straight into the bank) and L5-L9 are not
built, and section 10's questions 4 and 7 want a machine nobody has run.**
The rest of this file is the investigation they came from: section 9 is the
wave list with each one's state, and SPEC.md is the contract. The
owner asked whether the 286's undocumented `LOADALL` could give the Video
Player direct access to XMS, because a banked byte today goes
disk -> conventional -> XMS and then XMS -> conventional -> screen: two
copies. This file is the answer, the measurements behind it, and the waves
it would take. The one change made with it is to the instrument:
`tools/os88vidbuf.py` takes `--up` and `--down` now, so the two copies can
be priced apart (section 5).

**The answer in six lines:**

1. **LOADALL can remove BOTH copies on a 286 whose disk HDD.DRV has taken.**
   The drain (XMS -> conventional) goes because the hook decodes a record
   where it lies in XMS, the way SPEC.md 98.3.18.7 decodes out of an EMS
   frame. The fill (disk -> conventional -> XMS) goes because rung 1's
   `rep insw` (SPEC.md 52.1.2) is our own code and can write through a
   LOADALL'd ES - and since SPEC.md 52.1.1 rung 1 takes every drive the BIOS
   also knows on a 286, which is the owner's (VIDDISK: `int13 calls` **0**).
   A disk still on `int 13h` keeps the copy up (section 4.2).
2. **The drain is the copy worth more.** UP happens in the decode's slack;
   DOWN happens in a burst, when the CPU is scarce. Modelled on the owner's
   own `03-640FS.V88` (Gas Gas Gas, made for `286-fast` with a 2,784 KB
   bank), the slowest disk it plays on:

   | bank | today | fill direct | drain in place | both: zero copies |
   |---|---|---|---|---|
   | 2,784 KB | 284-292 | 270-278 | 245-248 | **242-243** |
   | 2 MB | 395-432 | 325-360 | 272-278 | **266-269** |
   | 1 MB | stalls at any rate | stalls | 452 / stalls 0.3 s | **363-401** |

   (KB/s, at the Victory's disk CPU of 0.85-1.0 ms a KB; section 5.)
3. **Swapping XMEM.DRV's `int 15h AH=87h` for LOADALL buys almost nothing
   in bytes a second.** On the owner's 286 the BIOS copy's per-KB cost (0.339
   ms) already IS that board's `rep movsw` (0.338 ms); only its 0.45 ms
   fixed part goes, ~3% at 32 KB. What it buys is an A20 line left open and
   shorter masked windows (section 4.3) - which matter only for a disk the
   fill cannot reach, since on rung 1 the bank stops copying up at all.
4. **A frame cannot be decoded in one masked window.** LOADALL's base lives
   in a hidden register any interrupt destroys, so it needs IF=0 - and the
   Victory decodes this file's frames at median 12 ms, p90 43 ms, worst
   79 ms. The decode has to re-arm at yield points every few milliseconds
   (section 3.2), which is a change to `apps/video/vdec.inc`'s hot loops.
5. **Its fixed table lands on the kernel's API cells.** LOADALL reads 102
   bytes at physical 0x800, which in this OS is `KERNEL_SEG:0x0200..0x0265`,
   cells 0x0204 to 0x0260. Borrowing them inside one IF=0 window and putting
   them back is safe (section 2); it is also a write into the kernel by
   whoever executes the instruction, and that is part of the decision in
   section 6.
6. **Nothing here can be gated automatically.** QEMU has no 286 and MartyPC
   is an 8088. 86Box executes it (its 286 opcode table maps `0F 05` to
   `opLOADALL` with exactly the layout below; its 386s map it to ILLEGAL)
   and has no automation. The Victory 286 is the instrument (section 8).

## 1. What LOADALL is

`0F 05`, 286 only: it loads EVERY register of the CPU from a 102-byte table
at physical 0x000800 - the general registers, the four segment selectors,
IP, FLAGS, the MSW, and, crucially, the **hidden descriptor cache** of each
segment: a 24-bit base, an access byte and a limit. In real mode a segment
register's base is normally `selector x 16` and nothing else, but the CPU
addresses through the CACHE, so a cache loaded with base 0x2A0000 makes
`DS:SI` reach 2.6 MB with no mode switch and no reset. That is how
HIMEM.SYS moved extended memory on a 286 without the BIOS's reset-to-return.

The layout, as 86Box loads it (`src/cpu/x86_ops_misc.h`, `opLOADALL`, read
2026-10-14), which agrees with the published descriptions of the
instruction:

| offset | field | | offset | field |
|---|---|---|---|---|
| 0x06 | MSW (PE cannot be cleared) | | 0x2C | SP |
| 0x16 | TR | | 0x2E / 0x30 / 0x32 / 0x34 | BX / DX / CX / AX |
| 0x18 | FLAGS | | 0x36 | ES cache |
| 0x1A | IP | | 0x3C | CS cache |
| 0x1C | LDTR | | 0x42 | SS cache |
| 0x1E / 0x20 / 0x22 / 0x24 | DS / SS / CS / ES selectors | | 0x48 | DS cache |
| 0x26 / 0x28 / 0x2A | DI / SI / BP | | 0x4E / 0x54 / 0x5A / 0x60 | GDTR / LDT / IDTR / TSS caches |

A cache entry is 3 bytes of base, 1 access byte, 2 bytes of limit. 195
clocks. Everything not wanted changed is written as it already is: the
IDTR must stay base 0 limit 3FFh (`sidt` at setup, a 286 instruction legal
in real mode), the MSW is `smsw`'s answer, and CS:IP is the instruction
after the LOADALL - so it behaves as "set DS's hidden base" and carries on.

**Only a 286.** A 386 executes `0F 05` as an invalid opcode (its LOADALL is
`0F 07`, a different table at ES:EDI) - 86Box's 386 tables say so, and a
386 has unreal mode anyway (SPEC.md 41.4, 0.05 ms a KB). `[cpu_tier]` = 1
is decided by FLAGS' top nibble (SPEC.md 60) and IS a 286. The include still
probes with INT 6 hooked for its one instruction, so a clone that does not
have it refuses rather than hangs.

**A20 must be OPEN**, because the descriptor base goes out on the address
bus as it is and the gate masks line 20 behind the CPU's back. A shut gate
does not fault: it aliases 1 MB down. For a READ that is a wrong picture;
for a write it is conventional memory overwritten, which is why every write
in this plan stays on the checked cell (section 6).

## 2. Where the table lands in this OS

`KERNEL_SEG` is 0x0060 (SPEC.md 2), so physical 0x800 is kernel offset
0x200, and that is **the published API table** - from `osapi_vol_mount_x`
at 0x0204 through `fsx_wait` at 0x0241 to `gfx_pen_cf`, whose cell is the
last six bytes, 0x0260-0x0265. Nothing can be moved out of the way: the
cells are the ABI.

So every LOADALL is a BORROW, inside one `pushf / cli ... popf`:
1. save the 102 bytes;
2. write the table;
3. LOADALL;
4. the work that needs the hidden base;
5. reload DS normally, put the 102 bytes back;
6. `popf`.

It is safe because nothing executes or reads those cells while interrupts
are off - no ISR in the tree far-calls a cell (the hook "may call nothing",
SPEC.md 98.3.18.7) and an NMI on an AT is a parity halt. Step 5 must not use
`rep` with an override (SPEC.md 1 rule 1, `t_asmrules` rule 5): DS is
reloaded first and the copy back is a plain `rep movsw`.

Its cost on the Victory: 306 bytes moved at that board's 0.33 us a byte, the
195 clocks and ~40 instructions of glue - **~0.12 ms a borrow, ESTIMATED**
from VIDBENCH's `movsw 8000 to RAM` row. Section 8's row measures it.

Keeping the table planted across an `sti` would halve that, and is refused:
it would make "no interrupt ever calls cells 0x0204-0x0260" a rule the whole
tree had to keep for ever, and its violation is a jump into a register
table.

## 3. What the hidden base survives

### 3.1 Any segment load ends it

`mov ds, ax`, `pop ds`, an `lds` - each rewrites the cache as
`selector x 16`. Every kernel ISR pushes and pops DS and ES (CLAUDE.md's
register discipline), so **an interrupt anywhere inside the work loses the
base silently**: the next `lodsb` reads conventional memory at the
selector's address. Hence IF=0 for the whole of step 4.

`apps/video/vdec.inc` loads no segment register anywhere (checked: no
`mov ds`, `push ds`/`pop ds`, `lds`, `les`) and the decode is already shaped
for this: `vp_decrec` does `push ds / mov ds, dx / call vd_native / pop ds`
and the decoder reaches its own state through `cs:` overrides - the shape
SPEC.md 98.3.18.7's in-place EMS decode needed too. The LOADALL replaces
the `mov ds, dx`.

### 3.2 A frame is too long for one window

`vp_hook` enters at IF=0 and does `sti` before it draws, on purpose. Run the
model's decode table for the owner's file (`286-fast`, VIDBENCH's Victory):

| | per frame | of a 50 ms period |
|---|---|---|
| median | 12.2 ms | 24% |
| p90 | 43.1 ms | 86% |
| worst | 79.3 ms | 159% |

79 ms masked loses an IRQ0 at the bracket's 20 Hz, sits on a Sound
Blaster's block interrupt for more than a 46 ms block at 11 kHz, and
overruns a 1,200-baud mouse's UART ten times over. So the in-place decode
must **yield**: a check at the head of each list in `vdec.inc` - SI past a
record-byte budget - that saves the decoder's registers into the table,
puts the cells back, opens a `sti / cli` window, and re-borrows. The
precedent for how long a window may be is the copy this replaces: today's
32 KB `int 15h` piece masks **11.3 ms** on the same machine and the bank
ships with it. A budget of ~2 KB of record is ~5 ms at this file's worst
rate (2.6 us a record byte).

That is a change to the most-tuned loops in the player, and PERFORMANCE.md
rule 5 applies: VIDBENCH's frame rows before and after, on the Victory.

### 3.3 The speaker is unchanged

SPEC.md 98.3.18.5 makes no XMS copy at all under the speaker's clock,
because its clock is one interrupt a sample. A masked decode window is the
same problem in a new place, so a speaker play keeps the disk-only path. A
Sound Blaster's DMA does not stop; its block interrupt only waits. **The
Victory's Sound Blaster is the PicoMEM 1.x's, and it works** (both PicoMEM
generations do, the owner, 2026-10-14; docs/FIELD-MACHINES.md), so on that
machine a file with sound plays on the card's clock and takes the bank and
this. One thing is the card's rather than a real one's: the 1.x moves each
DMA block through a multiplexer in its own BIOS that raises the SB's IRQ as
a software interrupt on the HOST CPU (SPEC.md 34.10.1), so a masked window
delays that handler too - section 3.2's window bound is set against the
block it waits behind, and L0 should be run with the card playing.

## 4. The three legs

### 4.1 XMS -> conventional (the drain): REMOVABLE

The hook decodes a record with DS's hidden base at its linear address. A
record is at most 32 KB (63.5 KB BIGSP, SPEC.md 98.1.4.1) and LOADALL's
window is a full 64 KB from any base, so **BIGSP fits** - SPEC.md 98.3.18.7
had to leave it out, the EMS frame being 48 KB at most.

Two consumers, in order of difficulty:
- **The whole-file hold** (SPEC.md 98.3.18): the file is a linear image in
  XMS, so a record at file offset F is at the block's base + F. No ring, no
  mirror, no wrap. Every `vp_xfill` copy goes.
- **The bank** (SPEC.md 98.3.18.2): a FIFO of 32 KB slots. It needs what the
  conventional ring has - a mirror slot after the last so a record across
  the wrap is contiguous (one extra copy a lap of the bank) - and its
  hand-over between ring chunks and bank chunks has to place every record
  in one memory or the other, which is the same enumeration SPEC.md
  98.3.18.7 did for EMS (`vp_addr`, the sound cursor, the layer, the
  palette events).

### 4.2 Disk -> XMS (the fill): REMOVABLE on rung 1

**The first draft of this file said the fill could not go, on the ground
that the owner's drive is the BIOS's. That was wrong** (the owner,
2026-10-14): SPEC.md 52.1.1 has HDD.DRV load at boot when a drive is found
and TAKE a drive the BIOS also knows, on a 286, once LBA 0 reads the same
both ways - precisely because `int 13h` moves a sector with interrupts off
and breaks the speaker and XMS's neighbours. VIDDISK on the Victory reads
`int13 calls, 8 x 32K` **0**. So the owner's stream reaches the drive
through rung 1's `rep insw`, which is our own code.

**The path a stream's bytes take**, all of it existing:
`OSAPI_FILE_READ_SEQ` -> `dskw_read_at_x` with ES:BX the caller's buffer ->
`dsk_xfer`, which since SPEC.md 18.95.1 sends a read that covers a whole
cache chunk STRAIGHT to the caller's buffer -> `DSV_BLK` with DX:BX ->
`hd_xfer_ide`'s `mov es, dx` and `rep insw`. Nothing between the caller and
the instruction copies.

**So the fill needs no 32-bit address threaded through the file layer.**
It needs HDD.DRV told, for one call, "a transfer into THIS buffer goes to
THAT linear address instead":
- **ARM** (a verb on HDD.DRV's package door - `DSV_PKGCALL` is published 0
  today, and `drv_pkg_call_x` already routes the disk class to it, so this
  is **zero kernel bytes**): the bounce slot's segment and length, and the
  XMS linear address its first byte stands for.
- **In `hd_xfer_ide`'s read loop**, a sector whose destination falls inside
  the armed range is read under a borrow (section 2): `cli`, the table with
  ES's cache at the linear address, LOADALL, `rep insw`, ES and the cells
  put back, `popf` - and its bit set in a 64-bit map of the slot's sectors.
- **DISARM** answers the map. The package copies UP only the sectors whose
  bit is CLEAR - normally none.

**Why the map, and why the bounce is still claimed**: the destination the
layers carry is the real bounce slot, so anything that does not go through
the armed driver still lands somewhere safe and correct. A sector served out
of the kernel's sector cache (a short probe read it earlier, SPEC.md 18.95)
is copied there by the kernel; a disk that is still on rung 0, a machine
whose probe refused LOADALL, a driver that does not know the verb - all
leave the map empty, and the package copies the slot up as it does today.
A sentinel segment would have been a WRITE through an address nothing
owns. The map makes the result exact whichever way each sector went.

**The masked window**: `rep insw` is 226 us a sector with interrupts off
(SPEC.md 52.1.2), and a borrow ~120 us, so one sector a borrow saves only
~50 us of the 172 the copy up costs. The borrow has to span a RUN: after a
sector, poll the status for the next DRQ for a bounded time (the drive is
mid-command and usually has it buffered) and stay inside the same borrow if
it comes, up to ~8 sectors - ~1.9 ms masked, under the 2.8 ms of the 8 KB
`int 15h` piece, and ~0.03 ms a KB of borrow. A DRQ that does not come puts
the cells back, opens the interrupts, waits as the loop does today, and
borrows again. Under the speaker's clock nothing is armed at all
(section 3.3, SPEC.md 98.3.18.5); rung 1's interrupts-on loop is untouched
for every unarmed read.

**Rung 0 stays a copy.** The BIOS computes the address from the ES
SELECTOR, waits for IRQ 14 with interrupts on, and every kernel ISR reloads
ES, so a LOADALL'd ES would not survive the first tick even if the BIOS
addressed through the cache. A disk the driver did not take (a BIOS row on
a drive over 16 heads, an MFM controller's ROM) keeps today's path.

### 4.3 The copy UP: LOADALL against `int 15h AH=87h`

Measured on the Victory (docs/reports/VIDDISK-XMS-286-2026-10-08.md and
docs/reports/VIDBENCH-PVGA286-2026-10-08.md):

| | per KB | fixed a call | 32 KB call | masked |
|---|---|---|---|---|
| `int 15h AH=87h` | 0.339 ms | 0.45 ms (the CPU reset) | 11.30 ms | all of it |
| `rep movsw` RAM to RAM | 0.338 ms | - | - | - |
| LOADALL, 8 KB pieces | 0.338 ms | ~0.12 ms a piece, ESTIMATED | ~11.3 ms | ~2.8 ms a piece |
| **MEASURED 2026-10-14**, `int 15h` | 0.257 ms | **0.41 ms** | 8.62 ms | all of it |
| **MEASURED 2026-10-14**, LOADALL | 0.257 ms | **0.14 ms** (a bare borrow 0.17) | 8.34 ms | the piece |

(The last two rows are VIDDISK `L` on the same machine six days on; the
board copies a quarter faster than it did, both transports alike, and
LOADALL saves 3% at 32 KB, 11% at 8 KB, 19% at 4 KB and **43% at 1 KB** -
the small copy a canvas in XMS is made of.)

The BIOS's copy already runs at the board's memory speed. LOADALL takes
the reset away and costs a borrow per piece instead, so at the same piece
size it is ~3% cheaper and at a quarter of the masked window it is level.
**Its value is not speed**:
- **A20 stays open.** Many AT BIOSes close the gate on the way out of a
  block move. XMEM.DRV opens it once at attach (SPEC.md 41.2) and never
  looks again - correct today, since every transport opens its own way. A
  bank decoded in place between copies UP needs the gate open at every
  frame; a transport that never calls the BIOS never closes it. Whether the
  Victory's BIOS closes it is unknown, and section 8's row asks.
- **Shorter masked windows**, at no cost in bytes a second.
- **No reset path** - a 286 BIOS's return-by-reset has been the field's
  least-understood part of XMS (SPEC.md 41.8 has the paragraph).

It lives in `drivers/xmem/xmem.asm` beside `xm_bios` and `xm_ucopy`, the
same ABI (SPEC.md 41.5), `int 15h` kept as the fallback when the attach
probe fails. **No kernel byte.**

## 5. What it buys - the owner's file, modelled

`tools/os88vidbuf.py` plays the file's real super-packets through a model
of the reader on a model of the machine (VIDEO-XMS-PLAN 3.1). `--down` is
new: the cost of a copy out of the bank, separately from `--up`, the copy
in. A drain decoded in place is `--down` of the borrows it costs: ~0.12 ms
a borrow, a borrow every frame and every ~5 ms of decode, is ~10 ms a
second at this file's rate, so **0.035 ms a KB** (the 0.01 row is the
borrows alone and agrees within 7 KB/s). A fill read straight into the bank
is `--up` of ITS borrows: one per run of up to 8 sectors, **0.03 ms a KB**
(section 4.2) - the `rep insw` itself is already in `--pio`, which is the
disk's CPU whichever memory it lands in.

`03-640FS.V88`: Gas Gas Gas, VGA4 640 x 400 at 20 fps, 70.7 s, 256 KB/s
mean, made with `--profile 286-fast --bank auto` (2,784 KB) `--prefill all`.
`--profile 286-fast --ring 8 --dcache 64`, the slowest disk that plays (KB/s),
at the Victory's disk CPU (0.85 and 1.0 ms a KB bracket VIDDISK's curve):

| bank | today: up 0.345, down 0.345 | fill direct: up 0.03 | drain in place: down 0.035 | both: up 0.03, down 0.035 | drain in place, LOADALL copy up 0.342 |
|---|---|---|---|---|---|
| none | stalls 0.7 / 1.3 s at any rate | same | same | same | same |
| 1 MB | stalls 0.2 / 0.5 s at any rate | stalls 0.1 / 0.4 s | 452 / stalls 0.3 s | **363 / 401** | 441 / stalls 0.2 s |
| 2 MB | 395 / 432 | 325 / 360 | 272 / 278 | **266 / 269** | 270 / 274 |
| 2,784 KB (the file's) | 284 / 292 | 270 / 278 | 245 / 248 | **242 / 243** | 245 / 247 |

(Two figures are pio 0.85 / 1.0. 85-93% of the stream passes through the
bank in every row. The last column's down is 0.01.)

**And the 24 fps encode of the same clip**, `GasGas24.V88` (the owner's
command line, section 7; 278 KB/s mean, its auto bank 2,464 KB), the same
way:

| bank | today | fill direct | drain in place | both |
|---|---|---|---|---|
| 1 MB | 464 / stalls 0.3 s | 445 / stalls 0.2 s | 385 / 432 | **344 / 353** |
| 2 MB | 326 / 349 | 283 / 291 | 267 / 269 | **263 / 264** |
| 2,464 KB (the file's) | 286 / 290 | 269 / 277 | 253 / 255 | **251 / 252** |

What it says:
- **On the bank the file was made for, ~15% less disk.** 242 KB/s is BELOW
  the file's own 256 KB/s mean, which only the prefill makes possible.
- **On a smaller bank it is the difference between playing and not.** 2 MB
  is a third less disk; 1 MB goes from CPU-bound to playing at both disk-CPU
  figures only when BOTH copies go.
- **The drain alone is most of it; the fill alone is a fraction.** The copy
  down falls in the bursts and the copy up in the slack, so a fill straight
  into the bank buys 14-72 KB/s where the drain buys 39-154. Together they
  are a little better than the drain alone and are what clears 1 MB.
- **The copy UP's transport moves nothing** - section 4.3's point, in the
  model's units: the third column against the last.
- The Victory streams 736 KB/s idle and 486 at a 50% hook, so on THAT disk
  the file plays today. The gain is the margin, and what section 7 lets the
  encoder spend it on.

Re-derive any of it:

    python3 tools/os88vidbuf.py 03-640FS.V88 --profile 286-fast --ring 8 \
        --minrate --xms 0 1024 2048 2784 --dcache 64 --pio 0.85 \
        --up 0.03 --down 0.035

The model's one approximation runs in the bank's DISFAVOUR: a banked chunk
still waits for ring room before the hook may reach it, which a bank read
in place does not.

## 6. The decision this needs: who executes the instruction

**For the FILL there is no decision**: it is inside `hd_xfer_ide`, system
code that already emits a 186 instruction by hand behind `CPU_286`. The
question is the DRAIN's, whose borrow runs in the hook.

**SPEC.md 1 rule 1 says a package is 8086, "no exception".** LOADALL, SMSW
and SIDT are 286 instructions. HDD.DRV emits `rep insw` by hand behind a
`CPU_286` gate (SPEC.md 52.1.2), so the DRIVER precedent exists; there is
no package precedent. Two shapes:

**A. An app-side include, `apps/os88la.inc`** (`os88gfx.inc`'s shape,
SPEC.md 5.12): probe (INT 6 hooked; a round trip against a signature
written through `OSAPI_XMEM_COPY`), the template built from SMSW/SIDT at
setup, and the borrow as one macro the decoder's yield calls.
- ZERO kernel bytes, zero driver bytes; ~150 bytes of code and 204 of bss
  in each package that includes it (the template and the save area),
  ESTIMATED. The hook is an ISR and may call nothing, and with A it calls
  nothing.
- Costs: SPEC.md 1 rule 1 grows a written exception (the 8087 paragraph's
  shape: a run-time gate, CPU_286 and the probe); a PACKAGE writes 102
  bytes of the kernel's API table and puts them back - which the owner's
  answer on trust (below) makes a non-issue, a package being real-mode code
  that could write there anyway; and the SDK's
  "an XMEM token is OPAQUE - never dereference it" (`apps/os88api.inc`,
  `OSAPI_XMEM_ALLOC`) becomes "on tier 1 it is the 24-bit linear base, and
  os88la.inc may READ through it" - a contract clarified, `abilock.py
  --update` without a bump (SPEC.md 20.2.0.1).

**B. XMEM.DRV executes it**, reached through one new rare kernel cell
(SPEC.md 20.3, ~6-10 resident bytes on every kern_big machine, an 8088's
included): "LOADALL into my continuation with DS based at this linear
address".
- Rule 1 holds for packages; only system code writes 0x800; the template
  lives in an overlay only a 286 loads.
- Costs: resident bytes on machines that can never use them (the reason
  SPEC.md 41.12 made the store an overlay at all); a cell that must be
  callable from an ISR at IF=0, which no cell is today; and a driver IMAGE
  that can move (docs/plans/HEAP-UNPIN-PLAN.md) being entered from inside a
  hook - whether a compaction can be mid-move of that image when IRQ0 runs
  the hook has to be ruled out, not assumed.

**DECIDED (the owner, 2026-10-14), by asking who else it serves**: *"Would
loadall benefit others, like say.. paint keeping its canvas in xms? And if
so, would xmem standardize the access for other users? If so, then xmem. If
everyone is going to have to recreate it anyway, then that would just be
overhead ... sometimes - like gfx_line - they surprisingly make more sense
in each app."* The answer splits the instruction's two uses, and each goes
where that test sends it:

- **The COPY goes in XMEM.DRV, for everyone** (wave L3). It is what a
  canvas in XMS would actually use. Paint's canvas reaches the screen
  through `gfx_blit1`/`gfx_blit4`, kernel code that loads its own segment
  registers with interrupts on, so a canvas above 1 MB still has to come
  down into a conventional band to be drawn - a copy, at the granularity of
  the rows a stroke touched. That is exactly where `int 15h AH=87h` is
  worst: 0.45 ms FIXED a call, so a stroke across 20 rows is ~9 ms of
  resets before a byte moves, and a LOADALL piece is ~0.12 ms. So the
  transport is the shared good: every XMS user - Paint's canvas, the disk
  cloner (SPEC.md 18.99.4), the parts loader, the player's hold and bank -
  gets cheaper small copies, no CPU reset, shorter masked windows and an A20
  line left open, and none of them changes a line. Nobody would recreate it.
- **The IN-PLACE WINDOW goes into the code that runs it, `gfx_line`'s
  shape.** It is only usable by a hot loop that owns its segment registers
  and can run masked in bounded slices: the player's decoder and HDD.DRV's
  `rep insw`. Paint is not one - its reads are the kernel's blitters - and
  neither caller could use a central one anyway: the hook is an ISR that may
  call nothing, and HDD.DRV is mid-transfer. A kernel cell for it would be
  resident bytes on every kern_big machine for two callers that cannot reach
  it. So it is written ONCE and shared as SOURCE - HDD.DRV and the player
  assemble the same include, as WEAVE and LOOM share theirs
  (WEAVE-SPEC §1.2) - and the copy transport in XMEM.DRV assembles it too.

What it costs: SPEC.md 1 rule 1 grows the written exception for the player
(LOADALL, SMSW and SIDT run only behind `CPU_286` and the probe, the 8087
paragraph's shape), and the SDK's "never dereference an XMEM token" becomes
"on tier 1 it is the linear base, and the include may read through it".

**Reads and writes are not equally dangerous, and the design keeps them
apart.** A LOADALL READ that goes wrong - a miscomputed base, a shut gate,
a lost cache - draws a wrong picture. A LOADALL WRITE that goes wrong puts
disk sectors over the interrupt table. So:
- **the drain only READS**, and the include's per-session check reads the
  block's signature through a borrow and refuses in place if it does not
  match, which also catches a gate that has closed;
- **the fill is the one write**, and it is HDD.DRV's, never a package's.
  Before its first borrowed sector it proves A20 open inside that same
  borrow (the wraparound probe, a few instructions) and refuses the run
  if not, leaving the sector to land in the bounce. The armed range must
  lie inside the pool `OSAPI_XMEM_CAPS` describes;
- **OWNERSHIP is the package's word** (the owner, 2026-10-14: *"we trust
  our packages - real mode and all, we have to. And we write them"*).
  `xm_chk` - is this range inside a block THIS instance holds - is behind
  XMEM.DRV's dispatcher and unpublished, and it stays so: ARM takes the
  address it is given, and no kernel slot is added to vouch for it. The A20
  and pool checks above stay, because they guard against an ACCIDENT of the
  machine (a gate a BIOS closed) rather than against the package.

### 6.1 On the HMA build (merged 2026-10-15)

`.cold`, SOUND.DRV, HDD.DRV and NET.DRV run from the HMA on a 286 now
(docs/plans/HMA-PLAN.md, SPEC.md 2.10.7), and three things in it meet this
plan:

- **The XMS pool starts above the HMA.** The 64 KB at 1 MB is the kernel's
  code, so nothing here may treat "the first megabyte up" as free: a borrow
  addresses only a block XMEM.DRV handed out. VIDDISK L already does - every
  extended address it builds comes from its own block (`vk_lxa`) - and its
  A20 probe flips HMA:0510 and back inside one `cli`, exactly as
  `xm_a20_probe` does on every copy now.
- **XMEM.DRV holds the PIC across `int 15h AH=87h` and re-opens A20 after
  it**, because an IBM AT's block move leaves the gate shut and the next
  interrupt would run HMA code through it. A LOADALL copy does not touch the
  gate and runs IF = 0, so the L3 transport needs neither - but it must not
  be reached on a machine whose gate is shut, which `CPU_F_A20` already says.
  The 87h row's fixed part grows by one probe a call on this build.
- **A borrow in code that RUNS FROM THE HMA** - the fill in HDD.DRV (L2), the
  transport in XMEM.DRV if it moves there - loads CS's cache too, so the
  table's CS base must be the running CS's (0xFFFF0 + its offset for
  segment FFFFh), not a constant. `vk_lprep` builds every cache from the
  segment it is given and the template from the running state, so the
  shared include (section 6) carries this for free; it is written here so
  that nobody hard-codes `KERNEL_SEG`.

Run on the merged tree: `tests/picomem.py` (all legs), `tests/vidsnd.py`
on MartyPC, and VIDDISK X under QEMU with the HMA claimed (the block at
1,088 KB) - all clean.

## 7. The encoder's side

`Encoder.set_bank` (`tools/os88venc.py`) charges a banked byte TWO copies
(`dslow = 1 + 2 x xcopy x rate`, VIDEO-OVERAGE-PLAN 3.2): at the file's
468,000 B/s that is 1.315, so while the bank holds anything the encode
budgets ~348 KB/s. One copy is 1.158, ~395 KB/s - **13.6% more picture in
every banked stretch**, for the same disk; and with the fill straight into
the bank as well (section 4.2) there is no copy to charge at all - 1.0,
the whole 457 KB/s, **31.5% more** - on a machine whose disk is on rung 1.

**MEASURED (2026-10-14), on the owner's own command line, and the answer
is NOTHING for this file.** `GasGas.mp4` encoded three times with the
command that made `GasGas24.V88` (below) - the profile charging two copies
a banked byte, one, and none - gives THREE BYTE-IDENTICAL FILES, 25,175,040
bytes, the 278.2 KB/s stream the owner's own file has. **Not one frame of
it is cut by the disk**: 221 frames are cut, 203 by the CPU's per-frame
ceiling and 33 by the 32 KB a record holds, and "cut by the disk 0" - a
468 KB/s disk behind a 2,464 KB bank keeps up with everything this clip
asks of `286-fast`, so what the bank's copies cost the disk is never
charged anywhere. The 13.6% and 31.5% are real only for an encode the DISK
binds: a slower disk, a smaller bank, a lighter CPU ceiling. So L5 is worth
building only behind a file that shows one, and the encoder's own summary
line ("cut by the disk N") says whether it does.

    os88venc --preset vga4 --box 640x400 --start 40 --end 110.7 --fps 24
        --profile 286-fast --disk 468000 --peak 4 --bank auto --prefill all
        --layer-disk 900000 --layer-profile 486-dx2-66 --layer-bank 7040
        --layer-prefill all --rate 11025 --vga4-hold 4 --scene-palettes
        --step-min 12 --vga4-order planes --vga4-hold-score 20
        --screen 640x400 --palette colour16 --clip 2
        --title "Gas Gas Gas - 640*400*16"

**The first attempt, kept because its trap is general: the 13.6% was
arithmetic, and that attempt came back empty too.** `GasGas.mp4` was encoded twice with `03-640FS.V88`'s own options
block (`--profile 286-fast --disk 468000 --avg 4 --peak 4 --bank auto
--pool 2944 --prefill all`, 40.0-110.7 s), once as shipped and once with
the profile's `xcopy` halved - which is exactly one copy in `dslow`. The
two files are the same 10,894,848 bytes with the same summary (0.58% error
as seen, 3.7 s visibly broken), because **in that reproduction the disk
never binds**: 140 KB/s of stream, every cut the CPU's per-frame ceiling
(107) or a record's 32 KB (10), "cut by the disk 0". The owner's file is
256 KB/s on a 640 x 300 canvas with page-ahead, and today's encoder given
its options block makes 640 x 360 with none, so the options block alone
does not reproduce it. The A/B wants the owner's own command line; until
then this section is a prediction.

A file budgeted for fewer copies and played by a player that makes more is
short by exactly that difference in its bursts. **DECIDED (2026-10-14): it
plays anyway and says so with a toast**, the bank-short precedent - but not
`Low memory`, which it is not: `Made for fast XMS` (17 characters, inside
the full screen's 18). The rest of this paragraph is why the file has to
say what it assumed: so the header has to say which it
assumed (98.2.1.3.2's two words, or a third) and the player has to refuse
the in-place budget it cannot honour - or the encoder keeps charging two
until the in-place player is the only one that ships. The header word is
the copies the encode charged (0, 1 or 2) in the zero tail beside the bank
and prefill words (SPEC.md 98.2.1.3.2); 0 there means "nothing said", which
every file made before it reads as - two copies, today's charge.

### 7.1 The copy DOWN is the playback's, and nothing charges it

**The owner's correction, and the A/B that measured it (2026-10-15).** The
section above prices the bank's copies in `dslow`, as DISK time, and that
is right for the copy UP: disk -> conventional -> XMS happens wherever the
reader has room. The copy DOWN is not that. XMS -> conventional is done for
every banked frame, inside the frame's period, ahead of its decode - it IS
playback - and a fully prefilled file with no disk in it at all still pays
it. The encoder charges it nowhere: `cost(rec)` is the decode alone, so
every banked frame is budgeted as though it were played out of conventional
memory.

`GasGas.mp4`, the owner's command line (`--fps 24`, without the `--layer`
half), encoded three ways; the only change is what a frame is charged:

| | the charge a banked frame adds | frames cut | of them, the per-frame ceiling | error as seen | visibly broken | stream |
|---|---|---|---|---|---|---|
| **S** shipped | none | **221** | 203 | 0.26% | 2.1 s | 278.2 KB/s |
| **T** today's copy, honest | 87h at the profile's 0.345 ms a KB | **320** | 298 | 0.35% | 2.4 s | 266.4 |
| **L** drained in place | one borrow (150 us) + 0.075 ms a KB | **261** | 240 | 0.29% | 2.2 s | 275.3 |

"Cut by the disk 0" in all three, and "cut by the CPU's average 0" - every
extra cut is the PEAK. So:

- **Charged honestly, the file cuts 99 more frames (+45%)**, its mean CPU
  191% -> 223% of an 8088. That is the copy down's real price at `--peak 4`.
- **LOADALL in place gives 59 of those 99 back (60%).** The drain is not
  free - a borrow and the read of the record, on a frame that then touches
  it again - which is why L is not S.
- **S plays on the Victory, T's charge included.** The file the owner made
  is the S file, and it plays: the copy the encoder did not charge was paid
  out of slack in `--peak 4`. **So `--peak` was carrying the copy down**, and
  how much more it carries is the leeway question - a file at the old peak
  5 (Bad Carrot) plays too, and the round on `make vidpm`'s floppy (VIDEOD's
  Lead and Dry lines on a real play) is how it is answered rather than
  argued.
- **T is today's charge on a slow boot**: 0.345 ms a KB is the profile's,
  and the Victory has since read 0.214, 0.257 and 0.337 on three boots with
  the bus setting making no difference
  (docs/reports/VIDPM-VICTORY286-2026-10-15.md) - so a good boot sits
  between T and S, and which boot a play gets is not yet understood.

**What it means for the encoder - PROPOSED, not built.** Charge the copy
down per banked frame, at the profile's transport (87h today; the in-place
drain once XMEM.DRV or the player has it, L3/L4), and refit the peak on the
remodelled `286-picomem1` with the leeway measured rather than implied. A
peak that silently pays for a copy is a peak that is too low for every
frame that is NOT banked - the conventional-memory stretches of the same
file are being cut for a cost they never pay.

## 8. Testing: nothing runs it automatically

| | LOADALL 286 | why |
|---|---|---|
| MartyPC | no | an 8088 (and a V20) |
| QEMU | no | no 286 at all, and the tier it reports is 2, so the path is never reached |
| 86Box `286`, `286-video` | **yes** | `opLOADALL` in all four 286 tables, the layout above; no debugger, no automation (docs/TESTING.md) |
| the Victory 286 | **yes** | silicon, an AMD 286 at 16 MHz, 4 MB |

So wave L0 is a FIELD INSTRUMENT before it is anything else, and **it is
BUILT**: VIDDISK's `L` key (`tests/vidbench/viddisk.asm`, `vk_larun`), on
`make vid286`'s floppy with its instructions in `FIELD286.TXT`, saving
`VDLALL.TXT`. `M` is the same run with the CS cache's access byte 9Bh
where `L` uses 93h, so a freeze at the probe costs a reset and not a round
trip. Nothing here can run its 286 half: under MartyPC (an 8088) and QEMU
(a 386) it says `NOT A 286` and saves. Three things in it are design, not
measurement, and carry into the waves:
- **a failed cache load is arranged to be harmless and loud**: the table's
  VISIBLE ES selector is the bench's own buffer, so a write through an ES
  whose cache did not take lands in memory the bench owns, and DS's is 0,
  so a read through a DS whose cache did not take reads the interrupt table
  and fails every comparison rather than passing one;
- **nothing is written through a cache until a read through one has been
  checked** - both aimed 1 KB into the bench's own buffer, below 1 MB, where
  either cache failing to take reads different bytes - and
- **every write through a cache asks the A20 gate first** and is skipped and
  counted when it is shut (`writes skipped, A20 SHUT`), because the rows
  interleave LOADALL writes with `OSAPI_XMEM_COPY`'s `AH=87h`, which some AT
  BIOSes follow by shutting the gate - and a write through a shut gate lands
  1 MB lower, in conventional memory. For the same reason the FILL runs
  first: its pattern goes up by LOADALL and its reference is the disk, so it
  needs no `AH=87h` at all.

It reports:
- the probe: tier, LOADALL present (INT 6 trapped or not), and the access
  bytes it used - **the CS cache's access byte is the one field nobody here
  has verified on silicon** (93h is the reset value for data segments; the
  row tries it and reports);
- **a 32 KB round trip checked byte for byte**: written up through
  `OSAPI_XMEM_COPY`, read back through a borrow, compared - and verified to
  go `BAD` with the base aimed one KB off, as `X` was;
- **A20 after `AH=87h`**: the gate's state read through a borrow right after
  an `OSAPI_XMEM_COPY` - the Victory BIOS's answer to section 4.3's question;
- **the borrow's fixed cost**, PIT-timed: 1,000 borrows of nothing;
- **a copy by LOADALL** at 32, 8, 4 and 1 KB pieces against `X`'s rows;
- **VIDBENCH's frames decoded from XMS** against the same frames from RAM -
  they should be identical in time, and if the Victory's board puts wait
  states on memory above 1 MB, this is where it shows;
- **rung 1 straight into XMS**: 32 KB `READ_SEQ` off STREAM.DAT with the
  driver armed, read back through a borrow and compared with the same chunk
  read plainly - `BAD` with the armed address one sector off - then `R`'s
  ceiling rows armed, and **how many sectors of a run arrived inside one
  borrow** (the DRQ already up when the last sector ended), which is what
  section 4.2's 0.03 ms a KB assumes.

86Box's `286-video` is where a human LOOKS at the row before the Victory
runs it; its numbers are not the machine's (VIDBENCH's mr286 rows are 1.2-
2.5x off the Victory).

## 9. Waves

- **L0 - the instrument - BUILT and RUN** (section 9.1): VIDDISK `L` and
  `M`, section 8. No package, driver or kernel change.
- **L1 - the hold in place - BUILT (2026-10-15, SPEC.md 98.3.18.11)**: on a
  286, a file held whole in XMS plays with no copy. The ring's bookkeeping
  stays and a read notes the chunk's file offset instead of copying it;
  `vp_addr`'s DX is a handle, and fifteen `mov ds, dx` sites are
  `VP_DSDX`/`VP_DSEND`, a borrow each; the decode yields at `vdec.inc`'s
  `VD_YCK` sites, which an 8088 meets as jumps over themselves (+0.34-0.70%
  a frame on MartyPC's 5150, +146 cycles an empty one). VIDEO.O88 +2,130
  bytes, no kernel byte. `tests/vidxip.py` runs it on QEMU through an LA_SIM
  build - a borrow there copies the window instead of executing LOADALL - and
  its window must match the copying play's pixel for pixel, silent and with
  an SB16. The card's line says `XMS in place (LOADALL)`.
  The plan's own text for the wave, as written: the decision in section 6 taken, the include,
  and `vp_decrec` borrowing for a file held whole in XMS (SPEC.md 98.3.18)
  instead of `vp_xfill` copying it down, with section 3.2's yields in
  `vdec.inc`. The linear image needs no mirror and no slot map, so it proves
  the mechanism and the yields on the simplest layout. It does not need L3:
  a held play makes no copies, so nothing closes A20 mid-play, and the
  signature check refuses if the fill's last copy did.
  ESTIMATED 400-600 bytes of `VIDEO.O88`, measured against SPEC.md
  98.3.18.7's +592 for the EMS equivalent.
- **L2 - the fill straight into the bank** (section 4.2): HDD.DRV's
  package door, ARM and DISARM, and the borrowed `rep insw` runs in
  `hd_xfer_ide`; the player arms around `vp_bstep`'s `READ_SEQ` and copies
  up only the sectors the map leaves clear. Zero kernel bytes; the image
  grows inside the 4 KB claim it already takes if it fits there, ESTIMATED
  250 bytes of code and ~220 of bss (the template, the save area, the map).
  It needs no in-place decode, and it is the wave that stops the bank
  calling `int 15h` at all on a rung-1 disk - so A20 stays as XMEM.DRV left
  it, and on such a disk L3 below is not needed.
- **L3 - XMEM.DRV's tier-1 transport - BUILT (2026-10-15, SPEC.md 41.13)**:
  LOADALL copies in 8 KB pieces, A20 probed per call, `int 15h` the
  fallback. Zero kernel bytes and zero heap (the image 1,156 -> 2,024, the
  whole-KB claim 2,048 either side). The borrow is `apps/os88la.inc`, which
  VIDDISK `L` now assembles too, so an `L` run proves the include on a 286.
  An attach-time pair of round trips against AH=87h, INT 6 hooked, decides
  per boot. `tests/xmla.py` checks every table field under unicorn;
  `vidxmsbios` runs the REFUSAL on QEMU's 386, where `0F 05` really traps -
  and caught the one defect the model could not, `xm_lainit` clobbering the
  pool's KB in BX across the attach.
- **L4 - the bank in place - BUILT (2026-10-15, SPEC.md 98.3.18.13)**: the
  ring's slots ARE the XMS bank's, `[vp_einp]` 2 riding every branch
  98.3.18.7's EMS decode takes. The bank's DRAIN - a 32 KB copy down a
  chunk - is gone; the read still goes disk -> a bounce (the ring's first
  conventional slot, placed at the board's gap, 98.3.18.12) -> copied up,
  and the bank's last slot is the MIRROR at its wrap. No "hand-over between
  ring and bank chunks" was needed: as with EMS the FIFO simply stops being
  a separate queue. The layer keeps its own claim (a flip does not carve an
  in-place ring). VIDEO.O88 +350 bytes. Gated by `vidxipbank`,
  `vidxipbankwrap` and `vidxipbanksnd`, each holding every tenth frame
  against the reference decode. With L2 this is section 5's zero-copy
  column: L2 is what would take the copy UP away too.
- **L5 - the encoder**: the copies charged as the file says the player
  makes them (section 7). VIDDISK `X`'s rows under `xg_gap answers G`
  (2026-10-15) are what to price them from: the same copies by XMEM.DRV's
  LOADALL and by `int 15h AH=87h`, 4-32 KB both ways, with the buffer at the
  gap the player places at - and at 0 apart, the old worst case - on ONE
  boot, which also re-takes the 16 KB piece decision (9.1, L3) at a fixed
  gap. TAKEN (docs/reports/XMS-TRANSPORT-VICTORY286-2026-10-15.md): a placed
  copy is ~0.28 ms + 0.173 ms a KB by either transport, the 16 KB piece
  stands, and the bank's fill models at ~763 KB/s.
- **L6 - the conventional memory L4 gives back, as more buffer** (the
  owner's, 2026-10-15; DEFERRED until the profiles and the encoder match
  real playback). In place the ring is the bank's slots and the
  conventional ring shrinks from up to 15 slots to two and a mirror; that
  memory is not needed to PLAY THROUGH any more, but it is still depth. The
  shape to cost first: a ring whose slots are a TABLE - the bank's in XMS
  and the conventional ones the copying ring would have had - so chunk c's
  slot says which memory it is in; a conventional slot decodes with no
  borrow at all (cheaper than in place), an XMS one through it. Questions
  to answer before building: does the reader fill the conventional slots
  straight off the disk (no bounce, no copy up - the cheapest chunks in the
  ring), and does the mirror rule survive a ring whose consecutive slots
  are in two memories (a super-packet crossing from an XMS slot into a
  conventional one is not contiguous in either - it wants a mirror per
  memory boundary, or the table ordered so the crossing never happens).

- **L7 - BIGSP in the bank in place** (9.2.1's third item; NOT STARTED,
  the smallest of these): `[vp_kr]` = the bank's slots less `[vp_msl]`, and
  `vp_xbread` mirrors slots 0 and 1 into kr and kr + 1 as the conventional
  ring does; `vp_xbipq` stops refusing `[vp_msl]` 2. Gate: `vidxip`'s
  `bankwrap` arm on a BIGSP clip (`spcap` past SP_MAX), the mirror's second
  slot broken on purpose. A held file in place (L1) needs nothing - it
  already plays BIGSP.
- **L8 - Repeat in place** (9.2.1's second item; NOT STARTED): `vp_warm`'s
  seam read stops going through the conventional ring. For the HOLD, the
  loop point is already in XMS - the warm read becomes the reader's cursor
  and `[vp_xslo]` noting the offset, nearly free; for the BANK, the seam's
  chunk goes through the bounce and a copy up like any other, the mirror
  with it. Then `vp_xipq` and `vp_xbipq` stop refusing `[vp_rep]`. Gate:
  `vidxms`'s `liverep` shape - two whole laps past the first - in place,
  every lap's seam held against the decode.
- **L9 - the speaker's clock in place** (9.2.1's first item; NOT STARTED,
  the largest): a sample ISR that loads no DS (ES carries the sample ring -
  a borrow leaves ES's cache its own selector x 16, so a `pop es` restores
  it exactly), every other IRQ masked at the PIC for the length of a
  borrow, and the period's chain to the kernel's IRQ0 deferred to the next
  yield - so IRQ0 alone runs inside a borrow and loses no sample. Both the
  decode's borrows and XMEM.DRV's copy pieces need it, so it reaches the
  driver too (or the speaker play keeps 98.3.18.5's no-copy rule and only
  the decode goes in place). Then the in-place paths and 98.3.18.5 stop
  refusing `VP_SPK`. Gate: `vidspk`-shaped, on QEMU's speaker under LA_SIM -
  the samples counted against the clip's, none lost.

All three are "not blocked, just more work" (the owner, 2026-10-15), and
none is scheduled ahead of the profiling and the encoder matching playback.

### 9.2.1 What keeps three plays off the in-place paths (2026-10-15)

- **The speaker's clock** (SPEC.md 98.3.18.5): its clock is IRQ0, one
  interrupt a SAMPLE - every 45-125 us - and a borrow runs with interrupts
  off, because `os88spk_isr` does `lds si` and `pop ds` and any DS load ends
  the borrow's hidden base. A decode window is up to ~5 ms (`VP_XYB`), an
  XMEM.DRV copy piece ~5 ms: 40-110 samples lost each, which plays as slow
  motion. Shrinking the windows below a sample is not possible (a borrow
  costs ~150 us). What would work: a sample ISR that loads NO DS - it can
  use ES, which a borrow does not change (its cache is its own selector x
  16, so a `pop es` restores it exactly) - and, for the length of a borrow,
  every other IRQ masked at the PIC and the period-end chain to the
  kernel's IRQ0 deferred to the next yield. Then IRQ0 alone runs inside a
  borrow and the speaker keeps time. A design, not a wall.
- **Repeat** (98.3.9): its SEAM - the loop point's record or key - is read
  by `vp_warm` with `vp_rdat` into the conventional ring's slot, and the
  next lap's chunks follow it there. In place there is no conventional
  slot: for the HOLD (L1) the loop point is already in XMS, so the warm
  read becomes noting the offset (nearly free); for the BANK (L4) it is the
  bounce and a copy up, with the mirror. Work, not a wall.
- **BIGSP** (98.1.4.1, super-packets past 32 KB): NOT refused by L1 - a
  hold is the file, contiguous. L4 refuses it because its mirror is one
  slot and BIGSP's is two (the conventional ring mirrors slots 0 and 1).
  The change is `[vp_kr]` = slots less `[vp_msl]` and the mirror copy for
  slot < `[vp_msl]` into kr + slot - small. EMS's refusal is its own: a
  BIGSP super-packet spans five pages and the hook's window maps three.

### 9.1 What L0 found on the Victory, and the order it recommends

docs/reports/LOADALL-VICTORY286-2026-10-14.md, the PicoMEM 1.x in and the
bus at 8 wait states: **LOADALL runs** with the table of section 1 and the
CS byte 93h; its caches, a read in place and a write in place all check;
**A20 stays open** after `AH=87h` on this BIOS; memory above 1 MB reads as
fast as below; a borrow costs ~0.14-0.17 ms; and the fill was not measured,
there being **no IDE drive at 1F0h** - C: is the PicoMEM's, behind the
card's own ROM.

Re-modelled with what it measured (section 5's method, the copy now 0.27 ms
a KB and the drain ~0.045), `GasGas24.V88` needs 270 -> **253** KB/s at its
own bank, 284 -> **266** at 2 MB and 449 -> **374** at 1 MB: the drain is
worth less than section 5 said, because the board's copies got cheaper, and
still worth 6-17% of disk.

**Recommended order from here:**
1. **L3 first - XMEM.DRV's tier-1 transport.** It is the one with every
   XMS user behind it (section 6), it is now measured rather than
   estimated (0.41 -> 0.14 ms fixed, 43% off a 1 KB copy), it is zero
   kernel bytes, and it is the borrow's code written once - the include
   L1 and L4 will assemble too.
2. **L1 - the hold in place.** The simplest layout to prove the decoder's
   yields on.
3. **L4 - the bank in place**, which on this BIOS does not need L3's A20
   behaviour, only its code.
4. **L5 - the encoder's copy count**, once a player honours it - and only
   for an encode the disk binds: on the owner's command line it changes
   no byte (section 7).
5. **L2 - the fill - stays parked** until a machine with an IDE drive on
   rung 1 runs VIDDISK `L`'s fill rows. On the Victory as configured its
   disk is the PicoMEM's and the fill cannot reach it at all; section 4.2's
   design stands for the machines it can.

### 9.2 Deferred: what the yield sites cost a play that never yields

**Recorded 2026-10-15, to be taken with the other optimisations near the
end of the work** (the owner's call). L1's `VD_YCK` sites are a taken
`jmp short` over ten bytes of live check on every play that is not in place
- which is every play on an 8088, a 386, and a 286 copying. Measured on the
Victory 286 (VIDBENCH at 8 wait states, the decode-to-RAM rows - the ones the
bus cannot move - against the same rows before L1):

| frame | before | after | cost |
|---|---:|---:|---:|
| real frames (SYNTH 1/6/7/19, P2-P6, SL, RUN16) | | | **+0.2 to +1.2%** |
| `RUN40 x60` | 438.7 us | 447.1 | +1.9% |
| `P1 row` | 171.8 | 178.1 | +3.7% |
| `P1 sparse` | 85.5 | 92.8 | +8.6% |
| `S empty` | 40.9 | 47.1 | +6.2 us, +15% |

Twice what MartyPC's 5150 measured (+0.34-0.70%): a taken jump empties the
286's prefetch queue, and everything around it got cheaper. Small frames pay
the most because their cost is mostly fixed and every list head is a site.
The options, none taken yet:

1. **Out of line - BUILT, MEASURED and NOT TAKEN (the owner, 2026-10-16;
   docs/reports/VIDEO-VDYCK-2026-10-16.md, the patch beside it as
   `VIDEO-VDYCK-2026-10-16.patch`).** As a 3-byte `test ax, rel16` patched to `call
   vd_yield` it saved 258 bytes and ~6 cycles a site on an 8088 (35-90 a
   frame, MEASURED), and cost a 286 decoding in place ~13 clocks a site
   (0.25-0.6% of a real frame, PREDICTED). The owner keeps the 286's
   in-place play as it is. The sketch as first written: a site becomes three bytes - three NOPs off, a `call
   vd_ychk` on - so a play that never yields falls through instead of
   jumping (9 clocks a site on a 286, against 7 + the next instruction's
   length for the jump, and ~3 fewer on an 8088's fetch) and the twenty
   sites shed ~140 bytes of inline check. The on-cost rises by a call/ret,
   paid only on an in-place play, where it is noise beside the copy it
   replaced.
2. **Fewer sites.** The list heads are what an empty or sparse frame
   meets; a site per span segment and per long span may be enough with
   `VP_XYB` lowered to keep the window's bound.
3. **Two decoders** - refused unless 1 and 2 leave something worth it:
   the decoder is the package's largest body.

## 10. Open questions

1. **Where the instruction lives** - ANSWERED (2026-10-14): the copy in
   XMEM.DRV for every XMS user, the in-place window as shared source in the
   two hot loops that can use it (section 6).
2. **Who vouches for the fill's target** - ANSWERED (2026-10-14): the
   package's word. Packages are trusted and all of them are in this tree
   (section 6).
3. **Does the Victory's BIOS close A20 after `AH=87h`?** - ANSWERED: no,
   it stays open (section 9.1). Other BIOSes still have to be asked.
4. **Does the next sector's DRQ come inside a borrow?** - NOT MEASURED: the
   Victory's C: is the PicoMEM's disk, and VIDDISK found no IDE drive at
   1F0h (section 9.1). It wants a machine with one. If the drive hands sectors over one at a time
   with gaps, the fill's borrow is per sector, saves ~50 us of the 172 a
   copy costs, and L2 is worth a third of what section 5 prices.
5. **The CS cache access byte** - ANSWERED: 93h runs on the Victory's AMD
   286.
6. **Is memory above 1 MB slower on the Victory than below it?** -
   ANSWERED: no (4,508 us against 4,353 for 32 KB, the difference one
   borrow).
7. **Does a masked window cost the PicoMEM's Sound Blaster anything?** Its
   block handler runs on the host (section 3.3), so L0's rows want taking
   with the card playing, and the window bound checked against what it
   hears.

## 11. Considered and not proposed

- **The stream through SS.** No ISR here loads SS, so an SS cache would
  survive an interrupt where DS's does not - but interrupts PUSH through it,
  and two ISRs swap SP to fixed offsets of private stacks (SPEC.md 8.5,
  9.10), so their frames would land inside the bank at those offsets.
- **Decoding with interrupts on and detecting the loss.** A lost base reads
  conventional memory and draws a wrong frame; a delta codec keeps it until
  the next key frame, and "did any ISR run" has no cheap exact answer.
- **The 386's LOADALL (`0F 07`).** Unreal mode already gives tier 2 the
  copy at 0.05 ms a KB, and the request is 286 only.
- **A 32-bit destination threaded through the file layer** for the fill.
  READ_SEQ, READ_AT, `dsk_xfer` and `DSV_BLK` all carry a segment; arming
  the driver with the bounce slot's segment and a map of what it redirected
  needs none of them changed (section 4.2).
- **Rung 0 into XMS** - section 4.2's last paragraph.
- **Keeping the table planted between windows** - section 2.
